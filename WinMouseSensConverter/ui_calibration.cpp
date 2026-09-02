#include "ui_internal.hpp"

#include <cmath>
#include <cwchar>
#include <iterator>

namespace ui::detail {

    HRESULT update_calibration_dpi_layout(UiState& state, const SharedDataSnapshot& shared_data, float width, float height, float primary_font_size, float suffix_font_size) noexcept {
        wchar_t text[128]{};
        const double counts = std::hypot(shared_data.accumulated_dx, shared_data.accumulated_dy);
        const double dpi = calibration_dpi(shared_data.accumulated_dx, shared_data.accumulated_dy, state.calibration_distance_cm);
        int written = 0;
        if (counts <= 0.0) {
            written = swprintf_s(text, L"\x2014 DPI");
        } else if (std::isfinite(dpi) && std::abs(dpi) < 1.0e9) {
            written = swprintf_s(text, L"%.2f DPI", dpi);
        } else {
            written = swprintf_s(text, L"%.2e DPI", dpi);
        }
        if (written <= 0) return E_FAIL;
        const wchar_t* separator = std::wcschr(text, L' ');
        if (separator == nullptr) return E_FAIL;
        const UINT32 suffix_start = static_cast<UINT32>(separator - text) + 1;
        return update_numeric_layout(state, state.calibration_dpi_layout, text, suffix_start, width, height, primary_font_size, suffix_font_size);
    }

} // namespace ui::detail

namespace ui::modes::calibration {

    void draw(detail::UiState& state, const detail::SharedDataSnapshot& shared_data) noexcept {
        wchar_t metadata[192]{};
        if (detail::format_calibration_metadata(state.calibration_distance_cm, state.reference_dpi, state.unit, metadata, std::size(metadata)) <= 0) return;

        detail::PageLayout page{};
        if (!detail::begin_page(state, shared_data, L"Calibration", metadata, page)) return;
        detail::draw_card(state, page.data_bounds, page.card_radius);

        const float label_height = 38.0f * page.scale;
        const D2D1_RECT_F label_bounds = D2D1::RectF(
            page.data_bounds.left + page.card_padding,
            page.data_bounds.top + page.card_padding,
            page.data_bounds.right - page.card_padding,
            page.data_bounds.top + page.card_padding + label_height);
        detail::draw_text(state, L"CALIBRATED DPI", state.label_format.Get(), label_bounds, state.secondary_text_brush.Get());
        const D2D1_RECT_F value_bounds = D2D1::RectF(
            page.data_bounds.left + page.card_padding,
            label_bounds.bottom,
            page.data_bounds.right - page.card_padding,
            page.data_bounds.bottom - page.card_padding);
        if (SUCCEEDED(detail::update_calibration_dpi_layout(
                state,
                shared_data,
                value_bounds.right - value_bounds.left,
                value_bounds.bottom - value_bounds.top,
                56.0f * page.scale,
                18.0f * page.scale))) {
            state.render_target->DrawTextLayout(
                D2D1::Point2F(value_bounds.left, value_bounds.top),
                state.calibration_dpi_layout.layout.Get(),
                state.primary_text_brush.Get(),
                D2D1_DRAW_TEXT_OPTIONS_CLIP);
        }
    }

} // namespace ui::modes::calibration
