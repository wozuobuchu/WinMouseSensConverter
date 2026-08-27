#include "ui.hpp"

#include "Resource.hpp"
#include "sync.hpp"

#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cwchar>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <string_view>
#include <utility>

namespace {

    using Microsoft::WRL::ComPtr;

    constexpr wchar_t kWindowClassName[] = L"WinMouseSensConverterMainWindow";
    constexpr wchar_t kWindowTitle[] = L"WinMouseSensConverter";

    constexpr UINT_PTR kUiTimer = 1;
    constexpr UINT kUiTimerIntervalMs = 8;

    constexpr int kDefaultClientWidthDip = 1280;
    constexpr int kDefaultClientHeightDip = 720;
    constexpr int kMinimumClientWidthDip = 640;
    constexpr int kMinimumClientHeightDip = 360;

    constexpr UINT kCommandDpiFirst = 1000;
    constexpr UINT kCommandDpi100 = 1000;
    constexpr UINT kCommandDpi400 = 1001;
    constexpr UINT kCommandDpi800 = 1002;
    constexpr UINT kCommandDpi1200 = 1003;
    constexpr UINT kCommandDpi1600 = 1004;
    constexpr UINT kCommandDpi3200 = 1005;
    constexpr UINT kCommandDpi10000 = 1006;
    constexpr UINT kCommandDpiCustom = 1007;
    constexpr UINT kCommandDpiLast = 1007;

    constexpr UINT kCommandUnitFirst = 1100;
    constexpr UINT kCommandUnitRaw = 1100;
    constexpr UINT kCommandUnitInch = 1101;
    constexpr UINT kCommandUnitMm = 1102;
    constexpr UINT kCommandUnitCm = 1103;
    constexpr UINT kCommandUnitDm = 1104;
    constexpr UINT kCommandUnitM = 1105;
    constexpr UINT kCommandUnitLast = 1105;

    constexpr UINT kCommandAbout = 1200;
    constexpr UINT kCommandInstruction = 1201;
    constexpr UINT kCommandExit = 1202;

    enum class Unit : uint8_t {
        raw,
        inch,
        mm,
        cm,
        dm,
        m,
    };

    struct DpiMenuEntry {
        UINT command;
        int dpi;
        const wchar_t* label;
    };

    struct UnitMenuEntry {
        UINT command;
        Unit unit;
        const wchar_t* label;
    };

    constexpr std::array<DpiMenuEntry, 7> kDpiMenuEntries{{
        {kCommandDpi100, 100, L"100"},
        {kCommandDpi400, 400, L"400"},
        {kCommandDpi800, 800, L"800"},
        {kCommandDpi1200, 1200, L"1200"},
        {kCommandDpi1600, 1600, L"1600"},
        {kCommandDpi3200, 3200, L"3200"},
        {kCommandDpi10000, 10000, L"10000"},
    }};

    constexpr std::array<UnitMenuEntry, 6> kUnitMenuEntries{{
        {kCommandUnitRaw, Unit::raw, L"raw"},
        {kCommandUnitInch, Unit::inch, L"inch"},
        {kCommandUnitMm, Unit::mm, L"mm"},
        {kCommandUnitCm, Unit::cm, L"cm"},
        {kCommandUnitDm, Unit::dm, L"dm"},
        {kCommandUnitM, Unit::m, L"m"},
    }};

    struct UiState {
        HWND hwnd = nullptr;
        HWND about_dialog = nullptr;
        HWND instruction_dialog = nullptr;
        HWND custom_dpi_dialog = nullptr;
        HMENU root_menu = nullptr;
        bool owned_by_window = false;
        bool in_size_move = false;
        bool minimized = false;
        bool redraw_dirty = true;
        UINT dpi = USER_DEFAULT_SCREEN_DPI;

        int reference_dpi = 800;
        UINT reference_dpi_command = kCommandDpi800;
        Unit unit = Unit::inch;

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

        ComPtr<IDWriteTextLayout> value_layout;
        double cached_raw_dx = std::numeric_limits<double>::quiet_NaN();
        int cached_reference_dpi = 0;
        Unit cached_unit = Unit::raw;
        float cached_value_width = 0.0f;
        float cached_value_height = 0.0f;

        ~UiState() {
            if (root_menu != nullptr) {
                DestroyMenu(root_menu);
                root_menu = nullptr;
            }
        }
    };

    int scale_for_dpi(int value, UINT dpi) noexcept {
        return MulDiv(value, static_cast<int>(dpi), USER_DEFAULT_SCREEN_DPI);
    }

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

