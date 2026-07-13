#include "platform/paths.h"

#include <cerrno>
#include <cstdlib>

#include <fcntl.h>
#include <pwd.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

namespace eosr {
namespace platform {

namespace {

const char* const app_directory_name = "eos-reimagined";
const mode_t owner_only_directory = 0700;
const mode_t owner_only_file = 0600;

std::string from_env(const char* name) {
    const char* value = std::getenv(name);
    return (value != 0 && value[0] != '\0') ? std::string(value) : std::string();
}

std::string home_directory() {
    const std::string home = from_env("HOME");
    if (!home.empty()) {
        return home;
    }
    // A game launched without an environment still has a passwd entry.
    const struct passwd* entry = getpwuid(getuid());
    if (entry != 0 && entry->pw_dir != 0) {
        return entry->pw_dir;
    }
    return std::string();
}

} // namespace

std::string user_data_directory() {
    const std::string override_directory = from_env("EOSR_DATA_DIR");
    if (!override_directory.empty()) {
        return override_directory;
    }
    const std::string xdg = from_env("XDG_DATA_HOME");
    if (!xdg.empty()) {
        return xdg + "/" + app_directory_name;
    }
    const std::string home = home_directory();
    if (home.empty()) {
        return std::string();
    }
    return home + "/.local/share/" + app_directory_name;
}

bool make_directories(const std::string& path) {
    if (path.empty()) {
        return false;
    }
    // Create each parent in turn. One that already exists is not an error -- another instance of the
    // game may well have made it a moment ago.
    for (std::size_t i = 1; i <= path.size(); i++) {
        if (i != path.size() && path[i] != '/') {
            continue;
        }
        const std::string parent = path.substr(0, i);
        if (mkdir(parent.c_str(), owner_only_directory) != 0 && errno != EEXIST) {
            return false;
        }
    }
    struct stat info;
    return stat(path.c_str(), &info) == 0 && S_ISDIR(info.st_mode);
}

bool write_private_file(const std::string& path, const std::string& text) {
    const int file = ::open(path.c_str(), O_CREAT | O_TRUNC | O_WRONLY | O_CLOEXEC, owner_only_file);
    if (file < 0) {
        return false;
    }
    std::size_t written = 0;
    while (written < text.size()) {
        const ssize_t count = ::write(file, text.data() + written, text.size() - written);
        if (count > 0) {
            written += static_cast<std::size_t>(count);
            continue;
        }
        if (count < 0 && errno == EINTR) {
            continue;
        }
        ::close(file);
        return false;
    }
    return ::close(file) == 0;
}

file_lock::file_lock() : handle_(-1), held_(false) {
}

file_lock::~file_lock() {
    release();
}

bool file_lock::acquire(const std::string& path) {
    release();
    const int file = ::open(path.c_str(), O_CREAT | O_RDWR | O_CLOEXEC, owner_only_file);
    if (file < 0) {
        return false;
    }
    // An advisory lock rather than a lock file we create and delete: the kernel drops it when the
    // descriptor closes, so an instance that crashes does not leave the slot held forever.
    if (flock(file, LOCK_EX | LOCK_NB) != 0) {
        ::close(file);
        return false;
    }
    handle_ = file;
    held_ = true;
    return true;
}

void file_lock::release() {
    if (!held_) {
        return;
    }
    const int file = static_cast<int>(handle_);
    flock(file, LOCK_UN);
    ::close(file);
    handle_ = -1;
    held_ = false;
}

} // namespace platform
} // namespace eosr
