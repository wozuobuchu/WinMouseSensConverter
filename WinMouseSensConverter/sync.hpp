#ifndef SYNC_HPP_
#define SYNC_HPP_

#include <cstdint>

#include "config.hpp"

namespace public_data {
    inline uint8_t on_recording_ = 0;
    inline config::AppMode current_mode_ = config::AppMode::measurement;

    inline double accumulated_muzmov_dx = 0.0;
    inline double accumulated_muzmov_dy = 0.0;
}

#endif // !_SYNC_HPP
