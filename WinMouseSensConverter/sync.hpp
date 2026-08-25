#ifndef SYNC_HPP_
#define SYNC_HPP_

#include <cstdint>
#include <stop_token>

#include "SYS/low_latency_keyboard.hpp"
#include "SYS/low_latency_mousemov.hpp"

namespace sync {
    inline std::stop_source sts_;
}

namespace public_data {
    inline uint8_t on_recording_ = 0;

    inline double accumulated_muzmov_dx = 0.0;
    inline double accumulated_muzmov_dy = 0.0;
}

#endif // !_SYNC_HPP
