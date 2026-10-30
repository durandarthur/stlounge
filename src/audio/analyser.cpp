#include "audio/analyser.h"

#include <algorithm>
#include <cmath>

namespace sp {

namespace {
inline float clean(float x) { return std::isfinite(x) ? std::clamp(x, -1.0f, 1.0f) : 0.0f; }
}  // namespace

void fft(std::vector<std::complex<float>>& a) {
    const size_t n = a.size();
    // Bit-reversal permutation.
    for (size_t i = 1, j = 0; i < n; ++i) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    const double pi = 3.14159265358979323846;
    for (size_t len = 2; len <= n; len <<= 1) {
        const double angle = -2.0 * pi / static_cast<double>(len);
        const std::complex<float> wl(static_cast<float>(std::cos(angle)), static_cast<float>(std::sin(angle)));
        for (size_t i = 0; i < n; i += len) {
            std::complex<float> w(1.0f, 0.0f);
            for (size_t k = 0; k < len / 2; ++k) {
                const std::complex<float> u = a[i + k];
                const std::complex<float> v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
                w *= wl;
            }
        }
    }
}

SpectrumAnalyser::SpectrumAnalyser(float smoothing, float minDb, float maxDb)
    : smoothing_(smoothing), minDb_(minDb), maxDb_(maxDb), scratch_(kFftSize) {
    // Blackman window exactly as the WebAudio spec defines it (alpha = 0.16).
    const double pi = 3.14159265358979323846;
    for (int n = 0; n < kFftSize; ++n) {
        const double x = static_cast<double>(n) / kFftSize;
        window_[static_cast<size_t>(n)] =
            static_cast<float>(0.42 - 0.5 * std::cos(2.0 * pi * x) + 0.08 * std::cos(4.0 * pi * x));
    }
}

void SpectrumAnalyser::reset() { smoothed_.fill(0.0f); }

void SpectrumAnalyser::silence(uint8_t frequency[kBins], uint8_t waveform[kBins]) {
    std::fill(frequency, frequency + kBins, uint8_t{0});
    std::fill(waveform, waveform + kBins, uint8_t{128});
}

void SpectrumAnalyser::process(const float* samples, uint8_t frequency[kBins], uint8_t waveform[kBins]) {
    // Time domain: b = floor(128 * (1 + x)), clamped.
    for (int i = 0; i < kBins; ++i) {
        const float v = std::floor(128.0f * (1.0f + clean(samples[i])));
        waveform[i] = static_cast<uint8_t>(std::clamp(v, 0.0f, 255.0f));
    }

    for (int n = 0; n < kFftSize; ++n)
        scratch_[static_cast<size_t>(n)] = {clean(samples[n]) * window_[static_cast<size_t>(n)], 0.0f};
    fft(scratch_);

    const float scale = 255.0f / (maxDb_ - minDb_);
    for (int k = 0; k < kBins; ++k) {
        const float magnitude = std::abs(scratch_[static_cast<size_t>(k)]) / kFftSize;
        float& s = smoothed_[static_cast<size_t>(k)];
        s = smoothing_ * s + (1.0f - smoothing_) * magnitude;
        if (!std::isfinite(s)) s = 0.0f;  // never let a bad sample poison the smoothing
        const float db = s > 0.0f ? 20.0f * std::log10(s) : -1e9f;
        const float b = std::floor(scale * (db - minDb_));
        frequency[k] = static_cast<uint8_t>(std::clamp(b, 0.0f, 255.0f));
    }
}

}  // namespace sp
