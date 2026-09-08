// Standalone Win32 example. Define D2DUI_EXAMPLE_COMPILE_ONLY to compile it into
// the automatic tests without adding an entry point or launching this window.
#include "../D2DUILIB_INTERFACE/d2dui_mouse_event_analyser.hpp"
#include "../D2DUILIB_COMPONENT/d2dui_switch.hpp"
#include "../D2DUILIB_COMPONENT/d2dui_text.hpp"

#include <optional>

namespace mouse_events_example {
    using namespace d2dui;
    constexpr UINT_PTR timer_id = 1;

    struct WindowState {
        HWND hwnd = nullptr;
        UINT dpi = 96;
        bool dirty = true;
        bool frame_due = false;
        bool minimized = false;
        bool sizing = false;
        bool second_page = false;
        D2duiContext context;
        std::shared_ptr<D2duiSystemRender> common = std::make_shared<D2duiSystemRender>();
        std::shared_ptr<D2duiSystemRender> first = std::make_shared<D2duiSystemRender>();
        std::shared_ptr<D2duiSystemRender> second = std::make_shared<D2duiSystemRender>();
        std::shared_ptr<D2duiSwitch> selector = std::make_shared<D2duiSwitch>();
        std::shared_ptr<D2duiText> instructions = std::make_shared<D2duiText>(L"Top switch: change page. Bottom switch: change its value.");
        std::shared_ptr<D2duiSwitch> first_switch = std::make_shared<D2duiSwitch>();
        std::shared_ptr<D2duiSwitch> second_switch = std::make_shared<D2duiSwitch>();
        std::optional<MouseEventAnalyser> mouse;

        WindowState() {
            common->register_component(instructions);
            common->register_component(selector);
            first->register_component(first_switch);
            second->register_component(second_switch);
            second_switch->set_colors({0x007AFF, 1}, {0xAACCEE, 1}, {0xFFFFFF, 1});
            selector->register_mouse_event_handler<D2duiMouseEvent::MOUSE_LEFT_CLICK_LEAVE>([this](const auto&) {
                const bool next = !second_page;
                mouse->set_renderers({common, next ? second : first});
                second_page = next;
                selector->set_checked(next);
            });
            for (const auto& component : {first_switch, second_switch}) {
                // Capturing the component weakly avoids component -> callback -> component cycles.
                component->register_mouse_event_handler<D2duiMouseEvent::MOUSE_LEFT_CLICK_LEAVE>(
                    [weak = std::weak_ptr<D2duiSwitch>(component)](const auto&) {
                        if (const auto toggle = weak.lock()) toggle->set_checked(!toggle->checked());
                    });
            }
        }

        void layout() {
            RECT client{};
            if (!GetClientRect(hwnd, &client)) return;
            const float width = static_cast<float>(client.right) * 96.0f / static_cast<float>(dpi);
            instructions->resize({20, 15, (std::max)(width - 20, 100.0f), 70}, 1);
            selector->resize({20, 80, 68, 104}, 1);
            first_switch->resize({20, 140, 68, 164}, 1);
            second_switch->resize({20, 140, 68, 164}, 1);
        }

        void draw_if_due() {
            if (!frame_due || !dirty || minimized || sizing) return;
            frame_due = false;
            if (context.begin_frame({0xF4F7FB, 1}) != S_OK) return;
            HRESULT result = common->draw(context);
            if (SUCCEEDED(result)) result = (second_page ? second : first)->draw(context);
            const HRESULT end = context.end_frame();
            dirty = FAILED(result) || FAILED(end);
        }
    };

