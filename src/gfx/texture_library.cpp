#include "gfx/texture_library.h"

#include <chrono>

#include "platform/log.h"
#include "textures/builtin_list.h"
#include "textures/image_io.h"

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

// Channel count of the built-in, so an override keeps the texture's format.
int builtinChannels(const std::string& id) {
    return id == "gray_noise_small" || id == "gray_noise_medium" || id == "bayer" ? 1 : 4;
}

}  // namespace

bool TextureLibrary::canOverride(const std::string& id) {
    const BuiltinTexture* d = findBuiltinTexture(id);
    return d && d->kind == InputType::Texture;
}

std::string TextureLibrary::overrideError(const std::string& id) const {
    auto it = overrideErrors_.find(id);
    return it == overrideErrors_.end() ? "" : it->second;
}

std::string TextureLibrary::setOverride(const std::string& id, const std::string& path) {
    if (!canOverride(id)) return "only 2D textures can be replaced";
    overrideErrors_.erase(id);
    if (path.empty()) overrides_.erase(id);
    else overrides_[id] = path;
    cpu_.erase(id);
    auto src = pixels(id);
    // Refresh GPU copies in place (same texture names, so bound shaders keep working).
    for (auto it = gpu_.begin(); it != gpu_.end();) {
        const std::string& key = it->first;
        if (key != id && key.rfind(id + "|", 0) != 0) {
            ++it;
            continue;
        }
        const bool vflip = key.find("|flip") != std::string::npos;
        const bool srgb = key.find("|srgb") != std::string::npos;
        if (src && upload(it->second, *src, vflip, srgb, id)) {
            ++it;
        } else {
            if (it->second.id) glDeleteTextures(1, &it->second.id);
            it = gpu_.erase(it);
        }
    }
    return overrideError(id);
}

std::shared_ptr<const ImageData> TextureLibrary::pixels(const std::string& id) {
    auto it = cpu_.find(id);
    if (it != cpu_.end()) return it->second;
    const auto start = std::chrono::steady_clock::now();
    std::shared_ptr<const ImageData> img;
    if (auto ov = overrides_.find(id); ov != overrides_.end()) {
        ImageData loaded;
        std::string error;
        if (loadImageFile(ov->second, builtinChannels(id), &loaded, &error)) {
            img = std::make_shared<const ImageData>(std::move(loaded));
            LOG_INFO("texture '%s' replaced by %s (%dx%d)", id.c_str(), ov->second.c_str(), img->width, img->height);
        } else {
            overrideErrors_[id] = error;
            LOG_WARN("texture '%s': %s; using the built-in", id.c_str(), error.c_str());
        }
    }
    if (!img) img = generate(id);
    if (img) {
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        LOG_DEBUG("generated texture '%s' (%dx%dx%d) in %.1f ms", id.c_str(), img->width, img->height, img->depth, ms);
    }
    cpu_[id] = img;
    return img;
}

const GpuTexture* TextureLibrary::get(const std::string& id, bool vflip, bool srgb) {
    const BuiltinTexture* desc = findBuiltinTexture(id);
    if (!desc || (!desc->available && !hasOverride(id))) return nullptr;

    const std::string key = id + (vflip ? "|flip" : "") + (srgb ? "|srgb" : "");
    if (auto it = gpu_.find(key); it != gpu_.end()) return &it->second;

    auto src = pixels(id);
    if (!src) return nullptr;
    GpuTexture tex;
    if (!upload(tex, *src, vflip, srgb, id)) return nullptr;
    return &(gpu_[key] = tex);
}

bool TextureLibrary::upload(GpuTexture& t, const ImageData& src, bool vflip, bool srgb, const std::string& id) {
    ImageData img = src;
    if (vflip) flipRows(img);
    t.width = img.width;
    t.height = img.height;
    t.depth = img.depth;
    t.target = img.depth > 1 ? GL_TEXTURE_3D : GL_TEXTURE_2D;
    const bool rgba = img.channels == 4;
    // sRGB decoding exists only for RGBA8 in GLES; single-channel textures ignore it.
    const GLenum internal = rgba ? (srgb ? GL_SRGB8_ALPHA8 : GL_RGBA8) : GL_R8;
    const GLenum format = rgba ? GL_RGBA : GL_RED;

    while (glGetError() != GL_NO_ERROR) {}
    const bool fresh = t.id == 0;
    if (fresh) glGenTextures(1, &t.id);
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
        if (fresh) {
            glDeleteTextures(1, &t.id);
            t.id = 0;
        }
        return false;
    }
    return true;
}

void TextureLibrary::clear() {
    for (auto& [key, t] : gpu_)
        if (t.id) glDeleteTextures(1, &t.id);
    gpu_.clear();
    cpu_.clear();
    overrideErrors_.clear();
}

}  // namespace sp
