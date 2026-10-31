#pragma once
// A texture with a framebuffer attached, used as a render destination.

#include <string>

#include "gfx/gl.h"

namespace sp {

// Vertex shader drawing one fullscreen triangle from gl_VertexID (no buffers).
extern const char* const kFullscreenVertexShader;

class RenderTarget {
public:
    RenderTarget() = default;
    ~RenderTarget() { destroy(); }
    RenderTarget(const RenderTarget&) = delete;
    RenderTarget& operator=(const RenderTarget&) = delete;
    RenderTarget(RenderTarget&& o) noexcept { *this = std::move(o); }
    RenderTarget& operator=(RenderTarget&& o) noexcept;

    // Allocates (or reallocates) and clears to transparent black.
    bool create(int width, int height, GLenum internalFormat, GLenum format, GLenum type, std::string* error);
    void destroy();
    void clear();

    bool valid() const { return fbo_ != 0; }
    GLuint texture() const { return texture_; }
    GLuint framebuffer() const { return fbo_; }
    int width() const { return width_; }
    int height() const { return height_; }

private:
    GLuint texture_ = 0, fbo_ = 0;
    int width_ = 0, height_ = 0;
};

// Draws a fullscreen triangle with an empty VAO (positions come from gl_VertexID).
class FullscreenTriangle {
public:
    ~FullscreenTriangle();
    void init();
    void draw() const;

private:
    GLuint vao_ = 0;
};

// Copies a texture to the current framebuffer, scaling with linear filtering.
class Blitter {
public:
    ~Blitter();
    bool init(std::string* error);
    void draw(GLuint texture, int dstWidth, int dstHeight, const FullscreenTriangle& tri) const;
    // Crossfade: from `a` (t = 0) to `b` (t = 1).
    void drawMix(GLuint a, GLuint b, float t, int dstWidth, int dstHeight, const FullscreenTriangle& tri) const;

private:
    GLuint program_ = 0, mixProgram_ = 0;
    GLint texLoc_ = -1, sizeLoc_ = -1;
    GLint mixTexLoc_[2] = {-1, -1}, mixSizeLoc_ = -1, mixLoc_ = -1;
};

}  // namespace sp
