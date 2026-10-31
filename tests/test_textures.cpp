#include <chrono>
#include <cmath>
#include <cstdio>
#include <set>

#include "test.h"
#include <cstdlib>
#include <string>

#include "textures/artistic.h"
#include "textures/builtin_list.h"
#include "textures/image_io.h"
#include "textures/procedural.h"
#include "ui/shader_editor.h"

using namespace sp;

namespace {

// Max relative deviation of a 16-bin histogram from flat.
double histogramDeviation(const ImageData& img, int channel) {
    int bins[16] = {};
    int n = 0;
    for (int z = 0; z < img.depth; ++z)
        for (int y = 0; y < img.height; ++y)
            for (int x = 0; x < img.width; ++x, ++n) ++bins[img.at(x, y, channel, z) >> 4];
    double worst = 0;
    for (int b : bins) worst = std::max(worst, std::fabs(b - n / 16.0) / (n / 16.0));
    return worst;
}

// Mean absolute difference between horizontal neighbours (higher = less low-frequency content).
double neighbourDiff(const ImageData& img, int channel) {
    double sum = 0;
    int n = 0;
    for (int y = 0; y < img.height; ++y)
        for (int x = 0; x + 1 < img.width; ++x, ++n)
            sum += std::abs(int(img.at(x, y, channel)) - int(img.at(x + 1, y, channel)));
    return sum / n;
}

}  // namespace

TEST(rgba_noise_has_shadertoy_offset_structure) {
    for (int size : {64, 256}) {
        ImageData img = makeRgbaNoise(size, 1234);
        REQUIRE(img.width == size && img.height == size && img.channels == 4);
        int mismatches = 0;
        for (int y = 0; y < size; ++y)
            for (int x = 0; x < size; ++x) {
                const int sx = (x + 37) % size, sy = (y + 17) % size;
                if (img.at(x, y, 1) != img.at(sx, sy, 0)) ++mismatches;
                if (img.at(x, y, 3) != img.at(sx, sy, 2)) ++mismatches;
            }
        CHECK_EQ(mismatches, 0);
        // R and B are independent, uniform noise.
        CHECK(histogramDeviation(img, 0) < (size == 256 ? 0.1 : 0.35));
        CHECK(histogramDeviation(img, 2) < (size == 256 ? 0.1 : 0.35));
        int sameRB = 0;
        for (int i = 0; i < size * size; ++i) sameRB += img.pixels[size_t(i) * 4] == img.pixels[size_t(i) * 4 + 2];
        CHECK(sameRB < size * size / 50);
    }
}

TEST(rgba_noise_offset_after_vflip) {
    // With vflip (ShaderToy's default) the relation becomes G(x,y) = R(x+37, y-17).
    const int size = 256;
    ImageData img = makeRgbaNoise(size, 99);
    flipRows(img);
    int mismatches = 0;
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x)
            if (img.at(x, y, 1) != img.at((x + 37) % size, (y - 17 + size) % size, 0)) ++mismatches;
    CHECK_EQ(mismatches, 0);
}

TEST(noise_is_deterministic) {
    CHECK(makeRgbaNoise(64, 7).pixels == makeRgbaNoise(64, 7).pixels);
    CHECK(makeRgbaNoise(64, 7).pixels != makeRgbaNoise(64, 8).pixels);
    CHECK(makeGrayNoise(256, 3).pixels == makeGrayNoise(256, 3).pixels);
}

TEST(gray_noise_and_volumes_are_uniform) {
    ImageData g = makeGrayNoise(256, 5);
    CHECK_EQ(g.channels, 1);
    CHECK(histogramDeviation(g, 0) < 0.1);
    ImageData v = makeNoiseVolume(32, 4, 6);
    CHECK_EQ(v.depth, 32);
    CHECK_EQ(v.pixels.size(), size_t(32 * 32 * 32 * 4));
    for (int c = 0; c < 4; ++c) CHECK(histogramDeviation(v, c) < 0.1);
}

TEST(bayer_is_a_permutation) {
    ImageData b = makeBayer8();
    std::set<int> values(b.pixels.begin(), b.pixels.end());
    CHECK_EQ(values.size(), size_t(64));
    CHECK_EQ(int(b.at(0, 0, 0)), 0);
    CHECK_EQ(int(b.at(1, 0, 0)), 32 * 255 / 63);  // classic row 0: 0 32 8 40 ...
    CHECK_EQ(int(b.at(0, 1, 0)), 48 * 255 / 63);  // classic row 1: 48 16 56 24 ...
}

