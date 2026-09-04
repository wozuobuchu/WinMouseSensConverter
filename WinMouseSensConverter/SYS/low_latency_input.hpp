#pragma once

#ifndef LOW_LATENCY_INPUT_HPP_
#define LOW_LATENCY_INPUT_HPP_

#include <Windows.h>
#include <array>
#include <atomic>
#include <bit>
#include <cstdint>
#include <future>
#include <thread>
#include <utility>

#include <boost/lockfree/spsc_queue.hpp>

namespace rawinput {

    class LowLatencyInputLifetimeGuard;

    class LowLatencyInput final {
    public:
        struct KeyEvent {
            uint16_t vkey = 0;
            uint16_t scancode = 0;
            uint16_t flags = 0;
            uint16_t down = 0;
        };

        inline static constexpr size_t kQueueCapacity = 2048;

        // Batch-only consumer API; the queue still requires a single consumer thread.
        template <std::size_t N>
        inline static size_t pop_events(KeyEvent (&outs)[N]) noexcept requires (N <= kQueueCapacity) {
            return queue_.pop(outs, N);
        }

        // Returns a lock-free snapshot of the latest processed key state.
        inline static bool is_keydown(uint16_t vkey) noexcept {
            if (vkey >= 256) return false;
            return key_down_[vkey].load(std::memory_order_relaxed) != 0;
        }

        // Read both axes from the same atomic snapshot
        inline static std::pair<int32_t, int32_t> sample() noexcept {
            const uint64_t packed = packed_movement_.exchange(0, std::memory_order_relaxed);
            return {
                std::bit_cast<int32_t>(static_cast<uint32_t>(packed)),
                std::bit_cast<int32_t>(static_cast<uint32_t>(packed >> 32))
            };
        }

    private:
        friend class LowLatencyInputLifetimeGuard;

        struct MouseButtonMapping {
            USHORT down_flag = 0;
            USHORT up_flag = 0;
            uint16_t vkey = 0;
        };

        // Larger bursts are handled by repeatedly draining this fixed-size buffer.
        inline static constexpr size_t kRawInputBatchCapacity = 64;
        inline static constexpr DWORD kRawInputBatchIntervalMs = 1;
        inline static constexpr DWORD kMessageLoopRetryDelayMs = 1;
        inline static constexpr DWORD kControlWakeMask = QS_ALLINPUT & ~static_cast<DWORD>(QS_RAWINPUT);
        inline static constexpr std::array<MouseButtonMapping, 5> kMouseButtonMappings{{
            {RI_MOUSE_LEFT_BUTTON_DOWN, RI_MOUSE_LEFT_BUTTON_UP, VK_LBUTTON},
            {RI_MOUSE_RIGHT_BUTTON_DOWN, RI_MOUSE_RIGHT_BUTTON_UP, VK_RBUTTON},
            {RI_MOUSE_MIDDLE_BUTTON_DOWN, RI_MOUSE_MIDDLE_BUTTON_UP, VK_MBUTTON},
            {RI_MOUSE_BUTTON_4_DOWN, RI_MOUSE_BUTTON_4_UP, VK_XBUTTON1},
            {RI_MOUSE_BUTTON_5_DOWN, RI_MOUSE_BUTTON_5_UP, VK_XBUTTON2},
        }};

        LowLatencyInput() = delete;

        // One-shot startup; readiness is reported only after Raw Input registration succeeds.
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

        // One-shot shutdown; WM_QUIT wakes the dedicated message thread before joining it.
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

        // Split generic modifier keys into their left/right virtual-key variants.
        inline static uint16_t normalize_vkey(const RAWKEYBOARD& keyboard) noexcept {
            uint16_t vkey = static_cast<uint16_t>(keyboard.VKey);
            const uint16_t flags = static_cast<uint16_t>(keyboard.Flags);

            if (vkey == VK_SHIFT) {
                vkey = (keyboard.MakeCode == 0x36) ? VK_RSHIFT : VK_LSHIFT;
            } else if (vkey == VK_CONTROL) {
                vkey = (flags & RI_KEY_E0) ? VK_RCONTROL : VK_LCONTROL;
            } else if (vkey == VK_MENU) {
                vkey = (flags & RI_KEY_E0) ? VK_RMENU : VK_LMENU;
            }

            return vkey;
        }

        inline static void push_key_event(uint16_t vkey, uint16_t scancode, uint16_t flags, uint16_t down) noexcept {
            if (vkey >= 256) return;

            const uint8_t old_down = key_down_[vkey].load(std::memory_order_relaxed);
            const uint8_t new_down = down != 0 ? 1 : 0;

            // Suppress hardware/OS repeats while preserving the latest key state.
            if (old_down == new_down) return;

            key_down_[vkey].store(new_down, std::memory_order_relaxed);

            const KeyEvent event{
                vkey,
                scancode,
                flags,
                new_down
            };

            // A full queue drops the event, but key_down_ remains up to date.
            const bool pushed = queue_.push(event);
            (void)pushed;
        }

        inline static void process_keyboard(const RAWKEYBOARD& keyboard) noexcept {
            if (keyboard.VKey == 255) return;

            const uint16_t vkey = normalize_vkey(keyboard);
            const uint16_t flags = static_cast<uint16_t>(keyboard.Flags);
            const uint16_t down = (flags & RI_KEY_BREAK) ? 0 : 1;

            push_key_event(
                vkey,
                static_cast<uint16_t>(keyboard.MakeCode),
                flags,
                down
            );
        }

