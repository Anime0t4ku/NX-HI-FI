#include "playback_diagnostics.hpp"
#include "storage.hpp"
#include "cover_image.hpp"
#include "smb_lifecycle.hpp"
#include "artwork.hpp"
#include <SDL_image.h>
#include <dirent.h>
#include <fcntl.h>
#include <smb2/smb2.h>
#include <smb2/libsmb2.h>
#include <sys/stat.h>
namespace nxh {
SmbSession::SmbSession(const Share &s) {
    playbackTrace("smb.context.create.begin");
    ctx_ = createSmbContext();
    playbackTrace("smb.context.create.end", bool(ctx_));
    if (!ctx_) {
        failure = "Could not allocate SMB session.";
        return;
    }
    smb2_set_timeout(ctx_, 5);
    smb2_set_security_mode(ctx_, SMB2_NEGOTIATE_SIGNING_ENABLED);
    smb2_set_user(ctx_, s.guest ? "guest" : s.user.c_str());
    smb2_set_password(ctx_, s.guest ? "" : s.password.c_str());
    if (!s.domain.empty())
        smb2_set_domain(ctx_, s.domain.c_str());
    playbackTrace("smb.connect.begin");
    if (smb2_connect_share(ctx_, s.server.c_str(), s.share.c_str(),
                           s.guest ? "guest" : s.user.c_str()) < 0) {
        failure = "SMB connection failed. Check Wi-Fi, host, share and credentials.";
        destroySmbContext(ctx_);
        ctx_ = nullptr;
    }
    playbackTrace("smb.connect.end", bool(ctx_));
}
SmbSession::~SmbSession() {
    if (ctx_) {
        playbackTrace("smb.disconnect.begin");
        smb2_disconnect_share(ctx_);
        destroySmbContext(ctx_);
        playbackTrace("smb.disconnect.end");
    }
}
bool SmbSession::valid() const { return ctx_ != nullptr; }
std::string SmbSession::error() const { return failure; }
class SmbInput final : public Input {
    std::unique_ptr<SmbSession> session_;
    smb2fh *file_ = nullptr;
    uint64_t size_ = 0, pos_ = 0;
    const std::atomic<uint64_t> *generation_;
    uint64_t expected_;
    std::string failure_;
    bool firstRead_ = true;

