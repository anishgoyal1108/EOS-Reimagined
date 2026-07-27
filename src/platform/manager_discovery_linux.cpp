#include "platform/manager_discovery.h"

#include <cerrno>
#include <cstdlib>
#include <limits>

#include <dirent.h>
#include <fcntl.h>
#include <limits.h>
#include <pwd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

namespace eosr {
namespace platform {

namespace {

std::string home_directory() {
    const char* home = std::getenv("HOME");
    if (home != 0 && home[0] != '\0') {
        return home;
    }
    const struct passwd* entry = getpwuid(getuid());
    return entry != 0 && entry->pw_dir != 0 ? entry->pw_dir : std::string();
}

} // namespace

bool manager_discovery_filesystem::canonical_directory(const std::string& path,
                                                       std::string& out) {
    out.clear();
    char resolved[PATH_MAX];
    if (realpath(path.c_str(), resolved) == 0) {
        return false;
    }
    struct stat info;
    if (stat(resolved, &info) != 0 || !S_ISDIR(info.st_mode)) {
        return false;
    }
    out = resolved;
    return true;
}

manager::discovery_read manager_discovery_filesystem::read_file(
    const std::string& path, std::size_t max_bytes, std::string& out) {
    out.clear();
    const int file = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (file < 0) {
        return (errno == ENOENT || errno == ENOTDIR) ? manager::discovery_read::missing
                                                     : manager::discovery_read::unreadable;
    }
    const std::size_t cap = max_bytes == std::numeric_limits<std::size_t>::max()
                                ? max_bytes : max_bytes + 1;
    std::string bytes;
    char buffer[4096];
    while (bytes.size() < cap) {
        const std::size_t remaining = cap - bytes.size();
        const std::size_t wanted = remaining < sizeof(buffer) ? remaining : sizeof(buffer);
        const ssize_t count = ::read(file, buffer, wanted);
        if (count < 0 && errno == EINTR) {
            continue;
        }
        if (count < 0) {
            ::close(file);
            return manager::discovery_read::unreadable;
        }
        if (count == 0) {
            break;
        }
        bytes.append(buffer, static_cast<std::size_t>(count));
    }
    if (::close(file) != 0) {
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
    DIR* directory = opendir(path.c_str());
    if (directory == 0) {
        return false;
    }
    errno = 0;
    struct dirent* entry = 0;
    while ((entry = readdir(directory)) != 0) {
        const std::string name = entry->d_name;
        if (name != "." && name != "..") {
            out.push_back(name);
        }
        errno = 0;
    }
    const bool read_ok = errno == 0;
    const bool close_ok = closedir(directory) == 0;
    if (!read_ok || !close_ok) {
        out.clear();
        return false;
    }
    return true;
}

bool manager_discovery_filesystem::list_directory(
    const std::string& path, std::vector<manager::target_directory_entry>& out) {
    out.clear();
    DIR* directory = opendir(path.c_str());
    if (directory == 0) {
        return false;
    }
    errno = 0;
    struct dirent* entry = 0;
    while ((entry = readdir(directory)) != 0) {
        const std::string name = entry->d_name;
        if (name == "." || name == "..") {
            errno = 0;
            continue;
        }
        manager::target_directory_entry item;
        item.name = name;
        const std::string child = path + "/" + name;
        struct stat info;
        if (lstat(child.c_str(), &info) != 0) {
            item.kind = manager::target_entry_kind::other;
        } else if (S_ISLNK(info.st_mode)) {
            item.kind = manager::target_entry_kind::symlink;
        } else if (S_ISDIR(info.st_mode)) {
            item.kind = manager::target_entry_kind::directory;
        } else if (S_ISREG(info.st_mode)) {
            item.kind = manager::target_entry_kind::file;
        } else {
            item.kind = manager::target_entry_kind::other;
        }
        out.push_back(item);
        errno = 0;
    }
    const bool read_ok = errno == 0;
    const bool close_ok = closedir(directory) == 0;
    if (!read_ok || !close_ok) {
        out.clear();
        return false;
    }
    return true;
}

manager::discovery_read manager_discovery_filesystem::read_prefix(
    const std::string& path, std::size_t max_bytes, std::string& out) {
    out.clear();
    const int file = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (file < 0) {
        return (errno == ENOENT || errno == ENOTDIR) ? manager::discovery_read::missing
                                                     : manager::discovery_read::unreadable;
    }
    std::string bytes;
    char buffer[4096];
    while (bytes.size() < max_bytes) {
        const std::size_t remaining = max_bytes - bytes.size();
        const std::size_t wanted = remaining < sizeof(buffer) ? remaining : sizeof(buffer);
        const ssize_t count = ::read(file, buffer, wanted);
        if (count < 0 && errno == EINTR) {
            continue;
        }
        if (count < 0) {
            ::close(file);
            return manager::discovery_read::unreadable;
        }
        if (count == 0) {
            break;
        }
        bytes.append(buffer, static_cast<std::size_t>(count));
    }
    if (::close(file) != 0) {
        return manager::discovery_read::unreadable;
    }
    out.swap(bytes);
    return manager::discovery_read::ok;
}

bool manager_discovery_filesystem::canonical_file(const std::string& path, std::string& out) {
    out.clear();
    char resolved[PATH_MAX];
    if (realpath(path.c_str(), resolved) == 0) {
        return false;
    }
    struct stat info;
    if (stat(resolved, &info) != 0 || !S_ISREG(info.st_mode)) {
        return false;
    }
    out = resolved;
    return true;
}

std::vector<manager::steam_root_candidate> platform_steam_root_candidates() {
    return manager::linux_steam_root_candidates(home_directory());
}

} // namespace platform
} // namespace eosr
