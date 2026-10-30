#pragma once
// ShaderToy's audio texture: 512x2, single channel (R8).
// Row 0 = FFT magnitudes, row 1 = waveform.

#include <cstdint>

#include "gfx/gl.h"

namespace sp {

class AudioTexture {
public:
    ~AudioTexture();
    void init();  // starts as silence
    void upload(const uint8_t frequency[512], const uint8_t waveform[512]);
    void uploadSilence();
    GLuint id() const { return texture_; }

private:
    GLuint texture_ = 0;
};

}  // namespace sp