TEST(blue_noise_is_flat_and_high_frequency) {
    const auto start = std::chrono::steady_clock::now();
    ImageData blue = makeBlueNoise(256, 128, 2, 42);
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    std::printf("    2 channels of 128x128 void-and-cluster: %.0f ms\n", ms);

    CHECK(histogramDeviation(blue, 0) < 0.01);  // ranks: exactly flat
    CHECK(histogramDeviation(blue, 1) < 0.01);
    // Tiles repeat every 128 pixels.
    CHECK_EQ(blue.at(5, 9, 0), blue.at(133, 137, 0));
    // Blue noise neighbours differ more than white noise neighbours (~85 for uniform bytes).
    ImageData white = makeGrayNoise(256, 1);
    const double blueDiff = neighbourDiff(blue, 0), whiteDiff = neighbourDiff(white, 0);
    std::printf("    neighbour difference: blue %.1f, white %.1f\n", blueDiff, whiteDiff);
    CHECK(blueDiff > whiteDiff * 1.2);
    // Channels are independent tiles.
    CHECK(neighbourDiff(blue, 1) > whiteDiff * 1.2);
    int same = 0;
    for (int i = 0; i < 128 * 128; ++i) same += blue.pixels[size_t(i) * 2] == blue.pixels[size_t(i) * 2 + 1];
    CHECK(same < 128 * 128 / 50);
}

TEST(flip_rows_flips_each_slice) {
    ImageData img;
    img.width = 1;
    img.height = 3;
    img.depth = 2;
    img.channels = 1;
    img.pixels = {1, 2, 3, 4, 5, 6};
    flipRows(img);
    CHECK((img.pixels == std::vector<uint8_t>{3, 2, 1, 6, 5, 4}));
}

TEST(builtin_list_ids_unique_and_findable) {
    std::set<std::string> ids;
    for (const BuiltinTexture& t : builtinTextures()) {
        CHECK(ids.insert(t.id).second);
        CHECK(findBuiltinTexture(t.id) == &t);
    }
    CHECK(findBuiltinTexture("nope") == nullptr);
    const BuiltinTexture* rgba = findBuiltinTexture("rgba_noise_medium");
    REQUIRE(rgba != nullptr);
    CHECK(rgba->available);
    CHECK_EQ(rgba->size, 256);
}

TEST(editor_round_trip_drops_empty_buffers) {
    Shader s;
    s.common = "float k = 1.0;";
    Pass a;
    a.type = PassType::Buffer;
    a.bufferIndex = 0;
    a.code = "A code";
    a.inputs[0].type = InputType::Buffer;
    a.inputs[0].bufferIndex = 0;
    Pass c;
    c.type = PassType::Buffer;
    c.bufferIndex = 2;
    c.code = "  \n ";  // whitespace only
    Pass img;
    img.type = PassType::Image;
    img.code = "image code";
    img.inputs[1].type = InputType::Texture;
    img.inputs[1].asset = "bayer";
    s.passes = {img, a, c};

    ShaderEditor ed;
    ed.setShader(s);
    Shader out = ed.build();
    CHECK_EQ(out.common, s.common);
    REQUIRE(out.passes.size() == size_t(2));
    CHECK(out.passes[0].type == PassType::Buffer);
    CHECK_EQ(out.passes[0].bufferIndex, 0);
    CHECK(out.passes[0].inputs[0].type == InputType::Buffer);
    CHECK(out.passes[1].type == PassType::Image);
    CHECK_EQ(out.passes[1].inputs[1].asset, std::string("bayer"));
    CHECK_EQ(describeInput(out.passes[1].inputs[1]), std::string("Bayer"));
}

// ---------------------------------------------------------------- look-alikes

namespace {

// Mean absolute RGB difference between two pixels' worth of rows/columns.
double edgeDiff(const ImageData& img, int ax, int ay, int bx, int by, int face = 0) {
    double d = 0;
    for (int c = 0; c < 3; ++c) d += std::abs(int(img.at(ax, ay, c, face)) - int(img.at(bx, by, c, face)));
    return d / 3.0;
}

}  // namespace

TEST(artistic_textures_have_catalogue_sizes) {
    for (const BuiltinTexture& t : builtinTextures()) {
        if (t.quality != TextureQuality::Approximation) continue;
        CHECK(t.available);
        const auto start = std::chrono::steady_clock::now();
        const ImageData img = makeArtisticTexture(t.id);
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        std::printf("    %-18s %4dx%-4d %s %6.1f ms\n", t.id, img.width, img.height, img.cube ? "cube" : "2D  ", ms);
        CHECK_EQ(img.width, t.size);
        CHECK_EQ(img.channels, 4);
        CHECK_EQ(img.pixels.size(), static_cast<size_t>(img.width) * img.height * img.depth * 4);
        CHECK_EQ(img.cube, t.kind == InputType::Cubemap);
        CHECK_EQ(img.depth, img.cube ? 6 : 1);
        CHECK_EQ(img.height, std::string(t.id) == "nyancat" ? 32 : t.size);
        CHECK(ms < 2000);  // generated on first use, so it must stay quick

        // Not flat: some variety in every texture.
        std::set<uint32_t> colours;
        for (size_t i = 0; i < img.pixels.size(); i += 4 * 97)
            colours.insert(img.pixels[i] | img.pixels[i + 1] << 8 | img.pixels[i + 2] << 16);
        const bool fewColours = std::string(t.id) == "font1" || std::string(t.id) == "nyancat";
        CHECK(colours.size() > (fewColours ? 1u : 20u));

        if (const char* dir = std::getenv("SP_DUMP_TEXTURES")) {  // for looking at them
            const std::string path = std::string(dir) + "/" + t.id + ".png";
            std::string err;
            savePng(path, img.width, img.height * img.depth, img.pixels.data(), &err);
        }
    }
}

