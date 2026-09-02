#include "ui_internal.hpp"

#include "sync.hpp"

#include <algorithm>
#include <cmath>
#include <cwchar>
#include <iterator>
#include <limits>
#include <utility>

namespace ui::detail {

    const wchar_t* unit_name(Unit unit) noexcept {
        switch (unit) {
            case Unit::raw:
                return L"raw";
            case Unit::inch:
                return L"inch";
            case Unit::mm:
                return L"mm";
            case Unit::cm:
                return L"cm";
            case Unit::dm:
                return L"dm";
            case Unit::m:
                return L"m";
        }
        return L"raw";
    }

    double convert_distance(double raw_count, int reference_dpi, Unit unit) noexcept {
        if (unit == Unit::raw) return raw_count;

        const double inches = raw_count / static_cast<double>(reference_dpi);
        switch (unit) {
            case Unit::inch:
                return inches;
            case Unit::mm:
                return inches * 25.4;
            case Unit::cm:
                return inches * 2.54;
            case Unit::dm:
                return inches * 0.254;
            case Unit::m:
                return inches * 0.0254;
            case Unit::raw:
                return raw_count;
        }
        return raw_count;
    }

    double calibration_dpi(double dx, double dy, int calibration_distance_cm) noexcept {
        const double counts = std::hypot(dx, dy);
        const double target_inches = static_cast<double>(calibration_distance_cm) / 2.54;
        return target_inches > 0.0 ? counts / target_inches : 0.0;
    }

    SharedDataSnapshot capture_shared_data() noexcept {
        return SharedDataSnapshot{
            public_data::current_mode_,
            public_data::on_recording_ != 0,
            public_data::accumulated_muzmov_dx,
            public_data::accumulated_muzmov_dy,
        };
    }

    static double normalize_display_value(double value) noexcept {
        return value > -0.0005 && value < 0.0005 ? 0.0 : value;
    }

    int format_distance_value(double raw_count, int reference_dpi, Unit unit, wchar_t* text, size_t capacity) noexcept {
        if (text == nullptr || capacity == 0) return -1;
        const double value = normalize_display_value(convert_distance(raw_count, reference_dpi, unit));
        if (std::isfinite(value) && std::abs(value) < 1.0e9) return swprintf_s(text, capacity, L"%.3f", value);
        return swprintf_s(text, capacity, L"%.3e", value);
    }

    int format_reference_dpi_cell(int reference_dpi, wchar_t* text, size_t capacity) noexcept {
        if (text == nullptr || capacity == 0) return -1;
        return swprintf_s(text, capacity, L"REFDPI %d", reference_dpi);
    }

    int format_calibration_distance_cell(int calibration_distance_cm, int reference_dpi, Unit unit, wchar_t* text, size_t capacity) noexcept {
        if (text == nullptr || capacity == 0) return -1;
        const double target_inches = static_cast<double>(calibration_distance_cm) / 2.54;
        const double target_reference_counts = target_inches * static_cast<double>(reference_dpi);
        wchar_t distance[128]{};
        if (format_distance_value(target_reference_counts, reference_dpi, unit, distance, std::size(distance)) <= 0) return -1;
        return swprintf_s(text, capacity, L"CALDIS %ls", distance);
    }

    int format_unit_cell(Unit unit, wchar_t* text, size_t capacity) noexcept {
        if (text == nullptr || capacity == 0) return -1;
        return swprintf_s(text, capacity, L"UNIT %ls", unit_name(unit));
    }

    void draw_text(UiState& state, const wchar_t* text, IDWriteTextFormat* format, const D2D1_RECT_F& bounds, ID2D1Brush* brush, D2D1_DRAW_TEXT_OPTIONS options) noexcept {
        state.render_target->DrawTextW(text, static_cast<UINT32>(std::wcslen(text)), format, bounds, brush, options);
    }

