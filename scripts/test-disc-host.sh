#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
mkdir -p build-host
${CXX:-c++} -O1 -std=c++17 -Wall -Wextra -Iinclude -Ithird_party source/disc_metadata.cpp tests/disc_test.cpp -lpthread -o build-host/disc-test
./build-host/disc-test
if command -v pkg-config >/dev/null 2>&1 && pkg-config --exists sdl2; then
    SDL2_CFLAGS=$(pkg-config --cflags sdl2)
    SDL2_LIBS=$(pkg-config --libs sdl2)
fi
if [ -z "${SDL2_LIBS:-}" ]; then
    printf '%s\n' 'Player test skipped: install SDL2 development headers and pkg-config, or set SDL2_CFLAGS and SDL2_LIBS.'
    exit 0
fi
${CC:-cc} -O1 -std=c11 -Ithird_party -c source/decoder.c -o build-host/disc-decoder.o
# Deliberate word splitting for compiler/linker flags supplied by pkg-config.
${CXX:-c++} -O1 -std=c++17 -Iinclude -Ithird_party ${SDL2_CFLAGS:-} source/core.cpp source/player.cpp tests/disc_player_test.cpp build-host/disc-decoder.o ${SDL2_LIBS} -Wl,--wrap=SDL_ClearQueuedAudio -lpthread -ldl -lm -o build-host/disc-player-test
./build-host/disc-player-test
