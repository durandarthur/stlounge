#include "library/library.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <ctime>
#include <random>

#include "platform/files.h"
#include "platform/log.h"
#include "shadertoy/json_format.h"

namespace sp {

using json = nlohmann::json;

namespace {

// The app's own data inside a shader file. Fixed, not APP_NAME, so renaming the app
// never orphans it. Files written before the app had its name use the legacy key.
constexpr const char* kMetaKey = "stlounge";
constexpr const char* kLegacyMetaKey = "shaderplayer";

// The metadata object of a parsed file, or nullptr.
const json* metaOf(const json& j) {
    for (const char* key : {kMetaKey, kLegacyMetaKey})
        if (j.contains(key) && j[key].is_object()) return &j[key];
    return nullptr;
}

// The metadata object to write to, moving a legacy one under the current key.
json& metaFor(json& j) {
    if (!(j.contains(kMetaKey) && j[kMetaKey].is_object())) {
        json m = j.contains(kLegacyMetaKey) && j[kLegacyMetaKey].is_object() ? j[kLegacyMetaKey] : json::object();
        j[kMetaKey] = std::move(m);
    }
    j.erase(kLegacyMetaKey);
    return j[kMetaKey];
}

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string isoNow() {
    const std::time_t t = std::time(nullptr);
    std::tm tm{};
    gmtime_r(&t, &tm);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
    return buf;
}

}  // namespace

bool ShaderLibrary::isSafeId(const std::string& id) {
    if (id.empty() || id.size() > 64) return false;
    for (char c : id)
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '-' && c != '_') return false;
    return true;
}

std::string ShaderLibrary::newLocalId() {
    static std::mt19937_64 rng(std::random_device{}() ^
                               static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count()));
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%08x", static_cast<unsigned>(rng() & 0xffffffffu));
    return std::string("local-") + buf;
}

void ShaderLibrary::scan() {
    entries_.clear();
    std::error_code ec;
    fs::create_directories(dir_, ec);
    for (const auto& de : fs::directory_iterator(dir_, ec)) {
        if (!de.is_regular_file() || de.path().extension() != ".json") continue;
        LibraryEntry e;
        e.path = de.path();
        e.id = de.path().stem().string();
        std::string text, err;
        if (!readTextFile(e.path, &text, &err)) {
            e.loadError = err;
        } else {
            json j = json::parse(text, nullptr, false);
            if (j.is_discarded() || !j.is_object()) {
                e.loadError = "not valid JSON";
            } else {
                const json& info = j.contains("info") && j["info"].is_object() ? j["info"] : json::object();
                e.name = info.value("name", "");
                e.author = info.value("username", "");
                if (e.author == "None") e.author.clear();
                if (isShadertoyId(e.id)) e.url = "https://www.shadertoy.com/view/" + e.id;
                if (const json* meta = metaOf(j)) {
                    const json& m = *meta;
                    e.source = m.value("source", "");
                    if (m.contains("favorite") && m["favorite"].is_boolean()) e.favorite = m["favorite"].get<bool>();
                    if (m.contains("crashed") && m["crashed"].is_boolean()) e.crashed = m["crashed"].get<bool>();
                    if (m.contains("duration") && m["duration"].is_number()) {
                        const double d = m["duration"].get<double>();
                        if (d > 0 && d < 1e6) e.duration = d;
                    }
                }
            }
        }
        if (e.name.empty()) e.name = e.id;
        if (!e.loadError.empty()) LOG_WARN("library: %s: %s", e.path.c_str(), e.loadError.c_str());
        entries_.push_back(std::move(e));
    }
    if (ec) LOG_WARN("library: cannot read %s: %s", dir_.c_str(), ec.message().c_str());
    std::sort(entries_.begin(), entries_.end(),
              [](const LibraryEntry& a, const LibraryEntry& b) { return lower(a.name) < lower(b.name); });
}

LibraryEntry* ShaderLibrary::findMutable(const std::string& id) {
    for (LibraryEntry& e : entries_)
        if (e.id == id) return &e;
    return nullptr;
}

const LibraryEntry* ShaderLibrary::find(const std::string& id) const {
    for (const LibraryEntry& e : entries_)
        if (e.id == id) return &e;
    return nullptr;
}

std::optional<Shader> ShaderLibrary::load(const std::string& id, std::vector<std::string>* warnings,
                                          std::string* error) const {
    if (!isSafeId(id)) {
        if (error) *error = "invalid shader id";
        return std::nullopt;
    }
    std::string text;
    if (!readTextFile(dir_ / (id + ".json"), &text, error)) return std::nullopt;
    std::vector<ImportResult> results = parseShadertoyJson(text);
    if (results.empty() || !results[0].ok) {
        if (error) *error = results.empty() ? "empty file" : results[0].error;
        return std::nullopt;
    }
    if (warnings) *warnings = results[0].warnings;
    Shader s = results[0].shader;
    s.info.id = id;  // the file name is authoritative (local ids aren't in the JSON)
    return s;
}

