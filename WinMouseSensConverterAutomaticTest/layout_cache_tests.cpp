#include "test_groups.hpp"

#include "ui_view.hpp"

#include "D2DUILIB/D2DUILIB_COMPONENT/d2dui_switch.hpp"
#include "D2DUILIB/D2DUILIB_COMPONENT/d2dui_text.hpp"

#include <array>
#include <cwchar>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string_view>

namespace automatic_test {

    namespace {

        class WindowFixture final {
        public:
            WindowFixture() {
                hwnd = CreateWindowExW(0, L"STATIC", L"D2DUI Test", WS_POPUP, 0, 0, 800, 450, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
                if (hwnd == nullptr) throw std::runtime_error("CreateWindowExW failed");
                if (FAILED(context.initialize(hwnd))) throw std::runtime_error("D2duiContext initialization failed");
            }

            ~WindowFixture() {
                context.shutdown();
                if (hwnd != nullptr) DestroyWindow(hwnd);
            }

            HWND hwnd = nullptr;
            d2dui::D2duiContext context;
        };

        bool has_positive_area(const D2D1_RECT_F& bounds) {
            return bounds.right > bounds.left && bounds.bottom > bounds.top;
        }

        bool is_inside(const D2D1_RECT_F& child, const D2D1_RECT_F& parent) {
            return child.left >= parent.left && child.top >= parent.top && child.right <= parent.right && child.bottom <= parent.bottom;
        }

    } // namespace

