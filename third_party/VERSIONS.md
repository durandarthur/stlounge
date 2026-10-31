# Third-party code

## Vendored (in this folder)

| Library | Version | Source | License |
|---|---|---|---|
| Dear ImGui | v1.92.9b (core, SDL2/OpenGL3 backends, misc/cpp/imgui_stdlib) | https://github.com/ocornut/imgui | MIT (`imgui/LICENSE.txt`) |
| stb_image | v2.30 | https://github.com/nothings/stb | Public domain / MIT (in file) |
| stb_image_write | v1.16 | https://github.com/nothings/stb | Public domain / MIT (in file) |

Dear ImGui also provides the ProggyForever font (embedded in `imgui_draw.cpp`, used to draw
the fallback Font 1 texture) and `imstb_truetype.h` (stb_truetype, public domain / MIT).

Vendored as plain copies (not submodules) so builds work offline. To update: replace the files
and update this table.

## From the system

| Library | How it is used | License |
|---|---|---|
| nlohmann/json | header-only, compiled into the program | MIT |
| SDL2 (on Arch: sdl2-compat, which loads SDL3) | linked dynamically; bundled in the AppImage | zlib |
| libpulse | linked dynamically; bundled in the AppImage | LGPL-2.1-or-later |
| EGL / GLESv2 (libglvnd, Mesa or NVIDIA) | linked dynamically; never bundled | various |

The AppImage also carries the libraries those depend on (picked by linuxdeploy). LGPL
libraries stay separate shared files, so they can be replaced.
