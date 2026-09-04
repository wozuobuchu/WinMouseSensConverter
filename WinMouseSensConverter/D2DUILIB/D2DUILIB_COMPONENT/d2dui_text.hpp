#pragma once

#ifndef D2DUI_TEXT_HPP_
#define D2DUI_TEXT_HPP_

#include "../D2DUILIB_INTERFACE/d2dui_component_base.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>

namespace d2dui {

    class D2duiText final : public D2duiComponentsBase {
    public:
        static constexpr UINT32 no_suffix = std::numeric_limits<UINT32>::max();

        D2duiText() = default;

        explicit D2duiText(std::wstring text, D2duiTextStyle style = {}, D2duiColor color = {})
            : text_(std::move(text)), style_(std::move(style)), color_(color) {}

        [[nodiscard]] const D2D1_RECT_F& get_bounds() const noexcept override { return bounds_; }

        void resize(const D2D1_RECT_F& bounds, float scale) noexcept override {
            if (bounds_.left == bounds.left && bounds_.top == bounds.top && bounds_.right == bounds.right && bounds_.bottom == bounds.bottom && scale_ == scale) return;
            bounds_ = bounds;
            scale_ = scale;
            dirty_ = true;
            bounds_dirty_ = true;
        }

        HRESULT draw(D2duiContext& context) noexcept override {
            HRESULT result = prepare_layout(context);
            if (FAILED(result)) return result;
            if (layout_ == nullptr || context.render_target() == nullptr) return E_UNEXPECTED;
            ID2D1SolidColorBrush* brush = nullptr;
            result = context.get_brush(color_, &brush);
            if (FAILED(result)) return result;
            context.render_target()->DrawTextLayout(
                D2D1::Point2F(bounds_.left, bounds_.top), layout_.Get(), brush, draw_options_);
            return S_OK;
        }

        void on_click() noexcept override {}

        void set_text(std::wstring text) {
            if (text_ == text) return;
            text_ = std::move(text);
            dirty_ = true;
            layout_dirty_ = true;
        }

        void set_style(D2duiTextStyle style) {
            if (style_ == style) return;
            style_ = std::move(style);
            dirty_ = true;
            layout_dirty_ = true;
        }

        void set_color(D2duiColor color) noexcept { color_ = color; }

        void set_suffix(UINT32 suffix_start, float suffix_font_size) noexcept {
            if (suffix_start_ == suffix_start && suffix_font_size_ == suffix_font_size) return;
            suffix_start_ = suffix_start;
            suffix_font_size_ = suffix_font_size;
            dirty_ = true;
            layout_dirty_ = true;
        }

        void set_fit_to_bounds(bool enabled) noexcept {
            if (fit_to_bounds_ == enabled) return;
            fit_to_bounds_ = enabled;
            dirty_ = true;
            bounds_dirty_ = true;
        }

        void set_font_scale(float scale) noexcept {
            if (font_scale_ == scale || scale <= 0.0f) return;
            font_scale_ = scale;
            dirty_ = true;
            bounds_dirty_ = true;
        }

        void set_draw_options(D2D1_DRAW_TEXT_OPTIONS options) noexcept { draw_options_ = options; }

        [[nodiscard]] const std::wstring& text() const noexcept { return text_; }
        [[nodiscard]] UINT32 suffix_start() const noexcept { return suffix_start_; }
        [[nodiscard]] float suffix_font_size() const noexcept { return suffix_font_size_; }
        [[nodiscard]] float content_fit_scale() const noexcept { return content_fit_scale_; }
        [[nodiscard]] float intrinsic_width() const noexcept { return intrinsic_width_; }
        [[nodiscard]] IDWriteTextLayout* layout() const noexcept { return layout_.Get(); }

