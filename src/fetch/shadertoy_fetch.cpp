#include "fetch/shadertoy_fetch.h"

#include <curl/curl.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <mutex>

#include "app_info.h"
#include "platform/log.h"
#include "shadertoy/json_format.h"

namespace sp {

using json = nlohmann::json;

namespace {

const char* const kJsonHint =
    " You can still add it: open the shader in your browser, export its JSON (a ShaderToy export "
    "extension works), then paste it into this dialog or use Import JSON file.";

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

bool contains(const std::string& hay, const char* needle) { return hay.find(needle) != std::string::npos; }

// A header line "cf-mitigated: challenge" is Cloudflare's own marker for a browser check.
bool hasChallengeHeader(const std::string& lowerHeaders) {
    size_t pos = 0;
    while ((pos = lowerHeaders.find("cf-mitigated:", pos)) != std::string::npos) {
        if (pos == 0 || lowerHeaders[pos - 1] == '\n') {
            const size_t eol = lowerHeaders.find('\n', pos);
            if (contains(lowerHeaders.substr(pos, eol == std::string::npos ? std::string::npos : eol - pos),
                         "challenge"))
                return true;
        }
        pos += 13;
    }
    return false;
}

bool looksLikeHtml(const std::string& body) {
    for (char c : body) {
        if (std::isspace(static_cast<unsigned char>(c))) continue;
        return c == '<';
    }
    return false;
}

FetchResult fail(FetchResult r, FetchResult::Kind kind, const std::string& message, bool hint = true) {
    r.kind = kind;
    r.message = message + (hint ? kJsonHint : "");
    return r;
}

struct Sink {
    std::string* text;
    size_t maxBytes;
    bool overflow = false;
};

size_t onBody(char* data, size_t size, size_t count, void* user) {
    auto* sink = static_cast<Sink*>(user);
    const size_t n = size * count;
    if (sink->text->size() + n > sink->maxBytes) {
        sink->overflow = true;
        return 0;  // makes libcurl stop with a write error
    }
    sink->text->append(data, n);
    return n;
}

size_t onHeader(char* data, size_t size, size_t count, void* user) {
    auto* headers = static_cast<std::string*>(user);
    const size_t n = size * count;
    if (headers->size() < 64 * 1024) headers->append(data, n);
    return n;
}

int onProgress(void* user, curl_off_t, curl_off_t, curl_off_t, curl_off_t) {
    return static_cast<const std::atomic<bool>*>(user)->load() ? 1 : 0;
}

std::string formEscape(const std::string& s) {
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : s) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            out += static_cast<char>(c);
        } else {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 15];
        }
    }
    return out;
}

std::once_flag curlInitOnce;

}  // namespace

const char* fetchKindName(FetchResult::Kind kind) {
    switch (kind) {
        case FetchResult::Kind::Ok: return "ok";
        case FetchResult::Kind::Blocked: return "blocked by browser check";
        case FetchResult::Kind::NotFound: return "not found";
        case FetchResult::Kind::RateLimited: return "rate limited";
        case FetchResult::Kind::Network: return "network error";
        case FetchResult::Kind::BadResponse: return "unexpected response";
        case FetchResult::Kind::Cancelled: return "cancelled";
    }
    return "?";
}

std::string fetchUserAgent() {
    std::string ua = std::string(APP_NAME) + "/" + APP_VERSION + " (Linux; personal ShaderToy player)";
    if (const curl_version_info_data* v = curl_version_info(CURLVERSION_NOW)) ua += std::string(" libcurl/") + v->version;
    return ua;
}

std::string fetchRequestBody(const std::string& id) {
    const json req = {{"shaders", json::array({id})}};
    return "s=" + formEscape(req.dump()) + "&nt=1&nl=1&np=1";
}

FetchResult classifyResponse(const std::string& id, long status, const std::string& headers,
                             const std::string& body) {
    FetchResult r;
    r.id = id;
    r.httpStatus = status;

    const bool html = looksLikeHtml(body);
    bool challenge = hasChallengeHeader(lower(headers));
    if (!challenge && html) {
        const std::string page = lower(body.substr(0, 64 * 1024));
        challenge = contains(page, "challenge-platform") || contains(page, "cf-chl") ||
                    contains(page, "just a moment");
    }
    if (challenge)
        return fail(r, FetchResult::Kind::Blocked,
                    "shadertoy.com answered with a Cloudflare browser check, which this app doesn't try to pass.");
    if (status == 429)
        return fail(r, FetchResult::Kind::RateLimited, "shadertoy.com says too many requests; try again in a minute.");
    if (status == 404) return fail(r, FetchResult::Kind::NotFound, "shadertoy.com says this shader doesn't exist.");
    if (status != 200)
        return fail(r, FetchResult::Kind::BadResponse, "shadertoy.com answered HTTP " + std::to_string(status) + ".");
    if (html)
        return fail(r, FetchResult::Kind::BadResponse,
                    "shadertoy.com sent a web page instead of shader data; its endpoint may have changed.");

    const json j = json::parse(body, nullptr, false);
    if (j.is_discarded())
        return fail(r, FetchResult::Kind::BadResponse,
                    "shadertoy.com's answer isn't JSON; its endpoint may have changed.");
    if (j.is_array() && j.empty())
        return fail(r, FetchResult::Kind::NotFound,
                    "shadertoy.com returned nothing for " + id + ": it doesn't exist or isn't public.");

    std::string error;
    std::vector<json> objs = shaderObjects(body, &error);
    if (objs.empty())
        return fail(r, FetchResult::Kind::BadResponse,
                    "shadertoy.com's answer isn't shader data (" + error + "); its endpoint may have changed.");
    const json* pick = &objs[0];
    for (const json& o : objs)
        if (o.contains("info") && o["info"].is_object() && o["info"].value("id", "") == id) pick = &o;
    const ImportResult check = shaderFromJson(*pick);
    if (!check.ok) return fail(r, FetchResult::Kind::BadResponse, "Can't read the fetched shader: " + check.error);

    r.kind = FetchResult::Kind::Ok;
    r.json = pick->dump();
    return r;
}

