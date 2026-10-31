#include "library/playlists.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>

#include "library/library.h"
#include "platform/files.h"
#include "platform/log.h"

namespace sp {

using json = nlohmann::json;

namespace {

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

}  // namespace

std::string PlaylistStore::fileStem(const std::string& name) {
    std::string out;
    for (char c : name) {
        const auto u = static_cast<unsigned char>(c);
        out += (std::isalnum(u) || c == '-' || c == '_') && u < 128 ? static_cast<char>(std::tolower(u)) : '_';
    }
    return out;
}

void PlaylistStore::scan() {
    lists_.clear();
    std::error_code ec;
    fs::create_directories(dir_, ec);
    for (const auto& de : fs::directory_iterator(dir_, ec)) {
        if (!de.is_regular_file() || de.path().extension() != ".json") continue;
        std::string text, error;
        if (!readTextFile(de.path(), &text, &error, 16u << 20)) {
            LOG_WARN("playlists: %s", error.c_str());
            continue;
        }
        const json j = json::parse(text, nullptr, false);
        if (!j.is_object() || !j.contains("name") || !j["name"].is_string()) {
            LOG_WARN("playlists: %s is not a playlist file; ignored", de.path().c_str());
            continue;
        }
        Playlist p;
        p.name = j["name"].get<std::string>();
        p.path = de.path();
        if (j.contains("shaders") && j["shaders"].is_array())
            for (const json& id : j["shaders"])
                if (id.is_string() && ShaderLibrary::isSafeId(id.get<std::string>()) &&
                    std::find(p.ids.begin(), p.ids.end(), id.get<std::string>()) == p.ids.end())
                    p.ids.push_back(id.get<std::string>());
        if (p.name.empty() || find(p.name)) {
            LOG_WARN("playlists: %s has an empty or duplicate name; ignored", de.path().c_str());
            continue;
        }
        lists_.push_back(std::move(p));
    }
    std::sort(lists_.begin(), lists_.end(),
              [](const Playlist& a, const Playlist& b) { return lower(a.name) < lower(b.name); });
}

const Playlist* PlaylistStore::find(const std::string& name) const {
    for (const Playlist& p : lists_)
        if (p.name == name) return &p;
    return nullptr;
}

Playlist* PlaylistStore::findMutable(const std::string& name) {
    for (Playlist& p : lists_)
        if (p.name == name) return &p;
    return nullptr;
}

std::string PlaylistStore::checkName(const std::string& name, const std::string& ignoring) const {
    if (name.empty()) return "The name is empty.";
    if (name.size() > 64) return "The name is too long (64 characters at most).";
    if (name.front() == '*' || name.front() == ' ' || name.back() == ' ') return "The name can't start with '*' or a space.";
    for (char c : name)
        if (static_cast<unsigned char>(c) < 32) return "The name contains control characters.";
    const std::string l = lower(name);
    if (l == "all shaders" || l == "favorites" || l == "favourites") return "That name is reserved.";
    const std::string stem = fileStem(name);
    for (const Playlist& p : lists_) {
        if (p.name == ignoring) continue;
        if (lower(p.name) == l) return "A playlist with that name exists.";
        if (fileStem(p.name) == stem) return "Too similar to the playlist '" + p.name + "'.";
    }
    return "";
}

bool PlaylistStore::save(const Playlist& p, std::string* error) {
    const json j = {{"name", p.name}, {"shaders", p.ids}};
    return writeFileAtomically(p.path, j.dump(2) + "\n", error);
}

bool PlaylistStore::create(const std::string& name, std::string* error) {
    if (std::string why = checkName(name); !why.empty()) {
        if (error) *error = why;
        return false;
    }
    Playlist p;
    p.name = name;
    p.path = dir_ / (fileStem(name) + ".json");
    if (!save(p, error)) return false;
    scan();
    return true;
}

bool PlaylistStore::rename(const std::string& from, const std::string& to, std::string* error) {
    Playlist* p = findMutable(from);
    if (!p) {
        if (error) *error = "no playlist named '" + from + "'";
        return false;
    }
    if (from == to) return true;
    if (std::string why = checkName(to, from); !why.empty()) {
        if (error) *error = why;
        return false;
    }
    Playlist moved = *p;
    moved.name = to;
    moved.path = dir_ / (fileStem(to) + ".json");
    if (!save(moved, error)) return false;
    std::error_code ec;
    if (moved.path != p->path) fs::remove(p->path, ec);
    scan();
    return true;
}

bool PlaylistStore::remove(const std::string& name, std::string* error) {
    const Playlist* p = find(name);
    if (!p) {
        if (error) *error = "no playlist named '" + name + "'";
        return false;
    }
    std::error_code ec;
    if (!fs::remove(p->path, ec) || ec) {
        if (error) *error = "cannot delete " + p->path.string() + (ec ? ": " + ec.message() : "");
        return false;
    }
    scan();
    return true;
}

bool PlaylistStore::addShader(const std::string& name, const std::string& id, std::string* error) {
    Playlist* p = findMutable(name);
    if (!p || !ShaderLibrary::isSafeId(id)) {
        if (error) *error = p ? "invalid shader id" : "no playlist named '" + name + "'";
        return false;
    }
    if (std::find(p->ids.begin(), p->ids.end(), id) != p->ids.end()) return true;
    p->ids.push_back(id);
    if (save(*p, error)) return true;
    p->ids.pop_back();
    return false;
}

bool PlaylistStore::removeShader(const std::string& name, const std::string& id, std::string* error) {
    Playlist* p = findMutable(name);
    if (!p) {
        if (error) *error = "no playlist named '" + name + "'";
        return false;
    }
    const auto before = p->ids;
    p->ids.erase(std::remove(p->ids.begin(), p->ids.end(), id), p->ids.end());
    if (p->ids.size() == before.size()) return true;
    if (save(*p, error)) return true;
    p->ids = before;
    return false;
}

bool PlaylistStore::move(const std::string& name, size_t index, int delta, std::string* error) {
    Playlist* p = findMutable(name);
    if (!p || index >= p->ids.size()) {
        if (error) *error = p ? "no such entry" : "no playlist named '" + name + "'";
        return false;
    }
    const long target = std::clamp(static_cast<long>(index) + delta, 0L, static_cast<long>(p->ids.size()) - 1);
    if (target == static_cast<long>(index)) return true;
    const auto before = p->ids;
    const std::string id = p->ids[index];
    p->ids.erase(p->ids.begin() + static_cast<long>(index));
    p->ids.insert(p->ids.begin() + target, id);
    if (save(*p, error)) return true;
    p->ids = before;
    return false;
}

void PlaylistStore::forgetShader(const std::string& id) {
    for (Playlist& p : lists_) {
        if (std::find(p.ids.begin(), p.ids.end(), id) == p.ids.end()) continue;
        std::string error;
        if (!removeShader(p.name, id, &error)) LOG_WARN("playlists: %s", error.c_str());
    }
}

}  // namespace sp
