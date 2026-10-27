#pragma once
#include <string>
#include <cstddef>
namespace nxh {
std::string websocketAccept(const std::string &key);
std::string websocketKey();
bool websocketWrite(int fd, unsigned opcode, const std::string &payload, bool masked = false);
bool websocketRead(int fd, unsigned &opcode, std::string &payload, bool expectMasked,
                   size_t maximum = 2 * 1024 * 1024);
} // namespace nxh
