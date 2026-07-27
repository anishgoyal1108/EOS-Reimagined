#include "platform/manager_discovery.h"

#include <algorithm>
#include <limits>
#include <set>
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
    if (path.compare(0, 4, L"\\\\?\\") == 0) {
        return path.substr(4);
    }
    return path;
}

bool registry_string(const wchar_t* name, std::wstring& out) {
    out.clear();
    HKEY key = 0;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", 0, KEY_QUERY_VALUE,
                      &key) != ERROR_SUCCESS) {
        return false;
    }
    DWORD type = 0;
    DWORD bytes = 0;
    LONG result = RegQueryValueExW(key, name, 0, &type, 0, &bytes);
    if (result != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ) ||
        bytes < sizeof(wchar_t) || bytes > 32768 * sizeof(wchar_t)) {
        RegCloseKey(key);
        return false;
    }
    std::vector<wchar_t> buffer(bytes / sizeof(wchar_t) + 1, 0);
    result = RegQueryValueExW(key, name, 0, &type, reinterpret_cast<BYTE*>(&buffer[0]), &bytes);
    RegCloseKey(key);
    if (result != ERROR_SUCCESS) {
        return false;
    }
    buffer[buffer.size() - 1] = 0;
    out.assign(&buffer[0]);
    if (type == REG_EXPAND_SZ) {
        const DWORD needed = ExpandEnvironmentStringsW(out.c_str(), 0, 0);
        if (needed == 0 || needed > 32768) {
            return false;
        }
        std::vector<wchar_t> expanded(needed, 0);
        if (ExpandEnvironmentStringsW(out.c_str(), &expanded[0], needed) != needed) {
            return false;
        }
        out.assign(&expanded[0]);
    }
    return !out.empty();
}

std::wstring environment_string(const wchar_t* name) {
    const DWORD needed = GetEnvironmentVariableW(name, 0, 0);
    if (needed == 0 || needed > 32768) {
        return std::wstring();
    }
    std::vector<wchar_t> value(needed, 0);
    if (GetEnvironmentVariableW(name, &value[0], needed) == 0) {
        return std::wstring();
    }
    return &value[0];
}

std::wstring parent_path(const std::wstring& path) {
    const std::wstring::size_type separator = path.find_last_of(L"\\/");
    return separator == std::wstring::npos ? std::wstring() : path.substr(0, separator);
}

void append_candidate(std::vector<manager::steam_root_candidate>& roots,
                      std::set<std::string>& seen, const std::wstring& wide_path) {
    std::string path;
    if (wide_path.empty() || !wide_to_utf8(wide_path, path) || !seen.insert(path).second) {
        return;
    }
    manager::steam_root_candidate candidate;
    candidate.path = path;
    candidate.kind = manager::steam_install_kind::native_windows;
    roots.push_back(candidate);
}

} // namespace

bool manager_discovery_filesystem::canonical_directory(const std::string& path,
                                                       std::string& out) {
    out.clear();
    std::wstring wide_path;
    if (!utf8_to_wide(path, wide_path)) {
        return false;
    }
    const HANDLE directory = CreateFileW(wide_path.c_str(), 0,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, 0, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS, 0);
    if (directory == INVALID_HANDLE_VALUE) {
        return false;
    }
    const DWORD attributes = GetFileAttributesW(wide_path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
        CloseHandle(directory);
        return false;
    }
    const DWORD needed = GetFinalPathNameByHandleW(directory, 0, 0, FILE_NAME_NORMALIZED);
    if (needed == 0 || needed > 32768) {
        CloseHandle(directory);
        return false;
    }
    std::vector<wchar_t> final_path(static_cast<std::size_t>(needed) + 1, 0);
    const DWORD written = GetFinalPathNameByHandleW(directory, &final_path[0],
                                                    static_cast<DWORD>(final_path.size()),
                                                    FILE_NAME_NORMALIZED);
    CloseHandle(directory);
    if (written == 0 || written >= final_path.size()) {
        return false;
    }
    return wide_to_utf8(strip_extended_prefix(std::wstring(&final_path[0], written)), out);
}

