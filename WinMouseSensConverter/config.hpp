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

    struct UserConfig {
        int reference_dpi = 800;
        OutputUnit unit = OutputUnit::inch;
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

        constexpr std::optional<int> parse_reference_dpi(std::string_view text) noexcept {
            if (text.empty() || text.size() > 6) return std::nullopt;

            int value = 0;
            for (const char character : text) {
                if (character < '0' || character > '9') return std::nullopt;
                value = value * 10 + static_cast<int>(character - '0');
            }

            if (value < 1 || value > 999999) return std::nullopt;
            return value;
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

        constexpr std::optional<UserConfig> parse_configuration(std::string_view text) noexcept {
            constexpr std::string_view utf8_bom = "\xEF\xBB\xBF";
            if (text.starts_with(utf8_bom)) text.remove_prefix(utf8_bom.size());

            UserConfig parsed{};
            bool found_reference_dpi = false;
            bool found_unit = false;
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
                    }
                }

                if (newline == std::string_view::npos) break;
                position = newline + 1;
            }

            if (!found_reference_dpi || !found_unit) return std::nullopt;
            return parsed;
        }

        constexpr bool parses_as(std::string_view text, int reference_dpi, OutputUnit unit) noexcept {
            const std::optional<UserConfig> parsed = parse_configuration(text);
            return parsed.has_value() && parsed->reference_dpi == reference_dpi && parsed->unit == unit;
        }

        static_assert(parses_as("reference_dpi=800\nunit=inch", 800, OutputUnit::inch));
        static_assert(parses_as("\n \t\n reference_dpi = 1200 \n\n unit = cm \n", 1200, OutputUnit::cm));
        static_assert(parses_as("unit = mm\r\n\r\nreference_dpi = 400\r\n", 400, OutputUnit::mm));
        static_assert(parses_as("unknown = retained\nunit=raw\nreference_dpi=1", 1, OutputUnit::raw));
        static_assert(parses_as("\xEF\xBB\xBFreference_dpi=3200\nunit=dm", 3200, OutputUnit::dm));
        static_assert(parses_as("reference_dpi=999999\nunit=m", 999999, OutputUnit::m));
        static_assert(!parse_configuration("").has_value());
        static_assert(!parse_configuration("reference_dpi=800").has_value());
        static_assert(!parse_configuration("unit=inch").has_value());
        static_assert(!parse_configuration("reference_dpi=0\nunit=inch").has_value());
        static_assert(!parse_configuration("reference_dpi=1000000\nunit=inch").has_value());
        static_assert(!parse_configuration("reference_dpi=8 00\nunit=inch").has_value());
        static_assert(!parse_configuration("reference_dpi=800\nunit=INCH").has_value());
        static_assert(!parse_configuration("reference_dpi=800\nreference_dpi=400\nunit=inch").has_value());
        static_assert(!parse_configuration("reference_dpi=800\nunit=inch\nunit=cm").has_value());
        static_assert(!parse_configuration("broken line\nreference_dpi=800\nunit=inch").has_value());

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
            return user_config.reference_dpi >= 1 && user_config.reference_dpi <= 999999 && !unit_name(user_config.unit).empty();
        }

        inline std::optional<std::string> serialize(const UserConfig& user_config) noexcept {
            if (!valid(user_config)) return std::nullopt;

            try {
                std::string contents = "reference_dpi = ";
                contents += std::to_string(user_config.reference_dpi);
                contents += "\r\nunit = ";
                contents += unit_name(user_config.unit);
                contents += "\r\n";
                return contents;
            } catch (...) {
                return std::nullopt;
            }
        }

    } // namespace detail

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
