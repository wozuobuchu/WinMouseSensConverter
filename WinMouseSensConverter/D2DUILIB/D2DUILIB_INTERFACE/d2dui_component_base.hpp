#pragma once

#ifndef D2DUI_COMPONENT_BASE_HPP_
#define D2DUI_COMPONENT_BASE_HPP_

#include "d2dui.hpp"

namespace d2dui {

    class D2duiComponentsBase {
    public:
        D2duiComponentsBase() = default;
        D2duiComponentsBase(const D2duiComponentsBase&) = delete;
        D2duiComponentsBase& operator=(const D2duiComponentsBase&) = delete;
        D2duiComponentsBase(D2duiComponentsBase&&) = default;
        D2duiComponentsBase& operator=(D2duiComponentsBase&&) = default;
        virtual ~D2duiComponentsBase() = default;

        [[nodiscard]] virtual const D2D1_RECT_F& get_bounds() const noexcept = 0;
        virtual void resize(const D2D1_RECT_F& bounds, float scale) noexcept = 0;
        virtual HRESULT draw(D2duiContext& context) noexcept = 0;
        virtual void on_click() noexcept = 0;

        [[nodiscard]] bool visible() const noexcept { return visible_; }
        void set_visible(bool visible) noexcept { visible_ = visible; }
        [[nodiscard]] bool enabled() const noexcept { return enabled_; }
        void set_enabled(bool enabled) noexcept { enabled_ = enabled; }

    protected:
        D2D1_RECT_F bounds_{};
        float scale_ = 1.0f;
        bool visible_ = true;
        bool enabled_ = true;
        bool dirty_ = true;
    };

} // namespace d2dui

#endif // D2DUI_COMPONENT_BASE_HPP_
