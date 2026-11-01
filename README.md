# stlounge

A lightweight Linux player for [ShaderToy](https://www.shadertoy.com) shaders, meant to be left
running as eye candy: playlists, shuffle, crossfades, and shaders that react to whatever your
desktop is playing.

- Runs ShaderToy shaders natively on OpenGL ES 3 (Wayland or X11): multipass Buffers A-D,
  Common, Volume A, textures, cubemaps, mouse, and audio input from your desktop sound.
- A shader library with playlists, favorites, shuffle, auto-advance, crossfade and lock.
- Safety nets: shaders that fail to compile, crash the driver or are too slow for your GPU are
  skipped instead of taking the desktop down.
- Small: a 3 MB program plus 8 MB of textures, no browser engine. Audio capture runs only
  while a shader uses it.

The full specification is in `SPEC.md`.

## Install

### AppImage

Download `stlounge-<version>-x86_64.AppImage`, make it executable and run it:

```sh
chmod +x stlounge-*-x86_64.AppImage
./stlounge-*-x86_64.AppImage
```

The AppImage uses your system's GPU driver (NVIDIA or Mesa), which must support OpenGL ES 3.0
through EGL. Every current desktop driver does. It also needs a glibc at least as new as the
one on the machine that built it.

### From source

Dependencies (Arch): `base-devel cmake ninja pkgconf sdl2 libpulse nlohmann-json mesa libglvnd`.
Dear ImGui and stb are included in `third_party/`.

```sh
cmake -S . -B build -G Ninja
cmake --build build
./build/stlounge                 # runs from the build folder; --help for options
sudo cmake --install build       # optional: installs to /usr/local
```

Tests (no display needed; the GPU tests use Mesa's software renderer and are skipped if no
OpenGL ES context can be created):

```sh
ctest --test-dir build --output-on-failure
```

### Building the AppImage

```sh
./packaging/build-appimage.sh            # --clean for a fresh build, --help for options
```

It needs `linuxdeploy` and `appimagetool`. Installed ones are used as they are. Otherwise the
script downloads them from their GitHub releases into `build-appimage/tools/`
(`--no-download` forbids that). To pin those downloads, put their SHA-256 in
`packaging/appimage-tools.sha256`: the script prints the lines to add, and refuses a file that
doesn't match. The result is `build-appimage/stlounge-<version>-x86_64.AppImage`.

- GPU driver libraries (EGL, GLES, GL, vendor and Vulkan drivers) are never bundled, and the
  script fails if one slips in.
- On Arch, SDL2 is sdl2-compat, which loads SDL3 at run time; the script bundles SDL3 too.
- The AppImage needs a glibc at least as new as the build machine's (the script prints the
  exact version). Built on an up-to-date Arch, that excludes most non-rolling distributions;
  for a wider audience, build on an older distribution (for example in an Ubuntu 22.04
  container).

## Using it

**Adding shaders.** Open a shader on shadertoy.com and export its JSON with a browser
extension (several "ShaderToy export" extensions exist). Then either:

- copy the JSON and press **Ctrl+V** in the app,
- use **Library > Import JSON file** (several files at once work), or
- drop the files onto the window.

You can also paste GLSL code into the Image tab, or write a shader yourself: the Add shader
dialog has a tab per pass and a channel picker for each `iChannel`. To change a shader later,
right-click it in the Library and pick **Edit...**.

(The app can't download shaders from a shadertoy.com link. The site answers programs with a
Cloudflare browser check, and getting past it would mean bundling a whole browser and evading
the site's bot protection.)

**Playing.** The Library window plays a playlist: *All shaders*, *Favorites* (click the `*`
next to a shader) or your own lists (right-click a shader > Add to playlist). Click any shader
to play it.

| Key | Action |
|---|---|
| Space | Pause / resume |
| N / Right, P / Left | Next / previous shader |
| R | Random shader |
| S | Shuffle on/off |
| L | Lock: no auto-advance or transitions |
| H / Tab, Esc | Hide / show the UI (Esc leaves fullscreen first) |
| F / F11 | Fullscreen |
| F5 | Reload the shader |
| O | Settings |
| Ctrl+V | Add a shader |
| F12 | Screenshot to `~/Pictures/stlounge/` (render resolution, no UI) |

With the UI hidden, the shader fills the window with nothing on top. Pause, lock, shuffle and
screenshots show a short message that fades away.

**Settings (O):**
- Display: FPS cap, render scale.
- Playlist: auto-advance interval (a shader's own play time overrides it: right-click > Play
  time), shuffle, lock, crossfade (off by default, since it renders two shaders during the fade).
- Audio: which output to listen to.
- Textures: your own images instead of the built-in textures.
- Advanced: a compile-time cap, and the frame-time limit.

**Audio.** Shaders with a music, microphone or SoundCloud input react to your desktop audio:
the app listens to the monitor of your default output (PulseAudio or PipeWire), or to the
output chosen in Settings. Capture runs only while the current shader uses it.

**Command line:** `--verbose` (debug messages in the terminal), `--safe-mode` (start with the
test pattern instead of the last shader), `--version`, `--help`. `SDL_VIDEODRIVER=x11` or
`=wayland` forces a display backend.

## Differences from shadertoy.com

ShaderToy runs shaders through WebGL 2 in a browser. This app runs them on your GPU driver
directly, so a few shaders behave differently:

- **Uninitialised variables.** WebGL sets every variable to zero; native drivers don't. A
  shader that reads a variable before writing it may show noise or flicker here.
- **Driver differences.** Each driver has its own compiler, precision and handling of NaN,
  infinity, division by zero, `pow` of negative numbers, loops and texture lookups at edges.
  Shaders that rely on undefined behaviour can look different, or fail to compile, on one
  driver but not another. The compile log in the app points at the right tab and line.
- **Unsupported inputs:** keyboard, webcam, video, Cubemap A buffers and sound shaders
  (`mainSound`). The app names them when a shader uses one, and the shader still runs, with a
  black texture in their place. A Sound tab is kept in the file but not played.
- **Audio** comes from your desktop rather than the shader's own track, so a shader reacts to
  whatever is playing. The audio texture is ShaderToy's 512x2 layout (spectrum, then
  waveform), computed like the browser's analyser.
