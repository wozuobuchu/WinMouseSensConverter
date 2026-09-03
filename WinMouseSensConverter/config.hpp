#pragma once

#ifndef CONFIG_HPP_
#define CONFIG_HPP_

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>
#include <ShlObj.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>

namespace config {

    enum class OutputUnit : uint8_t {
        raw,
        inch,
        mm,
        cm,
        dm,
        m,
    };

    enum class AppMode : uint8_t {
        measurement,
        calibration,
    };

    struct UserConfig {
        int reference_dpi = 800;
        OutputUnit unit = OutputUnit::cm;
        int calibration_distance_cm = 10;
        AppMode mode = AppMode::measurement;
        uint16_t recording_key = VK_F2;
    };

    namespace detail {

        constexpr size_t kMaximumConfigSize = 64 * 1024;
        constexpr std::wstring_view kApplicationDirectoryName = L"WinMouseSensConverter";
        constexpr std::wstring_view kConfigFileName = L"config.ini";

        constexpr bool is_horizontal_whitespace(char character) noexcept {
            return character == ' ' || character == '\t';
        }

        constexpr std::string_view trim(std::string_view text) noexcept {
            while (!text.empty() && is_horizontal_whitespace(text.front())) text.remove_prefix(1);
            while (!text.empty() && is_horizontal_whitespace(text.back())) text.remove_suffix(1);
            return text;
        }

        template <typename Character>
        constexpr std::optional<unsigned int> parse_unsigned_integer(std::basic_string_view<Character> text, unsigned int base, unsigned int maximum) noexcept {
            if (text.empty() || (base != 10 && base != 16)) return std::nullopt;

            unsigned int value = 0;
            for (const Character character : text) {
                unsigned int digit = 0;
                if (character >= static_cast<Character>('0') && character <= static_cast<Character>('9')) {
                    digit = static_cast<unsigned int>(character - static_cast<Character>('0'));
                } else if (base == 16 && character >= static_cast<Character>('a') && character <= static_cast<Character>('f')) {
                    digit = static_cast<unsigned int>(character - static_cast<Character>('a')) + 10;
                } else if (base == 16 && character >= static_cast<Character>('A') && character <= static_cast<Character>('F')) {
                    digit = static_cast<unsigned int>(character - static_cast<Character>('A')) + 10;
                } else {
                    return std::nullopt;
                }

                if (digit >= base || digit > maximum || value > (maximum - digit) / base) return std::nullopt;
                value = value * base + digit;
            }
            return value;
        }

        template <typename Character>
        constexpr std::optional<int> parse_reference_dpi_impl(std::basic_string_view<Character> text) noexcept {
            if (text.empty() || text.size() > 6) return std::nullopt;
            const std::optional<unsigned int> value = parse_unsigned_integer(text, 10, 999999);
            if (!value.has_value() || *value < 1) return std::nullopt;
            return static_cast<int>(*value);
        }

        constexpr std::optional<int> parse_reference_dpi(std::string_view text) noexcept {
            return parse_reference_dpi_impl(text);
        }

        constexpr std::optional<int> parse_reference_dpi(std::wstring_view text) noexcept {
            return parse_reference_dpi_impl(text);
        }

        constexpr std::optional<OutputUnit> parse_unit(std::string_view text) noexcept {
            if (text == "raw") return OutputUnit::raw;
            if (text == "inch") return OutputUnit::inch;
            if (text == "mm") return OutputUnit::mm;
            if (text == "cm") return OutputUnit::cm;
            if (text == "dm") return OutputUnit::dm;
            if (text == "m") return OutputUnit::m;
            return std::nullopt;
        }

        template <typename Character>
        constexpr std::optional<int> parse_calibration_distance_cm_impl(std::basic_string_view<Character> text) noexcept {
            if (text.empty() || text.size() > 4) return std::nullopt;
            const std::optional<unsigned int> value = parse_unsigned_integer(text, 10, 1000);
            if (!value.has_value() || *value < 10) return std::nullopt;
            return static_cast<int>(*value);
        }

        constexpr std::optional<int> parse_calibration_distance_cm(std::string_view text) noexcept {
            return parse_calibration_distance_cm_impl(text);
        }

        constexpr std::optional<int> parse_calibration_distance_cm(std::wstring_view text) noexcept {
            return parse_calibration_distance_cm_impl(text);
        }

        constexpr std::optional<AppMode> parse_mode(std::string_view text) noexcept {
            if (text == "measurement") return AppMode::measurement;
            if (text == "calibration") return AppMode::calibration;
            return std::nullopt;
        }

