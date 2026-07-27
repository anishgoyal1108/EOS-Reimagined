#include "manager/operation_log.h"

#include "manager/json.h"
#include "manager/manager_state.h"

namespace eosr {
namespace manager {

namespace {

bool token(const std::string& value, std::size_t cap) {
    if (value.empty() || value.size() > cap || value.find('\0') != std::string::npos) return false;
    for (std::size_t i = 0; i < value.size(); i++) {
        const char c = value[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '@' || c == '+' ||
              c == '-')) return false;
    }
    return true;
}

bool write_all(transaction_handle handle, const std::string& bytes,
               transaction_filesystem& filesystem) {
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        std::size_t written = 0;
        if (filesystem.write(handle,
                reinterpret_cast<const unsigned char*>(bytes.data()) + offset,
                bytes.size() - offset, written) != transaction_io_result::ok || written == 0 ||
            written > bytes.size() - offset) return false;
        offset += written;
    }
    return true;
}

} // namespace

operation_log_result::operation_log_result() : code(operation_log_code::invalid_request) {}

operation_log_result write_operation_log(const std::string& path,
                                         const operation_log_entry& entry,
                                         transaction_filesystem& filesystem) {
    operation_log_result result;
    if (!manager_absolute_path(path) || entry.created_utc.size() != 20 ||
        entry.created_utc[19] != 'Z' || !token(entry.action, 64) ||
        !token(entry.object_id, 128) || !token(entry.code, 64)) {
        result.detail = "operation log metadata is invalid";
        return result;
    }
    json_value document = json_object();
    document.members["schema_version"] = json_int(1);
    document.members["created_utc"] = json_string(entry.created_utc);
    document.members["action"] = json_string(entry.action);
    document.members["object_id"] = json_string(entry.object_id);
    document.members["code"] = json_string(entry.code);
    document.members["success"] = json_bool(entry.success);
    const std::string bytes = serialize_json(document);

    transaction_handle handle = 0;
    const transaction_io_result created = filesystem.create_new(path, handle);
    if (created != transaction_io_result::ok) {
        result.code = created == transaction_io_result::exists ?
            operation_log_code::destination_exists : operation_log_code::write_failed;
        result.detail = created == transaction_io_result::exists ?
            "operation log destination already exists" : "operation log could not be created";
        return result;
    }
    bool ok = write_all(handle, bytes, filesystem);
    if (ok) ok = filesystem.flush(handle) == transaction_io_result::ok;
    if (filesystem.close(handle) != transaction_io_result::ok) ok = false;
    if (ok) ok = filesystem.set_mode(path, 0600) == transaction_io_result::ok;
    if (ok) ok = filesystem.flush_parent(path) == transaction_io_result::ok;
    if (!ok) {
        result.code = filesystem.remove(path) == transaction_io_result::ok ?
            operation_log_code::write_failed : operation_log_code::cleanup_incomplete;
        result.detail = result.code == operation_log_code::write_failed ?
            "operation log write failed and its partial file was removed" :
            "operation log write failed and partial cleanup is incomplete";
        return result;
    }
    result.code = operation_log_code::written;
    return result;
}

} // namespace manager
} // namespace eosr