    constexpr double convert_distance(double raw_dx, int reference_dpi, Unit unit) noexcept {
        if (unit == Unit::raw) return raw_dx;

        const double inches = raw_dx / static_cast<double>(reference_dpi);
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
                return raw_dx;
        }
        return raw_dx;
    }

    constexpr double normalize_display_value(double value) noexcept {
        return value > -0.0005 && value < 0.0005 ? 0.0 : value;
    }

    constexpr std::optional<int> parse_reference_dpi(std::wstring_view text) noexcept {
        if (text.empty() || text.size() > 6) return std::nullopt;

        int value = 0;
        for (const wchar_t character : text) {
            if (character < L'0' || character > L'9') return std::nullopt;
            value = value * 10 + static_cast<int>(character - L'0');
        }

        if (value < 1 || value > 999999) return std::nullopt;
        return value;
    }

    constexpr bool parses_reference_dpi_as(std::wstring_view text, int expected) noexcept {
        const std::optional<int> parsed = parse_reference_dpi(text);
        return parsed.has_value() && *parsed == expected;
    }

    static_assert(convert_distance(800.0, 800, Unit::raw) == 800.0);
    static_assert(convert_distance(800.0, 800, Unit::inch) == 1.0);
    static_assert(convert_distance(800.0, 800, Unit::mm) == 25.4);
    static_assert(convert_distance(800.0, 800, Unit::cm) == 2.54);
    static_assert(convert_distance(800.0, 800, Unit::dm) == 0.254);
    static_assert(convert_distance(800.0, 800, Unit::m) == 0.0254);
    static_assert(convert_distance(-800.0, 800, Unit::inch) == -1.0);
    static_assert(normalize_display_value(-0.00049) == 0.0);
    static_assert(parses_reference_dpi_as(L"1", 1));
    static_assert(parses_reference_dpi_as(L"999999", 999999));
    static_assert(parses_reference_dpi_as(L"000800", 800));
    static_assert(!parse_reference_dpi(L"").has_value());
    static_assert(!parse_reference_dpi(L"0").has_value());
    static_assert(!parse_reference_dpi(L"1000000").has_value());
    static_assert(!parse_reference_dpi(L"-1").has_value());
    static_assert(!parse_reference_dpi(L" 800").has_value());
    static_assert(!parse_reference_dpi(L"dpi").has_value());
    static_assert(!parse_reference_dpi(L"8x0").has_value());

    HRESULT create_text_format(IDWriteFactory* factory, float font_size, DWRITE_FONT_WEIGHT weight, DWRITE_TEXT_ALIGNMENT alignment, DWRITE_PARAGRAPH_ALIGNMENT paragraph_alignment, IDWriteTextFormat** format) noexcept {
        HRESULT result = factory->CreateTextFormat(
            L"Segoe UI",
            nullptr,
            weight,
            DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL,
            font_size,
            L"en-us",
            format
        );
        if (FAILED(result)) return result;

        result = (*format)->SetTextAlignment(alignment);
        if (FAILED(result)) return result;
        result = (*format)->SetParagraphAlignment(paragraph_alignment);
        if (FAILED(result)) return result;
        return (*format)->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    }

    HRESULT initialize_device_independent_resources(UiState& state) noexcept {
        D2D1_FACTORY_OPTIONS options{};

        HRESULT result = D2D1CreateFactory(
            D2D1_FACTORY_TYPE_SINGLE_THREADED,
            __uuidof(ID2D1Factory),
            &options,
            reinterpret_cast<void**>(state.d2d_factory.GetAddressOf())
        );
        if (FAILED(result)) return result;

        result = DWriteCreateFactory(
            DWRITE_FACTORY_TYPE_SHARED,
            __uuidof(IDWriteFactory),
            reinterpret_cast<IUnknown**>(state.write_factory.GetAddressOf())
        );
        if (FAILED(result)) return result;

        result = create_text_format(state.write_factory.Get(), 26.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, state.title_format.GetAddressOf());
        if (FAILED(result)) return result;
        result = create_text_format(state.write_factory.Get(), 14.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, state.status_format.GetAddressOf());
        if (FAILED(result)) return result;
        result = create_text_format(state.write_factory.Get(), 64.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, state.value_format.GetAddressOf());
        if (FAILED(result)) return result;
        result = create_text_format(state.write_factory.Get(), 13.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, state.label_format.GetAddressOf());
        if (FAILED(result)) return result;
        result = create_text_format(state.write_factory.Get(), 22.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, state.setting_format.GetAddressOf());
        if (FAILED(result)) return result;
        result = create_text_format(state.write_factory.Get(), 17.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, state.shortcut_format.GetAddressOf());
        if (FAILED(result)) return result;
        result = create_text_format(state.write_factory.Get(), 14.0f, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_NEAR, state.body_format.GetAddressOf());
        if (FAILED(result)) return result;

        return state.body_format->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
    }

    void discard_device_resources(UiState& state) noexcept {
        state.status_text_brush.Reset();
        state.status_fill_brush.Reset();
        state.accent_brush.Reset();
        state.secondary_text_brush.Reset();
        state.primary_text_brush.Reset();
        state.shadow_brush.Reset();
        state.border_brush.Reset();
        state.surface_brush.Reset();
        state.render_target.Reset();
    }

    HRESULT create_brush(ID2D1RenderTarget* target, UINT32 rgb, ID2D1SolidColorBrush** brush, float alpha = 1.0f) noexcept {
        return target->CreateSolidColorBrush(D2D1::ColorF(rgb, alpha), brush);
    }

    HRESULT ensure_device_resources(UiState& state) noexcept {
        RECT client{};
        if (!GetClientRect(state.hwnd, &client)) return E_FAIL;
        const UINT width = static_cast<UINT>(std::max<LONG>(0, client.right - client.left));
        const UINT height = static_cast<UINT>(std::max<LONG>(0, client.bottom - client.top));
        if (width == 0 || height == 0) return S_FALSE;

        if (state.render_target != nullptr) {
            state.render_target->SetDpi(static_cast<float>(state.dpi), static_cast<float>(state.dpi));

            const D2D1_SIZE_U pixel_size = state.render_target->GetPixelSize();
            if (pixel_size.width != width || pixel_size.height != height) {
                return state.render_target->Resize(D2D1::SizeU(width, height));
            }
            return S_OK;
        }

        const D2D1_RENDER_TARGET_PROPERTIES target_properties = D2D1::RenderTargetProperties(
            D2D1_RENDER_TARGET_TYPE_DEFAULT,
            D2D1::PixelFormat(DXGI_FORMAT_UNKNOWN, D2D1_ALPHA_MODE_UNKNOWN),
            static_cast<float>(state.dpi),
            static_cast<float>(state.dpi)
        );
        const D2D1_HWND_RENDER_TARGET_PROPERTIES hwnd_properties = D2D1::HwndRenderTargetProperties(
            state.hwnd,
            D2D1::SizeU(width, height),
            D2D1_PRESENT_OPTIONS_RETAIN_CONTENTS
        );

        HRESULT result = state.d2d_factory->CreateHwndRenderTarget(target_properties, hwnd_properties, state.render_target.GetAddressOf());
        if (FAILED(result)) return result;

        state.render_target->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        state.render_target->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_CLEARTYPE);

        result = create_brush(state.render_target.Get(), 0xFFFFFF, state.surface_brush.GetAddressOf());
        if (FAILED(result)) return result;
        result = create_brush(state.render_target.Get(), 0xE1E7EF, state.border_brush.GetAddressOf());
        if (FAILED(result)) return result;
        result = create_brush(state.render_target.Get(), 0xD8E0EA, state.shadow_brush.GetAddressOf(), 0.72f);
        if (FAILED(result)) return result;
        result = create_brush(state.render_target.Get(), 0x172033, state.primary_text_brush.GetAddressOf());
        if (FAILED(result)) return result;
        result = create_brush(state.render_target.Get(), 0x687386, state.secondary_text_brush.GetAddressOf());
        if (FAILED(result)) return result;
        result = create_brush(state.render_target.Get(), 0x2563EB, state.accent_brush.GetAddressOf());
        if (FAILED(result)) return result;
        result = create_brush(state.render_target.Get(), 0xE9EDF2, state.status_fill_brush.GetAddressOf());
        if (FAILED(result)) return result;
        return create_brush(state.render_target.Get(), 0x687386, state.status_text_brush.GetAddressOf());
    }

    HMENU create_main_menu() noexcept {
        HMENU root = CreateMenu();
        HMENU options = CreatePopupMenu();
        HMENU dpi_menu = CreatePopupMenu();
        HMENU unit_menu = CreatePopupMenu();
        HMENU help = CreatePopupMenu();
        if (root == nullptr || options == nullptr || dpi_menu == nullptr || unit_menu == nullptr || help == nullptr) {
            if (root != nullptr) DestroyMenu(root);
            if (options != nullptr) DestroyMenu(options);
            if (dpi_menu != nullptr) DestroyMenu(dpi_menu);
            if (unit_menu != nullptr) DestroyMenu(unit_menu);
            if (help != nullptr) DestroyMenu(help);
            return nullptr;
        }

        for (const DpiMenuEntry& entry : kDpiMenuEntries) {
            AppendMenuW(dpi_menu, MF_STRING, entry.command, entry.label);
        }
        AppendMenuW(dpi_menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(dpi_menu, MF_STRING, kCommandDpiCustom, L"Custom...");
        for (const UnitMenuEntry& entry : kUnitMenuEntries) {
            AppendMenuW(unit_menu, MF_STRING, entry.command, entry.label);
        }

        AppendMenuW(options, MF_POPUP, reinterpret_cast<UINT_PTR>(dpi_menu), L"ReferenceDPI");
        AppendMenuW(options, MF_POPUP, reinterpret_cast<UINT_PTR>(unit_menu), L"Unit");
        AppendMenuW(help, MF_STRING, kCommandAbout, L"About");
        AppendMenuW(help, MF_STRING, kCommandInstruction, L"Instruction");

        AppendMenuW(root, MF_POPUP, reinterpret_cast<UINT_PTR>(options), L"&Options");
        AppendMenuW(root, MF_POPUP, reinterpret_cast<UINT_PTR>(help), L"&Help");
        AppendMenuW(root, MF_STRING, kCommandExit, L"E&xit");

        CheckMenuRadioItem(root, kCommandDpiFirst, kCommandDpiLast, kCommandDpi800, MF_BYCOMMAND);
        CheckMenuRadioItem(root, kCommandUnitFirst, kCommandUnitLast, kCommandUnitInch, MF_BYCOMMAND);
        return root;
    }

    void draw_text(UiState& state, const wchar_t* text, IDWriteTextFormat* format, const D2D1_RECT_F& bounds, ID2D1Brush* brush, D2D1_DRAW_TEXT_OPTIONS options = D2D1_DRAW_TEXT_OPTIONS_CLIP) noexcept {
        state.render_target->DrawTextW(text, static_cast<UINT32>(std::wcslen(text)), format, bounds, brush, options);
    }

    void draw_card(UiState& state, const D2D1_RECT_F& bounds, float radius) noexcept {
        const D2D1_RECT_F shadow_bounds = D2D1::RectF(bounds.left, bounds.top + 2.0f, bounds.right, bounds.bottom + 2.0f);
        state.render_target->FillRoundedRectangle(D2D1::RoundedRect(shadow_bounds, radius, radius), state.shadow_brush.Get());
        const D2D1_ROUNDED_RECT card = D2D1::RoundedRect(bounds, radius, radius);
        state.render_target->FillRoundedRectangle(card, state.surface_brush.Get());
        state.render_target->DrawRoundedRectangle(card, state.border_brush.Get(), 1.0f);
    }

    HRESULT update_value_layout(UiState& state, float width, float height) noexcept {
        const double raw_dx = public_data::accumulated_muzmov_dx;
        if (state.value_layout != nullptr && state.cached_raw_dx == raw_dx && state.cached_reference_dpi == state.reference_dpi && state.cached_unit == state.unit && state.cached_value_width == width && state.cached_value_height == height) {
            return S_OK;
        }

        const double value = normalize_display_value(convert_distance(raw_dx, state.reference_dpi, state.unit));

        wchar_t text[128]{};
        const int written = swprintf_s(text, L"%.3f %ls", value, unit_name(state.unit));
        if (written <= 0) return E_FAIL;

        ComPtr<IDWriteTextLayout> layout;
        HRESULT result = state.write_factory->CreateTextLayout(
            text,
            static_cast<UINT32>(written),
            state.value_format.Get(),
            std::max(1.0f, width),
            std::max(1.0f, height),
            layout.GetAddressOf()
        );
        if (FAILED(result)) return result;

        const wchar_t* separator = std::wcschr(text, L' ');
        if (separator != nullptr) {
            const UINT32 unit_start = static_cast<UINT32>((separator - text) + 1);
            const DWRITE_TEXT_RANGE unit_range{unit_start, static_cast<UINT32>(written) - unit_start};
            layout->SetFontSize(24.0f, unit_range);
            layout->SetFontWeight(DWRITE_FONT_WEIGHT_SEMI_BOLD, unit_range);
        }

        state.value_layout = std::move(layout);
        state.cached_raw_dx = raw_dx;
        state.cached_reference_dpi = state.reference_dpi;
        state.cached_unit = state.unit;
        state.cached_value_width = width;
        state.cached_value_height = height;
        return S_OK;
    }

    void draw_interface(UiState& state) noexcept {
        ID2D1HwndRenderTarget* target = state.render_target.Get();
        const D2D1_SIZE_F size = target->GetSize();
        const float width = size.width;
        const float height = size.height;
        if (width <= 1.0f || height <= 1.0f) return;

        const float outer_margin = std::clamp(width * 0.055f, 20.0f, 56.0f);
        const float content_width = std::min(width - outer_margin * 2.0f, 960.0f);
        const float left = (width - content_width) * 0.5f;
        const float right = left + content_width;
        const float top = std::clamp(height * 0.055f, 18.0f, 42.0f);

        draw_text(state, L"Mouse Sensitivity Meter", state.title_format.Get(), D2D1::RectF(left, top, right, top + 38.0f), state.primary_text_brush.Get());

        const float main_top = top + 54.0f;
        const float instruction_height = std::clamp(height * 0.12f, 64.0f, 86.0f);
        constexpr float card_gap = 16.0f;
        const float maximum_main_height = std::max(120.0f, height - main_top - card_gap - instruction_height - std::max(18.0f, outer_margin));
        const float main_height = std::min(std::clamp(height * 0.5f, 180.0f, 360.0f), maximum_main_height);
        const D2D1_RECT_F main_card = D2D1::RectF(left, main_top, right, main_top + main_height);
        draw_card(state, main_card, 18.0f);

        const float inner_padding = std::clamp(content_width * 0.04f, 22.0f, 36.0f);
        const bool recording = public_data::on_recording_ != 0;
        if (recording) {
            state.status_fill_brush->SetColor(D2D1::ColorF(0xE8F7EF));
            state.status_text_brush->SetColor(D2D1::ColorF(0x168A55));
        } else {
            state.status_fill_brush->SetColor(D2D1::ColorF(0xEEF1F5));
            state.status_text_brush->SetColor(D2D1::ColorF(0x687386));
        }

        const float status_top = main_card.top + std::max(14.0f, main_height * 0.055f);
        const D2D1_RECT_F status_bounds = D2D1::RectF(main_card.left + inner_padding, status_top, main_card.left + inner_padding + 148.0f, status_top + 32.0f);
        state.render_target->FillRoundedRectangle(D2D1::RoundedRect(status_bounds, 16.0f, 16.0f), state.status_fill_brush.Get());
        const D2D1_ELLIPSE status_dot = D2D1::Ellipse(D2D1::Point2F(status_bounds.left + 17.0f, status_bounds.top + 16.0f), 4.0f, 4.0f);
        state.render_target->FillEllipse(status_dot, state.status_text_brush.Get());
        draw_text(state, recording ? L"Recording ON" : L"Recording OFF", state.status_format.Get(), D2D1::RectF(status_bounds.left + 22.0f, status_bounds.top, status_bounds.right - 7.0f, status_bounds.bottom), state.status_text_brush.Get());

        const float settings_height = std::clamp(main_height * 0.30f, 58.0f, 82.0f);
        const float settings_top = main_card.bottom - settings_height;
        state.render_target->DrawLine(D2D1::Point2F(main_card.left + inner_padding, settings_top), D2D1::Point2F(main_card.right - inner_padding, settings_top), state.border_brush.Get(), 1.0f);

        const D2D1_RECT_F value_bounds = D2D1::RectF(main_card.left + inner_padding, status_bounds.bottom + 3.0f, main_card.right - inner_padding, settings_top - 2.0f);
        if (SUCCEEDED(update_value_layout(state, value_bounds.right - value_bounds.left, value_bounds.bottom - value_bounds.top))) {
            state.render_target->DrawTextLayout(D2D1::Point2F(value_bounds.left, value_bounds.top), state.value_layout.Get(), state.primary_text_brush.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
        }

        const float center_x = (main_card.left + main_card.right) * 0.5f;
        state.render_target->DrawLine(D2D1::Point2F(center_x, settings_top + 12.0f), D2D1::Point2F(center_x, main_card.bottom - 12.0f), state.border_brush.Get(), 1.0f);

        const D2D1_RECT_F dpi_label_bounds = D2D1::RectF(main_card.left + inner_padding, settings_top + 7.0f, center_x - 12.0f, settings_top + 27.0f);
        const D2D1_RECT_F unit_label_bounds = D2D1::RectF(center_x + 12.0f, settings_top + 7.0f, main_card.right - inner_padding, settings_top + 27.0f);
        draw_text(state, L"REFERENCE DPI", state.label_format.Get(), dpi_label_bounds, state.secondary_text_brush.Get());
        draw_text(state, L"OUTPUT UNIT", state.label_format.Get(), unit_label_bounds, state.secondary_text_brush.Get());

        wchar_t dpi_text[16]{};
        swprintf_s(dpi_text, L"%d", state.reference_dpi);
        const D2D1_RECT_F dpi_value_bounds = D2D1::RectF(dpi_label_bounds.left, settings_top + 25.0f, dpi_label_bounds.right, main_card.bottom - 5.0f);
        const D2D1_RECT_F unit_value_bounds = D2D1::RectF(unit_label_bounds.left, settings_top + 25.0f, unit_label_bounds.right, main_card.bottom - 5.0f);
        draw_text(state, dpi_text, state.setting_format.Get(), dpi_value_bounds, state.primary_text_brush.Get());
        draw_text(state, unit_name(state.unit), state.setting_format.Get(), unit_value_bounds, state.primary_text_brush.Get());

        const D2D1_RECT_F instruction_card = D2D1::RectF(left, main_card.bottom + card_gap, right, main_card.bottom + card_gap + instruction_height);
        draw_card(state, instruction_card, 16.0f);

        const float shortcut_size = 38.0f;
        const float shortcut_left = instruction_card.left + 18.0f;
        const float shortcut_top = instruction_card.top + (instruction_height - shortcut_size) * 0.5f;
        const D2D1_RECT_F shortcut_badge = D2D1::RectF(shortcut_left, shortcut_top, shortcut_left + 48.0f, shortcut_top + shortcut_size);
        state.render_target->FillRoundedRectangle(D2D1::RoundedRect(shortcut_badge, 10.0f, 10.0f), state.accent_brush.Get());
        draw_text(state, L"F1", state.status_format.Get(), shortcut_badge, state.surface_brush.Get());

        const float instruction_left = shortcut_badge.right + 18.0f;
        draw_text(state, L"Start / stop recording", state.shortcut_format.Get(), D2D1::RectF(instruction_left, instruction_card.top + 8.0f, instruction_card.right - 16.0f, instruction_card.top + 34.0f), state.primary_text_brush.Get());
        draw_text(state, L"Starting a new recording resets the measurement.", state.body_format.Get(), D2D1::RectF(instruction_left, instruction_card.top + 34.0f, instruction_card.right - 16.0f, instruction_card.bottom - 6.0f), state.secondary_text_brush.Get());
    }

    bool paint_window(UiState& state) noexcept {
        const HRESULT resource_result = ensure_device_resources(state);
        if (resource_result == S_FALSE) return false;
        if (FAILED(resource_result)) {
            discard_device_resources(state);
            return false;
        }

        state.render_target->BeginDraw();
        state.render_target->SetTransform(D2D1::Matrix3x2F::Identity());
        state.render_target->Clear(D2D1::ColorF(0xF4F7FB));
        draw_interface(state);

        const HRESULT draw_result = state.render_target->EndDraw();
        if (draw_result == D2DERR_RECREATE_TARGET) {
            discard_device_resources(state);
        }
        return SUCCEEDED(draw_result);
    }

    void update_menu_selection(UiState& state) noexcept {
        UINT unit_command = kCommandUnitInch;
        for (const UnitMenuEntry& entry : kUnitMenuEntries) {
            if (entry.unit == state.unit) {
                unit_command = entry.command;
                break;
            }
        }

        CheckMenuRadioItem(state.root_menu, kCommandDpiFirst, kCommandDpiLast, state.reference_dpi_command, MF_BYCOMMAND);
        CheckMenuRadioItem(state.root_menu, kCommandUnitFirst, kCommandUnitLast, unit_command, MF_BYCOMMAND);
    }

    enum class HelpDialogKind : uint8_t {
        about,
        instruction,
    };

    HWND& dialog_slot(UiState& state, HelpDialogKind kind) noexcept {
        return kind == HelpDialogKind::about ? state.about_dialog : state.instruction_dialog;
    }

    void center_dialog_on_owner(HWND dialog, HWND owner) noexcept {
        RECT dialog_rect{};
        RECT owner_rect{};
        if (!GetWindowRect(dialog, &dialog_rect) || !GetWindowRect(owner, &owner_rect)) return;

        const LONG width = dialog_rect.right - dialog_rect.left;
        const LONG height = dialog_rect.bottom - dialog_rect.top;
        LONG x = owner_rect.left + ((owner_rect.right - owner_rect.left) - width) / 2;
        LONG y = owner_rect.top + ((owner_rect.bottom - owner_rect.top) - height) / 2;

        MONITORINFO monitor_info{};
        monitor_info.cbSize = sizeof(monitor_info);
        const HMONITOR monitor = MonitorFromWindow(owner, MONITOR_DEFAULTTONEAREST);
        if (monitor != nullptr && GetMonitorInfoW(monitor, &monitor_info)) {
            const LONG maximum_x = std::max(monitor_info.rcWork.left, monitor_info.rcWork.right - width);
            const LONG maximum_y = std::max(monitor_info.rcWork.top, monitor_info.rcWork.bottom - height);
            x = std::clamp(x, monitor_info.rcWork.left, maximum_x);
            y = std::clamp(y, monitor_info.rcWork.top, maximum_y);
        }

        SetWindowPos(dialog, nullptr, x, y, 0, 0, SWP_NOACTIVATE | SWP_NOSIZE | SWP_NOZORDER);
    }

    INT_PTR help_dialog_proc(HWND dialog, UINT message, WPARAM wparam, LPARAM lparam, HelpDialogKind kind) noexcept {
        UiState* state = reinterpret_cast<UiState*>(GetWindowLongPtrW(dialog, DWLP_USER));

        switch (message) {
            case WM_INITDIALOG: {
                state = reinterpret_cast<UiState*>(lparam);
                SetWindowLongPtrW(dialog, DWLP_USER, reinterpret_cast<LONG_PTR>(state));

                const HINSTANCE instance = reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(dialog, GWLP_HINSTANCE));
                const HICON large_icon = LoadIconW(instance, MAKEINTRESOURCEW(IDI_WINMOUSESENSCONVERTER));
                const HICON small_icon = LoadIconW(instance, MAKEINTRESOURCEW(IDI_SMALL));
                SendMessageW(dialog, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(large_icon));
                SendMessageW(dialog, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(small_icon));

                if (state != nullptr) center_dialog_on_owner(dialog, state->hwnd);
                return TRUE;
            }

            case WM_COMMAND:
                if (LOWORD(wparam) == IDOK || LOWORD(wparam) == IDCANCEL) {
                    DestroyWindow(dialog);
                    return TRUE;
                }
                break;

            case WM_CLOSE:
                DestroyWindow(dialog);
                return TRUE;

            case WM_NCDESTROY:
                SetWindowLongPtrW(dialog, DWLP_USER, 0);
                if (state != nullptr) {
                    HWND& slot = dialog_slot(*state, kind);
                    if (slot == dialog) slot = nullptr;
                }
                break;

            default:
                break;
        }

        return FALSE;
    }

    INT_PTR CALLBACK about_dialog_proc(HWND dialog, UINT message, WPARAM wparam, LPARAM lparam) noexcept {
        return help_dialog_proc(dialog, message, wparam, lparam, HelpDialogKind::about);
    }

    INT_PTR CALLBACK instruction_dialog_proc(HWND dialog, UINT message, WPARAM wparam, LPARAM lparam) noexcept {
        return help_dialog_proc(dialog, message, wparam, lparam, HelpDialogKind::instruction);
    }

    INT_PTR CALLBACK custom_dpi_dialog_proc(HWND dialog, UINT message, WPARAM wparam, LPARAM lparam) noexcept {
        UiState* state = reinterpret_cast<UiState*>(GetWindowLongPtrW(dialog, DWLP_USER));

        switch (message) {
            case WM_INITDIALOG: {
                state = reinterpret_cast<UiState*>(lparam);
                if (state == nullptr) {
                    DestroyWindow(dialog);
                    return TRUE;
                }
                SetWindowLongPtrW(dialog, DWLP_USER, reinterpret_cast<LONG_PTR>(state));

                const HINSTANCE instance = reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(dialog, GWLP_HINSTANCE));
                const HICON large_icon = LoadIconW(instance, MAKEINTRESOURCEW(IDI_WINMOUSESENSCONVERTER));
                const HICON small_icon = LoadIconW(instance, MAKEINTRESOURCEW(IDI_SMALL));
                SendMessageW(dialog, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(large_icon));
                SendMessageW(dialog, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(small_icon));

                SetDlgItemInt(dialog, IDC_CUSTOM_DPI_VALUE, static_cast<UINT>(state->reference_dpi), FALSE);
                SendDlgItemMessageW(dialog, IDC_CUSTOM_DPI_VALUE, EM_SETLIMITTEXT, 7, 0);
                center_dialog_on_owner(dialog, state->hwnd);

                const HWND edit = GetDlgItem(dialog, IDC_CUSTOM_DPI_VALUE);
                if (edit != nullptr) {
                    SetFocus(edit);
                    SendMessageW(edit, EM_SETSEL, 0, -1);
                    return FALSE;
                }
                return TRUE;
            }

            case WM_COMMAND:
                switch (LOWORD(wparam)) {
                    case IDOK: {
                        if (state == nullptr) return TRUE;

                        wchar_t text[8]{};
                        const UINT length = GetDlgItemTextW(dialog, IDC_CUSTOM_DPI_VALUE, text, static_cast<int>(std::size(text)));
                        const std::optional<int> parsed = parse_reference_dpi(std::wstring_view(text, length));
                        if (!parsed.has_value()) {
                            const HWND edit = GetDlgItem(dialog, IDC_CUSTOM_DPI_VALUE);
                            if (edit != nullptr) {
                                SetFocus(edit);
                                SendMessageW(edit, EM_SETSEL, 0, -1);
                            }
                            return TRUE;
                        }

                        const bool changed = state->reference_dpi != *parsed || state->reference_dpi_command != kCommandDpiCustom;
                        state->reference_dpi = *parsed;
                        state->reference_dpi_command = kCommandDpiCustom;
                        update_menu_selection(*state);
                        if (changed) state->redraw_dirty = true;
                        DestroyWindow(dialog);
                        return TRUE;
                    }

                    case IDCANCEL:
                        DestroyWindow(dialog);
                        return TRUE;

                    default:
                        break;
                }
                break;

            case WM_CLOSE:
                DestroyWindow(dialog);
                return TRUE;

            case WM_NCDESTROY:
                SetWindowLongPtrW(dialog, DWLP_USER, 0);
                if (state != nullptr && state->custom_dpi_dialog == dialog) state->custom_dpi_dialog = nullptr;
                break;

            default:
                break;
        }

        return FALSE;
    }

    void show_modeless_dialog(UiState& state, int resource_id, HWND& slot, DLGPROC procedure) noexcept {
        if (slot != nullptr && IsWindow(slot)) {
            ShowWindow(slot, IsIconic(slot) ? SW_RESTORE : SW_SHOWNORMAL);
            SetForegroundWindow(slot);
            return;
        }

        slot = CreateDialogParamW(
            reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(state.hwnd, GWLP_HINSTANCE)),
            MAKEINTRESOURCEW(resource_id),
            state.hwnd,
            procedure,
            reinterpret_cast<LPARAM>(&state)
        );
        if (slot != nullptr) ShowWindow(slot, SW_SHOWNORMAL);
    }

    void close_modeless_dialog(HWND& dialog) noexcept {
        if (dialog != nullptr && IsWindow(dialog)) DestroyWindow(dialog);
        dialog = nullptr;
    }

    void close_modeless_dialogs(UiState& state) noexcept {
        close_modeless_dialog(state.about_dialog);
        close_modeless_dialog(state.instruction_dialog);
        close_modeless_dialog(state.custom_dpi_dialog);
    }

    bool handle_menu_command(UiState& state, UINT command) noexcept {
        for (const DpiMenuEntry& entry : kDpiMenuEntries) {
            if (entry.command == command) {
                if (state.reference_dpi != entry.dpi || state.reference_dpi_command != entry.command) {
                    state.reference_dpi = entry.dpi;
                    state.reference_dpi_command = entry.command;
                    update_menu_selection(state);
                    state.redraw_dirty = true;
                }
                return true;
            }
        }

        for (const UnitMenuEntry& entry : kUnitMenuEntries) {
            if (entry.command == command) {
                if (state.unit != entry.unit) {
                    state.unit = entry.unit;
                    update_menu_selection(state);
                    state.redraw_dirty = true;
                }
                return true;
            }
        }

        switch (command) {
            case kCommandDpiCustom:
                show_modeless_dialog(state, IDD_CUSTOM_DPI, state.custom_dpi_dialog, custom_dpi_dialog_proc);
                return true;
            case kCommandAbout:
                show_modeless_dialog(state, IDD_ABOUTBOX, state.about_dialog, about_dialog_proc);
                return true;
            case kCommandInstruction:
                show_modeless_dialog(state, IDD_INSTRUCTION, state.instruction_dialog, instruction_dialog_proc);
                return true;
            case kCommandExit:
                SendMessageW(state.hwnd, WM_CLOSE, 0, 0);
                return true;
            default:
                return false;
        }
    }

    void set_minimum_tracking_size(HWND hwnd, MINMAXINFO& info) noexcept {
        UINT dpi = GetDpiForWindow(hwnd);
        if (dpi == 0) dpi = USER_DEFAULT_SCREEN_DPI;

        RECT minimum_rect{0, 0, scale_for_dpi(kMinimumClientWidthDip, dpi), scale_for_dpi(kMinimumClientHeightDip, dpi)};
        const DWORD style = static_cast<DWORD>(GetWindowLongPtrW(hwnd, GWL_STYLE));
        const DWORD extended_style = static_cast<DWORD>(GetWindowLongPtrW(hwnd, GWL_EXSTYLE));
        if (AdjustWindowRectExForDpi(&minimum_rect, style, GetMenu(hwnd) != nullptr, extended_style, dpi)) {
            info.ptMinTrackSize.x = minimum_rect.right - minimum_rect.left;
            info.ptMinTrackSize.y = minimum_rect.bottom - minimum_rect.top;
        }
    }

    LRESULT CALLBACK main_window_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) noexcept {
        UiState* state = reinterpret_cast<UiState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

        if (message == WM_NCCREATE) {
            const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lparam);
            state = static_cast<UiState*>(create->lpCreateParams);
            state->hwnd = hwnd;
            state->dpi = GetDpiForWindow(hwnd);
            if (state->dpi == 0) state->dpi = USER_DEFAULT_SCREEN_DPI;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
        }

        switch (message) {
            case WM_CREATE:
                if (SetTimer(hwnd, kUiTimer, kUiTimerIntervalMs, nullptr) == 0) return -1;
                return 0;

            case WM_COMMAND:
                if (state != nullptr && HIWORD(wparam) == 0 && handle_menu_command(*state, LOWORD(wparam))) return 0;
                break;

            case WM_GETMINMAXINFO:
                set_minimum_tracking_size(hwnd, *reinterpret_cast<MINMAXINFO*>(lparam));
                return 0;

            case WM_ENTERSIZEMOVE:
                if (state != nullptr) state->in_size_move = true;
                return 0;

            case WM_EXITSIZEMOVE:
                if (state != nullptr) {
                    state->in_size_move = false;
                    state->redraw_dirty = true;
                }
                return 0;

            case WM_SIZE:
                if (state != nullptr) {
                    state->minimized = wparam == SIZE_MINIMIZED;
                    state->redraw_dirty = true;
                }
                return 0;

            case WM_DPICHANGED:
                if (state != nullptr) {
                    state->dpi = HIWORD(wparam);
                    const RECT* suggested = reinterpret_cast<const RECT*>(lparam);
                    SetWindowPos(hwnd, nullptr, suggested->left, suggested->top, suggested->right - suggested->left, suggested->bottom - suggested->top, SWP_NOACTIVATE | SWP_NOZORDER);
                    state->redraw_dirty = true;
                }
                return 0;

            case WM_DISPLAYCHANGE:
                if (state != nullptr) state->redraw_dirty = true;
                return 0;

            case WM_TIMER:
                if (wparam == kUiTimer) return 0;
                break;

            case WM_ERASEBKGND:
                return 1;

            case WM_PAINT: {
                PAINTSTRUCT paint{};
                BeginPaint(hwnd, &paint);
                EndPaint(hwnd, &paint);
                if (state != nullptr) state->redraw_dirty = true;
                return 0;
            }

            case WM_CLOSE:
                if (state != nullptr) close_modeless_dialogs(*state);
                DestroyWindow(hwnd);
                return 0;

            case WM_DESTROY:
                if (state != nullptr) close_modeless_dialogs(*state);
                KillTimer(hwnd, kUiTimer);
                sync::sts_.request_stop();
                PostQuitMessage(0);
                return 0;

            case WM_NCDESTROY: {
                SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
                const bool delete_state = state != nullptr && state->owned_by_window;
                const LRESULT result = DefWindowProcW(hwnd, message, wparam, lparam);
                if (delete_state) delete state;
                return result;
            }

            default:
                break;
        }

        return DefWindowProcW(hwnd, message, wparam, lparam);
    }

    bool register_window_class(HINSTANCE instance) noexcept {
        WNDCLASSEXW window_class{};
        window_class.cbSize = sizeof(window_class);
        window_class.lpfnWndProc = main_window_proc;
        window_class.hInstance = instance;
        window_class.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(IDI_WINMOUSESENSCONVERTER));
        window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        window_class.hbrBackground = nullptr;
        window_class.lpszClassName = kWindowClassName;
        window_class.hIconSm = LoadIconW(instance, MAKEINTRESOURCEW(IDI_SMALL));

        if (RegisterClassExW(&window_class) != 0) return true;
        return GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
    }

} // namespace

