#!/bin/sh
set -eu
cd "$(dirname "$0")"
: "${DEVKITPRO:=/opt/devkitpro}"
: "${DEVKITA64:=$DEVKITPRO/devkitA64}"
export DEVKITPRO DEVKITA64
PATH="$DEVKITA64/bin:$DEVKITPRO/tools/bin:$DEVKITPRO/portlibs/switch/bin:$PATH"
export PATH
for tool in make cmake aarch64-none-elf-g++ nacptool elf2nro; do
    command -v "$tool" >/dev/null 2>&1 || { echo "Missing build tool: $tool" >&2; exit 1; }
done
[ -f "$DEVKITPRO/cmake/Switch.cmake" ] || { echo 'Install switch-cmake with devkitPro pacman.' >&2; exit 1; }
[ -f "$DEVKITPRO/portlibs/switch/include/SDL2/SDL_ttf.h" ] || { echo 'Install switch-sdl2, switch-sdl2_ttf and switch-sdl2_image.' >&2; exit 1; }
[ -f "$DEVKITPRO/portlibs/switch/include/curl/curl.h" ] || { echo 'Install switch-curl.' >&2; exit 1; }
[ -f "$DEVKITPRO/portlibs/switch/include/mbedtls/md5.h" ] || { echo 'Install switch-mbedtls.' >&2; exit 1; }
cmake -S third_party/libsmb2 -B build-deps/smb2 \
    -DCMAKE_TOOLCHAIN_FILE="$DEVKITPRO/cmake/Switch.cmake" \
    -DCMAKE_INSTALL_PREFIX="$PWD/build-deps/install" \
    -DBUILD_SHARED_LIBS=OFF -DENABLE_EXAMPLES=OFF -DENABLE_LIBKRB5=OFF -DENABLE_GSSAPI=OFF
cmake --build build-deps/smb2 --parallel "${JOBS:-4}"
cmake --install build-deps/smb2
make -C third_party/libusbdvd libs -j"${JOBS:-4}"
make -j"${JOBS:-4}"
mkdir -p sd-card/switch/nx-hifi
cp nx-hifi.nro sd-card/switch/nx-hifi/nx-hifi.nro
cp config.example.json sd-card/switch/nx-hifi/config.example.json
printf '%s\n' 'Built NX Hi-Fi. Copy sd-card/ contents to the root of your Switch SD card.'
