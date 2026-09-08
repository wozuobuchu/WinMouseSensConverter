#include "test_groups.hpp"
#include "mouse_test_window.hpp"
#include <vector>

namespace automatic_test {
namespace {
using namespace d2dui;
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
        MouseTestWindow window;
        auto render = window.render;
        auto a = std::make_shared<Probe>();
        auto b = std::make_shared<Probe>();
        b->resize({100, 0, 200, 100}, 1);
        render->register_component(a); render->register_component(b);
        int enters = 0, repeats = 0, releases = 0, wrong = 0, hover = 0;
        count<Event::MOUSE_LEFT_CLICK_ENTER>(*a, enters);
        count<Event::MOUSE_LEFT_CLICK_ON>(*a, repeats);
        count<Event::MOUSE_HOVER_ON>(*a, hover);
        count<Event::MOUSE_LEFT_CLICK_ENTER>(*b, wrong);
        a->register_mouse_event_handler<Event::MOUSE_LEFT_CLICK_LEAVE>([&](const auto& p) {
            ++releases; TEST_EXPECT_NEAR(runner, p.position.x, 150, 0);
        });
        TEST_EXPECT(runner, window.button(VK_LBUTTON, true, 50, 50));
        window.analyser->tick();
        window.button(VK_RBUTTON, true, 50, 50);
        TEST_EXPECT(runner, repeats == 1 && hover == 1);
        window.message(WM_MOUSEMOVE, 0, MAKELPARAM(150, 50));
        window.button(VK_LBUTTON, false, 150, 50);
        TEST_EXPECT(runner, enters == 1 && releases == 1 && wrong == 0);
        window.button(VK_RBUTTON, false, 150, 50);
        window.button(VK_LBUTTON, true, -10, 0);
        window.message(WM_MOUSEMOVE, 0, MAKELPARAM(50, 50));
        window.analyser->tick();
        TEST_EXPECT(runner, enters == 1 && repeats == 1);
    });
    runner.run("hover uses half-open Direct2D rectangles", [&] {
        MouseTestWindow window;
        auto p = std::make_shared<Probe>();
        p->resize({-10.0f, -20.0f, 30.0f, 40.0f}, 1);
        window.render->register_component(p);
        int enter = 0, leave = 0;
        count<Event::MOUSE_HOVER_ENTER>(*p, enter);
        count<Event::MOUSE_HOVER_LEAVE>(*p, leave);
        auto hover = [&](short x, short y) { window.message(WM_MOUSEMOVE, 0, MAKELPARAM(x, y)); };
        hover(-10, -20);
        hover(0, 0);
        TEST_EXPECT(runner, enter == 1 && leave == 0);
        hover(30, 0);
        TEST_EXPECT(runner, enter == 1 && leave == 1);
        hover(0, 40);
        hover(-11, 0);
        hover(0, -21);
        hover(31, 0);
        hover(0, 41);
        TEST_EXPECT(runner, enter == 1 && leave == 1);
        hover(29, 39);
        TEST_EXPECT(runner, enter == 2 && leave == 1);
    });
    runner.run("all five buttons cancel independently without releases", [&] {
        MouseTestWindow window;
        auto render = window.render;
        auto p = std::make_shared<Probe>(); render->register_component(p);
        std::vector<int16_t> canceled;
        int releases = 0, leave = 0;
        p->register_mouse_event_handler<Event::MOUSE_CANCEL>([&](const auto& e) { canceled.push_back(e.vk); });
        count<Event::MOUSE_LEFT_CLICK_LEAVE>(*p, releases);
        count<Event::MOUSE_HOVER_LEAVE>(*p, leave);
        for (int16_t key : std::array<int16_t, 5>{VK_LBUTTON, VK_RBUTTON, VK_MBUTTON, VK_XBUTTON1, VK_XBUTTON2})
            window.button(key, true, 20, 20);
        TEST_EXPECT(runner, window.message(WM_CANCELMODE));
        window.message(WM_CANCELMODE);
        window.button(VK_LBUTTON, false, 20, 20);
        TEST_EXPECT(runner, canceled == (std::vector<int16_t>{VK_LBUTTON, VK_RBUTTON, VK_MBUTTON, VK_XBUTTON1, VK_XBUTTON2}));
        TEST_EXPECT(runner, releases == 0 && leave == 1);
    });
    runner.run("leave stops hover but preserves drag outside", [&] {
        MouseTestWindow window;
        auto render = window.render;
        auto p = std::make_shared<Probe>(); render->register_component(p);
        int hover = 0, hold = 0, up = 0;
        count<Event::MOUSE_HOVER_ON>(*p, hover);
        count<Event::MOUSE_LEFT_CLICK_ON>(*p, hold);
        count<Event::MOUSE_LEFT_CLICK_LEAVE>(*p, up);
        window.button(VK_LBUTTON, true, 20, 20);
        window.message(WM_MOUSELEAVE);
        window.analyser->tick();
        window.button(VK_LBUTTON, false, -50, -50);
        TEST_EXPECT(runner, hover == 0 && hold == 1 && up == 1);
    });
    runner.run("callbacks safely remove self or later entries and grow queue", [&] {
        for (int action = 0; action < 4; ++action) {
            MouseTestWindow window;
            auto render = window.render;
            auto a = std::make_shared<Probe>(); auto b = std::make_shared<Probe>();
            render->register_component(a); render->register_component(b);
            int b_calls = 0, added_calls = 0, a_down = 0;
            count<Event::MOUSE_LEFT_CLICK_ENTER>(*a, a_down);
            count<Event::MOUSE_HOVER_ENTER>(*b, b_calls);
            a->register_mouse_event_handler<Event::MOUSE_HOVER_ENTER>([&](const auto&) {
                if (action == 0) render->unregister_component(a);
                if (action == 1) render->unregister_component(b);
                if (action == 2) render->clear();
                if (action == 3) for (int i = 0; i < 100; ++i) {
                    auto added = std::make_shared<Probe>();
                    count<Event::MOUSE_HOVER_ENTER>(*added, added_calls);
                    render->register_component(added);
                }
            });
            window.button(VK_LBUTTON, true, 20, 20);
            TEST_EXPECT(runner, b_calls == ((action == 0 || action == 3) ? 1 : 0));
            TEST_EXPECT(runner, a_down == ((action == 1 || action == 3) ? 1 : 0));
            TEST_EXPECT(runner, added_calls == 0);
            window.analyser->tick();
            TEST_EXPECT(runner, added_calls == (action == 3 ? 100 : 0));
        }
    });
    runner.run("nested input is FIFO and nested cancellation aborts dispatch", [&] {
        MouseTestWindow window;
        auto render = window.render;
        auto a = std::make_shared<Probe>(); auto b = std::make_shared<Probe>();
        render->register_component(a); render->register_component(b);
        int cancels = 0, b_down = 0, releases = 0;
        count<Event::MOUSE_CANCEL>(*a, cancels);
        count<Event::MOUSE_LEFT_CLICK_ENTER>(*b, b_down);
        a->register_mouse_event_handler<Event::MOUSE_LEFT_CLICK_ENTER>([&](const auto&) {
            window.button(VK_LBUTTON, false, 20, 20);
            window.message(WM_CANCELMODE);
        });
        count<Event::MOUSE_LEFT_CLICK_LEAVE>(*a, releases);
        window.button(VK_LBUTTON, true, 20, 20);
        TEST_EXPECT(runner, cancels == 1 && b_down == 0 && releases == 0);
        a->register_mouse_event_handler<Event::MOUSE_LEFT_CLICK_ENTER>([&](const auto&) {
            window.button(VK_LBUTTON, false, 20, 20);
        });
        window.button(VK_LBUTTON, true, 20, 20);
        TEST_EXPECT(runner, releases == 1 && b_down == 1);
    });
    runner.run("self removal keeps sole-owned component alive during callback", [&] {
        MouseTestWindow window;
        auto render = window.render;
        auto p = std::make_shared<Probe>();
        std::weak_ptr<Probe> observer = p;
        bool alive = false;
        p->register_mouse_event_handler<Event::MOUSE_HOVER_ENTER>([&](const auto&) {
            render->unregister_component(observer.lock());
            alive = !observer.expired();
        });
        render->register_component(p);
        p.reset();
        window.message(WM_MOUSEMOVE, 0, MAKELPARAM(20, 20));
        TEST_EXPECT(runner, alive && observer.expired() && render->size() == 0);
    });

    runner.run("nested input preserves ordering across shared renderers", [&] {
        MouseTestWindow window;
        auto page = std::make_shared<D2duiSystemRender>();
        auto common = std::make_shared<Probe>();
        auto mode = std::make_shared<Probe>();
        window.render->register_component(common);
        page->register_component(mode);
        window.analyser->set_renderers({window.render, page});
        std::vector<int> order;
        common->register_mouse_event_handler<Event::MOUSE_LEFT_CLICK_ENTER>([&](const auto&) {
            order.push_back(1);
            window.button(VK_LBUTTON, false);
        });
        mode->register_mouse_event_handler<Event::MOUSE_LEFT_CLICK_ENTER>([&](const auto&) { order.push_back(2); });
        common->register_mouse_event_handler<Event::MOUSE_LEFT_CLICK_LEAVE>([&](const auto&) { order.push_back(3); });
        mode->register_mouse_event_handler<Event::MOUSE_LEFT_CLICK_LEAVE>([&](const auto&) { order.push_back(4); });
        TEST_EXPECT(runner, window.button(VK_LBUTTON, true));
        TEST_EXPECT(runner, order == (std::vector<int>{1, 2, 3, 4}));
        TEST_EXPECT(runner, GetCapture() != window.hwnd);
    });
    runner.run("queue switch retains common capture and physical baseline", [&] {
        MouseTestWindow window;
        auto previous = std::make_shared<D2duiSystemRender>();
        auto next = std::make_shared<D2duiSystemRender>();
        auto common = std::make_shared<Probe>();
        auto old_component = std::make_shared<Probe>();
        auto new_component = std::make_shared<Probe>();
        window.render->register_component(common);
        previous->register_component(old_component);
        next->register_component(new_component);
        window.analyser->set_renderers({window.render, previous});
        int common_hold = 0, common_cancel = 0, old_cancel = 0, new_down = 0, new_up = 0;
        count<Event::MOUSE_LEFT_CLICK_ON>(*common, common_hold);
        count<Event::MOUSE_CANCEL>(*common, common_cancel);
        count<Event::MOUSE_CANCEL>(*old_component, old_cancel);
        count<Event::MOUSE_LEFT_CLICK_ENTER>(*new_component, new_down);
        count<Event::MOUSE_LEFT_CLICK_LEAVE>(*new_component, new_up);
        window.button(VK_LBUTTON, true);
        TEST_EXPECT(runner, window.analyser->set_renderers({window.render, next}));
        window.button(VK_LBUTTON, true);
        window.analyser->tick();
        TEST_EXPECT(runner, common_hold == 1 && common_cancel == 0 && old_cancel == 1 && new_down == 0);
        window.button(VK_LBUTTON, false);
        TEST_EXPECT(runner, new_up == 0);
        window.button(VK_LBUTTON, true);
        TEST_EXPECT(runner, new_down == 1);
    });
    runner.run("callback queue replacement aborts old pass and pending input", [&] {
        MouseTestWindow window;
        auto next = std::make_shared<D2duiSystemRender>();
        auto first = std::make_shared<Probe>();
        auto later = std::make_shared<Probe>();
        auto added = std::make_shared<Probe>();
        window.render->register_component(first);
        window.render->register_component(later);
        next->register_component(added);
        std::weak_ptr<D2duiSystemRender> old = window.render;
        int canceled = 0, later_down = 0, new_down = 0, released = 0;
        count<Event::MOUSE_CANCEL>(*first, canceled);
        count<Event::MOUSE_LEFT_CLICK_ENTER>(*later, later_down);
        count<Event::MOUSE_LEFT_CLICK_ENTER>(*added, new_down);
        count<Event::MOUSE_LEFT_CLICK_LEAVE>(*first, released);
        bool alive_in_callback = false;
        first->register_mouse_event_handler<Event::MOUSE_LEFT_CLICK_ENTER>([&](const auto&) {
            window.button(VK_LBUTTON, false);
            window.analyser->set_renderers({next});
            window.render.reset();
            alive_in_callback = !old.expired();
        });
        window.button(VK_LBUTTON, true);
        TEST_EXPECT(runner, alive_in_callback && old.expired());
        TEST_EXPECT(runner, canceled == 1 && later_down == 0 && new_down == 0 && released == 0);
        window.button(VK_LBUTTON, true);
        TEST_EXPECT(runner, new_down == 0);
        window.button(VK_LBUTTON, false);
        window.button(VK_LBUTTON, true);
        TEST_EXPECT(runner, new_down == 1);
    });
    runner.run("analyser shares renderer ownership and validates lists atomically", [&] {
        MouseTestWindow window;
        std::weak_ptr<D2duiSystemRender> observer = window.render;
        auto p = std::make_shared<Probe>();
        window.render->register_component(p);
        int down = 0;
        count<Event::MOUSE_LEFT_CLICK_ENTER>(*p, down);
        for (bool duplicate : {false, true}) {
            bool rejected = false;
            try {
                window.analyser->set_renderers(duplicate
                    ? std::vector<std::shared_ptr<D2duiSystemRender>>{window.render, window.render}
                    : std::vector<std::shared_ptr<D2duiSystemRender>>{window.render, nullptr});
            } catch (const std::invalid_argument&) { rejected = true; }
            TEST_EXPECT(runner, rejected);
        }
        window.render.reset();
        TEST_EXPECT(runner, !observer.expired());
        window.button(VK_LBUTTON, true);
        TEST_EXPECT(runner, down == 1);
        window.analyser->set_renderers({});
        TEST_EXPECT(runner, observer.expired());
        window.button(VK_LBUTTON, false);
        TEST_EXPECT(runner, GetCapture() != window.hwnd);
    });
    runner.run("duplicate component registrations have independent interaction state", [&] {
        MouseTestWindow window;
        auto p = std::make_shared<Probe>();
        window.render->register_component(p);
        window.render->register_component(p);
        int down = 0, cancel = 0;
        count<Event::MOUSE_LEFT_CLICK_ENTER>(*p, down);
        count<Event::MOUSE_CANCEL>(*p, cancel);
        window.button(VK_LBUTTON, true);
        TEST_EXPECT(runner, down == 2);
        window.render->unregister_component(p);
        window.message(WM_CANCELMODE);
        TEST_EXPECT(runner, cancel == 1);
    });
    runner.run("analyser states are isolated even when renderers are shared", [&] {
        MouseTestWindow first;
        MouseTestWindow second;
        second.analyser->set_renderers({first.render});
        auto p = std::make_shared<Probe>();
        first.render->register_component(p);
        int enter = 0, leave = 0;
        count<Event::MOUSE_HOVER_ENTER>(*p, enter);
        count<Event::MOUSE_HOVER_LEAVE>(*p, leave);
        first.message(WM_MOUSEMOVE, 0, MAKELPARAM(20, 20));
        second.message(WM_MOUSEMOVE, 0, MAKELPARAM(20, 20));
        first.message(WM_MOUSELEAVE);
        second.analyser->tick();
        TEST_EXPECT(runner, enter == 2 && leave == 1);
    });
    runner.run("additions to later queues wait until the next event", [&] {
        MouseTestWindow window;
        auto page = std::make_shared<D2duiSystemRender>();
        window.analyser->set_renderers({window.render, page});
        auto first = std::make_shared<Probe>();
        auto added = std::make_shared<Probe>();
        window.render->register_component(first);
        int enter = 0;
        count<Event::MOUSE_HOVER_ENTER>(*added, enter);
        first->register_mouse_event_handler<Event::MOUSE_HOVER_ENTER>([&](const auto&) { page->register_component(added); });
        window.message(WM_MOUSEMOVE, 0, MAKELPARAM(20, 20));
        TEST_EXPECT(runner, enter == 0);
        window.analyser->tick();
        TEST_EXPECT(runner, enter == 1);
    });
    runner.run("DPI messages update signed message coordinates without canceling", [&] {
        for (UINT dpi : {96u, 144u, 192u}) {
            MouseTestWindow window;
            auto p = std::make_shared<Probe>();
            window.render->register_component(p);
            int down = 0, up = 0, cancel = 0;
            count<Event::MOUSE_CANCEL>(*p, cancel);
            p->register_mouse_event_handler<Event::MOUSE_X2_CLICK_ENTER>([&](const auto& e) {
                ++down;
                TEST_EXPECT_NEAR(runner, e.position.x, 30.0f, 0.001f);
                TEST_EXPECT(runner, e.vk == VK_XBUTTON2 && e.down == 1);
            });
            p->register_mouse_event_handler<Event::MOUSE_X2_CLICK_LEAVE>([&](const auto& e) {
                ++up;
                TEST_EXPECT_NEAR(runner, e.position.x, -30.0f * 96.0f / static_cast<float>(dpi), 0.001f);
                TEST_EXPECT_NEAR(runner, e.position.y, -20.0f * 96.0f / static_cast<float>(dpi), 0.001f);
            });
            window.message(WM_DPICHANGED, MAKEWPARAM(dpi, dpi));
            window.button(VK_XBUTTON2, true, static_cast<short>(30 * dpi / 96), 20);
            window.message(WM_DPICHANGED, MAKEWPARAM(dpi, dpi));
            const auto result = window.analyser->process_window_message(WM_XBUTTONUP, MAKEWPARAM(0, XBUTTON2), MAKELPARAM(-30, -20));
            TEST_EXPECT(runner, result.consumed && result.result == TRUE && result.callbacks_invoked);
            TEST_EXPECT(runner, down == 1 && up == 1 && cancel == 0);
        }
    });
    runner.run("explicit DPI updates preserve capture and ignore zero", [&] {
        MouseTestWindow window;
        auto p = std::make_shared<Probe>();
        window.render->register_component(p);
        std::vector<float> positions;
        int down = 0, cancel = 0, up = 0;
        count<Event::MOUSE_LEFT_CLICK_ENTER>(*p, down);
        count<Event::MOUSE_CANCEL>(*p, cancel);
        p->register_mouse_event_handler<Event::MOUSE_LEFT_CLICK_ON>([&](const auto& e) { positions.push_back(e.position.x); });
        p->register_mouse_event_handler<Event::MOUSE_LEFT_CLICK_LEAVE>([&](const auto& e) {
            ++up;
            TEST_EXPECT_NEAR(runner, e.position.x, -30.0f, 0.001f);
        });
        window.button(VK_LBUTTON, true, 60, 20);
        for (UINT dpi : {144u, 192u, 96u}) {
            window.analyser->set_dpi(dpi);
            window.analyser->set_dpi(0);
            TEST_EXPECT(runner, cancel == 0 && up == 0 && GetCapture() == window.hwnd);
            window.message(WM_MOUSEMOVE, 0, MAKELPARAM(60, 20));
            window.analyser->tick();
        }
        TEST_EXPECT(runner, positions == (std::vector<float>{40, 30, 60}));
        window.button(VK_LBUTTON, false, -30, -20);
        TEST_EXPECT(runner, down == 1 && cancel == 0 && up == 1);
    });
    runner.run("PMv2 parent notifications query window DPI and preserve host handling", [&] {
        MouseTestWindow window;
        const UINT actual_dpi = GetDpiForWindow(window.hwnd);
        TEST_EXPECT(runner, actual_dpi != 0);
        auto p = std::make_shared<Probe>();
        p->resize({0, 0, 10000, 10000}, 1);
        window.render->register_component(p);
        int held = 0, cancel = 0;
        count<Event::MOUSE_CANCEL>(*p, cancel);
        p->register_mouse_event_handler<Event::MOUSE_LEFT_CLICK_ON>([&](const auto& e) {
            ++held;
            TEST_EXPECT_NEAR(runner, e.position.x, 60.0f * 96.0f / static_cast<float>(actual_dpi), 0.001f);
        });
        window.button(VK_LBUTTON, true);
        for (UINT message : {WM_DPICHANGED_BEFOREPARENT, WM_DPICHANGED_AFTERPARENT}) {
            window.analyser->set_dpi(actual_dpi * 2);
            const auto result = window.analyser->process_window_message(message, 0, 0);
            TEST_EXPECT(runner, !result.consumed && !result.callbacks_invoked && result.result == 0);
            TEST_EXPECT(runner, GetCapture() == window.hwnd && cancel == 0);
            window.message(WM_MOUSEMOVE, 0, MAKELPARAM(60, 20));
            window.analyser->tick();
        }
        TEST_EXPECT(runner, held == 2);
        window.button(VK_LBUTTON, false);
    });
    runner.run("DPI changes during callbacks preserve sampled input coordinates", [&] {
        MouseTestWindow window;
        auto first = std::make_shared<Probe>();
        auto second = std::make_shared<Probe>();
        window.render->register_component(first);
        window.render->register_component(second);
        std::vector<float> positions;
        first->register_mouse_event_handler<Event::MOUSE_LEFT_CLICK_ENTER>([&](const auto&) {
            window.analyser->set_dpi(192);
            window.button(VK_LBUTTON, false, 60, 20);
            window.analyser->set_dpi(144);
        });
        second->register_mouse_event_handler<Event::MOUSE_LEFT_CLICK_ENTER>([&](const auto& e) { positions.push_back(e.position.x); });
        second->register_mouse_event_handler<Event::MOUSE_LEFT_CLICK_LEAVE>([&](const auto& e) { positions.push_back(e.position.x); });
        second->register_mouse_event_handler<Event::MOUSE_HOVER_ON>([&](const auto& e) { positions.push_back(e.position.x); });
        window.button(VK_LBUTTON, true, 60, 20);
        window.analyser->tick();
        window.message(WM_MOUSEMOVE, 0, MAKELPARAM(60, 20));
        window.analyser->tick();
        TEST_EXPECT(runner, positions == (std::vector<float>{60, 30, 30, 40}));
    });
    runner.run("mouse results distinguish consumed messages from callbacks", [&] {
        MouseTestWindow window;
        const auto move = window.analyser->process_window_message(WM_MOUSEMOVE, 0, 0);
        TEST_EXPECT(runner, move.consumed && move.result == 0 && !move.callbacks_invoked);
        for (UINT msg : {WM_MOUSEWHEEL, WM_MOUSEHWHEEL, WM_PAINT, WM_TIMER}) {
            const auto result = window.analyser->process_window_message(msg, 0, 0);
            TEST_EXPECT(runner, !result.consumed && !result.callbacks_invoked);
        }
        auto p = std::make_shared<Probe>();
        window.render->register_component(p);
        int down = 0;
        count<Event::MOUSE_LEFT_CLICK_ENTER>(*p, down);
        window.message(WM_LBUTTONDBLCLK, MK_LBUTTON, MAKELPARAM(20, 20));
        window.message(WM_LBUTTONDBLCLK, MK_LBUTTON, MAKELPARAM(20, 20));
        window.button(VK_LBUTTON, true);
        TEST_EXPECT(runner, down == 1);
        window.button(VK_LBUTTON, false);
        window.message(WM_LBUTTONDBLCLK, MK_LBUTTON, MAKELPARAM(20, 20));
        TEST_EXPECT(runner, down == 2);
    });
    runner.run("capture lasts until final release without cancellation", [&] {
        MouseTestWindow window;
        auto p = std::make_shared<Probe>();
        window.render->register_component(p);
        int cancel = 0, up = 0;
        count<Event::MOUSE_CANCEL>(*p, cancel);
        count<Event::MOUSE_LEFT_CLICK_LEAVE>(*p, up);
        window.button(VK_LBUTTON, true);
        window.button(VK_RBUTTON, true);
        window.button(VK_LBUTTON, false, -20, -20);
        TEST_EXPECT(runner, GetCapture() == window.hwnd);
        window.button(VK_RBUTTON, false);
        TEST_EXPECT(runner, GetCapture() != window.hwnd && cancel == 0 && up == 1);
    });
    runner.run("lifecycle cancels without release and gates input until resumed", [&] {
        for (UINT msg : {WM_ACTIVATE, WM_CAPTURECHANGED, WM_CANCELMODE, WM_ENTERMENULOOP, WM_ENTERSIZEMOVE, WM_SIZE, WM_DESTROY, WM_NCDESTROY}) {
            MouseTestWindow window;
            auto p = std::make_shared<Probe>();
            window.render->register_component(p);
            int cancel = 0, up = 0, hold = 0, down = 0;
            count<Event::MOUSE_CANCEL>(*p, cancel);
            count<Event::MOUSE_LEFT_CLICK_LEAVE>(*p, up);
            count<Event::MOUSE_LEFT_CLICK_ON>(*p, hold);
            count<Event::MOUSE_LEFT_CLICK_ENTER>(*p, down);
            window.button(VK_LBUTTON, true);
            TEST_EXPECT(runner, window.message(msg, msg == WM_SIZE ? SIZE_MINIMIZED : 0));
            TEST_EXPECT(runner, cancel == 1 && up == 0 && GetCapture() != window.hwnd);
            window.analyser->tick();
            TEST_EXPECT(runner, hold == 0);
            const bool suspended = msg != WM_CAPTURECHANGED && msg != WM_CANCELMODE;
            if (suspended) {
                window.button(VK_LBUTTON, true);
                TEST_EXPECT(runner, down == 1);
            }
            if (msg == WM_ACTIVATE) window.message(WM_ACTIVATE, WA_ACTIVE);
            if (msg == WM_ENTERMENULOOP) window.message(WM_EXITMENULOOP);
            if (msg == WM_ENTERSIZEMOVE) window.message(WM_EXITSIZEMOVE);
            if (msg == WM_SIZE) window.message(WM_SIZE, SIZE_RESTORED);
            window.button(VK_LBUTTON, true);
            TEST_EXPECT(runner, down == (msg == WM_DESTROY || msg == WM_NCDESTROY ? 1 : 2));
        }
    });

    runner.run("queue replacement during release still relinquishes capture", [&] {
        MouseTestWindow window;
        auto next = std::make_shared<D2duiSystemRender>();
        auto p = std::make_shared<Probe>();
        window.render->register_component(p);
        window.render->register_component(std::make_shared<Probe>());
        int up = 0, cancel = 0;
        count<Event::MOUSE_CANCEL>(*p, cancel);
        p->register_mouse_event_handler<Event::MOUSE_LEFT_CLICK_LEAVE>([&](const auto&) {
            ++up;
            window.analyser->set_renderers({next});
        });
        window.button(VK_LBUTTON, true);
        window.button(VK_LBUTTON, false);
        TEST_EXPECT(runner, up == 1 && cancel == 0 && GetCapture() != window.hwnd);
    });
    runner.run("real capture transfer cancels the previous window", [&] {
        MouseTestWindow first;
        MouseTestWindow second;
        auto p = std::make_shared<Probe>();
        first.render->register_component(p);
        int cancel = 0, up = 0;
        count<Event::MOUSE_CANCEL>(*p, cancel);
        count<Event::MOUSE_LEFT_CLICK_LEAVE>(*p, up);
        first.button(VK_LBUTTON, true);
        second.button(VK_LBUTTON, true);
        TEST_EXPECT(runner, cancel == 1 && up == 0 && GetCapture() == second.hwnd);
    });
    runner.run("cancellation tolerates callbacks replacing all renderer owners", [&] {
        MouseTestWindow window;
        auto old_page = std::make_shared<D2duiSystemRender>();
        auto next = std::make_shared<D2duiSystemRender>();
        auto common = std::make_shared<Probe>();
        auto page = std::make_shared<Probe>();
        window.render->register_component(common);
        old_page->register_component(page);
        window.analyser->set_renderers({window.render, old_page});
        std::vector<int> order;
        common->register_mouse_event_handler<Event::MOUSE_CANCEL>([&](const auto&) {
            order.push_back(1);
            window.analyser->set_renderers({next});
            window.render.reset();
            old_page.reset();
        });
        page->register_mouse_event_handler<Event::MOUSE_CANCEL>([&](const auto&) { order.push_back(2); });
        window.button(VK_LBUTTON, true);
        TEST_EXPECT(runner, window.message(WM_CANCELMODE));
        TEST_EXPECT(runner, order == (std::vector<int>{1, 2}) && GetCapture() != window.hwnd);
    });
    runner.run("destructor releases capture without invoking callbacks", [&] {
        MouseTestWindow window;
        auto p = std::make_shared<Probe>();
        window.render->register_component(p);
        int cancel = 0, leave = 0;
        count<Event::MOUSE_CANCEL>(*p, cancel);
        count<Event::MOUSE_HOVER_LEAVE>(*p, leave);
        window.button(VK_LBUTTON, true);
        window.analyser.reset();
        TEST_EXPECT(runner, GetCapture() != window.hwnd && cancel == 0 && leave == 0);
    });
}
}
