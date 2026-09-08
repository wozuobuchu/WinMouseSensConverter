#pragma once

#ifndef D2DUI_LABELED_VALUE_GRID_HPP_
#define D2DUI_LABELED_VALUE_GRID_HPP_

#include "d2dui_panel.hpp"
#include "d2dui_text.hpp"

#include <algorithm>
#include <memory>
#include <new>
#include <string>
#include <string_view>
#include <vector>

namespace d2dui {

    class D2duiLabeledValueGrid final : public D2duiComponentsBase {
    public:
        struct ItemText {
            std::wstring label;
            std::wstring value;
            UINT32 suffix_start = D2duiText::no_suffix;
        };

        [[nodiscard]] const D2D1_RECT_F& get_bounds() const noexcept override { return bounds_; }
        void resize(const D2D1_RECT_F& bounds, float scale) noexcept override {
            if (bounds_.left == bounds.left && bounds_.top == bounds.top && bounds_.right == bounds.right && bounds_.bottom == bounds.bottom && scale_ == scale) return;
            bounds_ = bounds; scale_ = scale; dirty_ = true;
        }

        HRESULT draw(D2duiContext& context) noexcept override {
            if (items_.empty()) return E_UNEXPECTED;
            if (dirty_) {
                const HRESULT arrange_result = arrange();
                if (FAILED(arrange_result)) return arrange_result;
            }
            HRESULT result = S_OK;
            float shared_fit = 1.0f;
            for (const auto& item : items_) {
                result = item->value.prepare_layout(context);
                if (FAILED(result)) return result;
                shared_fit = std::min(shared_fit, item->value.content_fit_scale());
            }
            for (const auto& item : items_) {
                result = item->value.apply_fit_scale(shared_fit);
                if (FAILED(result)) return result;
            }
            for (const auto& item : items_) {
                result = item->panel.draw(context);
                if (FAILED(result)) return result;
                result = item->label.draw(context);
                if (FAILED(result)) return result;
                result = item->value.draw(context);
                if (FAILED(result)) return result;
            }
            return S_OK;
        }

        void set_items(const std::vector<ItemText>& texts) {
            if (items_.size() != texts.size()) {
                items_.clear();
                items_.reserve(texts.size());
                for (const ItemText& text : texts) {
                    auto item = std::make_unique<Item>();
                    configure_item(*item);
                    item->label.set_text(text.label);
                    item->value.set_text(text.value);
                    item->value.set_suffix(text.suffix_start, 18.0f);
                    items_.push_back(std::move(item));
                }
            } else {
                for (size_t index = 0; index < texts.size(); ++index) {
                    items_[index]->label.set_text(texts[index].label);
                    items_[index]->value.set_text(texts[index].value);
                    items_[index]->value.set_suffix(texts[index].suffix_start, 18.0f);
                }
            }
            // Re-apply the current hover highlight so newly created cards render correctly.
            apply_highlight();
            dirty_ = true;
        }

        [[nodiscard]] size_t item_count() const noexcept { return items_.size(); }
        [[nodiscard]] const D2D1_RECT_F& item_bounds(size_t index) const { return items_.at(index)->panel.get_bounds(); }
        [[nodiscard]] D2duiText& value_component(size_t index) { return items_.at(index)->value; }

        void set_label(size_t index, std::wstring text) { items_.at(index)->label.set_text(std::move(text)); }
        void set_value(size_t index, std::wstring_view text, UINT32 suffix_start = D2duiText::no_suffix) {
            Item& item = *items_.at(index);
            const std::wstring& current = item.value.text();
            if (std::wstring_view(current.data(), current.size()) != text) {
                item.value.set_text(std::wstring(text));
            }
            item.value.set_suffix(suffix_start, 18.0f);
        }

        // Callback entry point: highlight only the card under `position`. Cards re-draw
        // each frame so just the panel colors switch; the layout stays untouched. When the
        // pointer covers no card (or the pointer leaves) no card stays highlighted.
        bool set_hover(const D2D1_POINT_2F& position) noexcept {
            return apply_hover_index(hit_test_item(position));
        }
        // Callback entry point: clear any highlighted card when the pointer leaves.
        bool clear_hover() noexcept { return apply_hover_index(kNoHover); }

