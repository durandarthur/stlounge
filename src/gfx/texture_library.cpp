#include "gfx/texture_library.h"

#include <chrono>

#include "platform/log.h"
#include "textures/artistic.h"
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
    else img = makeArtisticTexture(id);
    if (img.width == 0) return nullptr;
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
    refresh(id);
    return overrideError(id);
}

std::string TextureLibrary::folderImage(const std::string& id) const {
    auto it = folderImages_.find(id);
    return it == folderImages_.end() ? "" : it->second;
}

std::map<std::string, std::string> TextureLibrary::scanImages(const std::filesystem::path& dir, const char* what) {
    std::map<std::string, std::string> found;
    std::error_code ec;
    for (const auto& de : std::filesystem::directory_iterator(dir, ec)) {
        if (!de.is_regular_file(ec) || de.path().filename().string().rfind('.', 0) == 0) continue;
        std::string ext = de.path().extension().string();
        for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (ext != ".png" && ext != ".jpg" && ext != ".jpeg" && ext != ".bmp" && ext != ".tga") continue;
        const BuiltinTexture* t = findBuiltinByFileName(de.path().stem().string());
        if (!t || !canOverride(t->id)) {
            LOG_WARN("%s: '%s' doesn't name a 2D texture; ignored", what, de.path().filename().c_str());
            continue;
        }
        if (found.count(t->id)) LOG_WARN("%s: two images for '%s'; using %s", what, t->label, de.path().filename().c_str());
        found[t->id] = de.path().string();
    }
    return found;
}

int TextureLibrary::setReplacementFolder(const std::filesystem::path& dir) {
    const std::map<std::string, std::string> before = folderImages_;
    folder_ = dir;
    folderImages_ = scanImages(dir, "textures folder");
    // Update textures whose image appeared, changed or went away (files may have been
    // edited in place, so every folder texture is re-read).
    for (const auto& [id, file] : before)
        if (!folderImages_.count(id)) refresh(id);
    for (const auto& [id, file] : folderImages_) refresh(id);
    if (!folderImages_.empty())
        LOG_INFO("textures folder %s: %zu replacements", dir.c_str(), folderImages_.size());
    return static_cast<int>(folderImages_.size());
}

int TextureLibrary::setBundledFolder(const std::filesystem::path& dir) {
    bundledImages_ = scanImages(dir, "bundled textures");
    for (const auto& [id, file] : bundledImages_) refresh(id);
    LOG_INFO("bundled textures: %zu in %s", bundledImages_.size(), dir.c_str());
    return static_cast<int>(bundledImages_.size());
}

void TextureLibrary::refresh(const std::string& id) {
    overrideErrors_.erase(id);
    cpu_.erase(id);
    // Only textures already on the GPU are re-read now; others load on first use.
    bool onGpu = false;
    for (const auto& [key, t] : gpu_) onGpu = onGpu || key == id || key.rfind(id + "|", 0) == 0;
    if (!onGpu) return;
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
}

std::shared_ptr<const ImageData> TextureLibrary::pixels(const std::string& id) {
    auto it = cpu_.find(id);
    if (it != cpu_.end()) return it->second;
    const auto start = std::chrono::steady_clock::now();
    std::shared_ptr<const ImageData> img;
    std::string file;
    if (auto ov = overrides_.find(id); ov != overrides_.end()) file = ov->second;
    else if (auto f = folderImages_.find(id); f != folderImages_.end()) file = f->second;
    else if (auto b = bundledImages_.find(id); b != bundledImages_.end()) file = b->second;
    if (!file.empty()) {
        ImageData loaded;
        std::string error;
        if (loadImageFile(file, builtinChannels(id), &loaded, &error)) {
            img = std::make_shared<const ImageData>(std::move(loaded));
            LOG_DEBUG("texture '%s' from %s (%dx%d)", id.c_str(), file.c_str(), img->width, img->height);
        } else {
            overrideErrors_[id] = error;
            LOG_WARN("texture '%s': %s; using the generated one", id.c_str(), error.c_str());
        }
    }
    if (!img) img = generate(id);
    if (img) {
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        LOG_DEBUG("texture '%s' ready (%dx%dx%d) in %.1f ms", id.c_str(), img->width, img->height, img->depth, ms);
    }
    cpu_[id] = img;
    return img;
}

const GpuTexture* TextureLibrary::get(const std::string& id, bool vflip, bool srgb) {
    const BuiltinTexture* desc = findBuiltinTexture(id);
    if (!desc || (!desc->available && !usesImage(id))) return nullptr;

    if (desc->kind == InputType::Cubemap) vflip = false;  // faces are generated from directions
    const std::string key = id + (vflip ? "|flip" : "") + (srgb ? "|srgb" : "");
    if (auto it = gpu_.find(key); it != gpu_.end()) return &it->second;

    auto src = pixels(id);
    if (!src) return nullptr;
    GpuTexture tex;
    if (!upload(tex, *src, vflip, srgb, id)) return nullptr;
    // Big images and look-alikes are cheap to re-read or remake, so the CPU copy isn't kept.
    if (desc->quality == TextureQuality::Approximation || usesImage(id)) cpu_.erase(id);
    return &(gpu_[key] = tex);
}

bool TextureLibrary::upload(GpuTexture& t, const ImageData& src, bool vflip, bool srgb, const std::string& id) {
    ImageData img = src;
    if (vflip && !img.cube) flipRows(img);
    t.width = img.width;
    t.height = img.height;
    t.depth = img.cube ? 1 : img.depth;
    t.target = img.cube ? GL_TEXTURE_CUBE_MAP : img.depth > 1 ? GL_TEXTURE_3D : GL_TEXTURE_2D;
    const bool rgba = img.channels == 4;
    // sRGB decoding exists only for RGBA8 in GLES; single-channel textures ignore it.
    const GLenum internal = rgba ? (srgb ? GL_SRGB8_ALPHA8 : GL_RGBA8) : GL_R8;
    const GLenum format = rgba ? GL_RGBA : GL_RED;

    while (glGetError() != GL_NO_ERROR) {}
    const bool fresh = t.id == 0;
    if (fresh) glGenTextures(1, &t.id);
    glBindTexture(t.target, t.id);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    if (t.target == GL_TEXTURE_CUBE_MAP) {
        const size_t face = static_cast<size_t>(img.width) * img.height * img.channels;
        for (int f = 0; f < 6; ++f)
            glTexImage2D(static_cast<GLenum>(GL_TEXTURE_CUBE_MAP_POSITIVE_X + f), 0, static_cast<GLint>(internal),
                         img.width, img.height, 0, format, GL_UNSIGNED_BYTE, img.pixels.data() + face * f);
    } else if (t.target == GL_TEXTURE_3D)
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
