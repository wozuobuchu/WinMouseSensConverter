#include "test_groups.hpp"

#include "config.hpp"

#include <array>
#include <cmath>
#include <limits>
#include <string>
#include <string_view>

namespace automatic_test {

    namespace {

        constexpr std::string_view kValidConfiguration =
            "reference_dpi = 800\r\n"
            "unit = cm\r\n"
            "calibration_distance_cm = 10\r\n"
            "mode = measurement\r\n"
            "recording_key = 0x70\r\n";

    } // namespace

    void add_config_tests(TestRunner& runner) {
        runner.run("config scalar parsers accept boundaries", [&] {
            TEST_EXPECT(runner, config::UserConfig{}.recording_key == VK_F2);
            TEST_EXPECT(runner, config::detail::parse_reference_dpi("1") == 1);
            TEST_EXPECT(runner, config::detail::parse_reference_dpi("800.25") == 800.25);
            TEST_EXPECT(runner, config::detail::parse_reference_dpi("8e2") == 800.0);
            TEST_EXPECT(runner, config::detail::parse_reference_dpi("8E+2") == 800.0);
            TEST_EXPECT(runner, config::detail::parse_reference_dpi("999999") == 999999);
            TEST_EXPECT(runner, !config::detail::parse_reference_dpi("0").has_value());
            TEST_EXPECT(runner, !config::detail::parse_reference_dpi("1000000").has_value());
            TEST_EXPECT(runner, !config::detail::parse_reference_dpi("+800").has_value());

            TEST_EXPECT(runner, config::detail::parse_calibration_distance_cm("10") == 10);
            TEST_EXPECT(runner, config::detail::parse_calibration_distance_cm("10.125") == 10.125);
            TEST_EXPECT(runner, config::detail::parse_calibration_distance_cm("1e1") == 10.0);
            TEST_EXPECT(runner, config::detail::parse_calibration_distance_cm("1E+3") == 1000.0);
            TEST_EXPECT(runner, config::detail::parse_calibration_distance_cm("1000") == 1000);
            TEST_EXPECT(runner, !config::detail::parse_calibration_distance_cm("9").has_value());
            TEST_EXPECT(runner, !config::detail::parse_calibration_distance_cm("1001").has_value());

            std::string maximum_length(23, '0');
            maximum_length += '1';
            TEST_EXPECT(runner, maximum_length.size() == config::detail::kMaximumFloatingPointTextLength);
            TEST_EXPECT(runner, config::detail::parse_reference_dpi(maximum_length) == 1.0);
            std::string over_maximum_length(24, '0');
            over_maximum_length += '1';
            TEST_EXPECT(runner, !config::detail::parse_reference_dpi(over_maximum_length).has_value());

            constexpr std::array<std::string_view, 12> invalid_floating_point_values{{
                "", "1,5", "1.5x", "1.2.3", "1e", "1e+", "nan", "NaN", "inf", "-inf", "1e9999", " 800",
            }};
            for (const std::string_view value : invalid_floating_point_values) {
                TEST_EXPECT(runner, !config::detail::parse_reference_dpi(value).has_value());
            }

            TEST_EXPECT(runner, config::detail::parse_recording_key("1") == 1);
            TEST_EXPECT(runner, config::detail::parse_recording_key("254") == 254);
            TEST_EXPECT(runner, config::detail::parse_recording_key("0x1") == 1);
            TEST_EXPECT(runner, config::detail::parse_recording_key("0x01") == 1);
            TEST_EXPECT(runner, config::detail::parse_recording_key("0x70") == VK_F1);
            TEST_EXPECT(runner, config::detail::parse_recording_key("0XfE") == 254);
            TEST_EXPECT(runner, !config::detail::parse_recording_key("0").has_value());
            TEST_EXPECT(runner, !config::detail::parse_recording_key("255").has_value());
            TEST_EXPECT(runner, !config::detail::parse_recording_key("99999999999999999999").has_value());
            TEST_EXPECT(runner, !config::detail::parse_recording_key("0x").has_value());
            TEST_EXPECT(runner, !config::detail::parse_recording_key("0xGG").has_value());

            TEST_EXPECT(runner, config::detail::parse_reference_dpi(L"999999") == config::detail::parse_reference_dpi("999999"));
            TEST_EXPECT(runner, config::detail::parse_reference_dpi(L"800.25") == config::detail::parse_reference_dpi("800.25"));
            TEST_EXPECT(runner, config::detail::parse_reference_dpi(L"8E+2") == config::detail::parse_reference_dpi("8E+2"));
            TEST_EXPECT(runner, config::detail::parse_calibration_distance_cm(L"1000") == config::detail::parse_calibration_distance_cm("1000"));
            TEST_EXPECT(runner, config::detail::parse_calibration_distance_cm(L"10.125") == config::detail::parse_calibration_distance_cm("10.125"));
            TEST_EXPECT(runner, config::detail::parse_recording_key(L"0XfE") == config::detail::parse_recording_key("0XfE"));
            TEST_EXPECT(runner, config::detail::parse_recording_key(L"0xGG") == config::detail::parse_recording_key("0xGG"));
            TEST_EXPECT(runner, config::detail::parse_reference_dpi(L"+800") == config::detail::parse_reference_dpi("+800"));
        });

        runner.run("config enum parsers cover every supported value", [&] {
            constexpr std::array<std::pair<std::string_view, config::OutputUnit>, 6> units{{
                {"raw", config::OutputUnit::raw},
                {"inch", config::OutputUnit::inch},
                {"mm", config::OutputUnit::mm},
                {"cm", config::OutputUnit::cm},
                {"dm", config::OutputUnit::dm},
                {"m", config::OutputUnit::m},
            }};
            for (const auto& [text, unit] : units) {
                TEST_EXPECT(runner, config::detail::parse_unit(text) == unit);
                TEST_EXPECT(runner, config::detail::unit_name(unit) == text);
            }
            TEST_EXPECT(runner, !config::detail::parse_unit("CM").has_value());
            TEST_EXPECT(runner, !config::detail::parse_unit("").has_value());

            TEST_EXPECT(runner, config::detail::parse_mode("measurement") == config::AppMode::measurement);
            TEST_EXPECT(runner, config::detail::parse_mode("calibration") == config::AppMode::calibration);
            TEST_EXPECT(runner, !config::detail::parse_mode("invalid").has_value());
        });

        runner.run("config accepts supported formatting and unknown fields", [&] {
            const std::string with_bom_and_unknown =
                "\xEF\xBB\xBF"
                "unknown_before = ignored\n"
                " mode = calibration \n"
                "recording_key=112\n"
                "calibration_distance_cm = 5e1\n"
                "unit = mm\n"
                "reference_dpi = 1600.25\n"
                "unknown_after = ignored\n";
            const auto parsed = config::detail::parse_configuration(with_bom_and_unknown);
            TEST_EXPECT(runner, parsed.has_value());
            TEST_EXPECT(runner, parsed->reference_dpi == 1600.25);
            TEST_EXPECT(runner, parsed->unit == config::OutputUnit::mm);
            TEST_EXPECT(runner, parsed->calibration_distance_cm == 50);
            TEST_EXPECT(runner, parsed->mode == config::AppMode::calibration);
            TEST_EXPECT(runner, parsed->recording_key == VK_F1);
            TEST_EXPECT(runner, config::detail::parse_configuration(kValidConfiguration).has_value());
        });

        runner.run("config rejects missing and duplicated required fields", [&] {
            constexpr std::array<std::string_view, 5> missing_fields{{
                "unit=cm\ncalibration_distance_cm=10\nmode=measurement\nrecording_key=0x70\n",
                "reference_dpi=800\ncalibration_distance_cm=10\nmode=measurement\nrecording_key=0x70\n",
                "reference_dpi=800\nunit=cm\nmode=measurement\nrecording_key=0x70\n",
                "reference_dpi=800\nunit=cm\ncalibration_distance_cm=10\nrecording_key=0x70\n",
                "reference_dpi=800\nunit=cm\ncalibration_distance_cm=10\nmode=measurement\n",
            }};
            for (const std::string_view text : missing_fields) {
                TEST_EXPECT(runner, !config::detail::parse_configuration(text).has_value());
            }

            constexpr std::array<std::string_view, 5> duplicates{{
                "reference_dpi=800\n",
                "unit=cm\n",
                "calibration_distance_cm=10\n",
                "mode=measurement\n",
                "recording_key=0x70\n",
            }};
            for (const std::string_view duplicate : duplicates) {
                std::string text(kValidConfiguration);
                text.append(duplicate);
                TEST_EXPECT(runner, !config::detail::parse_configuration(text).has_value());
            }
        });

        runner.run("config rejects invalid known fields and malformed lines", [&] {
            constexpr std::array<std::string_view, 7> invalid_configurations{{
                "reference_dpi=0\nunit=cm\ncalibration_distance_cm=10\nmode=measurement\nrecording_key=0x70\n",
                "reference_dpi=800\nunit=feet\ncalibration_distance_cm=10\nmode=measurement\nrecording_key=0x70\n",
                "reference_dpi=800\nunit=cm\ncalibration_distance_cm=9\nmode=measurement\nrecording_key=0x70\n",
                "reference_dpi=800\nunit=cm\ncalibration_distance_cm=10\nmode=other\nrecording_key=0x70\n",
                "reference_dpi=800\nunit=cm\ncalibration_distance_cm=10\nmode=measurement\nrecording_key=0\n",
                "reference_dpi=800\nunit=cm\ncalibration_distance_cm=10\nmode=measurement\nmalformed\nrecording_key=0x70\n",
                "reference_dpi=800\nunit=cm\ncalibration_distance_cm=10\nmode=measurement\n=empty_key\nrecording_key=0x70\n",
            }};
            for (const std::string_view text : invalid_configurations) {
                TEST_EXPECT(runner, !config::detail::parse_configuration(text).has_value());
            }
        });

        runner.run("config serialization is canonical and round trips", [&] {
            const config::UserConfig user_config{
                3200,
                config::OutputUnit::inch,
                1000,
                config::AppMode::calibration,
                VK_RCONTROL,
            };
            const auto serialized = config::detail::serialize(user_config);
            TEST_EXPECT(runner, serialized.has_value());
            TEST_EXPECT(runner, *serialized ==
                "reference_dpi = 3200\r\n"
                "unit = inch\r\n"
                "calibration_distance_cm = 1000\r\n"
                "mode = calibration\r\n"
                "recording_key = 0xA3\r\n");

            const auto reparsed = config::detail::parse_configuration(*serialized);
            TEST_EXPECT(runner, reparsed.has_value());
            TEST_EXPECT(runner, reparsed->reference_dpi == user_config.reference_dpi);
            TEST_EXPECT(runner, reparsed->unit == user_config.unit);
            TEST_EXPECT(runner, reparsed->calibration_distance_cm == user_config.calibration_distance_cm);
            TEST_EXPECT(runner, reparsed->mode == user_config.mode);
            TEST_EXPECT(runner, reparsed->recording_key == user_config.recording_key);

            config::UserConfig invalid = user_config;
            invalid.unit = static_cast<config::OutputUnit>(255);
            TEST_EXPECT(runner, !config::detail::serialize(invalid).has_value());

            config::UserConfig precise = user_config;
            precise.reference_dpi = std::nextafter(800.0, 999999.0);
            precise.calibration_distance_cm = 10.125;
            const auto precise_serialized = config::detail::serialize(precise);
            TEST_EXPECT(runner, precise_serialized.has_value());
            const auto precise_reparsed = precise_serialized.has_value() ? config::detail::parse_configuration(*precise_serialized) : std::nullopt;
            TEST_EXPECT(runner, precise_reparsed.has_value());
            TEST_EXPECT(runner, precise_reparsed->reference_dpi == precise.reference_dpi);
            TEST_EXPECT(runner, precise_reparsed->calibration_distance_cm == precise.calibration_distance_cm);

            const auto scientific = config::detail::parse_configuration(
                "reference_dpi=8e2\nunit=cm\ncalibration_distance_cm=1E+1\nmode=measurement\nrecording_key=0x71\n");
            TEST_EXPECT(runner, scientific.has_value());
            const auto normalized = scientific.has_value() ? config::detail::serialize(*scientific) : std::nullopt;
            TEST_EXPECT(runner, normalized.has_value());
            TEST_EXPECT(runner, normalized->find("reference_dpi = 800\r\n") != std::string::npos);
            TEST_EXPECT(runner, normalized->find("calibration_distance_cm = 10\r\n") != std::string::npos);

            invalid = user_config;
            invalid.reference_dpi = std::numeric_limits<double>::infinity();
            TEST_EXPECT(runner, !config::detail::serialize(invalid).has_value());
            invalid = user_config;
            invalid.calibration_distance_cm = std::numeric_limits<double>::quiet_NaN();
            TEST_EXPECT(runner, !config::detail::serialize(invalid).has_value());
        });

        runner.run("default recording key serializes and round trips as F2", [&] {
            const config::UserConfig defaults{};
            const auto serialized = config::detail::serialize(defaults);
            TEST_EXPECT(runner, serialized.has_value());
            TEST_EXPECT(runner, serialized->find("recording_key = 0x71\r\n") != std::string::npos);

            const auto reparsed = config::detail::parse_configuration(*serialized);
            TEST_EXPECT(runner, reparsed.has_value());
            TEST_EXPECT(runner, reparsed->recording_key == VK_F2);
        });
    }

} // namespace automatic_test
