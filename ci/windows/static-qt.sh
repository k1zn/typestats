#!/usr/bin/env bash
# The static qtbase of the single-file Windows exe (CLAUDE.md, "Один exe"): only what the program
# uses, optimized for size, the MinGW runtime linked in. ~30-40 min on 4 cores; CI keeps the result
# in its cache.
#   ci/windows/static-qt.sh <install prefix>      (Git Bash; MinGW 13.1, CMake, Ninja in PATH)
set -euo pipefail

prefix=$(cygpath -m "$1" 2>/dev/null || echo "$1")
version=${QT_VERSION:-6.8.3}
work=$(cygpath -m "${RUNNER_TEMP:-${TMPDIR:-/tmp}}" 2>/dev/null || echo "${RUNNER_TEMP:-/tmp}")/qtbase-static
archive=qtbase-everywhere-src-$version.tar.xz
path=official_releases/qt/${version%.*}/$version/submodules/$archive

mkdir -p "$work"
cd "$work"
if [ ! -f "$archive" ]; then
    curl -fL --retry 3 -o "$archive" "https://download.qt.io/$path" ||
        curl -fL --retry 3 -o "$archive" "https://mirrors.ocf.berkeley.edu/qt/$path"
fi
rm -rf "qtbase-everywhere-src-$version" build
tar xf "$archive"

off="opengl dynamicgl dbus sql network printsupport concurrent xml jpeg gif freetype textodfwriter
     textmarkdownreader textmarkdownwriter pdf vulkan colordialog fontdialog wizard mdiarea calendarwidget
     dockwidget undoview columnview fontcombobox"
# Qt's own copies of the libraries: a runner has others in PATH (Strawberry Perl's zlib, libpng).
system="doubleconversion freetype harfbuzz jpeg libb2 pcre2 png textmarkdownreader zlib"
features=()
for f in $off; do
    features+=("-DFEATURE_$f=OFF")
done
for f in $system; do
    features+=("-DFEATURE_system_$f=OFF")
done

cmake -S "qtbase-everywhere-src-$version" -B build -G Ninja -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++ \
    -DCMAKE_INSTALL_PREFIX="$prefix" -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF \
    -DFEATURE_optimize_size=ON -DFEATURE_static_runtime=ON -DQT_BUILD_EXAMPLES=OFF -DQT_BUILD_TESTS=OFF \
    "${features[@]}"
cmake --build build
cmake --install build
