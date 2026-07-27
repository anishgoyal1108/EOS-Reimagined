#include "manager/install_transaction.h"

#include <set>
#include <vector>

#include "manager/json.h"
#include "manager/manager_state.h"
#include "manager/sha256.h"

namespace eosr {
namespace manager {

namespace {

const int transaction_version = 1;
const std::size_t max_transaction_file_bytes = 64 * 1024;
const std::size_t io_buffer_size = 64 * 1024;

struct installation_record {
    std::string transaction_id;
    std::string release_id;
    std::string target_path;
    std::string artifact_path;
    std::string backup_path;
    std::string journal_path;
    std::string descriptor_path;
    std::string record_path;
    std::string data_dir;
    std::string original_sha256;
    std::string staged_sha256;
    std::string descriptor_sha256;
    std::string journal_sha256;
    std::size_t staged_bytes;
    unsigned int original_mode;
    eos_binary_kind kind;
    std::vector<std::pair<std::string, std::string> > obsolete_journals;
};

struct update_intent {
    installation_record next;
    std::string previous_record_sha256;
    std::string previous_staged_sha256;
    std::string previous_descriptor_sha256;
    std::string previous_journal_path;
    std::string previous_journal_sha256;
};

install_result result(install_result_code code, const std::string& detail,
                      bool target_is_reimagined = false) {
    install_result out;
    out.code = code;
    out.detail = detail;
    out.target_is_reimagined = target_is_reimagined;
    return out;
}

bool hash_valid(const std::string& hash) {
    if (hash.size() != 64) {
        return false;
    }
    for (std::size_t i = 0; i < hash.size(); i++) {
        if (!((hash[i] >= '0' && hash[i] <= '9') || (hash[i] >= 'a' && hash[i] <= 'f'))) {
            return false;
        }
    }
    return true;
}

const char* kind_name(eos_binary_kind kind) {
    return kind == eos_binary_kind::windows_x86_64 ? "windows_x86_64" : "linux_x86_64";
}

bool parse_kind(const std::string& value, eos_binary_kind& out) {
    if (value == "windows_x86_64") {
        out = eos_binary_kind::windows_x86_64;
        return true;
    }
    if (value == "linux_x86_64") {
        out = eos_binary_kind::linux_x86_64;
        return true;
    }
    return false;
}

bool release_id_valid(const std::string& value) {
    if (value.empty() || value.size() > 128) {
        return false;
    }
    for (std::size_t i = 0; i < value.size(); i++) {
        const char c = value[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '+' || c == '-' ||
              c == '@')) {
            return false;
        }
    }
    return true;
}

bool read_all(const std::string& path, std::size_t cap, transaction_filesystem& filesystem,
              std::string& bytes) {
    bytes.clear();
    transaction_handle handle = 0;
    if (filesystem.open_read(path, handle) != transaction_io_result::ok) {
        return false;
    }
    unsigned char buffer[4096];
    bool ok = true;
    while (ok) {
        std::size_t count = 0;
        const transaction_io_result read = filesystem.read(handle, buffer, sizeof(buffer), count);
        if (read == transaction_io_result::end_of_file) {
            break;
        }
        if (read != transaction_io_result::ok || count == 0 || bytes.size() > cap - count) {
            ok = false;
            break;
        }
        bytes.append(reinterpret_cast<const char*>(buffer), count);
    }
    if (filesystem.close(handle) != transaction_io_result::ok) {
        ok = false;
    }
    if (!ok) {
        bytes.clear();
    }
    return ok;
}

bool hash_file(const std::string& path, transaction_filesystem& filesystem,
               std::string& hash, std::size_t* bytes, std::string* prefix,
               bool* eosr_signature = 0) {
    hash.clear();
    if (bytes != 0) {
        *bytes = 0;
    }
    if (prefix != 0) {
        prefix->clear();
    }
    if (eosr_signature != 0) {
        *eosr_signature = false;
    }
    transaction_handle handle = 0;
    if (filesystem.open_read(path, handle) != transaction_io_result::ok) {
        return false;
    }
    sha256_hasher hasher;
    unsigned char buffer[io_buffer_size];
    std::size_t total = 0;
    bool ok = true;
    std::string signature_tail;
    const std::string signature = "EOSR_DATA_DIR";
    while (ok) {
        std::size_t count = 0;
        const transaction_io_result read = filesystem.read(handle, buffer, sizeof(buffer), count);
        if (read == transaction_io_result::end_of_file) {
            break;
        }
        if (read != transaction_io_result::ok || count == 0 ||
            total > static_cast<std::size_t>(-1) - count) {
            ok = false;
            break;
        }
        if (prefix != 0 && prefix->size() < 4096) {
            const std::size_t remaining = 4096 - prefix->size();
            const std::size_t copy = count < remaining ? count : remaining;
            prefix->append(reinterpret_cast<const char*>(buffer), copy);
        }
        if (eosr_signature != 0 && !*eosr_signature) {
            std::string searchable = signature_tail;
            searchable.append(reinterpret_cast<const char*>(buffer), count);
            *eosr_signature = searchable.find(signature) != std::string::npos;
            if (!*eosr_signature) {
                const std::size_t keep = signature.size() - 1;
                signature_tail = searchable.size() > keep ?
                    searchable.substr(searchable.size() - keep) : searchable;
            }
        }
        hasher.update(buffer, count);
        total += count;
    }
    if (filesystem.close(handle) != transaction_io_result::ok) {
        ok = false;
    }
    if (!ok) {
        return false;
    }
    hash = hasher.final_hex();
    if (bytes != 0) {
        *bytes = total;
    }
    return true;
}

bool copy_to_new(const std::string& source, const std::string& destination,
                 transaction_filesystem& filesystem) {
    transaction_handle input = 0;
    if (filesystem.open_read(source, input) != transaction_io_result::ok) {
        return false;
    }
    transaction_handle output = 0;
    if (filesystem.create_new(destination, output) != transaction_io_result::ok) {
        filesystem.close(input);
        return false;
    }
    unsigned char buffer[io_buffer_size];
    bool ok = true;
    while (ok) {
        std::size_t count = 0;
        const transaction_io_result read = filesystem.read(input, buffer, sizeof(buffer), count);
        if (read == transaction_io_result::end_of_file) {
            break;
        }
        if (read != transaction_io_result::ok || count == 0) {
            ok = false;
            break;
        }
        std::size_t offset = 0;
        while (offset < count) {
            std::size_t written = 0;
            const transaction_io_result write = filesystem.write(
                output, buffer + offset, count - offset, written);
            if (write != transaction_io_result::ok || written == 0 || written > count - offset) {
                ok = false;
                break;
            }
            offset += written;
        }
    }
    if (ok && filesystem.flush(output) != transaction_io_result::ok) {
        ok = false;
    }
    if (filesystem.close(input) != transaction_io_result::ok) {
        ok = false;
    }
    if (filesystem.close(output) != transaction_io_result::ok) {
        ok = false;
    }
    return ok;
}

bool write_new(const std::string& path, const std::string& bytes,
               transaction_filesystem& filesystem) {
    transaction_handle handle = 0;
    if (filesystem.create_new(path, handle) != transaction_io_result::ok) {
        return false;
    }
    std::size_t offset = 0;
    bool ok = true;
    while (offset < bytes.size()) {
        std::size_t written = 0;
        const transaction_io_result write = filesystem.write(
            handle, reinterpret_cast<const unsigned char*>(bytes.data()) + offset,
            bytes.size() - offset, written);
        if (write != transaction_io_result::ok || written == 0 || written > bytes.size() - offset) {
            ok = false;
            break;
        }
        offset += written;
    }
    if (ok && filesystem.flush(handle) != transaction_io_result::ok) {
        ok = false;
    }
    if (filesystem.close(handle) != transaction_io_result::ok) {
        ok = false;
    }
    return ok;
}

bool atomic_new(const std::string& final_path, const std::string& temporary,
                const std::string& bytes, unsigned int mode,
                transaction_filesystem& filesystem) {
    if (!write_new(temporary, bytes, filesystem) ||
        filesystem.set_mode(temporary, mode) != transaction_io_result::ok ||
        filesystem.rename_no_replace(temporary, final_path) != transaction_io_result::ok ||
        filesystem.flush_parent(final_path) != transaction_io_result::ok) {
        return false;
    }
    return true;
}

std::string descriptor_json(const std::string& data_dir) {
    json_value descriptor = json_object();
    descriptor.members["data_dir"] = json_string(data_dir);
    descriptor.members["version"] = json_int(1);
    return serialize_json(descriptor);
}

json_value record_json(const installation_record& record, bool include_journal_hash) {
    json_value out = json_object();
    out.members["artifact_path"] = json_string(record.artifact_path);
    out.members["backup_path"] = json_string(record.backup_path);
    out.members["binary_kind"] = json_string(kind_name(record.kind));
    out.members["data_dir"] = json_string(record.data_dir);
    out.members["descriptor_path"] = json_string(record.descriptor_path);
    out.members["descriptor_sha256"] = json_string(record.descriptor_sha256);
    out.members["journal_path"] = json_string(record.journal_path);
    if (include_journal_hash) {
        out.members["journal_sha256"] = json_string(record.journal_sha256);
        json_value obsolete = json_array();
        for (std::size_t i = 0; i < record.obsolete_journals.size(); i++) {
            json_value item = json_object();
            item.members["path"] = json_string(record.obsolete_journals[i].first);
            item.members["sha256"] = json_string(record.obsolete_journals[i].second);
            obsolete.elements.push_back(item);
        }
        out.members["obsolete_journals"] = obsolete;
    }
    out.members["original_mode"] = json_int(record.original_mode);
    out.members["original_sha256"] = json_string(record.original_sha256);
    out.members["record_path"] = json_string(record.record_path);
    out.members["release_id"] = json_string(record.release_id);
    out.members["staged_bytes"] = json_int(static_cast<i64>(record.staged_bytes));
    out.members["staged_sha256"] = json_string(record.staged_sha256);
    out.members["target_path"] = json_string(record.target_path);
    out.members["transaction_id"] = json_string(record.transaction_id);
    out.members["version"] = json_int(transaction_version);
    return out;
}

json_value obsolete_journals_json(const installation_record& record) {
    json_value obsolete = json_array();
    for (std::size_t i = 0; i < record.obsolete_journals.size(); i++) {
        json_value item = json_object();
        item.members["path"] = json_string(record.obsolete_journals[i].first);
        item.members["sha256"] = json_string(record.obsolete_journals[i].second);
        obsolete.elements.push_back(item);
    }
    return obsolete;
}

bool required(const json_value& root, const char* name, json_kind kind,
              const json_value*& out, std::string& error) {
    out = json_member(root, name);
    if (out == 0 || out->kind != kind) {
        error = std::string("transaction field missing or mistyped: ") + name;
        return false;
    }
    return true;
}

bool parse_record_json(const std::string& bytes, bool expect_journal_hash,
                       installation_record& out, std::string& error) {
    json_value root;
    if (!parse_json(bytes, root, error) || root.kind != json_kind::object) {
        return false;
    }
    const std::size_t expected_fields = expect_journal_hash ? 18 : 16;
    if (root.members.size() != expected_fields) {
        error = "transaction has missing or unknown fields";
        return false;
    }
    const json_value* version = 0;
    const json_value* transaction_id = 0;
    const json_value* release_id = 0;
    const json_value* target = 0;
    const json_value* artifact = 0;
    const json_value* backup = 0;
    const json_value* journal = 0;
    const json_value* descriptor = 0;
    const json_value* record = 0;
    const json_value* data_dir = 0;
    const json_value* original_hash = 0;
    const json_value* staged_hash = 0;
    const json_value* descriptor_hash = 0;
    const json_value* staged_bytes = 0;
    const json_value* original_mode = 0;
    const json_value* kind = 0;
    const json_value* journal_hash = 0;
    const json_value* obsolete_journals = 0;
    if (!required(root, "version", json_kind::integer, version, error) ||
        !required(root, "transaction_id", json_kind::string, transaction_id, error) ||
        !required(root, "release_id", json_kind::string, release_id, error) ||
        !required(root, "target_path", json_kind::string, target, error) ||
        !required(root, "artifact_path", json_kind::string, artifact, error) ||
        !required(root, "backup_path", json_kind::string, backup, error) ||
        !required(root, "journal_path", json_kind::string, journal, error) ||
        !required(root, "descriptor_path", json_kind::string, descriptor, error) ||
        !required(root, "record_path", json_kind::string, record, error) ||
        !required(root, "data_dir", json_kind::string, data_dir, error) ||
        !required(root, "original_sha256", json_kind::string, original_hash, error) ||
        !required(root, "staged_sha256", json_kind::string, staged_hash, error) ||
        !required(root, "descriptor_sha256", json_kind::string, descriptor_hash, error) ||
        !required(root, "staged_bytes", json_kind::integer, staged_bytes, error) ||
        !required(root, "original_mode", json_kind::integer, original_mode, error) ||
        !required(root, "binary_kind", json_kind::string, kind, error) ||
        (expect_journal_hash &&
         (!required(root, "journal_sha256", json_kind::string, journal_hash, error) ||
          !required(root, "obsolete_journals", json_kind::array, obsolete_journals, error)))) {
        return false;
    }
    if (version->integer != transaction_version || !valid_manager_id(transaction_id->text) ||
        !release_id_valid(release_id->text) || !manager_absolute_path(target->text) ||
        !manager_absolute_path(artifact->text) || !manager_absolute_path(backup->text) ||
        !manager_absolute_path(journal->text) || !manager_absolute_path(descriptor->text) ||
        !manager_absolute_path(record->text) || !manager_absolute_path(data_dir->text) ||
        !hash_valid(original_hash->text) || !hash_valid(staged_hash->text) ||
        !hash_valid(descriptor_hash->text) || (expect_journal_hash &&
        !hash_valid(journal_hash->text)) || staged_bytes->integer < 0 ||
        original_mode->integer < 0 || original_mode->integer > 07777 ||
        !parse_kind(kind->text, out.kind)) {
        error = "invalid transaction field value";
        return false;
    }
    out.transaction_id = transaction_id->text;
    out.release_id = release_id->text;
    out.target_path = target->text;
    out.artifact_path = artifact->text;
    out.backup_path = backup->text;
    out.journal_path = journal->text;
    out.descriptor_path = descriptor->text;
    out.record_path = record->text;
    out.data_dir = data_dir->text;
    out.original_sha256 = original_hash->text;
    out.staged_sha256 = staged_hash->text;
    out.descriptor_sha256 = descriptor_hash->text;
    out.journal_sha256 = expect_journal_hash ? journal_hash->text : sha256_hex(bytes);
    out.staged_bytes = static_cast<std::size_t>(staged_bytes->integer);
    out.original_mode = static_cast<unsigned int>(original_mode->integer);
    if (expect_journal_hash) {
        std::set<std::string> obsolete_paths;
        for (std::size_t i = 0; i < obsolete_journals->elements.size(); i++) {
            const json_value& item = obsolete_journals->elements[i];
            const json_value* path = 0;
            const json_value* hash = 0;
            if (item.kind != json_kind::object || item.members.size() != 2 ||
                !required(item, "path", json_kind::string, path, error) ||
                !required(item, "sha256", json_kind::string, hash, error) ||
                !manager_absolute_path(path->text) || !hash_valid(hash->text) ||
                !obsolete_paths.insert(path->text).second || path->text == out.journal_path) {
                error = "invalid obsolete journal ownership record";
                return false;
            }
            out.obsolete_journals.push_back(std::make_pair(path->text, hash->text));
        }
    }
    return true;
}

std::string update_intent_json(const update_intent& intent) {
    json_value root = record_json(intent.next, false);
    root.members["operation"] = json_string("update");
    root.members["obsolete_journals"] = obsolete_journals_json(intent.next);
    root.members["previous_descriptor_sha256"] = json_string(
        intent.previous_descriptor_sha256);
    root.members["previous_journal_path"] = json_string(intent.previous_journal_path);
    root.members["previous_journal_sha256"] = json_string(intent.previous_journal_sha256);
    root.members["previous_record_sha256"] = json_string(intent.previous_record_sha256);
    root.members["previous_staged_sha256"] = json_string(intent.previous_staged_sha256);
    return serialize_json(root);
}

bool parse_obsolete_journals(const json_value& value, installation_record& record,
                             std::string& error) {
    if (value.kind != json_kind::array) {
        error = "update obsolete journals must be an array";
        return false;
    }
    std::set<std::string> paths;
    for (std::size_t i = 0; i < value.elements.size(); i++) {
        const json_value& item = value.elements[i];
        const json_value* path = 0;
        const json_value* hash = 0;
        if (item.kind != json_kind::object || item.members.size() != 2 ||
            !required(item, "path", json_kind::string, path, error) ||
            !required(item, "sha256", json_kind::string, hash, error) ||
            !manager_absolute_path(path->text) || !hash_valid(hash->text) ||
            !paths.insert(path->text).second || path->text == record.journal_path) {
            error = "invalid update obsolete journal";
            return false;
        }
        record.obsolete_journals.push_back(std::make_pair(path->text, hash->text));
    }
    return true;
}

bool parse_update_intent(const std::string& bytes, update_intent& out, std::string& error) {
    json_value root;
    if (!parse_json(bytes, root, error) || root.kind != json_kind::object ||
        root.members.size() != 23) {
        if (error.empty()) {
            error = "update journal has missing or unknown fields";
        }
        return false;
    }
    const json_value* operation = 0;
    const json_value* previous_record = 0;
    const json_value* previous_staged = 0;
    const json_value* previous_descriptor = 0;
    const json_value* previous_journal_path = 0;
    const json_value* previous_journal_hash = 0;
    const json_value* obsolete = 0;
    if (!required(root, "operation", json_kind::string, operation, error) ||
        !required(root, "previous_record_sha256", json_kind::string, previous_record, error) ||
        !required(root, "previous_staged_sha256", json_kind::string, previous_staged, error) ||
        !required(root, "previous_descriptor_sha256", json_kind::string,
                  previous_descriptor, error) ||
        !required(root, "previous_journal_path", json_kind::string,
                  previous_journal_path, error) ||
        !required(root, "previous_journal_sha256", json_kind::string,
                  previous_journal_hash, error) ||
        !required(root, "obsolete_journals", json_kind::array, obsolete, error) ||
        operation->text != "update" || !hash_valid(previous_record->text) ||
        !hash_valid(previous_staged->text) || !hash_valid(previous_descriptor->text) ||
        !manager_absolute_path(previous_journal_path->text) ||
        !hash_valid(previous_journal_hash->text)) {
        if (error.empty()) {
            error = "invalid update journal field";
        }
        return false;
    }
    const std::string previous_record_value = previous_record->text;
    const std::string previous_staged_value = previous_staged->text;
    const std::string previous_descriptor_value = previous_descriptor->text;
    const std::string previous_journal_path_value = previous_journal_path->text;
    const std::string previous_journal_hash_value = previous_journal_hash->text;
    const json_value obsolete_value = *obsolete;
    root.members.erase("operation");
    root.members.erase("previous_record_sha256");
    root.members.erase("previous_staged_sha256");
    root.members.erase("previous_descriptor_sha256");
    root.members.erase("previous_journal_path");
    root.members.erase("previous_journal_sha256");
    root.members.erase("obsolete_journals");
    installation_record next;
    if (!parse_record_json(serialize_json(root), false, next, error) ||
        !parse_obsolete_journals(obsolete_value, next, error)) {
        return false;
    }
    out.next = next;
    out.previous_record_sha256 = previous_record_value;
    out.previous_staged_sha256 = previous_staged_value;
    out.previous_descriptor_sha256 = previous_descriptor_value;
    out.previous_journal_path = previous_journal_path_value;
    out.previous_journal_sha256 = previous_journal_hash_value;
    return true;
}

bool path_missing(const std::string& path, transaction_filesystem& filesystem) {
    transaction_file_info info;
    return filesystem.inspect(path, info) == transaction_io_result::missing;
}

bool validate_request(const install_request& request) {
    if (!valid_manager_id(request.transaction_id) || !release_id_valid(request.release_id) ||
        !manager_absolute_path(request.target_path) ||
        !manager_absolute_path(request.artifact.path) ||
        !manager_absolute_path(request.backup_path) ||
        !manager_absolute_path(request.journal_path) ||
        !manager_absolute_path(request.descriptor_path) ||
        !manager_absolute_path(request.record_path) || !manager_absolute_path(request.data_dir) ||
        request.artifact.kind == eos_binary_kind::unknown || request.artifact.bytes == 0 ||
        !hash_valid(request.artifact.sha256)) {
        return false;
    }
    std::set<std::string> paths;
    paths.insert(request.target_path);
    paths.insert(request.artifact.path);
    paths.insert(request.backup_path);
    paths.insert(request.journal_path);
    paths.insert(request.descriptor_path);
    paths.insert(request.record_path);
    return paths.size() == 6;
}

bool verify_hash(const std::string& path, const std::string& expected,
                 transaction_filesystem& filesystem) {
    std::string actual;
    return hash_file(path, filesystem, actual, 0, 0) && actual == expected;
}

bool remove_owned_or_missing(const std::string& path, const std::string& expected_hash,
                             transaction_filesystem& filesystem) {
    transaction_file_info info;
    const transaction_io_result status = filesystem.inspect(path, info);
    if (status == transaction_io_result::missing) {
        return true;
    }
    return status == transaction_io_result::ok && info.regular && !info.symlink &&
           verify_hash(path, expected_hash, filesystem) &&
           filesystem.remove(path) == transaction_io_result::ok;
}

install_result finish_from_record(installation_record record, const std::string& journal_bytes,
                                  transaction_filesystem& filesystem) {
    if (!verify_hash(record.backup_path, record.original_sha256, filesystem)) {
        return result(install_result_code::backup_invalid, "verified original backup is unavailable");
    }
    std::string artifact_hash;
    std::size_t artifact_bytes = 0;
    std::string prefix;
    if (!hash_file(record.artifact_path, filesystem, artifact_hash, &artifact_bytes, &prefix) ||
        artifact_hash != record.staged_sha256 || artifact_bytes != record.staged_bytes ||
        classify_eos_binary(prefix) != record.kind) {
        return result(install_result_code::artifact_invalid, "release artifact no longer verifies");
    }

    std::string live_hash;
    if (!hash_file(record.target_path, filesystem, live_hash, 0, 0)) {
        return result(install_result_code::recovery_required, "target cannot be read");
    }
    const std::string stage = record.target_path + ".eosr-stage-" + record.transaction_id;
    if (live_hash == record.original_sha256) {
        if (!path_missing(stage, filesystem)) {
            if (!verify_hash(stage, record.staged_sha256, filesystem) ||
                filesystem.remove(stage) != transaction_io_result::ok) {
                return result(install_result_code::recovery_required,
                              "existing transaction stage is not safely reusable");
            }
        }
        if (!copy_to_new(record.artifact_path, stage, filesystem) ||
            filesystem.set_mode(stage, record.original_mode) != transaction_io_result::ok ||
            !verify_hash(stage, record.staged_sha256, filesystem) ||
            !verify_hash(record.target_path, record.original_sha256, filesystem)) {
            return result(install_result_code::stage_failed, "could not stage verified artifact");
        }
        if (filesystem.rename_replace(stage, record.target_path) != transaction_io_result::ok ||
            filesystem.flush_parent(record.target_path) != transaction_io_result::ok ||
            !verify_hash(record.target_path, record.staged_sha256, filesystem)) {
            return result(install_result_code::replace_failed,
                          "target replacement did not reach a verified state");
        }
        live_hash = record.staged_sha256;
    }
    if (live_hash != record.staged_sha256) {
        return result(install_result_code::externally_changed,
                      "live target matches neither original nor staged artifact");
    }

    const std::string descriptor = descriptor_json(record.data_dir);
    const std::string descriptor_temporary = record.descriptor_path + ".eosr-stage-" +
                                             record.transaction_id;
    transaction_file_info descriptor_info;
    const transaction_io_result descriptor_status = filesystem.inspect(
        record.descriptor_path, descriptor_info);
    if (descriptor_status == transaction_io_result::missing) {
        if (!path_missing(descriptor_temporary, filesystem)) {
            return result(install_result_code::recovery_required,
                          "descriptor staging path already exists", true);
        }
        if (!atomic_new(record.descriptor_path, descriptor_temporary, descriptor, 0600,
                        filesystem)) {
            return result(install_result_code::descriptor_failed,
                          "bootstrap descriptor could not be committed", true);
        }
    } else if (descriptor_status != transaction_io_result::ok ||
               !verify_hash(record.descriptor_path, record.descriptor_sha256, filesystem)) {
        return result(install_result_code::externally_changed,
                      "bootstrap descriptor is externally owned or changed", true);
    }

    record.journal_sha256 = sha256_hex(journal_bytes);
    const std::string record_bytes = serialize_json(record_json(record, true));
    const std::string record_temporary = record.record_path + ".eosr-stage-" +
                                         record.transaction_id;
    transaction_file_info record_info;
    const transaction_io_result record_status = filesystem.inspect(record.record_path, record_info);
    if (record_status == transaction_io_result::missing) {
        if (!path_missing(record_temporary, filesystem) ||
            !atomic_new(record.record_path, record_temporary, record_bytes, 0600, filesystem)) {
            return result(install_result_code::record_failed,
                          "central installation record could not be committed", true);
        }
    } else {
        std::string existing;
        if (record_status != transaction_io_result::ok ||
            !read_all(record.record_path, max_transaction_file_bytes, filesystem, existing) ||
            existing != record_bytes) {
            return result(install_result_code::externally_changed,
                          "central installation record is externally owned or changed", true);
        }
    }
    return result(install_result_code::installed, std::string(), true);
}

bool replace_owned_text(const std::string& path, const std::string& temporary,
                        const std::string& bytes, const std::string& expected_current_hash,
                        unsigned int mode, transaction_filesystem& filesystem) {
    const std::string expected_new_hash = sha256_hex(bytes);
    if (!path_missing(temporary, filesystem)) {
        if (!verify_hash(temporary, expected_new_hash, filesystem) ||
            filesystem.remove(temporary) != transaction_io_result::ok) {
            return false;
        }
    }
    if (!write_new(temporary, bytes, filesystem) ||
        filesystem.set_mode(temporary, mode) != transaction_io_result::ok ||
        !verify_hash(temporary, expected_new_hash, filesystem) ||
        !verify_hash(path, expected_current_hash, filesystem) ||
        filesystem.rename_replace(temporary, path) != transaction_io_result::ok ||
        filesystem.flush_parent(path) != transaction_io_result::ok ||
        !verify_hash(path, expected_new_hash, filesystem)) {
        return false;
    }
    return true;
}

install_result finish_update(update_intent intent, const std::string& journal_bytes,
                             transaction_filesystem& filesystem) {
    installation_record& next = intent.next;
    if (!verify_hash(next.backup_path, next.original_sha256, filesystem)) {
        return result(install_result_code::backup_invalid,
                      "verified original backup is unavailable", true);
    }
    std::string artifact_hash;
    std::size_t artifact_bytes = 0;
    std::string prefix;
    if (!hash_file(next.artifact_path, filesystem, artifact_hash, &artifact_bytes, &prefix) ||
        artifact_hash != next.staged_sha256 || artifact_bytes != next.staged_bytes ||
        classify_eos_binary(prefix) != next.kind) {
        return result(install_result_code::artifact_invalid,
                      "updated release artifact no longer verifies", true);
    }
    std::string live_hash;
    if (!hash_file(next.target_path, filesystem, live_hash, 0, 0)) {
        return result(install_result_code::recovery_required, "target cannot be hashed", true);
    }
    const std::string target_stage = next.target_path + ".eosr-stage-" + next.transaction_id;
    if (live_hash == intent.previous_staged_sha256) {
        if (!path_missing(target_stage, filesystem)) {
            if (!verify_hash(target_stage, next.staged_sha256, filesystem) ||
                filesystem.remove(target_stage) != transaction_io_result::ok) {
                return result(install_result_code::recovery_required,
                              "updated artifact stage is incomplete or changed", true);
            }
        }
        if (!copy_to_new(next.artifact_path, target_stage, filesystem) ||
            filesystem.set_mode(target_stage, next.original_mode) != transaction_io_result::ok ||
            !verify_hash(target_stage, next.staged_sha256, filesystem) ||
            !verify_hash(next.target_path, intent.previous_staged_sha256, filesystem)) {
            return result(install_result_code::stage_failed,
                          "updated artifact could not be staged", true);
        }
        if (filesystem.rename_replace(target_stage, next.target_path) != transaction_io_result::ok ||
            filesystem.flush_parent(next.target_path) != transaction_io_result::ok ||
            !verify_hash(next.target_path, next.staged_sha256, filesystem)) {
            return result(install_result_code::replace_failed,
                          "updated target replacement did not verify", true);
        }
        live_hash = next.staged_sha256;
    }
    if (live_hash != next.staged_sha256) {
        return result(install_result_code::externally_changed,
                      "live target matches neither previous nor updated artifact", true);
    }

    const std::string descriptor = descriptor_json(next.data_dir);
    std::string descriptor_hash;
    if (!hash_file(next.descriptor_path, filesystem, descriptor_hash, 0, 0)) {
        return result(install_result_code::externally_changed,
                      "bootstrap descriptor is missing or unreadable", true);
    }
    if (descriptor_hash == intent.previous_descriptor_sha256 &&
        descriptor_hash != next.descriptor_sha256) {
        const std::string temporary = next.descriptor_path + ".eosr-stage-" +
                                      next.transaction_id;
        if (!replace_owned_text(next.descriptor_path, temporary, descriptor,
                                intent.previous_descriptor_sha256, 0600, filesystem)) {
            return result(install_result_code::descriptor_failed,
                          "updated bootstrap descriptor could not be committed", true);
        }
        descriptor_hash = next.descriptor_sha256;
    }
    if (descriptor_hash != next.descriptor_sha256) {
        return result(install_result_code::externally_changed,
                      "bootstrap descriptor is externally changed", true);
    }

    next.journal_sha256 = sha256_hex(journal_bytes);
    const std::string next_record_bytes = serialize_json(record_json(next, true));
    std::string current_record;
    if (!read_all(next.record_path, max_transaction_file_bytes, filesystem, current_record)) {
        return result(install_result_code::record_failed,
                      "central installation record is unreadable", true);
    }
    if (current_record != next_record_bytes) {
        if (sha256_hex(current_record) != intent.previous_record_sha256) {
            return result(install_result_code::externally_changed,
                          "central installation record changed during update", true);
        }
        const std::string temporary = next.record_path + ".eosr-stage-" + next.transaction_id;
        if (!replace_owned_text(next.record_path, temporary, next_record_bytes,
                                intent.previous_record_sha256, 0600, filesystem)) {
            return result(install_result_code::record_failed,
                          "updated central installation record could not be committed", true);
        }
    }

    bool cleanup_ok = remove_owned_or_missing(intent.previous_journal_path,
                                              intent.previous_journal_sha256, filesystem);
    return cleanup_ok ? result(install_result_code::installed, std::string(), true)
                      : result(install_result_code::cleanup_incomplete,
                               "update completed but an older owned journal remains", true);
}

} // namespace

transaction_file_info::transaction_file_info()
    : exists(false), regular(false), symlink(false), writable(false), mode(0) {}

install_result::install_result()
    : code(install_result_code::invalid_request), target_is_reimagined(false) {}

installation_health::installation_health() : state(installation_state::missing) {}

install_result install_release(const install_request& request,
                               transaction_filesystem& filesystem) {
    if (!validate_request(request)) {
        return result(install_result_code::invalid_request, "invalid transaction request");
    }
    transaction_file_info target_info;
    const transaction_io_result target_status = filesystem.inspect(request.target_path, target_info);
    if (target_status == transaction_io_result::missing) {
        return result(install_result_code::target_missing, "EOS target is missing");
    }
    if (target_status != transaction_io_result::ok || !target_info.regular || target_info.symlink) {
        return result(install_result_code::target_unsafe, "EOS target is not a regular non-symlink file");
    }
    if (!target_info.writable) {
        return result(install_result_code::target_unwritable, "EOS target is not writable");
    }
    std::string canonical;
    if (filesystem.canonical_file(request.target_path, canonical) != transaction_io_result::ok ||
        canonical != request.target_path) {
        return result(install_result_code::target_unsafe, "EOS target path is not canonical");
    }
    bool in_use = false;
    if (filesystem.in_use(request.target_path, in_use) != transaction_io_result::ok || in_use) {
        return result(install_result_code::target_in_use, "EOS target is in use");
    }
    transaction_file_info artifact_info;
    if (filesystem.inspect(request.artifact.path, artifact_info) != transaction_io_result::ok ||
        !artifact_info.regular || artifact_info.symlink) {
        return result(install_result_code::artifact_invalid, "release artifact is unsafe");
    }
    std::string staged_hash;
    std::size_t staged_bytes = 0;
    std::string prefix;
    if (!hash_file(request.artifact.path, filesystem, staged_hash, &staged_bytes, &prefix) ||
        staged_hash != request.artifact.sha256 || staged_bytes != request.artifact.bytes ||
        classify_eos_binary(prefix) != request.artifact.kind) {
        return result(install_result_code::artifact_invalid, "release artifact failed verification");
    }
    const std::string stage = request.target_path + ".eosr-stage-" + request.transaction_id;
    const std::string journal_temporary = request.journal_path + ".eosr-stage-" +
                                          request.transaction_id;
    const std::string descriptor_temporary = request.descriptor_path + ".eosr-stage-" +
                                             request.transaction_id;
    const std::string record_temporary = request.record_path + ".eosr-stage-" +
                                         request.transaction_id;
    const char* sidecars[] = {request.backup_path.c_str(), request.journal_path.c_str(),
                              request.descriptor_path.c_str(), request.record_path.c_str(),
                              stage.c_str(), journal_temporary.c_str(),
                              descriptor_temporary.c_str(), record_temporary.c_str()};
    for (std::size_t i = 0; i < sizeof(sidecars) / sizeof(sidecars[0]); i++) {
        if (!path_missing(sidecars[i], filesystem)) {
            return result(install_result_code::sidecar_exists,
                          std::string("transaction path already exists: ") + sidecars[i]);
        }
    }
    std::string original_hash;
    bool original_is_eosr = false;
    if (!hash_file(request.target_path, filesystem, original_hash, 0, 0,
                   &original_is_eosr)) {
        return result(install_result_code::target_unsafe, "EOS target could not be hashed");
    }
    if (original_is_eosr) {
        return result(install_result_code::externally_changed,
                      "EOS Reimagined is present without a manager-owned verified original "
                      "backup; use Steam Verify Installed Files before installing",
                      true);
    }
    if (!copy_to_new(request.target_path, request.backup_path, filesystem) ||
        filesystem.set_mode(request.backup_path, target_info.mode) != transaction_io_result::ok ||
        filesystem.flush_parent(request.backup_path) != transaction_io_result::ok ||
        !verify_hash(request.backup_path, original_hash, filesystem)) {
        return result(install_result_code::backup_failed, "original backup could not be verified");
    }

    installation_record record;
    record.transaction_id = request.transaction_id;
    record.release_id = request.release_id;
    record.target_path = request.target_path;
    record.artifact_path = request.artifact.path;
    record.backup_path = request.backup_path;
    record.journal_path = request.journal_path;
    record.descriptor_path = request.descriptor_path;
    record.record_path = request.record_path;
    record.data_dir = request.data_dir;
    record.original_sha256 = original_hash;
    record.staged_sha256 = staged_hash;
    record.staged_bytes = staged_bytes;
    record.original_mode = target_info.mode;
    record.kind = request.artifact.kind;
    const std::string descriptor = descriptor_json(request.data_dir);
    record.descriptor_sha256 = sha256_hex(descriptor);
    const std::string journal_bytes = serialize_json(record_json(record, false));
    if (!atomic_new(request.journal_path, journal_temporary, journal_bytes, 0600, filesystem)) {
        return result(install_result_code::journal_failed, "recovery journal could not be committed");
    }
    return finish_from_record(record, journal_bytes, filesystem);
}

install_result update_installation(const update_request& request,
                                   transaction_filesystem& filesystem) {
    if (!valid_manager_id(request.transaction_id) || !release_id_valid(request.release_id) ||
        !manager_absolute_path(request.record_path) ||
        !manager_absolute_path(request.artifact.path) ||
        request.artifact.kind == eos_binary_kind::unknown || request.artifact.bytes == 0 ||
        !hash_valid(request.artifact.sha256) ||
        (!request.data_dir.empty() && !manager_absolute_path(request.data_dir))) {
        return result(install_result_code::invalid_request, "invalid update request");
    }
    std::string previous_record_bytes;
    if (!read_all(request.record_path, max_transaction_file_bytes, filesystem,
                  previous_record_bytes)) {
        return result(install_result_code::state_invalid,
                      "current installation record is unreadable");
    }
    installation_record previous;
    std::string error;
    if (!parse_record_json(previous_record_bytes, true, previous, error) ||
        previous.record_path != request.record_path) {
        return result(install_result_code::state_invalid,
                      error.empty() ? "record path does not match its contents" : error);
    }
    if (previous.kind != request.artifact.kind) {
        return result(install_result_code::artifact_invalid,
                      "updated artifact kind does not match the selected target", true);
    }
    transaction_file_info target;
    std::string canonical;
    if (filesystem.inspect(previous.target_path, target) != transaction_io_result::ok ||
        !target.regular || target.symlink || !target.writable ||
        filesystem.canonical_file(previous.target_path, canonical) != transaction_io_result::ok ||
        canonical != previous.target_path) {
        return result(install_result_code::target_unsafe, "update target is unsafe", true);
    }
    bool in_use = false;
    if (filesystem.in_use(previous.target_path, in_use) != transaction_io_result::ok || in_use) {
        return result(install_result_code::target_in_use, "update target is in use", true);
    }
    if (!verify_hash(previous.backup_path, previous.original_sha256, filesystem) ||
        !verify_hash(previous.descriptor_path, previous.descriptor_sha256, filesystem) ||
        !verify_hash(previous.journal_path, previous.journal_sha256, filesystem)) {
        return result(install_result_code::recovery_required,
                      "existing installation evidence no longer verifies", true);
    }
    transaction_file_info artifact;
    std::string artifact_hash;
    std::size_t artifact_bytes = 0;
    std::string prefix;
    if (filesystem.inspect(request.artifact.path, artifact) != transaction_io_result::ok ||
        !artifact.regular || artifact.symlink ||
        !hash_file(request.artifact.path, filesystem, artifact_hash, &artifact_bytes, &prefix) ||
        artifact_hash != request.artifact.sha256 || artifact_bytes != request.artifact.bytes ||
        classify_eos_binary(prefix) != request.artifact.kind) {
        return result(install_result_code::artifact_invalid,
                      "updated release artifact failed verification", true);
    }

    update_intent intent;
    intent.next = previous;
    intent.next.transaction_id = request.transaction_id;
    intent.next.release_id = request.release_id;
    intent.next.artifact_path = request.artifact.path;
    intent.next.staged_sha256 = request.artifact.sha256;
    intent.next.staged_bytes = request.artifact.bytes;
    intent.next.data_dir = request.data_dir.empty() ? previous.data_dir : request.data_dir;
    intent.next.descriptor_sha256 = sha256_hex(descriptor_json(intent.next.data_dir));
    intent.next.journal_path = request.record_path + ".update-" + request.transaction_id + ".json";
    intent.next.journal_sha256.clear();
    intent.next.obsolete_journals.push_back(
        std::make_pair(previous.journal_path, previous.journal_sha256));
    intent.previous_record_sha256 = sha256_hex(previous_record_bytes);
    intent.previous_staged_sha256 = previous.staged_sha256;
    intent.previous_descriptor_sha256 = previous.descriptor_sha256;
    intent.previous_journal_path = previous.journal_path;
    intent.previous_journal_sha256 = previous.journal_sha256;

    const std::string journal_temporary = intent.next.journal_path + ".eosr-stage-" +
                                          request.transaction_id;
    const std::string target_stage = previous.target_path + ".eosr-stage-" +
                                     request.transaction_id;
    const std::string descriptor_stage = previous.descriptor_path + ".eosr-stage-" +
                                         request.transaction_id;
    const std::string record_stage = previous.record_path + ".eosr-stage-" +
                                     request.transaction_id;
    const char* sidecars[] = {intent.next.journal_path.c_str(), journal_temporary.c_str(),
                              target_stage.c_str(), descriptor_stage.c_str(),
                              record_stage.c_str()};
    for (std::size_t i = 0; i < sizeof(sidecars) / sizeof(sidecars[0]); i++) {
        if (!path_missing(sidecars[i], filesystem)) {
            return result(install_result_code::sidecar_exists,
                          std::string("update path already exists: ") + sidecars[i], true);
        }
    }
    const std::string journal_bytes = update_intent_json(intent);
    if (!atomic_new(intent.next.journal_path, journal_temporary, journal_bytes, 0600,
                    filesystem)) {
        return result(install_result_code::journal_failed,
                      "update recovery journal could not be committed", true);
    }
    return finish_update(intent, journal_bytes, filesystem);
}

install_result recover_installation(const std::string& journal_path,
                                    transaction_filesystem& filesystem) {
    std::string bytes;
    if (!read_all(journal_path, max_transaction_file_bytes, filesystem, bytes)) {
        return result(install_result_code::state_invalid, "recovery journal is unreadable");
    }
    std::string error;
    json_value root;
    if (parse_json(bytes, root, error)) {
        const json_value* operation = json_member(root, "operation");
        if (operation != 0 && operation->kind == json_kind::string &&
            operation->text == "update") {
            update_intent intent;
            if (!parse_update_intent(bytes, intent, error) ||
                intent.next.journal_path != journal_path) {
                return result(install_result_code::state_invalid,
                              error.empty() ? "update journal path mismatch" : error);
            }
            transaction_file_info target;
            if (filesystem.inspect(intent.next.target_path, target) != transaction_io_result::ok ||
                !target.regular || target.symlink) {
                return result(install_result_code::target_unsafe,
                              "update recovery target is unsafe", true);
            }
            bool in_use = false;
            if (filesystem.in_use(intent.next.target_path, in_use) != transaction_io_result::ok ||
                in_use) {
                return result(install_result_code::target_in_use,
                              "update recovery target is in use", true);
            }
            return finish_update(intent, bytes, filesystem);
        }
    }
    error.clear();
    installation_record record;
    if (!parse_record_json(bytes, false, record, error) || record.journal_path != journal_path) {
        return result(install_result_code::state_invalid,
                      error.empty() ? "journal path does not match its contents" : error);
    }
    transaction_file_info target_info;
    if (filesystem.inspect(record.target_path, target_info) != transaction_io_result::ok ||
        !target_info.regular || target_info.symlink) {
        return result(install_result_code::target_unsafe, "recovery target is unsafe");
    }
    bool in_use = false;
    if (filesystem.in_use(record.target_path, in_use) != transaction_io_result::ok || in_use) {
        return result(install_result_code::target_in_use, "recovery target is in use");
    }
    return finish_from_record(record, bytes, filesystem);
}

install_result restore_installation(const std::string& record_path,
                                    transaction_filesystem& filesystem) {
    std::string record_bytes;
    if (!read_all(record_path, max_transaction_file_bytes, filesystem, record_bytes)) {
        return result(install_result_code::state_invalid, "installation record is unreadable");
    }
    installation_record record;
    std::string error;
    if (!parse_record_json(record_bytes, true, record, error) || record.record_path != record_path) {
        return result(install_result_code::state_invalid,
                      error.empty() ? "record path does not match its contents" : error);
    }
    transaction_file_info target_info;
    if (filesystem.inspect(record.target_path, target_info) != transaction_io_result::ok ||
        !target_info.regular || target_info.symlink) {
        return result(install_result_code::target_unsafe, "restore target is unsafe");
    }
    bool in_use = false;
    if (filesystem.in_use(record.target_path, in_use) != transaction_io_result::ok || in_use) {
        return result(install_result_code::target_in_use, "restore target is in use");
    }
    std::string live_hash;
    if (!hash_file(record.target_path, filesystem, live_hash, 0, 0) ||
        (live_hash != record.staged_sha256 && live_hash != record.original_sha256)) {
        return result(install_result_code::externally_changed,
                      "live target matches neither the installed artifact nor verified original",
                      true);
    }

    if (live_hash == record.staged_sha256) {
        if (!verify_hash(record.backup_path, record.original_sha256, filesystem)) {
            return result(install_result_code::backup_invalid,
                          "original backup no longer verifies", true);
        }
        if (!verify_hash(record.descriptor_path, record.descriptor_sha256, filesystem)) {
            return result(install_result_code::externally_changed,
                          "bootstrap descriptor is missing or changed", true);
        }
        std::string journal_bytes;
        if (!read_all(record.journal_path, max_transaction_file_bytes, filesystem, journal_bytes) ||
            sha256_hex(journal_bytes) != record.journal_sha256) {
            return result(install_result_code::state_invalid,
                          "recovery journal no longer verifies", true);
        }

        const std::string restore_stage = record.target_path + ".eosr-restore-" +
                                          record.transaction_id;
        if (!path_missing(restore_stage, filesystem)) {
            if (!verify_hash(restore_stage, record.original_sha256, filesystem) ||
                filesystem.remove(restore_stage) != transaction_io_result::ok) {
                return result(install_result_code::recovery_required,
                              "existing restore stage is incomplete or changed", true);
            }
        }
        if (!copy_to_new(record.backup_path, restore_stage, filesystem) ||
            filesystem.set_mode(restore_stage, record.original_mode) != transaction_io_result::ok ||
            !verify_hash(restore_stage, record.original_sha256, filesystem) ||
            !verify_hash(record.target_path, record.staged_sha256, filesystem)) {
            return result(install_result_code::stage_failed,
                          "original could not be staged safely", true);
        }
        if (filesystem.rename_replace(restore_stage, record.target_path) !=
                transaction_io_result::ok ||
            filesystem.flush_parent(record.target_path) != transaction_io_result::ok ||
            !verify_hash(record.target_path, record.original_sha256, filesystem)) {
            return result(install_result_code::replace_failed,
                          "original replacement did not verify", true);
        }
    }

    bool cleanup_ok = true;
    if (!remove_owned_or_missing(record.descriptor_path, record.descriptor_sha256, filesystem)) {
        cleanup_ok = false;
    }
    if (!remove_owned_or_missing(record.journal_path, record.journal_sha256, filesystem)) {
        cleanup_ok = false;
    }
    for (std::size_t i = 0; i < record.obsolete_journals.size(); i++) {
        if (!remove_owned_or_missing(record.obsolete_journals[i].first,
                                     record.obsolete_journals[i].second, filesystem)) {
            cleanup_ok = false;
        }
    }
    if (!remove_owned_or_missing(record.backup_path, record.original_sha256, filesystem)) {
        cleanup_ok = false;
    }
    std::string current_record;
    if (!read_all(record.record_path, max_transaction_file_bytes, filesystem, current_record) ||
        current_record != record_bytes ||
        filesystem.remove(record.record_path) != transaction_io_result::ok) {
        cleanup_ok = false;
    }
    if (filesystem.flush_parent(record.target_path) != transaction_io_result::ok) {
        cleanup_ok = false;
    }
    return cleanup_ok ? result(install_result_code::restored, std::string())
                      : result(install_result_code::cleanup_incomplete,
                               "original restored but some owned evidence could not be removed");
}

installation_health inspect_installation(const installation_probe& probe,
                                         transaction_filesystem& filesystem) {
    installation_health health;
    if (probe.ambiguous) {
        health.state = installation_state::ambiguous;
        health.detail = "more than one EOS target requires an explicit selection";
        return health;
    }
    transaction_file_info target;
    const transaction_io_result target_status = filesystem.inspect(probe.target_path, target);
    if (target_status == transaction_io_result::missing) {
        health.state = installation_state::missing;
        return health;
    }
    if (target_status != transaction_io_result::ok || !target.regular || target.symlink) {
        health.state = installation_state::unwritable;
        health.detail = "target is not a regular non-symlink file";
        return health;
    }
    if (!target.writable) {
        health.state = installation_state::unwritable;
        health.detail = "target is not writable";
        return health;
    }
    bool eosr_signature = false;
    if (!hash_file(probe.target_path, filesystem, health.live_sha256, 0, 0,
                   &eosr_signature)) {
        health.state = installation_state::unwritable;
        health.detail = "target cannot be hashed";
        return health;
    }

    transaction_file_info record_info;
    const transaction_io_result record_status = filesystem.inspect(probe.record_path, record_info);
    if (record_status == transaction_io_result::ok) {
        std::string record_bytes;
        installation_record record;
        std::string error;
        if (!read_all(probe.record_path, max_transaction_file_bytes, filesystem, record_bytes) ||
            !parse_record_json(record_bytes, true, record, error) ||
            record.target_path != probe.target_path || record.record_path != probe.record_path) {
            health.state = installation_state::recovery_required;
            health.detail = error.empty() ? "installation record is invalid" : error;
            return health;
        }
        if (health.live_sha256 != record.staged_sha256) {
            health.state = installation_state::externally_changed;
            health.detail = "live target no longer matches manager state";
            return health;
        }
        if (!verify_hash(record.backup_path, record.original_sha256, filesystem)) {
            health.state = installation_state::recovery_required;
            health.detail = "verified original backup is missing or changed";
            return health;
        }
        std::string journal;
        if (!read_all(record.journal_path, max_transaction_file_bytes, filesystem, journal) ||
            sha256_hex(journal) != record.journal_sha256) {
            health.state = installation_state::recovery_required;
            health.detail = "recovery journal is missing or changed";
            return health;
        }
        health.state = record.staged_sha256 == probe.selected_artifact.sha256
                           ? installation_state::installed_current
                           : installation_state::installed_other_build;
        return health;
    }
    if (record_status != transaction_io_result::missing) {
        health.state = installation_state::recovery_required;
        health.detail = "installation record cannot be inspected";
        return health;
    }

    transaction_file_info journal_info;
    const transaction_io_result journal_status = filesystem.inspect(probe.journal_path,
                                                                     journal_info);
    if (journal_status == transaction_io_result::ok) {
        health.state = installation_state::recovery_required;
        health.detail = "a transaction journal exists without completed manager state";
        return health;
    }
    if (journal_status != transaction_io_result::missing) {
        health.state = installation_state::recovery_required;
        health.detail = "transaction journal cannot be inspected";
        return health;
    }
    bool known = eosr_signature || health.live_sha256 == probe.selected_artifact.sha256;
    for (std::size_t i = 0; i < probe.known_reimagined_sha256.size(); i++) {
        known = known || health.live_sha256 == probe.known_reimagined_sha256[i];
    }
    health.state = known ? installation_state::original_unknown : installation_state::original;
    if (known) {
        health.detail = "EOS Reimagined is present without a manager-owned verified original "
                        "backup; use Steam Verify Installed Files before installing";
    }
    return health;
}

} // namespace manager
} // namespace eosr
