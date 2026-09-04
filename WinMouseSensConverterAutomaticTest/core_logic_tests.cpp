#include "test_groups.hpp"

#include "sync.hpp"
#include "ui_view.hpp"
#include "SYS/cursor_pos.hpp"

#include <array>
#include <limits>

namespace automatic_test {

    void add_core_logic_tests(TestRunner& runner) {
        runner.run("cursor hit testing uses half-open Direct2D rectangles", [&] {
            constexpr D2D1_RECT_F rect{-10.0f, -20.0f, 30.0f, 40.0f};

            TEST_EXPECT(runner, cursor_pos::is_in_rect(D2D1_POINT_2F{-10.0f, -20.0f}, rect));
            TEST_EXPECT(runner, cursor_pos::is_in_rect(D2D1_POINT_2F{0.0f, 0.0f}, rect));
            TEST_EXPECT(runner, !cursor_pos::is_in_rect(D2D1_POINT_2F{30.0f, 0.0f}, rect));
            TEST_EXPECT(runner, !cursor_pos::is_in_rect(D2D1_POINT_2F{0.0f, 40.0f}, rect));
            TEST_EXPECT(runner, !cursor_pos::is_in_rect(D2D1_POINT_2F{-10.01f, 0.0f}, rect));
            TEST_EXPECT(runner, !cursor_pos::is_in_rect(D2D1_POINT_2F{0.0f, -20.01f}, rect));
            TEST_EXPECT(runner, !cursor_pos::is_in_rect(D2D1_POINT_2F{30.01f, 0.0f}, rect));
            TEST_EXPECT(runner, !cursor_pos::is_in_rect(D2D1_POINT_2F{0.0f, 40.01f}, rect));
        });

        runner.run("distance conversion covers all units", [&] {
            constexpr double raw_count = 800.0;
            constexpr int reference_dpi = 800;
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
        });

        runner.run("calibration converts an existing count magnitude", [&] {
            TEST_EXPECT_NEAR(runner, ui::view::calibration_dpi_from_counts(5.0, 10), 1.27, 1e-12);
            TEST_EXPECT_NEAR(runner, ui::view::calibration_dpi_from_counts(0.0, 10), 0.0, 0.0);
            TEST_EXPECT_NEAR(runner, ui::view::calibration_dpi_from_counts(500.0, 50), 25.4, 1e-12);
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
