#include "platform/manager_runs.h"

#include <cerrno>
#include <limits>

#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

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

manager::run_io_result errno_result(int error) {
    if (error == ENOENT || error == ENOTDIR) return manager::run_io_result::missing;
    if (error == EEXIST) return manager::run_io_result::exists;
    if (error == EACCES || error == EPERM || error == EROFS)
        return manager::run_io_result::denied;
    return manager::run_io_result::io_error;
}

std::string join_path(const std::string& left, const std::string& right) {
    return !left.empty() && left[left.size() - 1] == '/' ? left + right : left + "/" + right;
}

u64 modified_time(const struct stat& info) {
    if (info.st_mtim.tv_sec < 0 || info.st_mtim.tv_nsec < 0) return 0;
    const u64 seconds = static_cast<u64>(info.st_mtim.tv_sec);
    if (seconds > (std::numeric_limits<u64>::max() -
                   static_cast<u64>(info.st_mtim.tv_nsec)) / 1000000000ULL) return 0;
    return seconds * 1000000000ULL + static_cast<u64>(info.st_mtim.tv_nsec);
}

} // namespace

manager::run_io_result manager_run_filesystem::list_directory(
    const std::string& path, std::vector<manager::run_entry>& out) {
    out.clear();
    DIR* directory = opendir(path.c_str());
    if (directory == 0) return errno_result(errno);
    errno = 0;
    struct dirent* native = 0;
    while (out.size() <= 4096 && (native = readdir(directory)) != 0) {
        const std::string name = native->d_name;
        if (name == "." || name == "..") continue;
        struct stat info;
        manager::run_entry entry;
        entry.name = name;
        if (lstat(join_path(path, name).c_str(), &info) != 0) {
            entry.kind = manager::run_entry_kind::other;
        } else {
            if (S_ISLNK(info.st_mode)) entry.kind = manager::run_entry_kind::symlink;
            else if (S_ISREG(info.st_mode)) entry.kind = manager::run_entry_kind::file;
            else if (S_ISDIR(info.st_mode)) entry.kind = manager::run_entry_kind::directory;
            else entry.kind = manager::run_entry_kind::other;
            if (S_ISREG(info.st_mode) && info.st_size >= 0)
                entry.size = static_cast<u64>(info.st_size);
            entry.modified = modified_time(info);
        }
        out.push_back(entry);
        errno = 0;
    }
    const int read_error = errno;
    const bool too_many = out.size() > 4096;
    const int close_error = closedir(directory);
    if (read_error != 0) return errno_result(read_error);
    if (close_error != 0) return errno_result(errno);
    return too_many ? manager::run_io_result::too_large : manager::run_io_result::ok;
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
    return mkdir(path.c_str(), 0700) == 0 ? manager::run_io_result::ok : errno_result(errno);
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
    return rmdir(path.c_str()) == 0 ? manager::run_io_result::ok : errno_result(errno);
}

manager::run_io_result manager_run_filesystem::flush_parent(const std::string& path) {
    return map_result(files_.flush_parent(path));
}

} // namespace platform
} // namespace eosr
