#pragma once

#ifndef UI_INTERNAL_HPP_
#define UI_INTERNAL_HPP_

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "config.hpp"

#include <Windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>

#include <array>
#include <cstddef>
#include <limits>

namespace ui::detail {

    using Microsoft::WRL::ComPtr;
    using Unit = config::OutputUnit;

    struct TextLayoutCache {
        ComPtr<IDWriteTextLayout> layout;
        std::array<wchar_t, 128> display_text{};
        UINT32 display_text_length = 0;
        float width = 0.0f;
        float height = 0.0f;
        float primary_font_size = 0.0f;
        float suffix_font_size = 0.0f;
        UINT32 suffix_start = std::numeric_limits<UINT32>::max();
    };

    struct HeaderCellLayoutCache {
        ComPtr<IDWriteTextLayout> layout;
        std::array<wchar_t, 128> display_text{};
        UINT32 display_text_length = 0;
        float font_size = 0.0f;
        float intrinsic_width = 0.0f;
    };

    struct UiState {
        HWND hwnd = nullptr;
        HWND about_dialog = nullptr;
        HWND instruction_dialog = nullptr;
        HWND custom_dpi_dialog = nullptr;
        HWND custom_calibration_distance_dialog = nullptr;
        HMENU root_menu = nullptr;
        bool owned_by_window = false;
        bool in_size_move = false;
        bool minimized = false;
        bool redraw_dirty = true;
        UINT dpi = USER_DEFAULT_SCREEN_DPI;
        config::UserConfig* user_config = nullptr;
        std::array<wchar_t, 64> recording_key_name{L'F', L'1', L'\0'};
        UINT32 recording_key_name_length = 2;
        float shortcut_badge_width = 48.0f;

        int reference_dpi = 800;
        UINT reference_dpi_command = 0;
        Unit unit = Unit::cm;
        int calibration_distance_cm = 10;
        UINT calibration_distance_command = 0;

        ComPtr<ID2D1Factory> d2d_factory;
        ComPtr<IDWriteFactory> write_factory;

        ComPtr<IDWriteTextFormat> value_format;
        ComPtr<IDWriteTextFormat> label_format;
        ComPtr<IDWriteTextFormat> header_cell_format;
        ComPtr<IDWriteTextFormat> mode_format;
        ComPtr<IDWriteTextFormat> footer_format;
        ComPtr<IDWriteTextFormat> badge_format;

        ComPtr<ID2D1HwndRenderTarget> render_target;
        ComPtr<ID2D1SolidColorBrush> surface_brush;
        ComPtr<ID2D1SolidColorBrush> border_brush;
        ComPtr<ID2D1SolidColorBrush> shadow_brush;
        ComPtr<ID2D1SolidColorBrush> primary_text_brush;
        ComPtr<ID2D1SolidColorBrush> secondary_text_brush;
        ComPtr<ID2D1SolidColorBrush> accent_brush;
        ComPtr<ID2D1SolidColorBrush> mode_fill_brush;
        ComPtr<ID2D1SolidColorBrush> switch_track_brush;

        std::array<TextLayoutCache, 2> measurement_value_layouts;
        TextLayoutCache calibration_dpi_layout;
        std::array<HeaderCellLayoutCache, 2> header_cell_layouts;

        ~UiState() {
            if (root_menu != nullptr) {
                DestroyMenu(root_menu);
                root_menu = nullptr;
            }
        }
    };

    struct PageLayout {
        float scale = 1.0f;
        float content_width = 0.0f;
        float card_gap = 0.0f;
        float card_padding = 0.0f;
        float card_radius = 0.0f;
        float header_cell_text_scale = 1.0f;
        D2D1_RECT_F header_bounds{};
        D2D1_RECT_F mode_pill_bounds{};
        std::array<D2D1_RECT_F, 2> header_cell_bounds{};
        D2D1_RECT_F data_bounds{};
        D2D1_RECT_F footer_bounds{};
        D2D1_RECT_F shortcut_badge_bounds{};
        D2D1_RECT_F shortcut_text_bounds{};
        D2D1_RECT_F switch_track_bounds{};
    };

    struct SharedDataSnapshot {
        config::AppMode mode = config::AppMode::measurement;
        bool recording = false;
        double accumulated_dx = 0.0;
        double accumulated_dy = 0.0;
    };

    const wchar_t* unit_name(Unit unit) noexcept;
    double convert_distance(double raw_count, int reference_dpi, Unit unit) noexcept;
    double calibration_dpi(double dx, double dy, int calibration_distance_cm) noexcept;
    SharedDataSnapshot capture_shared_data() noexcept;
    PageLayout calculate_page_layout(float width, float height, float shortcut_badge_width, const std::array<float, 2>& header_cell_text_widths) noexcept;
    std::array<D2D1_RECT_F, 2> calculate_measurement_card_bounds(const PageLayout& layout) noexcept;
    int format_distance_value(double raw_count, int reference_dpi, Unit unit, wchar_t* text, size_t capacity) noexcept;
    int format_reference_dpi_cell(int reference_dpi, wchar_t* text, size_t capacity) noexcept;
    int format_calibration_distance_cell(int calibration_distance_cm, int reference_dpi, Unit unit, wchar_t* text, size_t capacity) noexcept;
    int format_unit_cell(Unit unit, wchar_t* text, size_t capacity) noexcept;

    void draw_text(UiState& state, const wchar_t* text, IDWriteTextFormat* format, const D2D1_RECT_F& bounds, ID2D1Brush* brush, D2D1_DRAW_TEXT_OPTIONS options = D2D1_DRAW_TEXT_OPTIONS_CLIP) noexcept;
    void draw_card(UiState& state, const D2D1_RECT_F& bounds, float radius) noexcept;
    bool begin_page(UiState& state, const SharedDataSnapshot& shared_data, const wchar_t* mode_name, const std::array<const wchar_t*, 2>& header_cells, PageLayout& layout) noexcept;
    HRESULT update_header_cell_layout(UiState& state, HeaderCellLayoutCache& cache, const wchar_t* text, float font_size) noexcept;
    HRESULT fitting_numeric_font_size(UiState& state, const wchar_t* const* texts, size_t text_count, float width, float height, float requested_font_size, float& fitted_font_size) noexcept;
    HRESULT update_numeric_layout(UiState& state, TextLayoutCache& cache, const wchar_t* text, UINT32 suffix_start, float width, float height, float primary_font_size, float suffix_font_size = 0.0f) noexcept;
    HRESULT update_calibration_dpi_layout(UiState& state, const SharedDataSnapshot& shared_data, float width, float height, float primary_font_size, float suffix_font_size) noexcept;

} // namespace ui::detail

namespace ui::modes::measurement {
    void draw(detail::UiState& state, const detail::SharedDataSnapshot& shared_data) noexcept;
}

namespace ui::modes::calibration {
    void draw(detail::UiState& state, const detail::SharedDataSnapshot& shared_data) noexcept;
}

#endif // UI_INTERNAL_HPP_
