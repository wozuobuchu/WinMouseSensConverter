#include "test_groups.hpp"

#include "recording_key.hpp"
#include "ui_internal.hpp"

#include <array>

namespace automatic_test {

    void add_core_logic_tests(TestRunner& runner) {
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
                TEST_EXPECT_NEAR(runner, ui::detail::convert_distance(raw_count, reference_dpi, unit), value, 1e-12);
                TEST_EXPECT_NEAR(runner, ui::detail::convert_distance(-raw_count, reference_dpi, unit), -value, 1e-12);
                TEST_EXPECT_NEAR(runner, ui::detail::convert_distance(0.0, reference_dpi, unit), 0.0, 0.0);
            }
        });

        runner.run("calibration uses the final two-dimensional vector", [&] {
            TEST_EXPECT_NEAR(runner, ui::detail::calibration_dpi(3.0, 4.0, 10), 1.27, 1e-12);
            TEST_EXPECT_NEAR(runner, ui::detail::calibration_dpi(-3.0, -4.0, 10), 1.27, 1e-12);
            TEST_EXPECT_NEAR(runner, ui::detail::calibration_dpi(0.0, 0.0, 10), 0.0, 0.0);
            TEST_EXPECT_NEAR(runner, ui::detail::calibration_dpi(300.0, 400.0, 50), 25.4, 1e-12);
        });

        runner.run("recording key matching normalizes modifier sides", [&] {
            TEST_EXPECT(runner, main_loop::matches_recording_key(VK_F1, VK_F1));
            TEST_EXPECT(runner, !main_loop::matches_recording_key(VK_F1, VK_F2));

            TEST_EXPECT(runner, main_loop::matches_recording_key(VK_SHIFT, VK_LSHIFT));
            TEST_EXPECT(runner, main_loop::matches_recording_key(VK_SHIFT, VK_RSHIFT));
            TEST_EXPECT(runner, !main_loop::matches_recording_key(VK_SHIFT, VK_CONTROL));

            TEST_EXPECT(runner, main_loop::matches_recording_key(VK_CONTROL, VK_LCONTROL));
            TEST_EXPECT(runner, main_loop::matches_recording_key(VK_CONTROL, VK_RCONTROL));
            TEST_EXPECT(runner, !main_loop::matches_recording_key(VK_CONTROL, VK_SHIFT));

            TEST_EXPECT(runner, main_loop::matches_recording_key(VK_MENU, VK_LMENU));
            TEST_EXPECT(runner, main_loop::matches_recording_key(VK_MENU, VK_RMENU));
            TEST_EXPECT(runner, !main_loop::matches_recording_key(VK_MENU, VK_CONTROL));
        });
    }

} // namespace automatic_test
