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

    // nullptr if the id is unknown or has no generator yet.
    const GpuTexture* get(const std::string& id, bool vflip, bool srgb);
    void clear();

    // CPU pixels for a built-in id (cached), or nullptr. Exposed for tests.
    std::shared_ptr<const ImageData> pixels(const std::string& id);

private:
    std::map<std::string, std::shared_ptr<const ImageData>> cpu_;
    std::map<std::string, GpuTexture> gpu_;  // key: id + variant flags
};

}  // namespace sp
