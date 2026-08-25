#ifndef FPS_HPP
#define FPS_HPP

#pragma once

#include <chrono>
#include <thread>

namespace fps {

    template <double Fps>
    concept ValidFps = Fps >= 1e-2 && Fps <= 1e9;

    template <double MaxFps> requires ValidFps<MaxFps>
    class FpsLimiter {
    private:
        using Clock = std::chrono::steady_clock;
        using Seconds = std::chrono::duration<double>;
        using TimePoint = std::chrono::time_point<Clock, Seconds>;

        static constexpr Seconds frame{ 1.0 / MaxFps };

        TimePoint next_{ TimePoint{Clock::now()} + frame };

    public:
        void tick() noexcept {
            std::this_thread::sleep_until(std::chrono::ceil<Clock::duration>(next_));

            next_ += frame;

            const auto now = TimePoint{ Clock::now() };
            if (next_ < now) {
                next_ = now + frame;
            }
        }
    };

}

#endif