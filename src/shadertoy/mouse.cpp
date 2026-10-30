#include "shadertoy/mouse.h"

#include <algorithm>
#include <cmath>

namespace sp {

void MouseState::press(float x, float y) {
    posX_ = clickX_ = x;
    posY_ = clickY_ = y;
    held_ = true;
    clickFresh_ = true;
}

void MouseState::move(float x, float y) {
    if (!held_) return;
    posX_ = x;
    posY_ = y;
}

void MouseState::release() { held_ = false; }

void MouseState::uniform(float out[4]) const {
    out[0] = posX_;
    out[1] = posY_;
    out[2] = held_ ? std::fabs(clickX_) : -std::fabs(clickX_);
    out[3] = clickFresh_ ? std::fabs(clickY_) : -std::fabs(clickY_);
}

void toShaderPixels(float winX, float winY, int winW, int winH, int resW, int resH, float* outX, float* outY) {
    const float fx = winW > 0 ? std::clamp(winX / static_cast<float>(winW), 0.0f, 1.0f) : 0.0f;
    const float fy = winH > 0 ? std::clamp(winY / static_cast<float>(winH), 0.0f, 1.0f) : 0.0f;
    *outX = std::floor(std::min(fx * resW, resW - 1.0f));
    *outY = std::floor(std::min((1.0f - fy) * resH, resH - 1.0f));
    if (*outX < 0) *outX = 0;
    if (*outY < 0) *outY = 0;
}

}  // namespace sp
