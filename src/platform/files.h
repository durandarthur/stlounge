#pragma once
// Small file helpers shared by the library, settings and playlists.

#include <filesystem>
#include <string>

namespace sp {

namespace fs = std::filesystem;

// Reads a whole file. Fails (with a message) if it can't be opened or is larger than maxBytes.
bool readTextFile(const fs::path& path, std::string* out, std::string* error,
                  std::uintmax_t maxBytes = 64u * 1024u * 1024u);

// Writes `text` to path.tmp, then renames it over `path`, so a crash or full
// disk never leaves a half-written file. Creates the parent folder.
bool writeFileAtomically(const fs::path& path, const std::string& text, std::string* error);

}  // namespace sp
