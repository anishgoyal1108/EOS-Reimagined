#include "platform/manager_transactions.h"

#include <cerrno>
#include <cstdlib>
#include <fstream>
#include <sstream>

#include <dirent.h>
#include <fcntl.h>
#include <limits.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>

namespace eosr {
namespace platform {

namespace {

manager::transaction_io_result error_result(int error) {
    if (error == ENOENT || error == ENOTDIR) {
        return manager::transaction_io_result::missing;
    }
    if (error == EEXIST) {
        return manager::transaction_io_result::exists;
    }
    if (error == EACCES || error == EPERM || error == EROFS) {
        return manager::transaction_io_result::denied;
    }
    return manager::transaction_io_result::io_error;
}

std::string parent_path(const std::string& path) {
    const std::string::size_type separator = path.find_last_of('/');
    if (separator == std::string::npos) {
        return std::string(".");
    }
    return separator == 0 ? std::string("/") : path.substr(0, separator);
}

bool numeric_name(const char* name) {
    if (name == 0 || name[0] == '\0') {
        return false;
    }
    for (std::size_t i = 0; name[i] != '\0'; i++) {
        if (name[i] < '0' || name[i] > '9') {
            return false;
        }
    }
    return true;
}

bool process_maps_path(const std::string& maps_path, const std::string& canonical) {
    std::ifstream stream(maps_path.c_str());
    if (!stream) {
        return false;
    }
    std::string line;
    std::size_t lines = 0;
    while (lines++ < 100000 && std::getline(stream, line)) {
        const std::string::size_type marker = line.find('/');
        if (marker != std::string::npos && line.substr(marker) == canonical) {
            return true;
        }
        // Linux appends this marker when a mapped file was unlinked after loading.
        if (marker != std::string::npos &&
            line.substr(marker) == canonical + " (deleted)") {
            return true;
        }
    }
    return false;
}

} // namespace

manager_transaction_filesystem::manager_transaction_filesystem() : next_handle_(1) {}

manager_transaction_filesystem::~manager_transaction_filesystem() {
    for (std::map<manager::transaction_handle, i64>::const_iterator it = handles_.begin();
         it != handles_.end(); ++it) {
        ::close(static_cast<int>(it->second));
    }
}

manager::transaction_io_result manager_transaction_filesystem::inspect(
    const std::string& path, manager::transaction_file_info& out) {
    out = manager::transaction_file_info();
    struct stat info;
    if (lstat(path.c_str(), &info) != 0) {
        return error_result(errno);
    }
    out.exists = true;
    out.regular = S_ISREG(info.st_mode);
    out.symlink = S_ISLNK(info.st_mode);
    out.writable = access(path.c_str(), W_OK) == 0;
    out.mode = static_cast<unsigned int>(info.st_mode & 07777);
    return manager::transaction_io_result::ok;
}

manager::transaction_io_result manager_transaction_filesystem::canonical_file(
    const std::string& path, std::string& out) {
    out.clear();
    char resolved[PATH_MAX];
    if (realpath(path.c_str(), resolved) == 0) {
        return error_result(errno);
    }
    struct stat info;
    if (stat(resolved, &info) != 0 || !S_ISREG(info.st_mode)) {
        return manager::transaction_io_result::io_error;
    }
    out = resolved;
    return manager::transaction_io_result::ok;
}

manager::transaction_io_result manager_transaction_filesystem::in_use(
    const std::string& path, bool& out) {
    out = false;
    std::string canonical;
    if (canonical_file(path, canonical) != manager::transaction_io_result::ok) {
        return manager::transaction_io_result::io_error;
    }
    DIR* proc = opendir("/proc");
    if (proc == 0) {
        return manager::transaction_io_result::io_error;
    }
    std::size_t inspected = 0;
    struct dirent* entry = 0;
    while (inspected < 131072 && (entry = readdir(proc)) != 0) {
        if (!numeric_name(entry->d_name)) {
            continue;
        }
        inspected++;
        if (process_maps_path(std::string("/proc/") + entry->d_name + "/maps", canonical)) {
            out = true;
            break;
        }
    }
    closedir(proc);
    return manager::transaction_io_result::ok;
}

manager::transaction_io_result manager_transaction_filesystem::open_read(
    const std::string& path, manager::transaction_handle& out) {
    const int file = ::open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (file < 0) {
        return error_result(errno);
    }
    out = next_handle_++;
    handles_[out] = file;
    return manager::transaction_io_result::ok;
}

manager::transaction_io_result manager_transaction_filesystem::create_new(
    const std::string& path, manager::transaction_handle& out) {
    const int file = ::open(path.c_str(), O_CREAT | O_EXCL | O_WRONLY | O_CLOEXEC | O_NOFOLLOW,
                            0600);
    if (file < 0) {
        return error_result(errno);
    }
    out = next_handle_++;
    handles_[out] = file;
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
    ssize_t read_count;
    do {
        read_count = ::read(static_cast<int>(it->second), data, capacity);
    } while (read_count < 0 && errno == EINTR);
    if (read_count < 0) {
        return error_result(errno);
    }
    if (read_count == 0) {
        return manager::transaction_io_result::end_of_file;
    }
    count = static_cast<std::size_t>(read_count);
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
    ssize_t written;
    do {
        written = ::write(static_cast<int>(it->second), data, size);
    } while (written < 0 && errno == EINTR);
    if (written < 0) {
        return error_result(errno);
    }
    count = static_cast<std::size_t>(written);
    return manager::transaction_io_result::ok;
}

manager::transaction_io_result manager_transaction_filesystem::flush(
    manager::transaction_handle handle) {
    const std::map<manager::transaction_handle, i64>::const_iterator it = handles_.find(handle);
    if (it == handles_.end()) {
        return manager::transaction_io_result::io_error;
    }
    return fsync(static_cast<int>(it->second)) == 0 ? manager::transaction_io_result::ok
                                                    : error_result(errno);
}

manager::transaction_io_result manager_transaction_filesystem::close(
    manager::transaction_handle handle) {
    const std::map<manager::transaction_handle, i64>::iterator it = handles_.find(handle);
    if (it == handles_.end()) {
        return manager::transaction_io_result::io_error;
    }
    const int file = static_cast<int>(it->second);
    handles_.erase(it);
    return ::close(file) == 0 ? manager::transaction_io_result::ok : error_result(errno);
}

manager::transaction_io_result manager_transaction_filesystem::set_mode(
    const std::string& path, unsigned int mode) {
    struct stat info;
    if (lstat(path.c_str(), &info) != 0) {
        return error_result(errno);
    }
    if (!S_ISREG(info.st_mode) || S_ISLNK(info.st_mode)) {
        return manager::transaction_io_result::io_error;
    }
    return chmod(path.c_str(), static_cast<mode_t>(mode & 07777)) == 0
               ? manager::transaction_io_result::ok : error_result(errno);
}

manager::transaction_io_result manager_transaction_filesystem::rename_no_replace(
    const std::string& from, const std::string& to) {
#if defined(SYS_renameat2) && defined(RENAME_NOREPLACE)
    if (syscall(SYS_renameat2, AT_FDCWD, from.c_str(), AT_FDCWD, to.c_str(),
                RENAME_NOREPLACE) == 0) {
        return manager::transaction_io_result::ok;
    }
    if (errno != ENOSYS && errno != EINVAL) {
        return error_result(errno);
    }
#endif
    // link()+unlink() is an atomic no-replace publication for the regular files this adapter owns.
    if (link(from.c_str(), to.c_str()) != 0) {
        return error_result(errno);
    }
    if (unlink(from.c_str()) != 0) {
        const int saved = errno;
        unlink(to.c_str());
        return error_result(saved);
    }
    return manager::transaction_io_result::ok;
}

manager::transaction_io_result manager_transaction_filesystem::rename_replace(
    const std::string& from, const std::string& to) {
    return ::rename(from.c_str(), to.c_str()) == 0 ? manager::transaction_io_result::ok
                                                   : error_result(errno);
}

manager::transaction_io_result manager_transaction_filesystem::remove(const std::string& path) {
    if (unlink(path.c_str()) == 0) {
        return manager::transaction_io_result::ok;
    }
    return error_result(errno);
}

manager::transaction_io_result manager_transaction_filesystem::flush_parent(
    const std::string& path) {
    const std::string parent = parent_path(path);
    const int directory = ::open(parent.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (directory < 0) {
        return error_result(errno);
    }
    const int flushed = fsync(directory);
    const int saved = errno;
    const int closed = ::close(directory);
    if (flushed != 0) {
        return error_result(saved);
    }
    return closed == 0 ? manager::transaction_io_result::ok : error_result(errno);
}

} // namespace platform
} // namespace eosr
