#pragma once
#ifndef D2DUI_SYSTEM_RENDER_HPP_
#define D2DUI_SYSTEM_RENDER_HPP_

#include "d2dui_component_base.hpp"
#include "d2dui_cursor_pos.hpp"
#include <algorithm>
#include <array>
#include <deque>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

namespace d2dui {
    // move/down/up carry the message position; leave/tick/cancel retain it.
    // leave ends hover only. cancel also aborts every captured button, without a release.
    enum class MouseInputKind { move, leave, down, up, tick, cancel };
    struct MouseInput {
        MouseInputKind kind;
        D2D1_POINT_2F position{};
        int16_t button = 0;
    };

    class D2duiSystemRender final {
    public:
        D2duiSystemRender() = default;
        D2duiSystemRender(const D2duiSystemRender&) = delete;
        D2duiSystemRender& operator=(const D2duiSystemRender&) = delete;
        D2duiSystemRender(D2duiSystemRender&&) = default;
        D2duiSystemRender& operator=(D2duiSystemRender&&) = default;

        void register_component(std::shared_ptr<D2duiComponentsBase> component) {
            if (!component) throw std::invalid_argument("component must not be null");
            components_.push_back(std::make_shared<Entry>(std::move(component)));
        }

        bool unregister_component(const std::shared_ptr<D2duiComponentsBase>& component) noexcept {
            for (const auto& entry : components_) {
                if (entry->active && entry->component.get() == component.get()) {
                    entry->active = false;
                    if (!depth_) compact();
                    return true;
                }
            }
            return false;
        }

        void clear() noexcept {
            for (const auto& entry : components_) entry->active = false;
            if (!depth_) compact();
        }

        [[nodiscard]] size_t size() const noexcept {
            return static_cast<size_t>(std::count_if(components_.begin(), components_.end(),
                [](const auto& entry) { return entry->active; }));
        }

        HRESULT draw(D2duiContext& context) noexcept {
            if (!context.in_frame()) return D2DERR_WRONG_STATE;
            ++depth_;
            HRESULT result = S_OK;
            const size_t count = components_.size();
            for (size_t i = 0; i < count; ++i) {
                const auto entry = components_[i];
                if (entry->active) result = entry->component->draw(context);
                if (FAILED(result)) break;
            }
            finish();
            return result;
        }

        // Explicit DIP coordinates: transitions never sample the live cursor.
        // New entries participate in the next dispatch; removed entries stop immediately.
        // Nested cancellation is immediate, other nested input is drained in FIFO order.
        // Returns true if a callback completed, including callbacks from drained input.
        bool dispatch_mouse_events(MouseInput input) noexcept {
            if (depth_ && input.kind != MouseInputKind::cancel) {
                try { pending_.push_back(input); }
                catch (...) { return dispatch_mouse_events({MouseInputKind::cancel}); }
                return false;
            }
            ++depth_;
            bool handled = process(input);
            if (depth_ == 1) {
                while (!pending_.empty()) {
                    const auto next = pending_.front();
                    pending_.pop_front();
                    handled = process(next) || handled;
                }
            }
            finish();
            return handled;
        }
        bool cancel_mouse_events() noexcept { return dispatch_mouse_events({MouseInputKind::cancel}); }

    private:
        struct Entry {
            explicit Entry(std::shared_ptr<D2duiComponentsBase> value) : component(std::move(value)) {}
            std::shared_ptr<D2duiComponentsBase> component;
            bool active = true;
            bool hovered = false;
            MouseKeyStateBitset captured{};
        };

        static constexpr std::array<int16_t, 5> buttons{VK_LBUTTON, VK_RBUTTON, VK_MBUTTON, VK_XBUTTON1, VK_XBUTTON2};

        static D2duiMouseEvent event_for(int16_t button, int state) noexcept {
            return static_cast<D2duiMouseEvent>(button * 3 + state);
        }

        void compact() noexcept {
            std::erase_if(components_, [](const auto& entry) { return !entry->active; });
        }

        void finish() noexcept { if (--depth_ == 0) compact(); }

        bool process(MouseInput input) noexcept {
            const bool cancel = input.kind == MouseInputKind::cancel;
            if (cancel) { ++epoch_; pending_.clear(); }

            const auto epoch = epoch_;
            if (input.kind == MouseInputKind::move || input.kind == MouseInputKind::down || input.kind == MouseInputKind::up) {
                position_ = input.position;
                inside_ = true;
            }
            if (cancel || input.kind == MouseInputKind::leave) inside_ = false;

            const bool valid_button = std::find(buttons.begin(), buttons.end(), input.button) != buttons.end();
            bool handled = false;
            const size_t count = components_.size();

            for (size_t i = 0; i < count && epoch == epoch_; ++i) {
                const auto entry = components_[i];
                const auto emit = [&](D2duiMouseEvent event, int16_t button, int16_t down) {
                    if (entry->active && epoch == epoch_)
                        handled = entry->component->respond_mouse_event(event, {position_, button, down, 0}) || handled;
                };
                if (!entry->active) continue;
                const bool hovered = inside_ && cursor_pos::is_in_rect(position_, entry->component->get_bounds());
                if (hovered != entry->hovered) {
                    entry->hovered = hovered;
                    emit(hovered ? D2duiMouseEvent::MOUSE_HOVER_ENTER : D2duiMouseEvent::MOUSE_HOVER_LEAVE, 0, hovered ? 1 : 0);
                } else if (hovered && input.kind == MouseInputKind::tick) {
                    emit(D2duiMouseEvent::MOUSE_HOVER_ON, 0, 1);
                }
                if (!entry->active || epoch != epoch_) continue;
                for (const auto button : buttons) {
                    const size_t index = static_cast<size_t>(button) * 3;
                    if (cancel) {
                        if (entry->captured.test(index)) {
                            entry->captured.reset(index);
                            emit(D2duiMouseEvent::MOUSE_CANCEL, button, 0);
                        }
                    } else if (input.kind == MouseInputKind::tick && entry->captured.test(index)) {
                        emit(event_for(button, 1), button, 1);
                    } else if (valid_button && button == input.button) {
                        if (input.kind == MouseInputKind::down && hovered && !entry->captured.test(index)) {
                            entry->captured.set(index);
                            emit(event_for(button, 0), button, 1);
                        } else if (input.kind == MouseInputKind::up && entry->captured.test(index)) {
                            entry->captured.reset(index);
                            emit(event_for(button, 2), button, 0);
                        }
                    }
                    if (!entry->active || epoch != epoch_) break;
                }
            }
            return handled;
        }

        std::vector<std::shared_ptr<Entry>> components_;
        std::deque<MouseInput> pending_;
        size_t depth_ = 0;
        size_t epoch_ = 0;
        D2D1_POINT_2F position_{};
        bool inside_ = false;
    };
}
#endif
