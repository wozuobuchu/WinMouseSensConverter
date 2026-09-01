#include "ui_internal.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cwchar>
#include <utility>

namespace ui::detail {

    HRESULT update_calibration_dpi_layout(UiState& state, const SharedDataSnapshot& shared_data, float width, float height) noexcept {
        DpiResultLayoutCache& cache = state.calibration_dpi_layout;
        wchar_t text[64]{};
        const double counts = std::hypot(shared_data.accumulated_dx, shared_data.accumulated_dy);
        const int written = counts > 0.0
            ? swprintf_s(text, L"%.2f DPI", calibration_dpi(shared_data.accumulated_dx, shared_data.accumulated_dy, state.calibration_distance_cm))
            : swprintf_s(text, L"— DPI");
        if (written <= 0) return E_FAIL;
        const UINT32 text_length = static_cast<UINT32>(written);

        if (cache.layout != nullptr && cache.display_text_length == text_length && std::wmemcmp(cache.display_text.data(), text, text_length) == 0 && cache.width == width && cache.height == height) return S_OK;

        ComPtr<IDWriteTextLayout> layout;
        HRESULT result = state.write_factory->CreateTextLayout(text, text_length, state.value_format.Get(), std::max(1.0f, width), std::max(1.0f, height), layout.GetAddressOf());
        if (FAILED(result)) return result;

        const wchar_t* separator = std::wcschr(text, L' ');
        if (separator != nullptr) {
            const UINT32 unit_start = static_cast<UINT32>(separator - text) + 1;
            const DWRITE_TEXT_RANGE unit_range{unit_start, static_cast<UINT32>(written) - unit_start};
            result = layout->SetFontSize(18.0f, unit_range);
            if (FAILED(result)) return result;
            result = layout->SetFontWeight(DWRITE_FONT_WEIGHT_SEMI_BOLD, unit_range);
            if (FAILED(result)) return result;
        }

        cache.layout = std::move(layout);
        std::copy_n(text, text_length + 1, cache.display_text.begin());
        cache.display_text_length = text_length;
        cache.width = width;
        cache.height = height;
        return S_OK;
    }

} // namespace ui::detail

namespace ui::modes::calibration {

    void draw(detail::UiState& state, const detail::SharedDataSnapshot& shared_data) noexcept {
        detail::PageLayout page{};
        if (!detail::begin_page(state, shared_data, L"Mouse DPI Calibration", page)) return;

        ID2D1HwndRenderTarget* target = state.render_target.Get();
        const double counts = std::hypot(shared_data.accumulated_dx, shared_data.accumulated_dy);
        const double target_inches = static_cast<double>(state.calibration_distance_cm) / 2.54;
        const double target_reference_counts = target_inches * static_cast<double>(state.reference_dpi);

        const float metrics_height = std::clamp(page.main_height * 0.30f, 72.0f, 96.0f);
        const float metrics_top = page.main_card.bottom - metrics_height;
        target->DrawLine(D2D1::Point2F(page.main_card.left + page.inner_padding, metrics_top), D2D1::Point2F(page.main_card.right - page.inner_padding, metrics_top), state.border_brush.Get(), 1.0f);

        const D2D1_RECT_F result_bounds = D2D1::RectF(page.main_card.left + page.inner_padding, page.status_bounds.bottom + 4.0f, page.main_card.right - page.inner_padding, metrics_top - 2.0f);
        const float result_label_bottom = std::min(result_bounds.bottom, result_bounds.top + 24.0f);
        detail::draw_text(state, L"CALIBRATED DPI", state.label_format.Get(), D2D1::RectF(result_bounds.left, result_bounds.top + 2.0f, result_bounds.right, result_label_bottom), state.secondary_text_brush.Get());
        const D2D1_RECT_F dpi_value_bounds = D2D1::RectF(result_bounds.left, result_label_bottom, result_bounds.right, result_bounds.bottom);
        if (SUCCEEDED(detail::update_calibration_dpi_layout(state, shared_data, dpi_value_bounds.right - dpi_value_bounds.left, dpi_value_bounds.bottom - dpi_value_bounds.top))) {
            target->DrawTextLayout(D2D1::Point2F(dpi_value_bounds.left, dpi_value_bounds.top), state.calibration_dpi_layout.layout.Get(), state.primary_text_brush.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
        }

        const float center_x = (page.main_card.left + page.main_card.right) * 0.5f;
        target->DrawLine(D2D1::Point2F(center_x, metrics_top + 12.0f), D2D1::Point2F(center_x, page.main_card.bottom - 12.0f), state.border_brush.Get(), 1.0f);
        constexpr float column_gap = 14.0f;
        const std::array<D2D1_RECT_F, 2> metric_bounds{
            D2D1::RectF(page.main_card.left + page.inner_padding, metrics_top + 5.0f, center_x - column_gap, page.main_card.bottom - 4.0f),
            D2D1::RectF(center_x + column_gap, metrics_top + 5.0f, page.main_card.right - page.inner_padding, page.main_card.bottom - 4.0f),
        };
        constexpr std::array<const wchar_t*, 2> metric_labels{L"CALIBRATION DISTANCE", L"MEASURED DISTANCE"};
        const std::array<double, 2> metric_counts{target_reference_counts, counts};

        for (size_t index = 0; index < metric_bounds.size(); ++index) {
            const float label_bottom = std::min(metric_bounds[index].bottom, metric_bounds[index].top + 19.0f);
            detail::draw_text(state, metric_labels[index], state.label_format.Get(), D2D1::RectF(metric_bounds[index].left, metric_bounds[index].top, metric_bounds[index].right, label_bottom), state.secondary_text_brush.Get());
            const D2D1_RECT_F value_bounds = D2D1::RectF(metric_bounds[index].left, label_bottom, metric_bounds[index].right, metric_bounds[index].bottom);
            if (SUCCEEDED(detail::update_value_layout(state, state.calibration_value_layouts[index], metric_counts[index], value_bounds.right - value_bounds.left, value_bounds.bottom - value_bounds.top))) {
                target->DrawTextLayout(D2D1::Point2F(value_bounds.left, value_bounds.top), state.calibration_value_layouts[index].layout.Get(), state.primary_text_brush.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
            }
        }
    }

} // namespace ui::modes::calibration
