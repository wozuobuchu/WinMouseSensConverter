#ifndef SYNC_HPP_
#define SYNC_HPP_

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>

#include <cstdint>

#include "config.hpp"

namespace app_data {
    inline uint8_t on_recording_ = 0;
    inline config::AppMode current_mode_ = config::AppMode::measurement;

    inline double accumulated_muzmov_dx = 0.0;
    inline double accumulated_muzmov_dy = 0.0;
}

namespace app_func {
    inline bool toggle_recording(bool play_notification = true) noexcept {
        app_data::on_recording_ = app_data::on_recording_ == 0 ? static_cast<uint8_t>(~0) : uint8_t{0};
        const bool recording = app_data::on_recording_ != 0;

        if (recording) {
            app_data::accumulated_muzmov_dx = 0.0;
            app_data::accumulated_muzmov_dy = 0.0;
        }

        if (play_notification) {
            const UINT notification_sound = recording ? MB_ICONASTERISK : MB_ICONHAND;
            (void)MessageBeep(notification_sound);
        }

        return recording;
    }
}

#endif // !_SYNC_HPP
