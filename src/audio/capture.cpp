#include "audio/capture.h"

#include <pulse/pulseaudio.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <thread>

#include "app_info.h"
#include "platform/log.h"

namespace sp {

namespace {

constexpr auto kConnectTimeout = std::chrono::seconds(4);

void contextStateCb(pa_context*, void* self) { static_cast<AudioCapture*>(self)->onContextState(); }
void streamStateCb(pa_stream*, void* self) { static_cast<AudioCapture*>(self)->onStreamState(); }
void streamReadCb(pa_stream*, size_t bytes, void* self) { static_cast<AudioCapture*>(self)->onRead(bytes); }
void serverInfoCb(pa_context*, const pa_server_info* info, void* self) {
    static_cast<AudioCapture*>(self)->onServerInfo(info && info->default_sink_name ? info->default_sink_name : "");
}
void subscribeCb(pa_context*, pa_subscription_event_type_t type, uint32_t, void* self) {
    if ((type & PA_SUBSCRIPTION_EVENT_FACILITY_MASK) == PA_SUBSCRIPTION_EVENT_SERVER)
        static_cast<AudioCapture*>(self)->onServerChanged();
}

}  // namespace

void AudioCapture::start(const std::string& device) {
    stop();
    device_ = device;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        error_.clear();
        source_.clear();
        std::fill(ring_.begin(), ring_.end(), 0.0f);
        writePos_ = filled_ = 0;
    }
    defaultSink_.clear();
    started_ = std::chrono::steady_clock::now();
    state_ = State::Connecting;

    mainloop_ = pa_threaded_mainloop_new();
    if (!mainloop_) {
        fail("could not create the PulseAudio main loop");
        return;
    }
    context_ = pa_context_new(pa_threaded_mainloop_get_api(mainloop_), APP_DISPLAY_NAME);
    if (!context_) {
        fail("could not create a PulseAudio context");
        return;
    }
    pa_context_set_state_callback(context_, contextStateCb, this);
    // No autospawn: if no server runs, fail instead of starting one.
    if (pa_context_connect(context_, nullptr, PA_CONTEXT_NOAUTOSPAWN, nullptr) < 0) {
        fail(std::string("no PulseAudio/PipeWire server: ") + pa_strerror(pa_context_errno(context_)));
        return;
    }
    if (pa_threaded_mainloop_start(mainloop_) < 0) {
        fail("could not start the PulseAudio thread");
        return;
    }
}

void AudioCapture::stop() {
    if (mainloop_) {
        pa_threaded_mainloop_lock(mainloop_);
        if (stream_) {
            pa_stream_set_state_callback(stream_, nullptr, nullptr);
            pa_stream_set_read_callback(stream_, nullptr, nullptr);
            pa_stream_disconnect(stream_);
            pa_stream_unref(stream_);
            stream_ = nullptr;
        }
        if (context_) {
            pa_context_set_state_callback(context_, nullptr, nullptr);
            pa_context_set_subscribe_callback(context_, nullptr, nullptr);
            pa_context_disconnect(context_);
            pa_context_unref(context_);
            context_ = nullptr;
        }
        pa_threaded_mainloop_unlock(mainloop_);
        pa_threaded_mainloop_stop(mainloop_);
        pa_threaded_mainloop_free(mainloop_);
        mainloop_ = nullptr;
    } else if (context_) {
        pa_context_unref(context_);
        context_ = nullptr;
    }
    if (state_ != State::Failed) state_ = State::Off;
}

void AudioCapture::fail(const std::string& message) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (state_ == State::Failed) return;
        error_ = message;
    }
    state_ = State::Failed;
    LOG_WARN("audio capture: %s", message.c_str());
}

AudioCapture::State AudioCapture::state() {
    if (state_ == State::Connecting && std::chrono::steady_clock::now() - started_ > kConnectTimeout) {
        fail("timed out connecting to the audio server");
        stop();
    }
    return state_;
}

std::string AudioCapture::error() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return error_;
}

std::string AudioCapture::sourceName() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return source_;
}

// ------------------------------------------------- libpulse thread callbacks

void AudioCapture::onContextState() {
    switch (pa_context_get_state(context_)) {
        case PA_CONTEXT_READY:
            if (device_.empty()) {
                // Follow the default output: learn its name and watch for changes.
                pa_context_set_subscribe_callback(context_, subscribeCb, this);
                if (pa_operation* op = pa_context_subscribe(context_, PA_SUBSCRIPTION_MASK_SERVER, nullptr, nullptr))
                    pa_operation_unref(op);
                if (pa_operation* op = pa_context_get_server_info(context_, serverInfoCb, this))
                    pa_operation_unref(op);
            } else {
                connectStream();
            }
            break;
        case PA_CONTEXT_FAILED:
        case PA_CONTEXT_TERMINATED:
            fail(std::string(state_ == State::Connecting ? "cannot connect to the audio server (PulseAudio/PipeWire): "
                                                          : "audio server connection lost: ") +
                 pa_strerror(pa_context_errno(context_)));
            break;
        default:
            break;
    }
}

void AudioCapture::onServerChanged() {
    if (pa_operation* op = pa_context_get_server_info(context_, serverInfoCb, this)) pa_operation_unref(op);
}

void AudioCapture::onServerInfo(const char* defaultSink) {
    if (!device_.empty()) return;
    if (stream_ && defaultSink_ == defaultSink) return;  // nothing changed
    if (stream_) LOG_INFO("audio: default output changed to %s, reconnecting", defaultSink);
    defaultSink_ = defaultSink;
    connectStream();
}

