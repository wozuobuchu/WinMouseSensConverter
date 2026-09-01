#include "test_groups.hpp"

#include "config.hpp"

#include <array>
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
            TEST_EXPECT(runner, config::detail::parse_reference_dpi("1") == 1);
            TEST_EXPECT(runner, config::detail::parse_reference_dpi("999999") == 999999);
            TEST_EXPECT(runner, !config::detail::parse_reference_dpi("0").has_value());
            TEST_EXPECT(runner, !config::detail::parse_reference_dpi("1000000").has_value());
            TEST_EXPECT(runner, !config::detail::parse_reference_dpi("+800").has_value());

            TEST_EXPECT(runner, config::detail::parse_calibration_distance_cm("10") == 10);
            TEST_EXPECT(runner, config::detail::parse_calibration_distance_cm("1000") == 1000);
            TEST_EXPECT(runner, !config::detail::parse_calibration_distance_cm("9").has_value());
            TEST_EXPECT(runner, !config::detail::parse_calibration_distance_cm("1001").has_value());

            TEST_EXPECT(runner, config::detail::parse_recording_key("1") == 1);
            TEST_EXPECT(runner, config::detail::parse_recording_key("254") == 254);
            TEST_EXPECT(runner, config::detail::parse_recording_key("0x70") == VK_F1);
            TEST_EXPECT(runner, config::detail::parse_recording_key("0XfE") == 254);
            TEST_EXPECT(runner, !config::detail::parse_recording_key("0").has_value());
            TEST_EXPECT(runner, !config::detail::parse_recording_key("255").has_value());
            TEST_EXPECT(runner, !config::detail::parse_recording_key("0x").has_value());
            TEST_EXPECT(runner, !config::detail::parse_recording_key("0xGG").has_value());
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
                "calibration_distance_cm = 50\n"
                "unit = mm\n"
                "reference_dpi = 1600\n"
                "unknown_after = ignored\n";
            const auto parsed = config::detail::parse_configuration(with_bom_and_unknown);
            TEST_EXPECT(runner, parsed.has_value());
            TEST_EXPECT(runner, parsed->reference_dpi == 1600);
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
        });
    }

} // namespace automatic_test
