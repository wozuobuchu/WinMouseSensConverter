#include "test_groups.hpp"

#include "sync.hpp"
#include "ui_view.hpp"
#include "SYS/low_latency_input.hpp"
#include <vector>

#include <array>
#include <limits>

namespace automatic_test {

    void add_core_logic_tests(TestRunner& runner) {
        runner.run("buffered movement publishes once per run and before input transitions", [&] {
            rawinput::detail::MovementBatch batch;
            std::vector<int> order;
            std::vector<std::pair<uint32_t, uint32_t>> publications;
            auto publish = [&](uint32_t x, uint32_t y) { order.push_back(0); publications.emplace_back(x, y); };
            auto keyboard = [&](const RAWKEYBOARD&) { order.push_back(1); };
            auto buttons = [&](const RAWMOUSE&) { order.push_back(2); };
            RAWINPUT input{};
            input.header.dwType = RIM_TYPEMOUSE;
            input.data.mouse.lLastX = 3;
            input.data.mouse.lLastY = -2;
            for (int i = 0; i < 64; ++i) batch.process(input, keyboard, buttons, publish);
            TEST_EXPECT(runner, publications.empty());
            batch.flush(publish);
            TEST_EXPECT(runner, publications.size() == 1);
            TEST_EXPECT(runner, publications[0] == (std::pair<uint32_t, uint32_t>{192, static_cast<uint32_t>(-128)}));
            batch.process(input, keyboard, buttons, publish);
            input.data.mouse.usButtonFlags = RI_MOUSE_LEFT_BUTTON_DOWN;
            batch.process(input, keyboard, buttons, publish);
            input.header.dwType = RIM_TYPEKEYBOARD;
            batch.process(input, keyboard, buttons, publish);
            batch.flush(publish);
            TEST_EXPECT(runner, order == (std::vector<int>{0, 0, 2, 0, 1}));
            TEST_EXPECT(runner, publications.size() == 3);
        });
        runner.run("movement batching preserves axis wrap and ignores absolute motion and wheels as keys", [&] {
            rawinput::detail::MovementBatch batch;
            uint32_t x = 0, y = 0;
            int publications = 0, keys = 0;
            auto publish = [&](uint32_t dx, uint32_t dy) { ++publications; x = dx; y = dy; };
            auto keyboard = [&](const RAWKEYBOARD&) { ++keys; };
            auto buttons = [&](const RAWMOUSE&) { ++keys; };
            RAWINPUT input{};
            input.header.dwType = RIM_TYPEMOUSE;
            input.data.mouse.lLastX = std::numeric_limits<LONG>::max();
            input.data.mouse.lLastY = -1;
            batch.process(input, keyboard, buttons, publish);
            input.data.mouse.lLastX = 1;
            input.data.mouse.lLastY = 2;
            input.data.mouse.usButtonFlags = RI_MOUSE_WHEEL | RI_MOUSE_HWHEEL;
            batch.process(input, keyboard, buttons, publish);
            input.data.mouse.usFlags = MOUSE_MOVE_ABSOLUTE;
            batch.process(input, keyboard, buttons, publish);
            batch.flush(publish);
            TEST_EXPECT(runner, publications == 1 && keys == 0);
            TEST_EXPECT(runner, x == 0x80000000u && y == 1);
            input.data.mouse.usFlags = 0;
            input.data.mouse.lLastX = -1;
            input.data.mouse.lLastY = 0;
            batch.process(input, keyboard, buttons, publish);
            input.data.mouse.lLastX = 1;
            batch.process(input, keyboard, buttons, publish);
            batch.flush(publish);
            TEST_EXPECT(runner, publications == 1);
        });

        runner.run("distance conversion covers all units", [&] {
            constexpr double raw_count = 800.0;
            constexpr double reference_dpi = 800.0;
            constexpr std::array<std::pair<config::OutputUnit, double>, 6> expected{{
                {config::OutputUnit::raw, 800.0},
                {config::OutputUnit::inch, 1.0},
                {config::OutputUnit::mm, 25.4},
                {config::OutputUnit::cm, 2.54},
                {config::OutputUnit::dm, 0.254},
                {config::OutputUnit::m, 0.0254},
            }};

            for (const auto& [unit, value] : expected) {
                TEST_EXPECT_NEAR(runner, ui::view::convert_distance(raw_count, reference_dpi, unit), value, 1e-12);
                TEST_EXPECT_NEAR(runner, ui::view::convert_distance(-raw_count, reference_dpi, unit), -value, 1e-12);
                TEST_EXPECT_NEAR(runner, ui::view::convert_distance(0.0, reference_dpi, unit), 0.0, 0.0);
            }
            TEST_EXPECT_NEAR(runner, ui::view::convert_distance(800.25, 800.25, config::OutputUnit::inch), 1.0, 1e-12);
        });

        runner.run("calibration converts an existing count magnitude", [&] {
            TEST_EXPECT_NEAR(runner, ui::view::calibration_dpi_from_counts(5.0, 10), 1.27, 1e-12);
            TEST_EXPECT_NEAR(runner, ui::view::calibration_dpi_from_counts(0.0, 10), 0.0, 0.0);
            TEST_EXPECT_NEAR(runner, ui::view::calibration_dpi_from_counts(500.0, 50), 25.4, 1e-12);
            TEST_EXPECT_NEAR(runner, ui::view::calibration_dpi_from_counts(5.0, 10.5), 5.0 * 2.54 / 10.5, 1e-12);
        });

        runner.run("recording transitions reset only when starting", [&] {
            app_data::on_recording_ = 0;
            app_data::current_mode_ = config::AppMode::calibration;
            app_data::accumulated_muzmov_dx = 12.0;
            app_data::accumulated_muzmov_dy = -34.0;

            TEST_EXPECT(runner, app_func::toggle_recording(false));
            TEST_EXPECT(runner, app_data::on_recording_ != 0);
            TEST_EXPECT_NEAR(runner, app_data::accumulated_muzmov_dx, 0.0, 0.0);
            TEST_EXPECT_NEAR(runner, app_data::accumulated_muzmov_dy, 0.0, 0.0);
            TEST_EXPECT(runner, app_data::current_mode_ == config::AppMode::calibration);

            app_data::accumulated_muzmov_dx = 56.0;
            app_data::accumulated_muzmov_dy = -78.0;

            TEST_EXPECT(runner, !app_func::toggle_recording(false));
            TEST_EXPECT(runner, app_data::on_recording_ == 0);
            TEST_EXPECT_NEAR(runner, app_data::accumulated_muzmov_dx, 56.0, 0.0);
            TEST_EXPECT_NEAR(runner, app_data::accumulated_muzmov_dy, -78.0, 0.0);
            TEST_EXPECT(runner, app_data::current_mode_ == config::AppMode::calibration);

            app_data::current_mode_ = config::AppMode::measurement;
            app_data::accumulated_muzmov_dx = 0.0;
            app_data::accumulated_muzmov_dy = 0.0;
        });
    }

} // namespace automatic_test
