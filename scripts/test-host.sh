#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
mkdir -p build-host
${CC:-cc} -O1 -std=c11 -Ithird_party -c source/decoder.c -o build-host/decoder.o
${CXX:-c++} -O1 -std=c++17 -Iinclude -Ithird_party source/core.cpp tests/core_test.cpp build-host/decoder.o -lm -lpthread -ldl -o build-host/core-test
./build-host/core-test

${CXX:-c++} -O1 -std=c++17 -Iinclude -Ithird_party source/core.cpp source/remote.cpp source/discovery.cpp source/websocket.cpp tests/remote_test.cpp -lpthread -o build-host/remote-test
./build-host/remote-test

${CXX:-c++} -O1 -std=c++17 -Iinclude -Ithird_party source/core.cpp source/remote.cpp source/discovery.cpp source/websocket.cpp source/client.cpp tests/client_test.cpp -lpthread -o build-host/client-test
./build-host/client-test

${CXX:-c++} -O1 -std=c++17 -Iinclude tests/socket_write_test.cpp -lpthread -o build-host/socket-write-test
./build-host/socket-write-test

${CXX:-c++} -O1 -std=c++17 -Iinclude tests/layout_test.cpp -o build-host/layout-test
./build-host/layout-test

${CXX:-c++} -O1 -std=c++17 -Iinclude -Ithird_party source/core.cpp source/remote.cpp source/discovery.cpp source/websocket.cpp tests/discovery_test.cpp -lpthread -o build-host/discovery-test
./build-host/discovery-test
