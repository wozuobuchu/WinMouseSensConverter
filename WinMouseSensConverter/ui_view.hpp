#pragma once

#ifndef UI_VIEW_HPP_
#define UI_VIEW_HPP_

#include "config.hpp"

#include "D2DUILIB/D2DUILIB_COMPONENT/d2dui_labeled_value_grid.hpp"
#include "D2DUILIB/D2DUILIB_COMPONENT/d2dui_segmented_header.hpp"
#include "D2DUILIB/D2DUILIB_COMPONENT/d2dui_status_bar.hpp"
#include "D2DUILIB/D2DUILIB_INTERFACE/d2dui_system_render.hpp"

#include <cstddef>
#include <functional>
#include <string_view>

namespace ui::view {

    using Unit = config::OutputUnit;

    struct ViewSnapshot {
        config::AppMode mode = config::AppMode::measurement;
        bool recording = false;
        double accumulated_dx = 0.0;
        double accumulated_dy = 0.0;
        double reference_dpi = 800.0;
        Unit unit = Unit::cm;
        double calibration_distance_cm = 10.0;
        std::wstring_view recording_key_name = L"F2";
    };

    struct PageLayout {
        float scale = 1.0f;
        float content_width = 0.0f;
        D2D1_RECT_F header_bounds{};
        D2D1_RECT_F data_bounds{};
        D2D1_RECT_F footer_bounds{};
    };

    const wchar_t* unit_name(Unit unit) noexcept;
    double convert_distance(double raw_count, double reference_dpi, Unit unit) noexcept;
    double calibration_dpi_from_counts(double counts, double calibration_distance_cm) noexcept;
    PageLayout calculate_page_layout(float width, float height) noexcept;
    int format_distance_value(double raw_count, double reference_dpi, Unit unit, wchar_t* text, size_t capacity) noexcept;
    int format_reference_dpi_cell(double reference_dpi, wchar_t* text, size_t capacity) noexcept;
    int format_calibration_distance_cell(double calibration_distance_cm, double reference_dpi, Unit unit, wchar_t* text, size_t capacity) noexcept;
    int format_unit_cell(Unit unit, wchar_t* text, size_t capacity) noexcept;

    class MainView final {
    public:
        MainView();
        MainView(const MainView&) = delete;
        MainView& operator=(const MainView&) = delete;

        HRESULT prepare_resources(d2dui::D2duiContext& context) noexcept;
        HRESULT render(d2dui::D2duiContext& context, const ViewSnapshot& snapshot) noexcept;

        [[nodiscard]] d2dui::D2duiSystemRender& common_render() noexcept { return common_render_; }
        [[nodiscard]] d2dui::D2duiSystemRender& measurement_render() noexcept { return measurement_render_; }
        [[nodiscard]] d2dui::D2duiSystemRender& calibration_render() noexcept { return calibration_render_; }
        [[nodiscard]] d2dui::D2duiLabeledValueGrid& measurement_grid() noexcept { return measurement_grid_.get(); }
        [[nodiscard]] d2dui::D2duiLabeledValueGrid& calibration_grid() noexcept { return calibration_grid_.get(); }

    private:
        HRESULT update_common(const PageLayout& layout, const ViewSnapshot& snapshot);
        HRESULT update_measurement(const PageLayout& layout, const ViewSnapshot& snapshot);
        HRESULT update_calibration(const PageLayout& layout, const ViewSnapshot& snapshot);

        d2dui::D2duiSystemRender common_render_;
        d2dui::D2duiSystemRender measurement_render_;
        d2dui::D2duiSystemRender calibration_render_;

        std::reference_wrapper<d2dui::D2duiStatusBar> status_bar_;
        std::reference_wrapper<d2dui::D2duiSegmentedHeader> measurement_header_;
        std::reference_wrapper<d2dui::D2duiLabeledValueGrid> measurement_grid_;
        std::reference_wrapper<d2dui::D2duiSegmentedHeader> calibration_header_;
        std::reference_wrapper<d2dui::D2duiLabeledValueGrid> calibration_grid_;
    };

} // namespace ui::view

#endif // UI_VIEW_HPP_
