#pragma once

#include "Resource.hpp"

#include <Windows.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <unordered_map>
#include <utility>

#include "SYS/aop.hpp"
#include "SYS/fps.hpp"
#include "SYS/low_latency_keyboard.hpp"
#include "SYS/low_latency_mousemov.hpp"

#include "sync.hpp"

#include "ui.hpp"

namespace main_loop {

    inline static void pull_msg_kbd() {
        // Keyboard shortcut function
        static std::unordered_map<uint16_t, std::function<void()>> kbd_shortcut_func_table = {
            // Start/Stop record
            {static_cast<uint16_t>(VK_F1), []() noexcept -> void { public_data::on_recording_ = ~public_data::on_recording_; }},
        };

        // Pull keyboard shortcut event
        static constexpr size_t kque_size = 1024;
        static rawinput::LowLatencyKeyboard::KeyEvent kque[kque_size];
        size_t n = rawinput::LowLatencyKeyboard::pop_events<kque_size>(kque);
        for (size_t i = 0; i < n; ++i) {
            rawinput::LowLatencyKeyboard::KeyEvent ev = kque[i];
            auto it = kbd_shortcut_func_table.find(ev.vkey);
            if (it != kbd_shortcut_func_table.end()) {
                // Run shortcut function
                it->second();
            }
        }
    }

    inline static void pull_msg_mouse() {
        // Cache last bool state
        static uint8_t cached_on_recording_ = 0;

        // Pull mouse movement delta xy
        auto [dx, dy] = rawinput::LowLatencyMouseMov::sample();

        // Handle recording event
        static constexpr uint8_t state_off = static_cast<uint8_t>(0);
        static constexpr uint8_t state_on = static_cast<uint8_t>(~0);
        uint8_t now_on_recording_ = public_data::on_recording_;
        if (now_on_recording_ == state_on) {
            if (now_on_recording_ != cached_on_recording_) {
                public_data::accumulated_muzmov_dx = 0.0;
                public_data::accumulated_muzmov_dy = 0.0;
            }
            public_data::accumulated_muzmov_dx += static_cast<double>(dx);
            public_data::accumulated_muzmov_dy += static_cast<double>(dy);
        }

        // Update cache
        cached_on_recording_ = now_on_recording_;
    }

} // namespace main_loop
