// URL fetch: link parsing, response classification, rate limiting, and the libcurl path
// against a one-shot HTTP server on 127.0.0.1 (no internet needed).

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <string>
#include <thread>

#include "app_info.h"
#include "fetch/shadertoy_fetch.h"
#include "shadertoy/shader_ref.h"
#include "test.h"

using namespace sp;

namespace {

const char* kShader =
    R"({"ver":"0.1","info":{"id":"XsX3Rr","name":"Test shader","username":"someone"},)"
    R"("renderpass":[{"inputs":[],"outputs":[{"id":"4dfGRr","channel":0}],)"
    R"("code":"void mainImage(out vec4 c, in vec2 f) { c = vec4(1.0); }","name":"Image","type":"image"}]})";

bool has(const std::string& s, const char* needle) { return s.find(needle) != std::string::npos; }

// Accepts one connection, records the request and sends `response` (or nothing, if empty,
// until stopped).
class OneShotServer {
public:
    explicit OneShotServer(std::string response) : response_(std::move(response)) {
        fd_ = socket(AF_INET, SOCK_STREAM, 0);
        if (fd_ < 0) return;
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        socklen_t len = sizeof(a);
        if (bind(fd_, reinterpret_cast<sockaddr*>(&a), sizeof(a)) != 0 || listen(fd_, 1) != 0 ||
            getsockname(fd_, reinterpret_cast<sockaddr*>(&a), &len) != 0) {
            close(fd_);
            fd_ = -1;
            return;
        }
        port_ = ntohs(a.sin_port);
        thread_ = std::thread([this] { serve(); });
    }
    ~OneShotServer() {
        stop_ = true;
        if (fd_ >= 0) shutdown(fd_, SHUT_RDWR);
        if (thread_.joinable()) thread_.join();
        if (fd_ >= 0) close(fd_);
    }
    bool ok() const { return fd_ >= 0; }
    std::string url() const { return "http://127.0.0.1:" + std::to_string(port_) + "/shadertoy"; }
    std::string request() {
        for (int i = 0; i < 200 && !done_; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(10));
        return request_;
    }

private:
    void serve() {
        const int c = accept(fd_, nullptr, nullptr);
        if (c < 0) return;
        timeval tv{0, 200000};
        setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        std::string req;
        char buf[4096];
        while (!stop_) {
            const ssize_t n = recv(c, buf, sizeof(buf), 0);
            if (n > 0) req.append(buf, static_cast<size_t>(n));
            const size_t end = req.find("\r\n\r\n");
            if (end != std::string::npos) {
                const size_t cl = req.find("Content-Length: ");
                const size_t want = cl == std::string::npos ? 0 : std::stoul(req.substr(cl + 16));
                if (req.size() >= end + 4 + want) break;
            }
            if (n == 0) break;
        }
        request_ = req;
        done_ = true;
        if (!response_.empty()) {
            send(c, response_.data(), response_.size(), MSG_NOSIGNAL);
        } else {
            while (!stop_) std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        close(c);
    }

    int fd_ = -1;
    int port_ = 0;
    std::string response_;
    std::string request_;
    std::atomic<bool> stop_{false};
    std::atomic<bool> done_{false};
    std::thread thread_;
};

std::string httpResponse(int status, const std::string& extraHeaders, const std::string& body) {
    return "HTTP/1.1 " + std::to_string(status) + " X\r\nContent-Length: " + std::to_string(body.size()) +
           "\r\nConnection: close\r\n" + extraHeaders + "\r\n" + body;
}

FetchConfig loopbackConfig(const OneShotServer& s) {
    FetchConfig c;
    c.endpoint = s.url();
    c.allowPlainHttpLoopback = true;
    c.timeoutSeconds = 5;
    return c;
}

}  // namespace

TEST(shader_ref_parsing) {
    CHECK(parseShaderRef("XsX3Rr") == std::optional<std::string>("XsX3Rr"));
    CHECK(parseShaderRef("  XsX3Rr \n") == std::optional<std::string>("XsX3Rr"));
    CHECK(parseShaderRef("https://www.shadertoy.com/view/XsX3Rr") == std::optional<std::string>("XsX3Rr"));
    CHECK(parseShaderRef("http://shadertoy.com/view/XsX3Rr/") == std::optional<std::string>("XsX3Rr"));
    CHECK(parseShaderRef("HTTPS://WWW.ShaderToy.com/view/XsX3Rr?foo=1") == std::optional<std::string>("XsX3Rr"));
    CHECK(parseShaderRef("www.shadertoy.com/embed/XsX3Rr#x") == std::optional<std::string>("XsX3Rr"));
    CHECK(parseShaderRef("https://www.shadertoy.com/embed/XsX3Rr?gui=true&t=10") ==
          std::optional<std::string>("XsX3Rr"));
    CHECK(!parseShaderRef(""));
    CHECK(!parseShaderRef("XsX3R"));
    CHECK(!parseShaderRef("XsX3Rr7"));
    CHECK(!parseShaderRef("Xs-3Rr"));
    CHECK(!parseShaderRef("https://example.com/view/XsX3Rr"));
    CHECK(!parseShaderRef("https://shadertoy.com.evil.example/view/XsX3Rr"));
    CHECK(!parseShaderRef("https://www.shadertoy.com/user/XsX3Rr"));
    CHECK(!parseShaderRef("https://www.shadertoy.com/view/XsX3Rr/extra"));
    CHECK(!parseShaderRef("https://www.shadertoy.com/view/"));
}

TEST(shader_link_detection) {
    CHECK(isShaderLink("https://www.shadertoy.com/view/XsX3Rr"));
    CHECK(isShaderLink(" shadertoy.com/view/XsX3Rr "));
    CHECK(!isShaderLink("XsX3Rr"));  // a bare word isn't treated as a link
    CHECK(!isShaderLink("void mainImage(out vec4 c, in vec2 f) {}"));
}

TEST(fetch_request_is_honest) {
    CHECK_EQ(fetchRequestBody("XsX3Rr"), std::string("s=%7B%22shaders%22%3A%5B%22XsX3Rr%22%5D%7D&nt=1&nl=1&np=1"));
    const std::string ua = fetchUserAgent();
    CHECK(ua.rfind(std::string(APP_NAME) + "/" + APP_VERSION, 0) == 0);
    CHECK(has(ua, "libcurl/"));
    CHECK(!has(ua, "Mozilla"));
}

TEST(fetch_classify_responses) {
    using K = FetchResult::Kind;
    const std::string json = std::string("[") + kShader + "]";

    FetchResult r = classifyResponse("XsX3Rr", 403, "HTTP/2 403\r\ncf-mitigated: challenge\r\n", "<html>...</html>");
    CHECK(r.kind == K::Blocked);
    CHECK(has(r.message, "browser check"));
    CHECK(has(r.message, "Import JSON"));
    CHECK(classifyResponse("XsX3Rr", 403, "HTTP/2 403\r\nCF-Mitigated: Challenge\r\n", "").kind == K::Blocked);
    CHECK(classifyResponse("XsX3Rr", 503, "", "<!DOCTYPE html><title>Just a moment...</title>").kind == K::Blocked);
    CHECK(classifyResponse("XsX3Rr", 200, "", "<html><script src='/cdn-cgi/challenge-platform/x'>").kind ==
          K::Blocked);
    // A header that merely mentions the word elsewhere is not the marker.
    CHECK(classifyResponse("XsX3Rr", 500, "x-note: cf-mitigated: challenge\r\n", "").kind == K::BadResponse);

    CHECK(classifyResponse("XsX3Rr", 429, "", "").kind == K::RateLimited);
    CHECK(classifyResponse("XsX3Rr", 404, "", "").kind == K::NotFound);
    r = classifyResponse("XsX3Rr", 500, "", "oops");
    CHECK(r.kind == K::BadResponse);
    CHECK(has(r.message, "HTTP 500"));
    CHECK(classifyResponse("XsX3Rr", 200, "", "[]").kind == K::NotFound);
    CHECK(classifyResponse("XsX3Rr", 200, "", "<html><body>hello</body></html>").kind == K::BadResponse);
    CHECK(classifyResponse("XsX3Rr", 200, "", "not json").kind == K::BadResponse);
    CHECK(classifyResponse("XsX3Rr", 200, "", R"({"Error":"Shader not found"})").kind == K::BadResponse);

    r = classifyResponse("XsX3Rr", 200, "", json);
    CHECK(r.kind == K::Ok);
    CHECK(r.message.empty());
    CHECK(has(r.json, "Test shader"));
    CHECK(classifyResponse("XsX3Rr", 200, "", std::string(R"({"Shader":)") + kShader + "}").kind == K::Ok);

    // With several shaders, the one with the requested ID is picked.
    std::string other = kShader;
    other.replace(other.find("XsX3Rr"), 6, "AbCdEf");
    other.replace(other.find("Test shader"), 11, "Other");
    r = classifyResponse("XsX3Rr", 200, "", "[" + other + "," + kShader + "]");
    CHECK(r.kind == K::Ok);
    CHECK(has(r.json, "Test shader"));
}

TEST(fetch_rate_limiter) {
    FetchRateLimiter l;
    std::string why;
    CHECK(l.allowed(0, &why));
    l.started(0);
    CHECK(!l.allowed(1, &why));
    CHECK(has(why, "few seconds"));
    CHECK(l.allowed(3.5, &why));
    l.finished(FetchResult::Kind::Blocked, 4);
    CHECK(!l.allowed(100, &why));
    CHECK(has(why, "browser check"));
    CHECK(has(why, "4 min"));
    CHECK(l.allowed(305, &why));
    l.started(305);
    l.finished(FetchResult::Kind::RateLimited, 306);
    CHECK(!l.allowed(340, &why));
    CHECK(has(why, "slow down"));
    CHECK(l.allowed(367, &why));
}

TEST(fetch_loopback_ok) {
    OneShotServer s(httpResponse(200, "Content-Type: application/json\r\n", std::string("[") + kShader + "]"));
    if (!s.ok()) SKIP("can't open a loopback socket here");
    std::atomic<bool> cancel{false};
    const FetchResult r = ShadertoyFetcher::fetchBlocking(loopbackConfig(s), "XsX3Rr", cancel);
    CHECK(r.kind == FetchResult::Kind::Ok);
    CHECK_EQ(r.httpStatus, 200L);
    CHECK(has(r.json, "Test shader"));
    const std::string req = s.request();
    CHECK(req.rfind("POST /shadertoy HTTP/1.1\r\n", 0) == 0);
    CHECK(has(req, "Referer: https://www.shadertoy.com/view/XsX3Rr\r\n"));
    CHECK(has(req, ("User-Agent: " + fetchUserAgent() + "\r\n").c_str()));
    CHECK(has(req, "Content-Type: application/x-www-form-urlencoded\r\n"));
    CHECK(has(req, ("\r\n\r\n" + fetchRequestBody("XsX3Rr")).c_str()));
    CHECK(!has(req, "Cookie:"));
}

TEST(fetch_loopback_challenge) {
    OneShotServer s(httpResponse(403, "cf-mitigated: challenge\r\nContent-Type: text/html\r\n",
                                 "<!DOCTYPE html><html><title>Just a moment...</title></html>"));
    if (!s.ok()) SKIP("can't open a loopback socket here");
    std::atomic<bool> cancel{false};
    const FetchResult r = ShadertoyFetcher::fetchBlocking(loopbackConfig(s), "XsX3Rr", cancel);
    CHECK(r.kind == FetchResult::Kind::Blocked);
    CHECK_EQ(r.httpStatus, 403L);
}

TEST(fetch_loopback_too_large) {
    OneShotServer s(httpResponse(200, "", std::string(100000, 'x')));
    if (!s.ok()) SKIP("can't open a loopback socket here");
    FetchConfig c = loopbackConfig(s);
    c.maxBytes = 1000;
    std::atomic<bool> cancel{false};
    const FetchResult r = ShadertoyFetcher::fetchBlocking(c, "XsX3Rr", cancel);
    CHECK(r.kind == FetchResult::Kind::BadResponse);
    CHECK(has(r.message, "too large"));
}

TEST(fetch_loopback_timeout) {
    OneShotServer s("");  // never answers
    if (!s.ok()) SKIP("can't open a loopback socket here");
    FetchConfig c = loopbackConfig(s);
    c.timeoutSeconds = 1;
    std::atomic<bool> cancel{false};
    const auto t0 = std::chrono::steady_clock::now();
    const FetchResult r = ShadertoyFetcher::fetchBlocking(c, "XsX3Rr", cancel);
    CHECK(r.kind == FetchResult::Kind::Network);
    CHECK(has(r.message, "timed out"));
    CHECK(std::chrono::steady_clock::now() - t0 < std::chrono::seconds(4));
}

TEST(fetch_worker_cancel_is_quick) {
    OneShotServer s("");  // never answers
    if (!s.ok()) SKIP("can't open a loopback socket here");
    ShadertoyFetcher f(loopbackConfig(s));
    std::string why;
    REQUIRE(f.start("XsX3Rr", 0, &why));
    CHECK(f.busy());
    CHECK(!f.start("XsX3Rr", 10, &why));  // one at a time
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    f.cancel();
    const auto t0 = std::chrono::steady_clock::now();
    std::optional<FetchResult> r;
    while (!r && std::chrono::steady_clock::now() - t0 < std::chrono::seconds(3)) {
        r = f.poll(1);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    REQUIRE(r.has_value());
    CHECK(r->kind == FetchResult::Kind::Cancelled);
    CHECK(!f.busy());
}

TEST(fetch_refuses_plain_http) {
    FetchConfig c;
    c.endpoint = "http://www.shadertoy.com/shadertoy";  // not HTTPS, not loopback test mode
    std::atomic<bool> cancel{false};
    const FetchResult r = ShadertoyFetcher::fetchBlocking(c, "XsX3Rr", cancel);
    CHECK(r.kind == FetchResult::Kind::Network);
    CHECK_EQ(r.httpStatus, 0L);
}
