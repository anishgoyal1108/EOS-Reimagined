#include "platform/manager_application.h"

#include <cstdint>
#include <ctime>
#include <limits>
#include <vector>

#include <windows.h>
#include <bcrypt.h>
#include <objbase.h>
#include <shellapi.h>
#include <shlobj.h>

namespace eosr {
namespace platform {

namespace {

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

manager_action_result failure(const char* code, const std::string& detail) {
    manager_action_result out;
    out.code = code;
    out.detail = detail;
    return out;
}

std::wstring quote_argument(const std::wstring& value) {
    if (value.find_first_of(L" \t\"") == std::wstring::npos) return value;
    std::wstring out(1, L'\"');
    std::size_t slashes = 0;
    for (std::size_t i = 0; i < value.size(); i++) {
        if (value[i] == L'\\') {
            slashes++;
        } else {
            if (value[i] == L'\"') out.append(slashes * 2 + 1, L'\\');
            else out.append(slashes, L'\\');
            slashes = 0;
            out.push_back(value[i]);
        }
    }
    out.append(slashes * 2, L'\\');
    out.push_back(L'\"');
    return out;
}

manager_action_result create_process(const std::string& executable,
                                     const std::vector<std::string>& arguments) {
    std::wstring wide_executable;
    if (!utf8_to_wide(executable, wide_executable) || arguments.size() > 128)
        return failure("invalid_launch", "launch executable or argument array is invalid");
    std::wstring command = quote_argument(wide_executable);
    for (std::size_t i = 0; i < arguments.size(); i++) {
        std::wstring wide;
        if (!utf8_to_wide(arguments[i], wide))
            return failure("invalid_launch", "launch argument is not valid UTF-8");
        command += L" " + quote_argument(wide);
    }
    std::vector<wchar_t> mutable_command(command.begin(), command.end());
    mutable_command.push_back(0);
    STARTUPINFOW startup = {};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process = {};
    if (!CreateProcessW(wide_executable.c_str(), &mutable_command[0], 0, 0, FALSE,
                        CREATE_UNICODE_ENVIRONMENT, 0, 0, &startup, &process))
        return failure("create_process_failed", "launcher process could not be created (Win32 " +
                                                 std::to_string(GetLastError()) + ")");
    manager_action_result out;
    out.ok = true;
    out.process_id = process.dwProcessId;
    out.code = "request_accepted";
    out.detail = "launch request was accepted; application startup is verified separately";
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return out;
}

manager_action_result shell_open(const std::wstring& target) {
    SHELLEXECUTEINFOW info = {};
    info.cbSize = sizeof(info);
    info.fMask = SEE_MASK_FLAG_NO_UI | SEE_MASK_NOASYNC;
    info.lpVerb = L"open";
    info.lpFile = target.c_str();
    info.nShow = SW_SHOWNORMAL;
    if (!ShellExecuteExW(&info))
        return failure("shell_open_failed", "Windows shell rejected the open request (Win32 " +
                                            std::to_string(GetLastError()) + ")");
    manager_action_result out;
    out.ok = true;
    out.code = "request_accepted";
    out.detail = "Windows shell accepted the request; application startup is verified separately";
    return out;
}

bool absolute_path(const std::wstring& path) {
    return (path.size() >= 3 && ((path[0] >= L'A' && path[0] <= L'Z') ||
           (path[0] >= L'a' && path[0] <= L'z')) && path[1] == L':' &&
           (path[2] == L'\\' || path[2] == L'/')) ||
           (path.size() >= 3 && path[0] == L'\\' && path[1] == L'\\');
}

} // namespace

manager_action_result::manager_action_result() : ok(false), process_id(0) {}

std::string manager_data_root() {
    const DWORD needed = GetEnvironmentVariableW(L"LOCALAPPDATA", 0, 0);
    if (needed == 0 || needed > 32768) return std::string();
    std::vector<wchar_t> value(needed, 0);
    if (GetEnvironmentVariableW(L"LOCALAPPDATA", &value[0], needed) == 0) return std::string();
    std::string utf8;
    return wide_to_utf8(std::wstring(&value[0]) + L"\\eos-reimagined-manager", utf8) ?
        utf8 : std::string();
}

std::string manager_executable_path() {
    std::vector<wchar_t> buffer(1024);
    while (buffer.size() <= 32768) {
        const DWORD count = GetModuleFileNameW(0, &buffer[0], static_cast<DWORD>(buffer.size()));
        if (count == 0) return std::string();
        if (count < buffer.size() - 1) {
            std::string out;
            return wide_to_utf8(std::wstring(&buffer[0], count), out) ? out : std::string();
        }
        buffer.resize(buffer.size() * 2);
    }
    return std::string();
}

bool manager_ensure_private_directory(const std::string& path, std::string& error) {
    error.clear();
    std::wstring wide;
    if (!utf8_to_wide(path, wide) || !absolute_path(wide)) {
        error = "manager directory path is invalid or not absolute";
        return false;
    }
    std::size_t start = wide.size() >= 2 && wide[1] == L':' ? 3 : 2;
    for (std::size_t i = start; i <= wide.size(); i++) {
        if (i != wide.size() && wide[i] != L'\\' && wide[i] != L'/') continue;
        const std::wstring part = wide.substr(0, i);
        if (part.empty()) continue;
        DWORD attributes = GetFileAttributesW(part.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES) {
            if (!CreateDirectoryW(part.c_str(), 0)) {
                error = "manager directory could not be created (Win32 " +
                        std::to_string(GetLastError()) + ")";
                return false;
            }
            attributes = GetFileAttributesW(part.c_str());
        }
        if (attributes == INVALID_FILE_ATTRIBUTES ||
            (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0 ||
            (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
            error = "manager directory contains an unsafe component";
            return false;
        }
    }
    return true;
}

std::string manager_random_id(std::string& error) {
    error.clear();
    unsigned char bytes[16];
    if (BCryptGenRandom(0, bytes, sizeof(bytes), BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0) {
        error = "Windows system random source failed";
        return std::string();
    }
    const char hex[] = "0123456789abcdef";
    std::string out(32, '0');
    for (std::size_t i = 0; i < sizeof(bytes); i++) {
        out[i * 2] = hex[bytes[i] >> 4];
        out[i * 2 + 1] = hex[bytes[i] & 15];
    }
    return out;
}

std::string manager_utc_now() {
    const std::time_t now = std::time(0);
    struct tm utc;
    if (now == static_cast<std::time_t>(-1) || gmtime_s(&utc, &now) != 0) return std::string();
    char buffer[32];
    return std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &utc) != 0 ?
        buffer : std::string();
}

manager_action_result manager_launch_steam(const manager::steam_launch_request& request) {
    if (request.shell_command) return failure("invalid_launch", "shell commands are not accepted");
    if (request.executable.empty()) {
        if (request.arguments.size() != 1 ||
            request.arguments[0].compare(0, 12, "steam://run/") != 0)
            return failure("invalid_launch", "registered URI launch request is invalid");
        std::wstring uri;
        if (!utf8_to_wide(request.arguments[0], uri))
            return failure("invalid_launch", "Steam URI is not valid UTF-8");
        return shell_open(uri);
    }
    return create_process(request.executable, request.arguments);
}

manager_action_result manager_open_location(const std::string& path, bool select_file) {
    std::wstring wide;
    if (!utf8_to_wide(path, wide) || !absolute_path(wide))
        return failure("invalid_path", "location path is invalid or not absolute");
    if (!select_file) return shell_open(wide);
    const HRESULT initialized = CoInitializeEx(0, COINIT_APARTMENTTHREADED);
    PIDLIST_ABSOLUTE item = ILCreateFromPathW(wide.c_str());
    if (item == 0) {
        if (SUCCEEDED(initialized)) CoUninitialize();
        return failure("location_missing", "location does not exist");
    }
    const HRESULT selected = SHOpenFolderAndSelectItems(item, 0, 0, 0);
    ILFree(item);
    if (SUCCEEDED(initialized)) CoUninitialize();
    if (FAILED(selected))
        return failure("select_failed", "Explorer could not select the requested file");
    manager_action_result out;
    out.ok = true;
    out.code = "request_accepted";
    out.detail = "Explorer accepted the file selection request";
    return out;
}

} // namespace platform
} // namespace eosr
