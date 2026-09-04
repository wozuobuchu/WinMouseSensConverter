#pragma once

#ifndef D2DUI_SWITCH_HPP_
#define D2DUI_SWITCH_HPP_

#include "../D2DUILIB_INTERFACE/d2dui_component_base.hpp"

namespace d2dui {

    class D2duiSwitch final : public D2duiComponentsBase {
    public:
        [[nodiscard]] const D2D1_RECT_F& get_bounds() const noexcept override { return bounds_; }
        void resize(const D2D1_RECT_F& bounds, float scale) noexcept override { bounds_ = bounds; scale_ = scale; }
        HRESULT draw(D2duiContext& context) noexcept override {
            ID2D1SolidColorBrush* track = nullptr;
            ID2D1SolidColorBrush* thumb = nullptr;
            HRESULT result = context.get_brush(checked_ ? checked_color_ : unchecked_color_, &track);
            if (FAILED(result)) return result;
            result = context.get_brush(thumb_color_, &thumb);
            if (FAILED(result)) return result;

            const float height = bounds_.bottom - bounds_.top;
            const float radius = height * 0.5f;
            ID2D1HwndRenderTarget* target = context.render_target();
            target->FillRoundedRectangle(D2D1::RoundedRect(bounds_, radius, radius), track);
            const float thumb_radius = 10.0f * scale_;
            const float inset = 2.0f * scale_;
            const float x = checked_ ? bounds_.right - inset - thumb_radius : bounds_.left + inset + thumb_radius;
            const float y = (bounds_.top + bounds_.bottom) * 0.5f;
            target->FillEllipse(D2D1::Ellipse(D2D1::Point2F(x, y), thumb_radius, thumb_radius), thumb);
            return S_OK;
        }
        void on_click() noexcept override { if (enabled_) checked_ = !checked_; }
        void set_checked(bool checked) noexcept { checked_ = checked; }
        [[nodiscard]] bool checked() const noexcept { return checked_; }
        void set_colors(D2duiColor checked, D2duiColor unchecked, D2duiColor thumb) noexcept {
            checked_color_ = checked; unchecked_color_ = unchecked; thumb_color_ = thumb;
        }

    private:
        bool checked_ = false;
        D2duiColor checked_color_{0x34C759, 1.0f};
        D2duiColor unchecked_color_{0xC7CBD1, 1.0f};
        D2duiColor thumb_color_{0xFFFFFF, 1.0f};
    };

} // namespace d2dui

#endif // D2DUI_SWITCH_HPP_
