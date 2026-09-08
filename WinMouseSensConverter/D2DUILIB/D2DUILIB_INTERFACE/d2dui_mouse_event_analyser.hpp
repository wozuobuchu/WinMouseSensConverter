#pragma once

#include "d2dui_system_render.hpp"
#include "d2dui_cursor_pos.hpp"

#include <windowsx.h>
#include <array>
#include <bitset>
#include <deque>
#include <map>

namespace d2dui {

    // One analyser per window, used only on its UI thread. The host owns scheduling
    // and rendering, and must keep the analyser alive until callbacks return.
    class MouseEventAnalyser final {
    public:
        struct MessageResult {
            bool consumed = false;
            LRESULT result = 0;
            bool callbacks_invoked = false;
        };

        MouseEventAnalyser(HWND hwnd, UINT dpi) : hwnd_(hwnd), dpi_(dpi), active_(GetActiveWindow() == hwnd) {
            if (!hwnd || !dpi) throw std::invalid_argument("a window and positive DPI are required");
        }
        MouseEventAnalyser(const MouseEventAnalyser&) = delete;
        MouseEventAnalyser& operator=(const MouseEventAnalyser&) = delete;
        MouseEventAnalyser(MouseEventAnalyser&&) = delete;
        MouseEventAnalyser& operator=(MouseEventAnalyser&&) = delete;
        ~MouseEventAnalyser() { release_window_input(); }

        // Retain surviving groups, including their hover/capture state. Validate
        // and allocate before changing ownership; callbacks may replace this list.
        bool set_renderers(std::vector<std::shared_ptr<D2duiSystemRender>> renderers) {
            std::vector<std::shared_ptr<Group>> next;
            next.reserve(renderers.size());
            for (const auto& render : renderers) {
                if (!render || std::any_of(next.begin(), next.end(), [&](const auto& group) { return group->render == render; }))
                    throw std::invalid_argument("renderers must be non-null and unique");
                const auto found = std::find_if(groups_.begin(), groups_.end(), [&](const auto& group) { return group->render == render; });
                next.push_back(found == groups_.end() ? std::make_shared<Group>(render) : *found);
            }
            if (next == groups_) return false;
            enter();
            ++epoch_;
            pending_.clear();
            auto previous = std::move(groups_);
            groups_ = std::move(next);
            for (const auto& group : previous) {
                if (std::find(groups_.begin(), groups_.end(), group) == groups_.end()) cancel_group(*group);
            }
            return finish();
        }

        // Update conversion for subsequent messages without invoking callbacks or
        // changing capture. Zero (including a failed GetDpiForWindow) is ignored.
        // Already sampled DIP positions, including queued input, retain their DPI.
        void set_dpi(UINT dpi) noexcept { if (dpi) dpi_ = dpi; }

        MessageResult process_window_message(UINT message, WPARAM wparam, LPARAM lparam) noexcept {
            if (!hwnd_) return {};
            Input input{Kind::move};
            switch (message) {
            case WM_LBUTTONDOWN: case WM_LBUTTONDBLCLK: case WM_LBUTTONUP: input.button = VK_LBUTTON; break;
            case WM_RBUTTONDOWN: case WM_RBUTTONDBLCLK: case WM_RBUTTONUP: input.button = VK_RBUTTON; break;
            case WM_MBUTTONDOWN: case WM_MBUTTONDBLCLK: case WM_MBUTTONUP: input.button = VK_MBUTTON; break;
            case WM_XBUTTONDOWN: case WM_XBUTTONDBLCLK: case WM_XBUTTONUP:
                if (GET_XBUTTON_WPARAM(wparam) != XBUTTON1 && GET_XBUTTON_WPARAM(wparam) != XBUTTON2) return {true, TRUE};
                input.button = GET_XBUTTON_WPARAM(wparam) == XBUTTON1 ? VK_XBUTTON1 : VK_XBUTTON2;
                break;
            case WM_MOUSEMOVE: break;
            case WM_MOUSELEAVE:
                tracking_ = false;
                return {true, 0, dispatch({Kind::leave})};
            case WM_CAPTURECHANGED:
                return {true, 0, reinterpret_cast<HWND>(lparam) != hwnd_ && physical_buttons_.any() ? cancel() : false};
            case WM_CANCELMODE: return {false, 0, cancel()};
            case WM_ACTIVATE:
                active_ = LOWORD(wparam) != WA_INACTIVE;
                return {false, 0, !active_ ? cancel() : false};
            case WM_ENTERMENULOOP:
                in_menu_ = true;
                return {false, 0, cancel()};
            case WM_EXITMENULOOP: in_menu_ = false; return {};
            case WM_ENTERSIZEMOVE:
                in_size_move_ = true;
                return {false, 0, cancel()};
            case WM_EXITSIZEMOVE: in_size_move_ = false; return {};
            case WM_SIZE:
                minimized_ = wparam == SIZE_MINIMIZED;
                return {false, 0, minimized_ ? cancel() : false};
            case WM_DPICHANGED:
                set_dpi(HIWORD(wparam));
                return {};
            case WM_DPICHANGED_BEFOREPARENT:
            case WM_DPICHANGED_AFTERPARENT:
                // PMv2 child notifications have unused wParam/lParam. Query the
                // bound HWND in both phases; AFTERPARENT observes the final DPI.
                set_dpi(GetDpiForWindow(hwnd_));
                return {};
            case WM_DESTROY: case WM_NCDESTROY: {
                active_ = false;
                const bool invoked = cancel();
                hwnd_ = nullptr;
                return {false, 0, invoked};
            }
            default: return {};
            }
            const bool xbutton = input.button == VK_XBUTTON1 || input.button == VK_XBUTTON2;
            if (!enabled()) return {true, xbutton ? TRUE : 0};
            if (input.button) {
                const bool up = message == WM_LBUTTONUP || message == WM_RBUTTONUP || message == WM_MBUTTONUP || message == WM_XBUTTONUP;
                input.kind = up ? Kind::up : Kind::down;
            }
            const float factor = 96.0f / static_cast<float>(dpi_);
            input.position = {static_cast<float>(GET_X_LPARAM(lparam)) * factor, static_cast<float>(GET_Y_LPARAM(lparam)) * factor};
            return {true, xbutton ? TRUE : 0, dispatch(input)};
        }

