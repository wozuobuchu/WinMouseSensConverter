#include "ui.hpp"
#include "ui_internal.hpp"

#include "Resource.hpp"

#include "SYS/low_latency_keyboard.hpp"
#include "SYS/low_latency_mousemov.hpp"

#include "recording_key.hpp"

#include "sync.hpp"

#include <CommCtrl.h>
#include <d2d1.h>
#include <dwrite.h>
#include <shellapi.h>
#include <uxtheme.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cwchar>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace {

    using Microsoft::WRL::ComPtr;
    using ui::detail::UiState;

    constexpr wchar_t kWindowClassName[] = L"WinMouseSensConverterMainWindow";
    constexpr wchar_t kWindowTitle[] = L"WinMouseSensConverter";

    constexpr UINT_PTR kUiTimer = 1;
    constexpr UINT kUiTimerIntervalMs = 8;

    constexpr int kDefaultClientWidthDip = 1280;
    constexpr int kDefaultClientHeightDip = 720;
    constexpr int kMinimumClientWidthDip = 640;
    constexpr int kMinimumClientHeightDip = 360;

    constexpr UINT kCommandModeFirst = 900;
    constexpr UINT kCommandModeMeasurement = 900;
    constexpr UINT kCommandModeCalibration = 901;
    constexpr UINT kCommandModeLast = 901;

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

    constexpr UINT kCommandCalibrationDistanceFirst = 1120;
    constexpr UINT kCommandCalibrationDistance10 = 1120;
    constexpr UINT kCommandCalibrationDistance20 = 1121;
    constexpr UINT kCommandCalibrationDistance50 = 1122;
    constexpr UINT kCommandCalibrationDistanceCustom = 1123;
    constexpr UINT kCommandCalibrationDistanceLast = 1123;

    constexpr UINT kCommandRecordingKeyFirst = 1130;
    constexpr UINT kCommandRecordingKeyR = 1130;
    constexpr UINT kCommandRecordingKeyT = 1131;
    constexpr UINT kCommandRecordingKeyF2 = 1132;
    constexpr UINT kCommandRecordingKeyF5 = 1133;
    constexpr UINT kCommandRecordingKeyComma = 1134;
    constexpr UINT kCommandRecordingKeyPeriod = 1135;
    constexpr UINT kCommandRecordingKeyCustom = 1136;
    constexpr UINT kCommandRecordingKeyLast = 1136;

    constexpr UINT kCommandEditConfiguration = 1150;
    constexpr UINT kCommandAbout = 1200;
    constexpr UINT kCommandInstruction = 1201;
    constexpr UINT kCommandExit = 1202;

    using Unit = config::OutputUnit;

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

    struct CalibrationDistanceMenuEntry {
        UINT command;
        int distance_cm;
        const wchar_t* label;
    };

    struct RecordingKeyMenuEntry {
        UINT command;
        uint16_t virtual_key;
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

    constexpr std::array<CalibrationDistanceMenuEntry, 3> kCalibrationDistanceMenuEntries{{
        {kCommandCalibrationDistance10, 10, L"10 cm"},
        {kCommandCalibrationDistance20, 20, L"20 cm"},
        {kCommandCalibrationDistance50, 50, L"50 cm"},
    }};

    constexpr std::array<RecordingKeyMenuEntry, 6> kRecordingKeyMenuEntries{{
        {kCommandRecordingKeyR, 'R', L"R"},
        {kCommandRecordingKeyT, 'T', L"T"},
        {kCommandRecordingKeyF2, VK_F2, L"F2"},
        {kCommandRecordingKeyF5, VK_F5, L"F5"},
        {kCommandRecordingKeyComma, VK_OEM_COMMA, L"COMMA"},
        {kCommandRecordingKeyPeriod, VK_OEM_PERIOD, L"PERIOD"},
    }};

    bool pull_msg_kbd(uint16_t recording_key) noexcept {
        static constexpr size_t kQueueSize = 1024;
        static rawinput::LowLatencyKeyboard::KeyEvent queue[kQueueSize];
        const size_t count = rawinput::LowLatencyKeyboard::pop_events<kQueueSize>(queue);
        bool changed = false;

        for (size_t index = 0; index < count; ++index) {
            const rawinput::LowLatencyKeyboard::KeyEvent& event = queue[index];
            if (event.down == 0 || !main_loop::matches_recording_key(recording_key, event.vkey)) continue;

            public_data::on_recording_ = public_data::on_recording_ == 0 ? static_cast<uint8_t>(~0) : 0;
            if (public_data::on_recording_ != 0) {
                public_data::accumulated_muzmov_dx = 0.0;
                public_data::accumulated_muzmov_dy = 0.0;
            }

            const UINT notification_sound = public_data::on_recording_ != 0 ? MB_ICONASTERISK : MB_ICONHAND;
            (void)MessageBeep(notification_sound);
            changed = true;
        }

        return changed;
    }

    bool pull_msg_mouse() noexcept {
        const auto [dx, dy] = rawinput::LowLatencyMouseMov::sample();
        if (public_data::on_recording_ != 0) {
            public_data::accumulated_muzmov_dx += static_cast<double>(dx);
            public_data::accumulated_muzmov_dy += static_cast<double>(dy);
            return dx != 0 || dy != 0;
        }
        return false;
    }

    bool pull_pending_input(UiState& state) noexcept {
        if (state.user_config == nullptr) return false;

        // Apply recording-key state transitions before attributing the pending mouse snapshot.
        const bool keyboard_changed = pull_msg_kbd(state.user_config->recording_key);
        const bool mouse_changed = pull_msg_mouse();
        return keyboard_changed || (public_data::on_recording_ != 0 && mouse_changed);
    }

    int scale_for_dpi(int value, UINT dpi) noexcept {
        return MulDiv(value, static_cast<int>(dpi), USER_DEFAULT_SCREEN_DPI);
    }

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

        result = create_text_format(state.write_factory.Get(), 56.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, state.value_format.GetAddressOf());
        if (FAILED(result)) return result;
        result = create_text_format(state.write_factory.Get(), 14.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, state.label_format.GetAddressOf());
        if (FAILED(result)) return result;
        result = create_text_format(state.write_factory.Get(), 14.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, state.header_cell_format.GetAddressOf());
        if (FAILED(result)) return result;
        result = create_text_format(state.write_factory.Get(), 14.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, state.mode_format.GetAddressOf());
        if (FAILED(result)) return result;
        result = create_text_format(state.write_factory.Get(), 14.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, state.footer_format.GetAddressOf());
        if (FAILED(result)) return result;
        return create_text_format(state.write_factory.Get(), 14.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, state.badge_format.GetAddressOf());
    }

    constexpr bool uses_extended_key_name(uint16_t virtual_key) noexcept {
        switch (virtual_key) {
            case VK_CANCEL:
            case VK_RCONTROL:
            case VK_RMENU:
            case VK_PRIOR:
            case VK_NEXT:
            case VK_END:
            case VK_HOME:
            case VK_LEFT:
            case VK_UP:
            case VK_RIGHT:
            case VK_DOWN:
            case VK_SNAPSHOT:
            case VK_INSERT:
            case VK_DELETE:
            case VK_LWIN:
            case VK_RWIN:
            case VK_APPS:
            case VK_DIVIDE:
            case VK_NUMLOCK:
            case VK_SLEEP:
                return true;
            default:
                return false;
        }
    }

    void initialize_recording_key_display(UiState& state, uint16_t recording_key) noexcept {
        state.shortcut_badge_width = 48.0f;
        const UINT scan_code = MapVirtualKeyW(recording_key, MAPVK_VK_TO_VSC_EX);
        LONG key_name_parameter = static_cast<LONG>((scan_code & 0xFFu) << 16);
        if ((scan_code & 0xFF00u) != 0 || uses_extended_key_name(recording_key)) key_name_parameter |= 1L << 24;

        const int length = scan_code == 0 ? 0 : GetKeyNameTextW(key_name_parameter, state.recording_key_name.data(), static_cast<int>(state.recording_key_name.size()));
        if (length > 0) {
            state.recording_key_name_length = static_cast<UINT32>(length);
        } else {
            const int written = swprintf_s(state.recording_key_name.data(), state.recording_key_name.size(), L"VK 0x%02X", static_cast<unsigned int>(recording_key));
            state.recording_key_name_length = written > 0 ? static_cast<UINT32>(written) : 0;
        }

        ComPtr<IDWriteTextLayout> layout;
        const HRESULT layout_result = state.write_factory->CreateTextLayout(
            state.recording_key_name.data(),
            state.recording_key_name_length,
            state.badge_format.Get(),
            512.0f,
            34.0f,
            layout.GetAddressOf()
        );
        if (FAILED(layout_result)) return;

        DWRITE_TEXT_METRICS metrics{};
        if (SUCCEEDED(layout->GetMetrics(&metrics))) {
            state.shortcut_badge_width = std::max(48.0f, metrics.widthIncludingTrailingWhitespace + 24.0f);
        }
    }

    void discard_device_resources(UiState& state) noexcept {
        state.switch_track_brush.Reset();
        state.mode_fill_brush.Reset();
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
            D2D1_PRESENT_OPTIONS_NONE
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
        result = create_brush(state.render_target.Get(), 0xE8F0FE, state.mode_fill_brush.GetAddressOf());
        if (FAILED(result)) return result;
        return create_brush(state.render_target.Get(), 0xC7CBD1, state.switch_track_brush.GetAddressOf());
    }

    HMENU create_main_menu() noexcept {
        HMENU root = CreateMenu();
        HMENU mode_menu = CreatePopupMenu();
        HMENU options = CreatePopupMenu();
        HMENU dpi_menu = CreatePopupMenu();
        HMENU unit_menu = CreatePopupMenu();
        HMENU calibration_distance_menu = CreatePopupMenu();
        HMENU recording_key_menu = CreatePopupMenu();
        HMENU help = CreatePopupMenu();
        if (root == nullptr || mode_menu == nullptr || options == nullptr || dpi_menu == nullptr || unit_menu == nullptr || calibration_distance_menu == nullptr || recording_key_menu == nullptr || help == nullptr) {
            if (root != nullptr) DestroyMenu(root);
            if (mode_menu != nullptr) DestroyMenu(mode_menu);
            if (options != nullptr) DestroyMenu(options);
            if (dpi_menu != nullptr) DestroyMenu(dpi_menu);
            if (unit_menu != nullptr) DestroyMenu(unit_menu);
            if (calibration_distance_menu != nullptr) DestroyMenu(calibration_distance_menu);
            if (recording_key_menu != nullptr) DestroyMenu(recording_key_menu);
            if (help != nullptr) DestroyMenu(help);
            return nullptr;
        }

        AppendMenuW(mode_menu, MF_STRING, kCommandModeMeasurement, L"Measurement");
        AppendMenuW(mode_menu, MF_STRING, kCommandModeCalibration, L"Calibration");
        for (const DpiMenuEntry& entry : kDpiMenuEntries) {
            AppendMenuW(dpi_menu, MF_STRING, entry.command, entry.label);
        }
        AppendMenuW(dpi_menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(dpi_menu, MF_STRING, kCommandDpiCustom, L"Custom...");
        for (const UnitMenuEntry& entry : kUnitMenuEntries) {
            AppendMenuW(unit_menu, MF_STRING, entry.command, entry.label);
        }
        for (const CalibrationDistanceMenuEntry& entry : kCalibrationDistanceMenuEntries) {
            AppendMenuW(calibration_distance_menu, MF_STRING, entry.command, entry.label);
        }
        AppendMenuW(calibration_distance_menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(calibration_distance_menu, MF_STRING, kCommandCalibrationDistanceCustom, L"Custom...");
        for (const RecordingKeyMenuEntry& entry : kRecordingKeyMenuEntries) {
            AppendMenuW(recording_key_menu, MF_STRING, entry.command, entry.label);
        }
        AppendMenuW(recording_key_menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(recording_key_menu, MF_STRING, kCommandRecordingKeyCustom, L"Custom...");

        AppendMenuW(options, MF_POPUP, reinterpret_cast<UINT_PTR>(dpi_menu), L"ReferenceDPI");
        AppendMenuW(options, MF_POPUP, reinterpret_cast<UINT_PTR>(unit_menu), L"Unit");
        AppendMenuW(options, MF_POPUP, reinterpret_cast<UINT_PTR>(calibration_distance_menu), L"Calibration Distance");
        AppendMenuW(options, MF_POPUP, reinterpret_cast<UINT_PTR>(recording_key_menu), L"Recording Key");
        AppendMenuW(options, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(options, MF_STRING, kCommandEditConfiguration, L"Edit Configuration File...");
        AppendMenuW(help, MF_STRING, kCommandAbout, L"About");
        AppendMenuW(help, MF_STRING, kCommandInstruction, L"Instruction");

        AppendMenuW(root, MF_POPUP, reinterpret_cast<UINT_PTR>(mode_menu), L"&Mode");
        AppendMenuW(root, MF_POPUP, reinterpret_cast<UINT_PTR>(options), L"&Options");
        AppendMenuW(root, MF_POPUP, reinterpret_cast<UINT_PTR>(help), L"&Help");
        AppendMenuW(root, MF_STRING, kCommandExit, L"E&xit");

        CheckMenuRadioItem(root, kCommandModeFirst, kCommandModeLast, kCommandModeMeasurement, MF_BYCOMMAND);
        CheckMenuRadioItem(root, kCommandDpiFirst, kCommandDpiLast, kCommandDpi800, MF_BYCOMMAND);
        CheckMenuRadioItem(root, kCommandUnitFirst, kCommandUnitLast, kCommandUnitCm, MF_BYCOMMAND);
        CheckMenuRadioItem(root, kCommandCalibrationDistanceFirst, kCommandCalibrationDistanceLast, kCommandCalibrationDistance10, MF_BYCOMMAND);
        CheckMenuRadioItem(root, kCommandRecordingKeyFirst, kCommandRecordingKeyLast, kCommandRecordingKeyF2, MF_BYCOMMAND);
        return root;
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
        const ui::detail::SharedDataSnapshot shared_data = ui::detail::capture_shared_data();
        if (shared_data.mode == config::AppMode::calibration) {
            ui::modes::calibration::draw(state, shared_data);
        } else {
            ui::modes::measurement::draw(state, shared_data);
        }

        const HRESULT draw_result = state.render_target->EndDraw();
        if (draw_result == D2DERR_RECREATE_TARGET) {
            discard_device_resources(state);
        }
        return SUCCEEDED(draw_result);
    }

    void update_menu_selection(UiState& state) noexcept {
        if (state.user_config == nullptr) return;
        const HMENU root_menu = GetMenu(state.hwnd);
        if (root_menu == nullptr) return;

        UINT unit_command = kCommandUnitCm;
        for (const UnitMenuEntry& entry : kUnitMenuEntries) {
            if (entry.unit == state.user_config->unit) {
                unit_command = entry.command;
                break;
            }
        }

        const UINT mode_command = public_data::current_mode_ == config::AppMode::calibration ? kCommandModeCalibration : kCommandModeMeasurement;
        CheckMenuRadioItem(root_menu, kCommandModeFirst, kCommandModeLast, mode_command, MF_BYCOMMAND);
        CheckMenuRadioItem(root_menu, kCommandDpiFirst, kCommandDpiLast, state.reference_dpi_command, MF_BYCOMMAND);
        CheckMenuRadioItem(root_menu, kCommandUnitFirst, kCommandUnitLast, unit_command, MF_BYCOMMAND);
        CheckMenuRadioItem(root_menu, kCommandCalibrationDistanceFirst, kCommandCalibrationDistanceLast, state.calibration_distance_command, MF_BYCOMMAND);
        CheckMenuRadioItem(root_menu, kCommandRecordingKeyFirst, kCommandRecordingKeyLast, state.recording_key_command, MF_BYCOMMAND);
    }

    UINT dpi_command_for_value(int reference_dpi) noexcept {
        for (const DpiMenuEntry& entry : kDpiMenuEntries) {
            if (entry.dpi == reference_dpi) return entry.command;
        }
        return kCommandDpiCustom;
    }

    UINT calibration_distance_command_for_value(int distance_cm) noexcept {
        for (const CalibrationDistanceMenuEntry& entry : kCalibrationDistanceMenuEntries) {
            if (entry.distance_cm == distance_cm) return entry.command;
        }
        return kCommandCalibrationDistanceCustom;
    }

    UINT recording_key_command_for_value(uint16_t virtual_key) noexcept {
        for (const RecordingKeyMenuEntry& entry : kRecordingKeyMenuEntries) {
            if (entry.virtual_key == virtual_key) return entry.command;
        }
        return kCommandRecordingKeyCustom;
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

    BOOL CALLBACK apply_native_child_theme(HWND child, LPARAM) noexcept {
        wchar_t class_name[16]{};
        if (GetClassNameW(child, class_name, static_cast<int>(std::size(class_name))) == 0) return TRUE;

        if (lstrcmpiW(class_name, L"Button") == 0 || lstrcmpiW(class_name, L"Edit") == 0) {
            (void)SetWindowTheme(child, L"Explorer", nullptr);
        }
        return TRUE;
    }

    void apply_native_dialog_theme(HWND dialog) noexcept {
        (void)SetWindowTheme(dialog, L"Explorer", nullptr);
        (void)EnumChildWindows(dialog, apply_native_child_theme, 0);
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

                apply_native_dialog_theme(dialog);
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

    enum class CustomInputKind : uint8_t {
        reference_dpi,
        calibration_distance,
        recording_key,
    };

    struct CustomInputSpec {
        int edit_control_id;
        WPARAM text_limit;
        HWND UiState::*dialog_slot;
    };

    constexpr CustomInputSpec custom_input_spec(CustomInputKind kind) noexcept {
        switch (kind) {
            case CustomInputKind::reference_dpi:
                return {IDC_CUSTOM_DPI_VALUE, 7, &UiState::custom_dpi_dialog};
            case CustomInputKind::calibration_distance:
                return {IDC_CUSTOM_CALIBRATION_DISTANCE_VALUE, 4, &UiState::custom_calibration_distance_dialog};
            case CustomInputKind::recording_key:
                return {IDC_CUSTOM_RECORDING_KEY_VALUE, 4, &UiState::custom_recording_key_dialog};
        }
        return {IDC_CUSTOM_DPI_VALUE, 7, &UiState::custom_dpi_dialog};
    }

    UINT custom_input_initial_value(const UiState& state, CustomInputKind kind) noexcept {
        if (state.user_config == nullptr) return 0;
        switch (kind) {
            case CustomInputKind::reference_dpi:
                return static_cast<UINT>(state.user_config->reference_dpi);
            case CustomInputKind::calibration_distance:
                return static_cast<UINT>(state.user_config->calibration_distance_cm);
            case CustomInputKind::recording_key:
                return static_cast<UINT>(state.user_config->recording_key);
        }
        return 0;
    }

    bool submit_custom_input(UiState& state, CustomInputKind kind, std::wstring_view text) noexcept {
        if (state.user_config == nullptr) return false;

        bool changed = false;
        switch (kind) {
            case CustomInputKind::reference_dpi: {
                const std::optional<int> parsed = config::detail::parse_reference_dpi(text);
                if (!parsed.has_value()) return false;
                changed = state.user_config->reference_dpi != *parsed || state.reference_dpi_command != kCommandDpiCustom;
                state.user_config->reference_dpi = *parsed;
                state.reference_dpi_command = kCommandDpiCustom;
                break;
            }
            case CustomInputKind::calibration_distance: {
                const std::optional<int> parsed = config::detail::parse_calibration_distance_cm(text);
                if (!parsed.has_value()) return false;
                changed = state.user_config->calibration_distance_cm != *parsed || state.calibration_distance_command != kCommandCalibrationDistanceCustom;
                state.user_config->calibration_distance_cm = *parsed;
                state.calibration_distance_command = kCommandCalibrationDistanceCustom;
                break;
            }
            case CustomInputKind::recording_key: {
                const std::optional<uint16_t> parsed = config::detail::parse_recording_key(text);
                if (!parsed.has_value()) return false;
                changed = state.user_config->recording_key != *parsed || state.recording_key_command != kCommandRecordingKeyCustom;
                state.user_config->recording_key = *parsed;
                state.recording_key_command = kCommandRecordingKeyCustom;
                initialize_recording_key_display(state, *parsed);
                break;
            }
        }

        update_menu_selection(state);
        if (changed) state.redraw_dirty = true;
        return true;
    }

    void focus_and_select_dialog_edit(HWND dialog, int edit_control_id) noexcept {
        const HWND edit = GetDlgItem(dialog, edit_control_id);
        if (edit == nullptr) return;
        SetFocus(edit);
        SendMessageW(edit, EM_SETSEL, 0, -1);
    }

    INT_PTR custom_input_dialog_proc(HWND dialog, UINT message, WPARAM wparam, LPARAM lparam, CustomInputKind kind) noexcept {
        UiState* state = reinterpret_cast<UiState*>(GetWindowLongPtrW(dialog, DWLP_USER));
        const CustomInputSpec spec = custom_input_spec(kind);

        switch (message) {
            case WM_INITDIALOG: {
                state = reinterpret_cast<UiState*>(lparam);
                if (state == nullptr || state->user_config == nullptr) {
                    DestroyWindow(dialog);
                    return TRUE;
                }
                SetWindowLongPtrW(dialog, DWLP_USER, reinterpret_cast<LONG_PTR>(state));

                const HINSTANCE instance = reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(dialog, GWLP_HINSTANCE));
                SendMessageW(dialog, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(LoadIconW(instance, MAKEINTRESOURCEW(IDI_WINMOUSESENSCONVERTER))));
                SendMessageW(dialog, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(LoadIconW(instance, MAKEINTRESOURCEW(IDI_SMALL))));
                apply_native_dialog_theme(dialog);
                SetDlgItemInt(dialog, spec.edit_control_id, custom_input_initial_value(*state, kind), FALSE);
                SendDlgItemMessageW(dialog, spec.edit_control_id, EM_SETLIMITTEXT, spec.text_limit, 0);
                center_dialog_on_owner(dialog, state->hwnd);

                focus_and_select_dialog_edit(dialog, spec.edit_control_id);
                return GetDlgItem(dialog, spec.edit_control_id) == nullptr ? TRUE : FALSE;
            }

            case WM_COMMAND:
                if (LOWORD(wparam) == IDOK) {
                    if (state == nullptr) return TRUE;
                    wchar_t text[8]{};
                    const UINT length = GetDlgItemTextW(dialog, spec.edit_control_id, text, static_cast<int>(std::size(text)));
                    if (!submit_custom_input(*state, kind, std::wstring_view(text, length))) {
                        focus_and_select_dialog_edit(dialog, spec.edit_control_id);
                        return TRUE;
                    }
                    DestroyWindow(dialog);
                    return TRUE;
                }
                if (LOWORD(wparam) == IDCANCEL) {
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
                    HWND& slot = state->*(spec.dialog_slot);
                    if (slot == dialog) slot = nullptr;
                }
                break;

            default:
                break;
        }

        return FALSE;
    }

    INT_PTR CALLBACK custom_dpi_dialog_proc(HWND dialog, UINT message, WPARAM wparam, LPARAM lparam) noexcept {
        return custom_input_dialog_proc(dialog, message, wparam, lparam, CustomInputKind::reference_dpi);
    }

    INT_PTR CALLBACK custom_calibration_distance_dialog_proc(HWND dialog, UINT message, WPARAM wparam, LPARAM lparam) noexcept {
        return custom_input_dialog_proc(dialog, message, wparam, lparam, CustomInputKind::calibration_distance);
    }

    INT_PTR CALLBACK custom_recording_key_dialog_proc(HWND dialog, UINT message, WPARAM wparam, LPARAM lparam) noexcept {
        return custom_input_dialog_proc(dialog, message, wparam, lparam, CustomInputKind::recording_key);
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
        close_modeless_dialog(state.custom_calibration_distance_dialog);
        close_modeless_dialog(state.custom_recording_key_dialog);
    }

    void open_configuration_directory(HWND owner) noexcept {
        try {
            const std::optional<std::filesystem::path> directory = config::configuration_directory();
            if (!directory.has_value()) return;

            const std::wstring parameters = L"\"" + directory->wstring() + L"\"";
            SHELLEXECUTEINFOW execute_info{};
            execute_info.cbSize = sizeof(execute_info);
            execute_info.fMask = SEE_MASK_ASYNCOK | SEE_MASK_FLAG_NO_UI;
            execute_info.hwnd = owner;
            execute_info.lpVerb = L"open";
            execute_info.lpFile = L"explorer.exe";
            execute_info.lpParameters = parameters.c_str();
            execute_info.nShow = SW_SHOWNORMAL;
            (void)ShellExecuteExW(&execute_info);
        } catch (...) {
        }
    }

    bool handle_menu_command(UiState& state, UINT command) noexcept {
        if (command == kCommandModeMeasurement || command == kCommandModeCalibration) {
            const config::AppMode mode = command == kCommandModeCalibration ? config::AppMode::calibration : config::AppMode::measurement;
            if (public_data::current_mode_ != mode) {
                public_data::current_mode_ = mode;
                state.user_config->mode = mode;
                update_menu_selection(state);
                state.redraw_dirty = true;
            }
            return true;
        }

        for (const DpiMenuEntry& entry : kDpiMenuEntries) {
            if (entry.command == command) {
                if (state.user_config->reference_dpi != entry.dpi || state.reference_dpi_command != entry.command) {
                    state.reference_dpi_command = entry.command;
                    state.user_config->reference_dpi = entry.dpi;
                    update_menu_selection(state);
                    state.redraw_dirty = true;
                }
                return true;
            }
        }

        for (const UnitMenuEntry& entry : kUnitMenuEntries) {
            if (entry.command == command) {
                if (state.user_config->unit != entry.unit) {
                    state.user_config->unit = entry.unit;
                    update_menu_selection(state);
                    state.redraw_dirty = true;
                }
                return true;
            }
        }

        for (const CalibrationDistanceMenuEntry& entry : kCalibrationDistanceMenuEntries) {
            if (entry.command == command) {
                if (state.user_config->calibration_distance_cm != entry.distance_cm || state.calibration_distance_command != entry.command) {
                    state.calibration_distance_command = entry.command;
                    state.user_config->calibration_distance_cm = entry.distance_cm;
                    update_menu_selection(state);
                    state.redraw_dirty = true;
                }
                return true;
            }
        }

        for (const RecordingKeyMenuEntry& entry : kRecordingKeyMenuEntries) {
            if (entry.command == command) {
                if (state.user_config->recording_key != entry.virtual_key || state.recording_key_command != entry.command) {
                    state.user_config->recording_key = entry.virtual_key;
                    state.recording_key_command = entry.command;
                    initialize_recording_key_display(state, entry.virtual_key);
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
            case kCommandCalibrationDistanceCustom:
                show_modeless_dialog(state, IDD_CUSTOM_CALIBRATION_DISTANCE, state.custom_calibration_distance_dialog, custom_calibration_distance_dialog_proc);
                return true;
            case kCommandRecordingKeyCustom:
                show_modeless_dialog(state, IDD_CUSTOM_RECORDING_KEY, state.custom_recording_key_dialog, custom_recording_key_dialog_proc);
                return true;
            case kCommandEditConfiguration:
                open_configuration_directory(state.hwnd);
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
                if (wparam == kUiTimer) {
                    if (state != nullptr && pull_pending_input(*state)) state->redraw_dirty = true;
                    return 0;
                }
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

    void show_startup_rendering_error() noexcept {
        TASKDIALOGCONFIG config{};
        config.cbSize = sizeof(config);
        config.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION | TDF_SIZE_TO_CONTENT;
        config.dwCommonButtons = TDCBF_CLOSE_BUTTON;
        config.pszWindowTitle = kWindowTitle;
        config.pszMainIcon = TD_ERROR_ICON;
        config.pszMainInstruction = L"WinMouseSensConverter could not start.";
        config.pszContent = L"Direct2D or DirectWrite could not be initialized.";

        if (FAILED(TaskDialogIndirect(&config, nullptr, nullptr, nullptr))) {
            (void)MessageBoxW(nullptr, config.pszContent, kWindowTitle, MB_OK | MB_ICONERROR);
        }
    }

} // namespace

namespace ui {

    HWND create_main_window(HINSTANCE instance, config::UserConfig& user_config) noexcept {
        std::unique_ptr<UiState> state;
        HWND hwnd = nullptr;

        try {
            if (!register_window_class(instance)) return nullptr;

            state = std::make_unique<UiState>();
            state->user_config = &user_config;
            state->reference_dpi_command = dpi_command_for_value(user_config.reference_dpi);
            state->calibration_distance_command = calibration_distance_command_for_value(user_config.calibration_distance_cm);
            state->recording_key_command = recording_key_command_for_value(user_config.recording_key);
            if (FAILED(initialize_device_independent_resources(*state))) {
                show_startup_rendering_error();
                return nullptr;
            }
            initialize_recording_key_display(*state, user_config.recording_key);

            state->root_menu = create_main_menu();
            if (state->root_menu == nullptr) return nullptr;

            UINT dpi = GetDpiForSystem();
            if (dpi == 0) dpi = USER_DEFAULT_SCREEN_DPI;
            RECT window_rect{0, 0, scale_for_dpi(kDefaultClientWidthDip, dpi), scale_for_dpi(kDefaultClientHeightDip, dpi)};
            constexpr DWORD style = WS_OVERLAPPEDWINDOW;
            constexpr DWORD extended_style = 0;
            if (!AdjustWindowRectExForDpi(&window_rect, style, TRUE, extended_style, dpi)) return nullptr;

            hwnd = CreateWindowExW(
                extended_style,
                kWindowClassName,
                kWindowTitle,
                style,
                CW_USEDEFAULT,
                CW_USEDEFAULT,
                window_rect.right - window_rect.left,
                window_rect.bottom - window_rect.top,
                nullptr,
                nullptr,
                instance,
                state.get()
            );
            if (hwnd == nullptr) return nullptr;

            if (!SetMenu(hwnd, state->root_menu)) {
                DestroyWindow(hwnd);
                return nullptr;
            }

            update_menu_selection(*state);

            // SetMenu transfers menu lifetime to the window. Keep only non-owning access through GetMenu.
            state->root_menu = nullptr;
            state->owned_by_window = true;
            state.release();
            return hwnd;
        } catch (...) {
            if (hwnd != nullptr && IsWindow(hwnd)) DestroyWindow(hwnd);
            return nullptr;
        }
    }

    bool preprocess_modeless_dialog_message(HWND main_window, MSG& message) noexcept {
        if (main_window == nullptr || !IsWindow(main_window)) return false;

        UiState* state = reinterpret_cast<UiState*>(GetWindowLongPtrW(main_window, GWLP_USERDATA));
        if (state == nullptr) return false;

        const std::array<HWND, 5> dialogs{state->about_dialog, state->instruction_dialog, state->custom_dpi_dialog, state->custom_calibration_distance_dialog, state->custom_recording_key_dialog};
        for (HWND dialog : dialogs) {
            if (dialog != nullptr && IsWindow(dialog) && IsDialogMessageW(dialog, &message)) return true;
        }
        return false;
    }

    void finish_main_loop_iteration(HWND main_window, const MSG& message) noexcept {
        if (main_window == nullptr || !IsWindow(main_window)) return;

        UiState* state = reinterpret_cast<UiState*>(GetWindowLongPtrW(main_window, GWLP_USERDATA));
        if (state == nullptr) return;

        const bool redraw_tick = message.hwnd == main_window && message.message == WM_TIMER && message.wParam == kUiTimer;
        if (!redraw_tick || !state->redraw_dirty || state->in_size_move || state->minimized) return;

        if (paint_window(*state)) state->redraw_dirty = false;
    }

} // namespace ui
