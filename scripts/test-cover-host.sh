#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
mkdir -p build-host
flags="$(pkg-config --cflags sdl2 SDL2_image SDL2_ttf libjpeg libpng)"
libs="$(pkg-config --libs sdl2 SDL2_image SDL2_ttf libjpeg libpng)"
${CC:-cc} -O1 -g -std=c11 -fsanitize=address,undefined -fno-omit-frame-pointer -Iinclude -Ithird_party $flags -c source/cover_codec.c -o build-host/cover-codec-test.o
${CXX:-c++} -O1 -g -std=c++17 -fsanitize=address,undefined -fno-omit-frame-pointer -Iinclude -Ithird_party $flags tests/cover_codec_test.cpp build-host/cover-codec-test.o $libs -lpthread -o build-host/cover-codec-test
ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=1:halt_on_error=1}" ./build-host/cover-codec-test
