#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>
#include <vector>

#include "audio/analyser.h"
#include "audio/capture.h"
#include "test.h"

using namespace sp;

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr int N = SpectrumAnalyser::kFftSize;
constexpr int B = SpectrumAnalyser::kBins;

std::vector<float> sine(double cyclesPerWindow, float amplitude) {
    std::vector<float> s(N);
    for (int i = 0; i < N; ++i) s[size_t(i)] = amplitude * float(std::sin(2.0 * kPi * cyclesPerWindow * i / N));
    return s;
}

int argmax(const uint8_t* v, int n) {
    int best = 0;
    for (int i = 1; i < n; ++i)
        if (v[i] > v[best]) best = i;
    return best;
}

// Expected byte for a bin of linear magnitude m (WebAudio formula, defaults).
int expectedByte(double m) { return int(std::floor(255.0 / 70.0 * (20.0 * std::log10(m) + 100.0))); }

struct EnvGuard {
    std::string name, old;
    bool had;
    EnvGuard(const char* n, const char* value) : name(n) {
        const char* o = std::getenv(n);
        had = o != nullptr;
        if (o) old = o;
        setenv(n, value, 1);
    }
    ~EnvGuard() {
        if (had) setenv(name.c_str(), old.c_str(), 1);
        else unsetenv(name.c_str());
    }
};

}  // namespace

TEST(fft_matches_direct_dft) {
    std::vector<std::complex<float>> a(16);
    for (size_t i = 0; i < a.size(); ++i) a[i] = {float(std::cos(0.3 * i) + 0.1 * i), float(0.05 * i * i)};
    std::vector<std::complex<double>> ref(16);
    for (size_t k = 0; k < 16; ++k)
        for (size_t n = 0; n < 16; ++n)
            ref[k] += std::complex<double>(a[n].real(), a[n].imag()) * std::polar(1.0, -2.0 * kPi * double(k * n) / 16.0);
    fft(a);
    for (size_t k = 0; k < 16; ++k) {
        CHECK_NEAR(a[k].real(), ref[k].real(), 1e-3);
        CHECK_NEAR(a[k].imag(), ref[k].imag(), 1e-3);
    }
}

TEST(analyser_silence) {
    SpectrumAnalyser an;
    std::vector<float> zeros(N, 0.0f);
    uint8_t f[B], w[B];
    an.process(zeros.data(), f, w);
    for (int i = 0; i < B; ++i) {
        CHECK_EQ(int(f[i]), 0);
        CHECK_EQ(int(w[i]), 128);
    }
    uint8_t sf[B], sw[B];
    SpectrumAnalyser::silence(sf, sw);
    CHECK_EQ(int(sf[10]), 0);
    CHECK_EQ(int(sw[10]), 128);
}

TEST(analyser_sine_lands_in_its_bin_with_webaudio_level) {
    SpectrumAnalyser an(0.0f);  // no smoothing: one frame gives the steady value
    const float amp = 0.01f;   // quiet enough to stay inside -100..-30 dB
    auto s = sine(64.0, amp);
    uint8_t f[B], w[B];
    an.process(s.data(), f, w);
    CHECK_EQ(argmax(f, B), 64);
    // Windowed sine magnitude at its bin: amp * a0 / 2 = amp * 0.21.
    const int expected = expectedByte(amp * 0.21);
    std::printf("    bin 64 = %d (expected %d), bin 200 = %d\n", f[64], expected, f[200]);
    CHECK(std::abs(int(f[64]) - expected) <= 1);
    CHECK(f[200] < 20);  // far bins: Blackman sidelobes are tiny
}

TEST(analyser_smoothing_follows_webaudio) {
    SpectrumAnalyser smooth(0.8f), raw(0.0f);
    auto s = sine(32.0, 0.01f);
    uint8_t f[B], w[B], fr[B];
    raw.process(s.data(), fr, w);
    smooth.process(s.data(), f, w);
    // After one frame the smoothed magnitude is 20% of the raw one: -14 dB, ~51 bytes lower.
    const int drop = int(fr[32]) - int(f[32]);
    CHECK(drop >= 49 && drop <= 53);
    for (int i = 0; i < 60; ++i) smooth.process(s.data(), f, w);
    CHECK(std::abs(int(f[32]) - int(fr[32])) <= 1);  // converged
    smooth.reset();
    std::vector<float> zeros(N, 0.0f);
    smooth.process(zeros.data(), f, w);
    CHECK_EQ(int(f[32]), 0);
}

TEST(analyser_waveform_mapping_and_bad_samples) {
    SpectrumAnalyser an;
    std::vector<float> s(N, 0.0f);
    s[0] = -1.0f;
    s[1] = 1.0f;
    s[2] = 0.5f;
    s[3] = 7.0f;            // out of range: clamped
    s[4] = std::nanf("");   // garbage: treated as silence
    s[5] = INFINITY;
    s[600] = 1.0f;          // second half of the window is not in the waveform row
    uint8_t f[B], w[B];
    an.process(s.data(), f, w);
    CHECK_EQ(int(w[0]), 0);
    CHECK_EQ(int(w[1]), 255);
    CHECK_EQ(int(w[2]), 192);
    CHECK_EQ(int(w[3]), 255);
    CHECK_EQ(int(w[4]), 128);
    CHECK_EQ(int(w[5]), 128);
    for (int i = 0; i < B; ++i) CHECK(f[i] <= 255);
}

TEST(capture_fails_cleanly_without_server) {
    EnvGuard env("PULSE_SERVER", "unix:/nonexistent/stlounge-test/native");
    AudioCapture cap;
    const auto t0 = std::chrono::steady_clock::now();
    cap.start("");
    while (cap.state() == AudioCapture::State::Connecting &&
           std::chrono::steady_clock::now() - t0 < std::chrono::seconds(6))
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    CHECK(cap.state() == AudioCapture::State::Failed);
    std::printf("    error: %s\n", cap.error().c_str());
    CHECK(!cap.error().empty());
    float buf[64];
    cap.latest(buf, 64);  // safe, all zeros
    CHECK_EQ(buf[0], 0.0f);
    cap.stop();
    const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    CHECK(secs < 6.0);

    std::string err;
    auto sources = AudioCapture::listSources(&err, 1000);
    CHECK(sources.empty());
    CHECK(!err.empty());
}

TEST(capture_real_server_smoke) {
    std::string err;
    auto sources = AudioCapture::listSources(&err, 2000);
    if (!err.empty()) SKIP("no audio server reachable: " + err);
    int monitors = 0;
    for (const AudioSource& s : sources) monitors += s.isMonitor;
    std::printf("    %zu sources, %d monitors\n", sources.size(), monitors);

    AudioCapture cap;
    cap.start("");
    const auto t0 = std::chrono::steady_clock::now();
    while (cap.state() == AudioCapture::State::Connecting &&
           std::chrono::steady_clock::now() - t0 < std::chrono::seconds(5))
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    if (cap.state() == AudioCapture::State::Failed) std::printf("    error: %s\n", cap.error().c_str());
    CHECK(cap.state() == AudioCapture::State::Running);
    std::printf("    source: %s\n", cap.sourceName().c_str());
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    std::vector<float> buf(N);
    cap.latest(buf.data(), N);
    for (float v : buf) CHECK(std::isfinite(v));
    cap.stop();
    CHECK(cap.state() == AudioCapture::State::Off);
}
