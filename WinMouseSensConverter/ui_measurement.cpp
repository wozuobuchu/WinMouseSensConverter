#include "ui_internal.hpp"

#include <algorithm>
#include <array>
#include <cwchar>

namespace ui::modes::measurement {

    void draw(detail::UiState& state, const detail::SharedDataSnapshot& shared_data) noexcept {
        detail::PageLayout page{};
        if (!detail::begin_page(state, shared_data, L"Mouse Sensitivity Meter", page)) return;

        ID2D1HwndRenderTarget* target = state.render_target.Get();
        const float settings_height = std::clamp(page.main_height * 0.30f, 58.0f, 82.0f);
        const float settings_top = page.main_card.bottom - settings_height;
        target->DrawLine(D2D1::Point2F(page.main_card.left + page.inner_padding, settings_top), D2D1::Point2F(page.main_card.right - page.inner_padding, settings_top), state.border_brush.Get(), 1.0f);

        const float center_x = (page.main_card.left + page.main_card.right) * 0.5f;
        const D2D1_RECT_F measurement_bounds = D2D1::RectF(page.main_card.left + page.inner_padding, page.status_bounds.bottom + 3.0f, page.main_card.right - page.inner_padding, settings_top - 2.0f);
        const float divider_top = measurement_bounds.top + 8.0f;
        const float divider_bottom = measurement_bounds.bottom - 8.0f;
        if (divider_bottom > divider_top) target->DrawLine(D2D1::Point2F(center_x, divider_top), D2D1::Point2F(center_x, divider_bottom), state.border_brush.Get(), 1.0f);

        constexpr float axis_gap = 14.0f;
        const std::array<D2D1_RECT_F, 2> axis_bounds{
            D2D1::RectF(measurement_bounds.left, measurement_bounds.top, center_x - axis_gap, measurement_bounds.bottom),
            D2D1::RectF(center_x + axis_gap, measurement_bounds.top, measurement_bounds.right, measurement_bounds.bottom),
        };
        constexpr std::array<const wchar_t*, 2> axis_labels{L"X / HORIZONTAL", L"Y / VERTICAL"};
        const std::array<double, 2> raw_counts{shared_data.accumulated_dx, shared_data.accumulated_dy};
        const float label_top = measurement_bounds.top + 1.0f;
        const float label_bottom = std::min(measurement_bounds.bottom, label_top + 18.0f);

        for (size_t index = 0; index < axis_bounds.size(); ++index) {
            detail::draw_text(state, axis_labels[index], state.label_format.Get(), D2D1::RectF(axis_bounds[index].left, label_top, axis_bounds[index].right, label_bottom), state.secondary_text_brush.Get());
            const D2D1_RECT_F value_bounds = D2D1::RectF(axis_bounds[index].left, label_bottom, axis_bounds[index].right, axis_bounds[index].bottom);
            if (SUCCEEDED(detail::update_value_layout(state, state.measurement_value_layouts[index], raw_counts[index], value_bounds.right - value_bounds.left, value_bounds.bottom - value_bounds.top))) {
                target->DrawTextLayout(D2D1::Point2F(value_bounds.left, value_bounds.top), state.measurement_value_layouts[index].layout.Get(), state.primary_text_brush.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
            }
        }

        target->DrawLine(D2D1::Point2F(center_x, settings_top + 12.0f), D2D1::Point2F(center_x, page.main_card.bottom - 12.0f), state.border_brush.Get(), 1.0f);
        const D2D1_RECT_F dpi_label_bounds = D2D1::RectF(page.main_card.left + page.inner_padding, settings_top + 7.0f, center_x - 12.0f, settings_top + 27.0f);
        const D2D1_RECT_F unit_label_bounds = D2D1::RectF(center_x + 12.0f, settings_top + 7.0f, page.main_card.right - page.inner_padding, settings_top + 27.0f);
        detail::draw_text(state, L"REFERENCE DPI", state.label_format.Get(), dpi_label_bounds, state.secondary_text_brush.Get());
        detail::draw_text(state, L"OUTPUT UNIT", state.label_format.Get(), unit_label_bounds, state.secondary_text_brush.Get());

        wchar_t dpi_text[16]{};
        swprintf_s(dpi_text, L"%d", state.reference_dpi);
        detail::draw_text(state, dpi_text, state.setting_format.Get(), D2D1::RectF(dpi_label_bounds.left, settings_top + 25.0f, dpi_label_bounds.right, page.main_card.bottom - 5.0f), state.primary_text_brush.Get());
        detail::draw_text(state, detail::unit_name(state.unit), state.setting_format.Get(), D2D1::RectF(unit_label_bounds.left, settings_top + 25.0f, unit_label_bounds.right, page.main_card.bottom - 5.0f), state.primary_text_brush.Get());
    }

} // namespace ui::modes::measurement
