#pragma once

#include <cmath>
#include <exception>
#include <functional>
#include <iostream>
#include <string_view>

namespace automatic_test {

    class TestRunner final {
    public:
        void run(std::string_view name, const std::function<void()>& test) {
            ++test_count_;
            const int failures_before = failure_count_;
            current_test_ = name;

            try {
                test();
            } catch (const std::exception& error) {
                fail("unexpected exception", __FILE__, __LINE__, error.what());
            } catch (...) {
                fail("unexpected non-standard exception", __FILE__, __LINE__);
            }

            if (failure_count_ == failures_before) {
                std::cout << "[PASS] " << name << '\n';
            } else {
                ++failed_test_count_;
                std::cout << "[FAIL] " << name << '\n';
            }
        }

        void expect(bool condition, std::string_view expression, std::string_view file, int line) {
            ++assertion_count_;
            if (!condition) fail(expression, file, line);
        }

        void expect_near(double actual, double expected, double tolerance, std::string_view expression, std::string_view file, int line) {
            ++assertion_count_;
            if (std::isfinite(actual) && std::isfinite(expected) && std::abs(actual - expected) <= tolerance) return;

            std::cerr << file << '(' << line << "): " << current_test_ << ": expected " << expression
                      << " within " << tolerance << ", actual=" << actual << ", expected=" << expected << '\n';
            ++failure_count_;
        }

        int finish() const {
            std::cout << '\n' << (test_count_ - failed_test_count_) << '/' << test_count_ << " tests passed; "
                      << assertion_count_ << " assertions; " << failure_count_ << " failures.\n";
            return failure_count_ == 0 ? 0 : 1;
        }

    private:
        void fail(std::string_view expression, std::string_view file, int line, std::string_view detail = {}) {
            std::cerr << file << '(' << line << "): " << current_test_ << ": assertion failed: " << expression;
            if (!detail.empty()) std::cerr << " (" << detail << ')';
            std::cerr << '\n';
            ++failure_count_;
        }

        std::string_view current_test_;
        int test_count_ = 0;
        int failed_test_count_ = 0;
        int assertion_count_ = 0;
        int failure_count_ = 0;
    };

} // namespace automatic_test

#define TEST_EXPECT(runner, expression) (runner).expect(static_cast<bool>(expression), #expression, __FILE__, __LINE__)
#define TEST_EXPECT_NEAR(runner, actual, expected, tolerance) (runner).expect_near(static_cast<double>(actual), static_cast<double>(expected), static_cast<double>(tolerance), #actual, __FILE__, __LINE__)
