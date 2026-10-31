#include "platform/paths.h"

#include <pwd.h>
#include <unistd.h>

#include <cstdlib>
#include <fstream>
#include <sstream>
#include <system_error>

namespace sp {

EnvLookup processEnv() {
    return [](const char* name) -> const char* { return std::getenv(name); };
}

fs::path homeDir(const EnvLookup& env) {
    const char* home = env("HOME");
    if (home && *home) return fs::path(home);
    if (const passwd* pw = getpwuid(getuid()); pw && pw->pw_dir) return fs::path(pw->pw_dir);
    return {};
}

namespace {

// XDG spec: a relative path in an XDG_* variable is invalid and must be ignored.
fs::path xdgDir(const EnvLookup& env, const char* var, const fs::path& fallback) {
    const char* v = env(var);
    if (v && *v && fs::path(v).is_absolute()) return fs::path(v);
    return fallback;
}

}  // namespace

std::optional<fs::path> parsePicturesDir(const std::string& contents, const fs::path& home) {
    std::istringstream in(contents);
    std::string line;
    const std::string key = "XDG_PICTURES_DIR=";
    while (std::getline(in, line)) {
        size_t start = line.find_first_not_of(" \t");
        if (start == std::string::npos || line[start] == '#') continue;
        if (line.compare(start, key.size(), key) != 0) continue;
        std::string value = line.substr(start + key.size());
        if (value.size() >= 2 && value.front() == '"' && value.back() == '"')
            value = value.substr(1, value.size() - 2);
        if (value.rfind("$HOME", 0) == 0) {
            std::string rest = value.substr(5);
            if (!rest.empty() && rest.front() == '/') rest.erase(0, 1);
            return rest.empty() ? home : home / rest;
        }
        if (!value.empty() && value.front() == '/') return fs::path(value);
        return std::nullopt;  // malformed
    }
    return std::nullopt;
}

AppPaths resolveAppPaths(const std::string& appName, const EnvLookup& env) {
    const fs::path home = homeDir(env);
    const fs::path configHome = xdgDir(env, "XDG_CONFIG_HOME", home / ".config");
    const fs::path dataHome = xdgDir(env, "XDG_DATA_HOME", home / ".local/share");
    const fs::path stateHome = xdgDir(env, "XDG_STATE_HOME", home / ".local/state");

    fs::path pictures = home / "Pictures";
    if (const char* p = env("XDG_PICTURES_DIR"); p && *p && fs::path(p).is_absolute()) {
        pictures = p;
    } else {
        std::ifstream f(configHome / "user-dirs.dirs");
        if (f) {
            std::stringstream ss;
            ss << f.rdbuf();
            if (auto parsed = parsePicturesDir(ss.str(), home)) pictures = *parsed;
        }
    }

    AppPaths out;
    out.configDir = configHome / appName;
    out.dataDir = dataHome / appName;
    out.stateDir = stateHome / appName;
    out.picturesDir = pictures / appName;
    return out;
}

namespace {

bool makeDirs(const std::initializer_list<fs::path>& dirs, std::string* error) {
    for (const auto& d : dirs) {
        std::error_code ec;
        fs::create_directories(d, ec);
        if (ec || !fs::is_directory(d)) {
            if (error) *error = d.string() + ": " + (ec ? ec.message() : "not a directory");
            return false;
        }
    }
    return true;
}

}  // namespace

bool ensureAppDirs(AppPaths& paths, const std::string& appName, std::string* message) {
    std::string error;
    if (!paths.configDir.empty() && makeDirs({paths.configDir, paths.dataDir, paths.stateDir}, &error))
        return true;

    std::error_code ec;
    fs::path base = fs::current_path(ec) / (appName + "-data");
    AppPaths fb;
    fb.configDir = base / "config";
    fb.dataDir = base / "data";
    fb.stateDir = base / "state";
    fb.picturesDir = base / "screenshots";
    fb.usingFallback = true;
    std::string fbError;
    if (!makeDirs({fb.configDir, fb.dataDir, fb.stateDir}, &fbError)) {
        if (message) *message = "cannot create XDG folders (" + error + ") nor fallback (" + fbError + ")";
        return false;
    }
    if (message) *message = "cannot create XDG folders (" + error + "); using " + base.string();
    paths = fb;
    return true;
}

std::vector<fs::path> assetDirCandidates(const fs::path& exe, const std::string& appName,
                                         const fs::path& sourceAssets) {
    std::vector<fs::path> out;
    if (!exe.empty()) out.push_back(exe.parent_path().parent_path() / "share" / appName);
    if (!sourceAssets.empty()) out.push_back(sourceAssets);
    return out;
}

fs::path findAssetsDir(const std::string& appName) {
    std::error_code ec;
    const fs::path exe = fs::read_symlink("/proc/self/exe", ec);
#ifdef SP_SOURCE_ASSETS_DIR
    const fs::path source = SP_SOURCE_ASSETS_DIR;
#else
    const fs::path source;
#endif
    for (const fs::path& d : assetDirCandidates(ec ? fs::path() : exe, appName, source))
        if (fs::is_directory(d / "textures", ec)) return d;
    return {};
}

}  // namespace sp
