#!/usr/bin/env bash
# Builds <app>-<version>-x86_64.AppImage in build-appimage/.
#
# Needs linuxdeploy and appimagetool. They are used from PATH if installed;
# otherwise they are downloaded once from their official GitHub releases into
# build-appimage/tools/ (pass --no-download to forbid that).
#
# The GPU driver (libEGL, libGLESv2, libGL...) is never bundled: the AppImage
# uses the host's NVIDIA or Mesa driver. glibc isn't bundled either, so the
# AppImage runs on systems with a glibc at least as new as the build machine's.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="$ROOT/build-appimage"
TOOLS="$OUT/tools"
APPDIR="$OUT/AppDir"
DOWNLOAD=1
[[ "${1:-}" == "--no-download" ]] && DOWNLOAD=0

# Read the app identity from CMakeLists.txt, so a rename needs no change here.
APP_NAME="$(sed -n 's/^set(APP_NAME *"\(.*\)")/\1/p' "$ROOT/CMakeLists.txt")"
APP_VERSION="$(sed -n 's/^set(APP_VERSION *"\(.*\)")/\1/p' "$ROOT/CMakeLists.txt")"
[[ -n "$APP_NAME" && -n "$APP_VERSION" ]] || { echo "can't read APP_NAME/APP_VERSION from CMakeLists.txt" >&2; exit 1; }

# AppImages normally need FUSE; this makes the tools run without it.
export APPIMAGE_EXTRACT_AND_RUN=1

tool() {  # tool <name> <url>: path to the tool, downloading it if allowed
    local name="$1" url="$2"
    if command -v "$name" >/dev/null 2>&1; then command -v "$name"; return; fi
    if [[ -x "$TOOLS/$name" ]]; then echo "$TOOLS/$name"; return; fi
    if [[ $DOWNLOAD == 0 ]]; then
        echo "$name not found (install it, or run without --no-download)" >&2; exit 1
    fi
    mkdir -p "$TOOLS"
    echo "downloading $name from $url" >&2
    curl -fL --retry 3 -o "$TOOLS/$name.part" "$url"
    chmod +x "$TOOLS/$name.part"
    mv "$TOOLS/$name.part" "$TOOLS/$name"
    echo "$TOOLS/$name"
}
LINUXDEPLOY="$(tool linuxdeploy https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage)"
APPIMAGETOOL="$(tool appimagetool https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-x86_64.AppImage)"

echo "== building $APP_NAME $APP_VERSION (release)"
cmake -S "$ROOT" -B "$OUT/build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DSP_BUILD_TESTS=OFF -DCMAKE_INSTALL_PREFIX=/usr
cmake --build "$OUT/build"
rm -rf "$APPDIR"
DESTDIR="$APPDIR" cmake --install "$OUT/build"

# SDL2 on Arch is sdl2-compat, which loads SDL3 at run time (dlopen), so linuxdeploy
# can't see it: add it by hand when present.
EXTRA=()
SDL3="$(ldconfig -p 2>/dev/null | awk '/libSDL3\.so\.0 .*x86-64/ {print $NF; exit}')"
if ldd "$APPDIR/usr/bin/$APP_NAME" | grep -q 'libSDL2' && [[ -n "$SDL3" ]] &&
   strings "$(ldd "$APPDIR/usr/bin/$APP_NAME" | awk '/libSDL2/ {print $3}')" | grep -q 'libSDL3'; then
    echo "== SDL2 is sdl2-compat: bundling $SDL3"
    EXTRA+=(--library "$SDL3")
fi

echo "== collecting libraries"
"$LINUXDEPLOY" --appdir "$APPDIR" \
    --executable "$APPDIR/usr/bin/$APP_NAME" \
    --desktop-file "$APPDIR/usr/share/applications/$APP_NAME.desktop" \
    --icon-file "$APPDIR/usr/share/icons/hicolor/scalable/apps/$APP_NAME.svg" \
    "${EXTRA[@]}" \
    --exclude-library 'libEGL.so*' --exclude-library 'libGLESv2.so*' --exclude-library 'libGL.so*' \
    --exclude-library 'libGLX*' --exclude-library 'libGLdispatch*' --exclude-library 'libOpenGL*' \
    --exclude-library 'libgbm*' --exclude-library 'libdrm*' --exclude-library 'libnvidia*'

# Belt and braces: a bundled GPU driver library would break NVIDIA (or any) host drivers.
if ls "$APPDIR/usr/lib" | grep -E '^lib(EGL|GLESv2|GL|GLX|GLdispatch|nvidia)'; then
    echo "a GPU driver library ended up in the AppImage; refusing to continue" >&2; exit 1
fi

OUTPUT="$OUT/$APP_NAME-$APP_VERSION-x86_64.AppImage"
echo "== packing $OUTPUT"
ARCH=x86_64 "$APPIMAGETOOL" --no-appstream "$APPDIR" "$OUTPUT"
echo "done: $OUTPUT ($(du -h "$OUTPUT" | cut -f1))"
