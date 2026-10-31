#include "config/settings.h"

#include <nlohmann/json.hpp>

#include <cmath>

#include "platform/files.h"

namespace sp {

using json = nlohmann::json;

namespace {

struct Reader {
    const json& j;
    std::vector<std::string>* warnings;

    void warn(const std::string& m) const {
        if (warnings) warnings->push_back(m);
    }

    void boolean(const char* key, bool* v) const {
        if (!j.contains(key)) return;
        if (j[key].is_boolean()) *v = j[key].get<bool>();
        else warn(std::string("settings: '") + key + "' should be true or false; using the default");
    }
    template <typename T>
    void number(const char* key, T* v, double lo, double hi, bool zeroAllowed = false) const {
        if (!j.contains(key)) return;
        if (!j[key].is_number()) {
            warn(std::string("settings: '") + key + "' should be a number; using the default");
            return;
        }
        const double d = j[key].get<double>();
        if (zeroAllowed && d == 0) {
            *v = 0;
            return;
        }
        if (!std::isfinite(d) || d < lo || d > hi) {
            warn(std::string("settings: '") + key + "' is out of range; using the default");
            return;
        }
        *v = static_cast<T>(d);
    }
    void string(const char* key, std::string* v) const {
        if (!j.contains(key)) return;
        if (j[key].is_string()) *v = j[key].get<std::string>();
        else warn(std::string("settings: '") + key + "' should be text; using the default");
    }
};

}  // namespace

bool Settings::operator==(const Settings& o) const {
    return fpsCap == o.fpsCap && renderScale == o.renderScale && autoAdvance == o.autoAdvance &&
           autoAdvanceSeconds == o.autoAdvanceSeconds && shuffle == o.shuffle && locked == o.locked &&
           crossfade == o.crossfade && crossfadeSeconds == o.crossfadeSeconds && audioSource == o.audioSource &&
           textureOverrides == o.textureOverrides && compileTimeCapMs == o.compileTimeCapMs;
}

Settings settingsFromJson(const json& j, std::vector<std::string>* warnings) {
    Settings s;
    if (!j.is_object()) {
        if (warnings) warnings->push_back("settings: file is not a JSON object; using defaults");
        return s;
    }
    const Reader r{j, warnings};
    r.number("fps_cap", &s.fpsCap, 10, 500, true);
    r.number("render_scale", &s.renderScale, 0.25, 2.0);
    r.boolean("auto_advance", &s.autoAdvance);
    r.number("auto_advance_seconds", &s.autoAdvanceSeconds, 5, 3600);
    r.boolean("shuffle", &s.shuffle);
    r.boolean("locked", &s.locked);
    r.boolean("crossfade", &s.crossfade);
    r.number("crossfade_seconds", &s.crossfadeSeconds, 0.1, 30);
    r.string("audio_source", &s.audioSource);
    r.number("compile_time_cap_ms", &s.compileTimeCapMs, 100, 60000, true);
    if (j.contains("texture_overrides")) {
        const json& o = j["texture_overrides"];
        if (!o.is_object()) {
            r.warn("settings: 'texture_overrides' should be an object; ignored");
        } else {
            for (auto it = o.begin(); it != o.end(); ++it)
                if (it.value().is_string() && !it.value().get<std::string>().empty())
                    s.textureOverrides[it.key()] = it.value().get<std::string>();
        }
    }
    return s;
}

json settingsToJson(const Settings& s) {
    json j;
    j["fps_cap"] = s.fpsCap;
    j["render_scale"] = s.renderScale;
    j["auto_advance"] = s.autoAdvance;
    j["auto_advance_seconds"] = s.autoAdvanceSeconds;
    j["shuffle"] = s.shuffle;
    j["locked"] = s.locked;
    j["crossfade"] = s.crossfade;
    j["crossfade_seconds"] = s.crossfadeSeconds;
    j["audio_source"] = s.audioSource;
    j["texture_overrides"] = json::object();
    for (const auto& [id, path] : s.textureOverrides) j["texture_overrides"][id] = path;
    j["compile_time_cap_ms"] = s.compileTimeCapMs;
    return j;
}

bool loadSettings(const fs::path& path, Settings* out, std::vector<std::string>* warnings) {
    *out = Settings{};
    std::error_code ec;
    if (!fs::exists(path, ec)) return true;
    std::string text, error;
    if (!readTextFile(path, &text, &error, 1u << 20)) {
        if (warnings) warnings->push_back("settings: " + error + "; using defaults");
        return false;
    }
    const json j = json::parse(text, nullptr, false);
    if (j.is_discarded()) {
        if (warnings) warnings->push_back("settings: " + path.string() + " is not valid JSON; using defaults");
        return false;
    }
    *out = settingsFromJson(j, warnings);
    return true;
}

bool saveSettings(const fs::path& path, const Settings& s, std::string* error) {
    return writeFileAtomically(path, settingsToJson(s).dump(2) + "\n", error);
}

Session loadSession(const fs::path& path) {
    Session s;
    std::string text;
    if (!readTextFile(path, &text, nullptr, 1u << 16)) return s;
    const json j = json::parse(text, nullptr, false);
    if (!j.is_object()) return s;
    if (j.contains("last_shader") && j["last_shader"].is_string()) s.lastShader = j["last_shader"].get<std::string>();
    if (j.contains("playlist") && j["playlist"].is_string()) s.playlist = j["playlist"].get<std::string>();
    return s;
}

bool saveSession(const fs::path& path, const Session& s, std::string* error) {
    const json j = {{"last_shader", s.lastShader}, {"playlist", s.playlist}};
    return writeFileAtomically(path, j.dump(2) + "\n", error);
}

double advanceSecondsFor(const Settings& s, double shaderDuration) {
    return shaderDuration > 0 ? shaderDuration : s.autoAdvanceSeconds;
}

}  // namespace sp
