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

        friend struct InputBatchTestAccess;

        // Owned by the message thread, spanning every buffer read in one tick.
        struct MovementBatch {
            uint32_t x = 0;
            uint32_t y = 0;
        };

        template <typename MovementAtomic>
        inline static void publish_movement_to(MovementBatch& pending, MovementAtomic& movement) noexcept {
            if (pending.x == 0 && pending.y == 0) return;

            uint64_t expected = movement.load(std::memory_order_relaxed);
            while (true) {
                // Add axes separately so modulo-2^32 wrap never carries into Y.
                const uint32_t new_x = static_cast<uint32_t>(expected) + pending.x;
                const uint32_t new_y = static_cast<uint32_t>(expected >> 32) + pending.y;
                const uint64_t desired = static_cast<uint64_t>(new_x) | (static_cast<uint64_t>(new_y) << 32);
                if (movement.compare_exchange_weak(expected, desired, std::memory_order_relaxed, std::memory_order_relaxed)) {
                    pending = {};
                    return;
                }
            }
        }

        inline static void publish_movement(MovementBatch& pending) noexcept {
            publish_movement_to(pending, packed_movement_);
        }

        inline static constexpr size_t kRawInputBatchCapacity = 64;
        inline static constexpr DWORD kRawInputBatchIntervalMs = 1;
        inline static constexpr DWORD kMessageLoopRetryDelayMs = 1;
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

        inline static void process_raw_input(const RAWINPUT& raw_input, MovementBatch& pending) noexcept {

            if (raw_input.header.dwType != RIM_TYPEMOUSE) return;

            const RAWMOUSE& mouse = raw_input.data.mouse;
            if ((mouse.usFlags & MOUSE_MOVE_ABSOLUTE) != 0) return;
            if (mouse.lLastX == 0 && mouse.lLastY == 0) return;

            pending.x += static_cast<uint32_t>(mouse.lLastX);
            pending.y += static_cast<uint32_t>(mouse.lLastY);
        }

        // Drain all queued mouse packets into a fixed-size aligned buffer.
        template <typename ReadBuffer>
        inline static bool drain_raw_input_buffer(std::array<RAWINPUT, kRawInputBatchCapacity>& buffer,
            MovementBatch& pending, ReadBuffer& read_buffer) noexcept {
            using QWORD = ULONGLONG; // Required by the x64 RAWINPUT_ALIGN macro.

            while (true) {
                UINT buffer_size = static_cast<UINT>(sizeof(buffer));
                const UINT input_count = read_buffer(
                    buffer.data(),
                    &buffer_size,
                    sizeof(RAWINPUTHEADER)
                );

                if (input_count == 0) return true;
                if (input_count == static_cast<UINT>(-1)) return false;

                PRAWINPUT current = buffer.data();
                for (UINT index = 0; index < input_count; ++index) {
                    process_raw_input(*current, pending);
                    current = NEXTRAWINPUTBLOCK(current);
                }
            }

            return false;
        }

        // Defaults read Win32 input and publish to the shared movement accumulator.
        template <typename ReadBuffer = decltype(&GetRawInputBuffer), typename PublishMovement = decltype(&publish_movement)>
        inline static void run_message_loop(ReadBuffer read_buffer = &GetRawInputBuffer,
            PublishMovement publish = &publish_movement) noexcept {
            alignas(8) std::array<RAWINPUT, kRawInputBatchCapacity> buffer{};
            MovementBatch pending{};
            MSG message{};
            bool running = true;

            while (running) {
                // Publish the previous tick before waiting; never publish from packet processing.
                publish(pending);
                // Exclude Raw Input from the wake mask so high-rate reports accumulate until the 1 ms timeout.
                const DWORD wait_result = MsgWaitForMultipleObjectsEx(
                    0,
                    nullptr,
                    kRawInputBatchIntervalMs,
                    kControlWakeMask,
                    MWMO_INPUTAVAILABLE
                );
                bool retry_needed = wait_result == WAIT_FAILED;
                if (!retry_needed && !drain_raw_input_buffer(buffer, pending, read_buffer)) {
                    retry_needed = true;
                }

                // Leave WM_INPUT for the buffered API and dispatch control messages.
                while (PeekMessageW(&message, nullptr, 0, WM_INPUT - 1, PM_REMOVE) || PeekMessageW(&message, nullptr, WM_INPUT + 1, 0xFFFF, PM_REMOVE)) {
                    if (message.message == WM_QUIT) {
                        running = false;
                        break;
                    }
                    DispatchMessageW(&message);
                }

                if (retry_needed && running) {
                    Sleep(kMessageLoopRetryDelayMs);
                }
            }
            // WM_QUIT deliberately discards the final unpublished local batch.
        }

        inline static void message_thread_proc(std::promise<bool> ready) noexcept {
            (void)SetThreadDescription(GetCurrentThread(), L"THREAD_MouseRawInput");

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

            run_message_loop();

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
        // The application entry point owns the one-shot input lifetime.
        LowLatencyMouseMovLifetimeGuard() noexcept
            : started_(LowLatencyMouseMov::start_message_thread()) {}

        LowLatencyMouseMovLifetimeGuard(const LowLatencyMouseMovLifetimeGuard&) = delete;
        LowLatencyMouseMovLifetimeGuard& operator=(const LowLatencyMouseMovLifetimeGuard&) = delete;

        [[nodiscard]] bool started() const noexcept { return started_; }

        // Stop automatically before static thread storage is destroyed.
        ~LowLatencyMouseMovLifetimeGuard() {
            (void)LowLatencyMouseMov::stop_message_thread();
        }

    private:
        const bool started_;
    };

} // namespace rawinput

#endif // LOW_LATENCY_MOUSEMOV_HPP_
