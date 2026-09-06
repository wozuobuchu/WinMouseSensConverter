#pragma once

#ifndef CURSOR_POS_HPP_
#define CURSOR_POS_HPP_

#include <Windows.h>
#include <d2d1.h>

#include <cmath>
#include <optional>

namespace cursor_pos {

    inline std::optional<POINT> get_win32(HWND hwnd) noexcept {
        if (hwnd == nullptr) return std::nullopt;

        POINT position{};
        if (!GetCursorPos(&position) || !ScreenToClient(hwnd, &position)) {
            return std::nullopt;
        }

        return position;
    }

    inline std::optional<D2D1_POINT_2F> get_d2d(HWND hwnd, ID2D1RenderTarget* render_target) noexcept {
        constexpr auto pixels_to_dips = [](const POINT& position, const FLOAT dpi_x, const FLOAT dpi_y) noexcept -> std::optional<D2D1_POINT_2F> {
            if (!std::isfinite(dpi_x) || !std::isfinite(dpi_y) || dpi_x <= 0.0f || dpi_y <= 0.0f) {
                return std::nullopt;
            }

            constexpr FLOAT default_dpi = static_cast<FLOAT>(USER_DEFAULT_SCREEN_DPI);
            return D2D1_POINT_2F{
                static_cast<FLOAT>(position.x) * default_dpi / dpi_x,
                static_cast<FLOAT>(position.y) * default_dpi / dpi_y,
            };
        };

        if (hwnd == nullptr || render_target == nullptr) return std::nullopt;

        const std::optional<POINT> position = get_win32(hwnd);
        if (!position) return std::nullopt;

        FLOAT dpi_x = 0.0f;
        FLOAT dpi_y = 0.0f;
        render_target->GetDpi(&dpi_x, &dpi_y);

        return pixels_to_dips(*position, dpi_x, dpi_y);
    }

    constexpr bool is_in_rect(const D2D1_POINT_2F& position, const D2D1_RECT_F& rect) noexcept {
        return position.x >= rect.left && position.x < rect.right
            && position.y >= rect.top && position.y < rect.bottom;
    }

    inline bool is_cursor_in_rect(HWND hwnd, ID2D1RenderTarget* render_target, const D2D1_RECT_F& rect) noexcept {
        const std::optional<D2D1_POINT_2F> position = get_d2d(hwnd, render_target);
        return position && is_in_rect(*position, rect);
    }

} // namespace cursor_pos

#endif // CURSOR_POS_HPP_