    void draw_card(UiState& state, const D2D1_RECT_F& bounds, float radius) noexcept {
        const D2D1_RECT_F shadow_bounds = D2D1::RectF(bounds.left, bounds.top + 2.0f, bounds.right, bounds.bottom + 2.0f);
        state.render_target->FillRoundedRectangle(D2D1::RoundedRect(shadow_bounds, radius, radius), state.shadow_brush.Get());
        const D2D1_ROUNDED_RECT card = D2D1::RoundedRect(bounds, radius, radius);
        state.render_target->FillRoundedRectangle(card, state.surface_brush.Get());
        state.render_target->DrawRoundedRectangle(card, state.border_brush.Get(), 1.0f);
    }

    PageLayout calculate_page_layout(float width, float height, float shortcut_badge_width, const std::array<float, 2>& header_cell_text_widths) noexcept {
        PageLayout layout{};
        if (width <= 1.0f || height <= 1.0f) return layout;

        layout.scale = std::clamp(std::min(width / 800.0f, height / 450.0f), 0.80f, 1.0f);
        const float horizontal_margin = std::clamp(width * 0.04f, 16.0f, 40.0f);
        const float vertical_margin = std::clamp(height * 0.04f, 14.0f, 32.0f);
        layout.content_width = std::max(1.0f, std::min(width - horizontal_margin * 2.0f, 1040.0f));
        const float left = (width - layout.content_width) * 0.5f;
        const float right = left + layout.content_width;
        const float header_height = 32.0f * layout.scale;
        const float footer_height = 56.0f * layout.scale;
        const float section_gap = 14.0f * layout.scale;
        const float available_data_height = std::max(1.0f, height - vertical_margin * 2.0f - header_height - footer_height - section_gap * 2.0f);
        const float data_height = std::min(420.0f, available_data_height);
        const float block_height = header_height + section_gap + data_height + section_gap + footer_height;
        const float top = std::max(vertical_margin, (height - block_height) * 0.5f);

        layout.card_gap = 16.0f * layout.scale;
        layout.card_padding = 22.0f * layout.scale;
        layout.card_radius = 16.0f * layout.scale;
        layout.header_bounds = D2D1::RectF(left, top, right, top + header_height);
        const float mode_width = 126.0f * layout.scale;
        layout.mode_pill_bounds = D2D1::RectF(left, top + 2.0f * layout.scale, left + mode_width, top + header_height - 2.0f * layout.scale);
        const float header_gap = 12.0f * layout.scale;
        const float header_cell_padding = 12.0f * layout.scale;
        const float available_header_width = std::max(1.0f, right - layout.mode_pill_bounds.right - header_gap);
        const std::array<float, 2> natural_cell_widths{
            std::max(1.0f, header_cell_text_widths[0]) + header_cell_padding * 2.0f,
            std::max(1.0f, header_cell_text_widths[1]) + header_cell_padding * 2.0f,
        };
        const float natural_group_width = natural_cell_widths[0] + natural_cell_widths[1];
        layout.header_cell_text_scale = std::min(1.0f, available_header_width / natural_group_width);
        const float first_cell_width = natural_cell_widths[0] * layout.header_cell_text_scale;
        const float second_cell_width = natural_cell_widths[1] * layout.header_cell_text_scale;
        layout.header_cell_bounds[1] = D2D1::RectF(
            right - second_cell_width,
            layout.mode_pill_bounds.top,
            right,
            layout.mode_pill_bounds.bottom);
        layout.header_cell_bounds[0] = D2D1::RectF(
            layout.header_cell_bounds[1].left - first_cell_width,
            layout.mode_pill_bounds.top,
            layout.header_cell_bounds[1].left,
            layout.mode_pill_bounds.bottom);
        layout.data_bounds = D2D1::RectF(left, layout.header_bounds.bottom + section_gap, right, layout.header_bounds.bottom + section_gap + data_height);
        layout.footer_bounds = D2D1::RectF(left, layout.data_bounds.bottom + section_gap, right, layout.data_bounds.bottom + section_gap + footer_height);

        const float footer_padding = 16.0f * layout.scale;
        const float badge_height = 34.0f * layout.scale;
        const float badge_top = layout.footer_bounds.top + (footer_height - badge_height) * 0.5f;
        const float badge_width = std::max(48.0f, shortcut_badge_width);
        layout.shortcut_badge_bounds = D2D1::RectF(left + footer_padding, badge_top, left + footer_padding + badge_width, badge_top + badge_height);
        const float switch_width = 44.0f * layout.scale;
        const float switch_height = 24.0f * layout.scale;
        const float switch_right = right - footer_padding;
        const float switch_top = layout.footer_bounds.top + (footer_height - switch_height) * 0.5f;
        layout.switch_track_bounds = D2D1::RectF(switch_right - switch_width, switch_top, switch_right, switch_top + switch_height);
        layout.shortcut_text_bounds = D2D1::RectF(layout.shortcut_badge_bounds.right + 14.0f * layout.scale, layout.footer_bounds.top, layout.switch_track_bounds.left - 16.0f * layout.scale, layout.footer_bounds.bottom);
        return layout;
    }

