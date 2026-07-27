#include "platform/manager_application.h"

#include <cerrno>
#include <cstdlib>
#include <ctime>
#include <limits>
#include <vector>

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace eosr {
namespace platform {

namespace {

bool absolute_path(const std::string& path) {
    return !path.empty() && path[0] == '/' && path.find('\0') == std::string::npos;
}

std::string parent_path(const std::string& path) {
    const std::string::size_type at = path.find_last_of('/');
    if (at == std::string::npos) return std::string();
    return at == 0 ? std::string("/") : path.substr(0, at);
}

manager_action_result failure(const char* code, const std::string& detail) {
    manager_action_result out;
    out.code = code;
    out.detail = detail;
    return out;
}

manager_action_result detached_exec(const std::string& executable,
                                    const std::vector<std::string>& arguments) {
    if (executable.empty() || executable.find('\0') != std::string::npos ||
        arguments.size() > 128) return failure("invalid_launch", "launch argument array is invalid");
    int status_pipe[2];
    if (pipe(status_pipe) != 0) return failure("pipe_failed", "launch status pipe failed");
    fcntl(status_pipe[0], F_SETFD, FD_CLOEXEC);
    fcntl(status_pipe[1], F_SETFD, FD_CLOEXEC);
    const pid_t intermediate = fork();
    if (intermediate < 0) {
        close(status_pipe[0]); close(status_pipe[1]);
        return failure("fork_failed", "launcher process could not be created");
    }
    if (intermediate == 0) {
        close(status_pipe[0]);
        const pid_t child = fork();
        if (child < 0) {
            const int error = errno;
            static_cast<void>(write(status_pipe[1], &error, sizeof(error)));
            _exit(127);
        }
        if (child > 0) _exit(0);
        setsid();
        std::vector<char*> argv;
        argv.reserve(arguments.size() + 2);
        argv.push_back(const_cast<char*>(executable.c_str()));
        for (std::size_t i = 0; i < arguments.size(); i++)
            argv.push_back(const_cast<char*>(arguments[i].c_str()));
        argv.push_back(0);
        execvp(executable.c_str(), &argv[0]);
        const int error = errno;
        static_cast<void>(write(status_pipe[1], &error, sizeof(error)));
        _exit(127);
    }
    close(status_pipe[1]);
    int wait_status = 0;
    while (waitpid(intermediate, &wait_status, 0) < 0 && errno == EINTR) {}
    int child_error = 0;
    ssize_t count;
    do {
        count = read(status_pipe[0], &child_error, sizeof(child_error));
    } while (count < 0 && errno == EINTR);
    close(status_pipe[0]);
    if (count > 0)
        return failure("exec_failed", "launcher executable could not be started (errno " +
                                      std::to_string(child_error) + ")");
    if (count < 0) return failure("status_failed", "launcher status could not be read");
    manager_action_result out;
    out.ok = true;
    out.code = "request_accepted";
    out.detail = "launch request was accepted; application startup is verified separately";
    return out;
}

bool secure_component(const std::string& path) {
    struct stat info;
    if (lstat(path.c_str(), &info) != 0) return false;
    return S_ISDIR(info.st_mode) && !S_ISLNK(info.st_mode);
}

} // namespace

manager_action_result::manager_action_result() : ok(false), process_id(0) {}

std::string manager_data_root() {
    const char* xdg = std::getenv("XDG_DATA_HOME");
    if (xdg != 0 && absolute_path(xdg)) return std::string(xdg) + "/eos-reimagined-manager";
    const char* home = std::getenv("HOME");
    return home != 0 && absolute_path(home) ?
        std::string(home) + "/.local/share/eos-reimagined-manager" : std::string();
}

std::string manager_executable_path() {
    std::vector<char> buffer(1024);
    while (buffer.size() <= 1024 * 1024) {
        const ssize_t count = readlink("/proc/self/exe", &buffer[0], buffer.size());
        if (count < 0) return std::string();
        if (static_cast<std::size_t>(count) < buffer.size())
            return std::string(&buffer[0], static_cast<std::size_t>(count));
        buffer.resize(buffer.size() * 2);
    }
    return std::string();
}

bool manager_ensure_private_directory(const std::string& path, std::string& error) {
    error.clear();
    if (!absolute_path(path)) {
        error = "manager directory path is not absolute";
        return false;
    }
    std::size_t at = 1;
    while (at <= path.size()) {
        const std::size_t slash = path.find('/', at);
        const std::string part = path.substr(0, slash == std::string::npos ? path.size() : slash);
        if (!secure_component(part)) {
            if (errno != ENOENT || mkdir(part.c_str(), 0700) != 0 || !secure_component(part)) {
                error = "manager directory component is missing, unsafe, or unwritable: " + part;
                return false;
            }
        }
        if (slash == std::string::npos) break;
        at = slash + 1;
        while (at < path.size() && path[at] == '/') at++;
    }
    if (chmod(path.c_str(), 0700) != 0) {
        error = "manager directory permissions could not be restricted";
        return false;
    }
    return true;
}

std::string manager_random_id(std::string& error) {
    error.clear();
    unsigned char bytes[16];
    const int file = open("/dev/urandom", O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (file < 0) {
        error = "system random source could not be opened";
        return std::string();
    }
    std::size_t offset = 0;
    while (offset < sizeof(bytes)) {
        const ssize_t count = read(file, bytes + offset, sizeof(bytes) - offset);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) break;
        offset += static_cast<std::size_t>(count);
    }
    const bool closed = close(file) == 0;
    if (offset != sizeof(bytes) || !closed) {
        error = "system random source read failed";
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
    if (now == static_cast<std::time_t>(-1) || gmtime_r(&now, &utc) == 0) return std::string();
    char buffer[32];
    return std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &utc) != 0 ?
        buffer : std::string();
}

manager_action_result manager_launch_steam(const manager::steam_launch_request& request) {
    if (request.shell_command || request.executable.empty())
        return failure("invalid_launch", "Linux Steam launch needs an executable argument array");
    return detached_exec(request.executable, request.arguments);
}

manager_action_result manager_open_location(const std::string& path, bool select_file) {
    if (!absolute_path(path)) return failure("invalid_path", "location path is not absolute");
    const std::string opened = select_file ? parent_path(path) : path;
    if (opened.empty()) return failure("invalid_path", "location has no openable parent");
    std::vector<std::string> arguments;
    arguments.push_back(opened);
    manager_action_result out = detached_exec("xdg-open", arguments);
    if (out.ok)
        out.detail = "desktop open request was accepted; the opener reports any later failure";
    return out;
}

} // namespace platform
} // namespace eosr
