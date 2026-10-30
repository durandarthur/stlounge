// ShaderToy JSON import/export, library storage and the paste dialog's clipboard handling.

#include <nlohmann/json.hpp>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "library/library.h"
#include "shadertoy/json_format.h"
#include "shadertoy/wrapper.h"
#include "test.h"
#include "ui/paste_dialog.h"

using namespace sp;
using json = nlohmann::json;
namespace fs = std::filesystem;

namespace {

const fs::path kData = SP_TEST_DATA_DIR;

std::string readText(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

const Pass* findPass(const Shader& s, PassType t, int index = -1) {
    for (const Pass& p : s.passes)
        if (p.type == t && (index < 0 || p.bufferIndex == index)) return &p;
    return nullptr;
}

bool hasWarning(const std::vector<std::string>& w, const std::string& part) {
    for (const std::string& s : w)
        if (s.find(part) != std::string::npos) return true;
    return false;
}

fs::path tempDir(const char* name) {
    const char* tmp = std::getenv("TMPDIR");
    fs::path d = fs::path(tmp && *tmp ? tmp : "/tmp") / name;
    fs::remove_all(d);
    fs::create_directories(d);
    return d;
}

}  // namespace

TEST(import_catalogue_pass_and_input_types) {
    auto results = parseShadertoyJson(readText(kData / "catalogue_input_types.json"));
    REQUIRE(results.size() == size_t(1));
    const ImportResult& r = results[0];
    if (!r.ok) std::printf("    error: %s\n", r.error.c_str());
    REQUIRE(r.ok);
    const Shader& s = r.shader;
    CHECK(s.info.url.empty());     // id "-1": not on shadertoy.com
    CHECK(s.info.author.empty());  // "None"
    CHECK(s.common.find("someFunction") != std::string::npos);

    CHECK(findPass(s, PassType::Image) != nullptr);
    CHECK(findPass(s, PassType::Volume) != nullptr);
    for (int b = 0; b < 4; ++b) CHECK(findPass(s, PassType::Buffer, b) != nullptr);
    CHECK(hasWarning(r.warnings, "Sound tab"));
    CHECK(hasWarning(r.warnings, "Cubemap A tab"));

    const Pass* a = findPass(s, PassType::Buffer, 0);
    REQUIRE(a != nullptr);
    for (int c = 0; c < 4; ++c) {
        CHECK(a->inputs[size_t(c)].type == InputType::Buffer);
        CHECK_EQ(a->inputs[size_t(c)].bufferIndex, c);
    }
    CHECK(a->inputs[2].sampler.wrap == Wrap::Repeat);
    CHECK(a->inputs[3].sampler.filter == Filter::Nearest);

    const Pass* b = findPass(s, PassType::Buffer, 1);
    REQUIRE(b != nullptr);
    CHECK(b->inputs[0].type == InputType::VolumeBuffer);
    CHECK(b->inputs[1].type == InputType::Unsupported);
    CHECK_EQ(b->inputs[1].unsupportedWhat, std::string("Cubemap A"));
    CHECK_EQ(b->inputs[2].unsupportedWhat, std::string("Keyboard"));
    CHECK_EQ(b->inputs[3].unsupportedWhat, std::string("Webcam"));

    const Pass* c = findPass(s, PassType::Buffer, 2);
    REQUIRE(c != nullptr);
    CHECK(c->inputs[0].type == InputType::Audio);  // mic -> loopback
    CHECK(c->inputs[1].type == InputType::Texture);

    const Pass* v = findPass(s, PassType::Volume);
    REQUIRE(v != nullptr);
    for (const ChannelInput& in : v->inputs) CHECK(in.type == InputType::Texture);
}

TEST(import_api_format_and_arrays) {
    const char* api = R"({"Shader": {"ver": "0.1",
        "info": {"id": "AbCdEf", "name": "API shader", "username": "someone"},
        "renderpass": [
          {"type": "buffer", "name": "Buffer B", "code": "b", "outputs": [{"id": "XsXGR8", "channel": 0}],
           "inputs": [{"ctype": "buffer", "id": "XsXGR8", "src": "/media/previz/buffer01.png", "channel": 0,
                       "sampler": {"filter": "linear", "wrap": "clamp", "vflip": false, "srgb": false}}]},
          {"type": "image", "name": "Image", "code": "img",
           "inputs": [{"ctype": "texture", "id": "Xsf3zn", "src": "/media/a/whatever.png", "channel": 3},
                      {"ctype": "musicstream", "id": "x", "channel": 1},
                      {"ctype": "video", "id": "y", "channel": 2}]}]}})";
    auto results = parseShadertoyJson(api);
    REQUIRE(results.size() == size_t(1) && results[0].ok);
    const Shader& s = results[0].shader;
    CHECK_EQ(s.info.url, std::string("https://www.shadertoy.com/view/AbCdEf"));
    CHECK_EQ(s.info.author, std::string("someone"));
    const Pass* b = findPass(s, PassType::Buffer, 1);
    REQUIRE(b != nullptr);
    CHECK(b->inputs[0].type == InputType::Buffer && b->inputs[0].bufferIndex == 1);
    CHECK(!b->inputs[0].sampler.vflip);  // booleans accepted as well as strings
    const Pass* img = findPass(s, PassType::Image);
    REQUIRE(img != nullptr);
    CHECK_EQ(img->inputs[3].asset, std::string("rgba_noise_medium"));  // by media id
    CHECK(img->inputs[1].type == InputType::Audio);
    CHECK(img->inputs[2].type == InputType::Unsupported);

    const std::string arr = std::string("[") + api + "," + api + "]";
    CHECK_EQ(parseShadertoyJson(arr).size(), size_t(2));
}