        HRESULT prepare_layout(D2duiContext& context) noexcept {
            if (!dirty_ && layout_ != nullptr) return S_OK;
            if (context.write_factory() == nullptr || style_.font_size <= 0.0f) return E_INVALIDARG;
            if (text_.size() > std::numeric_limits<UINT32>::max()) return E_INVALIDARG;
            IDWriteTextFormat* format = nullptr;
            HRESULT result = context.get_text_format(style_, &format);
            if (FAILED(result)) return result;

            const float width = std::max(1.0f, bounds_.right - bounds_.left);
            const float height = std::max(1.0f, bounds_.bottom - bounds_.top);
            const DWRITE_TEXT_RANGE full_range{0, static_cast<UINT32>(text_.size())};
            ComPtr<IDWriteTextLayout> next_layout;
            IDWriteTextLayout* working_layout = layout_.Get();
            if (layout_dirty_ || working_layout == nullptr) {
                result = context.write_factory()->CreateTextLayout(
                    text_.c_str(), static_cast<UINT32>(text_.size()), format, width, height, next_layout.GetAddressOf());
                if (FAILED(result)) return result;
                working_layout = next_layout.Get();
            } else if (bounds_dirty_) {
                result = working_layout->SetMaxWidth(width);
                if (SUCCEEDED(result)) result = working_layout->SetMaxHeight(height);
                if (FAILED(result)) return result;
            }

            result = working_layout->SetFontSize(style_.font_size * font_scale_, full_range);
            if (FAILED(result)) return result;
            if (suffix_start_ < text_.size()) {
                result = working_layout->SetFontSize(
                    suffix_font_size_ * font_scale_, DWRITE_TEXT_RANGE{suffix_start_, static_cast<UINT32>(text_.size()) - suffix_start_});
                if (FAILED(result)) return result;
            }

            DWRITE_TEXT_METRICS metrics{};
            result = working_layout->GetMetrics(&metrics);
            if (FAILED(result)) return result;
            intrinsic_width_ = metrics.widthIncludingTrailingWhitespace;
            content_fit_scale_ = 1.0f;
            if (fit_to_bounds_) {
                const float usable_width = std::max(1.0f, width - 4.0f);
                const float usable_height = std::max(1.0f, height - 2.0f);
                const float width_scale = metrics.widthIncludingTrailingWhitespace > usable_width ? usable_width / metrics.widthIncludingTrailingWhitespace : 1.0f;
                const float height_scale = metrics.height > usable_height ? usable_height / metrics.height : 1.0f;
                const float fit = std::min(width_scale, height_scale);
                content_fit_scale_ = fit < 1.0f ? fit * 0.96f : 1.0f;
            }
            if (next_layout != nullptr) layout_ = std::move(next_layout);
            applied_fit_scale_ = 1.0f;
            dirty_ = false;
            layout_dirty_ = false;
            bounds_dirty_ = false;
            return apply_fit_scale(content_fit_scale_);
        }

        HRESULT apply_fit_scale(float fit_scale) noexcept {
            if (layout_ == nullptr || fit_scale <= 0.0f || !std::isfinite(fit_scale)) return E_INVALIDARG;
            if (applied_fit_scale_ == fit_scale) return S_OK;
            const UINT32 length = static_cast<UINT32>(text_.size());
            HRESULT result = layout_->SetFontSize(style_.font_size * font_scale_ * fit_scale, DWRITE_TEXT_RANGE{0, length});
            if (FAILED(result)) return result;
            if (suffix_start_ < length) {
                result = layout_->SetFontSize(
                    suffix_font_size_ * font_scale_ * fit_scale, DWRITE_TEXT_RANGE{suffix_start_, length - suffix_start_});
                if (FAILED(result)) return result;
            }
            applied_fit_scale_ = fit_scale;
            return S_OK;
        }

    private:
        std::wstring text_;
        D2duiTextStyle style_{};
        D2duiColor color_{};
        UINT32 suffix_start_ = no_suffix;
        float suffix_font_size_ = 0.0f;
        float font_scale_ = 1.0f;
        bool fit_to_bounds_ = false;
        D2D1_DRAW_TEXT_OPTIONS draw_options_ = D2D1_DRAW_TEXT_OPTIONS_CLIP;
        float intrinsic_width_ = 0.0f;
        float content_fit_scale_ = 1.0f;
        float applied_fit_scale_ = 1.0f;
        bool layout_dirty_ = true;
        bool bounds_dirty_ = true;
        ComPtr<IDWriteTextLayout> layout_;
    };

} // namespace d2dui

#endif // D2DUI_TEXT_HPP_
