#include "ui_internal.hpp"

#include <array>
#include <cwchar>
#include <iterator>

namespace ui::modes::measurement {

    void draw(detail::UiState& state, const detail::SharedDataSnapshot& shared_data) noexcept {
        if (state.user_config == nullptr) return;
        const config::UserConfig& user_config = *state.user_config;
        wchar_t reference_dpi_cell[64]{};
        wchar_t unit_cell[64]{};
        if (detail::format_reference_dpi_cell(user_config.reference_dpi, reference_dpi_cell, std::size(reference_dpi_cell)) <= 0) return;
        if (detail::format_unit_cell(user_config.unit, unit_cell, std::size(unit_cell)) <= 0) return;
        const std::array<const wchar_t*, 2> header_cells{reference_dpi_cell, unit_cell};

        detail::PageLayout page{};
        if (!detail::begin_page(state, shared_data, L"Measurement", header_cells, page)) return;

        const std::array<D2D1_RECT_F, 2> axis_cards = detail::calculate_measurement_card_bounds(page);
        constexpr std::array<const wchar_t*, 2> axis_labels{L"X \x00B7 HORIZONTAL", L"Y \x00B7 VERTICAL"};
        const std::array<double, 2> raw_counts{shared_data.accumulated_dx, shared_data.accumulated_dy};
        std::array<std::array<wchar_t, 128>, 2> value_texts{};
        for (size_t index = 0; index < value_texts.size(); ++index) {
            if (detail::format_distance_value(raw_counts[index], user_config.reference_dpi, user_config.unit, value_texts[index].data(), value_texts[index].size()) <= 0) return;
        }

        const float label_height = 34.0f * page.scale;
        const float value_width = axis_cards[0].right - axis_cards[0].left - page.card_padding * 2.0f;
        const float value_height = axis_cards[0].bottom - axis_cards[0].top - page.card_padding * 2.0f - label_height;
        const std::array<const wchar_t*, 2> group_texts{value_texts[0].data(), value_texts[1].data()};
        const float value_font_size = 56.0f * page.scale;
        if (FAILED(detail::update_measurement_value_layouts(state, group_texts, value_width, value_height, value_font_size))) return;

        for (size_t index = 0; index < axis_cards.size(); ++index) {
            detail::draw_card(state, axis_cards[index], page.card_radius);
            const D2D1_RECT_F label_bounds = D2D1::RectF(
                axis_cards[index].left + page.card_padding,
                axis_cards[index].top + page.card_padding,
                axis_cards[index].right - page.card_padding,
                axis_cards[index].top + page.card_padding + label_height);
            detail::draw_text(state, axis_labels[index], state.label_format.Get(), label_bounds, state.secondary_text_brush.Get());
            const D2D1_RECT_F value_bounds = D2D1::RectF(
                axis_cards[index].left + page.card_padding,
                label_bounds.bottom,
                axis_cards[index].right - page.card_padding,
                axis_cards[index].bottom - page.card_padding);
            state.render_target->DrawTextLayout(
                D2D1::Point2F(value_bounds.left, value_bounds.top),
                state.measurement_value_layouts[index].layout.Get(),
                state.primary_text_brush.Get(),
                D2D1_DRAW_TEXT_OPTIONS_CLIP);
        }
    }

} // namespace ui::modes::measurement
