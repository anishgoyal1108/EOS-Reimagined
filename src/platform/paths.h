#ifndef EOSR_PLATFORM_PATHS_H
#define EOSR_PLATFORM_PATHS_H

#include <string>

#include "common/types.h"

namespace eosr {
namespace platform {

// Where this instance keeps its profile. EOSR_DATA_DIR overrides it, which is how a launcher hands
// each local copy of a game its own profile -- and so its own identity -- without the copies having
// to agree on anything. Otherwise it is the user's per-application data directory. Empty when the OS
// gives us nowhere to write. The directory is not created here.
std::string user_data_directory();

// Create `path` and every missing parent. True when the directory exists afterwards.
bool make_directories(const std::string& path);

// Write `text` to `path` so only this user can read it. The permissions are set as the file is
// created, not afterwards, so the secret it holds is never briefly readable by anyone else.
bool write_private_file(const std::string& path, const std::string& text);

// An exclusive lock on a file, held for as long as this object lives. The OS drops it when the
// process exits -- a crash included -- so a stale lock can never strand a profile.
class file_lock {
public:
    file_lock();
    ~file_lock();

    file_lock(const file_lock&) = delete;
    file_lock& operator=(const file_lock&) = delete;

    // False when another process already holds `path`.
    bool acquire(const std::string& path);
    void release();
    bool held() const { return held_; }

private:
    // The OS handle as a plain integer -- a POSIX descriptor or a Win32 HANDLE -- so no OS header
    // has to reach this file.
    i64 handle_;
    bool held_;
};

} // namespace platform
} // namespace eosr

#endif
