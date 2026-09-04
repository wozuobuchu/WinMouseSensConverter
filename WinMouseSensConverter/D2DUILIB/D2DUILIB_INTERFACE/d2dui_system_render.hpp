#pragma once

#ifndef D2DUI_SYSTEM_RENDER_HPP_
#define D2DUI_SYSTEM_RENDER_HPP_

#include "d2dui_component_base.hpp"

#include <algorithm>
#include <concepts>
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

        template <typename T>
            requires std::derived_from<T, D2duiComponentsBase>
        T& register_component(std::unique_ptr<T> component) {
            if (component == nullptr) throw std::invalid_argument("component must not be null");
            T& reference = *component;
            components_.push_back(std::move(component));
            return reference;
        }

        template <typename T, typename... Args>
            requires std::derived_from<T, D2duiComponentsBase>
        T& emplace_component(Args&&... args) {
            return register_component(std::make_unique<T>(std::forward<Args>(args)...));
        }

        bool unregister_component(D2duiComponentsBase& component) noexcept {
            const auto found = std::find_if(components_.begin(), components_.end(),
                [&component](const auto& item) { return item.get() == &component; });
            if (found == components_.end()) return false;
            components_.erase(found);
            return true;
        }

        void clear() noexcept { components_.clear(); }
        [[nodiscard]] size_t size() const noexcept { return components_.size(); }

        HRESULT draw(D2duiContext& context) noexcept {
            if (!context.in_frame()) return D2DERR_WRONG_STATE;
            for (const auto& component : components_) {
                if (!component->visible()) continue;
                const HRESULT result = component->draw(context);
                if (FAILED(result)) return result;
            }
            return S_OK;
        }

    private:
        std::vector<std::unique_ptr<D2duiComponentsBase>> components_;
    };

} // namespace d2dui

#endif // D2DUI_SYSTEM_RENDER_HPP_
