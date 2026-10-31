#include "shadertoy/shader_ref.h"

#include <cctype>

#include "shadertoy/json_format.h"

namespace sp {

namespace {

std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

bool startsWithNoCase(const std::string& s, const std::string& prefix) {
    if (s.size() < prefix.size()) return false;
    for (size_t i = 0; i < prefix.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(s[i])) != prefix[i]) return false;
    return true;
}

// The ID from a link, or nullopt when `s` isn't a shadertoy.com shader link.
std::optional<std::string> idFromLink(std::string s) {
    for (const char* scheme : {"https://", "http://"})
        if (startsWithNoCase(s, scheme)) {
            s = s.substr(std::string(scheme).size());
            break;
        }
    if (startsWithNoCase(s, "www.")) s = s.substr(4);
    if (!startsWithNoCase(s, "shadertoy.com/")) return std::nullopt;
    s = s.substr(14);
    bool known = false;
    for (const char* kind : {"view/", "embed/"})
        if (startsWithNoCase(s, kind)) {
            s = s.substr(std::string(kind).size());
            known = true;
            break;
        }
    if (!known) return std::nullopt;
    const size_t end = s.find_first_of("/?#");
    if (end != std::string::npos) {
        // Only an empty remainder, a query or a fragment may follow the ID.
        if (s[end] == '/' && end + 1 < s.size() && s[end + 1] != '?' && s[end + 1] != '#') return std::nullopt;
        s = s.substr(0, end);
    }
    if (!isShadertoyId(s)) return std::nullopt;
    return s;
}

}  // namespace

std::optional<std::string> parseShaderRef(const std::string& input) {
    const std::string s = trim(input);
    if (isShadertoyId(s)) return s;
    return idFromLink(s);
}

bool isShaderLink(const std::string& input) { return idFromLink(trim(input)).has_value(); }

}  // namespace sp
