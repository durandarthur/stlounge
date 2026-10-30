#pragma once
// 1x1 opaque black textures bound to empty or unsupported channels, matching
// what WebGL returns when sampling an unbound texture: vec4(0, 0, 0, 1).

#include "gfx/gl.h"

namespace sp {

class NeutralTextures {
public:
    ~NeutralTextures();
    void init();
    GLuint tex2D() const { return tex2D_; }
    GLuint cube() const { return cube_; }
    GLuint volume() const { return volume_; }

private:
    GLuint tex2D_ = 0, cube_ = 0, volume_ = 0;
};

}  // namespace sp
