#ifndef EOSR_PLATFORM_MANAGER_TRANSACTIONS_H
#define EOSR_PLATFORM_MANAGER_TRANSACTIONS_H

#include <map>
#include <string>

#include "manager/install_transaction.h"

namespace eosr {
namespace platform {

class manager_transaction_filesystem : public manager::transaction_filesystem {
public:
    manager_transaction_filesystem();
    ~manager_transaction_filesystem();

    manager::transaction_io_result inspect(const std::string& path,
                                            manager::transaction_file_info& out);
    manager::transaction_io_result canonical_file(const std::string& path, std::string& out);
    manager::transaction_io_result in_use(const std::string& path, bool& out);
    manager::transaction_io_result open_read(const std::string& path,
                                             manager::transaction_handle& out);
    manager::transaction_io_result create_new(const std::string& path,
                                               manager::transaction_handle& out);
    manager::transaction_io_result read(manager::transaction_handle handle, unsigned char* data,
                                        std::size_t capacity, std::size_t& count);
    manager::transaction_io_result write(manager::transaction_handle handle,
                                         const unsigned char* data, std::size_t size,
                                         std::size_t& count);
    manager::transaction_io_result flush(manager::transaction_handle handle);
    manager::transaction_io_result close(manager::transaction_handle handle);
    manager::transaction_io_result set_mode(const std::string& path, unsigned int mode);
    manager::transaction_io_result rename_no_replace(const std::string& from,
                                                      const std::string& to);
    manager::transaction_io_result rename_replace(const std::string& from,
                                                   const std::string& to);
    manager::transaction_io_result remove(const std::string& path);
    manager::transaction_io_result flush_parent(const std::string& path);

private:
    manager_transaction_filesystem(const manager_transaction_filesystem&);
    manager_transaction_filesystem& operator=(const manager_transaction_filesystem&);

    std::map<manager::transaction_handle, i64> handles_;
    manager::transaction_handle next_handle_;
};

} // namespace platform
} // namespace eosr

#endif
