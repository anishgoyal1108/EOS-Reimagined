#ifndef EOSR_MANAGER_OPERATION_LOG_H
#define EOSR_MANAGER_OPERATION_LOG_H

#include <string>

#include "manager/install_transaction.h"

namespace eosr {
namespace manager {

struct operation_log_entry {
    std::string created_utc;
    std::string action;
    std::string object_id;
    std::string code;
    bool success;
};

enum class operation_log_code {
    written,
    invalid_request,
    destination_exists,
    write_failed,
    cleanup_incomplete
};

struct operation_log_result {
    operation_log_result();
    operation_log_code code;
    std::string detail;
};

// Writes one immutable, private JSON event. The schema intentionally has no free-form detail or
// path field, keeping credentials, raw EOS identifiers, and packet data outside manager logs.
operation_log_result write_operation_log(const std::string& path,
                                         const operation_log_entry& entry,
                                         transaction_filesystem& filesystem);

} // namespace manager
} // namespace eosr

#endif
