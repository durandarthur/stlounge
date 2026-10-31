#pragma once
// Catalogue of ShaderToy's built-in media, as substitutes this app can provide.

#include <string>
#include <vector>

#include "shadertoy/shader.h"

namespace sp {

enum class TextureQuality {
    Procedural,     // generated; statistically equivalent to the original
    Approximation,  // artistic texture, only a look-alike
};

struct BuiltinTexture {
    const char* id;     // stable id used in our files ("rgba_noise_medium")
    const char* label;  // ShaderToy's name ("RGBA Noise Medium")
    InputType kind;     // Texture, Cubemap or Volume
    int size;           // width (= height, = depth for volumes)
    TextureQuality quality;
    bool available;     // false if the app can't provide it
    const char* stId;   // ShaderToy media id ("Xsf3zn"), "" if not yet known
    const char* stHash; // file name hash in /media/a/<hash>.png, "" if not yet known
};

const std::vector<BuiltinTexture>& builtinTextures();
const BuiltinTexture* findBuiltinTexture(const std::string& id);
// Lookup from ShaderToy JSON: by media id, or by the hash in a /media/a/<hash>.<ext> path.
const BuiltinTexture* findBuiltinByShadertoyId(const std::string& stId);
const BuiltinTexture* findBuiltinByMediaPath(const std::string& path);

}  // namespace sp
