#pragma once
// Saved playlists: one JSON file per playlist in <data>/playlists/, holding
// the name and an ordered list of library ids. "All shaders" and "Favorites"
// are built in and not stored here.

#include <filesystem>
#include <string>
#include <vector>

namespace sp {

namespace fs = std::filesystem;

struct Playlist {
    std::string name;
    std::vector<std::string> ids;
    fs::path path;
};

class PlaylistStore {
public:
    explicit PlaylistStore(fs::path dir) : dir_(std::move(dir)) {}

    void scan();
    const std::vector<Playlist>& playlists() const { return lists_; }
    const Playlist* find(const std::string& name) const;
    const fs::path& dir() const { return dir_; }

    bool create(const std::string& name, std::string* error);
    bool rename(const std::string& from, const std::string& to, std::string* error);
    bool remove(const std::string& name, std::string* error);

    // Adding an id that is already there does nothing (and succeeds).
    bool addShader(const std::string& name, const std::string& id, std::string* error);
    bool removeShader(const std::string& name, const std::string& id, std::string* error);
    // Moves the entry at `index` by `delta` places (clamped).
    bool move(const std::string& name, size_t index, int delta, std::string* error);
    // Drops a deleted shader from every playlist.
    void forgetShader(const std::string& id);

    // "" if the name is usable, else why not. Names are trimmed by the caller.
    std::string checkName(const std::string& name, const std::string& ignoring = "") const;
    // File name for a playlist name: letters, digits, '-', '_' kept, others become '_'.
    static std::string fileStem(const std::string& name);

private:
    Playlist* findMutable(const std::string& name);
    bool save(const Playlist& p, std::string* error);

    fs::path dir_;
    std::vector<Playlist> lists_;
};

}  // namespace sp
