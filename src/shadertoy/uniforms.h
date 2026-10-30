#pragma once
// Per-frame ShaderToy uniform values.

#include <ctime>

namespace sp {

struct FrameUniforms {
    float resolution[3] = {1, 1, 1};
    float time = 0;
    float timeDelta = 0;
    float frameRate = 60;
    int frame = 0;
    float mouse[4] = {0, 0, 0, 0};
    float date[4] = {0, 0, 0, 0};
    float sampleRate = 44100;
    float channelTime[4] = {0, 0, 0, 0};
};

// iDate as ShaderToy defines it: year, month (0-based), day (1-based), seconds since midnight.
void computeDate(std::time_t now, double fractionalSeconds, float out[4]);

// Playback clock for one shader: iTime, iTimeDelta, iFrame, iFrameRate.
class PlaybackClock {
public:
    void reset() { *this = PlaybackClock(); }
    // Advances by real elapsed seconds (clamped, so a stall doesn't make iTime jump).
    void advance(double realDelta);
    double time() const { return time_; }
    float delta() const { return delta_; }
    int frame() const { return frame_; }
    float frameRate() const { return frameRate_; }
    void frameRendered() { ++frame_; }

private:
    double time_ = 0;
    float delta_ = 0;
    int frame_ = 0;
    float frameRate_ = 60;
};

}  // namespace sp
