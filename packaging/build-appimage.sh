#!/usr/bin/env bash
# Builds <app>-<version>-x86_64.AppImage in build-appimage/.
#
#   packaging/build-appimage.sh [--clean] [--no-download]
#
#   --clean        rebuild from scratch (deletes build-appimage/build first)
#   --no-download  never download linuxdeploy/appimagetool; they must be installed
#
# Needs linuxdeploy and appimagetool. They are used from PATH if installed;
# otherwise they are downloaded from their GitHub releases into
# build-appimage/tools/. Installed tools are used as they are; downloaded ones are
# checked: packaging/appimage-tools.sha256 (sha256sum format) pins
# the downloaded files: a tool listed there must match its hash, or the build
# stops. LINUXDEPLOY_URL / APPIMAGETOOL_URL choose other release files (for
# example a fixed version instead of "continuous").
#
# The GPU driver (libEGL, libGLESv2, libGL, vendor and Vulkan drivers) is never
# bundled: the AppImage uses the host's NVIDIA or Mesa driver. glibc isn't bundled
# either, so the AppImage needs a glibc at least as new as the one it was linked
# against; the script prints that version at the end. Build on an older
# distribution for a lower minimum.
#
# Libraries loaded at run time with dlopen() aren't seen by linuxdeploy:
#   - SDL3, when SDL2 is sdl2-compat (Arch): bundled by this script.
#   - SDL's display and input backends (Wayland, X11, xkbcommon, libdecor, D-Bus)
#     and the EGL vendor drivers loaded by libglvnd: taken from the host on
#     purpose, as they must match the desktop and the GPU driver.
#
# Note: pipelines here never end in a reader that stops early (grep -q, awk exit,
# head): with pipefail, the writer's SIGPIPE would count as a failure.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="$ROOT/build-appimage"
TOOLS="$OUT/tools"
APPDIR="$OUT/AppDir"
PINS="$ROOT/packaging/appimage-tools.sha256"

die() { echo "error: $*" >&2; exit 1; }

DOWNLOAD=1
CLEAN=0
for arg in "$@"; do
    case "$arg" in
        --no-download) DOWNLOAD=0 ;;
        --clean) CLEAN=1 ;;
        -h|--help) sed -n '2,8p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) die "unknown option '$arg' (see --help)" ;;
    esac
done

[[ "$(uname -m)" == x86_64 ]] || die "this script builds x86_64 AppImages only (this machine: $(uname -m))"

# The app identity, from the three set() lines at the top of CMakeLists.txt.
read_cmake_var() {
    local value
    value="$(sed -n "s/^set($1 *\"\\([^\"]*\\)\")\$/\\1/p" "$ROOT/CMakeLists.txt")"
    [[ -n "$value" && "$value" != *$'\n'* ]] ||
        die "expected exactly one line 'set($1 \"...\")' in CMakeLists.txt"
    echo "$value"
}
APP_NAME="$(read_cmake_var APP_NAME)"
APP_VERSION="$(read_cmake_var APP_VERSION)"
[[ "$APP_NAME" =~ ^[a-z0-9][a-z0-9._-]*$ ]] || die "APP_NAME '$APP_NAME' isn't a valid file name"
[[ "$APP_VERSION" =~ ^[0-9][0-9A-Za-z.+-]*$ ]] || die "APP_VERSION '$APP_VERSION' isn't a version"

# AppImages normally need FUSE; this makes the tools run without it.
export APPIMAGE_EXTRACT_AND_RUN=1

# The pinned SHA-256 of a downloaded tool, or "" when it isn't pinned.
pinned_hash() {
    [[ -f "$PINS" ]] || return 0
    awk -v n="$1" '$2 == n || $2 == "*" n {print $1}' "$PINS"
}

verify_tool() {  # verify_tool <name> <file>
    local want got
    want="$(pinned_hash "$1")"
    got="$(sha256sum "$2" | awk '{print $1}')"
    if [[ -n "$want" ]]; then
        [[ "$got" == "$want" ]] || { rm -f "$2"; die "$1 doesn't match its pinned SHA-256 in $PINS (deleted it)"; }
    else
        echo "warning: $1 isn't pinned. To pin this file, add this line to $PINS:" >&2
        echo "  $got  $1" >&2
    fi
}

tool() {  # tool <name> <url>: path to the tool, downloading it if allowed
    local name="$1" url="$2"
    if command -v "$name" >/dev/null 2>&1; then command -v "$name"; return; fi
    if [[ ! -x "$TOOLS/$name" ]]; then
        [[ $DOWNLOAD == 1 ]] || die "$name not found (install it, or run without --no-download)"
        command -v curl >/dev/null 2>&1 || die "curl is needed to download $name"
        mkdir -p "$TOOLS"
        echo "== downloading $name from $url" >&2
        curl -fL --retry 3 --proto '=https' --tlsv1.2 -o "$TOOLS/$name.part" "$url"
        chmod +x "$TOOLS/$name.part"
        mv "$TOOLS/$name.part" "$TOOLS/$name"
    fi
    verify_tool "$name" "$TOOLS/$name"
    echo "$TOOLS/$name"
}
LINUXDEPLOY="$(tool linuxdeploy "${LINUXDEPLOY_URL:-https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage}")"
APPIMAGETOOL="$(tool appimagetool "${APPIMAGETOOL_URL:-https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-x86_64.AppImage}")"

echo "== building $APP_NAME $APP_VERSION (release)"
[[ $CLEAN == 1 ]] && rm -rf "$OUT/build"
cmake -S "$ROOT" -B "$OUT/build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DSP_BUILD_TESTS=OFF -DSP_USE_SOURCE_ASSETS=OFF -DCMAKE_INSTALL_PREFIX=/usr
cmake --build "$OUT/build"
rm -rf "$APPDIR"
DESTDIR="$APPDIR" cmake --install "$OUT/build"

