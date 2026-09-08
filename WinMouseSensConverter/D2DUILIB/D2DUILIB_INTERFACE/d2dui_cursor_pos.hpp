#pragma once

#include <d2d1.h>

namespace cursor_pos {

    constexpr bool is_in_rect(const D2D1_POINT_2F& position, const D2D1_RECT_F& rect) noexcept {
        return position.x >= rect.left && position.x < rect.right
            && position.y >= rect.top && position.y < rect.bottom;
    }

} // namespace cursor_pos
