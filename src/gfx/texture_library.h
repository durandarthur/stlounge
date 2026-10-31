#pragma once
// GPU copies of built-in textures, created on first use and shared by all
// loaded shaders. Must be destroyed while the GL context is current.

#include <map>
#include <memory>
#include <string>

#include "gfx/gl.h"
#include "textures/procedural.h"

namespace sp {

struct GpuTexture {
    GLenum target = 0;  // GL_TEXTURE_2D, GL_TEXTURE_CUBE_MAP or GL_TEXTURE_3D
    GLuint id = 0;
    int width = 0, height = 0, depth = 1;
};

class TextureLibrary {
public:
    TextureLibrary() = default;
    ~TextureLibrary() { clear(); }
    TextureLibrary(const TextureLibrary&) = delete;
    TextureLibrary& operator=(const TextureLibrary&) = delete;

    // nullptr if the id is unknown or has neither a generator nor an override.
    const GpuTexture* get(const std::string& id, bool vflip, bool srgb);
    void clear();

    // Use an image file instead of a built-in 2D texture ("" path removes the
    // override). Textures already on the GPU are updated in place; reload
    // shaders afterwards so iChannelResolution follows a size change.
    // Returns "" or why the file can't be used (the built-in is used then).
    std::string setOverride(const std::string& id, const std::string& path);
    bool hasOverride(const std::string& id) const { return overrides_.count(id) != 0; }
    std::string overrideError(const std::string& id) const;
    // Overrides apply to 2D textures only (not cubemaps or volumes).
    static bool canOverride(const std::string& id);

    // CPU pixels for a built-in id (cached), or nullptr. Exposed for tests.
    std::shared_ptr<const ImageData> pixels(const std::string& id);

private:
    bool upload(GpuTexture& t, const ImageData& src, bool vflip, bool srgb, const std::string& id);

    std::map<std::string, std::string> overrides_;       // id -> file
    std::map<std::string, std::string> overrideErrors_;  // id -> why the file wasn't usable
    std::map<std::string, std::shared_ptr<const ImageData>> cpu_;
    std::map<std::string, GpuTexture> gpu_;  // key: id + variant flags
};

}  // namespace sp
