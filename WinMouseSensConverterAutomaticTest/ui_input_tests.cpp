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
        state.mouse_analyser.emplace(state.hwnd, state.dpi);
        state.mouse_analyser->set_renderers({state.main_view.common_render(), state.main_view.measurement_render()});
        main_window_proc(state.hwnd, WM_ACTIVATE, WA_ACTIVE, 0);
        update_input_layout(state);
        state.redraw_dirty = false;
    }
    ~InputWindow() {
        state.mouse_analyser.reset();
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
        main_window_proc(state.hwnd, WM_LBUTTONDOWN, MK_LBUTTON, position);
        TEST_EXPECT(runner, down == 1 && state.redraw_dirty);
        TEST_EXPECT(runner, GetCapture() == state.hwnd);
        state.redraw_dirty = false;
        process_ui_timer(state);
        TEST_EXPECT(runner, hold == 1 && state.redraw_dirty);
        main_window_proc(state.hwnd, WM_LBUTTONUP, 0, MAKELPARAM(-20, -20));
        TEST_EXPECT(runner, up == 1 && GetCapture() != state.hwnd);
        TEST_EXPECT(runner, state.d2dui_context.render_target() == nullptr && !state.d2dui_context.in_frame());
        TEST_EXPECT(runner, app_data::on_recording_ == 0);
    });
    runner.run("mode command cancels only old mode and keeps physical button baseline", [&] {
        InputWindow fixture;
        auto& state = fixture.state;
        auto common = std::make_shared<d2dui::D2duiSwitch>();
        common->resize({0, 0, 800, 450}, 1);
        state.main_view.common_render()->register_component(common);
        int common_cancel = 0, common_hold = 0, old_cancel = 0, new_down = 0;
        common->register_mouse_event_handler<Event::MOUSE_CANCEL>([&](const auto&) { ++common_cancel; });
        common->register_mouse_event_handler<Event::MOUSE_LEFT_CLICK_ON>([&](const auto&) { ++common_hold; });
        state.main_view.measurement_grid().register_mouse_event_handler<Event::MOUSE_CANCEL>([&](const auto&) { ++old_cancel; });
        state.main_view.calibration_grid().register_mouse_event_handler<Event::MOUSE_LEFT_CLICK_ENTER>([&](const auto&) { ++new_down; });
        const auto bounds = state.main_view.measurement_grid().get_bounds();
        const LPARAM pos = MAKELPARAM(static_cast<short>(bounds.left + 2), static_cast<short>(bounds.top + 2));
        main_window_proc(state.hwnd, WM_LBUTTONDOWN, MK_LBUTTON, pos);
        handle_menu_command(state, kCommandModeCalibration);
        process_ui_timer(state);
        main_window_proc(state.hwnd, WM_LBUTTONDOWN, MK_LBUTTON, pos);
        TEST_EXPECT(runner, common_cancel == 0 && common_hold == 1 && old_cancel == 1 && new_down == 0);
        main_window_proc(state.hwnd, WM_LBUTTONUP, 0, pos);
        main_window_proc(state.hwnd, WM_LBUTTONDOWN, MK_LBUTTON, pos);
        TEST_EXPECT(runner, new_down == 1);
    });
}
}
