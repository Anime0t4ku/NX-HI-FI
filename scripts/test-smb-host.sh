#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
build=build-host/smb-lifecycle
cmake -S third_party/libsmb2 -B "$build/library" \
    -DBUILD_SHARED_LIBS=OFF -DENABLE_EXAMPLES=OFF \
    -DENABLE_LIBKRB5=OFF -DENABLE_GSSAPI=OFF \
    -DCMAKE_C_FLAGS='-O1 -g -fsanitize=address -fno-omit-frame-pointer'
cmake --build "$build/library" --parallel "${JOBS:-4}"
${CXX:-c++} -O1 -g -std=c++17 -fsanitize=address -fno-omit-frame-pointer \
    -Iinclude -Ithird_party/libsmb2/include -I"$build/library" \
    tests/smb_lifecycle_test.cpp "$build/library/lib/libsmb2.a" \
    -lpthread -o "$build/test"
ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=1:halt_on_error=1}" "$build/test"
