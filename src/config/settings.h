#pragma once
// User settings (config dir, settings.json) and session state (state dir,
// session.json). Loading is lenient: unknown keys are ignored, and a missing
// or out-of-range value falls back to its default with a warning.

#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include <nlohmann/json_fwd.hpp>

namespace sp {

namespace fs = std::filesystem;

struct Settings {
    // Display
    int fpsCap = 0;             // 0 = display refresh (vsync); else 10..500
    float renderScale = 1.0f;   // 0.25..2

    // Playlist
    bool autoAdvance = true;
    double autoAdvanceSeconds = 120;  // 5..3600, a shader's own duration overrides it
    bool shuffle = false;
    bool locked = false;              // no auto-advance and no transitions
    bool crossfade = false;           // off by default: renders two shaders during the fade
    double crossfadeSeconds = 2.0;    // 0.1..30

    // Audio
    std::string audioSource;  // "" = monitor of the default output (follows changes)

    // Built-in texture id -> image file used instead
    std::map<std::string, std::string> textureOverrides;

    // Advanced
    int compileTimeCapMs = 0;  // 0 = off; else 100..60000 (needs parallel shader compile)
    int frameTimeGuardMs = 1000;  // skip a shader whose frames take longer on the GPU; 0 = off, else 200..10000

    bool operator==(const Settings& o) const;
    bool operator!=(const Settings& o) const { return !(*this == o); }
};

// What playing looks like when the app is restarted.
struct Session {
    std::string lastShader;  // library id
    std::string playlist;    // "" = all shaders, kFavoritesPlaylist, or a playlist name
};

inline constexpr const char* kFavoritesPlaylist = "*favorites";

Settings settingsFromJson(const nlohmann::json& j, std::vector<std::string>* warnings);
nlohmann::json settingsToJson(const Settings& s);

// A missing file gives defaults and returns true. An unreadable or invalid
// file gives defaults, returns false and explains in `warnings`.
bool loadSettings(const fs::path& path, Settings* out, std::vector<std::string>* warnings);
bool saveSettings(const fs::path& path, const Settings& s, std::string* error);

Session loadSession(const fs::path& path);
bool saveSession(const fs::path& path, const Session& s, std::string* error);

// Seconds a shader plays before auto-advance: its own duration if set, else the global one.
double advanceSecondsFor(const Settings& s, double shaderDuration);

}  // namespace sp
