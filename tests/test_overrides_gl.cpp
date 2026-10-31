// Texture overrides and the crossfade blit, on a headless GLES 3 context.

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <vector>

#include "gfx/gl_caps.h"
#include "gfx/render_target.h"
#include "gfx/texture_library.h"
#include "headless_gl.h"
#include "test.h"
#include "textures/image_io.h"

using namespace sp;
namespace fs = std::filesystem;

namespace {

fs::path tempDir(const char* name) {
    const char* tmp = std::getenv("TMPDIR");
    fs::path d = fs::path(tmp && *tmp ? tmp : "/tmp") / name;
    fs::remove_all(d);
    fs::create_directories(d);
    return d;
}

fs::path writeSolidPng(const fs::path& dir, const char* name, int w, int h, uint8_t r, uint8_t g, uint8_t b) {
    std::vector<uint8_t> px(static_cast<size_t>(w) * h * 4);
    for (size_t i = 0; i < px.size(); i += 4) {
        px[i] = r;
        px[i + 1] = g;
        px[i + 2] = b;
        px[i + 3] = 255;
    }
    std::string err;
    const fs::path p = dir / name;
    REQUIRE(savePng(p, w, h, px.data(), &err));
    return p;
}

void readTexel(GLuint tex, uint8_t out[4]) {
    GLuint fbo = 0;
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    glReadPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, out);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDeleteFramebuffers(1, &fbo);
}

}  // namespace

TEST(texture_override_replaces_in_place) {
    sptest::requireGl();
    const fs::path dir = tempDir("sp_test_overrides");
    TextureLibrary lib;
    // "abstract1" is a generated look-alike until an image replaces it.
    const GpuTexture* builtin = lib.get("abstract1", false, false);
    REQUIRE(builtin != nullptr);
    CHECK_EQ(builtin->width, 1024);
    const GLuint builtinId = builtin->id;
    CHECK(TextureLibrary::canOverride("abstract1"));
    CHECK(!TextureLibrary::canOverride("forest"));  // a cubemap

    CHECK(lib.setOverride("abstract1", writeSolidPng(dir, "red.png", 8, 4, 255, 0, 0).string()).empty());
    const GpuTexture* t = lib.get("abstract1", false, false);
    REQUIRE(t != nullptr);
    CHECK(t->width == 8 && t->height == 4);
    const GLuint id = t->id;
    CHECK_EQ(id, builtinId);  // replaced in place
    uint8_t px[4];
    readTexel(id, px);
    CHECK(px[0] == 255 && px[1] == 0);

    // A new file updates the same texture name, so running shaders keep working.
    CHECK(lib.setOverride("abstract1", writeSolidPng(dir, "green.png", 16, 16, 0, 255, 0).string()).empty());
    t = lib.get("abstract1", false, false);
    REQUIRE(t != nullptr);
    CHECK_EQ(t->id, id);
    CHECK_EQ(t->width, 16);
    readTexel(id, px);
    CHECK(px[0] == 0 && px[1] == 255);

    // A broken file: error reported, and the look-alike comes back.
    std::ofstream(dir / "bad.png") << "nope";
    CHECK(!lib.setOverride("abstract1", (dir / "bad.png").string()).empty());
    REQUIRE(lib.get("abstract1", false, false) != nullptr);
    CHECK_EQ(lib.get("abstract1", false, false)->width, 1024);

    // Overriding a procedural texture, then removing the override, brings the built-in back.
    const GpuTexture* noise = lib.get("rgba_noise_small", false, false);
    REQUIRE(noise != nullptr);
    CHECK_EQ(noise->width, 64);
    CHECK(lib.setOverride("rgba_noise_small", writeSolidPng(dir, "blue.png", 4, 4, 0, 0, 255).string()).empty());
    CHECK_EQ(lib.get("rgba_noise_small", false, false)->width, 4);
    CHECK(lib.setOverride("rgba_noise_small", "").empty());
    CHECK_EQ(lib.get("rgba_noise_small", false, false)->width, 64);
    CHECK_EQ(glGetError(), static_cast<GLenum>(GL_NO_ERROR));
}

