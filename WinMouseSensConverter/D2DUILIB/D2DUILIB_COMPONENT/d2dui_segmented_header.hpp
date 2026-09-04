#pragma once

#ifndef D2DUI_SEGMENTED_HEADER_HPP_
#define D2DUI_SEGMENTED_HEADER_HPP_

#include "d2dui_line.hpp"
#include "d2dui_panel.hpp"
#include "d2dui_text.hpp"

#include <algorithm>
#include <memory>
#include <new>
#include <string>
#include <string_view>
#include <vector>

namespace d2dui {

    class D2duiSegmentedHeader final : public D2duiComponentsBase {
    public:
        D2duiSegmentedHeader() {
            leading_panel_.set_fill_color({0xE8F0FE, 1.0f});
            leading_panel_.set_radius(14.0f);
            group_panel_.set_fill_color({0xFFFFFF, 1.0f});
            group_panel_.set_border({0xE1E7EF, 1.0f});
            group_panel_.set_radius(8.0f);
            leading_text_.set_color({0x2563EB, 1.0f});
            leading_text_.set_style(default_style());
        }

        [[nodiscard]] const D2D1_RECT_F& get_bounds() const noexcept override { return bounds_; }

        void resize(const D2D1_RECT_F& bounds, float scale) noexcept override {
            if (bounds_.left == bounds.left && bounds_.top == bounds.top && bounds_.right == bounds.right && bounds_.bottom == bounds.bottom && scale_ == scale) return;
            bounds_ = bounds;
            scale_ = scale;
            dirty_ = true;
        }

        HRESULT draw(D2duiContext& context) noexcept override {
            if (cells_.empty()) return E_UNEXPECTED;
            HRESULT result = S_OK;
            if (dirty_) {
                result = arrange(context);
                if (FAILED(result)) return result;
            }
            result = leading_panel_.draw(context);
            if (FAILED(result)) return result;
            result = leading_text_.draw(context);
            if (FAILED(result)) return result;
            result = group_panel_.draw(context);
            if (FAILED(result)) return result;
            for (size_t index = 1; index < cells_.size(); ++index) {
                result = dividers_[index - 1]->draw(context);
                if (FAILED(result)) return result;
            }
            for (const auto& cell : cells_) {
                result = cell->draw(context);
                if (FAILED(result)) return result;
            }
            return S_OK;
        }

        void on_click() noexcept override {}

        void set_leading_text(std::wstring text) { leading_text_.set_text(std::move(text)); }

        void set_cells(const std::vector<std::wstring>& texts) {
            if (cells_.size() != texts.size()) {
                cells_.clear();
                dividers_.clear();
                cells_.reserve(texts.size());
                for (const auto& text : texts) {
                    auto cell = std::make_unique<D2duiText>(text, default_style(), D2duiColor{0x687386, 1.0f});
                    cells_.push_back(std::move(cell));
                }
                for (size_t index = 1; index < texts.size(); ++index) {
                    auto divider = std::make_unique<D2duiLine>();
                    divider->set_color({0xE1E7EF, 1.0f});
                    dividers_.push_back(std::move(divider));
                }
            } else {
                for (size_t index = 0; index < texts.size(); ++index) cells_[index]->set_text(texts[index]);
            }
            dirty_ = true;
        }

        void set_cell_text(size_t index, std::wstring_view text) {
            D2duiText& cell = *cells_.at(index);
            const std::wstring& current = cell.text();
            if (std::wstring_view(current.data(), current.size()) == text) return;
            cell.set_text(std::wstring(text));
            dirty_ = true;
        }

        [[nodiscard]] size_t cell_count() const noexcept { return cells_.size(); }
        [[nodiscard]] const D2D1_RECT_F& leading_bounds() const noexcept { return leading_panel_.get_bounds(); }
        [[nodiscard]] const D2D1_RECT_F& cell_bounds(size_t index) const { return cells_.at(index)->get_bounds(); }

    private:
        static D2duiTextStyle default_style() {
            D2duiTextStyle style{};
            style.font_size = 14.0f;
            style.weight = DWRITE_FONT_WEIGHT_SEMI_BOLD;
            return style;
        }

        HRESULT arrange(D2duiContext& context) noexcept {
            try {
                const float leading_width = 126.0f * scale_;
                const D2D1_RECT_F leading_bounds = D2D1::RectF(
                    bounds_.left, bounds_.top + 2.0f * scale_, bounds_.left + leading_width, bounds_.bottom - 2.0f * scale_);
                leading_panel_.resize(leading_bounds, scale_);
                leading_text_.resize(leading_bounds, scale_);

                std::vector<float> natural_widths;
                natural_widths.resize(cells_.size());
                float natural_total = 0.0f;
                const float padding = 12.0f * scale_;
                for (size_t index = 0; index < cells_.size(); ++index) {
                    cells_[index]->set_style(default_style());
                    cells_[index]->resize(D2D1::RectF(0.0f, 0.0f, 4096.0f, 64.0f), scale_);
                    const HRESULT result = cells_[index]->prepare_layout(context);
                    if (FAILED(result)) return result;
                    natural_widths[index] = std::max(1.0f, cells_[index]->intrinsic_width()) + padding * 2.0f;
                    natural_total += natural_widths[index];
                }

                const float gap = 12.0f * scale_;
                const float available = std::max(1.0f, bounds_.right - leading_bounds.right - gap);
                const float fit = std::min(1.0f, available / std::max(1.0f, natural_total));
                float x = bounds_.right - natural_total * fit;
                const D2D1_RECT_F group_bounds = D2D1::RectF(x, leading_bounds.top, bounds_.right, leading_bounds.bottom);
                group_panel_.resize(group_bounds, scale_);
                for (size_t index = 0; index < cells_.size(); ++index) {
                    const float next_x = index + 1 == cells_.size() ? bounds_.right : x + natural_widths[index] * fit;
                    cells_[index]->set_style(default_style());
                    cells_[index]->resize(D2D1::RectF(x, group_bounds.top, next_x, group_bounds.bottom), scale_);
                    HRESULT result = cells_[index]->prepare_layout(context);
                    if (FAILED(result)) return result;
                    result = cells_[index]->apply_fit_scale(fit);
                    if (FAILED(result)) return result;
                    if (index > 0) {
                        dividers_[index - 1]->resize(D2D1::RectF(x, group_bounds.top + 1.0f, x, group_bounds.bottom - 1.0f), scale_);
                    }
                    x = next_x;
                }
                dirty_ = false;
                return S_OK;
            } catch (const std::bad_alloc&) {
                return E_OUTOFMEMORY;
            } catch (...) {
                return E_FAIL;
            }
        }

        D2duiPanel leading_panel_;
        D2duiText leading_text_;
        D2duiPanel group_panel_;
        std::vector<std::unique_ptr<D2duiText>> cells_;
        std::vector<std::unique_ptr<D2duiLine>> dividers_;
    };

} // namespace d2dui

#endif // D2DUI_SEGMENTED_HEADER_HPP_