TEST(import_rejects_bad_input_without_throwing) {
    CHECK(!parseShadertoyJson("not json")[0].ok);
    CHECK(!parseShadertoyJson("{}")[0].ok);
    CHECK(!parseShadertoyJson(R"({"renderpass": [{"type": "buffer", "code": "x"}]})")[0].ok);  // no Image
    // Wrong types everywhere must not throw.
    auto r = parseShadertoyJson(R"({"info": {"name": 5, "id": []}, "renderpass": [
        {"type": "image", "code": 7, "inputs": [{"channel": "x"}, {"channel": 9}, 3, {"channel": 0, "type": null}]}]})");
    REQUIRE(r.size() == size_t(1));
    CHECK(r[0].ok);
    CHECK(looksLikeShaderJson("  {\"renderpass\": []}"));
    CHECK(!looksLikeShaderJson("void mainImage(...) { \"renderpass\" }"));
}

TEST(export_round_trip) {
    auto in = parseShadertoyJson(readText(kData / "catalogue_input_types.json"));
    REQUIRE(in[0].ok);
    const Shader& s = in[0].shader;
    ImportResult back = shaderFromJson(shaderToJson(s));
    REQUIRE(back.ok);
    REQUIRE(back.shader.passes.size() == s.passes.size());
    CHECK_EQ(back.shader.common, s.common);
    for (size_t i = 0; i < s.passes.size(); ++i) {
        const Pass& p = s.passes[i];
        const Pass* q = findPass(back.shader, p.type, p.type == PassType::Buffer ? p.bufferIndex : -1);
        REQUIRE(q != nullptr);
        CHECK_EQ(q->code, p.code);
        for (size_t c = 0; c < 4; ++c) {
            CHECK(q->inputs[c].type == p.inputs[c].type);
            CHECK_EQ(q->inputs[c].asset, p.inputs[c].asset);
            CHECK_EQ(q->inputs[c].bufferIndex, p.inputs[c].bufferIndex);
            CHECK_EQ(q->inputs[c].unsupportedWhat, p.inputs[c].unsupportedWhat);
            CHECK(q->inputs[c].sampler.filter == p.inputs[c].sampler.filter);
            CHECK(q->inputs[c].sampler.vflip == p.inputs[c].sampler.vflip);
        }
    }
}

TEST(import_private_samples) {
    const fs::path dir = kData / "private";
    if (!fs::is_directory(dir)) SKIP("no private samples in tests/data/private");
    int count = 0;
    for (const auto& de : fs::directory_iterator(dir)) {
        if (de.path().extension() != ".json") continue;
        auto r = parseShadertoyJson(readText(de.path()));
        REQUIRE(r.size() == size_t(1));
        if (!r[0].ok) std::printf("    %s: %s\n", de.path().filename().c_str(), r[0].error.c_str());
        CHECK(r[0].ok);
        ++count;
    }
    std::printf("    %d sample files parsed\n", count);
    const fs::path tunnel = dir / "XtdGR7.json";
    if (fs::exists(tunnel)) {
        auto r = parseShadertoyJson(readText(tunnel));
        const Pass* img = findPass(r[0].shader, PassType::Image);
        REQUIRE(img != nullptr);
        CHECK_EQ(img->inputs[0].asset, std::string("rgba_noise_medium"));
        CHECK(!img->inputs[0].sampler.vflip);
        CHECK(img->inputs[3].type == InputType::Audio);  // SoundCloud music stream
    }
}

