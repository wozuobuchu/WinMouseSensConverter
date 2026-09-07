#include "test_groups.hpp"
#include "ui_view.hpp"
#include <vector>

namespace automatic_test {
namespace {
using namespace d2dui;
using Kind = MouseInputKind;
using Event = D2duiMouseEvent;
class Probe final : public D2duiComponentsBase {
public:
    Probe() { bounds_ = {0, 0, 100, 100}; }
    const D2D1_RECT_F& get_bounds() const noexcept override { return bounds_; }
    void resize(const D2D1_RECT_F& bounds, float) noexcept override { bounds_ = bounds; }
    HRESULT draw(D2duiContext&) noexcept override { return S_OK; }
};
template<Event E> void count(Probe& p, int& value) {
    p.register_mouse_event_handler<E>([&](const auto&) { ++value; });
}
}
void add_mouse_dispatch_tests(TestRunner& runner) {
    runner.run("mouse transitions use event position and ticks alone repeat", [&] {
        D2duiSystemRender render;
        auto a = std::make_shared<Probe>();
        auto b = std::make_shared<Probe>();
        b->resize({100, 0, 200, 100}, 1);
        render.register_component(a); render.register_component(b);
        int enters = 0, repeats = 0, releases = 0, wrong = 0, hover = 0;
        count<Event::MOUSE_LEFT_CLICK_ENTER>(*a, enters);
        count<Event::MOUSE_LEFT_CLICK_ON>(*a, repeats);
        count<Event::MOUSE_HOVER_ON>(*a, hover);
        count<Event::MOUSE_LEFT_CLICK_ENTER>(*b, wrong);
        a->register_mouse_event_handler<Event::MOUSE_LEFT_CLICK_LEAVE>([&](const auto& p) {
            ++releases; TEST_EXPECT_NEAR(runner, p.position.x, 150, 0);
        });
        TEST_EXPECT(runner, render.dispatch_mouse_events({Kind::down, {50, 50}, VK_LBUTTON}));
        render.dispatch_mouse_events({Kind::tick});
        render.dispatch_mouse_events({Kind::down, {50, 50}, VK_RBUTTON});
        TEST_EXPECT(runner, repeats == 1 && hover == 1);
        render.dispatch_mouse_events({Kind::move, {150, 50}});
        render.dispatch_mouse_events({Kind::up, {150, 50}, VK_LBUTTON});
        TEST_EXPECT(runner, enters == 1 && releases == 1 && wrong == 0);
        render.dispatch_mouse_events({Kind::up, {150, 50}, VK_RBUTTON});
        render.dispatch_mouse_events({Kind::down, {-10, 0}, VK_LBUTTON});
        render.dispatch_mouse_events({Kind::move, {50, 50}});
        render.dispatch_mouse_events({Kind::tick});
        TEST_EXPECT(runner, enters == 1 && repeats == 1);
    });
    runner.run("all five buttons cancel independently without releases", [&] {
        D2duiSystemRender render;
        auto p = std::make_shared<Probe>(); render.register_component(p);
        std::vector<int16_t> canceled;
        int releases = 0, leave = 0;
        p->register_mouse_event_handler<Event::MOUSE_CANCEL>([&](const auto& e) { canceled.push_back(e.vk); });
        count<Event::MOUSE_LEFT_CLICK_LEAVE>(*p, releases);
        count<Event::MOUSE_HOVER_LEAVE>(*p, leave);
        for (int16_t key : std::array<int16_t, 5>{VK_LBUTTON, VK_RBUTTON, VK_MBUTTON, VK_XBUTTON1, VK_XBUTTON2})
            render.dispatch_mouse_events({Kind::down, {20, 20}, key});
        TEST_EXPECT(runner, render.cancel_mouse_events());
        render.cancel_mouse_events();
        render.dispatch_mouse_events({Kind::up, {20, 20}, VK_LBUTTON});
        TEST_EXPECT(runner, canceled == (std::vector<int16_t>{VK_LBUTTON, VK_RBUTTON, VK_MBUTTON, VK_XBUTTON1, VK_XBUTTON2}));
        TEST_EXPECT(runner, releases == 0 && leave == 1);
    });
    runner.run("leave stops hover but preserves drag outside", [&] {
        D2duiSystemRender render;
        auto p = std::make_shared<Probe>(); render.register_component(p);
        int hover = 0, hold = 0, up = 0;
        count<Event::MOUSE_HOVER_ON>(*p, hover);
        count<Event::MOUSE_LEFT_CLICK_ON>(*p, hold);
        count<Event::MOUSE_LEFT_CLICK_LEAVE>(*p, up);
        render.dispatch_mouse_events({Kind::down, {20, 20}, VK_LBUTTON});
        render.dispatch_mouse_events({Kind::leave});
        render.dispatch_mouse_events({Kind::tick});
        render.dispatch_mouse_events({Kind::up, {-50, -50}, VK_LBUTTON});
        TEST_EXPECT(runner, hover == 0 && hold == 1 && up == 1);
    });
    runner.run("callbacks safely remove self or later entries and grow queue", [&] {
        for (int action = 0; action < 4; ++action) {
            D2duiSystemRender render;
            auto a = std::make_shared<Probe>(); auto b = std::make_shared<Probe>();
            render.register_component(a); render.register_component(b);
            int b_calls = 0, added_calls = 0, a_down = 0;
            count<Event::MOUSE_LEFT_CLICK_ENTER>(*a, a_down);
            count<Event::MOUSE_HOVER_ENTER>(*b, b_calls);
            a->register_mouse_event_handler<Event::MOUSE_HOVER_ENTER>([&](const auto&) {
                if (action == 0) render.unregister_component(a);
                if (action == 1) render.unregister_component(b);
                if (action == 2) render.clear();
                if (action == 3) for (int i = 0; i < 100; ++i) {
                    auto added = std::make_shared<Probe>();
                    count<Event::MOUSE_HOVER_ENTER>(*added, added_calls);
                    render.register_component(added);
                }
            });
            render.dispatch_mouse_events({Kind::down, {20, 20}, VK_LBUTTON});
            TEST_EXPECT(runner, b_calls == ((action == 0 || action == 3) ? 1 : 0));
            TEST_EXPECT(runner, a_down == ((action == 1 || action == 3) ? 1 : 0));
            TEST_EXPECT(runner, added_calls == 0);
            render.dispatch_mouse_events({Kind::tick});
            TEST_EXPECT(runner, added_calls == (action == 3 ? 100 : 0));
        }
    });
    runner.run("nested input is FIFO and nested cancellation aborts dispatch", [&] {
        D2duiSystemRender render;
        auto a = std::make_shared<Probe>(); auto b = std::make_shared<Probe>();
        render.register_component(a); render.register_component(b);
        int cancels = 0, b_down = 0, releases = 0;
        count<Event::MOUSE_CANCEL>(*a, cancels);
        count<Event::MOUSE_LEFT_CLICK_ENTER>(*b, b_down);
        a->register_mouse_event_handler<Event::MOUSE_LEFT_CLICK_ENTER>([&](const auto&) {
            render.dispatch_mouse_events({Kind::up, {20, 20}, VK_LBUTTON});
            render.cancel_mouse_events();
        });
        count<Event::MOUSE_LEFT_CLICK_LEAVE>(*a, releases);
        render.dispatch_mouse_events({Kind::down, {20, 20}, VK_LBUTTON});
        TEST_EXPECT(runner, cancels == 1 && b_down == 0 && releases == 0);
        a->register_mouse_event_handler<Event::MOUSE_LEFT_CLICK_ENTER>([&](const auto&) {
            render.dispatch_mouse_events({Kind::up, {20, 20}, VK_LBUTTON});
        });
        render.dispatch_mouse_events({Kind::down, {20, 20}, VK_LBUTTON});
        TEST_EXPECT(runner, releases == 1 && b_down == 1);
    });
    runner.run("self removal keeps sole-owned component alive during callback", [&] {
        D2duiSystemRender render;
        auto p = std::make_shared<Probe>();
        std::weak_ptr<Probe> observer = p;
        bool alive = false;
        p->register_mouse_event_handler<Event::MOUSE_HOVER_ENTER>([&](const auto&) {
            render.unregister_component(observer.lock());
            alive = !observer.expired();
        });
        render.register_component(p);
        p.reset();
        render.dispatch_mouse_events({Kind::move, {20, 20}});
        TEST_EXPECT(runner, alive && observer.expired() && render.size() == 0);
    });
    runner.run("nested view input preserves common and mode event order", [&] {
        ui::view::MainView view;
        auto common = std::make_shared<Probe>();
        auto mode = std::make_shared<Probe>();
        view.common_render().register_component(common);
        view.measurement_render().register_component(mode);
        std::vector<int> order;
        const auto measurement = config::AppMode::measurement;
        common->register_mouse_event_handler<Event::MOUSE_LEFT_CLICK_ENTER>([&](const auto&) {
            order.push_back(1);
            view.dispatch_mouse_events(measurement, {Kind::up, {20, 20}, VK_LBUTTON});
        });
        mode->register_mouse_event_handler<Event::MOUSE_LEFT_CLICK_ENTER>([&](const auto&) { order.push_back(2); });
        common->register_mouse_event_handler<Event::MOUSE_LEFT_CLICK_LEAVE>([&](const auto&) { order.push_back(3); });
        mode->register_mouse_event_handler<Event::MOUSE_LEFT_CLICK_LEAVE>([&](const auto&) { order.push_back(4); });
        view.dispatch_mouse_events(measurement, {Kind::down, {20, 20}, VK_LBUTTON});
        TEST_EXPECT(runner, order == (std::vector<int>{1, 2, 3, 4}));
    });    runner.run("stopped view processes input and mode cancellation without rendering", [&] {
        ui::view::MainView view;
        view.update_layout(800, 450);
        const auto bounds = view.measurement_grid().get_bounds();
        const D2D1_POINT_2F position{bounds.left + 1, bounds.top + 1};
        int down = 0, cancel = 0, hold = 0, calibration_down = 0;
        view.measurement_grid().register_mouse_event_handler<Event::MOUSE_LEFT_CLICK_ENTER>([&](const auto&) { ++down; });
        view.measurement_grid().register_mouse_event_handler<Event::MOUSE_CANCEL>([&](const auto&) { ++cancel; });
        view.measurement_grid().register_mouse_event_handler<Event::MOUSE_LEFT_CLICK_ON>([&](const auto&) { ++hold; });
        view.calibration_grid().register_mouse_event_handler<Event::MOUSE_LEFT_CLICK_ENTER>([&](const auto&) { ++calibration_down; });
        const auto measurement = config::AppMode::measurement, calibration = config::AppMode::calibration;
        TEST_EXPECT(runner, view.dispatch_mouse_events(measurement, {Kind::down, position, VK_LBUTTON}));
        TEST_EXPECT(runner, view.dispatch_mouse_events(measurement, {Kind::tick}));
        view.cancel_mouse_events(measurement, false);
        view.dispatch_mouse_events(calibration, {Kind::move, position});
        view.dispatch_mouse_events(calibration, {Kind::tick});
        view.dispatch_mouse_events(calibration, {Kind::up, position, VK_LBUTTON});
        TEST_EXPECT(runner, down == 1 && hold == 1 && cancel == 1 && calibration_down == 0);
        view.dispatch_mouse_events(calibration, {Kind::down, position, VK_LBUTTON});
        TEST_EXPECT(runner, calibration_down == 1);
    });
}
}
