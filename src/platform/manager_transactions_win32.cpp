#include "platform/manager_transactions.h"

#include <cstdint>
#include <limits>
#include <vector>

#include <windows.h>

namespace eosr {
namespace platform {

namespace {

bool utf8_to_wide(const std::string& text, std::wstring& out) {
    out.clear();
    if (text.empty()) {
        return true;
    }
    if (text.size() > static_cast<std::size_t>((std::numeric_limits<int>::max)())) {
        return false;
    }
    const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                                          static_cast<int>(text.size()), 0, 0);
    if (count <= 0) {
        return false;
    }
    out.resize(static_cast<std::size_t>(count));
    return MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                               static_cast<int>(text.size()), &out[0], count) == count;
}

bool wide_to_utf8(const std::wstring& text, std::string& out) {
    out.clear();
    if (text.empty()) {
        return true;
    }
    const int count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(),
                                          static_cast<int>(text.size()), 0, 0, 0, 0);
    if (count <= 0) {
        return false;
    }
    out.resize(static_cast<std::size_t>(count));
    return WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(),
                               static_cast<int>(text.size()), &out[0], count, 0, 0) == count;
}

std::wstring strip_extended_prefix(const std::wstring& path) {
    if (path.compare(0, 8, L"\\\\?\\UNC\\") == 0) {
        return L"\\\\" + path.substr(8);
    }
    return path.compare(0, 4, L"\\\\?\\") == 0 ? path.substr(4) : path;
}

manager::transaction_io_result error_result(DWORD error) {
    if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) {
        return manager::transaction_io_result::missing;
    }
    if (error == ERROR_FILE_EXISTS || error == ERROR_ALREADY_EXISTS) {
        return manager::transaction_io_result::exists;
    }
    if (error == ERROR_ACCESS_DENIED || error == ERROR_WRITE_PROTECT ||
        error == ERROR_SHARING_VIOLATION || error == ERROR_LOCK_VIOLATION) {
        return manager::transaction_io_result::denied;
    }
    return manager::transaction_io_result::io_error;
}

HANDLE native_handle(i64 value) {
    return reinterpret_cast<HANDLE>(static_cast<std::intptr_t>(value));
}

} // namespace

manager_transaction_filesystem::manager_transaction_filesystem() : next_handle_(1) {}

manager_transaction_filesystem::~manager_transaction_filesystem() {
    for (std::map<manager::transaction_handle, i64>::const_iterator it = handles_.begin();
         it != handles_.end(); ++it) {
        CloseHandle(native_handle(it->second));
    }
}

