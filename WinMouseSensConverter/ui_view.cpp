#include "ui_view.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cwchar>
#include <iterator>
#include <new>
#include <string>
#include <vector>

namespace ui::view {

    const wchar_t* unit_name(Unit unit) noexcept {
        switch (unit) {
            case Unit::raw: return L"raw";
            case Unit::inch: return L"inch";
            case Unit::mm: return L"mm";
            case Unit::cm: return L"cm";
            case Unit::dm: return L"dm";
            case Unit::m: return L"m";
        }
        return L"raw";
    }

    double convert_distance(double raw_count, int reference_dpi, Unit unit) noexcept {
        if (unit == Unit::raw) return raw_count;
        const double inches = raw_count / static_cast<double>(reference_dpi);
        switch (unit) {
            case Unit::inch: return inches;
            case Unit::mm: return inches * 25.4;
            case Unit::cm: return inches * 2.54;
            case Unit::dm: return inches * 0.254;
            case Unit::m: return inches * 0.0254;
            case Unit::raw: return raw_count;
        }
        return raw_count;
    }

    double calibration_dpi_from_counts(double counts, int calibration_distance_cm) noexcept {
        const double target_inches = static_cast<double>(calibration_distance_cm) / 2.54;
        return target_inches > 0.0 ? counts / target_inches : 0.0;
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

    PageLayout calculate_page_layout(float width, float height) noexcept {
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
        layout.header_bounds = D2D1::RectF(left, top, right, top + header_height);
        layout.data_bounds = D2D1::RectF(left, layout.header_bounds.bottom + section_gap, right, layout.header_bounds.bottom + section_gap + data_height);
        layout.footer_bounds = D2D1::RectF(left, layout.data_bounds.bottom + section_gap, right, layout.data_bounds.bottom + section_gap + footer_height);
        return layout;
    }

    MainView::MainView()
        : status_bar_(common_render_.emplace_component<d2dui::D2duiStatusBar>()),
          measurement_header_(measurement_render_.emplace_component<d2dui::D2duiSegmentedHeader>()),
          measurement_grid_(measurement_render_.emplace_component<d2dui::D2duiLabeledValueGrid>()),
          calibration_header_(calibration_render_.emplace_component<d2dui::D2duiSegmentedHeader>()),
          calibration_grid_(calibration_render_.emplace_component<d2dui::D2duiLabeledValueGrid>()) {
        measurement_header_.get().set_leading_text(L"Measurement");
        measurement_header_.get().set_cells({L"REFDPI 800", L"UNIT cm"});
        measurement_grid_.get().set_items({
            {L"X \x00B7 HORIZONTAL", L"0.000", d2dui::D2duiText::no_suffix},
            {L"Y \x00B7 VERTICAL", L"0.000", d2dui::D2duiText::no_suffix},
        });
        calibration_header_.get().set_leading_text(L"Calibration");
        calibration_header_.get().set_cells({L"CALDIS 10.000", L"UNIT cm"});
        calibration_grid_.get().set_items({
            {L"CALIBRATED DPI", L"\x2014 DPI", 2},
        });
    }

    HRESULT MainView::update_common(const PageLayout& layout, const ViewSnapshot& snapshot) {
        auto& status = status_bar_.get();
        status.resize(layout.footer_bounds, layout.scale);
        status.set_badge_text(std::wstring(snapshot.recording_key_name));
        status.set_checked(snapshot.recording);
        return S_OK;
    }

    HRESULT MainView::update_measurement(const PageLayout& layout, const ViewSnapshot& snapshot) {
        wchar_t reference_dpi[64]{};
        wchar_t unit[64]{};
        wchar_t x_value[128]{};
        wchar_t y_value[128]{};
        if (format_reference_dpi_cell(snapshot.reference_dpi, reference_dpi, std::size(reference_dpi)) <= 0) return E_FAIL;
        if (format_unit_cell(snapshot.unit, unit, std::size(unit)) <= 0) return E_FAIL;
        if (format_distance_value(snapshot.accumulated_dx, snapshot.reference_dpi, snapshot.unit, x_value, std::size(x_value)) <= 0) return E_FAIL;
        if (format_distance_value(snapshot.accumulated_dy, snapshot.reference_dpi, snapshot.unit, y_value, std::size(y_value)) <= 0) return E_FAIL;

        auto& header = measurement_header_.get();
        header.resize(layout.header_bounds, layout.scale);
        header.set_cell_text(0, reference_dpi);
        header.set_cell_text(1, unit);
        auto& grid = measurement_grid_.get();
        grid.resize(layout.data_bounds, layout.scale);
        grid.set_value(0, x_value);
        grid.set_value(1, y_value);
        return S_OK;
    }

    HRESULT MainView::update_calibration(const PageLayout& layout, const ViewSnapshot& snapshot) {
        wchar_t calibration_distance[192]{};
        wchar_t unit[64]{};
        wchar_t value[128]{};
        if (format_calibration_distance_cell(snapshot.calibration_distance_cm, snapshot.reference_dpi, snapshot.unit, calibration_distance, std::size(calibration_distance)) <= 0) return E_FAIL;
        if (format_unit_cell(snapshot.unit, unit, std::size(unit)) <= 0) return E_FAIL;
        const double counts = std::hypot(snapshot.accumulated_dx, snapshot.accumulated_dy);
        const double dpi = calibration_dpi_from_counts(counts, snapshot.calibration_distance_cm);
        int written = 0;
        if (counts <= 0.0) written = swprintf_s(value, L"\x2014 DPI");
        else if (std::isfinite(dpi) && std::abs(dpi) < 1.0e9) written = swprintf_s(value, L"%.2f DPI", dpi);
        else written = swprintf_s(value, L"%.2e DPI", dpi);
        if (written <= 0) return E_FAIL;
        const wchar_t* separator = std::wcschr(value, L' ');
        if (separator == nullptr) return E_FAIL;

        auto& header = calibration_header_.get();
        header.resize(layout.header_bounds, layout.scale);
        header.set_cell_text(0, calibration_distance);
        header.set_cell_text(1, unit);
        auto& grid = calibration_grid_.get();
        grid.resize(layout.data_bounds, layout.scale);
        grid.set_value(0, value, static_cast<UINT32>(separator - value) + 1);
        return S_OK;
    }

    HRESULT MainView::render(d2dui::D2duiContext& context, const ViewSnapshot& snapshot) noexcept {
        const HRESULT begin_result = context.begin_frame({0xF4F7FB, 1.0f});
        if (begin_result != S_OK) return begin_result;

        HRESULT content_result = S_OK;
        try {
            const D2D1_SIZE_F size = context.size();
            const PageLayout layout = calculate_page_layout(size.width, size.height);
            if (layout.content_width <= 1.0f || layout.data_bounds.bottom <= layout.data_bounds.top) {
                content_result = E_FAIL;
            } else {
                content_result = update_common(layout, snapshot);
                if (SUCCEEDED(content_result)) {
                    content_result = snapshot.mode == config::AppMode::calibration
                        ? update_calibration(layout, snapshot)
                        : update_measurement(layout, snapshot);
                }
                if (SUCCEEDED(content_result)) content_result = common_render_.draw(context);
                if (SUCCEEDED(content_result)) {
                    content_result = snapshot.mode == config::AppMode::calibration
                        ? calibration_render_.draw(context)
                        : measurement_render_.draw(context);
                }
            }
        } catch (const std::bad_alloc&) {
            content_result = E_OUTOFMEMORY;
        } catch (...) {
            content_result = E_FAIL;
        }

        const HRESULT end_result = context.end_frame();
        return FAILED(end_result) ? end_result : content_result;
    }

} // namespace ui::view