        template <typename Character>
        constexpr std::optional<uint16_t> parse_recording_key_impl(std::basic_string_view<Character> text) noexcept {
            unsigned int base = 10;
            constexpr Character lower_prefix[]{static_cast<Character>('0'), static_cast<Character>('x')};
            constexpr Character upper_prefix[]{static_cast<Character>('0'), static_cast<Character>('X')};
            if (text.starts_with(std::basic_string_view<Character>(lower_prefix, 2)) || text.starts_with(std::basic_string_view<Character>(upper_prefix, 2))) {
                base = 16;
                text.remove_prefix(2);
            }
            const std::optional<unsigned int> value = parse_unsigned_integer(text, base, 254);
            if (!value.has_value() || *value == 0) return std::nullopt;
            return static_cast<uint16_t>(*value);
        }

        constexpr std::optional<uint16_t> parse_recording_key(std::string_view text) noexcept {
            return parse_recording_key_impl(text);
        }

        constexpr std::optional<uint16_t> parse_recording_key(std::wstring_view text) noexcept {
            return parse_recording_key_impl(text);
        }

        constexpr std::string_view unit_name(OutputUnit unit) noexcept {
            switch (unit) {
                case OutputUnit::raw:
                    return "raw";
                case OutputUnit::inch:
                    return "inch";
                case OutputUnit::mm:
                    return "mm";
                case OutputUnit::cm:
                    return "cm";
                case OutputUnit::dm:
                    return "dm";
                case OutputUnit::m:
                    return "m";
            }
            return {};
        }

        constexpr std::string_view mode_name(AppMode mode) noexcept {
            switch (mode) {
                case AppMode::measurement:
                    return "measurement";
                case AppMode::calibration:
                    return "calibration";
            }
            return {};
        }

        constexpr std::optional<UserConfig> parse_configuration(std::string_view text) noexcept {
            constexpr std::string_view utf8_bom = "\xEF\xBB\xBF";
            if (text.starts_with(utf8_bom)) text.remove_prefix(utf8_bom.size());

            UserConfig parsed{};
            bool found_reference_dpi = false;
            bool found_unit = false;
            bool found_calibration_distance_cm = false;
            bool found_mode = false;
            bool found_recording_key = false;
            size_t position = 0;

            while (position <= text.size()) {
                const size_t newline = text.find('\n', position);
                const size_t line_end = newline == std::string_view::npos ? text.size() : newline;
                std::string_view line = text.substr(position, line_end - position);
                if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
                line = trim(line);

                if (!line.empty()) {
                    const size_t separator = line.find('=');
                    if (separator == std::string_view::npos) return std::nullopt;

                    const std::string_view key = trim(line.substr(0, separator));
                    const std::string_view value = trim(line.substr(separator + 1));
                    if (key.empty()) return std::nullopt;

                    if (key == "reference_dpi") {
                        if (found_reference_dpi) return std::nullopt;
                        const std::optional<int> dpi = parse_reference_dpi(value);
                        if (!dpi.has_value()) return std::nullopt;
                        parsed.reference_dpi = *dpi;
                        found_reference_dpi = true;
                    } else if (key == "unit") {
                        if (found_unit) return std::nullopt;
                        const std::optional<OutputUnit> unit = parse_unit(value);
                        if (!unit.has_value()) return std::nullopt;
                        parsed.unit = *unit;
                        found_unit = true;
                    } else if (key == "calibration_distance_cm") {
                        if (found_calibration_distance_cm) return std::nullopt;
                        const std::optional<int> distance = parse_calibration_distance_cm(value);
                        if (!distance.has_value()) return std::nullopt;
                        parsed.calibration_distance_cm = *distance;
                        found_calibration_distance_cm = true;
                    } else if (key == "mode") {
                        if (found_mode) return std::nullopt;
                        const std::optional<AppMode> mode = parse_mode(value);
                        if (!mode.has_value()) return std::nullopt;
                        parsed.mode = *mode;
                        found_mode = true;
                    } else if (key == "recording_key") {
                        if (found_recording_key) return std::nullopt;
                        const std::optional<uint16_t> recording_key = parse_recording_key(value);
                        if (!recording_key.has_value()) return std::nullopt;
                        parsed.recording_key = *recording_key;
                        found_recording_key = true;
                    }
                }

                if (newline == std::string_view::npos) break;
                position = newline + 1;
            }

            if (!found_reference_dpi || !found_unit || !found_calibration_distance_cm || !found_mode || !found_recording_key) return std::nullopt;
            return parsed;
        }

        constexpr bool parses_as(std::string_view text, int reference_dpi, OutputUnit unit, int calibration_distance_cm, AppMode mode, uint16_t recording_key) noexcept {
            const std::optional<UserConfig> parsed = parse_configuration(text);
            return parsed.has_value() && parsed->reference_dpi == reference_dpi && parsed->unit == unit && parsed->calibration_distance_cm == calibration_distance_cm && parsed->mode == mode && parsed->recording_key == recording_key;
        }

