#include "test_groups.hpp"

#include "ui_internal.hpp"

#include <array>
#include <cwchar>
#include <stdexcept>
#include <string_view>

namespace automatic_test {

    namespace {

        class DWriteFixture final {
        public:
            DWriteFixture() {
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
                    DWRITE_FONT_WEIGHT_NORMAL,
                    DWRITE_FONT_STYLE_NORMAL,
                    DWRITE_FONT_STRETCH_NORMAL,
                    48.0f,
                    L"en-us",
                    state.value_format.GetAddressOf());
                if (FAILED(format_result)) throw std::runtime_error("CreateTextFormat failed");
            }

            ui::detail::UiState state;
        };

        std::wstring_view cached_text(const ui::detail::ValueLayoutCache& cache) {
            return std::wstring_view(cache.display_text.data(), cache.display_text_length);
        }

        std::wstring_view cached_text(const ui::detail::DpiResultLayoutCache& cache) {
            return std::wstring_view(cache.display_text.data(), cache.display_text_length);
        }

    } // namespace

    void add_layout_cache_tests(TestRunner& runner) {
        runner.run("measurement layout reuses identical formatted text", [&] {
            DWriteFixture fixture;
            fixture.state.reference_dpi = 800;
            fixture.state.unit = config::OutputUnit::cm;
            ui::detail::ValueLayoutCache cache;

            TEST_EXPECT(runner, SUCCEEDED(ui::detail::update_value_layout(fixture.state, cache, 1.0, 400.0f, 100.0f)));
            IDWriteTextLayout* first_layout = cache.layout.Get();
            TEST_EXPECT(runner, first_layout != nullptr);
            TEST_EXPECT(runner, cached_text(cache) == L"0.003 cm");

            TEST_EXPECT(runner, SUCCEEDED(ui::detail::update_value_layout(fixture.state, cache, 1.1, 400.0f, 100.0f)));
            TEST_EXPECT(runner, cache.layout.Get() == first_layout);

            TEST_EXPECT(runner, SUCCEEDED(ui::detail::update_value_layout(fixture.state, cache, 2.0, 400.0f, 100.0f)));
            TEST_EXPECT(runner, cache.layout.Get() != first_layout);
            TEST_EXPECT(runner, cached_text(cache) == L"0.006 cm");
        });

        runner.run("measurement layout invalidates on either dimension", [&] {
            DWriteFixture fixture;
            ui::detail::ValueLayoutCache cache;

            TEST_EXPECT(runner, SUCCEEDED(ui::detail::update_value_layout(fixture.state, cache, 800.0, 300.0f, 90.0f)));
            IDWriteTextLayout* initial_layout = cache.layout.Get();
            TEST_EXPECT(runner, SUCCEEDED(ui::detail::update_value_layout(fixture.state, cache, 800.0, 301.0f, 90.0f)));
            TEST_EXPECT(runner, cache.layout.Get() != initial_layout);

            IDWriteTextLayout* width_layout = cache.layout.Get();
            TEST_EXPECT(runner, SUCCEEDED(ui::detail::update_value_layout(fixture.state, cache, 800.0, 301.0f, 91.0f)));
            TEST_EXPECT(runner, cache.layout.Get() != width_layout);
            TEST_EXPECT(runner, cache.width == 301.0f);
            TEST_EXPECT(runner, cache.height == 91.0f);
        });

        runner.run("measurement layout preserves cache after creation failure", [&] {
            DWriteFixture fixture;
            ui::detail::ValueLayoutCache cache;
            TEST_EXPECT(runner, SUCCEEDED(ui::detail::update_value_layout(fixture.state, cache, 800.0, 300.0f, 90.0f)));

            IDWriteTextLayout* original_layout = cache.layout.Get();
            const auto original_text = cache.display_text;
            const UINT32 original_length = cache.display_text_length;
            const float original_width = cache.width;
            const float original_height = cache.height;
            fixture.state.value_format.Reset();

            TEST_EXPECT(runner, FAILED(ui::detail::update_value_layout(fixture.state, cache, 1600.0, 320.0f, 100.0f)));
            TEST_EXPECT(runner, cache.layout.Get() == original_layout);
            TEST_EXPECT(runner, cache.display_text == original_text);
            TEST_EXPECT(runner, cache.display_text_length == original_length);
            TEST_EXPECT(runner, cache.width == original_width);
            TEST_EXPECT(runner, cache.height == original_height);
        });

        runner.run("measurement layout retains mixed styles and shrinking", [&] {
            DWriteFixture fixture;
            fixture.state.unit = config::OutputUnit::cm;
            ui::detail::ValueLayoutCache cache;
            TEST_EXPECT(runner, SUCCEEDED(ui::detail::update_value_layout(fixture.state, cache, 800.0, 600.0f, 120.0f)));

            const wchar_t* separator = std::wcschr(cache.display_text.data(), L' ');
            TEST_EXPECT(runner, separator != nullptr);
            const UINT32 unit_start = static_cast<UINT32>(separator - cache.display_text.data()) + 1;
            float value_size = 0.0f;
            float unit_size = 0.0f;
            DWRITE_FONT_WEIGHT unit_weight = DWRITE_FONT_WEIGHT_NORMAL;
            TEST_EXPECT(runner, SUCCEEDED(cache.layout->GetFontSize(0, &value_size)));
            TEST_EXPECT(runner, SUCCEEDED(cache.layout->GetFontSize(unit_start, &unit_size)));
            TEST_EXPECT(runner, SUCCEEDED(cache.layout->GetFontWeight(unit_start, &unit_weight)));
            TEST_EXPECT_NEAR(runner, value_size, 48.0f, 0.001f);
            TEST_EXPECT_NEAR(runner, unit_size, 18.0f, 0.001f);
            TEST_EXPECT(runner, unit_weight == DWRITE_FONT_WEIGHT_SEMI_BOLD);

            ui::detail::ValueLayoutCache narrow_cache;
            TEST_EXPECT(runner, SUCCEEDED(ui::detail::update_value_layout(fixture.state, narrow_cache, 999999999.0, 90.0f, 100.0f)));
            float narrow_value_size = 0.0f;
            TEST_EXPECT(runner, SUCCEEDED(narrow_cache.layout->GetFontSize(0, &narrow_value_size)));
            TEST_EXPECT(runner, narrow_value_size >= 20.0f);
            TEST_EXPECT(runner, narrow_value_size < 48.0f);
        });

        runner.run("calibration layout handles placeholder and formatted reuse", [&] {
            DWriteFixture fixture;
            fixture.state.calibration_distance_cm = 10;
            ui::detail::SharedDataSnapshot shared_data{};

            TEST_EXPECT(runner, SUCCEEDED(ui::detail::update_calibration_dpi_layout(fixture.state, shared_data, 400.0f, 100.0f)));
            IDWriteTextLayout* placeholder_layout = fixture.state.calibration_dpi_layout.layout.Get();
            TEST_EXPECT(runner, cached_text(fixture.state.calibration_dpi_layout) == L"— DPI");
            TEST_EXPECT(runner, SUCCEEDED(ui::detail::update_calibration_dpi_layout(fixture.state, shared_data, 400.0f, 100.0f)));
            TEST_EXPECT(runner, fixture.state.calibration_dpi_layout.layout.Get() == placeholder_layout);

            shared_data.accumulated_dx = 100.0;
            TEST_EXPECT(runner, SUCCEEDED(ui::detail::update_calibration_dpi_layout(fixture.state, shared_data, 400.0f, 100.0f)));
            IDWriteTextLayout* value_layout = fixture.state.calibration_dpi_layout.layout.Get();
            TEST_EXPECT(runner, value_layout != placeholder_layout);
            TEST_EXPECT(runner, cached_text(fixture.state.calibration_dpi_layout) == L"25.40 DPI");

            shared_data.accumulated_dx = 100.01;
            TEST_EXPECT(runner, SUCCEEDED(ui::detail::update_calibration_dpi_layout(fixture.state, shared_data, 400.0f, 100.0f)));
            TEST_EXPECT(runner, fixture.state.calibration_dpi_layout.layout.Get() == value_layout);

            shared_data.accumulated_dx = 101.0;
            TEST_EXPECT(runner, SUCCEEDED(ui::detail::update_calibration_dpi_layout(fixture.state, shared_data, 400.0f, 100.0f)));
            TEST_EXPECT(runner, fixture.state.calibration_dpi_layout.layout.Get() != value_layout);
            TEST_EXPECT(runner, cached_text(fixture.state.calibration_dpi_layout) == L"25.65 DPI");
        });

        runner.run("calibration layout invalidates on size and preserves failures", [&] {
            DWriteFixture fixture;
            ui::detail::SharedDataSnapshot shared_data{};
            shared_data.accumulated_dx = 100.0;
            TEST_EXPECT(runner, SUCCEEDED(ui::detail::update_calibration_dpi_layout(fixture.state, shared_data, 300.0f, 90.0f)));

            IDWriteTextLayout* initial_layout = fixture.state.calibration_dpi_layout.layout.Get();
            TEST_EXPECT(runner, SUCCEEDED(ui::detail::update_calibration_dpi_layout(fixture.state, shared_data, 301.0f, 90.0f)));
            TEST_EXPECT(runner, fixture.state.calibration_dpi_layout.layout.Get() != initial_layout);
            IDWriteTextLayout* resized_layout = fixture.state.calibration_dpi_layout.layout.Get();

            TEST_EXPECT(runner, SUCCEEDED(ui::detail::update_calibration_dpi_layout(fixture.state, shared_data, 301.0f, 91.0f)));
            TEST_EXPECT(runner, fixture.state.calibration_dpi_layout.layout.Get() != resized_layout);
            IDWriteTextLayout* original_layout = fixture.state.calibration_dpi_layout.layout.Get();
            const auto original_text = fixture.state.calibration_dpi_layout.display_text;
            const UINT32 original_length = fixture.state.calibration_dpi_layout.display_text_length;
            const float original_width = fixture.state.calibration_dpi_layout.width;
            const float original_height = fixture.state.calibration_dpi_layout.height;

            fixture.state.value_format.Reset();
            shared_data.accumulated_dx = 200.0;
            TEST_EXPECT(runner, FAILED(ui::detail::update_calibration_dpi_layout(fixture.state, shared_data, 320.0f, 100.0f)));
            TEST_EXPECT(runner, fixture.state.calibration_dpi_layout.layout.Get() == original_layout);
            TEST_EXPECT(runner, fixture.state.calibration_dpi_layout.display_text == original_text);
            TEST_EXPECT(runner, fixture.state.calibration_dpi_layout.display_text_length == original_length);
            TEST_EXPECT(runner, fixture.state.calibration_dpi_layout.width == original_width);
            TEST_EXPECT(runner, fixture.state.calibration_dpi_layout.height == original_height);
        });

        runner.run("calibration layout retains DPI unit styling", [&] {
            DWriteFixture fixture;
            ui::detail::SharedDataSnapshot shared_data{};
            shared_data.accumulated_dx = 100.0;
            TEST_EXPECT(runner, SUCCEEDED(ui::detail::update_calibration_dpi_layout(fixture.state, shared_data, 400.0f, 100.0f)));

            const auto& cache = fixture.state.calibration_dpi_layout;
            const wchar_t* separator = std::wcschr(cache.display_text.data(), L' ');
            TEST_EXPECT(runner, separator != nullptr);
            const UINT32 unit_start = static_cast<UINT32>(separator - cache.display_text.data()) + 1;
            float value_size = 0.0f;
            float unit_size = 0.0f;
            DWRITE_FONT_WEIGHT unit_weight = DWRITE_FONT_WEIGHT_NORMAL;
            TEST_EXPECT(runner, SUCCEEDED(cache.layout->GetFontSize(0, &value_size)));
            TEST_EXPECT(runner, SUCCEEDED(cache.layout->GetFontSize(unit_start, &unit_size)));
            TEST_EXPECT(runner, SUCCEEDED(cache.layout->GetFontWeight(unit_start, &unit_weight)));
            TEST_EXPECT_NEAR(runner, value_size, 48.0f, 0.001f);
            TEST_EXPECT_NEAR(runner, unit_size, 18.0f, 0.001f);
            TEST_EXPECT(runner, unit_weight == DWRITE_FONT_WEIGHT_SEMI_BOLD);
        });

        runner.run("calibration layout shrinks to fit constrained height", [&] {
            DWriteFixture fixture;
            ui::detail::SharedDataSnapshot shared_data{};
            shared_data.accumulated_dx = 3196.062992125984;
            TEST_EXPECT(runner, SUCCEEDED(ui::detail::update_calibration_dpi_layout(fixture.state, shared_data, 400.0f, 30.0f)));

            const auto& cache = fixture.state.calibration_dpi_layout;
            const wchar_t* separator = std::wcschr(cache.display_text.data(), L' ');
            TEST_EXPECT(runner, separator != nullptr);
            const UINT32 unit_start = static_cast<UINT32>(separator - cache.display_text.data()) + 1;
            float value_size = 0.0f;
            float unit_size = 0.0f;
            DWRITE_TEXT_METRICS metrics{};
            TEST_EXPECT(runner, SUCCEEDED(cache.layout->GetFontSize(0, &value_size)));
            TEST_EXPECT(runner, SUCCEEDED(cache.layout->GetFontSize(unit_start, &unit_size)));
            TEST_EXPECT(runner, SUCCEEDED(cache.layout->GetMetrics(&metrics)));
            TEST_EXPECT(runner, value_size >= 20.0f);
            TEST_EXPECT(runner, value_size < 48.0f);
            TEST_EXPECT(runner, unit_size >= 9.0f);
            TEST_EXPECT(runner, unit_size < 18.0f);
            TEST_EXPECT(runner, metrics.height <= 28.0f);
        });
    }

} // namespace automatic_test
