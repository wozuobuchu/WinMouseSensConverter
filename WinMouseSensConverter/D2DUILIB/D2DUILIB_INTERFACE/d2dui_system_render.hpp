#pragma once

#ifndef D2DUI_SYSTEM_RENDER_HPP_
#define D2DUI_SYSTEM_RENDER_HPP_

#include "d2dui_component_base.hpp"

#include <algorithm>
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
            components_.push_back(std::move(component));
        }

        // Release the first entry matching the object address; return false for null or absent components.
        bool unregister_component(const std::shared_ptr<D2duiComponentsBase>& component) noexcept {
            const auto found = std::find_if(components_.begin(), components_.end(),
                [&component](const auto& item) { return item.get() == component.get(); });
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
            for (const auto& component : components_) {
                const HRESULT result = component->draw(context);
                if (FAILED(result)) return result;
            }
            return S_OK;
        }

    private:
        std::vector<std::shared_ptr<D2duiComponentsBase>> components_;

        // Distribute a mouse event to all registered components.
        void distribute_event(D2duiMouseEvent event, const D2duiMouseEventParam& param) noexcept {
            for (const auto& component : components_) {
                (void)component->respond_mouse_event(event, param);
            }
        }
    };

} // namespace d2dui

#endif // D2DUI_SYSTEM_RENDER_HPP_