        inline std::optional<std::filesystem::path> config_directory() noexcept {
            PWSTR local_app_data = nullptr;
            if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_DEFAULT, nullptr, &local_app_data))) {
                return std::nullopt;
            }

            try {
                std::filesystem::path directory(local_app_data);
                CoTaskMemFree(local_app_data);
                directory /= kApplicationDirectoryName;
                return directory;
            } catch (...) {
                CoTaskMemFree(local_app_data);
                return std::nullopt;
            }
        }

        inline std::optional<std::filesystem::path> config_path() noexcept {
            try {
                std::optional<std::filesystem::path> directory = config_directory();
                if (!directory.has_value()) return std::nullopt;
                *directory /= kConfigFileName;
                return directory;
            } catch (...) {
                return std::nullopt;
            }
        }

        inline std::optional<std::string> read_file(const std::filesystem::path& path) noexcept {
            try {
                std::ifstream file(path, std::ios::binary | std::ios::ate);
                if (!file.is_open()) return std::nullopt;

                const std::streampos end = file.tellg();
                if (end < 0 || static_cast<uintmax_t>(end) > kMaximumConfigSize) return std::nullopt;

                std::string contents(static_cast<size_t>(end), '\0');
                file.seekg(0, std::ios::beg);
                if (!contents.empty()) {
                    file.read(contents.data(), static_cast<std::streamsize>(contents.size()));
                    if (file.gcount() != static_cast<std::streamsize>(contents.size())) return std::nullopt;
                }
                return contents;
            } catch (...) {
                return std::nullopt;
            }
        }

        inline bool valid(const UserConfig& user_config) noexcept {
            return user_config.reference_dpi >= 1 && user_config.reference_dpi <= 999999 && !unit_name(user_config.unit).empty() && user_config.calibration_distance_cm >= 10 && user_config.calibration_distance_cm <= 1000 && !mode_name(user_config.mode).empty() && user_config.recording_key >= 1 && user_config.recording_key <= 254;
        }

        inline std::optional<std::string> serialize(const UserConfig& user_config) noexcept {
            if (!valid(user_config)) return std::nullopt;

            try {
                std::string contents = "reference_dpi = ";
                contents += std::to_string(user_config.reference_dpi);
                contents += "\r\nunit = ";
                contents += unit_name(user_config.unit);
                contents += "\r\ncalibration_distance_cm = ";
                contents += std::to_string(user_config.calibration_distance_cm);
                contents += "\r\nmode = ";
                contents += mode_name(user_config.mode);
                constexpr char hex_digits[] = "0123456789ABCDEF";
                contents += "\r\nrecording_key = 0x";
                contents += hex_digits[(user_config.recording_key >> 4) & 0x0F];
                contents += hex_digits[user_config.recording_key & 0x0F];
                contents += "\r\n";
                return contents;
            } catch (...) {
                return std::nullopt;
            }
        }

    } // namespace detail

    inline std::optional<std::filesystem::path> configuration_directory() noexcept {
        return detail::config_directory();
    }

    inline bool save(const UserConfig& user_config) noexcept {
        try {
            const std::optional<std::filesystem::path> directory = detail::config_directory();
            const std::optional<std::string> contents = detail::serialize(user_config);
            if (!directory.has_value() || !contents.has_value()) return false;

            std::error_code error;
            std::filesystem::create_directories(*directory, error);
            if (error) return false;

            std::filesystem::path target = *directory / detail::kConfigFileName;
            std::filesystem::path temporary = target;
            temporary += L"." + std::to_wstring(GetCurrentProcessId()) + L".tmp";

            {
                std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
                if (!file.is_open()) return false;
                file.write(contents->data(), static_cast<std::streamsize>(contents->size()));
                file.flush();
                if (!file) {
                    file.close();
                    (void)DeleteFileW(temporary.c_str());
                    return false;
                }
                file.close();
                if (file.fail()) {
                    (void)DeleteFileW(temporary.c_str());
                    return false;
                }
            }

            if (!MoveFileExW(temporary.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
                (void)DeleteFileW(temporary.c_str());
                return false;
            }
            return true;
        } catch (...) {
            return false;
        }
    }

    inline UserConfig load_or_create() noexcept {
        const UserConfig defaults{};
        const std::optional<std::filesystem::path> path = detail::config_path();
        if (path.has_value()) {
            const std::optional<std::string> contents = detail::read_file(*path);
            if (contents.has_value()) {
                const std::optional<UserConfig> parsed = detail::parse_configuration(*contents);
                if (parsed.has_value()) return *parsed;
            }
        }

        (void)save(defaults);
        return defaults;
    }

} // namespace config

#endif // CONFIG_HPP_