- **Volume A** (ShaderToy's undocumented volume tab) was checked against the site: it runs
  before the buffers, every frame, and reads its own previous frame. One site quirk isn't
  copied: on the site, rewind clears the buffers but not the volume.
- `HW_PERFORMANCE` is 1 and `iFrameRate` is the measured rate. `iChannelTime` follows `iTime`.

## Safety nets

- **Compile errors.** The log is shown with ShaderToy's tab names and line numbers. The previous
  shader keeps running, and playlists skip the broken one.
- **Crash guard.** While a shader starts (its first second), a marker file names it. If the app
  or the desktop dies then, that shader is flagged on the next start: it's skipped and shown in
  red in the Library. Click it to try it again.
- **Frame-time limit.** A shader that takes more than 1 s per frame on your GPU can freeze the
  whole desktop, so it is skipped (Settings > Advanced; 0.2 to 10 s, or off). Normal frames
  aren't measured: only after a slow frame does the app wait for the GPU to time the next one.
- **Compile-time cap** (optional, Settings > Advanced): gives up on shaders that take too long
  to compile. It can only interrupt a compile when the driver compiles in the background.
- **Broken files** (settings, playlists, library entries) produce a warning, not a crash. An
  unreadable settings file is kept as `settings.json.bad` before anything replaces it.

## Troubleshooting

- **A shader crashes or freezes the app at start:** run with `--safe-mode`, then fix or delete
  the shader in the Library. The crash guard normally does this for you.
- **Black screen or no window:** run with `--verbose` and look at the first lines (video
  driver, GL renderer and version). Try `SDL_VIDEODRIVER=x11` or `=wayland`.
- **No audio reaction:** check Settings > Audio, and that the shader has an audio input. The
  info panel's Audio section shows the capture state.
- The log is in `~/.local/state/stlounge/stlounge.log` (the previous run's in `.old`).

## Files

| What | Where |
|---|---|
| Settings (`settings.json`) | `~/.config/stlounge/` |
| Library (`shaders/`), playlists (`playlists/`), your textures (`textures/`) | `~/.local/share/stlounge/` |
| Log, last shader (`session.json`), crash marker, UI layout | `~/.local/state/stlounge/` |
| Screenshots | `~/Pictures/stlounge/` (your XDG pictures folder) |

Each library shader is one file in ShaderToy's JSON format, with the app's own data (favorite,
play time) in a separate `"stlounge"` key, so the files stay usable elsewhere. (Files saved
before the rename used a `"shaderplayer"` key; it is still read and moves on the next save.)

## Development

- `src/` layout: `app/` (main loop, UI panels, guards), `shadertoy/` (wrapper, JSON, renderer),
  `gfx/` (GL helpers), `textures/`, `audio/`, `library/`, `config/`,
  `platform/`, `ui/`.
- `docs/tests/volume-a-check.md` holds the shaders used to check Volume input against the site.
- **App icon:** `assets/icons/icon.svg`. After editing it, run `packaging/render-icons.sh`
  (needs `rsvg-convert`) to re-render the PNG sizes, which are committed.
- Developer aids: `SP_DUMP_TEXTURES=<dir>` (unit tests) saves every generated texture, and
  `SP_DUMP_SAMPLES=<dir>` (GL tests) saves pictures of the shaders in `tests/data/private/`.
- A sanitizer build: `cmake -B build-asan -DCMAKE_BUILD_TYPE=Debug
  -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined"`, then run the tests with
  `ASAN_OPTIONS=detect_leaks=0`.

## License

- **Code:** MIT, see `LICENSE`.
- **Third-party code:** see `third_party/VERSIONS.md`. Dear ImGui and stb are MIT / public
  domain. The AppImage also bundles SDL (zlib) and libpulse (LGPL-2.1+) as separate shared
  libraries.
- **Shaders** belong to their authors. ShaderToy's default license is CC BY-NC-SA 3.0 unless
  the author says otherwise. The app shows each shader's author and link, and ships no shaders.