manager::discovery_read manager_discovery_filesystem::read_file(
    const std::string& path, std::size_t max_bytes, std::string& out) {
    out.clear();
    std::wstring wide_path;
    if (!utf8_to_wide(path, wide_path)) {
        return manager::discovery_read::unreadable;
    }
    const HANDLE file = CreateFileW(wide_path.c_str(), GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, 0, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL, 0);
    if (file == INVALID_HANDLE_VALUE) {
        const DWORD error = GetLastError();
        return (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND)
                   ? manager::discovery_read::missing : manager::discovery_read::unreadable;
    }
    const std::size_t cap = max_bytes == (std::numeric_limits<std::size_t>::max)()
                                ? max_bytes : max_bytes + 1;
    std::string bytes;
    char buffer[4096];
    while (bytes.size() < cap) {
        const std::size_t remaining = cap - bytes.size();
        const DWORD wanted = static_cast<DWORD>(remaining < sizeof(buffer) ? remaining
                                                                           : sizeof(buffer));
        DWORD count = 0;
        if (!ReadFile(file, buffer, wanted, &count, 0)) {
            CloseHandle(file);
            return manager::discovery_read::unreadable;
        }
        if (count == 0) {
            break;
        }
        bytes.append(buffer, count);
    }
    if (!CloseHandle(file)) {
        return manager::discovery_read::unreadable;
    }
    if (bytes.size() > max_bytes) {
        return manager::discovery_read::too_large;
    }
    out.swap(bytes);
    return manager::discovery_read::ok;
}

bool manager_discovery_filesystem::list_file_names(const std::string& path,
                                                   std::vector<std::string>& out) {
    out.clear();
    std::wstring wide_path;
    if (!utf8_to_wide(path, wide_path)) {
        return false;
    }
    if (!wide_path.empty() && wide_path[wide_path.size() - 1] != L'\\' &&
        wide_path[wide_path.size() - 1] != L'/') {
        wide_path += L'\\';
    }
    WIN32_FIND_DATAW found;
    const HANDLE search = FindFirstFileW((wide_path + L"*").c_str(), &found);
    if (search == INVALID_HANDLE_VALUE) {
        return false;
    }
    do {
        const std::wstring name = found.cFileName;
        std::string utf8;
        if (name != L"." && name != L".." && wide_to_utf8(name, utf8)) {
            out.push_back(utf8);
        }
    } while (FindNextFileW(search, &found));
    const DWORD error = GetLastError();
    FindClose(search);
    if (error != ERROR_NO_MORE_FILES) {
        out.clear();
        return false;
    }
    return true;
}