manager::transaction_io_result manager_transaction_filesystem::inspect(
    const std::string& path, manager::transaction_file_info& out) {
    out = manager::transaction_file_info();
    std::wstring wide;
    if (!utf8_to_wide(path, wide)) {
        return manager::transaction_io_result::io_error;
    }
    const DWORD attributes = GetFileAttributesW(wide.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        return error_result(GetLastError());
    }
    out.exists = true;
    out.regular = (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0 &&
                  (attributes & FILE_ATTRIBUTE_DEVICE) == 0;
    out.symlink = (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
    out.writable = (attributes & FILE_ATTRIBUTE_READONLY) == 0;
    out.mode = out.writable ? 0644 : 0444;
    return manager::transaction_io_result::ok;
}

manager::transaction_io_result manager_transaction_filesystem::canonical_file(
    const std::string& path, std::string& out) {
    out.clear();
    std::wstring wide;
    if (!utf8_to_wide(path, wide)) {
        return manager::transaction_io_result::io_error;
    }
    const HANDLE file = CreateFileW(wide.c_str(), 0,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, 0, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL, 0);
    if (file == INVALID_HANDLE_VALUE) {
        return error_result(GetLastError());
    }
    const DWORD needed = GetFinalPathNameByHandleW(file, 0, 0, FILE_NAME_NORMALIZED);
    if (needed == 0 || needed > 32768) {
        CloseHandle(file);
        return manager::transaction_io_result::io_error;
    }
    std::vector<wchar_t> value(static_cast<std::size_t>(needed) + 1, 0);
    const DWORD written = GetFinalPathNameByHandleW(file, &value[0],
                                                    static_cast<DWORD>(value.size()),
                                                    FILE_NAME_NORMALIZED);
    CloseHandle(file);
    if (written == 0 || written >= value.size() ||
        !wide_to_utf8(strip_extended_prefix(std::wstring(&value[0], written)), out)) {
        return manager::transaction_io_result::io_error;
    }
    return manager::transaction_io_result::ok;
}

manager::transaction_io_result manager_transaction_filesystem::in_use(
    const std::string& path, bool& out) {
    out = false;
    std::wstring wide;
    if (!utf8_to_wide(path, wide)) {
        return manager::transaction_io_result::io_error;
    }
    const HANDLE file = CreateFileW(wide.c_str(), GENERIC_READ, 0, 0, OPEN_EXISTING,
                                    FILE_ATTRIBUTE_NORMAL, 0);
    if (file != INVALID_HANDLE_VALUE) {
        CloseHandle(file);
        return manager::transaction_io_result::ok;
    }
    const DWORD error = GetLastError();
    if (error == ERROR_SHARING_VIOLATION || error == ERROR_LOCK_VIOLATION) {
        out = true;
        return manager::transaction_io_result::ok;
    }
    return error_result(error);
}

manager::transaction_io_result manager_transaction_filesystem::open_read(
    const std::string& path, manager::transaction_handle& out) {
    std::wstring wide;
    if (!utf8_to_wide(path, wide)) {
        return manager::transaction_io_result::io_error;
    }
    const HANDLE file = CreateFileW(wide.c_str(), GENERIC_READ, FILE_SHARE_READ, 0,
                                    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL |
                                    FILE_FLAG_OPEN_REPARSE_POINT, 0);
    if (file == INVALID_HANDLE_VALUE) {
        return error_result(GetLastError());
    }
    BY_HANDLE_FILE_INFORMATION info;
    if (!GetFileInformationByHandle(file, &info) ||
        (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
        CloseHandle(file);
        return manager::transaction_io_result::io_error;
    }
    out = next_handle_++;
    handles_[out] = static_cast<i64>(reinterpret_cast<std::intptr_t>(file));
    return manager::transaction_io_result::ok;
}

manager::transaction_io_result manager_transaction_filesystem::create_new(
    const std::string& path, manager::transaction_handle& out) {
    std::wstring wide;
    if (!utf8_to_wide(path, wide)) {
        return manager::transaction_io_result::io_error;
    }
    const HANDLE file = CreateFileW(wide.c_str(), GENERIC_WRITE, 0, 0, CREATE_NEW,
                                    FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, 0);
    if (file == INVALID_HANDLE_VALUE) {
        return error_result(GetLastError());
    }
    out = next_handle_++;
    handles_[out] = static_cast<i64>(reinterpret_cast<std::intptr_t>(file));
    return manager::transaction_io_result::ok;
}

manager::transaction_io_result manager_transaction_filesystem::read(
    manager::transaction_handle handle, unsigned char* data, std::size_t capacity,
    std::size_t& count) {
    count = 0;
    const std::map<manager::transaction_handle, i64>::const_iterator it = handles_.find(handle);
    if (it == handles_.end()) {
        return manager::transaction_io_result::io_error;
    }
    const DWORD wanted = capacity > MAXDWORD ? MAXDWORD : static_cast<DWORD>(capacity);
    DWORD read_count = 0;
    if (!ReadFile(native_handle(it->second), data, wanted, &read_count, 0)) {
        return error_result(GetLastError());
    }
    if (read_count == 0) {
        return manager::transaction_io_result::end_of_file;
    }
    count = read_count;
    return manager::transaction_io_result::ok;
}

manager::transaction_io_result manager_transaction_filesystem::write(
    manager::transaction_handle handle, const unsigned char* data, std::size_t size,
    std::size_t& count) {
    count = 0;
    const std::map<manager::transaction_handle, i64>::const_iterator it = handles_.find(handle);
    if (it == handles_.end()) {
        return manager::transaction_io_result::io_error;
    }
    const DWORD wanted = size > MAXDWORD ? MAXDWORD : static_cast<DWORD>(size);
    DWORD written = 0;
    if (!WriteFile(native_handle(it->second), data, wanted, &written, 0)) {
        return error_result(GetLastError());
    }
    count = written;
    return manager::transaction_io_result::ok;
}

manager::transaction_io_result manager_transaction_filesystem::flush(
    manager::transaction_handle handle) {
    const std::map<manager::transaction_handle, i64>::const_iterator it = handles_.find(handle);
    if (it == handles_.end()) {
        return manager::transaction_io_result::io_error;
    }
    return FlushFileBuffers(native_handle(it->second)) ? manager::transaction_io_result::ok
                                                       : error_result(GetLastError());
}

manager::transaction_io_result manager_transaction_filesystem::close(
    manager::transaction_handle handle) {
    const std::map<manager::transaction_handle, i64>::iterator it = handles_.find(handle);
    if (it == handles_.end()) {
        return manager::transaction_io_result::io_error;
    }
    const HANDLE file = native_handle(it->second);
    handles_.erase(it);
    return CloseHandle(file) ? manager::transaction_io_result::ok : error_result(GetLastError());
}

manager::transaction_io_result manager_transaction_filesystem::set_mode(
    const std::string& path, unsigned int mode) {
    std::wstring wide;
    if (!utf8_to_wide(path, wide)) {
        return manager::transaction_io_result::io_error;
    }
    DWORD attributes = GetFileAttributesW(wide.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES ||
        (attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) != 0) {
        return attributes == INVALID_FILE_ATTRIBUTES ? error_result(GetLastError())
                                                     : manager::transaction_io_result::io_error;
    }
    if ((mode & 0200) != 0) {
        attributes &= ~FILE_ATTRIBUTE_READONLY;
    } else {
        attributes |= FILE_ATTRIBUTE_READONLY;
    }
    return SetFileAttributesW(wide.c_str(), attributes) ? manager::transaction_io_result::ok
                                                        : error_result(GetLastError());
}

manager::transaction_io_result manager_transaction_filesystem::rename_no_replace(
    const std::string& from, const std::string& to) {
    std::wstring wide_from;
    std::wstring wide_to;
    if (!utf8_to_wide(from, wide_from) || !utf8_to_wide(to, wide_to)) {
        return manager::transaction_io_result::io_error;
    }
    return MoveFileExW(wide_from.c_str(), wide_to.c_str(), MOVEFILE_WRITE_THROUGH)
               ? manager::transaction_io_result::ok : error_result(GetLastError());
}

manager::transaction_io_result manager_transaction_filesystem::rename_replace(
    const std::string& from, const std::string& to) {
    std::wstring wide_from;
    std::wstring wide_to;
    if (!utf8_to_wide(from, wide_from) || !utf8_to_wide(to, wide_to)) {
        return manager::transaction_io_result::io_error;
    }
    return MoveFileExW(wide_from.c_str(), wide_to.c_str(),
                       MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)
               ? manager::transaction_io_result::ok : error_result(GetLastError());
}

manager::transaction_io_result manager_transaction_filesystem::remove(const std::string& path) {
    std::wstring wide;
    if (!utf8_to_wide(path, wide)) {
        return manager::transaction_io_result::io_error;
    }
    return DeleteFileW(wide.c_str()) ? manager::transaction_io_result::ok
                                     : error_result(GetLastError());
}

manager::transaction_io_result manager_transaction_filesystem::flush_parent(const std::string&) {
    // File writes use FILE_FLAG_WRITE_THROUGH and both rename paths use MOVEFILE_WRITE_THROUGH.
    // Windows does not provide a portable directory-fsync equivalent for ordinary applications.
    return manager::transaction_io_result::ok;
}

} // namespace platform
} // namespace eosr
