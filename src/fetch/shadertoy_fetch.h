#pragma once
// Fetches one shader's JSON from shadertoy.com through the site's own (unofficial)
// endpoint. This is the only code that talks to the network, and the only code that
// links libcurl. Everything runs on a worker thread; the app polls for the result.
//
// shadertoy.com sits behind Cloudflare, which often answers scripts with a browser
// check. The app doesn't try to pass it: it reports the check and points the user to
// JSON paste/import instead.

#include <atomic>
#include <future>
#include <memory>
#include <optional>
#include <string>

namespace sp {

struct FetchResult {
    enum class Kind { Ok, Blocked, NotFound, RateLimited, Network, BadResponse, Cancelled };
    Kind kind = Kind::Network;
    std::string id;
    std::string message;  // readable, for the UI (empty when Ok)
    std::string json;     // the shader object, when Ok
    long httpStatus = 0;
};

const char* fetchKindName(FetchResult::Kind kind);

struct FetchConfig {
    std::string endpoint = "https://www.shadertoy.com/shadertoy";
    std::string referer = "https://www.shadertoy.com/view/";  // + id
    long connectTimeoutSeconds = 10;
    long timeoutSeconds = 20;
    size_t maxBytes = 8u * 1024u * 1024u;
    bool allowPlainHttpLoopback = false;  // tests only: http://127.0.0.1 servers
};

// The User-Agent sent with every request: names the app, version and libcurl.
std::string fetchUserAgent();

// The form body the site's own page sends.
std::string fetchRequestBody(const std::string& id);

// Decides what a response means. `headers` is the raw header block.
FetchResult classifyResponse(const std::string& id, long status, const std::string& headers,
                             const std::string& body);

// Spaces requests out, and backs off after a browser check or HTTP 429.
class FetchRateLimiter {
public:
    static constexpr double kMinInterval = 3.0;       // between any two requests
    static constexpr double kBlockedCooldown = 300.0;  // after a Cloudflare check
    static constexpr double kRateLimitedCooldown = 60.0;

    // False (with a readable reason) when a request now would be too soon.
    bool allowed(double now, std::string* why) const;
    void started(double now) { last_ = now; }
    void finished(FetchResult::Kind kind, double now);

private:
    double last_ = -1e9;
    double until_ = -1e9;
    FetchResult::Kind lastKind_ = FetchResult::Kind::Ok;
};

class ShadertoyFetcher {
public:
    explicit ShadertoyFetcher(FetchConfig config = {});
    ~ShadertoyFetcher();  // cancels a running fetch and waits for it (quickly)
    ShadertoyFetcher(const ShadertoyFetcher&) = delete;
    ShadertoyFetcher& operator=(const ShadertoyFetcher&) = delete;

    // Starts fetching `id` (a valid ShaderToy ID). False, with a reason, when busy or
    // rate limited. `now` is in seconds, from any steady clock.
    bool start(const std::string& id, double now, std::string* why);
    bool busy() const { return future_.valid(); }
    // The result once the fetch has finished, else nullopt.
    std::optional<FetchResult> poll(double now);
    void cancel() { cancel_ = true; }

    // Runs one request on the calling thread (what the worker does).
    static FetchResult fetchBlocking(const FetchConfig& config, const std::string& id,
                                     const std::atomic<bool>& cancel);

private:
    FetchConfig config_;
    FetchRateLimiter limiter_;
    std::future<FetchResult> future_;
    std::atomic<bool> cancel_{false};
};

}  // namespace sp