  public:
    SmbInput(const Source &s, const std::string &p, std::string &error,
             const std::atomic<uint64_t> *g, uint64_t e)
        : session_(new SmbSession(s.share)), generation_(g), expected_(e) {
        if (!session_->valid()) {
            error = session_->error();
            return;
        }
        playbackTrace("smb.file.open.begin");
        file_ = smb2_open(session_->get(), join(s.root, p).c_str(), O_RDONLY);
        playbackTrace("smb.file.open.end", bool(file_));
        if (!file_) {
            error = "Cannot open SMB file.";
            return;
        }
        smb2_stat_64 stat{};
        if (smb2_fstat(session_->get(), file_, &stat) < 0) {
            error = "Cannot read SMB file size.";
            smb2_close(session_->get(), file_);
            file_ = nullptr;
            return;
        }
        size_ = stat.smb2_size;
    }
    ~SmbInput() {
        if (file_)
            smb2_close(session_->get(), file_);
    }
    bool valid() const { return file_ != nullptr; }
    size_t read(void *d, size_t n) override {
        if (!file_ || (generation_ && generation_->load() != expected_))
            return 0;
        n = std::min<uint64_t>(n, std::min<uint64_t>(size_ - pos_, 256 * 1024));
        if (firstRead_)
            playbackTrace("smb.file.read.begin", n);
        int result = smb2_pread(session_->get(), file_, static_cast<uint8_t *>(d), n, pos_);
        if (firstRead_) {
            playbackTrace("smb.file.read.end", result);
            firstRead_ = false;
        }
        if (result < 0) {
            failure_ = "SMB read failed. Check your network connection.";
            return 0;
        }
        pos_ += result;
        return result;
    }
    bool seek(int64_t n, int origin) override {
        int64_t next = origin == SEEK_SET   ? n
                       : origin == SEEK_CUR ? int64_t(pos_) + n
                                            : int64_t(size_) + n;
        if (next < 0 || uint64_t(next) > size_)
            return false;
        pos_ = next;
        return true;
    }
    std::string error() const override { return failure_; }
    uint64_t size() const override { return size_; }
    uint64_t position() const override { return pos_; }
};
std::unique_ptr<Input> openInput(const Source &s, const std::string &p, std::string &error,
                                 const std::atomic<uint64_t> *g, uint64_t e) {
    if (!safeRelative(p)) {
        error = "Invalid relative path.";
        return {};
    }
    if (s.smb) {
        auto input = std::make_unique<SmbInput>(s, p, error, g, e);
        if (!input->valid())
            return {};
        return std::make_unique<BufferedInput>(std::move(input));
    }
    auto input = std::make_unique<LocalInput>(join(s.root, p));
    if (!input->valid()) {
        error = "Cannot open SD file.";
        return {};
    }
    return input;
}
bool listDirectory(const Source &s, const std::string &p, std::vector<Entry> &out,
                   std::string &error) {
    out.clear();
    if (!safeRelative(p)) {
        error = "Invalid directory path.";
        return false;
    }
    if (s.smb) {
        SmbSession session(s.share);
        if (!session.valid()) {
            error = session.error();
            return false;
        }
        auto dir = smb2_opendir(session.get(), join(s.root, p).c_str());
        if (!dir) {
            error = "Cannot list SMB folder. Check the subfolder and read permissions.";
            return false;
        }
        while (auto ent = smb2_readdir(session.get(), dir)) {
            std::string name = ent->name;
            if (name.empty() || name[0] == '.' || !safeRelative(name))
                continue;
            bool folder = ent->st.smb2_type == SMB2_TYPE_DIRECTORY;
            if (folder || audioFile(name))
                out.push_back({name, join(p, name), folder});
        }
        smb2_closedir(session.get(), dir);
    } else {
        auto full = join(s.root, p);
        DIR *dir = opendir(full.c_str());
        if (!dir) {
            error = "Cannot open SD folder. Create the music folder or change it in Settings.";
            return false;
        }
        while (auto ent = readdir(dir)) {
            std::string name = ent->d_name;
            if (name.empty() || name[0] == '.')
                continue;
            struct stat st{};
            if (stat(join(full, name).c_str(), &st) != 0)
                continue;
            bool folder = S_ISDIR(st.st_mode);
            if (folder || audioFile(name))
                out.push_back({name, join(p, name), folder});
        }
        closedir(dir);
    }
    sortEntries(out);
    return true;
}
static bool validArt(const std::vector<unsigned char> &bytes) {
    playbackTrace("artwork.validate.begin", bytes.size());
    auto *surface = boundedCoverSurface(bytes);
    playbackTrace("artwork.validate.end", bool(surface));
    if (!surface)
        return false;
    SDL_FreeSurface(surface);
    return true;
}

static std::vector<unsigned char> folderCover(const Source &s, const std::string &track) {
    std::string ignored;
    const auto folder = parent(track);
    std::vector<std::string> files;
    if (s.smb) {
        SmbSession session(s.share);
        if (!session.valid())
            return {};
        auto *dir = smb2_opendir(session.get(), join(s.root, folder).c_str());
        if (!dir)
            return {};
        while (auto *e = smb2_readdir(session.get(), dir))
            if (e->st.smb2_type != SMB2_TYPE_DIRECTORY && safeRelative(e->name))
                files.emplace_back(e->name);
        smb2_closedir(session.get(), dir);
    } else {
        auto *dir = opendir(join(s.root, folder).c_str());
        if (!dir)
            return {};
        while (auto *e = readdir(dir))
            files.emplace_back(e->d_name);
        closedir(dir);
    }
    for (const char *name : {"cover.jpg", "cover.jpeg", "cover.png", "folder.jpg", "folder.jpeg",
                             "folder.png", "front.jpg", "front.jpeg", "front.png"}) {
        for (auto &actual : files) {
            if (lower(actual) != name)
                continue;
            auto file = openInput(s, join(folder, actual), ignored);
            if (!file || !file->size() || file->size() > MaxArtBytes)
                continue;
            std::vector<unsigned char> bytes(file->size());
            if (artRead(*file, bytes.data(), bytes.size()) && validArt(bytes))
                return bytes;
        }
    }
    return {};
}
std::vector<unsigned char> loadCover(const Source &s, const std::string &track, bool preferFolder) {
    if (preferFolder) {
        auto bytes = folderCover(s, track);
        if (!bytes.empty())
            return bytes;
    }
    std::string ignored;
    auto input = openInput(s, track, ignored);
    if (input) {
        auto bytes = embeddedCover(*input, validArt);
        if (!bytes.empty())
            return bytes;
    }
    return folderCover(s, track);
}
} // namespace nxh
