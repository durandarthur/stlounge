#pragma once
// What the current GLES context supports, probed once at startup.

#include <string>
#include <unordered_set>

#include "gfx/gl.h"

namespace sp {

// Storage format for multipass buffers, best first.
enum class BufferFormat { RGBA32F, RGBA16F, RGBA8 };
const char* bufferFormatName(BufferFormat f);

struct GlCaps {
    std::string vendor, renderer, version, glslVersion;
    int major = 0, minor = 0;
    std::unordered_set<std::string> extensions;
    int maxTextureSize = 0;
    bool parallelCompile = false;  // GL_KHR_parallel_shader_compile

    BufferFormat bufferFormat = BufferFormat::RGBA8;
    bool bufferLinearFilter = true;  // can buffers be sampled with linear filtering
    std::string bufferFormatNote;    // why this format was chosen

    bool has(const char* ext) const { return extensions.count(ext) != 0; }
    bool isGles3() const { return major >= 3; }
    std::string summary() const;
};

// Requires a current GLES context. Leaves texture/framebuffer bindings at 0.
GlCaps probeGlCaps();

// True if a texture of this format can be a complete framebuffer attachment
// and cleared without GL errors. Requires a current context.
bool canRenderTo(GLenum internalFormat, GLenum format, GLenum type);

// GL internal format / format / type triple for a buffer format.
void bufferFormatGl(BufferFormat f, GLenum* internalFormat, GLenum* format, GLenum* type);

}  // namespace sp
