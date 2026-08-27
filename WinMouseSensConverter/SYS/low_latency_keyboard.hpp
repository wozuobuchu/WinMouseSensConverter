#pragma once

#ifndef LOW_LATENCY_KEYBOARD_HPP_
#define LOW_LATENCY_KEYBOARD_HPP_

#include <Windows.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <future>
#include <thread>

#include <boost/lockfree/spsc_queue.hpp>

namespace rawinput {

    class LowLatencyKeyboardLifetimeGuard;

    class LowLatencyKeyboard final {
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

    private:
        friend class LowLatencyKeyboardLifetimeGuard;

        // Larger bursts are handled by repeatedly draining this fixed-size buffer.
        inline static constexpr size_t kRawInputBatchCapacity = 64;

        LowLatencyKeyboard() = delete;

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

        inline static void process_raw_input(const RAWINPUT& raw_input) noexcept {
            if (raw_input.header.dwType != RIM_TYPEKEYBOARD) return;

            const RAWKEYBOARD& keyboard = raw_input.data.keyboard;
            if (keyboard.VKey == 255) return;

            const uint16_t vkey = normalize_vkey(keyboard);
            if (vkey >= 256) return;

            const uint16_t flags = static_cast<uint16_t>(keyboard.Flags);
            const uint8_t old_down = key_down_[vkey].load(std::memory_order_relaxed);
            const uint8_t new_down = (flags & RI_KEY_BREAK) ? 0 : 1;

            // Suppress hardware/OS repeats while preserving the latest key state.
            if (old_down == new_down) return;

            key_down_[vkey].store(new_down, std::memory_order_relaxed);

            const KeyEvent event{
                vkey,
                static_cast<uint16_t>(keyboard.MakeCode),
                flags,
                new_down
            };

            // A full queue drops the event, but key_down_ remains up to date.
            const bool pushed = queue_.push(event);
            (void)pushed;
        }

        // Drain every queued RAWINPUT block
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
            constexpr const wchar_t* class_name = L"LowLatencyKeyboardBufferedMessageWindow";

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

            RAWINPUTDEVICE device{};
            device.usUsagePage = 0x01;
            device.usUsage = 0x06;
            // Keep legacy keyboard messages while receiving background Raw Input.
            device.dwFlags = RIDEV_INPUTSINK;
            device.hwndTarget = hwnd;

            if (!RegisterRawInputDevices(&device, 1, sizeof(device))) {
                DestroyWindow(hwnd);
                ready.set_value(false);
                return;
            }

            // Publish the thread ID only after its message queue and registration are ready.
            message_thread_id_.store(GetCurrentThreadId(), std::memory_order_release);
            ready.set_value(true);

            // Eight-byte base alignment also satisfies the WOW64 buffered-input requirement.
            alignas(8) std::array<RAWINPUT, kRawInputBatchCapacity> buffer{};
            MSG message{};
            bool running = true;

            while (running) {
                // Wake for queued input without removing the pending WM_INPUT messages.
                const DWORD wait_result = MsgWaitForMultipleObjectsEx(
                    0,
                    nullptr,
                    INFINITE,
                    QS_ALLINPUT,
                    MWMO_INPUTAVAILABLE
                );
                if (wait_result == WAIT_FAILED) break;
                if (!drain_raw_input_buffer(buffer)) break;

                // Dispatch control messages while deliberately leaving WM_INPUT to the buffer API.
                while (PeekMessageW(&message, nullptr, 0, WM_INPUT - 1, PM_REMOVE) || PeekMessageW(&message, nullptr, WM_INPUT + 1, 0xFFFF, PM_REMOVE)) {
                    if (message.message == WM_QUIT) {
                        running = false;
                        break;
                    }
                    DispatchMessageW(&message);
                }
            }

            // Stop accepting shutdown posts before unregistering and destroying the target window.
            message_thread_id_.store(0, std::memory_order_release);

            RAWINPUTDEVICE remove_device{};
            remove_device.usUsagePage = 0x01;
            remove_device.usUsage = 0x06;
            remove_device.dwFlags = RIDEV_REMOVE;
            remove_device.hwndTarget = nullptr;
            RegisterRawInputDevices(&remove_device, 1, sizeof(remove_device));

            DestroyWindow(hwnd);
        }

        inline static std::thread message_thread_{};
        inline static std::atomic<DWORD> message_thread_id_{0};

        // The message thread is the sole producer; callers must provide one consumer.
        inline static boost::lockfree::spsc_queue<KeyEvent, boost::lockfree::capacity<kQueueCapacity>> queue_{};
        inline static std::array<std::atomic<uint8_t>, 256> key_down_{};
    };

    class LowLatencyKeyboardLifetimeGuard final {
    public:
        // Start automatically during static initialization.
        LowLatencyKeyboardLifetimeGuard() noexcept {
            static bool init = []() -> bool {
                (void)LowLatencyKeyboard::start_message_thread();
                return true;
            }();
            (void)init;
        }

        // Stop automatically before static thread storage is destroyed.
        ~LowLatencyKeyboardLifetimeGuard() {
            static bool stop = []() -> bool {
                (void)LowLatencyKeyboard::stop_message_thread();
                return true;
            }();
            (void)stop;
        }
    };

    inline LowLatencyKeyboardLifetimeGuard keyboard_lifetime_guard;

} // namespace rawinput

#endif // LOW_LATENCY_KEYBOARD_HPP_
