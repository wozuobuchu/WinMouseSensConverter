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

    class LowLatencyKeyboard final {
    public:
        struct KeyEvent {
            uint16_t vkey = 0;
            uint16_t scancode = 0;
            uint16_t flags = 0;
            uint16_t down = 0;
        };

        inline static constexpr size_t kQueueCapacity = 2048;

        inline static bool start_message_thread() {
            static bool init = [] () -> bool {
                std::promise<bool> ready_promise;
                auto ready_future = ready_promise.get_future();
                try {
                    message_thread_ = std::thread(
                        [promise = std::move(ready_promise)] () mutable ->void {
                            message_thread_proc(std::move(promise));
                        }
                    );
                } catch (...) {
                    return false;
                }

                const bool success = ready_future.get();
                if (!success) {
                    if (message_thread_.joinable()) message_thread_.join();
                    return false;
                }

                return true;
            } ();
            return init;
        }

        inline static void stop_message_thread() noexcept {
            static bool stop = [] () ->bool {
                if (!message_thread_.joinable()) return false;

                HWND hwnd = message_hwnd_.load(std::memory_order_acquire);
                if (hwnd) PostMessageW(hwnd, WM_CLOSE, 0, 0);

                message_thread_.join();

                return true;
            } ();
        }

        inline static bool pop_event(KeyEvent& out) noexcept {
            return queue_.pop(out);
        }

        template <std::size_t N>
        inline static size_t pop_events(KeyEvent (&outs)[N]) noexcept requires (N == kQueueCapacity) {
            size_t cnt = 0;
            for (; cnt < kQueueCapacity; ++cnt) {
                if (!queue_.pop(outs[cnt])) break;
            }
            return cnt;
        }

        inline static bool is_keydown(uint16_t vkey) noexcept {
            if (vkey >= 256) return false;
            return key_down_[vkey].load(std::memory_order_relaxed);
        }

    private:
        friend class LowLatencyKeyboardDestructorGuard;

        LowLatencyKeyboard() = default;
        LowLatencyKeyboard(const LowLatencyKeyboard&) = delete;
        LowLatencyKeyboard& operator=(const LowLatencyKeyboard&) = delete;
        LowLatencyKeyboard(LowLatencyKeyboard&&) = delete;
        LowLatencyKeyboard& operator=(LowLatencyKeyboard&&) = delete;
        ~LowLatencyKeyboard() { stop_message_thread(); }

        inline static LRESULT CALLBACK keyboard_wnd_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
            (void)hwnd, (void)msg, (void)wParam, (void)lParam;
            switch (msg) {
                case WM_INPUT: {
                    onRawInput(lParam);
                    break;
                }
                case WM_CLOSE: {
                    DestroyWindow(hwnd);
                    return 0;
                }
                case WM_DESTROY: {
                    PostQuitMessage(0);
                    return 0;
                }
            }
            return DefWindowProcW(hwnd, msg, wParam, lParam);
        }

        inline static bool register_raw_input(HWND hwnd) {
            if (!hwnd) return false;

            RAWINPUTDEVICE rid{};
            rid.usUsagePage = 0x01;
            rid.usUsage = 0x06;
            rid.dwFlags = RIDEV_NOLEGACY | RIDEV_INPUTSINK;
            rid.hwndTarget = hwnd;

            return RegisterRawInputDevices(&rid, 1, sizeof(rid)) != FALSE;
        }

        inline static void unregister_raw_input() noexcept {
            RAWINPUTDEVICE rid{};
            rid.usUsagePage = 0x01;
            rid.usUsage = 0x06;
            rid.dwFlags = RIDEV_REMOVE;
            rid.hwndTarget = nullptr;
            RegisterRawInputDevices(&rid, 1, sizeof(rid));
        }

        inline static void message_thread_proc(std::promise<bool> ready) {
            MSG msg{};
            PeekMessageW(
                &msg,
                nullptr,
                WM_USER,
                WM_USER,
                PM_NOREMOVE
            );

            const HINSTANCE instance = GetModuleHandleW(nullptr);
            constexpr const wchar_t* class_name = L"LowLatencyKeyboardMessageWindow";

            WNDCLASSW wc{};
            wc.lpfnWndProc = keyboard_wnd_proc;
            wc.hInstance = instance;
            wc.lpszClassName = class_name;

            if (!RegisterClassW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
                ready.set_value(false);
                return;
            }

            HWND hwnd = CreateWindowExW(
                0, class_name, L"", 0,
                0, 0, 0, 0,
                HWND_MESSAGE, nullptr, instance, nullptr
            );

            if (!hwnd) {
                ready.set_value(false);
                return;
            }

            if (!register_raw_input(hwnd)) {
                DestroyWindow(hwnd);
                ready.set_value(false);
                return;
            }

            message_hwnd_.store(hwnd, std::memory_order_release);

            ready.set_value(true);

            while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
                DispatchMessageW(&msg);
            }

            message_hwnd_.store(nullptr, std::memory_order_release);

            unregister_raw_input();
        }

        inline static uint16_t normalizeVKey(const RAWKEYBOARD& kbd) {
            uint16_t vkey = (uint16_t)kbd.VKey;
            const uint16_t flags = (uint16_t)kbd.Flags;
            if (vkey == VK_SHIFT) {
                vkey = (kbd.MakeCode == 0x36) ? VK_RSHIFT : VK_LSHIFT;
            } else if (vkey == VK_CONTROL) {
                vkey = (flags & RI_KEY_E0) ? VK_RCONTROL : VK_LCONTROL;
            } else if (vkey == VK_MENU) {
                vkey = (flags & RI_KEY_E0) ? VK_RMENU : VK_LMENU;
            }
            return vkey;
        }

        inline static void onRawInput(LPARAM lParam) {
            RAWINPUT raw{};
            UINT size = sizeof(raw);

            if (GetRawInputData((HRAWINPUT)lParam, RID_INPUT, &raw, &size, sizeof(RAWINPUTHEADER)) == (UINT)(-1)) return;

            if (raw.header.dwType != RIM_TYPEKEYBOARD) return;

            const RAWKEYBOARD& kbd = raw.data.keyboard;
            if (kbd.VKey == 255) return; // fake key

            const uint16_t vkey = normalizeVKey(kbd);
            if (vkey >= 256) return;

            const uint16_t scan = (uint16_t)kbd.MakeCode;
            const uint16_t flags = (uint16_t)kbd.Flags;
  
            const uint8_t newDown = (flags & RI_KEY_BREAK) ? 0 : 1;
            const uint8_t oldDown = key_down_[vkey].exchange(newDown, std::memory_order_relaxed);

            if (oldDown == newDown) return;

            KeyEvent ev{ vkey, scan, flags, newDown };
            bool push_res = queue_.push(ev);
            (void) push_res;
        }

        inline static std::thread message_thread_{};
        inline static std::atomic<HWND> message_hwnd_{ nullptr };

        // SPSC queue, fixed size
        inline static boost::lockfree::spsc_queue<KeyEvent, boost::lockfree::capacity<kQueueCapacity>> queue_{};

        inline static std::array<std::atomic<uint8_t>, 256> key_down_{};
    };

    class LowLatencyKeyboardDestructorGuard final {
    public:
        LowLatencyKeyboardDestructorGuard() {
            static bool init = [] () ->bool {
                static LowLatencyKeyboard instance;
                (void) instance;
                return true;
            } ();
        }
    };

    inline LowLatencyKeyboardDestructorGuard kbd_guard;

} // namespace

#endif // !_LOW_LATENCY_KEYBOARD_HPP_