    private:
        // Locate the card whose half-open DIP bounds contain `position`, if any. The same
        // half-open rule the input analyser uses keeps card borders from overlapping.
        [[nodiscard]] size_t hit_test_item(const D2D1_POINT_2F& position) const noexcept {
            for (size_t index = 0; index < items_.size(); ++index) {
                const D2D1_RECT_F& card = items_[index]->panel.get_bounds();
                if (position.x >= card.left && position.x < card.right
                    && position.y >= card.top && position.y < card.bottom) {
                    return index;
                }
            }
            return kNoHover;
        }

        // Switch the hovered card, skipping redundant redraw repainting when unchanged.
        bool apply_hover_index(size_t index) noexcept {
            if (hovered_ == index) return false;
            hovered_ = index;
            apply_highlight();
            return true;
        }

        // Recolor every card from its current geometry; only the hovered index differs.
        void apply_highlight() noexcept {
            for (size_t index = 0; index < items_.size(); ++index) {
                const bool active = index == hovered_;
                items_[index]->panel.set_fill_color(active ? hover_fill_ : normal_fill_);
                items_[index]->panel.set_border(active ? hover_border_ : normal_border_);
            }
        }

        static constexpr D2duiColor normal_fill_{0xFFFFFF, 1.0f};
        static constexpr D2duiColor normal_border_{0xE1E7EF, 1.0f};
        static constexpr D2duiColor hover_fill_{0xEFF6FF, 1.0f};
        static constexpr D2duiColor hover_border_{0x3B82F6, 1.0f};
        static constexpr size_t kNoHover = static_cast<size_t>(-1);

        size_t hovered_ = kNoHover;
        struct Item {
            D2duiPanel panel;
            D2duiText label;
            D2duiText value;
        };

        static D2duiTextStyle label_style() {
            D2duiTextStyle style{};
            style.font_size = 14.0f;
            style.weight = DWRITE_FONT_WEIGHT_SEMI_BOLD;
            return style;
        }

        static D2duiTextStyle value_style() {
            D2duiTextStyle style{};
            style.font_size = 56.0f;
            style.weight = DWRITE_FONT_WEIGHT_SEMI_BOLD;
            return style;
        }

        static void configure_item(Item& item) {
            item.panel.set_fill_color({0xFFFFFF, 1.0f});
            item.panel.set_border({0xE1E7EF, 1.0f});
            item.panel.set_shadow({0xD8E0EA, 0.72f});
            item.panel.set_radius(16.0f);
            item.label.set_style(label_style());
            item.label.set_color({0x687386, 1.0f});
            item.value.set_color({0x172033, 1.0f});
            item.value.set_fit_to_bounds(true);
        }

        HRESULT arrange() noexcept {
            try {
                const float gap = 16.0f * scale_;
                const float padding = 22.0f * scale_;
                const float label_height = (items_.size() == 1 ? 38.0f : 34.0f) * scale_;
                const float total_gap = gap * static_cast<float>(items_.size() - 1);
                const float card_width = std::max(1.0f, (bounds_.right - bounds_.left - total_gap) / static_cast<float>(items_.size()));
                for (size_t index = 0; index < items_.size(); ++index) {
                    const float left = bounds_.left + static_cast<float>(index) * (card_width + gap);
                    const float right = index + 1 == items_.size() ? bounds_.right : left + card_width;
                    const D2D1_RECT_F card = D2D1::RectF(left, bounds_.top, right, bounds_.bottom);
                    items_[index]->panel.resize(card, scale_);
                    items_[index]->label.set_style(label_style());
                    items_[index]->label.resize(D2D1::RectF(
                        left + padding, bounds_.top + padding, right - padding, bounds_.top + padding + label_height), scale_);
                    items_[index]->value.set_style(value_style());
                    items_[index]->value.set_font_scale(scale_);
                    items_[index]->value.set_suffix(items_[index]->value.suffix_start(), 18.0f);
                    items_[index]->value.resize(D2D1::RectF(
                        left + padding, bounds_.top + padding + label_height, right - padding, bounds_.bottom - padding), scale_);
                }
                dirty_ = false;
                return S_OK;
            } catch (const std::bad_alloc&) {
                return E_OUTOFMEMORY;
            } catch (...) {
                return E_FAIL;
            }
        }

        std::vector<std::unique_ptr<Item>> items_;
    };

} // namespace d2dui

#endif // D2DUI_LABELED_VALUE_GRID_HPP_
