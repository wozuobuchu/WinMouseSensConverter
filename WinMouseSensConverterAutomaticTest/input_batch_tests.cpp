#include "SYS/low_latency_input.hpp"
#include "SYS/low_latency_mousemov.hpp"
#include "SYS/low_latency_keyboard.hpp"
#include "test_groups.hpp"

#include <limits>
#include <string>
#include <type_traits>

namespace {
    template <typename Guard>
    constexpr bool has_explicit_input_lifetime = std::is_nothrow_default_constructible_v<Guard> &&
        !std::is_copy_constructible_v<Guard> && !std::is_copy_assignable_v<Guard> &&
        !std::is_move_constructible_v<Guard> && !std::is_move_assignable_v<Guard> &&
        std::is_nothrow_destructible_v<Guard> &&
        std::is_same_v<decltype(std::declval<const Guard&>().started()), bool> &&
        noexcept(std::declval<const Guard&>().started());

    static_assert(has_explicit_input_lifetime<rawinput::LowLatencyInputLifetimeGuard>);
    static_assert(has_explicit_input_lifetime<rawinput::LowLatencyMouseMovLifetimeGuard>);
    static_assert(has_explicit_input_lifetime<rawinput::LowLatencyKeyboardLifetimeGuard>);
}

namespace rawinput {
    // All instrumentation lives here; including any of the three input headers does not start a thread.
    struct InputBatchTestAccess {
        template <typename Input>
        using Batch = typename Input::MovementBatch;

        template <typename Input>
        inline static size_t publication_count = 0;

        template <typename Input>
        struct CountedMovementAtomic {
            uint64_t load(std::memory_order order) const noexcept {
                return Input::packed_movement_.load(order);
            }

            bool compare_exchange_weak(uint64_t& expected, uint64_t desired,
                std::memory_order success, std::memory_order failure) noexcept {
                const bool published = Input::packed_movement_.compare_exchange_weak(expected, desired, success, failure);
                if (published) ++publication_count<Input>;
                return published;
            }
        };

        template <typename Input>
        static void reset() {
            Input::packed_movement_.store(0, std::memory_order_relaxed);
            publication_count<Input> = 0;
            if constexpr (std::is_same_v<Input, LowLatencyInput>) {
                Input::queue_.reset();
                for (auto& key : Input::key_down_) key.store(0, std::memory_order_relaxed);
            }
        }

        template <typename Input>
        static void process(const RAWINPUT& report, Batch<Input>& batch) {
            Input::process_raw_input(report, batch);
        }

        template <typename Input>
        static void publish(Batch<Input>& batch) {
            CountedMovementAtomic<Input> movement;
            Input::publish_movement_to(batch, movement);
        }

        template <typename Input>
        static size_t publications() { return publication_count<Input>; }

        template <typename Input, typename Reader>
        static bool drain(Batch<Input>& batch, Reader reader) {
            alignas(8) std::array<RAWINPUT, Input::kRawInputBatchCapacity> buffer{};
            return Input::drain_raw_input_buffer(buffer, batch, reader);
        }

        template <typename Input, typename Reader>
        static void loop(Reader reader) {
            Input::run_message_loop(reader, &publish<Input>);
        }
    };
}

namespace automatic_test {
namespace {
    using Access = rawinput::InputBatchTestAccess;

    RAWINPUT movement(LONG x, LONG y, USHORT flags = MOUSE_MOVE_RELATIVE, USHORT buttons = 0) {
        RAWINPUT report{};
        report.header.dwType = RIM_TYPEMOUSE;
        report.header.dwSize = sizeof(RAWINPUT);
        report.data.mouse.usFlags = flags;
        report.data.mouse.usButtonFlags = buttons;
        report.data.mouse.lLastX = x;
        report.data.mouse.lLastY = y;
        return report;
    }

    template <typename Input>
    void expect_sample(TestRunner& runner, int32_t x, int32_t y) {
        const auto actual = Input::sample();
        TEST_EXPECT(runner, actual.first == x);
        TEST_EXPECT(runner, actual.second == y);
    }

