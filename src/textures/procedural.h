#pragma once
// CPU generators for ShaderToy's procedural textures. Deterministic (fixed
// seeds), so a shader looks the same on every run and machine.
//
// Row 0 of `pixels` is uploaded as texture row t=0 when vflip is off.

#include <cstdint>
#include <vector>

namespace sp {

struct ImageData {
    int width = 0, height = 0, depth = 1;  // depth > 1 for volumes
    int channels = 4;                      // 1 (R8) or 4 (RGBA8)
    std::vector<uint8_t> pixels;           // width*height*depth*channels

    uint8_t at(int x, int y, int c, int z = 0) const {
        return pixels[((static_cast<size_t>(z) * height + y) * width + x) * channels + c];
    }
};

// White noise with ShaderToy's channel structure: G is R shifted by (37,17)
// pixels and A is B shifted by (37,17), i.e. G(x,y) = R(x+37, y+17) with
// wrap-around. Shaders doing 3D value noise from a 2D texture rely on this.
ImageData makeRgbaNoise(int size, uint32_t seed);

// Single-channel uniform white noise.
ImageData makeGrayNoise(int size, uint32_t seed);

// 8x8 ordered-dither (Bayer) matrix, single channel, values spread over 0..255.
ImageData makeBayer8();

// Blue noise via void-and-cluster on a `tile`-sized torus, one independent
// tile per channel, repeated to fill `size`. Each tile's values are a
// permutation of evenly spaced levels, so the histogram is flat.
ImageData makeBlueNoise(int size, int tile, int channels, uint32_t seed);

// 3D uniform white noise.
ImageData makeNoiseVolume(int size, int channels, uint32_t seed);

// Flips rows (each depth slice separately).
void flipRows(ImageData& img);

}  // namespace sp
