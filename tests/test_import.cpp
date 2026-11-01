// ShaderToy JSON import/export, library storage and the paste dialog's clipboard handling.

#include <nlohmann/json.hpp>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

#include <set>

#include "library/library.h"
#include "shadertoy/json_format.h"
#include "shadertoy/shader_ref.h"
#include "shadertoy/wrapper.h"
#include "test.h"
#include "textures/builtin_list.h"
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
    CHECK_EQ(stored["stlounge"]["source"].get<std::string>(), std::string("import"));

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

TEST(import_catalogues_map_every_builtin_texture) {
    // One channel per ShaderToy texture, cubemap and volume, as labelled in the files.
    struct Row {
        const char* file;
        const char* pass;
        int channel;
        const char* builtin;
    };
    const Row rows[] = {
        {"catalogue_textures.json", "Buffer A", 0, "abstract1"},
        {"catalogue_textures.json", "Buffer A", 1, "abstract2"},
        {"catalogue_textures.json", "Buffer A", 2, "abstract3"},
        {"catalogue_textures.json", "Buffer A", 3, "bayer"},
        {"catalogue_textures.json", "Buffer B", 0, "blue_noise"},
        {"catalogue_textures.json", "Buffer B", 1, "font1"},
        {"catalogue_textures.json", "Buffer B", 2, "gray_noise_medium"},
        {"catalogue_textures.json", "Buffer B", 3, "gray_noise_small"},
        {"catalogue_textures.json", "Buffer D", 0, "lichen"},
        {"catalogue_textures.json", "Buffer D", 1, "london"},
        {"catalogue_textures.json", "Buffer D", 2, "nyancat"},
        {"catalogue_textures.json", "Buffer D", 3, "organic1"},
        {"catalogue_textures.json", "Buffer C", 0, "organic2"},
        {"catalogue_textures.json", "Buffer C", 1, "organic3"},
        {"catalogue_textures.json", "Buffer C", 2, "organic4"},
        {"catalogue_textures.json", "Buffer C", 3, "pebbles"},
        {"catalogue_textures.json", "Volume A", 0, "rgba_noise_medium"},
        {"catalogue_textures.json", "Volume A", 1, "rgba_noise_small"},
        {"catalogue_textures.json", "Volume A", 2, "rock_tiles"},
        {"catalogue_textures.json", "Volume A", 3, "rusty_metal"},
        {"catalogue_textures.json", "Cube A", 0, "stars"},
        {"catalogue_textures.json", "Cube A", 1, "wood"},
        {"catalogue_cubemaps.json", "Image", 0, "forest"},
        {"catalogue_cubemaps.json", "Image", 1, "forest_blurred"},
        {"catalogue_cubemaps.json", "Image", 2, "st_peters"},
        {"catalogue_cubemaps.json", "Image", 3, "st_peters_blurred"},
        {"catalogue_cubemaps.json", "Buffer A", 0, "uffizi"},
        {"catalogue_cubemaps.json", "Buffer A", 1, "uffizi_blurred"},
        {"catalogue_volumes.json", "Image", 0, "gray_noise_3d"},
        {"catalogue_volumes.json", "Image", 1, "rgba_noise_3d"},
    };
    std::set<std::string> covered;
    for (const char* file : {"catalogue_textures.json", "catalogue_cubemaps.json", "catalogue_volumes.json"}) {
        auto r = parseShadertoyJson(readText(kData / file));
        REQUIRE(r.size() == size_t(1) && r[0].ok);
        for (const std::string& w : r[0].warnings)
            if (w.find("Cubemap A tab") == std::string::npos) std::printf("    %s: %s\n", file, w.c_str());
        for (const Row& row : rows) {
            if (std::string(row.file) != file) continue;
            const Pass* pass = nullptr;
            for (const Pass& p : r[0].shader.passes)
                if (p.name() == row.pass) pass = &p;
            if (!pass) continue;  // Cube A tab is skipped on import; its inputs are checked below
            CHECK_EQ(pass->inputs[size_t(row.channel)].asset, std::string(row.builtin));
            covered.insert(row.builtin);
        }
    }
    // The Cube A tab (Stars, Wood) is skipped on import, so check those two by id directly.
    CHECK(findBuiltinByShadertoyId("XdfGRn") == findBuiltinTexture("stars"));
    CHECK(findBuiltinByMediaPath("/media/a/1f7dca9c22f324751f2a5a59c9b181dfe3b5564a04b724c657732d0bf09c99db.jpg") ==
          findBuiltinTexture("wood"));
    covered.insert("stars");
    covered.insert("wood");
    for (const BuiltinTexture& t : builtinTextures()) {
        CHECK(*t.stId != '\0');
        if (!covered.count(t.id)) std::printf("    not covered by a catalogue: %s\n", t.id);
        CHECK(covered.count(t.id) == 1);
    }
}

