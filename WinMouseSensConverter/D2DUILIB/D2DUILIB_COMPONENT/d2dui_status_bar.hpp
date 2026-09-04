#pragma once

#ifndef D2DUI_STATUS_BAR_HPP_
#define D2DUI_STATUS_BAR_HPP_

#include "d2dui_panel.hpp"
#include "d2dui_switch.hpp"
#include "d2dui_text.hpp"

#include <algorithm>
#include <string>

namespace d2dui {

    class D2duiStatusBar final : public D2duiComponentsBase {
    public:
        D2duiStatusBar() {
            panel_.set_fill_color({0xFFFFFF, 1.0f});
            panel_.set_border({0xE1E7EF, 1.0f});
            panel_.set_shadow({0xD8E0EA, 0.72f});
            panel_.set_radius(16.0f);
            badge_.set_fill_color({0x2563EB, 1.0f});
            badge_.set_radius(9.0f);
            badge_text_.set_style(center_style());
            badge_text_.set_color({0xFFFFFF, 1.0f});
            label_text_.set_style(leading_style());
            label_text_.set_color({0x172033, 1.0f});
            label_text_.set_text(L"Recording");
            toggle_.set_enabled(false);
        }

        [[nodiscard]] const D2D1_RECT_F& get_bounds() const noexcept override { return bounds_; }
        void resize(const D2D1_RECT_F& bounds, float scale) noexcept override {
            if (bounds_.left == bounds.left && bounds_.top == bounds.top && bounds_.right == bounds.right && bounds_.bottom == bounds.bottom && scale_ == scale) return;
            bounds_ = bounds; scale_ = scale; dirty_ = true;
        }

        HRESULT draw(D2duiContext& context) noexcept override {
            HRESULT result = S_OK;
            if (dirty_) {
                result = arrange(context);
                if (FAILED(result)) return result;
            }
            result = panel_.draw(context);
            if (FAILED(result)) return result;
            result = badge_.draw(context);
            if (FAILED(result)) return result;
            result = badge_text_.draw(context);
            if (FAILED(result)) return result;
            result = label_text_.draw(context);
            if (FAILED(result)) return result;
            return toggle_.draw(context);
        }

        void on_click() noexcept override { if (enabled_) toggle_.on_click(); }
        void set_badge_text(std::wstring text) {
            if (badge_text_.text() == text) return;
            badge_text_.set_text(std::move(text));
            dirty_ = true;
        }
        void set_label_text(std::wstring text) { label_text_.set_text(std::move(text)); }
        void set_checked(bool checked) noexcept { toggle_.set_checked(checked); }

    private:
        static D2duiTextStyle center_style() {
            D2duiTextStyle style{};
            style.font_size = 14.0f;
            style.weight = DWRITE_FONT_WEIGHT_SEMI_BOLD;
            return style;
        }

        static D2duiTextStyle leading_style() {
            D2duiTextStyle style = center_style();
            style.text_alignment = DWRITE_TEXT_ALIGNMENT_LEADING;
            return style;
        }

        HRESULT arrange(D2duiContext& context) noexcept {
            panel_.resize(bounds_, scale_);
            badge_text_.set_style(center_style());
            badge_text_.resize(D2D1::RectF(0.0f, 0.0f, 512.0f, 34.0f), scale_);
            HRESULT result = badge_text_.prepare_layout(context);
            if (FAILED(result)) return result;

            const float footer_height = bounds_.bottom - bounds_.top;
            const float padding = 16.0f * scale_;
            const float badge_height = 34.0f * scale_;
            const float badge_top = bounds_.top + (footer_height - badge_height) * 0.5f;
            const float badge_width = std::max(48.0f, badge_text_.intrinsic_width() + 24.0f);
            const D2D1_RECT_F badge_bounds = D2D1::RectF(
                bounds_.left + padding, badge_top, bounds_.left + padding + badge_width, badge_top + badge_height);
            badge_.resize(badge_bounds, scale_);
            badge_text_.resize(badge_bounds, scale_);

            const float switch_width = 44.0f * scale_;
            const float switch_height = 24.0f * scale_;
            const float switch_right = bounds_.right - padding;
            const float switch_top = bounds_.top + (footer_height - switch_height) * 0.5f;
            const D2D1_RECT_F switch_bounds = D2D1::RectF(
                switch_right - switch_width, switch_top, switch_right, switch_top + switch_height);
            toggle_.resize(switch_bounds, scale_);
            label_text_.set_style(leading_style());
            label_text_.resize(D2D1::RectF(
                badge_bounds.right + 14.0f * scale_, bounds_.top, switch_bounds.left - 16.0f * scale_, bounds_.bottom), scale_);
            dirty_ = false;
            return S_OK;
        }

        D2duiPanel panel_;
        D2duiPanel badge_;
        D2duiText badge_text_;
        D2duiText label_text_;
        D2duiSwitch toggle_;
    };

} // namespace d2dui

#endif // D2DUI_STATUS_BAR_HPP_
