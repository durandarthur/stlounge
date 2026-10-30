#include "textures/builtin_list.h"

namespace sp {

const std::vector<BuiltinTexture>& builtinTextures() {
    using Q = TextureQuality;
    static const std::vector<BuiltinTexture> list = {
        // Procedural, generated in-app.
        {"rgba_noise_small", "RGBA Noise Small", InputType::Texture, 64, Q::Procedural, true, "", ""},
        {"rgba_noise_medium", "RGBA Noise Medium", InputType::Texture, 256, Q::Procedural, true, "Xsf3zn",
         "f735bee5b64ef98879dc618b016ecf7939a5756040c2cde21ccb15e69a6e1cfb"},
        {"gray_noise_small", "Gray Noise Small", InputType::Texture, 64, Q::Procedural, true, "", ""},
        {"gray_noise_medium", "Gray Noise Medium", InputType::Texture, 256, Q::Procedural, true, "", ""},
        {"bayer", "Bayer", InputType::Texture, 8, Q::Procedural, true, "", ""},
        {"blue_noise", "Blue Noise", InputType::Texture, 1024, Q::Procedural, true, "", ""},
        {"gray_noise_3d", "Gray Noise 3D", InputType::Volume, 32, Q::Procedural, true, "", ""},
        {"rgba_noise_3d", "RGBA Noise 3D", InputType::Volume, 32, Q::Procedural, true, "", ""},
        // Artistic textures: look-alike approximations (milestone 8). Sizes are placeholders.
        {"abstract1", "Abstract 1", InputType::Texture, 1024, Q::Approximation, false, "", ""},
        {"abstract2", "Abstract 2", InputType::Texture, 512, Q::Approximation, false, "", ""},
        {"abstract3", "Abstract 3", InputType::Texture, 1024, Q::Approximation, false, "", ""},
        {"font1", "Font 1", InputType::Texture, 1024, Q::Approximation, false, "", ""},
        {"lichen", "Lichen", InputType::Texture, 1024, Q::Approximation, false, "", ""},
        {"london", "London", InputType::Texture, 512, Q::Approximation, false, "", ""},
        {"nyancat", "Nyancat", InputType::Texture, 256, Q::Approximation, false, "", ""},
        {"organic1", "Organic 1", InputType::Texture, 1024, Q::Approximation, false, "", ""},
        {"organic2", "Organic 2", InputType::Texture, 1024, Q::Approximation, false, "", ""},
        {"organic3", "Organic 3", InputType::Texture, 1024, Q::Approximation, false, "", ""},
        {"organic4", "Organic 4", InputType::Texture, 1024, Q::Approximation, false, "", ""},
        {"pebbles", "Pebbles", InputType::Texture, 512, Q::Approximation, false, "", ""},
        {"rock_tiles", "Rock Tiles", InputType::Texture, 512, Q::Approximation, false, "", ""},
        {"rusty_metal", "Rusty Metal", InputType::Texture, 512, Q::Approximation, false, "", ""},
        {"stars", "Stars", InputType::Texture, 512, Q::Approximation, false, "", ""},
        {"wood", "Wood", InputType::Texture, 512, Q::Approximation, false, "", ""},
        {"forest", "Forest", InputType::Cubemap, 512, Q::Approximation, false, "", ""},
        {"forest_blurred", "Forest Blurred", InputType::Cubemap, 128, Q::Approximation, false, "", ""},
        {"st_peters", "St Peter's Basilica", InputType::Cubemap, 512, Q::Approximation, false, "", ""},
        {"st_peters_blurred", "St Peter's Basilica Blurred", InputType::Cubemap, 128, Q::Approximation, false, "", ""},
        {"uffizi", "Uffizi Gallery", InputType::Cubemap, 512, Q::Approximation, false, "", ""},
        {"uffizi_blurred", "Uffizi Gallery Blurred", InputType::Cubemap, 128, Q::Approximation, false, "", ""},
    };
    return list;
}

// ShaderToy media ids and file hashes come from real exported JSON (see
// tests/data). Entries with empty ids are not identified yet.
const BuiltinTexture* findBuiltinTexture(const std::string& id) {
    for (const BuiltinTexture& t : builtinTextures())
        if (id == t.id) return &t;
    return nullptr;
}

const BuiltinTexture* findBuiltinByShadertoyId(const std::string& stId) {
    if (stId.empty()) return nullptr;
    for (const BuiltinTexture& t : builtinTextures())
        if (stId == t.stId) return &t;
    return nullptr;
}

const BuiltinTexture* findBuiltinByMediaPath(const std::string& path) {
    const size_t slash = path.find_last_of('/');
    std::string file = slash == std::string::npos ? path : path.substr(slash + 1);
    const size_t dot = file.find('.');
    if (dot != std::string::npos) file.resize(dot);
    if (file.empty()) return nullptr;
    for (const BuiltinTexture& t : builtinTextures())
        if (file == t.stHash) return &t;
    return nullptr;
}

}  // namespace sp
