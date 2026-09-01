#pragma once

#ifndef LOW_LATENCY_MOUSEMOV_HPP_
#define LOW_LATENCY_MOUSEMOV_HPP_

#include <Windows.h>
#include <array>
#include <atomic>
#include <bit>
#include <cstdint>
#include <future>
#include <thread>
#include <utility>

namespace rawinput {

    class LowLatencyMouseMovLifetimeGuard;

    class LowLatencyMouseMov final {
    public:
        // Read both axes from the same atomic snapshot
        inline static std::pair<int32_t, int32_t> sample() noexcept {
            const uint64_t packed = packed_movement_.exchange(0, std::memory_order_relaxed);
            return {
                std::bit_cast<int32_t>(static_cast<uint32_t>(packed)),
                std::bit_cast<int32_t>(static_cast<uint32_t>(packed >> 32))
            };
        }

    private:
        friend class LowLatencyMouseMovLifetimeGuard;

        inline static constexpr size_t kRawInputBatchCapacity = 64;
        inline static constexpr DWORD kRawInputBatchIntervalMs = 1;
        inline static constexpr DWORD kControlWakeMask = QS_ALLINPUT & ~static_cast<DWORD>(QS_RAWINPUT);

        LowLatencyMouseMov() = delete;

        // One-shot startup; readiness is reported after Raw Input registration.
        inline static bool start_message_thread() noexcept {
            static bool init = []() -> bool {
                try {
                    std::promise<bool> ready_promise;
                    auto ready_future = ready_promise.get_future();

                    message_thread_ = std::thread(
                        [promise = std::move(ready_promise)]() mutable noexcept -> void {
                            message_thread_proc(std::move(promise));
                        }
                    );

                    const bool success = ready_future.get();
                    if (!success && message_thread_.joinable()) {
                        message_thread_.join();
                    }
                    return success;
                } catch (...) {
                    if (message_thread_.joinable()) {
                        message_thread_.join();
                    }
                    return false;
                }
            }();
            return init;
        }

        // One-shot shutdown; WM_QUIT wakes the dedicated message thread.
        inline static bool stop_message_thread() noexcept {
            static bool stop = []() -> bool {
                if (!message_thread_.joinable()) return false;

                const DWORD thread_id = message_thread_id_.load(std::memory_order_acquire);
                if (thread_id != 0) {
                    PostThreadMessageW(thread_id, WM_QUIT, 0, 0);
                }

                message_thread_.join();
                return true;
            }();
            return stop;
        }

        inline static void process_raw_input(const RAWINPUT& raw_input) noexcept {

            constexpr auto cas_mov = [](int32_t delta_x, int32_t delta_y) noexcept -> void {
                uint64_t expected = packed_movement_.load(std::memory_order_relaxed);

                while (true) {
                    const uint32_t old_x = static_cast<uint32_t>(expected);
                    const uint32_t old_y = static_cast<uint32_t>(expected >> 32);
                    const uint32_t new_x = old_x + static_cast<uint32_t>(delta_x);
                    const uint32_t new_y = old_y + static_cast<uint32_t>(delta_y);
                    const uint64_t desired = static_cast<uint64_t>(new_x) | (static_cast<uint64_t>(new_y) << 32);

                    if (packed_movement_.compare_exchange_weak(expected, desired, std::memory_order_relaxed, std::memory_order_relaxed)) {
                        return;
                    }
                }
            };

            if (raw_input.header.dwType != RIM_TYPEMOUSE) return;

            const RAWMOUSE& mouse = raw_input.data.mouse;
            if ((mouse.usFlags & MOUSE_MOVE_ABSOLUTE) != 0) return;
            if (mouse.lLastX == 0 && mouse.lLastY == 0) return;

            cas_mov(static_cast<int32_t>(mouse.lLastX), static_cast<int32_t>(mouse.lLastY));
        }

