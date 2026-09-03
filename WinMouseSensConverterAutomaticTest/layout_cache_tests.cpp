#include "test_groups.hpp"

#include "ui_internal.hpp"

#include <array>
#include <cwchar>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <string_view>

namespace automatic_test {

    namespace {

        class DWriteFixture final {
        public:
            DWriteFixture() {
                state.user_config = &user_config;
                const HRESULT factory_result = DWriteCreateFactory(
                    DWRITE_FACTORY_TYPE_ISOLATED,
                    __uuidof(IDWriteFactory),
                    reinterpret_cast<IUnknown**>(state.write_factory.GetAddressOf()));
                if (FAILED(factory_result)) throw std::runtime_error("DWriteCreateFactory failed");
                create_value_format();
            }

            void create_value_format() {
                state.value_format.Reset();
                const HRESULT format_result = state.write_factory->CreateTextFormat(
                    L"Segoe UI",
                    nullptr,
                    DWRITE_FONT_WEIGHT_SEMI_BOLD,
                    DWRITE_FONT_STYLE_NORMAL,
                    DWRITE_FONT_STRETCH_NORMAL,
                    56.0f,
                    L"en-us",
                    state.value_format.GetAddressOf());
                if (FAILED(format_result)) throw std::runtime_error("CreateTextFormat failed");
                if (FAILED(state.value_format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER))) throw std::runtime_error("SetTextAlignment failed");
                if (FAILED(state.value_format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER))) throw std::runtime_error("SetParagraphAlignment failed");
                if (FAILED(state.value_format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP))) throw std::runtime_error("SetWordWrapping failed");
                const HRESULT header_cell_result = state.write_factory->CreateTextFormat(
                    L"Segoe UI",
                    nullptr,
                    DWRITE_FONT_WEIGHT_SEMI_BOLD,
                    DWRITE_FONT_STYLE_NORMAL,
                    DWRITE_FONT_STRETCH_NORMAL,
                    14.0f,
                    L"en-us",
                    state.header_cell_format.GetAddressOf());
                if (FAILED(header_cell_result)) throw std::runtime_error("header cell CreateTextFormat failed");
                if (FAILED(state.header_cell_format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER))) throw std::runtime_error("header cell SetTextAlignment failed");
                if (FAILED(state.header_cell_format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER))) throw std::runtime_error("header cell SetParagraphAlignment failed");
                if (FAILED(state.header_cell_format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP))) throw std::runtime_error("header cell SetWordWrapping failed");
            }

            config::UserConfig user_config;
            ui::detail::UiState state;
        };

        std::wstring_view cached_text(const ui::detail::TextLayoutCache& cache) {
            return std::wstring_view(cache.display_text.data(), cache.display_text_length);
        }

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
            TEST_EXPECT(runner, ui::detail::format_distance_value(800.0, 800, config::OutputUnit::cm, text, std::size(text)) > 0);
            TEST_EXPECT(runner, std::wstring_view(text) == L"2.540");
            TEST_EXPECT(runner, ui::detail::format_distance_value(-0.01, 800, config::OutputUnit::cm, text, std::size(text)) > 0);
            TEST_EXPECT(runner, std::wstring_view(text) == L"0.000");
            TEST_EXPECT(runner, ui::detail::format_distance_value(1.0e12, 800, config::OutputUnit::raw, text, std::size(text)) > 0);
            TEST_EXPECT(runner, std::wcschr(text, L'e') != nullptr);
            TEST_EXPECT(runner, std::wcsstr(text, L"raw") == nullptr);
        });

        runner.run("header configuration values are split into two cells", [&] {
            DWriteFixture fixture;
            wchar_t reference_dpi[64]{};
            wchar_t calibration_distance[192]{};
            wchar_t unit[64]{};
            TEST_EXPECT(runner, ui::detail::format_reference_dpi_cell(999999, reference_dpi, std::size(reference_dpi)) > 0);
            TEST_EXPECT(runner, std::wstring_view(reference_dpi) == L"REFDPI 999999");
            TEST_EXPECT(runner, ui::detail::format_calibration_distance_cell(1000, 999999, config::OutputUnit::raw, calibration_distance, std::size(calibration_distance)) > 0);
            TEST_EXPECT(runner, std::wstring_view(calibration_distance) == L"CALDIS 393700393.701");
            TEST_EXPECT(runner, std::wcschr(reference_dpi, L'|') == nullptr);
            TEST_EXPECT(runner, std::wcschr(calibration_distance, L'|') == nullptr);

            constexpr std::array units{
                config::OutputUnit::raw,
                config::OutputUnit::inch,
                config::OutputUnit::mm,
                config::OutputUnit::cm,
                config::OutputUnit::dm,
                config::OutputUnit::m,
            };
            for (const config::OutputUnit output_unit : units) {
                TEST_EXPECT(runner, ui::detail::format_unit_cell(output_unit, unit, std::size(unit)) > 0);
                TEST_EXPECT(runner, std::wstring_view(unit).starts_with(L"UNIT "));
                TEST_EXPECT(runner, std::wcschr(unit, L'|') == nullptr);
            }
        });

        runner.run("header cell layouts cache measured text widths", [&] {
            DWriteFixture fixture;
            ui::detail::HeaderCellLayoutCache first_cell;
            ui::detail::HeaderCellLayoutCache second_cell;
            TEST_EXPECT(runner, SUCCEEDED(ui::detail::update_header_cell_layout(fixture.state, first_cell, L"CALDIS 393700393.701", 14.0f)));
            TEST_EXPECT(runner, SUCCEEDED(ui::detail::update_header_cell_layout(fixture.state, second_cell, L"UNIT raw", 14.0f)));
            IDWriteTextLayout* first_layout = first_cell.layout.Get();
            TEST_EXPECT(runner, first_layout != nullptr);
            TEST_EXPECT(runner, first_cell.intrinsic_width > 0.0f);
            TEST_EXPECT(runner, second_cell.intrinsic_width > 0.0f);
            TEST_EXPECT(runner, SUCCEEDED(ui::detail::update_header_cell_layout(fixture.state, first_cell, L"CALDIS 393700393.701", 14.0f)));
            TEST_EXPECT(runner, first_cell.layout.Get() == first_layout);
            TEST_EXPECT(runner, SUCCEEDED(ui::detail::update_header_cell_layout(fixture.state, first_cell, L"CALDIS 393700393.701", 12.0f)));
            TEST_EXPECT(runner, first_cell.layout.Get() == first_layout);
            TEST_EXPECT_NEAR(runner, first_cell.font_size, 12.0f, 0.001f);

            const std::array<float, 2> measured_widths{first_cell.intrinsic_width, second_cell.intrinsic_width};
            const ui::detail::PageLayout page = ui::detail::calculate_page_layout(640.0f, 360.0f, 48.0f, measured_widths);
            TEST_EXPECT(runner, page.header_cell_text_scale > 0.0f);
            TEST_EXPECT(runner, page.mode_pill_bounds.right < page.header_cell_bounds[0].left);
            TEST_EXPECT_NEAR(runner, page.header_cell_bounds[0].right, page.header_cell_bounds[1].left, 0.001f);
        });

        runner.run("shared page layout remains separated at supported sizes", [&] {
            const std::array<D2D1_SIZE_F, 4> sizes{
                D2D1_SIZE_F{640.0f, 360.0f},
                D2D1_SIZE_F{800.0f, 450.0f},
                D2D1_SIZE_F{1280.0f, 720.0f},
                D2D1_SIZE_F{1920.0f, 1080.0f},
            };
            constexpr std::array<float, 2> header_text_widths{160.0f, 72.0f};
            for (const D2D1_SIZE_F size : sizes) {
                const ui::detail::PageLayout page = ui::detail::calculate_page_layout(size.width, size.height, 160.0f, header_text_widths);
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
                TEST_EXPECT(runner, has_positive_area(page.header_cell_bounds[0]));
                TEST_EXPECT(runner, has_positive_area(page.header_cell_bounds[1]));
                TEST_EXPECT(runner, is_inside(page.header_cell_bounds[0], page.header_bounds));
                TEST_EXPECT(runner, is_inside(page.header_cell_bounds[1], page.header_bounds));
                TEST_EXPECT_NEAR(runner, page.header_cell_bounds[0].right, page.header_cell_bounds[1].left, 0.001f);
                TEST_EXPECT_NEAR(runner, page.header_cell_bounds[1].right, page.header_bounds.right, 0.001f);
                TEST_EXPECT_NEAR(runner, page.header_cell_bounds[0].top, page.mode_pill_bounds.top, 0.001f);
                TEST_EXPECT_NEAR(runner, page.header_cell_bounds[1].bottom, page.mode_pill_bounds.bottom, 0.001f);
                TEST_EXPECT(runner, page.mode_pill_bounds.right + 12.0f * page.scale <= page.header_cell_bounds[0].left + 0.001f);
                TEST_EXPECT(runner, page.shortcut_badge_bounds.right <= page.shortcut_text_bounds.left);
                TEST_EXPECT(runner, page.shortcut_text_bounds.right <= page.switch_track_bounds.left);
                TEST_EXPECT(runner, is_inside(page.shortcut_badge_bounds, page.footer_bounds));
                TEST_EXPECT(runner, is_inside(page.switch_track_bounds, page.footer_bounds));
            }
            TEST_EXPECT_NEAR(runner, ui::detail::calculate_page_layout(640.0f, 360.0f, 48.0f, header_text_widths).scale, 0.80f, 0.001f);
            TEST_EXPECT_NEAR(runner, ui::detail::calculate_page_layout(800.0f, 450.0f, 48.0f, header_text_widths).scale, 1.00f, 0.001f);
        });

        runner.run("header cells scale together when width is constrained", [&] {
            constexpr std::array<float, 2> wide_header_texts{600.0f, 300.0f};
            const ui::detail::PageLayout page = ui::detail::calculate_page_layout(640.0f, 360.0f, 48.0f, wide_header_texts);
            TEST_EXPECT(runner, page.header_cell_text_scale > 0.0f && page.header_cell_text_scale < 1.0f);
            TEST_EXPECT_NEAR(runner, page.header_cell_bounds[0].right, page.header_cell_bounds[1].left, 0.001f);
            TEST_EXPECT_NEAR(runner, page.header_cell_bounds[1].right, page.header_bounds.right, 0.001f);
            TEST_EXPECT_NEAR(runner, page.header_cell_bounds[0].left, page.mode_pill_bounds.right + 12.0f * page.scale, 0.001f);
        });

        runner.run("measurement cards exactly occupy calibration data region", [&] {
            const ui::detail::PageLayout page = ui::detail::calculate_page_layout(640.0f, 360.0f, 48.0f, std::array<float, 2>{80.0f, 60.0f});
            const auto cards = ui::detail::calculate_measurement_card_bounds(page);
            TEST_EXPECT(runner, cards[0].left == page.data_bounds.left);
            TEST_EXPECT(runner, cards[1].right == page.data_bounds.right);
            TEST_EXPECT(runner, cards[0].top == page.data_bounds.top && cards[1].top == page.data_bounds.top);
            TEST_EXPECT(runner, cards[0].bottom == page.data_bounds.bottom && cards[1].bottom == page.data_bounds.bottom);
            TEST_EXPECT_NEAR(runner, cards[1].left - cards[0].right, page.card_gap, 0.001f);
            TEST_EXPECT(runner, cards[0].right < cards[1].left);
        });

        runner.run("numeric layout caches text size and style", [&] {
            DWriteFixture fixture;
            ui::detail::TextLayoutCache cache;
            constexpr UINT32 no_suffix = std::numeric_limits<UINT32>::max();
            TEST_EXPECT(runner, SUCCEEDED(ui::detail::update_numeric_layout(fixture.state, cache, L"2.540", no_suffix, 300.0f, 100.0f, 56.0f)));
            IDWriteTextLayout* first_layout = cache.layout.Get();
            TEST_EXPECT(runner, first_layout != nullptr);
            TEST_EXPECT(runner, cached_text(cache) == L"2.540");
            TEST_EXPECT(runner, SUCCEEDED(ui::detail::update_numeric_layout(fixture.state, cache, L"2.540", no_suffix, 300.0f, 100.0f, 56.0f)));
            TEST_EXPECT(runner, cache.layout.Get() == first_layout);
            TEST_EXPECT(runner, SUCCEEDED(ui::detail::update_numeric_layout(fixture.state, cache, L"2.540", no_suffix, 300.0f, 100.0f, 44.8f)));
            TEST_EXPECT(runner, cache.layout.Get() != first_layout);
            IDWriteTextLayout* resized_layout = cache.layout.Get();
            TEST_EXPECT(runner, SUCCEEDED(ui::detail::update_numeric_layout(fixture.state, cache, L"2.540", no_suffix, 301.0f, 100.0f, 44.8f)));
            TEST_EXPECT(runner, cache.layout.Get() != resized_layout);
        });

        runner.run("numeric cache is preserved after layout creation failure", [&] {
            DWriteFixture fixture;
            ui::detail::TextLayoutCache cache;
            constexpr UINT32 no_suffix = std::numeric_limits<UINT32>::max();
            TEST_EXPECT(runner, SUCCEEDED(ui::detail::update_numeric_layout(fixture.state, cache, L"2.540", no_suffix, 300.0f, 100.0f, 56.0f)));
            IDWriteTextLayout* original_layout = cache.layout.Get();
            const auto original_text = cache.display_text;
            const float original_font_size = cache.requested_primary_font_size;
            fixture.state.value_format.Reset();
            TEST_EXPECT(runner, FAILED(ui::detail::update_numeric_layout(fixture.state, cache, L"3.810", no_suffix, 320.0f, 100.0f, 56.0f)));
            TEST_EXPECT(runner, cache.layout.Get() == original_layout);
            TEST_EXPECT(runner, cache.display_text == original_text);
            TEST_EXPECT(runner, cache.requested_primary_font_size == original_font_size);
        });

        runner.run("measurement peers reuse formal layouts and share their smaller fit", [&] {
            DWriteFixture fixture;
            std::array<const wchar_t*, 2> values{L"1.000", L"-1.235e+12"};
            TEST_EXPECT(runner, SUCCEEDED(ui::detail::update_measurement_value_layouts(fixture.state, values, 150.0f, 80.0f, 56.0f)));
            TEST_EXPECT(runner, fixture.state.numeric_layout_creation_count == 2);

            auto& first = fixture.state.measurement_value_layouts[0];
            auto& second = fixture.state.measurement_value_layouts[1];
            IDWriteTextLayout* first_layout = first.layout.Get();
            IDWriteTextLayout* second_layout = second.layout.Get();
            TEST_EXPECT(runner, first.content_fit_scale > second.content_fit_scale);
            TEST_EXPECT(runner, first.applied_fit_scale == second.applied_fit_scale);
            float first_font_size = 0.0f;
            float second_font_size = 0.0f;
            TEST_EXPECT(runner, SUCCEEDED(first.layout->GetFontSize(0, &first_font_size)));
            TEST_EXPECT(runner, SUCCEEDED(second.layout->GetFontSize(0, &second_font_size)));
            TEST_EXPECT_NEAR(runner, first_font_size, second_font_size, 0.001f);

            TEST_EXPECT(runner, SUCCEEDED(ui::detail::update_measurement_value_layouts(fixture.state, values, 150.0f, 80.0f, 56.0f)));
            TEST_EXPECT(runner, fixture.state.numeric_layout_creation_count == 2);
            TEST_EXPECT(runner, first.layout.Get() == first_layout);
            TEST_EXPECT(runner, second.layout.Get() == second_layout);

            values[0] = L"2.000";
            TEST_EXPECT(runner, SUCCEEDED(ui::detail::update_measurement_value_layouts(fixture.state, values, 150.0f, 80.0f, 56.0f)));
            TEST_EXPECT(runner, fixture.state.numeric_layout_creation_count == 3);
            TEST_EXPECT(runner, first.layout.Get() != first_layout);
            TEST_EXPECT(runner, second.layout.Get() == second_layout);

            TEST_EXPECT(runner, SUCCEEDED(ui::detail::update_measurement_value_layouts(fixture.state, values, 800.0f, 100.0f, 56.0f)));
            TEST_EXPECT(runner, fixture.state.numeric_layout_creation_count == 5);
            TEST_EXPECT_NEAR(runner, first.content_fit_scale, 1.0f, 0.001f);
            TEST_EXPECT_NEAR(runner, second.content_fit_scale, 1.0f, 0.001f);
            TEST_EXPECT(runner, SUCCEEDED(first.layout->GetFontSize(0, &first_font_size)));
            TEST_EXPECT(runner, SUCCEEDED(second.layout->GetFontSize(0, &second_font_size)));
            TEST_EXPECT_NEAR(runner, first_font_size, 56.0f, 0.001f);
            TEST_EXPECT_NEAR(runner, second_font_size, 56.0f, 0.001f);
        });

        runner.run("calibration result keeps only its DPI suffix", [&] {
            DWriteFixture fixture;
            ui::detail::SharedDataSnapshot shared_data{};
            TEST_EXPECT(runner, SUCCEEDED(ui::detail::update_calibration_dpi_layout(fixture.state, shared_data, 400.0f, 100.0f, 56.0f, 18.0f)));
            TEST_EXPECT(runner, cached_text(fixture.state.calibration_dpi_layout) == L"\x2014 DPI");
            shared_data.accumulated_dx = 3.0;
            shared_data.accumulated_dy = 4.0;
            TEST_EXPECT(runner, SUCCEEDED(ui::detail::update_calibration_dpi_layout(fixture.state, shared_data, 400.0f, 100.0f, 56.0f, 18.0f)));
            TEST_EXPECT(runner, cached_text(fixture.state.calibration_dpi_layout) == L"1.27 DPI");
            TEST_EXPECT(runner, std::wcsstr(fixture.state.calibration_dpi_layout.display_text.data(), L"MEASURED") == nullptr);
            float value_size = 0.0f;
            float suffix_size = 0.0f;
            TEST_EXPECT(runner, SUCCEEDED(fixture.state.calibration_dpi_layout.layout->GetFontSize(0, &value_size)));
            TEST_EXPECT(runner, SUCCEEDED(fixture.state.calibration_dpi_layout.layout->GetFontSize(6, &suffix_size)));
            TEST_EXPECT_NEAR(runner, value_size, 56.0f, 0.001f);
            TEST_EXPECT_NEAR(runner, suffix_size, 18.0f, 0.001f);
        });

        runner.run("calibration result shrinks within constrained height", [&] {
            DWriteFixture fixture;
            ui::detail::SharedDataSnapshot shared_data{};
            shared_data.accumulated_dx = 3196.062992125984;
            TEST_EXPECT(runner, SUCCEEDED(ui::detail::update_calibration_dpi_layout(fixture.state, shared_data, 400.0f, 30.0f, 56.0f, 18.0f)));
            DWRITE_TEXT_METRICS metrics{};
            TEST_EXPECT(runner, SUCCEEDED(fixture.state.calibration_dpi_layout.layout->GetMetrics(&metrics)));
            TEST_EXPECT(runner, metrics.height <= 28.0f);
        });
    }

} // namespace automatic_test
