#include "ui_internal.hpp"

#include "sync.hpp"

#include <algorithm>
#include <cmath>
#include <cwchar>
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

    bool begin_page(UiState& state, const SharedDataSnapshot& shared_data, const wchar_t* title, PageLayout& layout) noexcept {
        ID2D1HwndRenderTarget* target = state.render_target.Get();
        const D2D1_SIZE_F size = target->GetSize();
        const float width = size.width;
        const float height = size.height;
        if (width <= 1.0f || height <= 1.0f) return false;

        const float outer_margin = std::clamp(width * 0.055f, 20.0f, 56.0f);
        layout.content_width = std::min(width - outer_margin * 2.0f, 960.0f);
        const float left = (width - layout.content_width) * 0.5f;
        const float right = left + layout.content_width;
        const float top = std::clamp(height * 0.055f, 18.0f, 42.0f);

        draw_text(state, title, state.title_format.Get(), D2D1::RectF(left, top, right, top + 38.0f), state.primary_text_brush.Get());

        const float main_top = top + 54.0f;
        const float instruction_height = std::clamp(height * 0.12f, 64.0f, 86.0f);
        constexpr float card_gap = 16.0f;
        const float maximum_main_height = std::max(120.0f, height - main_top - card_gap - instruction_height - std::max(18.0f, outer_margin));
        layout.main_height = std::min(std::clamp(height * 0.5f, 180.0f, 360.0f), maximum_main_height);
        layout.main_card = D2D1::RectF(left, main_top, right, main_top + layout.main_height);
        draw_card(state, layout.main_card, 18.0f);

        layout.inner_padding = std::clamp(layout.content_width * 0.04f, 22.0f, 36.0f);
        const bool recording = shared_data.recording;
        if (recording) {
            state.status_fill_brush->SetColor(D2D1::ColorF(0xE8F7EF));
            state.status_text_brush->SetColor(D2D1::ColorF(0x168A55));
        } else {
            state.status_fill_brush->SetColor(D2D1::ColorF(0xEEF1F5));
            state.status_text_brush->SetColor(D2D1::ColorF(0x687386));
        }

        const float status_top = layout.main_card.top + std::max(14.0f, layout.main_height * 0.055f);
        layout.status_bounds = D2D1::RectF(layout.main_card.left + layout.inner_padding, status_top, layout.main_card.left + layout.inner_padding + 148.0f, status_top + 32.0f);
        target->FillRoundedRectangle(D2D1::RoundedRect(layout.status_bounds, 16.0f, 16.0f), state.status_fill_brush.Get());
        target->FillEllipse(D2D1::Ellipse(D2D1::Point2F(layout.status_bounds.left + 17.0f, layout.status_bounds.top + 16.0f), 4.0f, 4.0f), state.status_text_brush.Get());
        draw_text(state, recording ? L"Recording ON" : L"Recording OFF", state.status_format.Get(), D2D1::RectF(layout.status_bounds.left + 22.0f, layout.status_bounds.top, layout.status_bounds.right - 7.0f, layout.status_bounds.bottom), state.status_text_brush.Get());

        layout.instruction_card = D2D1::RectF(left, layout.main_card.bottom + card_gap, right, layout.main_card.bottom + card_gap + instruction_height);
        draw_card(state, layout.instruction_card, 16.0f);

        const float shortcut_size = 38.0f;
        const float shortcut_left = layout.instruction_card.left + 18.0f;
        const float shortcut_top = layout.instruction_card.top + (instruction_height - shortcut_size) * 0.5f;
        const D2D1_RECT_F shortcut_badge = D2D1::RectF(shortcut_left, shortcut_top, shortcut_left + state.shortcut_badge_width, shortcut_top + shortcut_size);
        target->FillRoundedRectangle(D2D1::RoundedRect(shortcut_badge, 10.0f, 10.0f), state.accent_brush.Get());
        draw_text(state, state.recording_key_name.data(), state.status_format.Get(), shortcut_badge, state.surface_brush.Get());

        const float instruction_left = shortcut_badge.right + 18.0f;
        draw_text(state, L"Start / stop recording", state.shortcut_format.Get(), D2D1::RectF(instruction_left, layout.instruction_card.top + 8.0f, layout.instruction_card.right - 16.0f, layout.instruction_card.top + 34.0f), state.primary_text_brush.Get());
        draw_text(state, L"Starting a new recording resets the measurement.", state.body_format.Get(), D2D1::RectF(instruction_left, layout.instruction_card.top + 34.0f, layout.instruction_card.right - 16.0f, layout.instruction_card.bottom - 6.0f), state.secondary_text_brush.Get());
        return true;
    }

    HRESULT update_value_layout(UiState& state, ValueLayoutCache& cache, double raw_count, float width, float height) noexcept {
        if (cache.layout != nullptr && cache.raw_count == raw_count && cache.reference_dpi == state.reference_dpi && cache.unit == state.unit && cache.width == width && cache.height == height) return S_OK;

        const double value = normalize_display_value(convert_distance(raw_count, state.reference_dpi, state.unit));
        wchar_t text[128]{};
        const int written = swprintf_s(text, L"%.3f %ls", value, unit_name(state.unit));
        if (written <= 0) return E_FAIL;

        ComPtr<IDWriteTextLayout> text_layout;
        HRESULT result = state.write_factory->CreateTextLayout(text, static_cast<UINT32>(written), state.value_format.Get(), std::max(1.0f, width), std::max(1.0f, height), text_layout.GetAddressOf());
        if (FAILED(result)) return result;

        constexpr float default_value_font_size = 48.0f;
        constexpr float default_unit_font_size = 18.0f;
        constexpr float minimum_value_font_size = 20.0f;
        constexpr float minimum_unit_font_size = 9.0f;
        const wchar_t* separator = std::wcschr(text, L' ');
        if (separator == nullptr) return E_FAIL;

        const UINT32 separator_index = static_cast<UINT32>(separator - text);
        const UINT32 unit_start = separator_index + 1;
        const DWRITE_TEXT_RANGE value_range{0, separator_index};
        const DWRITE_TEXT_RANGE unit_range{unit_start, static_cast<UINT32>(written) - unit_start};
        result = text_layout->SetFontSize(default_value_font_size, value_range);
        if (FAILED(result)) return result;
        result = text_layout->SetFontSize(default_unit_font_size, unit_range);
        if (FAILED(result)) return result;
        result = text_layout->SetFontWeight(DWRITE_FONT_WEIGHT_SEMI_BOLD, unit_range);
        if (FAILED(result)) return result;

        DWRITE_TEXT_METRICS metrics{};
        result = text_layout->GetMetrics(&metrics);
        if (FAILED(result)) return result;
        const float usable_width = std::max(1.0f, width - 4.0f);
        const float usable_height = std::max(1.0f, height - 2.0f);
        const float width_scale = metrics.widthIncludingTrailingWhitespace > usable_width ? usable_width / metrics.widthIncludingTrailingWhitespace : 1.0f;
        const float height_scale = metrics.height > usable_height ? usable_height / metrics.height : 1.0f;
        const float scale = std::min(width_scale, height_scale);
        if (scale < 1.0f) {
            constexpr float fit_safety_factor = 0.9f;
            result = text_layout->SetFontSize(std::max(minimum_value_font_size, default_value_font_size * scale * fit_safety_factor), value_range);
            if (FAILED(result)) return result;
            result = text_layout->SetFontSize(std::max(minimum_unit_font_size, default_unit_font_size * scale * fit_safety_factor), unit_range);
            if (FAILED(result)) return result;
        }

        cache.layout = std::move(text_layout);
        cache.raw_count = raw_count;
        cache.reference_dpi = state.reference_dpi;
        cache.unit = state.unit;
        cache.width = width;
        cache.height = height;
        return S_OK;
    }

} // namespace ui::detail
