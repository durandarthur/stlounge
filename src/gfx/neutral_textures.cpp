#include "gfx/neutral_textures.h"

namespace sp {

namespace {

void nearestClamp(GLenum target) {
    glTexParameteri(target, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(target, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(target, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(target, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    if (target != GL_TEXTURE_2D) glTexParameteri(target, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
}

}  // namespace

void NeutralTextures::init() {
    const unsigned char black[4] = {0, 0, 0, 255};

    glGenTextures(1, &tex2D_);
    glBindTexture(GL_TEXTURE_2D, tex2D_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, black);
    nearestClamp(GL_TEXTURE_2D);

    glGenTextures(1, &cube_);
    glBindTexture(GL_TEXTURE_CUBE_MAP, cube_);
    for (int face = 0; face < 6; ++face)
        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + static_cast<GLenum>(face), 0, GL_RGBA8, 1, 1, 0, GL_RGBA,
                     GL_UNSIGNED_BYTE, black);
    nearestClamp(GL_TEXTURE_CUBE_MAP);

    glGenTextures(1, &volume_);
    glBindTexture(GL_TEXTURE_3D, volume_);
    glTexImage3D(GL_TEXTURE_3D, 0, GL_RGBA8, 1, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, black);
    nearestClamp(GL_TEXTURE_3D);

    glBindTexture(GL_TEXTURE_2D, 0);
    glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
    glBindTexture(GL_TEXTURE_3D, 0);
}

NeutralTextures::~NeutralTextures() {
    const GLuint t[3] = {tex2D_, cube_, volume_};
    for (GLuint id : t)
        if (id) glDeleteTextures(1, &id);
}

}  // namespace sp
