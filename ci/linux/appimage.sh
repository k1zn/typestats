#!/usr/bin/env bash
# The AppImage of the port (re/crossplatform.md, step 7): `cmake --install` into an AppDir, then
# linuxdeploy with its Qt plugin. Downloads linuxdeploy (network).
#   ci/linux/appimage.sh <build dir>      (configured with -DCMAKE_INSTALL_PREFIX=/usr)
# The result: <build dir>/TypingStatistics-x86_64.AppImage.
set -euo pipefail

build=$(realpath "$1")
appdir="$build/AppDir"
tools="$build/appimage-tools"
app_id=org.typingstatistics.TypingStatistics

mkdir -p "$tools"
for tool in linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage \
            linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-x86_64.AppImage; do
    file="$tools/$(basename "$tool")"
    [ -f "$file" ] || curl -fsSL -o "$file" "https://github.com/linuxdeploy/$tool"
    chmod +x "$file"
done

rm -rf "$appdir"
DESTDIR="$appdir" cmake --install "$build"
# The udev rule needs root and stays outside: without it the program shows the commands that install it.
rm -rf "$appdir/usr/lib/udev"

export PATH="$tools:$PATH"
export APPIMAGE_EXTRACT_AND_RUN=1 # no FUSE in containers
export QMAKE="${QMAKE:-$(command -v qmake6 || command -v qmake)}"
export LDAI_OUTPUT="$build/TypingStatistics-x86_64.AppImage" OUTPUT="$build/TypingStatistics-x86_64.AppImage"
cd "$build"
linuxdeploy-x86_64.AppImage --appdir "$appdir" --plugin qt --output appimage \
    --desktop-file "$appdir/usr/share/applications/$app_id.desktop"
ls -la "$LDAI_OUTPUT"
