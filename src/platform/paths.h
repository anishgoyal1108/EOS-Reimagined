#ifndef EOSR_PLATFORM_PATHS_H
#define EOSR_PLATFORM_PATHS_H

#include <cstddef>
#include <string>

#include "common/types.h"

namespace eosr {
namespace platform {

// The outcome of a bounded file read.
enum class file_read {
    ok,          // the read succeeded and `out` holds the bytes
    missing,     // the file does not exist
    unreadable,  // it exists but could not be opened or read
    too_large    // it is larger than the cap
};

// Read `path` into `out`, reading at most `max_bytes` + 1 bytes so a file larger than `max_bytes` is
// detected as `too_large` (and leaves `out` empty) without slurping the whole thing. Missing is
// distinguished from unreadable, so a caller can treat an absent optional file as normal and an
// absent required one as an error.
file_read read_file_capped(const std::string& path, std::size_t max_bytes, std::string& out);

// Whether `path` is absolute under THIS platform's rules -- so a relative config or trace path can be
// resolved against a base directory rather than the process cwd. On POSIX that is a leading '/', and
// a backslash is an ordinary filename byte; on Windows it is a leading '/' or '\', or a drive letter
// with a separator after the colon (C:\ or C:/). Pure string analysis, no I/O.
bool path_is_absolute(const std::string& path);

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
