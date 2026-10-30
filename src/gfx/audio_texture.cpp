#include "gfx/audio_texture.h"

#include <algorithm>

namespace sp {

AudioTexture::~AudioTexture() {
    if (texture_) glDeleteTextures(1, &texture_);
}

void AudioTexture::init() {
    if (texture_) return;
    glGenTextures(1, &texture_);
    glBindTexture(GL_TEXTURE_2D, texture_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, 512, 2, 0, GL_RED, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
    uploadSilence();
}

void AudioTexture::upload(const uint8_t frequency[512], const uint8_t waveform[512]) {
    uint8_t data[1024];
    std::copy(frequency, frequency + 512, data);
    std::copy(waveform, waveform + 512, data + 512);
    glBindTexture(GL_TEXTURE_2D, texture_);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 512, 2, GL_RED, GL_UNSIGNED_BYTE, data);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void AudioTexture::uploadSilence() {
    uint8_t freq[512], wave[512];
    std::fill(freq, freq + 512, uint8_t{0});
    std::fill(wave, wave + 512, uint8_t{128});
    upload(freq, wave);
}

}  // namespace sp
