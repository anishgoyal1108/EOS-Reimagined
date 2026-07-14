#include "platform/paths.h"

#include <cstdint>
#include <cstdlib>
#include <limits>

#include <windows.h>

namespace eosr {
namespace platform {

namespace {

const char* const app_directory_name = "eos-reimagined";

std::string from_env(const char* name) {
    const char* value = std::getenv(name);
    return (value != 0 && value[0] != '\0') ? std::string(value) : std::string();
}

// A path component like "C:" names a drive, not a directory anyone can create.
bool is_drive(const std::string& component) {
    return component.size() == 2 && component[1] == ':';
}

} // namespace

std::string user_data_directory() {
    const std::string override_directory = from_env("EOSR_DATA_DIR");
    if (!override_directory.empty()) {
        return override_directory;
    }
    std::string base = from_env("LOCALAPPDATA");
    if (base.empty()) {
        base = from_env("APPDATA");
    }
    if (base.empty()) {
        const std::string profile = from_env("USERPROFILE");
        if (profile.empty()) {
            return std::string();
        }
        base = profile + "\\AppData\\Local";
    }
    return base + "\\" + app_directory_name;
}

bool make_directories(const std::string& path) {
    if (path.empty()) {
        return false;
    }
    for (std::size_t i = 1; i <= path.size(); i++) {
        if (i != path.size() && path[i] != '\\' && path[i] != '/') {
            continue;
        }
        const std::string parent = path.substr(0, i);
        if (is_drive(parent)) {
            continue;
        }
        if (!CreateDirectoryA(parent.c_str(), 0) && GetLastError() != ERROR_ALREADY_EXISTS) {
            return false;
        }
    }
    const DWORD attributes = GetFileAttributesA(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

bool write_private_file(const std::string& path, const std::string& text) {
    // The per-user application data directory is already restricted to this user by its inherited
    // ACL, so the file is private by virtue of where it lives; we only have to keep other processes
    // from sharing the handle while we write it.
    const HANDLE file = CreateFileA(path.c_str(), GENERIC_WRITE, 0, 0, CREATE_ALWAYS,
                                    FILE_ATTRIBUTE_NORMAL, 0);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }
    std::size_t written = 0;
    while (written < text.size()) {
        DWORD count = 0;
        if (!WriteFile(file, text.data() + written, static_cast<DWORD>(text.size() - written),
                       &count, 0) || count == 0) {
            CloseHandle(file);
            return false;
        }
        written += count;
    }
    CloseHandle(file);
    return true;
}

file_read read_file_capped(const std::string& path, std::size_t max_bytes, std::string& out) {
    out.clear();
    const HANDLE file = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING,
                                    FILE_ATTRIBUTE_NORMAL, 0);
    if (file == INVALID_HANDLE_VALUE) {
        const DWORD last = GetLastError();
        return (last == ERROR_FILE_NOT_FOUND || last == ERROR_PATH_NOT_FOUND) ? file_read::missing
                                                                              : file_read::unreadable;
    }
    // Read at most one byte past the cap: enough to tell "larger than max_bytes" from "exactly
    // max_bytes" without reading the whole oversized file. Guard the +1 against wrapping at the
    // maximum representable cap.
    const std::size_t cap = (max_bytes == std::numeric_limits<std::size_t>::max()) ? max_bytes
                                                                                   : max_bytes + 1;
    std::string buffer;
    char chunk[4096];
    std::size_t total = 0;
    while (total < cap) {
        const std::size_t want = (cap - total < sizeof(chunk)) ? (cap - total) : sizeof(chunk);
        DWORD count = 0;
        if (!ReadFile(file, chunk, static_cast<DWORD>(want), &count, 0)) {
            CloseHandle(file);
            return file_read::unreadable;
        }
        if (count == 0) {
            break;
        }
        total += count;
        buffer.append(chunk, count);
    }
    CloseHandle(file);
    if (total > max_bytes) {
        return file_read::too_large;
    }
    out.swap(buffer);
    return file_read::ok;
}

file_lock::file_lock() : handle_(-1), held_(false) {
}

file_lock::~file_lock() {
    release();
}

bool file_lock::acquire(const std::string& path) {
    release();
    // Opening with no share mode is the exclusion itself: a second instance asking for the same file
    // is refused. Windows closes the handle when the process ends, a crash included, so the slot
    // never stays held by an instance that is gone.
    const HANDLE file = CreateFileA(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, 0, OPEN_ALWAYS,
                                    FILE_ATTRIBUTE_NORMAL, 0);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }
    handle_ = static_cast<i64>(reinterpret_cast<std::intptr_t>(file));
    held_ = true;
    return true;
}

void file_lock::release() {
    if (!held_) {
        return;
    }
    CloseHandle(reinterpret_cast<HANDLE>(static_cast<std::intptr_t>(handle_)));
    handle_ = -1;
    held_ = false;
}

} // namespace platform
} // namespace eosr