    LRESULT CALLBACK window_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) noexcept {
        auto* state = reinterpret_cast<WindowState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        try {
            if (message == WM_NCCREATE) {
                state = static_cast<WindowState*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);
                state->hwnd = hwnd;
                state->dpi = GetDpiForWindow(hwnd);
                if (!state->dpi) state->dpi = 96;
                state->mouse.emplace(hwnd, state->dpi);
                state->mouse->set_renderers({state->common, state->first});
                SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
            }
            if (!state) return DefWindowProcW(hwnd, message, wparam, lparam);

            // Forward lifecycle messages too, even when this host also handles them.
            if (message >= WM_MOUSEFIRST && message <= WM_MOUSELAST) state->layout();
            const auto input = state->mouse->process_window_message(message, wparam, lparam);
            if (input.callbacks_invoked) state->dirty = true;
            if (input.consumed) return input.result;

            switch (message) {
            case WM_CREATE: return SetTimer(hwnd, timer_id, 8, nullptr) ? 0 : -1;
            case WM_TIMER:
                if (wparam != timer_id) break;
                state->layout();
                if (state->mouse->tick()) state->dirty = true;
                state->frame_due = true;
                return 0;
            case WM_SIZE:
                state->minimized = wparam == SIZE_MINIMIZED;
                state->layout();
                state->dirty = true;
                return 0;
            case WM_ENTERSIZEMOVE: state->sizing = true; return 0;
            case WM_EXITSIZEMOVE: state->sizing = false; state->dirty = true; return 0;
            case WM_DPICHANGED: {
                state->dpi = HIWORD(wparam);
                state->context.set_dpi(state->dpi);
                const auto* rect = reinterpret_cast<RECT*>(lparam);
                SetWindowPos(hwnd, nullptr, rect->left, rect->top, rect->right - rect->left,
                    rect->bottom - rect->top, SWP_NOACTIVATE | SWP_NOZORDER);
                state->layout();
                state->dirty = true;
                return 0;
            }
            // Useful when embedding this view as a PMv2 child window. These
            // notifications carry no DPI or suggested RECT in their parameters.
            case WM_DPICHANGED_BEFOREPARENT:
            case WM_DPICHANGED_AFTERPARENT:
                if (const UINT dpi = GetDpiForWindow(hwnd)) {
                    state->dpi = dpi;
                    state->context.set_dpi(dpi);
                    state->layout();
                    state->dirty = true;
                }
                return 0;
            case WM_PAINT: {
                PAINTSTRUCT paint{};
                BeginPaint(hwnd, &paint);
                EndPaint(hwnd, &paint);
                state->dirty = true;
                return 0;
            }
            case WM_ERASEBKGND: return 1;
            case WM_DESTROY:
                KillTimer(hwnd, timer_id);
                PostQuitMessage(0);
                return 0;
            case WM_NCDESTROY:
                SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
                break;
            }
        } catch (...) {
            if (message == WM_NCCREATE) return FALSE;
            if (message == WM_CREATE) return -1;
        }
        return DefWindowProcW(hwnd, message, wparam, lparam);
    }

    int run(HINSTANCE instance, int show) {
        SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        WNDCLASSW window_class{};
        window_class.style = CS_DBLCLKS;
        window_class.lpfnWndProc = window_proc;
        window_class.hInstance = instance;
        window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        window_class.lpszClassName = L"D2duiMouseEventsExample";
        if (!RegisterClassW(&window_class)) return 1;
        WindowState state;
        const HWND hwnd = CreateWindowExW(0, window_class.lpszClassName, L"D2DUILIB MouseEventAnalyser",
            WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 700, 300, nullptr, nullptr, instance, &state);
        if (!hwnd) return 1;
        if (FAILED(state.context.initialize(hwnd, state.dpi))) { DestroyWindow(hwnd); return 1; }
        state.layout();
        ShowWindow(hwnd, show);
        MSG message{};
        BOOL result;
        while ((result = GetMessageW(&message, nullptr, 0, 0)) > 0) {
            state.frame_due = false;
            TranslateMessage(&message);
            DispatchMessageW(&message);
            // The only rendering path is outside window handlers, gated by the timer.
            state.draw_if_due();
        }
        if (IsWindow(hwnd)) DestroyWindow(hwnd);
        return result == -1 ? 1 : static_cast<int>(message.wParam);
    }
}

#ifndef D2DUI_EXAMPLE_COMPILE_ONLY
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
    try { return mouse_events_example::run(instance, show); }
    catch (...) { return 1; }
}
#endif