std::string ShaderLibrary::add(const json& shaderObject, const std::string& source, std::string* error,
                               bool rescan) {
    json obj = shaderObject;
    if (obj.contains("Shader") && obj["Shader"].is_object()) obj = obj["Shader"];
    ImportResult check = shaderFromJson(obj);
    if (!check.ok) {
        if (error) *error = check.error;
        return "";
    }
    std::string id = check.shader.info.id;
    if (!isShadertoyId(id)) {
        // Keep a previous local id when re-saving our own file, else make one.
        const json* m = metaOf(obj);
        const std::string prev = m ? m->value("local_id", "") : "";
        id = isSafeId(prev) ? prev : newLocalId();
    }
    json& meta = metaFor(obj);
    meta["source"] = source;
    if (!meta.contains("added")) meta["added"] = isoNow();
    if (!isShadertoyId(check.shader.info.id)) meta["local_id"] = id;

    if (!writeFileAtomically(dir_ / (id + ".json"), obj.dump(2), error)) return "";
    LOG_INFO("library: saved '%s' as %s.json", check.shader.info.name.c_str(), id.c_str());
    if (rescan) scan();
    return id;
}

std::string ShaderLibrary::addShader(const Shader& shader, const std::string& source, std::string* error) {
    json obj = shaderToJson(shader);
    if (!isShadertoyId(shader.info.id) && isSafeId(shader.info.id))
        obj[kMetaKey] = {{"local_id", shader.info.id}};
    return add(obj, source, error);
}

bool ShaderLibrary::remove(const std::string& id, std::string* error, bool rescan) {
    if (!isSafeId(id)) {
        if (error) *error = "invalid shader id";
        return false;
    }
    std::error_code ec;
    if (!fs::remove(dir_ / (id + ".json"), ec) || ec) {
        if (error) *error = ec ? ec.message() : "no such shader";
        return false;
    }
    LOG_INFO("library: removed %s", id.c_str());
    if (rescan) scan();
    return true;
}

bool ShaderLibrary::update(const std::string& id, const Shader& edited, std::string* error) {
    if (!isSafeId(id)) {
        if (error) *error = "invalid shader id";
        return false;
    }
    const fs::path path = dir_ / (id + ".json");
    std::string text;
    if (!readTextFile(path, &text, error)) return false;
    json j = json::parse(text, nullptr, false);
    if (j.is_discarded() || !j.is_object()) {
        if (error) *error = path.string() + " is not valid JSON";
        return false;
    }
    json passes = shaderToJson(edited)["renderpass"];
    if (j.contains("renderpass") && j["renderpass"].is_array())
        for (const json& p : j["renderpass"]) {
            const std::string type = p.is_object() ? p.value("type", "") : "";
            if (type != "image" && type != "buffer" && type != "common" && type != "volume") passes.push_back(p);
        }
    j["renderpass"] = passes;
    if (!j.contains("info") || !j["info"].is_object()) j["info"] = json::object();
    j["info"]["name"] = edited.info.name;
    j["info"]["username"] = edited.info.author;
    json& meta = metaFor(j);
    meta["edited"] = isoNow();
    meta.erase("crashed");
    if (!isShadertoyId(id)) meta["local_id"] = id;

    // Refuse to store something the app can't read back.
    if (!shaderFromJson(j).ok) {
        if (error) *error = "the edited shader can't be stored";
        return false;
    }
    if (!writeFileAtomically(path, j.dump(2), error)) return false;
    LOG_INFO("library: saved changes to %s.json", id.c_str());
    scan();
    return true;
}

bool ShaderLibrary::setMeta(const std::string& id, const char* key, const json& value, std::string* error) {
    if (!isSafeId(id)) {
        if (error) *error = "invalid shader id";
        return false;
    }
    const fs::path path = dir_ / (id + ".json");
    std::string text;
    if (!readTextFile(path, &text, error)) return false;
    json j = json::parse(text, nullptr, false);
    if (j.is_discarded() || !j.is_object()) {
        if (error) *error = path.string() + " is not valid JSON";
        return false;
    }
    json& meta = metaFor(j);
    if (value.is_null())
        meta.erase(key);
    else
        meta[key] = value;
    return writeFileAtomically(path, j.dump(2), error);
}

bool ShaderLibrary::setFavorite(const std::string& id, bool favorite, std::string* error) {
    if (!setMeta(id, "favorite", favorite ? json(true) : json(nullptr), error)) return false;
    if (LibraryEntry* e = findMutable(id)) e->favorite = favorite;
    return true;
}

bool ShaderLibrary::setDuration(const std::string& id, double seconds, std::string* error) {
    if (!(seconds >= 0) || seconds > 1e6) {
        if (error) *error = "invalid duration";
        return false;
    }
    if (!setMeta(id, "duration", seconds > 0 ? json(seconds) : json(nullptr), error)) return false;
    if (LibraryEntry* e = findMutable(id)) e->duration = seconds;
    return true;
}

bool ShaderLibrary::setCrashed(const std::string& id, bool crashed, std::string* error) {
    if (!setMeta(id, "crashed", crashed ? json(true) : json(nullptr), error)) return false;
    if (LibraryEntry* e = findMutable(id)) e->crashed = crashed;
    return true;
}

}  // namespace sp
