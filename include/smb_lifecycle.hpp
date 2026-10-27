#pragma once
#include <cstdint>
#include <mutex>
#include <smb2/smb2.h>
#include <smb2/libsmb2.h>
namespace nxh {
// libsmb2-6.2 links every context into one process-wide, unlocked list.
// Separate contexts allow parallel I/O, but creating/destroying them must not
// overlap: removal walks the list and can otherwise touch a freed context.
inline std::mutex &smbLifecycleMutex() {
    static std::mutex mutex;
    return mutex;
}
inline smb2_context *createSmbContext() {
    std::lock_guard<std::mutex> lock(smbLifecycleMutex());
    return smb2_init_context();
}
inline void destroySmbContext(smb2_context *context) {
    if (!context)
        return;
    std::lock_guard<std::mutex> lock(smbLifecycleMutex());
    smb2_destroy_context(context);
}
} // namespace nxh
