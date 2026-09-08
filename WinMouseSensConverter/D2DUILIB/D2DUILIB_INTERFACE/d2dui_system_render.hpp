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

    private:
        friend class MouseEventAnalyser;
        struct Entry {
            explicit Entry(std::shared_ptr<D2duiComponentsBase> value) : component(std::move(value)) {}
            std::shared_ptr<D2duiComponentsBase> component;
            bool active = true;
        };

        void compact() noexcept {
            std::erase_if(components_, [](const auto& entry) { return !entry->active; });
        }

        void finish() noexcept { if (--depth_ == 0) compact(); }

        std::vector<std::shared_ptr<Entry>> components_;
        size_t depth_ = 0;
    };
}
#endif
