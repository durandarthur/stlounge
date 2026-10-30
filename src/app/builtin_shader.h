#pragma once
// Built-in test pattern shown at startup (original code, not from ShaderToy).

namespace sp {

inline const char* const kBuiltinShader = R"(// Built-in test pattern.
// Grid: one line every 100 shader pixels (changes with render scale).
// iMouse: white ring = iMouse.xy, dot = click origin |iMouse.zw|,
// green while the button is held (z > 0), red after release (z < 0).
// The screen flashes for the single frame where iMouse.w > 0.

float ring(vec2 p, vec2 c, float r) { return smoothstep(2.0, 0.0, abs(length(p - c) - r)); }
float disc(vec2 p, vec2 c, float r) { return smoothstep(r, r - 2.0, length(p - c)); }

void mainImage(out vec4 fragColor, in vec2 fragCoord)
{
    vec2 uv = fragCoord / iResolution.xy;
    vec2 p = (2.0 * fragCoord - iResolution.xy) / iResolution.y;
    float a = atan(p.y, p.x), r = length(p);
    vec3 col = 0.12 + 0.08 * cos(vec3(0.0, 2.1, 4.2) + iTime * 0.4 + 4.0 * r - a);
    col *= 0.6 + 0.4 * uv.y;

    vec2 g = mod(fragCoord, 100.0);
    col += 0.05 * step(min(g.x, g.y), 1.0);

    if (iMouse.x > 0.0 || iMouse.y > 0.0)
        col = mix(col, vec3(1.0), ring(fragCoord, iMouse.xy, 18.0));
    if (iMouse.z != 0.0) {
        vec3 c = iMouse.z > 0.0 ? vec3(0.2, 1.0, 0.3) : vec3(1.0, 0.25, 0.2);
        col = mix(col, c, disc(fragCoord, abs(iMouse.zw), 8.0));
    }
    if (iMouse.w > 0.0) col += 0.5;

    fragColor = vec4(col, 1.0);
}
)";

}  // namespace sp