        bool tick() noexcept { return enabled() ? dispatch({Kind::tick}) : false; }

    private:
        enum class Kind { move, leave, down, up, tick };

        struct Input {
            Kind kind;
            D2D1_POINT_2F position{};
            int16_t button = 0;
        };

        using Entry = D2duiSystemRender::Entry;

        struct Interaction {
            bool hovered = false;
            std::bitset<5> captured;
        };

        struct Group {
            explicit Group(std::shared_ptr<D2duiSystemRender> value) : render(std::move(value)) {}
            std::shared_ptr<D2duiSystemRender> render;
            // Weak keys never extend component lifetime beyond queue membership.
            std::map<std::weak_ptr<Entry>, Interaction, std::owner_less<std::weak_ptr<Entry>>> states;
        };

        struct Pass {
            std::shared_ptr<Group> group;
            std::vector<std::shared_ptr<Entry>> entries;
        };

        static constexpr std::array<int16_t, 5> buttons{VK_LBUTTON, VK_RBUTTON, VK_MBUTTON, VK_XBUTTON1, VK_XBUTTON2};

        bool enabled() const noexcept { return hwnd_ && active_ && !minimized_ && !in_menu_ && !in_size_move_; }

        void enter() noexcept { if (depth_++ == 0) callbacks_invoked_ = false; }

        bool finish() noexcept {
            if (depth_ == 1) {
                while (!pending_.empty()) {
                    const auto input = pending_.front();
                    pending_.pop_front();
                    process_safely(input);
                }
                for (const auto& group : groups_) {
                    std::erase_if(group->states, [](const auto& state) {
                        const auto entry = state.first.lock();
                        return !entry || !entry->active;
                    });
                }
                if (hwnd_ && physical_buttons_.none() && GetCapture() == hwnd_) ReleaseCapture();
            }
            --depth_;
            return callbacks_invoked_;
        }

        void emit(const std::shared_ptr<Entry>& entry, D2duiMouseEvent event, int16_t button, int16_t down) noexcept {
            if (entry->active)
                callbacks_invoked_ = entry->component->respond_mouse_event(event, {position_, button, down, 0}) || callbacks_invoked_;
        }

        void cancel_group(Group& group) noexcept {
            // Use registration order, and defer compaction while callbacks mutate
            // the render queue. No allocation is needed on cancellation paths.
            auto& render = *group.render;
            ++render.depth_;
            const size_t count = render.components_.size();
            for (size_t index = 0; index < count; ++index) {
                const auto entry = render.components_[index];
                const auto found = group.states.find(entry);
                if (!entry->active || found == group.states.end()) continue;
                auto& state = found->second;
                if (state.hovered) {
                    state.hovered = false;
                    emit(entry, D2duiMouseEvent::MOUSE_HOVER_LEAVE, 0, 0);
                }
                for (size_t i = 0; i < buttons.size(); ++i) {
                    if (state.captured.test(i)) {
                        state.captured.reset(i);
                        emit(entry, D2duiMouseEvent::MOUSE_CANCEL, buttons[i], 0);
                    }
                }
            }
            render.finish();
        }

