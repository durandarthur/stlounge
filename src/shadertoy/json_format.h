#pragma once
// ShaderToy JSON <-> Shader.
//
// Reads the two shapes found in the wild:
//   - the website's own format (what the browser extension exports):
//       {"ver", "info", "renderpass": [{"type", "name", "code", "inputs": [{"type", "id", "filepath", ...}]}]}
//   - the official API format: {"Shader": {...}} with inputs using "ctype"/"src"
// and arrays of either. Writes the website format.

#include <nlohmann/json_fwd.hpp>

#include <string>
#include <vector>

#include "shadertoy/shader.h"

namespace sp {

struct ImportResult {
    bool ok = false;
    std::string error;                  // when !ok
    Shader shader;
    std::vector<std::string> warnings;  // skipped tabs, unknown textures, unsupported inputs
};

// Parses one shader object (already unwrapped from {"Shader": ...}).
ImportResult shaderFromJson(const nlohmann::json& obj);

// Parses text holding one shader or an array of shaders. Invalid JSON gives a
// single failed result with a readable error.
std::vector<ImportResult> parseShadertoyJson(const std::string& text);

// Unwraps {"Shader": {...}} and returns the shader objects in `text`
// (one or many). Empty if the text isn't shader JSON.
std::vector<nlohmann::json> shaderObjects(const std::string& text, std::string* error);

// Cheap check used for the clipboard: does this look like shader JSON?
bool looksLikeShaderJson(const std::string& text);

// Website-format JSON for a shader (used for pasted shaders).
nlohmann::json shaderToJson(const Shader& shader);

// True for ShaderToy's 6-character ids ("XsX3Rn"); false for "-1" or local ids.
bool isShadertoyId(const std::string& id);

}  // namespace sp
