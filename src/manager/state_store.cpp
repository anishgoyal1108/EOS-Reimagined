#include "manager/state_store.h"

#include "manager/sha256.h"

namespace eosr {
namespace manager {

namespace {

const std::size_t max_state_bytes = 1024 * 1024;

bool hash_file(const std::string& path, transaction_filesystem& filesystem, std::string& out) {
    out.clear();
    transaction_handle handle = 0;
    if (filesystem.open_read(path, handle) != transaction_io_result::ok) return false;
    sha256_hasher hash;
    unsigned char buffer[8192];
    bool ok = true;
    while (ok) {
        std::size_t count = 0;
        const transaction_io_result read = filesystem.read(handle, buffer, sizeof(buffer), count);
        if (read == transaction_io_result::end_of_file) break;
        if (read != transaction_io_result::ok || count == 0) {
            ok = false;
            break;
        }
        hash.update(buffer, count);
    }
    if (filesystem.close(handle) != transaction_io_result::ok) ok = false;
    if (ok) out = hash.final_hex();
    return ok;
}

state_load_code read_bounded(const std::string& path, transaction_filesystem& filesystem,
                             std::string& bytes, std::string& hash, std::string& detail) {
    bytes.clear();
    hash.clear();
    transaction_handle handle = 0;
    const transaction_io_result opened = filesystem.open_read(path, handle);
    if (opened == transaction_io_result::missing) return state_load_code::missing;
    if (opened != transaction_io_result::ok) {
        detail = "state document could not be opened safely";
        return state_load_code::unreadable;
    }
    sha256_hasher hasher;
    unsigned char buffer[8192];
    state_load_code code = state_load_code::loaded;
    while (true) {
        std::size_t count = 0;
        const transaction_io_result read = filesystem.read(handle, buffer, sizeof(buffer), count);
        if (read == transaction_io_result::end_of_file) break;
        if (read != transaction_io_result::ok || count == 0) {
            code = state_load_code::unreadable;
            detail = "state document read failed";
            break;
        }
        if (count > max_state_bytes - bytes.size()) {
            code = state_load_code::too_large;
            detail = "state document exceeds 1 MiB";
            break;
        }
        bytes.append(reinterpret_cast<const char*>(buffer), count);
        hasher.update(buffer, count);
    }
    if (filesystem.close(handle) != transaction_io_result::ok && code == state_load_code::loaded) {
        code = state_load_code::unreadable;
        detail = "state document close failed";
    }
    if (code == state_load_code::loaded) hash = hasher.final_hex();
    else bytes.clear();
    return code;
}

bool write_new(const std::string& path, const std::string& bytes,
               transaction_filesystem& filesystem) {
    transaction_handle handle = 0;
    if (filesystem.create_new(path, handle) != transaction_io_result::ok) return false;
    std::size_t offset = 0;
    bool ok = true;
    while (offset < bytes.size()) {
        std::size_t count = 0;
        if (filesystem.write(handle,
                reinterpret_cast<const unsigned char*>(bytes.data()) + offset,
                bytes.size() - offset, count) != transaction_io_result::ok || count == 0 ||
            count > bytes.size() - offset) {
            ok = false;
            break;
        }
        offset += count;
    }
    if (ok && filesystem.flush(handle) != transaction_io_result::ok) ok = false;
    if (filesystem.close(handle) != transaction_io_result::ok) ok = false;
    return ok;
}

bool lowercase_hash(const std::string& value) {
    if (value.size() != 64) return false;
    for (std::size_t i = 0; i < value.size(); i++)
        if (!((value[i] >= '0' && value[i] <= '9') ||
              (value[i] >= 'a' && value[i] <= 'f'))) return false;
    return true;
}

state_save_result save_result(state_save_code code, const std::string& detail) {
    state_save_result out;
    out.code = code;
    out.detail = detail;
    return out;
}

state_save_result save_document(const state_save_request& request, const std::string& bytes,
                                transaction_filesystem& filesystem) {
    if (!manager_absolute_path(request.path) ||
        !manager_absolute_path(request.temporary_path) || request.path == request.temporary_path ||
        (!request.expected_sha256.empty() && !lowercase_hash(request.expected_sha256))) {
        return save_result(state_save_code::invalid_request,
                           "state path, stage path, or expected hash is invalid");
    }
    transaction_file_info temporary;
    if (filesystem.inspect(request.temporary_path, temporary) != transaction_io_result::missing)
        return save_result(state_save_code::temporary_exists,
                           "state stage exists and is not silently adopted");

    transaction_file_info live;
    const transaction_io_result inspected = filesystem.inspect(request.path, live);
    const bool existing = inspected == transaction_io_result::ok;
    if (inspected != transaction_io_result::missing && inspected != transaction_io_result::ok)
        return save_result(state_save_code::externally_changed,
                           "state document could not be inspected");
    if (existing) {
        std::string current_hash;
        if (!live.regular || live.symlink || !live.writable ||
            request.expected_sha256.empty() ||
            !hash_file(request.path, filesystem, current_hash) ||
            current_hash != request.expected_sha256)
            return save_result(state_save_code::externally_changed,
                               "state document changed since it was loaded");
    } else if (!request.expected_sha256.empty()) {
        return save_result(state_save_code::externally_changed,
                           "expected state document is now missing");
    }

    const std::string expected = sha256_hex(bytes);
    if (!write_new(request.temporary_path, bytes, filesystem) ||
        filesystem.set_mode(request.temporary_path, 0600) != transaction_io_result::ok) {
        filesystem.remove(request.temporary_path); // exclusively created by this attempt
        return save_result(state_save_code::write_failed, "state staging write failed");
    }
    std::string staged_hash;
    if (!hash_file(request.temporary_path, filesystem, staged_hash) || staged_hash != expected) {
        filesystem.remove(request.temporary_path);
        return save_result(state_save_code::write_failed, "state staging verification failed");
    }
    if (existing) {
        std::string current_hash;
        if (!hash_file(request.path, filesystem, current_hash) ||
            current_hash != request.expected_sha256) {
            filesystem.remove(request.temporary_path);
            return save_result(state_save_code::externally_changed,
                               "state document changed before publication");
        }
    }
    const transaction_io_result renamed = existing ?
        filesystem.rename_replace(request.temporary_path, request.path) :
        filesystem.rename_no_replace(request.temporary_path, request.path);
    if (renamed != transaction_io_result::ok) {
        filesystem.remove(request.temporary_path);
        return save_result(state_save_code::replace_failed, "state publication failed");
    }
    state_save_result out;
    out.saved_sha256 = expected;
    std::string live_hash;
    const bool durable = filesystem.flush_parent(request.path) == transaction_io_result::ok;
    const bool verified = hash_file(request.path, filesystem, live_hash) && live_hash == expected;
    if (!verified) {
        out.code = state_save_code::commit_uncertain;
        out.detail = "published state could not be verified; reload before another write";
    } else if (!durable) {
        out.code = state_save_code::commit_uncertain;
        out.detail = "published state is verified but directory durability was not confirmed";
    } else {
        out.code = state_save_code::saved;
    }
    return out;
}

} // namespace

manager_index_load_result::manager_index_load_result() : code(state_load_code::unreadable) {}
game_state_load_result::game_state_load_result() : code(state_load_code::unreadable) {}
state_save_result::state_save_result() : code(state_save_code::invalid_request) {}

manager_index_load_result load_manager_index(const std::string& path,
                                             transaction_filesystem& filesystem) {
    manager_index_load_result out;
    std::string bytes;
    out.code = read_bounded(path, filesystem, bytes, out.sha256, out.detail);
    if (out.code == state_load_code::loaded &&
        !parse_manager_index(bytes, out.state, out.detail)) {
        out.code = state_load_code::invalid;
        out.sha256.clear();
    }
    return out;
}

game_state_load_result load_game_state(const std::string& path,
                                       transaction_filesystem& filesystem) {
    game_state_load_result out;
    std::string bytes;
    out.code = read_bounded(path, filesystem, bytes, out.sha256, out.detail);
    if (out.code == state_load_code::loaded && !parse_game_state(bytes, out.state, out.detail)) {
        out.code = state_load_code::invalid;
        out.sha256.clear();
    }
    return out;
}

state_save_result save_manager_index(const state_save_request& request,
                                     const manager_index& state,
                                     transaction_filesystem& filesystem) {
    const std::string bytes = serialize_manager_index(state);
    manager_index parsed;
    std::string error;
    if (!parse_manager_index(bytes, parsed, error))
        return save_result(state_save_code::invalid_state, error);
    return save_document(request, bytes, filesystem);
}

state_save_result save_game_state(const state_save_request& request, const game_state& state,
                                  transaction_filesystem& filesystem) {
    const std::string bytes = serialize_game_state(state);
    game_state parsed;
    std::string error;
    if (!parse_game_state(bytes, parsed, error))
        return save_result(state_save_code::invalid_state, error);
    return save_document(request, bytes, filesystem);
}

} // namespace manager
} // namespace eosr
