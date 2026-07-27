#ifndef EOSR_MANAGER_INSTALL_TRANSACTION_H
#define EOSR_MANAGER_INSTALL_TRANSACTION_H

#include <cstddef>
#include <string>
#include <vector>

#include "common/types.h"
#include "manager/target_inspection.h"

namespace eosr {
namespace manager {

typedef u64 transaction_handle;

enum class transaction_io_result {
    ok,
    missing,
    exists,
    denied,
    end_of_file,
    io_error
};

struct transaction_file_info {
    transaction_file_info();
    bool exists;
    bool regular;
    bool symlink;
    bool writable;
    unsigned int mode;
};

class transaction_filesystem {
public:
    virtual ~transaction_filesystem() {}
    virtual transaction_io_result inspect(const std::string& path,
                                          transaction_file_info& out) = 0;
    virtual transaction_io_result canonical_file(const std::string& path, std::string& out) = 0;
    virtual transaction_io_result in_use(const std::string& path, bool& out) = 0;
    virtual transaction_io_result open_read(const std::string& path, transaction_handle& out) = 0;
    virtual transaction_io_result create_new(const std::string& path, transaction_handle& out) = 0;
    virtual transaction_io_result read(transaction_handle handle, unsigned char* data,
                                       std::size_t capacity, std::size_t& count) = 0;
    virtual transaction_io_result write(transaction_handle handle, const unsigned char* data,
                                        std::size_t size, std::size_t& count) = 0;
    virtual transaction_io_result flush(transaction_handle handle) = 0;
    virtual transaction_io_result close(transaction_handle handle) = 0;
    virtual transaction_io_result set_mode(const std::string& path, unsigned int mode) = 0;
    virtual transaction_io_result rename_no_replace(const std::string& from,
                                                     const std::string& to) = 0;
    virtual transaction_io_result rename_replace(const std::string& from,
                                                  const std::string& to) = 0;
    virtual transaction_io_result remove(const std::string& path) = 0;
    virtual transaction_io_result flush_parent(const std::string& path) = 0;
};

struct install_request {
    std::string transaction_id;
    std::string release_id;
    std::string target_path;
    release_artifact artifact;
    std::string backup_path;
    std::string journal_path;
    std::string descriptor_path;
    std::string record_path;
    std::string data_dir;
};

struct update_request {
    std::string transaction_id;
    std::string release_id;
    std::string record_path;
    release_artifact artifact;
    std::string data_dir;
};

enum class install_result_code {
    installed,
    restored,
    invalid_request,
    target_missing,
    target_unsafe,
    target_unwritable,
    target_in_use,
    artifact_invalid,
    sidecar_exists,
    backup_failed,
    journal_failed,
    stage_failed,
    target_changed,
    replace_failed,
    descriptor_failed,
    record_failed,
    state_invalid,
    externally_changed,
    backup_invalid,
    recovery_required,
    cleanup_incomplete
};

struct install_result {
    install_result();
    install_result_code code;
    std::string detail;
    bool target_is_reimagined;
};

enum class installation_state {
    original,
    installed_current,
    installed_other_build,
    externally_changed,
    recovery_required,
    original_unknown,
    missing,
    ambiguous,
    unwritable
};

struct installation_probe {
    std::string target_path;
    std::string record_path;
    std::string journal_path;
    release_artifact selected_artifact;
    std::vector<std::string> known_reimagined_sha256;
    bool ambiguous;
};

struct installation_health {
    installation_health();
    installation_state state;
    std::string live_sha256;
    std::string detail;
};

install_result install_release(const install_request& request, transaction_filesystem& filesystem);
install_result update_installation(const update_request& request,
                                   transaction_filesystem& filesystem);
install_result recover_installation(const std::string& journal_path,
                                    transaction_filesystem& filesystem);
install_result restore_installation(const std::string& record_path,
                                    transaction_filesystem& filesystem);
installation_health inspect_installation(const installation_probe& probe,
                                         transaction_filesystem& filesystem);

} // namespace manager
} // namespace eosr

#endif
