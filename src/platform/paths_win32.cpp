#include "platform/paths.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <sstream>

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

struct rtl_os_version_info {
    ULONG size;
    ULONG major;
    ULONG minor;
    ULONG build;
    ULONG platform;
    WCHAR service_pack[128];
};

} // namespace

bool system_versions(std::string& os_version, std::string& wine_version) {
    os_version.clear();
    wine_version.clear();
    HMODULE ntdll = GetModuleHandleA("ntdll.dll");
    if (ntdll == 0) {
        return false;
    }

    typedef LONG (WINAPI *rtl_get_version_fn)(rtl_os_version_info*);
    const FARPROC version_address = GetProcAddress(ntdll, "RtlGetVersion");
    rtl_get_version_fn get_version = 0;
    static_assert(sizeof(get_version) == sizeof(version_address), "function pointer size mismatch");
    std::memcpy(&get_version, &version_address, sizeof(get_version));
    if (get_version != 0) {
        rtl_os_version_info info = {};
        info.size = sizeof(info);
        if (get_version(&info) == 0) {
            std::ostringstream version;
            version << info.major << '.' << info.minor << '.' << info.build;
            os_version = version.str();
        }
    }

    typedef const char* (__cdecl *wine_get_version_fn)();
    const FARPROC wine_address = GetProcAddress(ntdll, "wine_get_version");
    wine_get_version_fn get_wine = 0;
    static_assert(sizeof(get_wine) == sizeof(wine_address), "function pointer size mismatch");
    std::memcpy(&get_wine, &wine_address, sizeof(get_wine));
    if (get_wine != 0) {
        const char* value = get_wine();
        if (value != 0) {
            wine_version = value;
        }
    }
    return !os_version.empty();
}

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

bool path_is_absolute(const std::string& path) {
    if (path.empty()) {
        return false;
    }
    if (path[0] == '/' || path[0] == '\\') {
        return true; // rooted on the current drive
    }
    // A drive-absolute path needs a separator after the colon (C:\ or C:/). C:traces is drive-relative.
    const char c = path[0];
    return path.size() >= 3 && path[1] == ':' && (path[2] == '/' || path[2] == '\\') &&
           ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'));
}

bool append_file(const std::string& path, const std::string& data) {
    const HANDLE file = CreateFileA(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, 0, OPEN_ALWAYS,
                                    FILE_ATTRIBUTE_NORMAL, 0);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }
    std::size_t written = 0;
    while (written < data.size()) {
        DWORD count = 0;
        if (!WriteFile(file, data.data() + written, static_cast<DWORD>(data.size() - written),
                       &count, 0) ||
            count == 0) {
            CloseHandle(file);
            return false;
        }
        written += count;
    }
    CloseHandle(file);
    return true;
}

bool create_new_file(const std::string& path) {
    // CREATE_NEW fails if the file already exists, so the ownership claim is atomic and never clobbers
    // or appends to a stale run stream.
    const HANDLE file = CreateFileA(path.c_str(), GENERIC_WRITE, 0, 0, CREATE_NEW,
                                    FILE_ATTRIBUTE_NORMAL, 0);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }
    CloseHandle(file);
    return true;
}

bool directory_exists(const std::string& path) {
    const DWORD attributes = GetFileAttributesA(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

u64 process_id() {
    return static_cast<u64>(GetCurrentProcessId());
}

bool utc_now(utc_time& out) {
    SYSTEMTIME st;
    GetSystemTime(&st); // already UTC, no timezone conversion needed
    out.year = st.wYear;
    out.month = st.wMonth;
    out.day = st.wDay;
    out.hour = st.wHour;
    out.minute = st.wMinute;
    out.second = st.wSecond;
    return true;
}

bool rename_file(const std::string& from, const std::string& to) {
    return MoveFileExA(from.c_str(), to.c_str(), MOVEFILE_REPLACE_EXISTING) != 0;
}

bool remove_file(const std::string& path) {
    if (DeleteFileA(path.c_str())) {
        return true;
    }
    const DWORD last = GetLastError();
    return last == ERROR_FILE_NOT_FOUND || last == ERROR_PATH_NOT_FOUND;
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
