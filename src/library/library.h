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
    std::string source;     // "paste", "import", "fetch"
    fs::path path;
    std::string loadError;  // set if the file can't be parsed (entry still listed)
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

    // "local-1a2b3c4d": ids for shaders that don't come from shadertoy.com.
    static std::string newLocalId();
    // Only [A-Za-z0-9_-]; anything else would be unsafe as a file name.
    static bool isSafeId(const std::string& id);

private:
    bool writeAtomically(const fs::path& path, const std::string& text, std::string* error) const;

    fs::path dir_;
    std::vector<LibraryEntry> entries_;
};

}  // namespace sp
