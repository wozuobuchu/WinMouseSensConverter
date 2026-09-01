#pragma once

#include "test_framework.hpp"

namespace automatic_test {

    void add_config_tests(TestRunner& runner);
    void add_core_logic_tests(TestRunner& runner);
    void add_layout_cache_tests(TestRunner& runner);

} // namespace automatic_test
