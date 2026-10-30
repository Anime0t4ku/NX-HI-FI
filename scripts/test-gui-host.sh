#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
if [[ -z ${HOST_EXTRA_CFLAGS:-} ]]; then
    export HOST_EXTRA_CFLAGS="$(pkg-config --cflags sdl2 SDL2_image SDL2_ttf libpng libjpeg)"
fi
if [[ -z ${HOST_EXTRA_LIBS:-} ]]; then
    export HOST_EXTRA_LIBS="$(pkg-config --libs sdl2 SDL2_image SDL2_ttf libpng libjpeg) -lcurl"
fi
make -f Makefile.host test-player -j"${JOBS:-4}"
read -r -a flags <<< "$HOST_EXTRA_CFLAGS"
read -r -a libs <<< "$HOST_EXTRA_LIBS"
"${CXX:-c++}" -O1 -std=c++17 -Iinclude -Ithird_party "${flags[@]}" tests/badge_layout_test.cpp source/core.cpp "${libs[@]}" -o build-host/badge-layout-test
./build-host/badge-layout-test
