#pragma once
// iMouse with ShaderToy's semantics (all values in shader pixels, origin bottom-left):
//   xy: current position while the button is held, last drag position after release
//       (0,0 until the first click)
//   z:  +click x while held, -click x after release
//   w:  +click y only during the first frame after the click, -click y afterwards
// Only the left button counts, and only presses that start over the shader.

namespace sp {

class MouseState {
public:
    // Positions are already in shader pixels (see toShaderPixels).
    void press(float x, float y);
    void move(float x, float y);  // ignored unless the button is held
    void release();

    // Value to upload for the frame about to be rendered.
    void uniform(float out[4]) const;
    // Call after each rendered frame (clears the one-frame click flag).
    void frameRendered() { clickFresh_ = false; }

    bool held() const { return held_; }
    void reset() { *this = MouseState(); }

private:
    float posX_ = 0, posY_ = 0, clickX_ = 0, clickY_ = 0;
    bool held_ = false;
    bool clickFresh_ = false;
};

// Window coordinates (origin top-left, window units) to shader pixels
// (origin bottom-left, render resolution), as ShaderToy floors them.
void toShaderPixels(float winX, float winY, int winW, int winH, int resW, int resH, float* outX, float* outY);

}  // namespace sp
