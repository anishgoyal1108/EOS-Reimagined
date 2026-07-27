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

// Whether a bootstrap path is independent of process drive/current-directory state. POSIX uses the
// same leading-slash rule as path_is_absolute. Windows requires a drive-absolute or UNC path; a
// single leading slash/backslash is only rooted on the current drive and is not sufficient.
bool path_is_fully_qualified(const std::string& path);

// The file that contains this implementation as reported by the OS loader. In the shipped SDK this
// is the loaded .so/.dll; there is deliberately no current-working-directory fallback.
bool loaded_module_path(std::string& out);

// Append `data` to `path`, creating it if absent. The whole write is one call; returns false on any
// failure. Used by the trace sink to flush buffered lines.
bool append_file(const std::string& path, const std::string& data);

// Create `path` as a new, empty file, failing if it already exists. This is the atomic ownership
// claim the trace sink makes on its trace.jsonl: an existing file is left untouched and the call
// returns false, so a stale or colliding run stream is never appended to.
bool create_new_file(const std::string& path);

// Whether `path` exists and is a directory. Used by the trace sink in runner mode, where the run
// directory is provided and must already exist rather than be created by the library.
bool directory_exists(const std::string& path);

// The OS process id, for the trace envelope and the run id. There is no portable C++ way to get it,
// so it lives behind the shim (getpid vs GetCurrentProcessId).
u64 process_id();

// Best-effort runtime version strings for alpha manifests. `wine_version` is empty outside Wine.
bool system_versions(std::string& os_version, std::string& wine_version);

// A broken-down UTC time, enough to format both the compact run-id stamp and the ISO-8601 timestamp
// in runtime.json without a locale or timezone database.
struct utc_time {
    int year;    // e.g. 2026
    int month;   // 1-12
    int day;     // 1-31
    int hour;    // 0-23
    int minute;  // 0-59
    int second;  // 0-60 (a leap second is possible)
};

// The current wall-clock time in UTC. False only if the clock is unavailable, in which case `out` is
// untouched. Behind the shim because C++11 has no portable UTC break-down (gmtime_r vs GetSystemTime).
bool utc_now(utc_time& out);

// Rename `from` to `to`, replacing an existing `to`. Returns false on failure. Used for log rotation.
bool rename_file(const std::string& from, const std::string& to);

// Remove `path`. Returns true if it is gone afterwards (an already-absent file is success), false only
// on a real error.
bool remove_file(const std::string& path);

// Where this instance keeps its profile. EOSR_DATA_DIR overrides it, which is how a launcher hands
// each local copy of a game its own profile -- and so its own identity -- without the copies having
// to agree on anything. Otherwise it is the user's per-application data directory. Empty when the OS
// gives us nowhere to write. The directory is not created here.
std::string user_data_directory();

// The same platform location without consulting EOSR_DATA_DIR. Bootstrap resolution handles that
// variable before consulting the sibling descriptor and calls this only for the final fallback.
std::string default_user_data_directory();

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
