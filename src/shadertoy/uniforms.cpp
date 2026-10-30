#include "shadertoy/uniforms.h"

#include <algorithm>

namespace sp {

void computeDate(std::time_t now, double fractionalSeconds, float out[4]) {
    std::tm local{};
    localtime_r(&now, &local);
    out[0] = static_cast<float>(local.tm_year + 1900);
    out[1] = static_cast<float>(local.tm_mon);  // 0-based, like JavaScript's getMonth()
    out[2] = static_cast<float>(local.tm_mday);
    out[3] = static_cast<float>(local.tm_hour * 3600 + local.tm_min * 60 + local.tm_sec + fractionalSeconds);
}

void PlaybackClock::advance(double realDelta) {
    // Clamp: after a long stall (compile, window drag) keep animation smooth
    // instead of jumping ahead. ShaderToy behaves similarly.
    const double dt = std::clamp(realDelta, 0.0, 0.25);
    time_ += dt;
    delta_ = static_cast<float>(dt);
    if (dt > 0) {
        const float instant = static_cast<float>(1.0 / dt);
        frameRate_ = frameRate_ * 0.9f + instant * 0.1f;  // smoothed, like a FPS counter
    }
}

}  // namespace sp