        inline static void process_mouse(const RAWMOUSE& mouse) noexcept {
            const USHORT button_flags = mouse.usButtonFlags;
            for (const MouseButtonMapping& mapping : kMouseButtonMappings) {
                if ((button_flags & mapping.down_flag) != 0) {
                    push_key_event(mapping.vkey, 0, 0, 1);
                }
                if ((button_flags & mapping.up_flag) != 0) {
                    push_key_event(mapping.vkey, 0, RI_KEY_BREAK, 0);
                }
            }

            if ((mouse.usFlags & MOUSE_MOVE_ABSOLUTE) != 0) return;
            if (mouse.lLastX == 0 && mouse.lLastY == 0) return;

            int32_t delta_x = static_cast<int32_t>(mouse.lLastX);
            int32_t delta_y = static_cast<int32_t>(mouse.lLastY);
            uint64_t expected = packed_movement_.load(std::memory_order_relaxed);

            // CAS loop to accumulate mouse movement
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
        }

        inline static void process_raw_input(const RAWINPUT& raw_input) noexcept {
            if (raw_input.header.dwType == RIM_TYPEKEYBOARD) {
                process_keyboard(raw_input.data.keyboard);
            } else if (raw_input.header.dwType == RIM_TYPEMOUSE) {
                process_mouse(raw_input.data.mouse);
            }
        }

        // Drain every queued RAWINPUT block into one fixed-size aligned buffer.
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

            return false;
        }

        inline static void message_thread_proc(std::promise<bool> ready) noexcept {
            (void)SetThreadDescription(GetCurrentThread(), L"THREAD_RawInput");

            const HINSTANCE instance = GetModuleHandleW(nullptr);
            constexpr const wchar_t* class_name = L"LowLatencyInputBufferedMessageWindow";

            WNDCLASSW window_class{};
            // The window is only a Raw Input target; WM_INPUT is never dispatched to it.
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

            std::array<RAWINPUTDEVICE, 2> devices{{
                {0x01, 0x02, RIDEV_INPUTSINK, hwnd},
                {0x01, 0x06, RIDEV_INPUTSINK, hwnd},
            }};

            // Keep legacy keyboard and mouse messages while receiving background Raw Input.
            if (!RegisterRawInputDevices(devices.data(), static_cast<UINT>(devices.size()), sizeof(RAWINPUTDEVICE))) {
                DestroyWindow(hwnd);
                ready.set_value(false);
                return;
            }

            // Publish the thread ID only after its message queue and both registrations are ready.
            message_thread_id_.store(GetCurrentThreadId(), std::memory_order_release);
            ready.set_value(true);

            // Eight-byte base alignment also satisfies the WOW64 buffered-input requirement.
            alignas(8) std::array<RAWINPUT, kRawInputBatchCapacity> buffer{};
            MSG message{};
            bool running = true;

            while (running) {
                // Exclude Raw Input from the wake mask so reports accumulate until the 1 ms timeout.
                const DWORD wait_result = MsgWaitForMultipleObjectsEx(
                    0,
                    nullptr,
                    kRawInputBatchIntervalMs,
                    kControlWakeMask,
                    MWMO_INPUTAVAILABLE
                );
                bool retry_needed = wait_result == WAIT_FAILED;
                if (!retry_needed && !drain_raw_input_buffer(buffer)) {
                    retry_needed = true;
                }

                // Dispatch control messages while deliberately leaving WM_INPUT to the buffer API.
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

            // Stop accepting shutdown posts before unregistering and destroying the target window.
            message_thread_id_.store(0, std::memory_order_release);

            for (RAWINPUTDEVICE& device : devices) {
                device.dwFlags = RIDEV_REMOVE;
                device.hwndTarget = nullptr;
            }
            RegisterRawInputDevices(devices.data(), static_cast<UINT>(devices.size()), sizeof(RAWINPUTDEVICE));

            DestroyWindow(hwnd);
        }

        inline static std::thread message_thread_{};
        inline static std::atomic<DWORD> message_thread_id_{0};

        // The message thread is the sole producer; callers must provide one consumer.
        inline static boost::lockfree::spsc_queue<KeyEvent, boost::lockfree::capacity<kQueueCapacity>> queue_{};
        inline static std::array<std::atomic<uint8_t>, 256> key_down_{};
        inline static std::atomic<uint64_t> packed_movement_{0};
    };

    class LowLatencyInputLifetimeGuard final {
    public:
        // Start automatically during static initialization.
        LowLatencyInputLifetimeGuard() noexcept {
            static bool init = []() -> bool {
                (void)LowLatencyInput::start_message_thread();
                return true;
            }();
            (void)init;
        }

        // Stop automatically before static thread storage is destroyed.
        ~LowLatencyInputLifetimeGuard() {
            static bool stop = []() -> bool {
                (void)LowLatencyInput::stop_message_thread();
                return true;
            }();
            (void)stop;
        }
    };

    inline LowLatencyInputLifetimeGuard input_lifetime_guard;

} // namespace rawinput

#endif // LOW_LATENCY_INPUT_HPP_