TEST(artistic_2d_textures_tile) {
    for (const char* id : {"abstract1", "abstract2", "abstract3", "lichen", "organic1", "organic2", "organic3",
                           "organic4", "pebbles", "rock_tiles", "rusty_metal", "wood"}) {
        const ImageData img = makeArtisticTexture(id);
        const int w = img.width, h = img.height;
        double wrapX = 0, wrapY = 0, inner = 0;
        for (int i = 0; i < w; ++i) {
            wrapX += edgeDiff(img, w - 1, i, 0, i);   // across the left/right seam
            wrapY += edgeDiff(img, i, h - 1, i, 0);   // across the top/bottom seam
            inner += edgeDiff(img, w / 2, i, w / 2 + 1, i) + edgeDiff(img, i, h / 2, i, h / 2 + 1);
        }
        inner /= 2;
        if (!(wrapX < 2.5 * inner + 2.0 * w && wrapY < 2.5 * inner + 2.0 * w))
            std::printf("    %s: seam %.1f/%.1f vs inner %.1f per pixel\n", id, wrapX / w, wrapY / w, inner / w);
        CHECK(wrapX < 2.5 * inner + 2.0 * w);
        CHECK(wrapY < 2.5 * inner + 2.0 * w);
    }
}

TEST(artistic_cubemaps_have_no_seams) {
    for (const char* id : {"forest", "st_peters_blurred", "uffizi"}) {
        const ImageData img = makeArtisticTexture(id);
        const int n = img.width;
        // +X face (0) right edge meets -Z face (5) left edge: same directions.
        // +Z face (4) right edge meets +X face (0) left edge.
        double seam = 0, inner = 0;
        for (int i = 0; i < n; ++i) {
            seam += std::abs(int(img.at(n - 1, i, 1, 0)) - int(img.at(0, i, 1, 5))) +
                    std::abs(int(img.at(n - 1, i, 1, 4)) - int(img.at(0, i, 1, 0)));
            inner += 2.0 * std::abs(int(img.at(n / 2, i, 1, 0)) - int(img.at(n / 2 + 1, i, 1, 0)));
        }
        if (!(seam < 2.5 * inner + 3.0 * n)) std::printf("    %s: seam %.2f vs inner %.2f\n", id, seam / n, inner / n);
        CHECK(seam < 2.5 * inner + 3.0 * n);
    }
}

TEST(font_texture_layout) {
    const ImageData f = makeArtisticTexture("font1");
    REQUIRE(f.width == 1024 && f.height == 1024);
    auto cellStats = [&](int c, int* inside, int* minA) {
        const int cx = (c % 16) * 64, cy = (c / 16) * 64;
        *inside = 0;
        *minA = 255;
        for (int y = cy; y < cy + 64; ++y)
            for (int x = cx; x < cx + 64; ++x) {
                if (f.at(x, y, 0) > 200) ++*inside;
                *minA = std::min(*minA, int(f.at(x, y, 3)));
            }
    };
    int inside = 0, minA = 0;
    cellStats(' ', &inside, &minA);
    CHECK_EQ(inside, 0);
    CHECK_EQ(minA, 255);
    for (int c : {'A', 'g', '0', '#', '~'}) {
        cellStats(c, &inside, &minA);
        CHECK(inside > 40);   // a glyph is drawn in its cell
        CHECK(minA < 100);    // and the distance falls below 0.5 inside it
    }
    // The distance is 0.5 on the edge: coverage and alpha agree.
    for (int y = 4 * 64; y < 5 * 64; ++y)
        for (int x = 64; x < 128; ++x) {
            if (f.at(x, y, 0) > 230) CHECK(f.at(x, y, 3) < 128);
            if (f.at(x, y, 0) < 20) CHECK(f.at(x, y, 3) > 128);
        }
}

TEST(artistic_textures_are_deterministic) {
    const ImageData a = makeArtisticTexture("abstract2"), b = makeArtisticTexture("abstract2");
    CHECK(a.pixels == b.pixels);
    CHECK(makeArtisticTexture("nope").width == 0);
}
