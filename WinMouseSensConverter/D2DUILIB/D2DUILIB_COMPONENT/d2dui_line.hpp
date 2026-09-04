#pragma once

#ifndef D2DUI_LINE_HPP_
#define D2DUI_LINE_HPP_

#include "../D2DUILIB_INTERFACE/d2dui_component_base.hpp"

namespace d2dui {

    class D2duiLine final : public D2duiComponentsBase {
    public:
        [[nodiscard]] const D2D1_RECT_F& get_bounds() const noexcept override { return bounds_; }
        void resize(const D2D1_RECT_F& bounds, float scale) noexcept override { bounds_ = bounds; scale_ = scale; }
        HRESULT draw(D2duiContext& context) noexcept override {
            ID2D1SolidColorBrush* brush = nullptr;
            const HRESULT result = context.get_brush(color_, &brush);
            if (FAILED(result)) return result;
            context.render_target()->DrawLine(
                D2D1::Point2F(bounds_.left, bounds_.top), D2D1::Point2F(bounds_.right, bounds_.bottom), brush, width_);
            return S_OK;
        }
        void on_click() noexcept override {}
        void set_color(D2duiColor color) noexcept { color_ = color; }
        void set_width(float width) noexcept { width_ = width; }

    private:
        D2duiColor color_{};
        float width_ = 1.0f;
    };

} // namespace d2dui

#endif // D2DUI_LINE_HPP_