        // Drain all queued mouse packets into a fixed-size aligned buffer.
        inline static bool drain_raw_input_buffer(std::array<RAWINPUT, kRawInputBatchCapacity>& buffer) noexcept {
            using QWORD = ULONGLONG; // Required by the x64 RAWINPUT_ALIGN macro.

            while (true) {
                UINT buffer_size = static_cast<UINT>(sizeof(buffer));
                const UINT input_count = GetRawInputBuffer(
                    buffer.data(),
                    &buffer_size,
                    sizeof(RAWINPUTHEADER)
                );

                if (input_count == 0) return true;
                if (input_count == static_cast<UINT>(-1)) return false;

                PRAWINPUT current = buffer.data();
                for (UINT index = 0; index < input_count; ++index) {
                    process_raw_input(*current);
                    current = NEXTRAWINPUTBLOCK(current);
                }
            }
        }

        inline static void message_thread_proc(std::promise<bool> ready) noexcept {
            const HINSTANCE instance = GetModuleHandleW(nullptr);
            constexpr const wchar_t* class_name = L"LowLatencyMouseMovBufferedMessageWindow";

            WNDCLASSW window_class{};
            window_class.lpfnWndProc = DefWindowProcW;
            window_class.hInstance = instance;
            window_class.lpszClassName = class_name;

            if (!RegisterClassW(&window_class) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
                ready.set_value(false);
                return;
            }

            const HWND hwnd = CreateWindowExW(
                0,
                class_name,
                L"",
                0,
                0, 0, 0, 0,
                HWND_MESSAGE,
                nullptr,
                instance,
                nullptr
            );
            if (!hwnd) {
                ready.set_value(false);
                return;
            }

            RAWINPUTDEVICE device{};
            device.usUsagePage = 0x01;
            device.usUsage = 0x02;
            // Keep legacy mouse messages while receiving background Raw Input.
            device.dwFlags = RIDEV_INPUTSINK;
            device.hwndTarget = hwnd;

            if (!RegisterRawInputDevices(&device, 1, sizeof(device))) {
                DestroyWindow(hwnd);
                ready.set_value(false);
                return;
            }

            message_thread_id_.store(GetCurrentThreadId(), std::memory_order_release);
            ready.set_value(true);

            alignas(8) std::array<RAWINPUT, kRawInputBatchCapacity> buffer{};
            MSG message{};
            bool running = true;

            while (running) {
                // Exclude Raw Input from the wake mask so high-rate reports accumulate until the 1 ms timeout.
                const DWORD wait_result = MsgWaitForMultipleObjectsEx(
                    0,
                    nullptr,
                    kRawInputBatchIntervalMs,
                    kControlWakeMask,
                    MWMO_INPUTAVAILABLE
                );
                if (wait_result == WAIT_FAILED) break;
                if (!drain_raw_input_buffer(buffer)) break;

                // Leave WM_INPUT for the buffered API and dispatch control messages.
                while (PeekMessageW(&message, nullptr, 0, WM_INPUT - 1, PM_REMOVE) || PeekMessageW(&message, nullptr, WM_INPUT + 1, 0xFFFF, PM_REMOVE)) {
                    if (message.message == WM_QUIT) {
                        running = false;
                        break;
                    }
                    DispatchMessageW(&message);
                }
            }

            message_thread_id_.store(0, std::memory_order_release);

            RAWINPUTDEVICE remove_device{};
            remove_device.usUsagePage = 0x01;
            remove_device.usUsage = 0x02;
            remove_device.dwFlags = RIDEV_REMOVE;
            remove_device.hwndTarget = nullptr;
            RegisterRawInputDevices(&remove_device, 1, sizeof(remove_device));

            DestroyWindow(hwnd);
        }

        inline static std::thread message_thread_{};
        inline static std::atomic<DWORD> message_thread_id_{0};

        inline static std::atomic<uint64_t> packed_movement_{0};
    };

    class LowLatencyMouseMovLifetimeGuard final {
    public:
        // Start automatically during static initialization.
        LowLatencyMouseMovLifetimeGuard() noexcept {
            static bool init = []() -> bool {
                (void)LowLatencyMouseMov::start_message_thread();
                return true;
            }();
            (void)init;
        }

        // Stop before static thread storage is destroyed.
        ~LowLatencyMouseMovLifetimeGuard() {
            static bool stop = []() -> bool {
                (void)LowLatencyMouseMov::stop_message_thread();
                return true;
            }();
            (void)stop;
        }
    };

    inline LowLatencyMouseMovLifetimeGuard mousemov_lifetime_guard;

} // namespace rawinput

#endif // LOW_LATENCY_MOUSEMOV_HPP_
