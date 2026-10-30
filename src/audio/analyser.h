#pragma once
// Emulation of WebAudio's AnalyserNode as ShaderToy uses it (fftSize 1024),
// producing the two rows of ShaderToy's 512x2 audio texture:
//   row 0: getByteFrequencyData  (Blackman window, smoothing 0.8, -100..-30 dB)
//   row 1: getByteTimeDomainData (first 512 samples of the 1024-sample window)

#include <array>
#include <complex>
#include <cstdint>
#include <vector>

namespace sp {

// In-place radix-2 FFT. Size must be a power of two.
void fft(std::vector<std::complex<float>>& data);

class SpectrumAnalyser {
public:
    static constexpr int kFftSize = 1024;
    static constexpr int kBins = kFftSize / 2;  // 512, the texture width

    explicit SpectrumAnalyser(float smoothing = 0.8f, float minDb = -100.0f, float maxDb = -30.0f);

    // `samples`: the most recent kFftSize mono samples, oldest first, in [-1, 1].
    void process(const float* samples, uint8_t frequency[kBins], uint8_t waveform[kBins]);
    void reset();

    // Output for silence / no capture: frequency all 0, waveform all 128.
    static void silence(uint8_t frequency[kBins], uint8_t waveform[kBins]);

private:
    float smoothing_, minDb_, maxDb_;
    std::array<float, kFftSize> window_;
    std::array<float, kBins> smoothed_{};
    std::vector<std::complex<float>> scratch_;
};

}  // namespace sp