TEST(library_add_load_remove) {
    ShaderLibrary lib(tempDir("sp_test_library"));
    lib.scan();
    CHECK(lib.entries().empty());

    // A shader from shadertoy.com keeps its id; extra keys are kept (lossless).
    json st = json::parse(R"({"ver": "0.1", "flags": {"mFlagVR": false}, "somethingNew": [1, 2],
        "info": {"id": "AbCdEf", "name": "Zebra", "username": "x"},
        "renderpass": [{"type": "image", "name": "Image", "code": "void mainImage(out vec4 c, in vec2 p) {}"}]})");
    std::string err;
    CHECK_EQ(lib.add(st, "import", &err), std::string("AbCdEf"));
    json stored = json::parse(readText(lib.dir() / "AbCdEf.json"));
    CHECK(stored.contains("somethingNew"));
    CHECK(stored.contains("flags"));
    CHECK_EQ(stored["shaderplayer"]["source"].get<std::string>(), std::string("import"));

    // A pasted shader gets a local id, and keeps it when saved again.
    Shader pasted;
    pasted.info.name = "alpha";
    Pass img;
    img.type = PassType::Image;
    img.code = "void mainImage(out vec4 c, in vec2 p) { c = vec4(1); }";
    pasted.passes.push_back(img);
    const std::string local = lib.addShader(pasted, "paste", &err);
    CHECK(local.rfind("local-", 0) == 0);
    REQUIRE(lib.entries().size() == size_t(2));
    CHECK_EQ(lib.entries()[0].name, std::string("alpha"));  // sorted by name, case-insensitive

    auto loaded = lib.load(local, nullptr, &err);
    REQUIRE(loaded.has_value());
    CHECK_EQ(loaded->info.id, local);
    CHECK_EQ(loaded->imagePass()->code, img.code);
    CHECK_EQ(lib.addShader(*loaded, "paste", &err), local);  // re-save: same file
    CHECK_EQ(lib.entries().size(), size_t(2));

    CHECK(lib.remove(local, &err));
    CHECK_EQ(lib.entries().size(), size_t(1));
    CHECK(!lib.remove("../../etc/passwd", &err));  // unsafe ids never reach the filesystem
    CHECK(!lib.load("../x", nullptr, &err).has_value());
    CHECK(!ShaderLibrary::isSafeId("a/b"));
    CHECK(ShaderLibrary::isSafeId("local-1a2b3c4d"));
    fs::remove_all(lib.dir());
}

TEST(library_lists_broken_files) {
    const fs::path dir = tempDir("sp_test_library_broken");
    std::ofstream(dir / "bad.json") << "{ not json";
    std::ofstream(dir / "notes.txt") << "ignored";
    ShaderLibrary lib(dir);
    lib.scan();
    REQUIRE(lib.entries().size() == size_t(1));
    CHECK(!lib.entries()[0].loadError.empty());
    std::string err;
    CHECK(!lib.load("bad", nullptr, &err).has_value());
    CHECK(!err.empty());
    fs::remove_all(dir);
}

TEST(paste_dialog_fills_from_clipboard) {
    PasteDialog d;
    d.open(readText(kData / "catalogue_input_types.json"));
    Shader s = d.shader();
    CHECK(s.volumePass() != nullptr);
    CHECK_EQ(s.passes.size(), size_t(6));  // Volume A, Buffers A-D, Image
    CHECK(!d.unchangedOriginalJson().empty());  // untouched: stored as-is

    d.open("void mainImage(out vec4 c, in vec2 p) { c = vec4(1); }");
    s = d.shader();
    REQUIRE(s.passes.size() == size_t(1));
    CHECK(s.imagePass()->code.find("mainImage") != std::string::npos);
    CHECK(d.unchangedOriginalJson().empty());

    d.open("hello");
    CHECK(d.shader().imagePass()->code.empty());
    CHECK(!d.notes().empty());
}

TEST(volume_static_detection) {
    Pass v;
    v.type = PassType::Volume;
    v.code = "void mainVolume(out vec4 c, in vec3 p) { c = vec4(p.x / iResolution.x); }";
    CHECK(!passIsDynamic("", v));
    CHECK(passIsDynamic("float t() { return iTime; }", v));  // via Common
    v.code += " // iFrame";
    CHECK(passIsDynamic("", v));
    v.code = "void mainVolume(out vec4 c, in vec3 p) { c = vec4(0); }";
    v.inputs[0].type = InputType::VolumeBuffer;
    CHECK(passIsDynamic("", v));
    v.inputs[0].type = InputType::Texture;
    CHECK(!passIsDynamic("", v));
    v.code = "float iTimeScale = 1.0;";  // whole words only
    CHECK(!passIsDynamic("", v));
}
