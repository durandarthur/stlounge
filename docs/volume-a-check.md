# Volume A behaviour check

ShaderToy's Volume A tab (`mainVolume`) is undocumented. This app makes three choices about it:

1. **Order:** Volume A runs before Buffers A–D each frame, so buffers see this frame's volume.
2. **Self-reading:** a Volume A that reads itself gets its previous frame, like a buffer does.
3. **Static volumes render once:** a volume that uses no time, frame, mouse, date or changing
   inputs (buffers, audio, itself) is drawn once instead of every frame.

Choice 3 can't look different from the site: such a volume produces the same voxels every frame.
Choices 1 and 2 *could* differ. The shader below tests both:

- **On shadertoy.com:** make a new shader, add a Volume A tab and Buffer A, paste each tab's code
  and set the channels as noted.
- **In this app:** import `docs/volume-a-check.json` (Library → Import JSON file, or drag it onto
  the window). It should be all green.

## Reading the result

The screen is split into three areas. Each one is **green** if the app's choice matches, **red**
if not:

| Area | Green means | Red means |
|---|---|---|
| Top left | Buffer A sees this frame's volume (Volume A runs before buffers) | Volume A runs after buffers (buffers see it one frame late) |
| Top right | Image sees this frame's volume | Volume A runs after Image (unlikely) |
| Bottom | Volume A reading itself gets its previous frame | Self-reading isn't supported, or works differently |

Blue anywhere means something unexpected; please note what you see.

If the site shows red top-left, the fix is a one-line change in `src/shadertoy/renderer.cpp`
(move the volume pass after the buffers). If the bottom is red, Volume A self-reads behave
differently and need a closer look.

## The shader

**Volume A** (iChannel0 = Volume A):

```glsl
// Voxel (1,0,0) copies voxel (2,0,0) from the volume's previous frame,
// i.e. the previous iFrame. Every other voxel stores the current iFrame.
void mainVolume(out vec4 fragColor, in vec3 fragCoord)
{
    ivec3 q = ivec3(fragCoord);
    if (q == ivec3(1, 0, 0))
        fragColor = vec4(texelFetch(iChannel0, ivec3(2, 0, 0), 0).x);
    else
        fragColor = vec4(float(iFrame));
}
```

**Buffer A** (iChannel0 = Volume A):

```glsl
// How many frames behind is the volume, as seen from Buffer A?
void mainImage(out vec4 fragColor, in vec2 fragCoord)
{
    fragColor = vec4(float(iFrame) - texelFetch(iChannel0, ivec3(0), 0).x);
}
```

**Image** (iChannel0 = Buffer A, iChannel1 = Volume A):

```glsl
void mainImage(out vec4 fragColor, in vec2 fragCoord)
{
    vec2 uv = fragCoord / iResolution.xy;
    float d;
    if (uv.y > 0.5)
        d = uv.x < 0.5 ? texelFetch(iChannel0, ivec2(0), 0).x                     // Buffer A's lag
                       : float(iFrame) - texelFetch(iChannel1, ivec3(0), 0).x;    // Image's lag
    else
        d = float(iFrame - 1) - texelFetch(iChannel1, ivec3(1, 0, 0), 0).x;       // self-read: previous iFrame
    vec3 col = d > 0.5 ? vec3(1.0, 0.2, 0.2) : d < -0.5 ? vec3(0.3, 0.3, 1.0) : vec3(0.2, 1.0, 0.3);
    fragColor = vec4(col, 1.0);
}
```

The bottom area is blue for the very first frame only (there is no previous frame yet).
The check doesn't depend on when the shader started, so pausing or rewinding is fine.
