#pragma once
// In-memory model of a ShaderToy shader: tabs (passes) and their channel inputs.

#include <array>
#include <string>
#include <vector>

namespace sp {

enum class PassType { Buffer, Image, Volume };  // Volume = ShaderToy's "Volume A" (mainVolume)

enum class InputType {
    None,
    Texture,      // 2D texture (built-in or user override)
    Cubemap,
    Volume,       // 3D texture
    Buffer,       // output of Buffer A-D
    VolumeBuffer, // output of the Volume A pass (3D texture)
    Audio,        // 512x2 FFT + waveform from loopback capture
    Unsupported,  // webcam, video, keyboard, cubemap buffer... (neutral fallback texture)
};

enum class Filter { Nearest, Linear, Mipmap };
enum class Wrap { Clamp, Repeat };

struct SamplerSettings {
    Filter filter = Filter::Mipmap;
    Wrap wrap = Wrap::Repeat;
    bool vflip = true;
    bool srgb = false;
};

struct ChannelInput {
    InputType type = InputType::None;
    std::string asset;          // built-in texture name, for Texture/Cubemap/Volume
    int bufferIndex = -1;       // 0..3 = Buffer A..D, for Buffer (0 for VolumeBuffer)
    SamplerSettings sampler;
    std::string unsupportedWhat;  // e.g. "webcam", for Unsupported
};

struct Pass {
    PassType type = PassType::Image;
    int bufferIndex = -1;  // 0..3 for Buffer A..D, 0 for Volume A
    std::string code;
    std::array<ChannelInput, 4> inputs;

    std::string name() const;  // "Image", "Buffer A", ...
};

struct ShaderInfo {
    std::string id;           // ShaderToy id, or a local id for pasted shaders
    std::string name;
    std::string author;
    std::string description;
    std::string url;
};

struct Shader {
    ShaderInfo info;
    std::string common;       // Common tab, prepended to every pass
    std::vector<Pass> passes; // any order; the renderer sorts them

    const Pass* imagePass() const;
    const Pass* volumePass() const;
};

}  // namespace sp
