#pragma once
// XDG base directory resolution for the app's folders.

#include <filesystem>
#include <functional>
#include <optional>
#include <string>

namespace sp {

namespace fs = std::filesystem;

// Looks up an environment variable; returns nullptr if unset.
using EnvLookup = std::function<const char*(const char*)>;
EnvLookup processEnv();

struct AppPaths {
    fs::path configDir;    // settings          ($XDG_CONFIG_HOME/<app>)
    fs::path dataDir;      // library, playlists ($XDG_DATA_HOME/<app>)
    fs::path stateDir;     // log, crash marker  ($XDG_STATE_HOME/<app>)
    fs::path picturesDir;  // screenshots        (XDG_PICTURES_DIR/<app>)
    bool usingFallback = false;  // true if XDG folders were unusable
};

// Home directory: $HOME, else the passwd entry, else empty.
fs::path homeDir(const EnvLookup& env);

// Pure resolution, no filesystem access except reading user-dirs.dirs.
AppPaths resolveAppPaths(const std::string& appName, const EnvLookup& env);

// Parses XDG_PICTURES_DIR from the contents of user-dirs.dirs.
std::optional<fs::path> parsePicturesDir(const std::string& userDirsContents, const fs::path& home);

// Creates config/data/state folders. If that fails, switches to
// "<cwd>/<app>-data/..." and sets usingFallback. Returns false only if
// even the fallback can't be created. `message` explains what happened.
bool ensureAppDirs(AppPaths& paths, const std::string& appName, std::string* message);

}  // namespace sp