namespace ui {

    bool enable_process_dpi_awareness() noexcept {
        if (SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) return true;
        if (GetLastError() == ERROR_ACCESS_DENIED) return true;
        return SetProcessDPIAware() != FALSE;
    }

    HWND create_main_window(HINSTANCE instance) noexcept {
        try {
            if (!register_window_class(instance)) return nullptr;

            auto state = std::make_unique<UiState>();
            if (FAILED(initialize_device_independent_resources(*state))) {
                MessageBoxW(nullptr, L"Direct2D or DirectWrite could not be initialized.", kWindowTitle, MB_OK | MB_ICONERROR);
                return nullptr;
            }

            state->root_menu = create_main_menu();
            if (state->root_menu == nullptr) return nullptr;

            UINT dpi = GetDpiForSystem();
            if (dpi == 0) dpi = USER_DEFAULT_SCREEN_DPI;
            RECT window_rect{0, 0, scale_for_dpi(kDefaultClientWidthDip, dpi), scale_for_dpi(kDefaultClientHeightDip, dpi)};
            constexpr DWORD style = WS_OVERLAPPEDWINDOW;
            constexpr DWORD extended_style = 0;
            if (!AdjustWindowRectExForDpi(&window_rect, style, TRUE, extended_style, dpi)) return nullptr;

            HWND hwnd = CreateWindowExW(
                extended_style,
                kWindowClassName,
                kWindowTitle,
                style,
                CW_USEDEFAULT,
                CW_USEDEFAULT,
                window_rect.right - window_rect.left,
                window_rect.bottom - window_rect.top,
                nullptr,
                state->root_menu,
                instance,
                state.get()
            );
            if (hwnd == nullptr) return nullptr;

            state->owned_by_window = true;
            state.release();
            return hwnd;
        } catch (...) {
            return nullptr;
        }
    }

