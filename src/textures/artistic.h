#pragma once
// Look-alikes for ShaderToy's artistic textures and cubemaps (stone, wood, Organic,
// London, the cubemaps...). Generated procedurally, so no original images are
// shipped. They only resemble the originals: shaders that depend on them look
// different here. All of them are deterministic, and the 2D ones tile seamlessly.

#include <string>

#include "textures/procedural.h"

namespace sp {

// The look-alike for a built-in id ("wood", "forest", "font1"...). Empty (width 0)
// when `id` has no look-alike. 2D images are stored top row first, like a PNG.
// Cubemaps have faces = 6, in GL order (+X, -X, +Y, -Y, +Z, -Z), each face's row 0
// at t = 0, so they upload without flipping.
ImageData makeArtisticTexture(const std::string& id);

// Font 1: a 16x16 grid of 64-pixel cells, character c at column c % 16 and row
// c / 16 counted from the top. RGB = glyph coverage, A = distance to the glyph
// edge (0.5 on the edge, growing outwards), as ShaderToy's font texture.
// Drawn from ProggyForever (the scalable font embedded in Dear ImGui).
ImageData makeFontTexture();

}  // namespace sp
