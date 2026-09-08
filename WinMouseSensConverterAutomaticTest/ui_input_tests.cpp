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
    runner.run("stationary hover skips frames but card changes and exposure repaint", [&] {
        InputWindow fixture;
        auto& state = fixture.state;
        TEST_EXPECT(runner, SUCCEEDED(state.d2dui_context.initialize(state.hwnd)));
        class DrawCounter final : public d2dui::D2duiComponentsBase {
        public:
            int frames = 0;
            bool fail = false;
            const D2D1_RECT_F& get_bounds() const noexcept override { return bounds_; }
            void resize(const D2D1_RECT_F&, float) noexcept override {}
            HRESULT draw(d2dui::D2duiContext&) noexcept override { ++frames; return fail ? E_FAIL : S_OK; }
        };
        auto counter = std::make_shared<DrawCounter>();
        state.main_view.common_render()->register_component(counter);
        MSG timer{};
        timer.hwnd = state.hwnd;
        timer.message = WM_TIMER;
        timer.wParam = kUiTimer;
        auto tick = [&] {
            process_ui_timer(state);
            ui::finish_main_loop_iteration(state.hwnd, timer);
        };
        state.redraw_dirty = true;
        tick();
        TEST_EXPECT(runner, counter->frames == 1);
        auto move_to_card = [&](size_t index) {
            const auto bounds = state.main_view.measurement_grid().item_bounds(index);
            main_window_proc(state.hwnd, WM_MOUSEMOVE, 0,
                MAKELPARAM(static_cast<short>((bounds.left + bounds.right) / 2), static_cast<short>((bounds.top + bounds.bottom) / 2)));
        };
        move_to_card(0);
        tick();
        TEST_EXPECT(runner, counter->frames == 2);
        for (int i = 0; i < 100; ++i) tick();
        TEST_EXPECT(runner, counter->frames == 2 && !state.redraw_dirty);
        move_to_card(1);
        tick();
        TEST_EXPECT(runner, counter->frames == 3);
        for (int i = 0; i < 100; ++i) tick();
        TEST_EXPECT(runner, counter->frames == 3);
        main_window_proc(state.hwnd, WM_PAINT, 0, 0);
        tick();
        TEST_EXPECT(runner, counter->frames == 4);
        state.d2dui_context.discard_device_resources();
        state.redraw_dirty = true;
        tick();
        TEST_EXPECT(runner, counter->frames == 5 && !state.redraw_dirty);
        counter->fail = true;
        state.redraw_dirty = true;
        tick();
        TEST_EXPECT(runner, counter->frames == 6 && state.redraw_dirty);
        counter->fail = false;
        tick();
        TEST_EXPECT(runner, counter->frames == 7 && !state.redraw_dirty);
    });

    runner.run("window adapter schedules stopped input without a render target", [&] {
        InputWindow fixture;
        auto& state = fixture.state;
        int down = 0, hold = 0, up = 0;
        auto& grid = state.main_view.measurement_grid();
        grid.register_mouse_event_handler<Event::MOUSE_LEFT_CLICK_ENTER>([&](const auto&) { ++down; return true; });
        grid.register_mouse_event_handler<Event::MOUSE_LEFT_CLICK_ON>([&](const auto&) { ++hold; return true; });
        grid.register_mouse_event_handler<Event::MOUSE_LEFT_CLICK_LEAVE>([&](const auto&) { ++up; return true; });
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
        common->register_mouse_event_handler<Event::MOUSE_CANCEL>([&](const auto&) { ++common_cancel; return true; });
        common->register_mouse_event_handler<Event::MOUSE_LEFT_CLICK_ON>([&](const auto&) { ++common_hold; return true; });
        state.main_view.measurement_grid().register_mouse_event_handler<Event::MOUSE_CANCEL>([&](const auto&) { ++old_cancel; return true; });
        state.main_view.calibration_grid().register_mouse_event_handler<Event::MOUSE_LEFT_CLICK_ENTER>([&](const auto&) { ++new_down; return true; });
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
