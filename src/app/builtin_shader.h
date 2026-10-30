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

// Multipass demo (original): Buffer A keeps a fading trail in a float buffer
// (values go above 1.0), Image tone-maps it and adds grain from a noise texture.
inline const char* const kDemoBufferA = R"(// Buffer A. iChannel0 = Buffer A (itself, previous frame).
void mainImage(out vec4 fragColor, in vec2 fragCoord)
{
    vec2 uv = fragCoord / iResolution.xy;
    vec3 prev = texture(iChannel0, uv).rgb;
    vec2 dotPos = iResolution.xy * (0.5 + 0.35 * vec2(cos(iTime), sin(iTime * 1.3)));
    vec3 add = vec3(1.0, 0.6, 0.2) * smoothstep(12.0, 0.0, length(fragCoord - dotPos));
    if (iMouse.z > 0.0)
        add += vec3(0.2, 0.6, 1.0) * smoothstep(20.0, 0.0, length(fragCoord - iMouse.xy));
    fragColor = iFrame == 0 ? vec4(0.0) : vec4(prev * 0.97 + add, 1.0);
}
)";

inline const char* const kDemoImage = R"(// Image. iChannel0 = Buffer A, iChannel1 = RGBA Noise Medium.
void mainImage(out vec4 fragColor, in vec2 fragCoord)
{
    vec3 col = texture(iChannel0, fragCoord / iResolution.xy).rgb;
    col = col / (1.0 + col);  // the trail exceeds 1.0 in the float buffer
    float grain = texture(iChannel1, fragCoord / 256.0).r - 0.5;
    fragColor = vec4(col + 0.03 * grain, 1.0);
}
)";

// Audio demo (original): spectrum bars from row 0, waveform from row 1.
inline const char* const kDemoAudio = R"(// Audio demo. iChannel0 = Audio.
// Texture row 0 (y = 0.25) is the FFT, row 1 (y = 0.75) the waveform.
void mainImage(out vec4 fragColor, in vec2 fragCoord)
{
    vec2 uv = fragCoord / iResolution.xy;
    float fft  = texture(iChannel0, vec2(uv.x, 0.25)).x;
    float wave = texture(iChannel0, vec2(uv.x, 0.75)).x;

    vec3 col = vec3(0.04, 0.04, 0.06);
    col += vec3(0.2, 0.7, 1.0) * step(uv.y, fft) * (0.35 + 0.65 * uv.y);
    col += vec3(1.0, 0.55, 0.2) * smoothstep(3.0 / iResolution.y, 0.0, abs(uv.y - wave));
    fragColor = vec4(col, 1.0);
}
)";

}  // namespace sp
