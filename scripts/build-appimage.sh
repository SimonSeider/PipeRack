#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root"

build_dir="${BUILD_DIR:-build}"
appdir="packaging/appimage/AppDir"
out="${OUTPUT:-PipeRack.AppImage}"

appimagetool="${APPIMAGETOOL:-./appimagetool-x86_64.AppImage}"
if [[ ! -x "$appimagetool" ]]; then
    echo "appimagetool not found at $appimagetool" >&2
    echo "Set APPIMAGETOOL=/path/to/appimagetool-x86_64.AppImage" >&2
    exit 1
fi

echo "==> configuring"
cmake -S . -B "$build_dir" -DCMAKE_BUILD_TYPE=Release

echo "==> building"
cmake --build "$build_dir" -j"$(nproc)"

echo "==> staging AppDir"
rm -rf "$appdir/usr/bin" "$appdir/usr/lib" "$appdir/usr/plugins" "$appdir/usr/share"
cmake --install "$build_dir" --prefix "$appdir/usr"

install -Dm644 packaging/appimage/AppDir/piperack.desktop \
    "$appdir/usr/share/applications/piperack.desktop"

host_libs='^(ld-linux|libc|libm|libdl|libpthread|librt|libresolv|libgcc_s|libstdc\+\+|
libpipewire|libpulse|libasound|libjack|libspa|
libX11|libXext|libXrender|libXi|libXfixes|libXcursor|libXrandr|libXinerama|libXau|libXdmcp|libxcb|libxkbcommon|
libGL|libGLX|libGLdispatch|libEGL|libgbm|libdrm|libglapi|libwayland|
libglib-2|libgobject-2|libgio-2|libgmodule-2|libgthread-2|
libdbus-1|libsystemd|libudev|libselinux|libcap|libgpg-error|libgcrypt|
libfontconfig|libfreetype|libexpat|libz|libbz2|liblzma|libuuid|libffi)'
host_libs="${host_libs//$'\n'/}"

bundle_lib() {
    local src="$1"
    local base
    base="$(basename "$src")"
    [[ -e "$appdir/usr/lib/$base" ]] && return 0
    install -Dm644 "$src" "$appdir/usr/lib/$base"
}

bundle_deps() {
    local target="$1"
    local line lib
    while read -r line; do
        lib="$(awk '{ for (i = 1; i <= NF; ++i) if ($i == "=>") { print $(i + 1); exit } }' <<<"$line")"
        [[ -z "${lib:-}" || ! -e "$lib" ]] && continue
        [[ "$(basename "$lib")" =~ $host_libs ]] && continue
        bundle_lib "$lib"
    done < <(ldd "$target" 2>/dev/null | grep '=>')
}

echo "==> bundling Qt"
bundle_deps "$appdir/usr/bin/piperack"

qt_plugins="$(pkg-config --variable=libdir Qt6Core 2>/dev/null || echo /usr/lib)/qt6/plugins"
for group in platforms platformthemes xcbglintegrations wayland-shell-integration \
             wayland-decoration-client wayland-graphics-integration-client imageformats iconengines; do
    [[ -d "$qt_plugins/$group" ]] || continue
    mkdir -p "$appdir/usr/plugins/$group"
    for plugin in "$qt_plugins/$group"/*.so; do
        [[ -e "$plugin" ]] || continue
        install -Dm644 "$plugin" "$appdir/usr/plugins/$group/$(basename "$plugin")"
        bundle_deps "$plugin"
    done
done

for pass in 1 2 3; do
    for lib in "$appdir"/usr/lib/*.so*; do
        [[ -e "$lib" ]] || continue
        bundle_deps "$lib"
    done
done

echo "==> packaging"
rm -f "$out"
ARCH=x86_64 "$appimagetool" "$appdir" "$out"
echo "==> wrote $out"