bool FetchRateLimiter::allowed(double now, std::string* why) const {
    if (now < until_) {
        const int left = static_cast<int>(std::ceil(until_ - now));
        const std::string wait = left >= 90 ? std::to_string((left + 59) / 60) + " min" : std::to_string(left) + " s";
        if (why)
            *why = lastKind_ == FetchResult::Kind::Blocked
                       ? "shadertoy.com asked for a browser check a moment ago, so fetching is paused for " + wait +
                             ". Paste or import the shader's JSON instead."
                       : "shadertoy.com asked to slow down; try again in " + wait + ".";
        return false;
    }
    if (now - last_ < kMinInterval) {
        if (why) *why = "Wait a few seconds between fetches.";
        return false;
    }
    return true;
}

void FetchRateLimiter::finished(FetchResult::Kind kind, double now) {
    lastKind_ = kind;
    if (kind == FetchResult::Kind::Blocked) until_ = now + kBlockedCooldown;
    else if (kind == FetchResult::Kind::RateLimited) until_ = now + kRateLimitedCooldown;
}

ShadertoyFetcher::ShadertoyFetcher(FetchConfig config) : config_(std::move(config)) {
    std::call_once(curlInitOnce, [] { curl_global_init(CURL_GLOBAL_DEFAULT); });
}

ShadertoyFetcher::~ShadertoyFetcher() {
    cancel_ = true;
    if (future_.valid()) future_.wait();
}

bool ShadertoyFetcher::start(const std::string& id, double now, std::string* why) {
    if (busy()) {
        if (why) *why = "A fetch is already running.";
        return false;
    }
    if (!isShadertoyId(id)) {
        if (why) *why = "Not a ShaderToy shader ID.";
        return false;
    }
    if (!limiter_.allowed(now, why)) return false;
    limiter_.started(now);
    cancel_ = false;
    LOG_INFO("fetch: requesting %s from %s", id.c_str(), config_.endpoint.c_str());
    future_ = std::async(std::launch::async, [this, config = config_, id] { return fetchBlocking(config, id, cancel_); });
    return true;
}

std::optional<FetchResult> ShadertoyFetcher::poll(double now) {
    if (!future_.valid() || future_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return std::nullopt;
    FetchResult r = future_.get();
    limiter_.finished(r.kind, now);
    if (r.kind == FetchResult::Kind::Ok)
        LOG_INFO("fetch: %s ok (%zu bytes)", r.id.c_str(), r.json.size());
    else
        LOG_WARN("fetch: %s: %s (HTTP %ld)", r.id.c_str(), fetchKindName(r.kind), r.httpStatus);
    return r;
}

FetchResult ShadertoyFetcher::fetchBlocking(const FetchConfig& config, const std::string& id,
                                            const std::atomic<bool>& cancel) {
    FetchResult r;
    r.id = id;
    CURL* curl = curl_easy_init();
    if (!curl) return fail(r, FetchResult::Kind::Network, "Could not start libcurl.", false);

    std::string body, headers;
    Sink sink{&body, config.maxBytes};
    char errbuf[CURL_ERROR_SIZE] = {0};
    const std::string postFields = fetchRequestBody(id);
    const std::string referer = "Referer: " + config.referer + id;
    curl_slist* list = nullptr;
    list = curl_slist_append(list, referer.c_str());
    list = curl_slist_append(list, "Content-Type: application/x-www-form-urlencoded");
    list = curl_slist_append(list, "Accept: application/json");
    const std::string ua = fetchUserAgent();

    const bool loopback = config.allowPlainHttpLoopback && config.endpoint.rfind("http://127.0.0.1", 0) == 0;
    curl_easy_setopt(curl, CURLOPT_URL, config.endpoint.c_str());
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, loopback ? "http" : "https");
    if (loopback) curl_easy_setopt(curl, CURLOPT_NOPROXY, "127.0.0.1");
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);  // required with threads
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, list);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, ua.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, postFields.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(postFields.size()));
    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");  // whatever libcurl can decode
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, config.connectTimeoutSeconds);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, config.timeoutSeconds);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, onBody);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &sink);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, onHeader);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &headers);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, onProgress);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, const_cast<std::atomic<bool>*>(&cancel));
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errbuf);

    const CURLcode res = curl_easy_perform(curl);
    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    curl_slist_free_all(list);
    curl_easy_cleanup(curl);
    r.httpStatus = status;

    if (sink.overflow)
        return fail(r, FetchResult::Kind::BadResponse,
                    "shadertoy.com's answer is too large (over " + std::to_string(config.maxBytes >> 20) + " MB).");
    if (res == CURLE_ABORTED_BY_CALLBACK || cancel) return fail(r, FetchResult::Kind::Cancelled, "Cancelled.", false);
    if (res != CURLE_OK) {
        std::string detail = errbuf[0] ? errbuf : curl_easy_strerror(res);
        if (res == CURLE_OPERATION_TIMEDOUT) detail = "the request timed out";
        else if (res == CURLE_COULDNT_RESOLVE_HOST) detail = "can't find shadertoy.com (offline?)";
        return fail(r, FetchResult::Kind::Network, "Can't reach shadertoy.com: " + detail + ".");
    }
    return classifyResponse(id, status, headers, body);
}

}  // namespace sp
