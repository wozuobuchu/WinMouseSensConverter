#pragma once

#ifndef D2DUI_PANEL_HPP_
#define D2DUI_PANEL_HPP_

#include "../D2DUILIB_INTERFACE/d2dui_component_base.hpp"

namespace d2dui {

    class D2duiPanel final : public D2duiComponentsBase {
    public:
        [[nodiscard]] const D2D1_RECT_F& get_bounds() const noexcept override { return bounds_; }
        void resize(const D2D1_RECT_F& bounds, float scale) noexcept override {
            bounds_ = bounds;
            scale_ = scale;
            dirty_ = false;
        }

        HRESULT draw(D2duiContext& context) noexcept override {
            ID2D1SolidColorBrush* fill = nullptr;
            ID2D1SolidColorBrush* border = nullptr;
            ID2D1SolidColorBrush* shadow = nullptr;
            HRESULT result = context.get_brush(fill_color_, &fill);
            if (FAILED(result)) return result;
            if (draw_border_) {
                result = context.get_brush(border_color_, &border);
                if (FAILED(result)) return result;
            }
            if (draw_shadow_) {
                result = context.get_brush(shadow_color_, &shadow);
                if (FAILED(result)) return result;
            }

            ID2D1HwndRenderTarget* target = context.render_target();
            if (target == nullptr) return E_UNEXPECTED;
            const float radius = radius_ * scale_;
            if (draw_shadow_) {
                const D2D1_RECT_F shadow_bounds = D2D1::RectF(
                    bounds_.left, bounds_.top + shadow_offset_, bounds_.right, bounds_.bottom + shadow_offset_);
                target->FillRoundedRectangle(D2D1::RoundedRect(shadow_bounds, radius, radius), shadow);
            }
            const D2D1_ROUNDED_RECT panel = D2D1::RoundedRect(bounds_, radius, radius);
            target->FillRoundedRectangle(panel, fill);
            if (draw_border_) target->DrawRoundedRectangle(panel, border, border_width_);
            return S_OK;
        }

        void on_click() noexcept override {}

        void set_fill_color(D2duiColor color) noexcept { fill_color_ = color; }
        void set_border(D2duiColor color, float width = 1.0f) noexcept { border_color_ = color; border_width_ = width; draw_border_ = true; }
        void disable_border() noexcept { draw_border_ = false; }
        void set_shadow(D2duiColor color, float vertical_offset = 2.0f) noexcept { shadow_color_ = color; shadow_offset_ = vertical_offset; draw_shadow_ = true; }
        void disable_shadow() noexcept { draw_shadow_ = false; }
        void set_radius(float radius) noexcept { radius_ = radius; }

    private:
        D2duiColor fill_color_{0xFFFFFF, 1.0f};
        D2duiColor border_color_{0x000000, 1.0f};
        D2duiColor shadow_color_{0x000000, 0.0f};
        float radius_ = 0.0f;
        float border_width_ = 1.0f;
        float shadow_offset_ = 2.0f;
        bool draw_border_ = false;
        bool draw_shadow_ = false;
    };

} // namespace d2dui

#endif // D2DUI_PANEL_HPP_
