#include "test_groups.hpp"

#include <Windows.h>

namespace {
    void expect_no_raw_input_devices(automatic_test::TestRunner& runner) {
        UINT count = 0;
        const UINT result = GetRegisteredRawInputDevices(nullptr, &count, sizeof(RAWINPUTDEVICE));
        TEST_EXPECT(runner, result == 0);
        TEST_EXPECT(runner, count == 0);
    }
}

int main() {
    automatic_test::TestRunner runner;
    runner.run("test startup does not register Raw Input", [&] { expect_no_raw_input_devices(runner); });
    automatic_test::add_config_tests(runner);
    automatic_test::add_core_logic_tests(runner);
    automatic_test::add_layout_cache_tests(runner);
    automatic_test::add_mouse_dispatch_tests(runner);
    automatic_test::add_ui_input_tests(runner);
    runner.run("UI tests do not register Raw Input", [&] { expect_no_raw_input_devices(runner); });
    return runner.finish();
}
