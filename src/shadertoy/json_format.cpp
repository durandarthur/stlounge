#include "shadertoy/json_format.h"

#include <nlohmann/json.hpp>

#include <cctype>
#include <ctime>

#include "textures/builtin_list.h"

namespace sp {

using json = nlohmann::json;

namespace {

// Fixed output ids ShaderToy gives each tab (seen in exported files).
const char* const kBufferIds[4] = {"4dXGR8", "XsXGR8", "4sXGR8", "XdfGR8"};
const char* const kImageOutputId = "4dfGRr";
const char* const kCubeAOutputId = "4dX3Rr";
const char* const kVolumeAOutputId = "XsX3Rr";

std::string str(const json& obj, const char* key, const std::string& fallback = "") {
    auto it = obj.find(key);
    if (it == obj.end() || it->is_null()) return fallback;
    if (it->is_string()) return it->get<std::string>();
    if (it->is_number_integer()) return std::to_string(it->get<long long>());
    return fallback;
}

// Sampler flags appear as "true"/"false" strings, sometimes as booleans.
bool flag(const json& obj, const char* key, bool fallback) {
    auto it = obj.find(key);
    if (it == obj.end()) return fallback;
    if (it->is_boolean()) return it->get<bool>();
    if (it->is_string()) return it->get<std::string>() == "true";
    return fallback;
}

int bufferIndexFromId(const std::string& id, const std::string& path) {
    for (int i = 0; i < 4; ++i)
        if (id == kBufferIds[i]) return i;
    // Fallback: preview path "/media/previz/buffer02.png"
    const size_t p = path.find("buffer0");
    if (p != std::string::npos && p + 7 < path.size()) {
        const char c = path[p + 7];
        if (c >= '0' && c <= '3') return c - '0';
    }
    return -1;
}

SamplerSettings parseSampler(const json& in) {
    SamplerSettings s;
    auto it = in.find("sampler");
    if (it == in.end() || !it->is_object()) return s;
    const std::string f = str(*it, "filter", "mipmap");
    s.filter = f == "nearest" ? Filter::Nearest : f == "linear" ? Filter::Linear : Filter::Mipmap;
    s.wrap = str(*it, "wrap", "repeat") == "clamp" ? Wrap::Clamp : Wrap::Repeat;
    s.vflip = flag(*it, "vflip", true);
    s.srgb = flag(*it, "srgb", false);
    return s;
}

std::string readableType(const std::string& type) {
    if (type == "keyboard") return "Keyboard";
    if (type == "webcam") return "Webcam";
    if (type == "video") return "Video";
    if (type.empty()) return "Unknown input";
    return type;
}

ChannelInput parseInput(const json& in, const std::string& where, std::vector<std::string>& warnings) {
    ChannelInput c;
    c.sampler = parseSampler(in);
    std::string type = str(in, "type");
    if (type.empty()) type = str(in, "ctype");  // API format
    const std::string id = str(in, "id");
    std::string path = str(in, "filepath");
    if (path.empty()) path = str(in, "src");  // API format

    auto media = [&](InputType kind, const char* kindName) {
        // Our own exports carry the built-in id directly.
        const BuiltinTexture* t = findBuiltinTexture(str(in, "sp_asset"));
        if (!t) t = findBuiltinByShadertoyId(id);
        if (!t) t = findBuiltinByMediaPath(path);
        c.type = kind;
        if (t) {
            c.type = t->kind;
            c.asset = t->id;
        } else {
            c.asset = "shadertoy:" + (id.empty() ? path : id);
            warnings.push_back(where + "unknown " + kindName + " (ShaderToy id " + id + "); it will be black.");
        }
    };

    if (type == "texture") {
        media(InputType::Texture, "texture");
    } else if (type == "cubemap") {
        if (id == kCubeAOutputId || path.find("previz/cubemap") != std::string::npos) {
            c.type = InputType::Unsupported;
            c.unsupportedWhat = "Cubemap A";
        } else {
            media(InputType::Cubemap, "cubemap");
        }
    } else if (type == "volume") {
        if (id == kVolumeAOutputId || path.find("previz/volume") != std::string::npos) {
            c.type = InputType::VolumeBuffer;
            c.bufferIndex = 0;
        } else {
            media(InputType::Volume, "volume");
        }
    } else if (type == "buffer") {
        c.type = InputType::Buffer;
        c.bufferIndex = bufferIndexFromId(id, path);
        if (c.bufferIndex < 0) {
            c.type = InputType::Unsupported;
            c.unsupportedWhat = "unknown buffer " + id;
        }
    } else if (type == "mic" || type == "music" || type == "musicstream" || type == "soundcloud") {
        c.type = InputType::Audio;  // all audio sources map to the desktop loopback
    } else {
        c.type = InputType::Unsupported;
        c.unsupportedWhat = readableType(type);
    }
    return c;
}

}  // namespace

bool isShadertoyId(const std::string& id) {
    if (id.size() != 6) return false;
    for (char ch : id)
        if (!std::isalnum(static_cast<unsigned char>(ch))) return false;
    return true;
}

ImportResult shaderFromJson(const json& root) {
    ImportResult r;
    if (!root.is_object()) {
        r.error = "not a shader object";
        return r;
    }
    const json* obj = &root;
    if (auto it = root.find("Shader"); it != root.end() && it->is_object()) obj = &*it;

    auto passesIt = obj->find("renderpass");
    if (passesIt == obj->end() || !passesIt->is_array()) {
        r.error = "no \"renderpass\" list: this doesn't look like ShaderToy JSON";
        return r;
    }

    if (auto info = obj->find("info"); info != obj->end() && info->is_object()) {
        r.shader.info.id = str(*info, "id");
        r.shader.info.name = str(*info, "name");
        r.shader.info.author = str(*info, "username");
        r.shader.info.description = str(*info, "description");
        if (r.shader.info.author == "None") r.shader.info.author.clear();
    }
    if (isShadertoyId(r.shader.info.id)) r.shader.info.url = "https://www.shadertoy.com/view/" + r.shader.info.id;
    if (r.shader.info.name.empty()) r.shader.info.name = "Untitled";

    bool seenImage = false;
    for (const json& p : *passesIt) {
        if (!p.is_object()) continue;
        const std::string type = str(p, "type");
        const std::string name = str(p, "name", type);
        const std::string code = str(p, "code");

        if (type == "common") {
            if (!r.shader.common.empty()) r.shader.common += "\n";
            r.shader.common += code;
            continue;
        }
        if (type == "sound") {
            r.warnings.push_back("Sound tab (mainSound) is not supported; the shader plays without its own sound.");
            continue;
        }
        if (type == "cubemap") {
            r.warnings.push_back("Cubemap A tab is not supported; inputs that read it will be black.");
            continue;
        }

        Pass pass;
        pass.code = code;
        if (type == "image") {
            if (seenImage) {
                r.warnings.push_back("more than one Image tab; using the first.");
                continue;
            }
            seenImage = true;
            pass.type = PassType::Image;
        } else if (type == "buffer") {
            pass.type = PassType::Buffer;
            std::string outId;
            if (auto o = p.find("outputs"); o != p.end() && o->is_array() && !o->empty() && (*o)[0].is_object())
                outId = str((*o)[0], "id");
            pass.bufferIndex = bufferIndexFromId(outId, "");
            if (pass.bufferIndex < 0 && name.size() == 8 && name.rfind("Buffer ", 0) == 0)
                pass.bufferIndex = name[7] - 'A';
            if (pass.bufferIndex < 0 || pass.bufferIndex > 3) {
                r.warnings.push_back("tab '" + name + "' is not Buffer A-D; skipped.");
                continue;
            }
        } else if (type == "volume") {
            pass.type = PassType::Volume;
            pass.bufferIndex = 0;
        } else {
            r.warnings.push_back("tab '" + name + "' has unknown type '" + type + "'; skipped.");
            continue;
        }

        if (auto ins = p.find("inputs"); ins != p.end() && ins->is_array()) {
            for (const json& in : *ins) {
                if (!in.is_object()) continue;
                auto ch = in.find("channel");
                if (ch == in.end() || !ch->is_number_integer()) continue;
                const int channel = ch->get<int>();
                if (channel < 0 || channel > 3) continue;
                const std::string where = pass.name() + " iChannel" + std::to_string(channel) + ": ";
                ChannelInput c = parseInput(in, where, r.warnings);
                if (c.type == InputType::Unsupported)
                    r.warnings.push_back(where + c.unsupportedWhat + " input is not supported; it will be black.");
                pass.inputs[static_cast<size_t>(channel)] = c;
            }
        }
        r.shader.passes.push_back(pass);
    }

    if (!seenImage) {
        r.error = "the shader has no Image tab";
        return r;
    }
    r.ok = true;
    return r;
}

std::vector<json> shaderObjects(const std::string& text, std::string* error) {
    json doc = json::parse(text, nullptr, false);
    if (doc.is_discarded()) {
        if (error) *error = "not valid JSON";
        return {};
    }
    std::vector<json> out;
    auto unwrap = [&](const json& j) {
        if (j.is_object() && j.contains("Shader") && j["Shader"].is_object()) out.push_back(j["Shader"]);
        else if (j.is_object() && j.contains("renderpass")) out.push_back(j);
    };
    if (doc.is_array()) {
        for (const json& j : doc) unwrap(j);
    } else if (doc.is_object() && doc.contains("Shaders") && doc["Shaders"].is_array()) {
        for (const json& j : doc["Shaders"]) unwrap(j);
    } else {
        unwrap(doc);
    }
    if (out.empty() && error) *error = "the JSON doesn't contain a ShaderToy shader (no \"renderpass\")";
    return out;
}

std::vector<ImportResult> parseShadertoyJson(const std::string& text) {
    std::string error;
    std::vector<json> objs = shaderObjects(text, &error);
    std::vector<ImportResult> results;
    if (objs.empty()) {
        ImportResult r;
        r.error = error;
        results.push_back(r);
        return results;
    }
    for (const json& o : objs) results.push_back(shaderFromJson(o));
    return results;
}

bool looksLikeShaderJson(const std::string& text) {
    const size_t start = text.find_first_not_of(" \t\r\n");
    if (start == std::string::npos || (text[start] != '{' && text[start] != '[')) return false;
    return text.find("\"renderpass\"") != std::string::npos;
}

// ------------------------------------------------------------------ export

namespace {

json samplerJson(const SamplerSettings& s) {
    return {{"filter", s.filter == Filter::Nearest ? "nearest" : s.filter == Filter::Linear ? "linear" : "mipmap"},
            {"wrap", s.wrap == Wrap::Clamp ? "clamp" : "repeat"},
            {"vflip", s.vflip ? "true" : "false"},
            {"srgb", s.srgb ? "true" : "false"},
            {"internal", "byte"}};
}

json inputJson(const ChannelInput& in, int channel) {
    json j = {{"channel", channel}, {"sampler", samplerJson(in.sampler)}};
    switch (in.type) {
        case InputType::None:
            return nullptr;
        case InputType::Texture:
        case InputType::Cubemap:
        case InputType::Volume: {
            j["type"] = in.type == InputType::Texture ? "texture" : in.type == InputType::Cubemap ? "cubemap" : "volume";
            const BuiltinTexture* t = findBuiltinTexture(in.asset);
            if (t) {
                j["sp_asset"] = t->id;
                j["id"] = t->stId;
                j["filepath"] = *t->stHash ? std::string("/media/a/") + t->stHash : "";
            } else {
                const std::string prefix = "shadertoy:";
                j["id"] = in.asset.rfind(prefix, 0) == 0 ? in.asset.substr(prefix.size()) : in.asset;
                j["filepath"] = "";
            }
            break;
        }
        case InputType::Buffer:
            j["type"] = "buffer";
            j["id"] = in.bufferIndex >= 0 && in.bufferIndex < 4 ? kBufferIds[in.bufferIndex] : "";
            j["filepath"] = "/media/previz/buffer0" + std::to_string(std::max(0, in.bufferIndex)) + ".png";
            break;
        case InputType::VolumeBuffer:
            j["type"] = "volume";
            j["id"] = kVolumeAOutputId;
            j["filepath"] = "/media/previz/volume00.png";
            break;
        case InputType::Audio:
            j["type"] = "mic";
            j["id"] = "";
            j["filepath"] = "";
            break;
        case InputType::Unsupported: {
            const std::string& w = in.unsupportedWhat;
            if (w == "Cubemap A") {
                j["type"] = "cubemap";
                j["id"] = kCubeAOutputId;
                j["filepath"] = "/media/previz/cubemap00.png";
            } else {
                std::string t = w;
                for (char& ch : t) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
                j["type"] = t;
                j["id"] = "";
                j["filepath"] = nullptr;
            }
            break;
        }
    }
    return j;
}

json passJson(const Pass& p) {
    json inputs = json::array();
    for (int c = 0; c < 4; ++c) {
        json in = inputJson(p.inputs[static_cast<size_t>(c)], c);
        if (!in.is_null()) inputs.push_back(in);
    }
    std::string type = "image", outId = kImageOutputId;
    if (p.type == PassType::Buffer) {
        type = "buffer";
        outId = p.bufferIndex >= 0 && p.bufferIndex < 4 ? kBufferIds[p.bufferIndex] : "";
    } else if (p.type == PassType::Volume) {
        type = "volume";
        outId = kVolumeAOutputId;
    }
    return {{"inputs", inputs},
            {"outputs", json::array({{{"channel", 0}, {"id", outId}}})},
            {"code", p.code},
            {"name", p.name()},
            {"description", ""},
            {"type", type}};
}

}  // namespace

json shaderToJson(const Shader& s) {
    json passes = json::array();
    if (!s.common.empty())
        passes.push_back({{"inputs", json::array()},
                          {"outputs", json::array()},
                          {"code", s.common},
                          {"name", "Common"},
                          {"description", ""},
                          {"type", "common"}});
    for (const Pass& p : s.passes) passes.push_back(passJson(p));
    return {{"ver", "0.1"},
            {"info",
             {{"id", s.info.id.empty() ? "-1" : s.info.id},
              {"date", std::to_string(std::time(nullptr))},
              {"viewed", 0},
              {"name", s.info.name},
              {"username", s.info.author},
              {"description", s.info.description},
              {"likes", 0},
              {"published", 0},
              {"hasliked", 0},
              {"tags", json::array()}}},
            {"renderpass", passes}};
}

}  // namespace sp