    bool preprocess_modeless_dialog_message(HWND main_window, MSG& message) noexcept {
        if (main_window == nullptr || !IsWindow(main_window)) return false;

        UiState* state = reinterpret_cast<UiState*>(GetWindowLongPtrW(main_window, GWLP_USERDATA));
        if (state == nullptr) return false;

        const std::array<HWND, 3> dialogs{state->about_dialog, state->instruction_dialog, state->custom_dpi_dialog};
        for (HWND dialog : dialogs) {
            if (dialog != nullptr && IsWindow(dialog) && IsDialogMessageW(dialog, &message)) return true;
        }
        return false;
    }

    void finish_main_loop_iteration(HWND main_window, const MSG& message, bool content_changed) noexcept {
        if (main_window == nullptr || !IsWindow(main_window)) return;

        UiState* state = reinterpret_cast<UiState*>(GetWindowLongPtrW(main_window, GWLP_USERDATA));
        if (state == nullptr) return;

        if (content_changed) state->redraw_dirty = true;

        const bool redraw_tick = message.hwnd == main_window && message.message == WM_TIMER && message.wParam == kUiTimer;
        if (!redraw_tick || !state->redraw_dirty || state->in_size_move || state->minimized) return;

        if (paint_window(*state)) state->redraw_dirty = false;
    }

} // namespace ui
