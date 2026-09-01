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

namespace ui::detail {

    using Microsoft::WRL::ComPtr;
    using Unit = config::OutputUnit;

    struct ValueLayoutCache {
        ComPtr<IDWriteTextLayout> layout;
        std::array<wchar_t, 128> display_text{};
        UINT32 display_text_length = 0;
        float width = 0.0f;
        float height = 0.0f;
    };

    struct DpiResultLayoutCache {
        ComPtr<IDWriteTextLayout> layout;
        std::array<wchar_t, 64> display_text{};
        UINT32 display_text_length = 0;
        float width = 0.0f;
        float height = 0.0f;
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

        ComPtr<IDWriteTextFormat> title_format;
        ComPtr<IDWriteTextFormat> status_format;
        ComPtr<IDWriteTextFormat> value_format;
        ComPtr<IDWriteTextFormat> label_format;
        ComPtr<IDWriteTextFormat> setting_format;
        ComPtr<IDWriteTextFormat> shortcut_format;
        ComPtr<IDWriteTextFormat> body_format;

        ComPtr<ID2D1HwndRenderTarget> render_target;
        ComPtr<ID2D1SolidColorBrush> surface_brush;
        ComPtr<ID2D1SolidColorBrush> border_brush;
        ComPtr<ID2D1SolidColorBrush> shadow_brush;
        ComPtr<ID2D1SolidColorBrush> primary_text_brush;
        ComPtr<ID2D1SolidColorBrush> secondary_text_brush;
        ComPtr<ID2D1SolidColorBrush> accent_brush;
        ComPtr<ID2D1SolidColorBrush> status_fill_brush;
        ComPtr<ID2D1SolidColorBrush> status_text_brush;

        std::array<ValueLayoutCache, 2> measurement_value_layouts;
        std::array<ValueLayoutCache, 2> calibration_value_layouts;
        DpiResultLayoutCache calibration_dpi_layout;

        ~UiState() {
            if (root_menu != nullptr) {
                DestroyMenu(root_menu);
                root_menu = nullptr;
            }
        }
    };

    struct PageLayout {
        float content_width = 0.0f;
        float inner_padding = 0.0f;
        float main_height = 0.0f;
        D2D1_RECT_F main_card{};
        D2D1_RECT_F instruction_card{};
        D2D1_RECT_F status_bounds{};
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

    void draw_text(UiState& state, const wchar_t* text, IDWriteTextFormat* format, const D2D1_RECT_F& bounds, ID2D1Brush* brush, D2D1_DRAW_TEXT_OPTIONS options = D2D1_DRAW_TEXT_OPTIONS_CLIP) noexcept;
    void draw_card(UiState& state, const D2D1_RECT_F& bounds, float radius) noexcept;
    bool begin_page(UiState& state, const SharedDataSnapshot& shared_data, const wchar_t* title, PageLayout& layout) noexcept;
    HRESULT update_value_layout(UiState& state, ValueLayoutCache& cache, double raw_count, float width, float height) noexcept;

} // namespace ui::detail

namespace ui::modes::measurement {
    void draw(detail::UiState& state, const detail::SharedDataSnapshot& shared_data) noexcept;
}

namespace ui::modes::calibration {
    void draw(detail::UiState& state, const detail::SharedDataSnapshot& shared_data) noexcept;
}

#endif // UI_INTERNAL_HPP_
