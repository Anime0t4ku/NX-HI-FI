#pragma once
#include "core.hpp"
#include <atomic>
struct smb2_context;
namespace nxh {
class SmbSession {
    ::smb2_context *ctx_ = nullptr;

  public:
    explicit SmbSession(const Share &share);
    ~SmbSession();
    bool valid() const;
    std::string error() const;
    ::smb2_context *get() const { return ctx_; }
    std::string failure;
};
std::unique_ptr<Input> openInput(const Source &, const std::string &, std::string &,
                                 const std::atomic<uint64_t> *generation = nullptr,
                                 uint64_t expected = 0);
bool listDirectory(const Source &, const std::string &, std::vector<Entry> &, std::string &);
std::vector<unsigned char> loadCover(const Source &, const std::string &track,
                                     bool preferFolder = false);
} // namespace nxh