        void release_window_input() noexcept {
            physical_buttons_.reset();
            if (tracking_ && hwnd_) {
                tracking_ = false;
                TRACKMOUSEEVENT tracking{sizeof(TRACKMOUSEEVENT), TME_CANCEL | TME_LEAVE, hwnd_, 0};
                TrackMouseEvent(&tracking);
            }
            if (hwnd_ && GetCapture() == hwnd_) ReleaseCapture();
        }

        bool cancel() noexcept {
            enter();
            ++epoch_;
            pending_.clear();
            inside_ = false;
            release_window_input();
            cancel_current_groups();
            return finish();
        }

        void cancel_current_groups() noexcept {
            // Retain each group and use the epoch to restart after a callback changes
            // the list. Already reset interaction bits make repeated cancellation safe.
            size_t i = 0;
            while (i < groups_.size()) {
                const auto epoch = epoch_;
                const auto group = groups_[i];
                cancel_group(*group);
                i = epoch == epoch_ ? i + 1 : 0;
            }
        }

        bool dispatch(Input input) noexcept {
            if (depth_) {
                try { pending_.push_back(input); }
                catch (...) { return cancel(); }
                return false;
            }
            enter();
            process_safely(input);
            return finish();
        }

        void process_safely(Input input) noexcept {
            try { process(input); }
            catch (...) { (void)cancel(); }
        }

        void process(Input input) {
            if (input.kind != Kind::leave && !enabled()) return;
            const auto epoch = epoch_;
            if (input.button) {
                const size_t index = static_cast<size_t>(std::find(buttons.begin(), buttons.end(), input.button) - buttons.begin());
                if (input.kind == Kind::down && physical_buttons_.test(index)) return;
                physical_buttons_.set(index, input.kind == Kind::down);
                if (input.kind == Kind::down && GetCapture() != hwnd_) {
                    SetCapture(hwnd_);
                    if (GetCapture() != hwnd_) { (void)cancel(); return; }
                    if (epoch != epoch_) return;
                }
            }
            if (input.kind == Kind::move || input.kind == Kind::down || input.kind == Kind::up) {
                position_ = input.position;
                inside_ = true;
                if (!tracking_) {
                    TRACKMOUSEEVENT tracking{sizeof(TRACKMOUSEEVENT), TME_LEAVE, hwnd_, 0};
                    tracking_ = TrackMouseEvent(&tracking) != FALSE;
                }
            }
            if (input.kind == Kind::leave) inside_ = false;

            // Snapshot all queues before invoking callbacks: additions to any queue
            // participate only in the next event, removals remain immediately visible.
            std::vector<Pass> passes;
            passes.reserve(groups_.size());
            for (const auto& group : groups_) passes.push_back({group, group->render->components_});
            for (const auto& pass : passes) {
                for (const auto& entry : pass.entries) {
                    if (epoch != epoch_) return;
                    if (!entry->active) continue;
                    auto& state = pass.group->states[entry];
                    const bool hovered = inside_ && cursor_pos::is_in_rect(position_, entry->component->get_bounds());
                    if (hovered != state.hovered) {
                        state.hovered = hovered;
                        emit(entry, hovered ? D2duiMouseEvent::MOUSE_HOVER_ENTER : D2duiMouseEvent::MOUSE_HOVER_LEAVE, 0, hovered ? 1 : 0);
                    } else if (hovered && input.kind == Kind::tick) {
                        emit(entry, D2duiMouseEvent::MOUSE_HOVER_ON, 0, 1);
                    }
                    for (size_t i = 0; i < buttons.size() && entry->active && epoch == epoch_; ++i) {
                        const auto button = buttons[i];
                        if (input.kind == Kind::tick && state.captured.test(i)) {
                            emit(entry, static_cast<D2duiMouseEvent>(button * 3 + 1), button, 1);
                        } else if (button == input.button) {
                            if (input.kind == Kind::down && hovered && !state.captured.test(i)) {
                                state.captured.set(i);
                                emit(entry, static_cast<D2duiMouseEvent>(button * 3), button, 1);
                            } else if (input.kind == Kind::up && state.captured.test(i)) {
                                state.captured.reset(i);
                                emit(entry, static_cast<D2duiMouseEvent>(button * 3 + 2), button, 0);
                            }
                        }
                    }
                }
            }
        }

        HWND hwnd_;
        UINT dpi_;
        bool active_;
        bool minimized_ = false;
        bool in_menu_ = false;
        bool in_size_move_ = false;
        bool tracking_ = false;
        bool inside_ = false;
        bool callbacks_invoked_ = false;
        size_t depth_ = 0;
        size_t epoch_ = 0;
        D2D1_POINT_2F position_{};
        std::bitset<5> physical_buttons_;
        std::vector<std::shared_ptr<Group>> groups_;
        std::deque<Input> pending_;
    };

} // namespace d2dui