    template <typename Input>
    void add_movement_tests(TestRunner& runner, const std::string& prefix) {
        runner.run(prefix + "multiple full buffers publish once at the next boundary", [&] {
            Access::reset<Input>();
            Access::Batch<Input> batch{};
            int reads = 0;
            const bool drained = Access::drain<Input>(batch, [&](PRAWINPUT buffer, PUINT size, UINT header_size) -> UINT {
                TEST_EXPECT(runner, *size == 64 * sizeof(RAWINPUT));
                TEST_EXPECT(runner, header_size == sizeof(RAWINPUTHEADER));
                TEST_EXPECT(runner, reinterpret_cast<uintptr_t>(buffer) % 8 == 0);
                TEST_EXPECT(runner, Access::publications<Input>() == 0);
                expect_sample<Input>(runner, 0, 0);
                if (reads++ == 3) return 0;
                for (size_t i = 0; i < 64; ++i) buffer[i] = movement(3, -2);
                return 64;
            });
            TEST_EXPECT(runner, drained);
            TEST_EXPECT(runner, reads == 4);
            expect_sample<Input>(runner, 0, 0);
            Access::publish<Input>(batch);
            TEST_EXPECT(runner, Access::publications<Input>() == 1);
            TEST_EXPECT(runner, batch.x == 0 && batch.y == 0);
            expect_sample<Input>(runner, 576, -384);
            Access::publish<Input>(batch);
            TEST_EXPECT(runner, Access::publications<Input>() == 1);
            expect_sample<Input>(runner, 0, 0);
        });

        runner.run(prefix + "empty filtered and cancelled batches skip publication", [&] {
            Access::reset<Input>();
            Access::Batch<Input> batch{};
            Access::publish<Input>(batch);
            Access::process<Input>(movement(100, 200, MOUSE_MOVE_ABSOLUTE | MOUSE_VIRTUAL_DESKTOP), batch);
            Access::process<Input>(movement(0, 0), batch);
            RAWINPUT non_mouse{};
            non_mouse.header.dwType = RIM_TYPEHID;
            Access::process<Input>(non_mouse, batch);
            Access::process<Input>(movement(-37, 91), batch);
            Access::process<Input>(movement(37, -91), batch);
            Access::publish<Input>(batch);
            TEST_EXPECT(runner, Access::publications<Input>() == 0);
            expect_sample<Input>(runner, 0, 0);
            Access::process<Input>(movement(-7, 0), batch);
            Access::publish<Input>(batch);
            expect_sample<Input>(runner, -7, 0);
            Access::process<Input>(movement(0, 9), batch);
            Access::publish<Input>(batch);
            expect_sample<Input>(runner, 0, 9);
        });

        runner.run(prefix + "local and published sums wrap each axis independently", [&] {
            Access::reset<Input>();
            Access::Batch<Input> batch{};
            constexpr auto high = (std::numeric_limits<int32_t>::max)();
            constexpr auto low = (std::numeric_limits<int32_t>::min)();
            Access::process<Input>(movement(high, low), batch);
            Access::process<Input>(movement(1, -1), batch);
            Access::publish<Input>(batch);
            expect_sample<Input>(runner, low, high);
            Access::process<Input>(movement(-1, 17), batch);
            Access::publish<Input>(batch);
            Access::process<Input>(movement(1, 0), batch);
            Access::publish<Input>(batch);
            expect_sample<Input>(runner, 0, 17);
            Access::process<Input>(movement(high, low), batch);
            Access::publish<Input>(batch);
            Access::process<Input>(movement(1, -1), batch);
            Access::publish<Input>(batch);
            expect_sample<Input>(runner, low, high);
            Access::process<Input>(movement(-1, -1), batch);
            Access::process<Input>(movement(1, 1), batch);
            const size_t before = Access::publications<Input>();
            Access::publish<Input>(batch);
            TEST_EXPECT(runner, Access::publications<Input>() == before);
            expect_sample<Input>(runner, 0, 0);
        });

        // Exercise the actual loop on this ordinary test thread. Only the buffer
        // reader is replaced; WM_QUIT uses and is consumed from the real message queue.
        runner.run(prefix + "partial drain survives error and publishes on the next tick", [&] {
            Access::reset<Input>();
            int reads = 0;
            Access::loop<Input>([&](PRAWINPUT buffer, PUINT, UINT) -> UINT {
                switch (reads++) {
                case 0:
                    buffer[0] = movement(12, -5);
                    return 1;
                case 1:
                    buffer[0] = movement(-2, 8);
                    return 1;
                case 2:
                    expect_sample<Input>(runner, 0, 0);
                    TEST_EXPECT(runner, Access::publications<Input>() == 0);
                    return static_cast<UINT>(-1);
                default:
                    expect_sample<Input>(runner, 10, 3);
                    TEST_EXPECT(runner, Access::publications<Input>() == 1);
                    PostQuitMessage(0);
                    return 0;
                }
            });
            TEST_EXPECT(runner, reads == 4);
            expect_sample<Input>(runner, 0, 0);
        });

        runner.run(prefix + "normal ticks publish while WM_QUIT discards the last batch", [&] {
            Access::reset<Input>();
            int reads = 0;
            Access::loop<Input>([&](PRAWINPUT buffer, PUINT, UINT) -> UINT {
                switch (reads++) {
                case 0:
                    buffer[0] = movement(5, -9);
                    return 1;
                case 1:
                    expect_sample<Input>(runner, 0, 0);
                    return 0;
                case 2:
                    TEST_EXPECT(runner, Access::publications<Input>() == 1);
                    buffer[0] = movement(700, 800);
                    return 1;
                default:
                    PostQuitMessage(0);
                    return 0;
                }
            });
            TEST_EXPECT(runner, reads == 4);
            TEST_EXPECT(runner, Access::publications<Input>() == 1);
            expect_sample<Input>(runner, 5, -9);
            expect_sample<Input>(runner, 0, 0);
        });

        runner.run(prefix + "concurrent batch CAS and snapshots preserve totals and axis pairing", [&] {
            Access::reset<Input>();
            constexpr size_t batches = 20000;
            std::atomic<bool> start{false};
            std::atomic<bool> done{false};
            std::jthread producer([&] {
                while (!start.load(std::memory_order_acquire)) std::this_thread::yield();
                Access::Batch<Input> batch{};
                for (size_t tick = 0; tick <= batches; ++tick) {
                    Access::publish<Input>(batch);
                    if (tick == batches) break;
                    for (int i = 0; i < 17; ++i) {
                        Access::process<Input>(movement(i % 2 ? -2 : 3, i % 3 ? -1 : 4), batch);
                    }
                }
                done.store(true, std::memory_order_release);
            });
            int64_t x = 0;
            int64_t y = 0;
            bool paired = true;
            auto consume = [&] {
                const auto sample = Input::sample();
                x += sample.first;
                y += sample.second;
                paired = paired && static_cast<int64_t>(sample.first) * 13 == static_cast<int64_t>(sample.second) * 11;
            };
            start.store(true, std::memory_order_release);
            while (!done.load(std::memory_order_acquire)) consume();
            producer.join();
            consume();
            TEST_EXPECT(runner, paired);
            TEST_EXPECT(runner, x == static_cast<int64_t>(batches) * 11);
            TEST_EXPECT(runner, y == static_cast<int64_t>(batches) * 13);
            TEST_EXPECT(runner, Access::publications<Input>() == batches);
            expect_sample<Input>(runner, 0, 0);
        });
    }
}

void add_input_batch_tests(TestRunner& runner) {
    using Input = rawinput::LowLatencyInput;
    add_movement_tests<Input>(runner, "combined input: ");
    add_movement_tests<rawinput::LowLatencyMouseMov>(runner, "legacy mouse: ");
    runner.run("mouse buttons remain immediate and deduplicated before movement publication", [&] {
        Access::reset<Input>();
        Access::Batch<Input> batch{};
        constexpr USHORT downs = RI_MOUSE_LEFT_BUTTON_DOWN | RI_MOUSE_RIGHT_BUTTON_DOWN |
            RI_MOUSE_MIDDLE_BUTTON_DOWN | RI_MOUSE_BUTTON_4_DOWN | RI_MOUSE_BUTTON_5_DOWN;
        constexpr USHORT ups = RI_MOUSE_LEFT_BUTTON_UP | RI_MOUSE_RIGHT_BUTTON_UP |
            RI_MOUSE_MIDDLE_BUTTON_UP | RI_MOUSE_BUTTON_4_UP | RI_MOUSE_BUTTON_5_UP;
        constexpr std::array<uint16_t, 5> keys{VK_LBUTTON, VK_RBUTTON, VK_MBUTTON, VK_XBUTTON1, VK_XBUTTON2};
        Access::process<Input>(movement(7, -4, MOUSE_MOVE_RELATIVE, downs), batch);
        Access::process<Input>(movement(7, -4, MOUSE_MOVE_RELATIVE, downs), batch);
        RAWINPUT shared_key{};
        shared_key.header.dwType = RIM_TYPEKEYBOARD;
        shared_key.data.keyboard.VKey = VK_LBUTTON;
        Access::process<Input>(shared_key, batch);
        Input::KeyEvent events[16]{};
        TEST_EXPECT(runner, Input::pop_events(events) == keys.size());
        for (size_t i = 0; i < keys.size(); ++i) {
            TEST_EXPECT(runner, events[i].vkey == keys[i] && events[i].down == 1);
            TEST_EXPECT(runner, Input::is_keydown(keys[i]));
        }
        Access::process<Input>(movement(900, 900, MOUSE_MOVE_ABSOLUTE, ups), batch);
        TEST_EXPECT(runner, Input::pop_events(events) == keys.size());
        for (size_t i = 0; i < keys.size(); ++i) {
            TEST_EXPECT(runner, events[i].vkey == keys[i] && events[i].down == 0 && events[i].flags == RI_KEY_BREAK);
            TEST_EXPECT(runner, !Input::is_keydown(keys[i]));
        }
        Access::process<Input>(movement(0, 0, MOUSE_MOVE_RELATIVE, RI_MOUSE_WHEEL | RI_MOUSE_HWHEEL), batch);
        TEST_EXPECT(runner, Input::pop_events(events) == 0);
        expect_sample<Input>(runner, 0, 0);
        TEST_EXPECT(runner, Access::publications<Input>() == 0);
        Access::publish<Input>(batch);
        expect_sample<Input>(runner, 14, -8);
        Access::reset<Input>();
    });
}
}