    void add_layout_cache_tests(TestRunner& runner) {
        runner.run("distance formatting omits repeated units and compacts extremes", [&] {
            wchar_t text[128]{};
            TEST_EXPECT(runner, ui::view::format_distance_value(800.0, 800, config::OutputUnit::cm, text, std::size(text)) > 0);
            TEST_EXPECT(runner, std::wstring_view(text) == L"2.540");
            TEST_EXPECT(runner, ui::view::format_distance_value(-0.01, 800, config::OutputUnit::cm, text, std::size(text)) > 0);
            TEST_EXPECT(runner, std::wstring_view(text) == L"0.000");
            TEST_EXPECT(runner, ui::view::format_distance_value(1.0e12, 800, config::OutputUnit::raw, text, std::size(text)) > 0);
            TEST_EXPECT(runner, std::wcschr(text, L'e') != nullptr);
            TEST_EXPECT(runner, std::wcsstr(text, L"raw") == nullptr);
        });

        runner.run("header configuration values remain split", [&] {
            wchar_t reference_dpi[64]{};
            wchar_t calibration_distance[192]{};
            wchar_t unit[64]{};
            TEST_EXPECT(runner, ui::view::format_reference_dpi_cell(999999, reference_dpi, std::size(reference_dpi)) > 0);
            TEST_EXPECT(runner, std::wstring_view(reference_dpi) == L"REFDPI 999999");
            TEST_EXPECT(runner, ui::view::format_calibration_distance_cell(1000, 999999, config::OutputUnit::raw, calibration_distance, std::size(calibration_distance)) > 0);
            TEST_EXPECT(runner, std::wstring_view(calibration_distance) == L"CALDIS 393700393.701");
            TEST_EXPECT(runner, ui::view::format_unit_cell(config::OutputUnit::cm, unit, std::size(unit)) > 0);
            TEST_EXPECT(runner, std::wstring_view(unit) == L"UNIT cm");
        });

        runner.run("page layout remains separated at supported sizes", [&] {
            const std::array<D2D1_SIZE_F, 4> sizes{
                D2D1_SIZE_F{640.0f, 360.0f}, D2D1_SIZE_F{800.0f, 450.0f},
                D2D1_SIZE_F{1280.0f, 720.0f}, D2D1_SIZE_F{1920.0f, 1080.0f},
            };
            for (const D2D1_SIZE_F size : sizes) {
                const ui::view::PageLayout page = ui::view::calculate_page_layout(size.width, size.height);
                const D2D1_RECT_F client = D2D1::RectF(0.0f, 0.0f, size.width, size.height);
                TEST_EXPECT(runner, page.scale >= 0.80f && page.scale <= 1.0f);
                TEST_EXPECT(runner, has_positive_area(page.header_bounds));
                TEST_EXPECT(runner, has_positive_area(page.data_bounds));
                TEST_EXPECT(runner, has_positive_area(page.footer_bounds));
                TEST_EXPECT(runner, is_inside(page.header_bounds, client));
                TEST_EXPECT(runner, is_inside(page.data_bounds, client));
                TEST_EXPECT(runner, is_inside(page.footer_bounds, client));
                TEST_EXPECT(runner, page.header_bounds.bottom <= page.data_bounds.top);
                TEST_EXPECT(runner, page.data_bounds.bottom <= page.footer_bounds.top);
            }
            TEST_EXPECT_NEAR(runner, ui::view::calculate_page_layout(640.0f, 360.0f).scale, 0.80f, 0.001f);
            TEST_EXPECT_NEAR(runner, ui::view::calculate_page_layout(800.0f, 450.0f).scale, 1.00f, 0.001f);
        });

        runner.run("system renderer returns stable registered references", [&] {
            d2dui::D2duiSystemRender render;
            auto& first = render.emplace_component<d2dui::D2duiSwitch>();
            d2dui::D2duiSwitch* first_address = &first;
            for (int index = 0; index < 64; ++index) render.emplace_component<d2dui::D2duiSwitch>();
            TEST_EXPECT(runner, &first == first_address);
            TEST_EXPECT(runner, render.size() == 65);
            TEST_EXPECT(runner, render.unregister_component(first));
            TEST_EXPECT(runner, render.size() == 64);
        });

        runner.run("switch click is behavior only", [&] {
            d2dui::D2duiSwitch toggle;
            TEST_EXPECT(runner, !toggle.checked());
            toggle.on_click();
            TEST_EXPECT(runner, toggle.checked());
            toggle.set_enabled(false);
            toggle.on_click();
            TEST_EXPECT(runner, toggle.checked());
        });

        runner.run("text layout is reused until its input changes", [&] {
            WindowFixture fixture;
            TEST_EXPECT(runner, fixture.context.begin_frame({0xFFFFFF, 1.0f}) == S_OK);
            d2dui::D2duiText text(L"2.540");
            d2dui::D2duiTextStyle style{};
            style.font_size = 56.0f;
            style.weight = DWRITE_FONT_WEIGHT_SEMI_BOLD;
            text.set_style(style);
            text.set_fit_to_bounds(true);
            text.resize(D2D1::RectF(0.0f, 0.0f, 300.0f, 100.0f), 1.0f);
            TEST_EXPECT(runner, SUCCEEDED(text.draw(fixture.context)));
            IDWriteTextLayout* first_layout = text.layout();
            TEST_EXPECT(runner, first_layout != nullptr);
            TEST_EXPECT(runner, SUCCEEDED(text.draw(fixture.context)));
            TEST_EXPECT(runner, text.layout() == first_layout);
            text.set_text(L"3.810");
            TEST_EXPECT(runner, SUCCEEDED(text.draw(fixture.context)));
            TEST_EXPECT(runner, text.layout() != first_layout);
            TEST_EXPECT(runner, SUCCEEDED(fixture.context.end_frame()));
        });

        runner.run("three render view draws both modes through one context", [&] {
            WindowFixture fixture;
            ui::view::MainView view;
            TEST_EXPECT(runner, view.common_render().size() == 1);
            TEST_EXPECT(runner, view.measurement_render().size() == 2);
            TEST_EXPECT(runner, view.calibration_render().size() == 2);

            ui::view::ViewSnapshot snapshot{};
            snapshot.recording_key_name = L"F2";
            snapshot.mode = config::AppMode::measurement;
            snapshot.accumulated_dx = 800.0;
            TEST_EXPECT(runner, view.calibration_grid().value_component(0).layout() == nullptr);
            TEST_EXPECT(runner, SUCCEEDED(view.render(fixture.context, snapshot)));
            IDWriteTextLayout* measurement_layout = view.measurement_grid().value_component(0).layout();
            TEST_EXPECT(runner, measurement_layout != nullptr);
            TEST_EXPECT(runner, view.calibration_grid().value_component(0).layout() == nullptr);
            TEST_EXPECT(runner, SUCCEEDED(view.render(fixture.context, snapshot)));
            TEST_EXPECT(runner, view.measurement_grid().value_component(0).layout() == measurement_layout);
            snapshot.mode = config::AppMode::calibration;
            snapshot.accumulated_dx = 3.0;
            snapshot.accumulated_dy = 4.0;
            TEST_EXPECT(runner, SUCCEEDED(view.render(fixture.context, snapshot)));
            TEST_EXPECT(runner, view.calibration_grid().value_component(0).layout() != nullptr);
            TEST_EXPECT(runner, !fixture.context.in_frame());
        });

        runner.run("renderer submits only inside an active frame", [&] {
            WindowFixture fixture;
            d2dui::D2duiSystemRender render;
            render.emplace_component<d2dui::D2duiSwitch>();
            TEST_EXPECT(runner, render.draw(fixture.context) == D2DERR_WRONG_STATE);
            TEST_EXPECT(runner, fixture.context.begin_frame({0xFFFFFF, 1.0f}) == S_OK);
            TEST_EXPECT(runner, SUCCEEDED(render.draw(fixture.context)));
            TEST_EXPECT(runner, SUCCEEDED(fixture.context.end_frame()));
            TEST_EXPECT(runner, render.draw(fixture.context) == D2DERR_WRONG_STATE);
        });

        runner.run("text supports a separately sized suffix", [&] {
            WindowFixture fixture;
            TEST_EXPECT(runner, fixture.context.begin_frame({0xFFFFFF, 1.0f}) == S_OK);
            d2dui::D2duiText text(L"1.27 DPI");
            d2dui::D2duiTextStyle style{};
            style.font_size = 56.0f;
            style.weight = DWRITE_FONT_WEIGHT_SEMI_BOLD;
            text.set_style(style);
            text.set_suffix(5, 18.0f);
            text.resize(D2D1::RectF(0.0f, 0.0f, 400.0f, 100.0f), 1.0f);
            TEST_EXPECT(runner, SUCCEEDED(text.draw(fixture.context)));
            float value_size = 0.0f;
            float suffix_size = 0.0f;
            TEST_EXPECT(runner, SUCCEEDED(text.layout()->GetFontSize(0, &value_size)));
            TEST_EXPECT(runner, SUCCEEDED(text.layout()->GetFontSize(5, &suffix_size)));
            TEST_EXPECT_NEAR(runner, value_size, 56.0f, 0.001f);
            TEST_EXPECT_NEAR(runner, suffix_size, 18.0f, 0.001f);
            TEST_EXPECT(runner, SUCCEEDED(fixture.context.end_frame()));
        });

        runner.run("value grid shares fit and preserves card geometry", [&] {
            WindowFixture fixture;
            TEST_EXPECT(runner, fixture.context.begin_frame({0xFFFFFF, 1.0f}) == S_OK);
            d2dui::D2duiLabeledValueGrid grid;
            grid.set_items({
                {L"FIRST", L"1.000", d2dui::D2duiText::no_suffix},
                {L"SECOND", L"-1.235e+12", d2dui::D2duiText::no_suffix},
            });
            grid.resize(D2D1::RectF(0.0f, 0.0f, 300.0f, 150.0f), 1.0f);
            TEST_EXPECT(runner, SUCCEEDED(grid.draw(fixture.context)));
            float first_size = 0.0f;
            float second_size = 0.0f;
            TEST_EXPECT(runner, SUCCEEDED(grid.value_component(0).layout()->GetFontSize(0, &first_size)));
            TEST_EXPECT(runner, SUCCEEDED(grid.value_component(1).layout()->GetFontSize(0, &second_size)));
            TEST_EXPECT_NEAR(runner, first_size, second_size, 0.001f);
            TEST_EXPECT(runner, first_size < 56.0f);
            TEST_EXPECT(runner, grid.item_bounds(0).right < grid.item_bounds(1).left);
            TEST_EXPECT_NEAR(runner, grid.item_bounds(1).left - grid.item_bounds(0).right, 16.0f, 0.001f);
            TEST_EXPECT(runner, SUCCEEDED(fixture.context.end_frame()));
        });

        runner.run("context shares caches across component queues", [&] {
            WindowFixture fixture;
            TEST_EXPECT(runner, fixture.context.begin_frame({0xFFFFFF, 1.0f}) == S_OK);
            ID2D1SolidColorBrush* first_brush = nullptr;
            ID2D1SolidColorBrush* second_brush = nullptr;
            TEST_EXPECT(runner, SUCCEEDED(fixture.context.get_brush({0x2563EB, 1.0f}, &first_brush)));
            TEST_EXPECT(runner, SUCCEEDED(fixture.context.get_brush({0x2563EB, 1.0f}, &second_brush)));
            TEST_EXPECT(runner, first_brush == second_brush);
            TEST_EXPECT(runner, fixture.context.brush_cache_size() == 1);
            d2dui::D2duiTextStyle style{};
            IDWriteTextFormat* first_format = nullptr;
            IDWriteTextFormat* second_format = nullptr;
            TEST_EXPECT(runner, SUCCEEDED(fixture.context.get_text_format(style, &first_format)));
            TEST_EXPECT(runner, SUCCEEDED(fixture.context.get_text_format(style, &second_format)));
            TEST_EXPECT(runner, first_format == second_format);
            TEST_EXPECT(runner, fixture.context.text_format_cache_size() == 1);
            TEST_EXPECT(runner, SUCCEEDED(fixture.context.end_frame()));
            fixture.context.discard_device_resources();
            TEST_EXPECT(runner, fixture.context.brush_cache_size() == 0);
            TEST_EXPECT(runner, fixture.context.text_format_cache_size() == 1);
        });
    }

} // namespace automatic_test
