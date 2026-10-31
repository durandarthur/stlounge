#include "textures/image_io.h"

#include <cstdio>
#include <memory>

#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_BMP
#define STBI_ONLY_TGA
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

namespace sp {

bool loadImageFile(const fs::path& path, int channels, ImageData* out, std::string* error) {
    std::unique_ptr<FILE, int (*)(FILE*)> f(std::fopen(path.c_str(), "rb"), &std::fclose);
    if (!f) {
        if (error) *error = "cannot open " + path.string();
        return false;
    }
    int w = 0, h = 0, n = 0;
    if (!stbi_info_from_file(f.get(), &w, &h, &n)) {
        if (error) *error = path.filename().string() + ": not a PNG, JPEG, BMP or TGA image";
        return false;
    }
    if (w <= 0 || h <= 0 || w > kMaxImageSide || h > kMaxImageSide) {
        if (error) *error = path.filename().string() + " is " + std::to_string(w) + "x" + std::to_string(h) +
                            "; the limit is " + std::to_string(kMaxImageSide) + " per side";
        return false;
    }
    std::unique_ptr<stbi_uc, void (*)(void*)> px(stbi_load_from_file(f.get(), &w, &h, &n, channels), &stbi_image_free);
    if (!px) {
        if (error) *error = path.filename().string() + ": " + stbi_failure_reason();
        return false;
    }
    out->width = w;
    out->height = h;
    out->depth = 1;
    out->channels = channels;
    out->pixels.assign(px.get(), px.get() + static_cast<size_t>(w) * h * channels);
    return true;
}

bool savePng(const fs::path& path, int width, int height, const uint8_t* rgba, std::string* error) {
    if (!stbi_write_png(path.c_str(), width, height, 4, rgba, width * 4)) {
        if (error) *error = "cannot write " + path.string();
        return false;
    }
    return true;
}

}  // namespace sp
