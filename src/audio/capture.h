#pragma once
// Desktop audio loopback capture through libpulse (works with PulseAudio
// and with PipeWire via pipewire-pulse). Runs on libpulse's own thread;
// the app only copies the latest samples once per frame.
//
// Failure is never fatal: with no audio server the state becomes Failed
// with a readable message and audio inputs stay silent.

#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <vector>

struct pa_threaded_mainloop;
struct pa_context;
struct pa_stream;

namespace sp {

struct AudioSource {
    std::string name;         // PulseAudio source name, used to connect
    std::string description;  // human-readable
    bool isMonitor = false;   // monitor of an output (loopback)
};

class AudioCapture {
public:
    enum class State { Off, Connecting, Running, Failed };
    static constexpr int kSampleRate = 48000;

    AudioCapture() = default;
    ~AudioCapture() { stop(); }
    AudioCapture(const AudioCapture&) = delete;
    AudioCapture& operator=(const AudioCapture&) = delete;

    // device: a source name, or "" for the monitor of the default output
    // (followed when the default output changes).
    void start(const std::string& device);
    void stop();

    State state();  // also turns a connection that takes too long into Failed
    std::string error() const;
    std::string sourceName() const;  // the source actually connected, once Running
    const std::string& requestedDevice() const { return device_; }

    // Copies the most recent n samples, oldest first. Zero-padded at the start
    // if fewer have been captured so far.
    void latest(float* out, int n) const;

    // Lists capture sources (monitors first). Blocks up to timeoutMs.
    static std::vector<AudioSource> listSources(std::string* error, int timeoutMs = 2000);

    // Called from libpulse's thread; public only for the C callbacks.
    void onContextState();
    void onStreamState();
    void onRead(size_t bytes);
    void onServerInfo(const char* defaultSink);
    void onServerChanged();

private:
    void connectStream();  // mainloop lock must be held
    void fail(const std::string& message);

    std::string device_;
    pa_threaded_mainloop* mainloop_ = nullptr;
    pa_context* context_ = nullptr;
    pa_stream* stream_ = nullptr;
    std::string defaultSink_;  // when following the default output

    std::atomic<State> state_{State::Off};
    std::chrono::steady_clock::time_point started_;
    mutable std::mutex mutex_;  // guards ring_, writePos_, filled_, error_, source_
    std::vector<float> ring_ = std::vector<float>(16384, 0.0f);
    size_t writePos_ = 0, filled_ = 0;
    std::string error_, source_;
};

}  // namespace sp