    std::array<D2D1_RECT_F, 2> calculate_measurement_card_bounds(const PageLayout& layout) noexcept {
        const float half_gap = layout.card_gap * 0.5f;
        const float center_x = (layout.data_bounds.left + layout.data_bounds.right) * 0.5f;
        return {
            D2D1::RectF(layout.data_bounds.left, layout.data_bounds.top, center_x - half_gap, layout.data_bounds.bottom),
            D2D1::RectF(center_x + half_gap, layout.data_bounds.top, layout.data_bounds.right, layout.data_bounds.bottom),
        };
    }

    HRESULT update_header_cell_layout(UiState& state, HeaderCellLayoutCache& cache, const wchar_t* text, float font_size) noexcept {
        if (text == nullptr || font_size <= 0.0f) return E_INVALIDARG;
        const UINT32 text_length = static_cast<UINT32>(std::wcslen(text));
        if (text_length >= cache.display_text.size()) return E_INVALIDARG;

        const bool text_changed = cache.layout == nullptr
            || cache.display_text_length != text_length
            || std::wmemcmp(cache.display_text.data(), text, text_length) != 0;
        if (text_changed) {
            constexpr float base_font_size = 14.0f;
            ComPtr<IDWriteTextLayout> text_layout;
            HRESULT result = state.write_factory->CreateTextLayout(
                text,
                text_length,
                state.header_cell_format.Get(),
                4096.0f,
                64.0f,
                text_layout.GetAddressOf());
            if (FAILED(result)) return result;
            result = text_layout->SetFontSize(base_font_size, DWRITE_TEXT_RANGE{0, text_length});
            if (FAILED(result)) return result;

            DWRITE_TEXT_METRICS metrics{};
            result = text_layout->GetMetrics(&metrics);
            if (FAILED(result)) return result;

            cache.layout = std::move(text_layout);
            std::copy_n(text, text_length + 1, cache.display_text.begin());
            cache.display_text_length = text_length;
            cache.font_size = base_font_size;
            cache.intrinsic_width = metrics.widthIncludingTrailingWhitespace;
        }

        if (cache.font_size == font_size) return S_OK;
        const HRESULT result = cache.layout->SetFontSize(font_size, DWRITE_TEXT_RANGE{0, cache.display_text_length});
        if (SUCCEEDED(result)) cache.font_size = font_size;
        return result;
    }