BIN="$APPDIR/usr/bin/$APP_NAME"
DESKTOP="$APPDIR/usr/share/applications/$APP_NAME.desktop"
# appimagetool needs a PNG (or XPM) icon for .DirIcon; SVG isn't accepted.
ICON="$APPDIR/usr/share/icons/hicolor/256x256/apps/$APP_NAME.png"
for f in "$BIN" "$DESKTOP" "$ICON"; do
    [[ -f "$f" ]] || die "the install step didn't create $f (check the install() rules in CMakeLists.txt)"
done
[[ -n "$(find "$APPDIR/usr/share/$APP_NAME/textures" -type f 2>/dev/null)" ]] ||
    echo "warning: no bundled textures in the AppDir; the AppImage will use generated ones" >&2

LDD_OUT="$(ldd "$BIN" 2>&1)" || die "ldd failed on $BIN:"$'\n'"$LDD_OUT"
if [[ "$LDD_OUT" == *"not found"* ]]; then
    die "libraries missing on this machine:"$'\n'"$(grep 'not found' <<<"$LDD_OUT")"
fi

# SDL2 on Arch is sdl2-compat, which loads SDL3 at run time (dlopen), so linuxdeploy
# can't see it: bundle it by hand. Look next to SDL2 first, then in the ld cache.
EXTRA=()
SDL2_LIB="$(awk '/libSDL2/ && $3 ~ /^\// && !n++ {print $3}' <<<"$LDD_OUT")"
if [[ -n "$SDL2_LIB" ]] && grep -aq 'libSDL3\.so' "$SDL2_LIB"; then
    SDL3="$(dirname "$(readlink -f "$SDL2_LIB")")/libSDL3.so.0"
    if [[ ! -e "$SDL3" ]]; then
        SDL3="$(ldconfig -p 2>/dev/null | awk '/libSDL3\.so\.0 .*x86-64/ && !n++ {print $NF}')" || SDL3=""
    fi
    [[ -n "$SDL3" && -e "$SDL3" ]] || die "SDL2 here is sdl2-compat, but SDL3 (libSDL3.so.0) wasn't found to bundle"
    echo "== SDL2 is sdl2-compat: bundling $SDL3"
    EXTRA+=(--library "$SDL3")
fi

echo "== collecting libraries"
"$LINUXDEPLOY" --appdir "$APPDIR" \
    --executable "$BIN" \
    --desktop-file "$DESKTOP" \
    --icon-file "$ICON" \
    "${EXTRA[@]}" \
    --exclude-library 'libEGL*' --exclude-library 'libGLESv*' --exclude-library 'libGL.so*' \
    --exclude-library 'libGLX*' --exclude-library 'libGLdispatch*' --exclude-library 'libOpenGL*' \
    --exclude-library 'libgbm*' --exclude-library 'libdrm*' --exclude-library 'libnvidia*' \
    --exclude-library 'libvulkan*'

# A bundled GPU driver library would break the host's driver: check the whole AppDir.
BAD="$(find "$APPDIR" \( -type f -o -type l \) -regextype posix-extended \
    -regex '.*/(lib(EGL(_[^/]*)?|GLESv[12](_[^/]*)?|GL|GLX(_[^/]*)?|GLdispatch|OpenGL|nvidia-[^/]*|vulkan|gbm|drm(_[^/]*)?)\.so[^/]*|[^/]*_dri\.so)' \
    -print)"
[[ -z "$BAD" ]] || die "GPU driver libraries ended up in the AppDir:"$'\n'"$BAD"
if [[ ${#EXTRA[@]} -gt 0 ]]; then
    [[ -n "$(find "$APPDIR/usr/lib" -name 'libSDL3.so*' -print)" ]] || die "SDL3 was not bundled"
fi

# The AppImage's own icon: <name>.png at the top of the AppDir, from which appimagetool
# makes .DirIcon. Replace whatever linuxdeploy put there.
rm -f "$APPDIR/.DirIcon" "$APPDIR/$APP_NAME.svg" "$APPDIR/$APP_NAME.xpm"
cp "$ICON" "$APPDIR/$APP_NAME.png"

OUTPUT="$OUT/$APP_NAME-$APP_VERSION-x86_64.AppImage"
RUNTIME=()
[[ -n "${APPIMAGE_RUNTIME_FILE:-}" ]] && RUNTIME=(--runtime-file "$APPIMAGE_RUNTIME_FILE")
echo "== packing $OUTPUT"
rm -f "$OUTPUT"
ARCH=x86_64 "$APPIMAGETOOL" --no-appstream "${RUNTIME[@]}" "$APPDIR" "$OUTPUT"
[[ -s "$OUTPUT" ]] || die "appimagetool didn't produce $OUTPUT"

# The newest glibc symbol version anything in the AppImage needs.
if command -v objdump >/dev/null 2>&1; then
    NEED="$({ find "$APPDIR/usr" -type f \( -name '*.so*' -o -path '*/bin/*' \) -exec objdump -T {} + 2>/dev/null || true; } |
        grep -o 'GLIBC_[0-9][0-9.]*' | sort -uV | tail -n 1)" || NEED=""
    [[ -n "$NEED" ]] && echo "needs glibc >= ${NEED#GLIBC_} (this machine: $(getconf GNU_LIBC_VERSION 2>/dev/null || echo unknown))"
fi
echo "done: $OUTPUT ($(du -h "$OUTPUT" | cut -f1))"
echo "sha256: $(sha256sum "$OUTPUT" | cut -d' ' -f1)"
