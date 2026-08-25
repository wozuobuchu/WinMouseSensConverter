#pragma once

#include "Resource.hpp"

#include <Windows.h>

#include <cstdint>
#include <utility>

#include "SYS/aop.hpp"
#include "SYS/fps.hpp"
#include "SYS/low_latency_keyboard.hpp"
#include "SYS/low_latency_mousemov.hpp"

#include "sync.hpp"

#include "ui.hpp"

namespace main_loop {

    inline static bool pull_msg_kbd() noexcept {
        static constexpr size_t kque_size = 1024;
        static rawinput::LowLatencyKeyboard::KeyEvent kque[kque_size];
        const size_t n = rawinput::LowLatencyKeyboard::pop_events<kque_size>(kque);
        bool changed = false;

        for (size_t i = 0; i < n; ++i) {
            const rawinput::LowLatencyKeyboard::KeyEvent& event = kque[i];
            if (event.down == 0 || event.vkey != VK_F1) continue;

            public_data::on_recording_ = public_data::on_recording_ == 0 ? static_cast<uint8_t>(~0) : 0;
            if (public_data::on_recording_ != 0) {
                public_data::accumulated_muzmov_dx = 0.0;
                public_data::accumulated_muzmov_dy = 0.0;
            }
            changed = true;
        }

        return changed;
    }

    inline static bool pull_msg_mouse() noexcept {
        const auto [dx, dy] = rawinput::LowLatencyMouseMov::sample();
        if (public_data::on_recording_ != 0) {
            public_data::accumulated_muzmov_dx += static_cast<double>(dx);
            public_data::accumulated_muzmov_dy += static_cast<double>(dy);
            return dx != 0 || dy != 0;
        }
        return false;
    }

} // namespace main_loop