    bool begin_page(UiState& state, const SharedDataSnapshot& shared_data, const wchar_t* mode_name, const std::array<const wchar_t*, 2>& header_cells, PageLayout& layout) noexcept {
        ID2D1HwndRenderTarget* target = state.render_target.Get();
        const D2D1_SIZE_F size = target->GetSize();
        constexpr float header_font_size = 14.0f;
        std::array<float, 2> header_cell_text_widths{};
        for (size_t index = 0; index < header_cells.size(); ++index) {
            HeaderCellLayoutCache& cache = state.header_cell_layouts[index];
            const float retained_font_size = cache.layout != nullptr ? cache.font_size : header_font_size;
            if (FAILED(update_header_cell_layout(state, cache, header_cells[index], retained_font_size))) return false;
            header_cell_text_widths[index] = cache.intrinsic_width;
        }

        layout = calculate_page_layout(size.width, size.height, state.shortcut_badge_width, header_cell_text_widths);
        if (layout.content_width <= 1.0f || layout.data_bounds.bottom <= layout.data_bounds.top) return false;

        target->FillRoundedRectangle(D2D1::RoundedRect(layout.mode_pill_bounds, (layout.mode_pill_bounds.bottom - layout.mode_pill_bounds.top) * 0.5f, (layout.mode_pill_bounds.bottom - layout.mode_pill_bounds.top) * 0.5f), state.mode_fill_brush.Get());
        draw_text(state, mode_name, state.mode_format.Get(), layout.mode_pill_bounds, state.accent_brush.Get());

        const D2D1_RECT_F header_cell_group_bounds = D2D1::RectF(
            layout.header_cell_bounds[0].left,
            layout.header_cell_bounds[0].top,
            layout.header_cell_bounds[1].right,
            layout.header_cell_bounds[1].bottom);
        const float header_cell_radius = 8.0f * layout.scale;
        const D2D1_ROUNDED_RECT header_cell_group = D2D1::RoundedRect(header_cell_group_bounds, header_cell_radius, header_cell_radius);
        target->FillRoundedRectangle(header_cell_group, state.surface_brush.Get());
        target->DrawRoundedRectangle(header_cell_group, state.border_brush.Get(), 1.0f);
        const float divider_x = layout.header_cell_bounds[0].right;
        target->DrawLine(
            D2D1::Point2F(divider_x, header_cell_group_bounds.top + 1.0f),
            D2D1::Point2F(divider_x, header_cell_group_bounds.bottom - 1.0f),
            state.border_brush.Get(),
            1.0f);

        const float fitted_header_font_size = header_font_size * layout.header_cell_text_scale;
        for (size_t index = 0; index < header_cells.size(); ++index) {
            HeaderCellLayoutCache& cache = state.header_cell_layouts[index];
            if (FAILED(update_header_cell_layout(state, cache, header_cells[index], fitted_header_font_size))) return false;
            const D2D1_RECT_F& bounds = layout.header_cell_bounds[index];
            if (FAILED(cache.layout->SetMaxWidth(bounds.right - bounds.left))) return false;
            if (FAILED(cache.layout->SetMaxHeight(bounds.bottom - bounds.top))) return false;
            target->DrawTextLayout(D2D1::Point2F(bounds.left, bounds.top), cache.layout.Get(), state.secondary_text_brush.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
        }

        draw_card(state, layout.footer_bounds, layout.card_radius);
        target->FillRoundedRectangle(D2D1::RoundedRect(layout.shortcut_badge_bounds, 9.0f * layout.scale, 9.0f * layout.scale), state.accent_brush.Get());
        draw_text(state, state.recording_key_name.data(), state.badge_format.Get(), layout.shortcut_badge_bounds, state.surface_brush.Get());
        draw_text(state, L"Recording", state.footer_format.Get(), layout.shortcut_text_bounds, state.primary_text_brush.Get());

        state.switch_track_brush->SetColor(D2D1::ColorF(shared_data.recording ? 0x34C759 : 0xC7CBD1));
        const float track_height = layout.switch_track_bounds.bottom - layout.switch_track_bounds.top;
        const float track_radius = track_height * 0.5f;
        target->FillRoundedRectangle(D2D1::RoundedRect(layout.switch_track_bounds, track_radius, track_radius), state.switch_track_brush.Get());
        const float inset = 2.0f * layout.scale;
        const float thumb_radius = 10.0f * layout.scale;
        const float thumb_x = shared_data.recording
            ? layout.switch_track_bounds.right - inset - thumb_radius
            : layout.switch_track_bounds.left + inset + thumb_radius;
        const float thumb_y = (layout.switch_track_bounds.top + layout.switch_track_bounds.bottom) * 0.5f;
        target->FillEllipse(D2D1::Ellipse(D2D1::Point2F(thumb_x, thumb_y), thumb_radius, thumb_radius), state.surface_brush.Get());
        return true;
    }

    HRESULT fitting_numeric_font_size(UiState& state, const wchar_t* const* texts, size_t text_count, float width, float height, float requested_font_size, float& fitted_font_size) noexcept {
        if (texts == nullptr || text_count == 0) return E_INVALIDARG;
        float fit_scale = 1.0f;
        for (size_t index = 0; index < text_count; ++index) {
            if (texts[index] == nullptr) return E_INVALIDARG;
            const UINT32 length = static_cast<UINT32>(std::wcslen(texts[index]));
            ComPtr<IDWriteTextLayout> layout;
            HRESULT result = state.write_factory->CreateTextLayout(texts[index], length, state.value_format.Get(), std::max(1.0f, width), std::max(1.0f, height), layout.GetAddressOf());
            if (FAILED(result)) return result;
            result = layout->SetFontSize(requested_font_size, DWRITE_TEXT_RANGE{0, length});
            if (FAILED(result)) return result;
            DWRITE_TEXT_METRICS metrics{};
            result = layout->GetMetrics(&metrics);
            if (FAILED(result)) return result;
            const float usable_width = std::max(1.0f, width - 4.0f);
            const float usable_height = std::max(1.0f, height - 2.0f);
            const float width_scale = metrics.widthIncludingTrailingWhitespace > usable_width ? usable_width / metrics.widthIncludingTrailingWhitespace : 1.0f;
            const float height_scale = metrics.height > usable_height ? usable_height / metrics.height : 1.0f;
            fit_scale = std::min(fit_scale, std::min(width_scale, height_scale));
        }
        fitted_font_size = requested_font_size * (fit_scale < 1.0f ? fit_scale * 0.96f : 1.0f);
        return S_OK;
    }

    HRESULT update_numeric_layout(UiState& state, TextLayoutCache& cache, const wchar_t* text, UINT32 suffix_start, float width, float height, float primary_font_size, float suffix_font_size) noexcept {
        if (text == nullptr) return E_INVALIDARG;
        const UINT32 text_length = static_cast<UINT32>(std::wcslen(text));
        const bool has_suffix = suffix_start < text_length;
        if (cache.layout != nullptr && cache.display_text_length == text_length && std::wmemcmp(cache.display_text.data(), text, text_length) == 0 && cache.width == width && cache.height == height && cache.primary_font_size == primary_font_size && cache.suffix_font_size == suffix_font_size && cache.suffix_start == suffix_start) return S_OK;

        ComPtr<IDWriteTextLayout> text_layout;
        HRESULT result = state.write_factory->CreateTextLayout(text, text_length, state.value_format.Get(), std::max(1.0f, width), std::max(1.0f, height), text_layout.GetAddressOf());
        if (FAILED(result)) return result;

        const DWRITE_TEXT_RANGE full_range{0, text_length};
        result = text_layout->SetFontSize(primary_font_size, full_range);
        if (FAILED(result)) return result;
        if (has_suffix) {
            result = text_layout->SetFontSize(suffix_font_size, DWRITE_TEXT_RANGE{suffix_start, text_length - suffix_start});
            if (FAILED(result)) return result;
        }

        DWRITE_TEXT_METRICS metrics{};
        result = text_layout->GetMetrics(&metrics);
        if (FAILED(result)) return result;
        const float usable_width = std::max(1.0f, width - 4.0f);
        const float usable_height = std::max(1.0f, height - 2.0f);
        const float width_scale = metrics.widthIncludingTrailingWhitespace > usable_width ? usable_width / metrics.widthIncludingTrailingWhitespace : 1.0f;
        const float height_scale = metrics.height > usable_height ? usable_height / metrics.height : 1.0f;
        const float fit_scale = std::min(width_scale, height_scale);
        if (fit_scale < 1.0f) {
            const float adjusted_scale = fit_scale * 0.96f;
            result = text_layout->SetFontSize(primary_font_size * adjusted_scale, full_range);
            if (FAILED(result)) return result;
            if (has_suffix) {
                result = text_layout->SetFontSize(suffix_font_size * adjusted_scale, DWRITE_TEXT_RANGE{suffix_start, text_length - suffix_start});
                if (FAILED(result)) return result;
            }
        }

        cache.layout = std::move(text_layout);
        std::copy_n(text, text_length + 1, cache.display_text.begin());
        cache.display_text_length = text_length;
        cache.width = width;
        cache.height = height;
        cache.primary_font_size = primary_font_size;
        cache.suffix_font_size = suffix_font_size;
        cache.suffix_start = suffix_start;
        return S_OK;
    }

} // namespace ui::detail
