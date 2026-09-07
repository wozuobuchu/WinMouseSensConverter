// Compile the window adapter in this test translation unit to exercise its private
// scheduling boundary without starting WinMain, Raw Input, or an elevated process.
#include "../WinMouseSensConverter/ui.cpp"
#include "test_groups.hpp"

namespace automatic_test {
namespace {
class InputWindow final {
public:
    InputWindow() {
        state.hwnd = CreateWindowExW(0, L"STATIC", L"Input adapter test", WS_POPUP,
            0, 0, 800, 450, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        if (!state.hwnd) throw std::runtime_error("CreateWindowExW failed");
        state.user_config = &config;
        SetWindowLongPtrW(state.hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&state));
        app_data::current_mode_ = config::AppMode::measurement;
        app_data::on_recording_ = 0;
        update_input_layout(state);
        state.redraw_dirty = false;
    }
    ~InputWindow() {
        cancel_mouse_interaction(state);
        SetWindowLongPtrW(state.hwnd, GWLP_USERDATA, 0);
        DestroyWindow(state.hwnd);
    }
    config::UserConfig config;
    UiState state;
};
}
void add_ui_input_tests(TestRunner& runner) {
    using Event = d2dui::D2duiMouseEvent;
    runner.run("window adapter schedules stopped input without a render target", [&] {
        InputWindow fixture;
        auto& state = fixture.state;
        int down = 0, hold = 0, up = 0;
        auto& grid = state.main_view.measurement_grid();
        grid.register_mouse_event_handler<Event::MOUSE_LEFT_CLICK_ENTER>([&](const auto&) { ++down; });
        grid.register_mouse_event_handler<Event::MOUSE_LEFT_CLICK_ON>([&](const auto&) { ++hold; });
        grid.register_mouse_event_handler<Event::MOUSE_LEFT_CLICK_LEAVE>([&](const auto&) { ++up; });
        const auto rect = grid.get_bounds();
        const LPARAM position = MAKELPARAM(static_cast<short>(rect.left + 2), static_cast<short>(rect.top + 2));
        process_window_mouse(state, WM_LBUTTONDOWN, MK_LBUTTON, position);
        TEST_EXPECT(runner, down == 1 && state.redraw_dirty);
        TEST_EXPECT(runner, GetCapture() == state.hwnd);
        state.redraw_dirty = false;
        process_ui_timer(state, true);
        TEST_EXPECT(runner, hold == 1 && state.redraw_dirty);
        process_window_mouse(state, WM_LBUTTONUP, 0, MAKELPARAM(-20, -20));
        TEST_EXPECT(runner, up == 1 && GetCapture() != state.hwnd);
        TEST_EXPECT(runner, state.d2dui_context.render_target() == nullptr && !state.d2dui_context.in_frame());
        TEST_EXPECT(runner, app_data::on_recording_ == 0);
    });
    runner.run("window adapter decodes signed coordinates and DPI", [&] {
        for (UINT dpi : {96u, 144u, 192u}) {
            InputWindow fixture;
            auto& state = fixture.state;
            state.dpi = dpi;
            update_input_layout(state);
            auto& grid = state.main_view.measurement_grid();
            int count = 0;
            const auto bounds = grid.get_bounds();
            const short x = static_cast<short>((bounds.left + 10) * static_cast<float>(dpi) / 96.0f);
            const short y = static_cast<short>((bounds.top + 10) * static_cast<float>(dpi) / 96.0f);
            grid.register_mouse_event_handler<Event::MOUSE_X2_CLICK_ENTER>([&](const auto& event) {
                ++count;
                TEST_EXPECT_NEAR(runner, event.position.x, static_cast<float>(x) * 96.0f / static_cast<float>(dpi), 0.001);
            });
            grid.register_mouse_event_handler<Event::MOUSE_X2_CLICK_LEAVE>([&](const auto& event) {
                ++count;
                TEST_EXPECT_NEAR(runner, event.position.x, -30.0f * 96.0f / static_cast<float>(dpi), 0.001);
            });
            process_window_mouse(state, WM_XBUTTONDOWN, MAKEWPARAM(MK_XBUTTON2, XBUTTON2), MAKELPARAM(x, y));
            process_window_mouse(state, WM_XBUTTONUP, MAKEWPARAM(0, XBUTTON2), MAKELPARAM(-30, -20));
            TEST_EXPECT(runner, count == 2);
        }
    });
    runner.run("mode command cancels only old mode and keeps physical button baseline", [&] {
        InputWindow fixture;
        auto& state = fixture.state;
        auto common = std::make_shared<d2dui::D2duiSwitch>();
        common->resize({0, 0, 800, 450}, 1);
        state.main_view.common_render().register_component(common);
        int common_cancel = 0, common_hold = 0, old_cancel = 0, new_down = 0;
        common->register_mouse_event_handler<Event::MOUSE_CANCEL>([&](const auto&) { ++common_cancel; });
        common->register_mouse_event_handler<Event::MOUSE_LEFT_CLICK_ON>([&](const auto&) { ++common_hold; });
        state.main_view.measurement_grid().register_mouse_event_handler<Event::MOUSE_CANCEL>([&](const auto&) { ++old_cancel; });
        state.main_view.calibration_grid().register_mouse_event_handler<Event::MOUSE_LEFT_CLICK_ENTER>([&](const auto&) { ++new_down; });
        const auto bounds = state.main_view.measurement_grid().get_bounds();
        const LPARAM pos = MAKELPARAM(static_cast<short>(bounds.left + 2), static_cast<short>(bounds.top + 2));
        process_window_mouse(state, WM_LBUTTONDOWN, MK_LBUTTON, pos);
        handle_menu_command(state, kCommandModeCalibration);
        process_ui_timer(state, true);
        process_window_mouse(state, WM_LBUTTONDOWN, MK_LBUTTON, pos);
        TEST_EXPECT(runner, common_cancel == 0 && common_hold == 1 && old_cancel == 1 && new_down == 0);
        process_window_mouse(state, WM_LBUTTONUP, 0, pos);
        process_window_mouse(state, WM_LBUTTONDOWN, MK_LBUTTON, pos);
        TEST_EXPECT(runner, new_down == 1);
    });
    runner.run("window capture lasts until final button release", [&] {
        InputWindow fixture;
        auto& state = fixture.state;
        int canceled = 0;
        state.main_view.measurement_grid().register_mouse_event_handler<Event::MOUSE_CANCEL>([&](const auto&) { ++canceled; });
        process_window_mouse(state, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(100, 100));
        process_window_mouse(state, WM_RBUTTONDOWN, MK_LBUTTON | MK_RBUTTON, MAKELPARAM(100, 100));
        process_window_mouse(state, WM_LBUTTONUP, MK_RBUTTON, MAKELPARAM(100, 100));
        TEST_EXPECT(runner, GetCapture() == state.hwnd);
        process_window_mouse(state, WM_RBUTTONUP, 0, MAKELPARAM(100, 100));
        main_window_proc(state.hwnd, WM_CAPTURECHANGED, 0, 0);
        TEST_EXPECT(runner, GetCapture() != state.hwnd && canceled == 0);
    });    runner.run("window lifecycle messages cancel captured interactions", [&] {
        for (UINT message : {WM_ACTIVATE, WM_CAPTURECHANGED, WM_CANCELMODE, WM_ENTERMENULOOP, WM_ENTERSIZEMOVE, WM_SIZE}) {
            InputWindow fixture;
            auto& state = fixture.state;
            int canceled = 0, released = 0;
            auto& grid = state.main_view.measurement_grid();
            grid.register_mouse_event_handler<Event::MOUSE_CANCEL>([&](const auto&) { ++canceled; });
            grid.register_mouse_event_handler<Event::MOUSE_LEFT_CLICK_LEAVE>([&](const auto&) { ++released; });
            const auto bounds = grid.get_bounds();
            process_window_mouse(state, WM_LBUTTONDOWN, MK_LBUTTON,
                MAKELPARAM(static_cast<short>(bounds.left + 2), static_cast<short>(bounds.top + 2)));
            main_window_proc(state.hwnd, message, message == WM_SIZE ? SIZE_MINIMIZED : 0, 0);
            TEST_EXPECT(runner, canceled == 1 && released == 0 && state.mouse_buttons.none());
            TEST_EXPECT(runner, GetCapture() != state.hwnd);
        }
    });
}
}
