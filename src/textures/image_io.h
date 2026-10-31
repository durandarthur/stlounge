#pragma once
// Image files: loading user textures (PNG, JPEG, BMP, TGA) and saving PNG screenshots.

#include <filesystem>
#include <string>

#include "textures/procedural.h"

namespace sp {

namespace fs = std::filesystem;

constexpr int kMaxImageSide = 8192;

// Loads an image as `channels` (1 or 4) 8-bit channels. Row 0 of the result
// is the top row of the file. Fails on files larger than kMaxImageSide.
bool loadImageFile(const fs::path& path, int channels, ImageData* out, std::string* error);

// Writes 8-bit RGBA pixels (row 0 = top) as PNG.
bool savePng(const fs::path& path, int width, int height, const uint8_t* rgba, std::string* error);

}  // namespace sp
