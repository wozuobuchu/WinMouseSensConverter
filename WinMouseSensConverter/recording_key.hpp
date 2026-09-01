#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>

#include <cstdint>

namespace main_loop {

    constexpr bool matches_recording_key(uint16_t configured_key, uint16_t event_key) noexcept {
        switch (configured_key) {
            case VK_SHIFT:
                return event_key == VK_LSHIFT || event_key == VK_RSHIFT;
            case VK_CONTROL:
                return event_key == VK_LCONTROL || event_key == VK_RCONTROL;
            case VK_MENU:
                return event_key == VK_LMENU || event_key == VK_RMENU;
            default:
                return configured_key == event_key;
        }
    }

} // namespace main_loop