bool manager_discovery_filesystem::list_directory(
    const std::string& path, std::vector<manager::target_directory_entry>& out) {
    out.clear();
    std::wstring wide_path;
    if (!utf8_to_wide(path, wide_path)) {
        return false;
    }
    if (!wide_path.empty() && wide_path[wide_path.size() - 1] != L'\\' &&
        wide_path[wide_path.size() - 1] != L'/') {
        wide_path += L'\\';
    }
    WIN32_FIND_DATAW found;
    const HANDLE search = FindFirstFileW((wide_path + L"*").c_str(), &found);
    if (search == INVALID_HANDLE_VALUE) {
        return false;
    }
    do {
        const std::wstring name = found.cFileName;
        manager::target_directory_entry item;
        if (name == L"." || name == L".." || !wide_to_utf8(name, item.name)) {
            continue;
        }
        if ((found.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
            item.kind = manager::target_entry_kind::symlink;
        } else if ((found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
            item.kind = manager::target_entry_kind::directory;
        } else if ((found.dwFileAttributes & FILE_ATTRIBUTE_DEVICE) == 0) {
            item.kind = manager::target_entry_kind::file;
        } else {
            item.kind = manager::target_entry_kind::other;
        }
        out.push_back(item);
    } while (FindNextFileW(search, &found));
    const DWORD error = GetLastError();
    FindClose(search);
    if (error != ERROR_NO_MORE_FILES) {
        out.clear();
        return false;
    }
    return true;
}

manager::discovery_read manager_discovery_filesystem::read_prefix(
    const std::string& path, std::size_t max_bytes, std::string& out) {
    out.clear();
    std::wstring wide_path;
    if (!utf8_to_wide(path, wide_path)) {
        return manager::discovery_read::unreadable;
    }
    const HANDLE file = CreateFileW(wide_path.c_str(), GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, 0, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL, 0);
    if (file == INVALID_HANDLE_VALUE) {
        const DWORD error = GetLastError();
        return (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND)
                   ? manager::discovery_read::missing : manager::discovery_read::unreadable;
    }
    std::string bytes;
    char buffer[4096];
    while (bytes.size() < max_bytes) {
        const std::size_t remaining = max_bytes - bytes.size();
        const DWORD wanted = static_cast<DWORD>(remaining < sizeof(buffer) ? remaining
                                                                           : sizeof(buffer));
        DWORD count = 0;
        if (!ReadFile(file, buffer, wanted, &count, 0)) {
            CloseHandle(file);
            return manager::discovery_read::unreadable;
        }
        if (count == 0) {
            break;
        }
        bytes.append(buffer, count);
    }
    if (!CloseHandle(file)) {
        return manager::discovery_read::unreadable;
    }
    out.swap(bytes);
    return manager::discovery_read::ok;
}

bool manager_discovery_filesystem::canonical_file(const std::string& path, std::string& out) {
    out.clear();
    std::wstring wide_path;
    if (!utf8_to_wide(path, wide_path)) {
        return false;
    }
    const HANDLE file = CreateFileW(wide_path.c_str(), 0,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, 0, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL, 0);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }
    const DWORD attributes = GetFileAttributesW(wide_path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
        CloseHandle(file);
        return false;
    }
    const DWORD needed = GetFinalPathNameByHandleW(file, 0, 0, FILE_NAME_NORMALIZED);
    if (needed == 0 || needed > 32768) {
        CloseHandle(file);
        return false;
    }
    std::vector<wchar_t> final_path(static_cast<std::size_t>(needed) + 1, 0);
    const DWORD written = GetFinalPathNameByHandleW(file, &final_path[0],
                                                    static_cast<DWORD>(final_path.size()),
                                                    FILE_NAME_NORMALIZED);
    CloseHandle(file);
    if (written == 0 || written >= final_path.size()) {
        return false;
    }
    return wide_to_utf8(strip_extended_prefix(std::wstring(&final_path[0], written)), out);
}

std::vector<manager::steam_root_candidate> platform_steam_root_candidates() {
    std::vector<manager::steam_root_candidate> roots;
    std::set<std::string> seen;
    std::wstring value;
    if (registry_string(L"SteamPath", value)) {
        append_candidate(roots, seen, value);
    }
    if (registry_string(L"SteamExe", value)) {
        append_candidate(roots, seen, parent_path(value));
    }
    const std::wstring program_files_x86 = environment_string(L"ProgramFiles(x86)");
    if (!program_files_x86.empty()) {
        append_candidate(roots, seen, program_files_x86 + L"\\Steam");
    }
    const std::wstring program_files = environment_string(L"ProgramFiles");
    if (!program_files.empty()) {
        append_candidate(roots, seen, program_files + L"\\Steam");
    }
    append_candidate(roots, seen, L"C:\\Program Files (x86)\\Steam");
    append_candidate(roots, seen, L"C:\\Program Files\\Steam");
    return roots;
}

} // namespace platform
} // namespace eosr
