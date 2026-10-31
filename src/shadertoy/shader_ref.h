#pragma once
// Turns what a user pastes (a shadertoy.com link or a bare shader ID) into a shader ID.

#include <optional>
#include <string>

namespace sp {

// Accepts "XsX3Rr", "https://www.shadertoy.com/view/XsX3Rr", "shadertoy.com/embed/XsX3Rr",
// with or without a trailing slash, query or fragment. Surrounding spaces are ignored.
// Other hosts and other paths give nullopt.
std::optional<std::string> parseShaderRef(const std::string& input);

// True when `input` is a full shadertoy.com shader link (not a bare ID). The paste
// dialog only treats the clipboard as a link in that case, since any 6-letter word
// is a valid ID.
bool isShaderLink(const std::string& input);

}  // namespace sp
