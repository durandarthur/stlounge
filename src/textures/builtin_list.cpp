#include "textures/builtin_list.h"

namespace sp {

const std::vector<BuiltinTexture>& builtinTextures() {
    using Q = TextureQuality;
    static const std::vector<BuiltinTexture> list = {
        // Procedural, generated in-app.
        {"rgba_noise_small", "RGBA Noise Small", InputType::Texture, 64, Q::Procedural, true, "XdXGzn",
         "3083c722c0c738cad0f468383167a0d246f91af2bfa373e9c5c094fb8c8413e0"},
        {"rgba_noise_medium", "RGBA Noise Medium", InputType::Texture, 256, Q::Procedural, true, "Xsf3zn",
         "f735bee5b64ef98879dc618b016ecf7939a5756040c2cde21ccb15e69a6e1cfb"},
        {"gray_noise_small", "Gray Noise Small", InputType::Texture, 64, Q::Procedural, true, "4sf3Rn",
         "0a40562379b63dfb89227e6d172f39fdce9022cba76623f1054a2c83d6c0ba5d"},
        {"gray_noise_medium", "Gray Noise Medium", InputType::Texture, 256, Q::Procedural, true, "4dXGzn",
         "0c7bf5fe9462d5bffbd11126e82908e39be3ce56220d900f633d58fb432e56f5"},
        {"bayer", "Bayer", InputType::Texture, 8, Q::Procedural, true, "Xdf3zn",
         "85a6d68622b36995ccb98a89bbb119edf167c914660e4450d313de049320005c"},
        {"blue_noise", "Blue Noise", InputType::Texture, 1024, Q::Procedural, true, "XsBSR3",
         "cb49c003b454385aa9975733aff4571c62182ccdda480aaba9a8d250014f00ec"},
        {"gray_noise_3d", "Grey Noise3D", InputType::Volume, 32, Q::Procedural, true, "4sfGRr",
         "27012b4eadd0c3ce12498b867058e4f717ce79e10a99568cca461682d84a4b04"},
        {"rgba_noise_3d", "RGBA Noise3D", InputType::Volume, 32, Q::Procedural, true, "XdX3Rr",
         "aea6b99da1d53055107966b59ac5444fc8bc7b3ce2d0bbb6a4a3cbae1d97f3aa"},
        // Artistic textures: procedural look-alikes (textures/artistic.cpp), not the originals.
        {"abstract1", "Abstract 1", InputType::Texture, 1024, Q::Approximation, true, "XdX3Rn",
         "52d2a8f514c4fd2d9866587f4d7b2a5bfa1a11a0e772077d7682deb8b3b517e5"},
        {"abstract2", "Abstract 2", InputType::Texture, 512, Q::Approximation, true, "4dX3Rn",
         "bd6464771e47eed832c5eb2cd85cdc0bfc697786b903bfd30f890f9d4fc36657"},
        {"abstract3", "Abstract 3", InputType::Texture, 1024, Q::Approximation, true, "XdXGzr",
         "8979352a182bde7c3c651ba2b2f4e0615de819585cc37b7175bcefbca15a6683"},
        {"font1", "Font 1", InputType::Texture, 1024, Q::Approximation, true, "4dXGzr",
         "08b42b43ae9d3c0605da11d0eac86618ea888e62cdd9518ee8b9097488b31560"},
        {"lichen", "Lichen", InputType::Texture, 1024, Q::Approximation, true, "4sfGRn",
         "fb918796edc3d2221218db0811e240e72e340350008338b0c07a52bd353666a6"},
        {"london", "London", InputType::Texture, 512, Q::Approximation, true, "4dfGRn",
         "8de3a3924cb95bd0e95a443fff0326c869f9d4979cd1d5b6e94e2a01f5be53e9"},
        {"nyancat", "Nyancat", InputType::Texture, 256, Q::Approximation, true, "Xsf3Rn",
         "cbcbb5a6cfb55c36f8f021fbb0e3f69ac96339a39fa85cd96f2017a2192821b5"},
        {"organic1", "Organic 1", InputType::Texture, 1024, Q::Approximation, true, "XsXGRn",
         "cd4c518bc6ef165c39d4405b347b51ba40f8d7a065ab0e8d2e4f422cbc1e8a43"},
        {"organic2", "Organic 2", InputType::Texture, 1024, Q::Approximation, true, "XsX3Rn",
         "92d7758c402f0927011ca8d0a7e40251439fba3a1dac26f5b8b62026323501aa"},
        {"organic3", "Organic 3", InputType::Texture, 1024, Q::Approximation, true, "Xsf3Rr",
         "79520a3d3a0f4d3caa440802ef4362e99d54e12b1392973e4ea321840970a88a"},
        {"organic4", "Organic 4", InputType::Texture, 1024, Q::Approximation, true, "4df3Rr",
         "3871e838723dd6b166e490664eead8ec60aedd6b8d95bc8e2fe3f882f0fd90f0"},
        {"pebbles", "Pebbles", InputType::Texture, 512, Q::Approximation, true, "4sf3Rr",
         "ad56fba948dfba9ae698198c109e71f118a54d209c0ea50d77ea546abad89c57"},
        {"rock_tiles", "Rock Tiles", InputType::Texture, 512, Q::Approximation, true, "4dXGRn",
         "10eb4fe0ac8a7dc348a2cc282ca5df1759ab8bf680117e4047728100969e7b43"},
        {"rusty_metal", "Rusty Metal", InputType::Texture, 512, Q::Approximation, true, "4sXGRn",
         "95b90082f799f48677b4f206d856ad572f1d178c676269eac6347631d4447258"},
        {"stars", "Stars", InputType::Texture, 512, Q::Approximation, true, "XdfGRn",
         "e6e5631ce1237ae4c05b3563eda686400a401df4548d0f9fad40ecac1659c46c"},
        {"wood", "Wood", InputType::Texture, 512, Q::Approximation, true, "XsfGRn",
         "1f7dca9c22f324751f2a5a59c9b181dfe3b5564a04b724c657732d0bf09c99db"},
        {"forest", "Forest", InputType::Cubemap, 512, Q::Approximation, true, "XsX3zn",
         "94284d43be78f00eb6b298e6d78656a1b34e2b91b34940d02f1ca8b22310e8a0"},
        {"forest_blurred", "Forest Blurred", InputType::Cubemap, 128, Q::Approximation, true, "4sX3zn",
         "0681c014f6c88c356cf9c0394ffe015acc94ec1474924855f45d22c3e70b5785"},
        {"st_peters", "St. Peter's Basilica", InputType::Cubemap, 512, Q::Approximation, true, "XdX3zn",
         "488bd40303a2e2b9a71987e48c66ef41f5e937174bf316d3ed0e86410784b919"},
        {"st_peters_blurred", "St. Peter's Basilica Blurred", InputType::Cubemap, 128, Q::Approximation, true, "4dX3zn",
         "550a8cce1bf403869fde66dddf6028dd171f1852f4a704a465e1b80d23955663"},
        {"uffizi", "Uffizi Gallery", InputType::Cubemap, 512, Q::Approximation, true, "XsfGzn",
         "585f9546c092f53ded45332b343144396c0b2d70d9965f585ebc172080d8aa58"},
        {"uffizi_blurred", "Uffizi Gallery Blurred", InputType::Cubemap, 128, Q::Approximation, true, "4sfGzn",
         "793a105653fbdadabdc1325ca08675e1ce48ae5f12e37973829c87bea4be3232"},
    };
    return list;
}

// ShaderToy media ids and file hashes come from exported JSON catalogues in
// tests/data (catalogue_*.json), one channel per known texture.
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