TEST(blitter_crossfade_mixes) {
    sptest::requireGl();
    FullscreenTriangle tri;
    tri.init();
    Blitter blit;
    std::string err;
    REQUIRE(blit.init(&err));
    RenderTarget a, b, out;
    REQUIRE(a.create(4, 4, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, &err));
    REQUIRE(b.create(4, 4, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, &err));
    REQUIRE(out.create(4, 4, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, &err));
    glBindFramebuffer(GL_FRAMEBUFFER, a.framebuffer());
    glClearColor(1, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glBindFramebuffer(GL_FRAMEBUFFER, b.framebuffer());
    glClearColor(0, 0, 1, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glBindFramebuffer(GL_FRAMEBUFFER, out.framebuffer());
    blit.drawMix(a.texture(), b.texture(), 0.25f, 4, 4, tri);
    uint8_t px[4];
    glReadPixels(1, 1, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    CHECK(px[0] > 185 && px[0] < 196);  // 0.75 * 255
    CHECK(px[2] > 58 && px[2] < 70);    // 0.25 * 255
    CHECK_EQ(px[3], 255);
}

TEST(texture_folder_replaces_built_ins) {
    sptest::requireGl();
    const fs::path dir = tempDir("sp_test_texture_folder");
    writeSolidPng(dir, "Organic 2.png", 8, 8, 255, 0, 0);
    writeSolidPng(dir, "forest.png", 4, 4, 0, 255, 0);         // a cubemap: ignored
    writeSolidPng(dir, "holiday photo.png", 4, 4, 0, 255, 0);  // no such texture: ignored
    TextureLibrary lib;
    const GpuTexture* before = lib.get("organic2", true, false);  // already on the GPU
    REQUIRE(before != nullptr);
    const GLuint id = before->id;
    CHECK_EQ(before->width, 1024);

    CHECK_EQ(lib.setReplacementFolder(dir), 1);
    CHECK(lib.usesUserImage("organic2"));
    CHECK(!lib.usesUserImage("forest"));
    const GpuTexture* t = lib.get("organic2", true, false);
    REQUIRE(t != nullptr);
    CHECK_EQ(t->id, id);  // updated in place
    CHECK_EQ(t->width, 8);
    uint8_t px[4];
    readTexel(id, px);
    CHECK(px[0] == 255 && px[1] == 0);

    // An image chosen in Settings wins over the folder.
    CHECK(lib.setOverride("organic2", writeSolidPng(dir / "..", "blue_override.png", 2, 2, 0, 0, 255).string()).empty());
    CHECK_EQ(lib.get("organic2", true, false)->width, 2);
    CHECK(lib.setOverride("organic2", "").empty());
    CHECK_EQ(lib.get("organic2", true, false)->width, 8);

    // Removing the image and rescanning brings the built-in back.
    fs::remove(dir / "Organic 2.png");
    CHECK_EQ(lib.setReplacementFolder(dir), 0);
    CHECK(!lib.usesUserImage("organic2"));
    CHECK_EQ(lib.get("organic2", true, false)->width, 1024);
    CHECK_EQ(glGetError(), static_cast<GLenum>(GL_NO_ERROR));
}

TEST(bundled_images_come_before_generated_textures) {
    sptest::requireGl();
    const fs::path bundled = tempDir("sp_test_bundled");
    writeSolidPng(bundled, "organic2.png", 16, 16, 0, 0, 255);
    TextureLibrary lib;
    CHECK(!lib.usesImage("organic2"));
    CHECK_EQ(lib.setBundledFolder(bundled), 1);
    CHECK(lib.usesImage("organic2"));
    CHECK(!lib.usesUserImage("organic2"));
    CHECK_EQ(lib.get("organic2", false, false)->width, 16);
    // The user's folder wins over the bundled image.
    const fs::path user = tempDir("sp_test_bundled_user");
    writeSolidPng(user, "Organic 2.png", 8, 8, 255, 0, 0);
    lib.setReplacementFolder(user);
    CHECK_EQ(lib.get("organic2", false, false)->width, 8);
    CHECK(lib.usesUserImage("organic2"));
}
