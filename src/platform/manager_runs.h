#ifndef EOSR_PLATFORM_MANAGER_RUNS_H
#define EOSR_PLATFORM_MANAGER_RUNS_H

#include "manager/run_service.h"
#include "platform/manager_transactions.h"

namespace eosr {
namespace platform {

// Native, no-follow adapter for the portable diagnostic-run and support-bundle service.
class manager_run_filesystem : public manager::run_filesystem {
public:
    manager::run_io_result list_directory(const std::string& path,
                                           std::vector<manager::run_entry>& out);
    manager::run_io_result read_file(const std::string& path, std::size_t cap,
                                     std::string& out);
    manager::run_io_result open_read(const std::string& path, manager::run_handle& out);
    manager::run_io_result create_private_directory(const std::string& path);
    manager::run_io_result create_new(const std::string& path, manager::run_handle& out);
    manager::run_io_result read(manager::run_handle handle, unsigned char* data,
                                std::size_t capacity, std::size_t& count);
    manager::run_io_result write(manager::run_handle handle, const unsigned char* data,
                                 std::size_t size, std::size_t& count);
    manager::run_io_result flush(manager::run_handle handle);
    manager::run_io_result close(manager::run_handle handle);
    manager::run_io_result remove_file(const std::string& path);
    manager::run_io_result remove_empty_directory(const std::string& path);
    manager::run_io_result flush_parent(const std::string& path);

private:
    manager_transaction_filesystem files_;
};

} // namespace platform
} // namespace eosr

#endif
