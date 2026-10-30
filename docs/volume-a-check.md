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

## Step 2: print the raw values

The colours on shadertoy.com came out top-left blue, everything else red (2026-10-30), which no
ordering explains. This replacement **Image** tab prints the values instead. Keep Volume A,
Buffer A and the channels exactly as above (Image: iChannel0 = Buffer A, iChannel1 = Volume A),
paste this over the Image code, then pause and read the rows from the top:

| Row | Value | This app shows |
|---|---|---|
| 1 | `iFrame` in Image | N |
| 2 | Buffer A's output: its `iFrame` minus the volume value it read | 0 |
| 3 | voxel (0,0,0) as Image reads it | N |
| 4 | voxel (1,0,0) as Image reads it | N − 1 |
| 5 | voxel (2,0,0) as Image reads it | N |
| 6–8 | Volume A size, `iChannelResolution[1]` x, y, z | 128, 128, 128 |
| 9–10 | Buffer A size, `iChannelResolution[0]` x, y | the canvas size |

```glsl
// Prints values with a tiny 3x5 pixel font, one number per row.
float glyph(int g, vec2 q)
{
    // 0-9, '-', '.'; 3 bits per row, top row first.
    const int font[12] = int[12](31599, 9362, 29671, 29391, 23497, 31183, 31215, 29257, 31727, 31695, 448, 2);
    int bit = int(q.y) * 3 + (2 - int(q.x));
    return float((font[g] >> bit) & 1);
}

float printValue(vec2 p, float v)
{
    if (p.x < 0.0 || p.y < 0.0 || p.y >= 5.0) return 0.0;
    int cell = int(p.x / 4.0);
    vec2 q = vec2(mod(p.x, 4.0), p.y);
    if (q.x >= 3.0) return 0.0;
    float a = abs(v);
    int ip = int(a);
    int nd = 1;
    for (int t = ip; t >= 10; t /= 10) nd++;
    int lead = v < 0.0 ? 1 : 0;
    int g = -1;
    if (cell < lead) g = 10;
    else if (cell < lead + nd) {
        int t = ip;
        for (int i = 0; i < nd - 1 - (cell - lead); i++) t /= 10;
        g = t % 10;
    }
    else if (cell == lead + nd) g = 11;
    else if (cell == lead + nd + 1) g = int(fract(a) * 10.0);
    return g < 0 ? 0.0 : glyph(g, q);
}

void mainImage(out vec4 fragColor, in vec2 fragCoord)
{
    float s = max(1.0, floor(iResolution.y / 90.0));  // font pixel size
    vec2 p = fragCoord / s;
    float top = floor(iResolution.y / s) - 2.0;
    float r = floor((top - p.y) / 8.0);                 // row, from the top
    vec2 q = vec2(p.x - 2.0, p.y - (top - 8.0 * r - 6.0));

    float v = 0.0;
    if      (r == 0.0) v = float(iFrame);
    else if (r == 1.0) v = texelFetch(iChannel0, ivec2(0), 0).x;
    else if (r == 2.0) v = texelFetch(iChannel1, ivec3(0, 0, 0), 0).x;
    else if (r == 3.0) v = texelFetch(iChannel1, ivec3(1, 0, 0), 0).x;
    else if (r == 4.0) v = texelFetch(iChannel1, ivec3(2, 0, 0), 0).x;
    else if (r == 5.0) v = iChannelResolution[1].x;
    else if (r == 6.0) v = iChannelResolution[1].y;
    else if (r == 7.0) v = iChannelResolution[1].z;
    else if (r == 8.0) v = iChannelResolution[0].x;
    else if (r == 9.0) v = iChannelResolution[0].y;
    float ink = r < 10.0 ? printValue(q, v) : 0.0;
    fragColor = vec4(mix(vec3(0.05), vec3(1.0), ink), 1.0);
}
```
