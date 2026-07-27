#include "platform/manager_runs.h"

#include <cstdint>
#include <limits>
#include <vector>

#include <windows.h>

namespace eosr {
namespace platform {

namespace {

manager::run_io_result map_result(manager::transaction_io_result result) {
    switch (result) {
        case manager::transaction_io_result::ok: return manager::run_io_result::ok;
        case manager::transaction_io_result::missing: return manager::run_io_result::missing;
        case manager::transaction_io_result::exists: return manager::run_io_result::exists;
        case manager::transaction_io_result::denied: return manager::run_io_result::denied;
        case manager::transaction_io_result::end_of_file:
            return manager::run_io_result::end_of_file;
        case manager::transaction_io_result::io_error: return manager::run_io_result::io_error;
    }
    return manager::run_io_result::io_error;
}

manager::run_io_result error_result(DWORD error) {
    if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND)
        return manager::run_io_result::missing;
    if (error == ERROR_FILE_EXISTS || error == ERROR_ALREADY_EXISTS)
        return manager::run_io_result::exists;
    if (error == ERROR_ACCESS_DENIED || error == ERROR_WRITE_PROTECT ||
        error == ERROR_SHARING_VIOLATION) return manager::run_io_result::denied;
    return manager::run_io_result::io_error;
}

bool utf8_to_wide(const std::string& text, std::wstring& out) {
    out.clear();
    if (text.empty() || text.size() > static_cast<std::size_t>((std::numeric_limits<int>::max)()))
        return false;
    const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                                          static_cast<int>(text.size()), 0, 0);
    if (count <= 0) return false;
    out.resize(static_cast<std::size_t>(count));
    return MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                               static_cast<int>(text.size()), &out[0], count) == count;
}

bool wide_to_utf8(const std::wstring& text, std::string& out) {
    out.clear();
    if (text.empty()) return true;
    const int count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(),
                                          static_cast<int>(text.size()), 0, 0, 0, 0);
    if (count <= 0) return false;
    out.resize(static_cast<std::size_t>(count));
    return WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(),
                               static_cast<int>(text.size()), &out[0], count, 0, 0) == count;
}

std::wstring search_pattern(const std::wstring& path) {
    if (!path.empty() && path[path.size() - 1] != L'\\' && path[path.size() - 1] != L'/')
        return path + L"\\*";
    return path + L"*";
}

u64 file_time(const FILETIME& time) {
    ULARGE_INTEGER value;
    value.LowPart = time.dwLowDateTime;
    value.HighPart = time.dwHighDateTime;
    return static_cast<u64>(value.QuadPart);
}

} // namespace

manager::run_io_result manager_run_filesystem::list_directory(
    const std::string& path, std::vector<manager::run_entry>& out) {
    out.clear();
    std::wstring wide;
    if (!utf8_to_wide(path, wide)) return manager::run_io_result::io_error;
    WIN32_FIND_DATAW native;
    HANDLE find = FindFirstFileW(search_pattern(wide).c_str(), &native);
    if (find == INVALID_HANDLE_VALUE) return error_result(GetLastError());
    bool ok = true;
    do {
        const std::wstring wide_name = native.cFileName;
        if (wide_name == L"." || wide_name == L"..") continue;
        manager::run_entry entry;
        if (!wide_to_utf8(wide_name, entry.name)) {
            ok = false;
            break;
        }
        if ((native.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
            entry.kind = manager::run_entry_kind::symlink;
        else if ((native.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
            entry.kind = manager::run_entry_kind::directory;
        else if ((native.dwFileAttributes & FILE_ATTRIBUTE_DEVICE) == 0)
            entry.kind = manager::run_entry_kind::file;
        else
            entry.kind = manager::run_entry_kind::other;
        if (entry.kind == manager::run_entry_kind::file)
            entry.size = (static_cast<u64>(native.nFileSizeHigh) << 32U) | native.nFileSizeLow;
        entry.modified = file_time(native.ftLastWriteTime);
        out.push_back(entry);
        if (out.size() > 4096) break;
    } while (FindNextFileW(find, &native));
    const DWORD last = GetLastError();
    if (!FindClose(find)) ok = false;
    if (out.size() > 4096) return manager::run_io_result::too_large;
    if (!ok || last != ERROR_NO_MORE_FILES) return manager::run_io_result::io_error;
    return manager::run_io_result::ok;
}

manager::run_io_result manager_run_filesystem::read_file(const std::string& path,
                                                          std::size_t cap, std::string& out) {
    out.clear();
    manager::run_handle handle = 0;
    manager::run_io_result result = open_read(path, handle);
    if (result != manager::run_io_result::ok) return result;
    unsigned char buffer[8192];
    while (out.size() <= cap) {
        std::size_t count = 0;
        result = read(handle, buffer, sizeof(buffer), count);
        if (result == manager::run_io_result::end_of_file) {
            result = manager::run_io_result::ok;
            break;
        }
        if (result != manager::run_io_result::ok || count == 0) break;
        if (count > cap - (out.size() > cap ? cap : out.size())) {
            result = manager::run_io_result::too_large;
            break;
        }
        out.append(reinterpret_cast<const char*>(buffer), count);
    }
    if (close(handle) != manager::run_io_result::ok && result == manager::run_io_result::ok)
        result = manager::run_io_result::io_error;
    if (result != manager::run_io_result::ok) out.clear();
    return result;
}

manager::run_io_result manager_run_filesystem::open_read(const std::string& path,
                                                          manager::run_handle& out) {
    return map_result(files_.open_read(path, out));
}

manager::run_io_result manager_run_filesystem::create_private_directory(const std::string& path) {
    std::wstring wide;
    if (!utf8_to_wide(path, wide)) return manager::run_io_result::io_error;
    return CreateDirectoryW(wide.c_str(), 0) ? manager::run_io_result::ok :
                                              error_result(GetLastError());
}

manager::run_io_result manager_run_filesystem::create_new(const std::string& path,
                                                           manager::run_handle& out) {
    return map_result(files_.create_new(path, out));
}

manager::run_io_result manager_run_filesystem::read(manager::run_handle handle,
                                                     unsigned char* data, std::size_t capacity,
                                                     std::size_t& count) {
    return map_result(files_.read(handle, data, capacity, count));
}

manager::run_io_result manager_run_filesystem::write(manager::run_handle handle,
                                                      const unsigned char* data, std::size_t size,
                                                      std::size_t& count) {
    return map_result(files_.write(handle, data, size, count));
}

manager::run_io_result manager_run_filesystem::flush(manager::run_handle handle) {
    return map_result(files_.flush(handle));
}

manager::run_io_result manager_run_filesystem::close(manager::run_handle handle) {
    return map_result(files_.close(handle));
}

manager::run_io_result manager_run_filesystem::remove_file(const std::string& path) {
    return map_result(files_.remove(path));
}

manager::run_io_result manager_run_filesystem::remove_empty_directory(const std::string& path) {
    std::wstring wide;
    if (!utf8_to_wide(path, wide)) return manager::run_io_result::io_error;
    return RemoveDirectoryW(wide.c_str()) ? manager::run_io_result::ok :
                                           error_result(GetLastError());
}

manager::run_io_result manager_run_filesystem::flush_parent(const std::string& path) {
    return map_result(files_.flush_parent(path));
}

} // namespace platform
} // namespace eosr
