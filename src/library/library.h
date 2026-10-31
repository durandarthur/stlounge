#pragma once
// Shader library: one JSON file per shader in <data>/shaders/<id>.json, in
// ShaderToy's website format. Imported JSON is kept as-is (lossless); the app
// adds only a "shaderplayer" object with its own metadata.

#include <nlohmann/json_fwd.hpp>

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "shadertoy/shader.h"

namespace sp {

namespace fs = std::filesystem;

struct LibraryEntry {
    std::string id;
    std::string name;
    std::string author;
    std::string url;
    std::string source;     // "paste" or "import"
    fs::path path;
    std::string loadError;  // set if the file can't be parsed (entry still listed)
    bool favorite = false;
    double duration = 0;    // auto-advance override in seconds, 0 = use the global setting
    bool crashed = false;   // the app died while starting it (crash guard); skipped until retried
};

class ShaderLibrary {
public:
    explicit ShaderLibrary(fs::path dir) : dir_(std::move(dir)) {}

    // Reads every *.json in the folder. Broken files are listed with loadError.
    void scan();
    const std::vector<LibraryEntry>& entries() const { return entries_; }
    const LibraryEntry* find(const std::string& id) const;
    const fs::path& dir() const { return dir_; }

    // Parses the stored file. `warnings` gets import warnings (unknown textures...).
    std::optional<Shader> load(const std::string& id, std::vector<std::string>* warnings, std::string* error) const;

    // Stores one shader object (website or API shape). Keeps its ShaderToy id,
    // or assigns a local one. Replaces an existing entry with the same id.
    // Returns the id, or "" on failure. rescan=false defers the folder rescan
    // (call scan() once after a bulk operation).
    std::string add(const nlohmann::json& shaderObject, const std::string& source, std::string* error,
                    bool rescan = true);
    // Stores a shader built in the app (paste dialog).
    std::string addShader(const Shader& shader, const std::string& source, std::string* error);

    bool remove(const std::string& id, std::string* error, bool rescan = true);

    // Saves edits to an existing shader: its tabs, channels, name and author. The rest
    // of the file is kept (ShaderToy metadata, favorite, play time, and tabs this app
    // can't show, like Sound). Clears the crashed flag, since the code changed.
    bool update(const std::string& id, const Shader& edited, std::string* error);

    // Per-shader settings, stored in the file's "shaderplayer" object. They
    // update the in-memory entry too, so no rescan is needed.
    bool setFavorite(const std::string& id, bool favorite, std::string* error);
    bool setDuration(const std::string& id, double seconds, std::string* error);
    bool setCrashed(const std::string& id, bool crashed, std::string* error);

    // "local-1a2b3c4d": ids for shaders that don't come from shadertoy.com.
    static std::string newLocalId();
    // Only [A-Za-z0-9_-]; anything else would be unsafe as a file name.
    static bool isSafeId(const std::string& id);

private:
    // Rewrites one field of the "shaderplayer" object (null removes it).
    bool setMeta(const std::string& id, const char* key, const nlohmann::json& value, std::string* error);
    LibraryEntry* findMutable(const std::string& id);

    fs::path dir_;
    std::vector<LibraryEntry> entries_;
};

}  // namespace sp
