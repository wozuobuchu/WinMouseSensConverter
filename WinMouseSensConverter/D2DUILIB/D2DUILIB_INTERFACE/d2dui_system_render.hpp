#pragma once

#ifndef D2DUI_SYSTEM_RENDER_HPP_
#define D2DUI_SYSTEM_RENDER_HPP_

#include "d2dui_component_base.hpp"

#include "d2dui_cursor_pos.hpp"

#include <algorithm>
#include <array>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

namespace d2dui {

    class D2duiSystemRender final {
    public:
        D2duiSystemRender() = default;
        D2duiSystemRender(const D2duiSystemRender&) = delete;
        D2duiSystemRender& operator=(const D2duiSystemRender&) = delete;
        D2duiSystemRender(D2duiSystemRender&&) = default;
        D2duiSystemRender& operator=(D2duiSystemRender&&) = default;

        // Append a non-null component, sharing ownership with the caller.
        void register_component(std::shared_ptr<D2duiComponentsBase> component) {
            if (component == nullptr) throw std::invalid_argument("component must not be null");
            components_.push_back({ std::move(component) });
        }

        // Release the first entry matching the object address; return false for null or absent components.
        bool unregister_component(const std::shared_ptr<D2duiComponentsBase>& component) noexcept {
            const auto found = std::find_if(
                components_.begin(), components_.end(),
                [&component](const auto& item) { return item.component.get() == component.get(); }
            );
            if (found == components_.end()) return false;
            components_.erase(found);
            return true;
        }

        // Release all queue entries. External shared owners keep their components alive.
        void clear() noexcept { components_.clear(); }

        // Get the number of components in the system render.
        [[nodiscard]] size_t size() const noexcept { return components_.size(); }

        // CPU and GPU time taking for rendering all components. Returns S_OK on success, or an error code if any component fails to draw.
        HRESULT draw(D2duiContext& context) noexcept {
            if (!context.in_frame()) return D2DERR_WRONG_STATE;
            for (const auto& entry : components_) {
                const HRESULT result = entry.component->draw(context);
                if (FAILED(result)) return result;
            }
            return S_OK;
        }

        // Construct and dispatch mouse events using the cursor position and current button states.
        void dispatch_mouse_events(HWND hwnd, ID2D1RenderTarget* render_target, const MouseKeyStateBitset& mouse_key_states) noexcept {
            const std::optional<D2D1_POINT_2F> position = cursor_pos::get_d2d(hwnd, render_target);
            if (!position) return;

            constexpr std::array<int16_t, 5> mouse_vkeys{
                VK_LBUTTON,
                VK_RBUTTON,
                VK_MBUTTON,
                VK_XBUTTON1,
                VK_XBUTTON2,
            };

            for (auto& entry : components_) {
                const bool is_hovered = cursor_pos::is_in_rect(*position, entry.component->get_bounds());
                if (is_hovered != entry.is_hovered) {
                    entry.component->respond_mouse_event(
                        is_hovered ? D2duiMouseEvent::MOUSE_HOVER_ENTER : D2duiMouseEvent::MOUSE_HOVER_LEAVE,
                        D2duiMouseEventParam{ *position, 0, static_cast<int16_t>(is_hovered), 0 }
                    );
                    entry.is_hovered = is_hovered;
                } else if (is_hovered) {
                    entry.component->respond_mouse_event(
                        D2duiMouseEvent::MOUSE_HOVER_ON,
                        D2duiMouseEventParam{ *position, 0, 1, 0 }
                    );
                }

                for (const int16_t vkey : mouse_vkeys) {
                    const size_t state_index = static_cast<size_t>(vkey) * 3;
                    const bool is_down = mouse_key_states.test(state_index);
                    const bool was_down = mouse_key_states_.test(state_index);

                    if (is_down && !was_down && is_hovered) {
                        entry.captured_mouse_keys.set(state_index);
                        entry.component->respond_mouse_event(
                            mouse_event_for(vkey, 0),
                            D2duiMouseEventParam{ *position, vkey, 1, 0 }
                        );
                    } else if (is_down && was_down && entry.captured_mouse_keys.test(state_index)) {
                        entry.component->respond_mouse_event(
                            mouse_event_for(vkey, 1),
                            D2duiMouseEventParam{ *position, vkey, 1, 0 }
                        );
                    } else if (!is_down && was_down && entry.captured_mouse_keys.test(state_index)) {
                        entry.captured_mouse_keys.reset(state_index);
                        entry.component->respond_mouse_event(
                            mouse_event_for(vkey, 2),
                            D2duiMouseEventParam{ *position, vkey, 0, 0 }
                        );
                    }
                }
            }

            mouse_key_states_ = mouse_key_states;
        }

    private:
        struct ComponentEntry {
            std::shared_ptr<D2duiComponentsBase> component;
            bool is_hovered = false;
            // Tracks buttons pressed on this entry, so only the component that received CLICK_ENTER receives CLICK_ON and CLICK_LEAVE.
            // mouse_key_states_ separately stores global button state for detecting frame-to-frame transitions.
            MouseKeyStateBitset captured_mouse_keys{};
        };

        static constexpr D2duiMouseEvent mouse_event_for(const int16_t vkey, const int32_t state) noexcept {
            return static_cast<D2duiMouseEvent>(static_cast<int32_t>(vkey) * 3 + state);
        }

        std::vector<ComponentEntry> components_;
        MouseKeyStateBitset mouse_key_states_{};
    };

} // namespace d2dui

#endif // D2DUI_SYSTEM_RENDER_HPP_