void AudioCapture::connectStream() {
    if (stream_) {
        pa_stream_set_state_callback(stream_, nullptr, nullptr);
        pa_stream_set_read_callback(stream_, nullptr, nullptr);
        pa_stream_disconnect(stream_);
        pa_stream_unref(stream_);
        stream_ = nullptr;
    }
    const pa_sample_spec spec{PA_SAMPLE_FLOAT32LE, kSampleRate, 1};  // the server downmixes to mono
    stream_ = pa_stream_new(context_, "loopback", &spec, nullptr);
    if (!stream_) {
        fail(std::string("cannot create capture stream: ") + pa_strerror(pa_context_errno(context_)));
        return;
    }
    pa_stream_set_state_callback(stream_, streamStateCb, this);
    pa_stream_set_read_callback(stream_, streamReadCb, this);

    pa_buffer_attr attr;
    std::memset(&attr, 0xff, sizeof(attr));  // (uint32_t)-1 = server default
    attr.fragsize = 512 * sizeof(float);     // ~10 ms chunks: low latency for visuals

    std::string target = device_;
    if (target.empty()) target = defaultSink_.empty() ? "@DEFAULT_MONITOR@" : defaultSink_ + ".monitor";
    if (pa_stream_connect_record(stream_, target.c_str(), &attr, PA_STREAM_ADJUST_LATENCY) < 0)
        fail("cannot capture from '" + target + "': " + pa_strerror(pa_context_errno(context_)));
}

void AudioCapture::onStreamState() {
    switch (pa_stream_get_state(stream_)) {
        case PA_STREAM_READY: {
            const char* dev = pa_stream_get_device_name(stream_);
            {
                std::lock_guard<std::mutex> lock(mutex_);
                source_ = dev ? dev : "";
            }
            state_ = State::Running;
            LOG_INFO("audio capture running from %s", dev ? dev : "?");
            break;
        }
        case PA_STREAM_FAILED:
            fail(std::string("capture stream failed: ") + pa_strerror(pa_context_errno(context_)));
            break;
        default:
            break;
    }
}

void AudioCapture::onRead(size_t) {
    const void* data = nullptr;
    size_t bytes = 0;
    while (pa_stream_readable_size(stream_) > 0) {
        if (pa_stream_peek(stream_, &data, &bytes) < 0 || bytes == 0) return;
        const size_t count = bytes / sizeof(float);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            const float* samples = static_cast<const float*>(data);  // null = hole: write silence
            for (size_t i = 0; i < count; ++i) {
                float v = samples ? samples[i] : 0.0f;
                if (!std::isfinite(v)) v = 0.0f;
                ring_[writePos_] = v;
                writePos_ = (writePos_ + 1) % ring_.size();
            }
            filled_ = std::min(ring_.size(), filled_ + count);
        }
        pa_stream_drop(stream_);
    }
}

void AudioCapture::latest(float* out, int n) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const size_t want = static_cast<size_t>(n);
    const size_t have = std::min(filled_, std::min(want, ring_.size()));
    const size_t pad = want - have;
    std::fill(out, out + pad, 0.0f);
    size_t pos = (writePos_ + ring_.size() - have) % ring_.size();
    for (size_t i = 0; i < have; ++i) {
        out[pad + i] = ring_[pos];
        pos = (pos + 1) % ring_.size();
    }
}

// ------------------------------------------------- source listing

namespace {

struct ListState {
    std::vector<AudioSource> sources;
    bool done = false;
    bool failed = false;
};

void sourceInfoCb(pa_context*, const pa_source_info* info, int eol, void* userdata) {
    auto* st = static_cast<ListState*>(userdata);
    if (eol != 0 || !info) {
        st->done = true;
        return;
    }
    AudioSource s;
    s.name = info->name ? info->name : "";
    s.description = info->description ? info->description : s.name;
    s.isMonitor = info->monitor_of_sink != PA_INVALID_INDEX;
    st->sources.push_back(s);
}

}  // namespace

std::vector<AudioSource> AudioCapture::listSources(std::string* error, int timeoutMs) {
    pa_mainloop* ml = pa_mainloop_new();
    if (!ml) {
        if (error) *error = "could not create the PulseAudio main loop";
        return {};
    }
    pa_context* ctx = pa_context_new(pa_mainloop_get_api(ml), APP_DISPLAY_NAME);
    ListState st;
    bool requested = false;
    std::string err;
    if (!ctx || pa_context_connect(ctx, nullptr, PA_CONTEXT_NOAUTOSPAWN, nullptr) < 0) {
        err = std::string("no PulseAudio/PipeWire server: ") + (ctx ? pa_strerror(pa_context_errno(ctx)) : "?");
    } else {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
        while (!st.done) {
            if (std::chrono::steady_clock::now() > deadline) {
                err = "timed out listing audio sources";
                break;
            }
            pa_mainloop_iterate(ml, 0, nullptr);
            const pa_context_state_t cs = pa_context_get_state(ctx);
            if (cs == PA_CONTEXT_FAILED || cs == PA_CONTEXT_TERMINATED) {
                err = std::string("audio server connection failed: ") + pa_strerror(pa_context_errno(ctx));
                break;
            }
            if (cs == PA_CONTEXT_READY && !requested) {
                requested = true;
                if (pa_operation* op = pa_context_get_source_info_list(ctx, sourceInfoCb, &st)) pa_operation_unref(op);
            }
            if (!st.done) std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }
    if (ctx) {
        pa_context_disconnect(ctx);
        pa_context_unref(ctx);
    }
    pa_mainloop_free(ml);
    if (error) *error = err;
    std::stable_sort(st.sources.begin(), st.sources.end(),
                     [](const AudioSource& a, const AudioSource& b) { return a.isMonitor && !b.isMonitor; });
    return st.sources;
}

}  // namespace sp