TEST(docs_volume_check_json_imports) {
    auto r = parseShadertoyJson(readText(kData / ".." / ".." / "docs" / "tests" / "volume-a-check.json"));
    REQUIRE(r.size() == size_t(1) && r[0].ok);
    CHECK(r[0].warnings.empty());
    REQUIRE(r[0].shader.volumePass() != nullptr);
    CHECK(r[0].shader.volumePass()->inputs[0].type == InputType::VolumeBuffer);
    CHECK(r[0].shader.imagePass()->inputs[0].type == InputType::Buffer);
}

TEST(library_bulk_add_defers_rescan) {
    ShaderLibrary lib(tempDir("sp_test_library_bulk"));
    lib.scan();
    std::string err;
    for (int i = 0; i < 20; ++i) {
        json obj = json::parse(R"({"info": {"id": "-1", "name": "n"}, "renderpass": [{"type": "image", "code": "x"}]})");
        obj["info"]["name"] = "shader " + std::to_string(i);
        CHECK(!lib.add(obj, "import", &err, false).empty());
    }
    CHECK(lib.entries().empty());  // not rescanned yet
    lib.scan();
    CHECK_EQ(lib.entries().size(), size_t(20));
    for (int i = 0; i < 10; ++i) CHECK(lib.remove(lib.entries()[size_t(i)].id, &err, false));
    lib.scan();
    CHECK_EQ(lib.entries().size(), size_t(10));
    fs::remove_all(lib.dir());
}

TEST(shader_ref_parsing) {
    CHECK(parseShaderRef("XsX3Rr") == std::optional<std::string>("XsX3Rr"));
    CHECK(parseShaderRef("  XsX3Rr \n") == std::optional<std::string>("XsX3Rr"));
    CHECK(parseShaderRef("https://www.shadertoy.com/view/XsX3Rr") == std::optional<std::string>("XsX3Rr"));
    CHECK(parseShaderRef("http://shadertoy.com/view/XsX3Rr/") == std::optional<std::string>("XsX3Rr"));
    CHECK(parseShaderRef("HTTPS://WWW.ShaderToy.com/view/XsX3Rr?foo=1") == std::optional<std::string>("XsX3Rr"));
    CHECK(parseShaderRef("www.shadertoy.com/embed/XsX3Rr#x") == std::optional<std::string>("XsX3Rr"));
    CHECK(!parseShaderRef(""));
    CHECK(!parseShaderRef("XsX3R"));
    CHECK(!parseShaderRef("Xs-3Rr"));
    CHECK(!parseShaderRef("https://example.com/view/XsX3Rr"));
    CHECK(!parseShaderRef("https://shadertoy.com.evil.example/view/XsX3Rr"));
    CHECK(!parseShaderRef("https://www.shadertoy.com/user/XsX3Rr"));
    CHECK(!parseShaderRef("https://www.shadertoy.com/view/XsX3Rr/extra"));
    CHECK(!isShaderLink("XsX3Rr"));  // a bare word isn't treated as a link
    CHECK(isShaderLink(" shadertoy.com/view/XsX3Rr "));
}

TEST(paste_dialog_explains_links) {
    PasteDialog d;
    d.open("https://www.shadertoy.com/view/XsX3Rr");
    REQUIRE(!d.notes().empty());
    CHECK(d.notes()[0].find("export") != std::string::npos);
    CHECK(d.editor().build().passes.size() <= 1);
}

TEST(paste_dialog_edit_mode) {
    PasteDialog d;
    Shader s;
    s.info.name = "Mine";
    s.info.author = "me";
    Pass image;
    image.type = PassType::Image;
    image.code = "void mainImage(out vec4 c, in vec2 f) { c = vec4(1); }";
    s.passes.push_back(image);
    d.openForEdit(s, "local-12345678");
    CHECK_EQ(d.editId(), std::string("local-12345678"));
    CHECK_EQ(d.shader().info.name, std::string("Mine"));
    CHECK_EQ(d.shader().passes.size(), size_t(1));
    d.open("");  // adding a new one leaves edit mode
    CHECK(d.editId().empty());
}
