#include "app/demos.h"

#include "app/builtin_shader.h"

namespace sp {

Shader makeImageShader(const std::string& name, const std::string& code) {
    Shader s;
    s.info.name = name;
    Pass image;
    image.type = PassType::Image;
    image.code = code;
    s.passes.push_back(image);
    return s;
}

Shader makeMultipassDemo() {
    Shader s;
    s.info.name = "Multipass demo";
    Pass a;
    a.type = PassType::Buffer;
    a.bufferIndex = 0;
    a.code = kDemoBufferA;
    a.inputs[0].type = InputType::Buffer;
    a.inputs[0].bufferIndex = 0;
    a.inputs[0].sampler = {Filter::Linear, Wrap::Clamp, false, false};
    Pass image;
    image.type = PassType::Image;
    image.code = kDemoImage;
    image.inputs[0] = a.inputs[0];
    image.inputs[1].type = InputType::Texture;
    image.inputs[1].asset = "rgba_noise_medium";
    image.inputs[1].sampler = {Filter::Mipmap, Wrap::Repeat, true, false};
    s.passes = {a, image};
    return s;
}

Shader makeAudioDemo() {
    Shader s = makeImageShader("Audio demo", kDemoAudio);
    s.passes[0].inputs[0].type = InputType::Audio;
    s.passes[0].inputs[0].sampler = {Filter::Linear, Wrap::Clamp, false, false};
    return s;
}

Shader makeTestPattern() { return makeImageShader("Built-in test pattern", kBuiltinShader); }

}  // namespace sp
