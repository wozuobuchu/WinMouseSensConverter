#include "test_groups.hpp"

int main() {
    automatic_test::TestRunner runner;
    automatic_test::add_config_tests(runner);
    automatic_test::add_core_logic_tests(runner);
    automatic_test::add_layout_cache_tests(runner);
    automatic_test::add_mouse_dispatch_tests(runner);
    automatic_test::add_ui_input_tests(runner);
    return runner.finish();
}
