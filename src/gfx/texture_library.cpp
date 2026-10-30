#include "gfx/texture_library.h"

#include <chrono>

#include "platform/log.h"
#include "textures/builtin_list.h"

namespace sp {

namespace {

// Fixed seeds: textures must be identical on every run.
constexpr uint32_t kSeedRgbaSmall = 0x5EED0001, kSeedRgbaMedium = 0x5EED0002;
constexpr uint32_t kSeedGraySmall = 0x5EED0003, kSeedGrayMedium = 0x5EED0004;
constexpr uint32_t kSeedBlue = 0x5EED0005, kSeedGray3d = 0x5EED0006, kSeedRgba3d = 0x5EED0007;
constexpr int kBlueNoiseTile = 128;

std::shared_ptr<const ImageData> generate(const std::string& id) {
    ImageData img;
    if (id == "rgba_noise_small") img = makeRgbaNoise(64, kSeedRgbaSmall);
    else if (id == "rgba_noise_medium") img = makeRgbaNoise(256, kSeedRgbaMedium);
    else if (id == "gray_noise_small") img = makeGrayNoise(64, kSeedGraySmall);
    else if (id == "gray_noise_medium") img = makeGrayNoise(256, kSeedGrayMedium);
    else if (id == "bayer") img = makeBayer8();
    else if (id == "blue_noise") img = makeBlueNoise(1024, kBlueNoiseTile, 4, kSeedBlue);
    else if (id == "gray_noise_3d") img = makeNoiseVolume(32, 1, kSeedGray3d);
    else if (id == "rgba_noise_3d") img = makeNoiseVolume(32, 4, kSeedRgba3d);
    else return nullptr;
    return std::make_shared<const ImageData>(std::move(img));
}

}  // namespace

std::shared_ptr<const ImageData> TextureLibrary::pixels(const std::string& id) {
    auto it = cpu_.find(id);
    if (it != cpu_.end()) return it->second;
    const auto start = std::chrono::steady_clock::now();
    auto img = generate(id);
    if (img) {
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        LOG_DEBUG("generated texture '%s' (%dx%dx%d) in %.1f ms", id.c_str(), img->width, img->height, img->depth, ms);
    }
    cpu_[id] = img;
    return img;
}

const GpuTexture* TextureLibrary::get(const std::string& id, bool vflip, bool srgb) {
    const BuiltinTexture* desc = findBuiltinTexture(id);
    if (!desc || !desc->available) return nullptr;

    const std::string key = id + (vflip ? "|flip" : "") + (srgb ? "|srgb" : "");
    if (auto it = gpu_.find(key); it != gpu_.end()) return &it->second;

    auto src = pixels(id);
    if (!src) return nullptr;
    ImageData img = *src;
    if (vflip) flipRows(img);

    GpuTexture t;
    t.width = img.width;
    t.height = img.height;
    t.depth = img.depth;
    t.target = img.depth > 1 ? GL_TEXTURE_3D : GL_TEXTURE_2D;
    const bool rgba = img.channels == 4;
    // sRGB decoding exists only for RGBA8 in GLES; single-channel textures ignore it.
    const GLenum internal = rgba ? (srgb ? GL_SRGB8_ALPHA8 : GL_RGBA8) : GL_R8;
    const GLenum format = rgba ? GL_RGBA : GL_RED;

    while (glGetError() != GL_NO_ERROR) {}
    glGenTextures(1, &t.id);
    glBindTexture(t.target, t.id);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    if (t.target == GL_TEXTURE_3D)
        glTexImage3D(GL_TEXTURE_3D, 0, static_cast<GLint>(internal), img.width, img.height, img.depth, 0, format,
                     GL_UNSIGNED_BYTE, img.pixels.data());
    else
        glTexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(internal), img.width, img.height, 0, format,
                     GL_UNSIGNED_BYTE, img.pixels.data());
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glGenerateMipmap(t.target);
    glBindTexture(t.target, 0);
    if (GLenum err = glGetError(); err != GL_NO_ERROR) {
        LOG_ERROR("uploading texture '%s' failed (GL error 0x%x)", id.c_str(), err);
        glDeleteTextures(1, &t.id);
        return nullptr;
    }
    return &(gpu_[key] = t);
}

void TextureLibrary::clear() {
    for (auto& [key, t] : gpu_)
        if (t.id) glDeleteTextures(1, &t.id);
    gpu_.clear();
    cpu_.clear();
}

}  // namespace sp
