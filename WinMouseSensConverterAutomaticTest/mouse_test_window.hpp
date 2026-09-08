#pragma once

#include "D2DUILIB/D2DUILIB_INTERFACE/d2dui_mouse_event_analyser.hpp"
#include <optional>

namespace automatic_test {
    // A current-user window with real capture notifications, no render target,
    // and explicit activation messages so tests do not steal foreground focus.
    class MouseTestWindow final {
    public:
        explicit MouseTestWindow(UINT dpi = 96) {
            hwnd = CreateWindowExW(0, L"STATIC", L"Mouse analyser test", WS_POPUP,
                0, 0, 800, 450, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
            if (!hwnd) throw std::runtime_error("CreateWindowExW failed");
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
            SetWindowLongPtrW(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&window_proc));
            analyser.emplace(hwnd, dpi);
            analyser->set_renderers({render});
            message(WM_ACTIVATE, WA_ACTIVE);
        }
        ~MouseTestWindow() {
            analyser.reset();
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
            DestroyWindow(hwnd);
        }
        bool message(UINT msg, WPARAM wp = 0, LPARAM lp = 0) {
            return analyser->process_window_message(msg, wp, lp).callbacks_invoked;
        }
        bool button(int16_t vk, bool down, short x = 20, short y = 20) {
            UINT msg = 0;
            WPARAM wp = 0;
            switch (vk) {
            case VK_LBUTTON: msg = down ? WM_LBUTTONDOWN : WM_LBUTTONUP; break;
            case VK_RBUTTON: msg = down ? WM_RBUTTONDOWN : WM_RBUTTONUP; break;
            case VK_MBUTTON: msg = down ? WM_MBUTTONDOWN : WM_MBUTTONUP; break;
            case VK_XBUTTON1: case VK_XBUTTON2:
                msg = down ? WM_XBUTTONDOWN : WM_XBUTTONUP;
                wp = MAKEWPARAM(0, vk == VK_XBUTTON1 ? XBUTTON1 : XBUTTON2);
                break;
            default: throw std::invalid_argument("not a mouse button");
            }
            return message(msg, wp, MAKELPARAM(x, y));
        }

        HWND hwnd = nullptr;
        std::shared_ptr<d2dui::D2duiSystemRender> render = std::make_shared<d2dui::D2duiSystemRender>();
        std::optional<d2dui::MouseEventAnalyser> analyser;

    private:
        static LRESULT CALLBACK window_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
            const auto self = reinterpret_cast<MouseTestWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
            if (self && self->analyser) {
                const auto result = self->analyser->process_window_message(msg, wp, lp);
                if (result.consumed) return result.result;
            }
            return DefWindowProcW(hwnd, msg, wp, lp);
        }
    };
}
