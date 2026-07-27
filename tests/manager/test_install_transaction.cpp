#include "doctest.h"

#include <algorithm>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "manager/install_transaction.h"
#include "manager/configuration_model.h"
#include "manager/sha256.h"
#include "manager/state_store.h"

using namespace eosr::manager;

namespace {

class memory_transaction_filesystem : public transaction_filesystem {
public:
    struct file_value {
        std::string bytes;
        unsigned int mode;
        bool symlink;
        bool writable;
        file_value() : mode(0644), symlink(false), writable(true) {}
    };

    struct open_value {
        std::string path;
        std::size_t offset;
        bool writing;
    };

    memory_transaction_filesystem() : next_handle_(1), fail_call_(0), calls_(0) {}

    void file(const std::string& path, const std::string& bytes) {
        files_[path].bytes = bytes;
    }

    void writable(const std::string& path, bool value) { files_[path].writable = value; }
    void symlink(const std::string& path, bool value) { files_[path].symlink = value; }

    bool has(const std::string& path) const { return files_.find(path) != files_.end(); }
    std::string bytes(const std::string& path) const {
        std::map<std::string, file_value>::const_iterator it = files_.find(path);
        return it == files_.end() ? std::string() : it->second.bytes;
    }
    void fail_on_call(std::size_t call) { fail_call_ = call; calls_ = 0; }
    void clear_failure() { fail_call_ = 0; calls_ = 0; }
    std::size_t calls() const { return calls_; }
    const std::vector<std::string>& operations() const { return operations_; }

    transaction_io_result inspect(const std::string& path, transaction_file_info& out) {
        if (failed("inspect")) return transaction_io_result::io_error;
        std::map<std::string, file_value>::const_iterator it = files_.find(path);
        out = transaction_file_info();
        if (it == files_.end()) return transaction_io_result::missing;
        out.exists = true;
        out.regular = true;
        out.symlink = it->second.symlink;
        out.writable = it->second.writable;
        out.mode = it->second.mode;
        return transaction_io_result::ok;
    }
    transaction_io_result canonical_file(const std::string& path, std::string& out) {
        if (failed("canonical")) return transaction_io_result::io_error;
        if (!has(path)) return transaction_io_result::missing;
        out = path;
        return transaction_io_result::ok;
    }
    transaction_io_result in_use(const std::string&, bool& out) {
        if (failed("in_use")) return transaction_io_result::io_error;
        out = false;
        return transaction_io_result::ok;
    }
    transaction_io_result open_read(const std::string& path, transaction_handle& out) {
        if (failed("open_read")) return transaction_io_result::io_error;
        if (!has(path)) return transaction_io_result::missing;
        open_value value;
        value.path = path;
        value.offset = 0;
        value.writing = false;
        out = next_handle_++;
        open_[out] = value;
        return transaction_io_result::ok;
    }
    transaction_io_result create_new(const std::string& path, transaction_handle& out) {
        if (failed("create")) return transaction_io_result::io_error;
        if (has(path)) return transaction_io_result::exists;
        files_[path] = file_value();
        open_value value;
        value.path = path;
        value.offset = 0;
        value.writing = true;
        out = next_handle_++;
        open_[out] = value;
        return transaction_io_result::ok;
    }
    transaction_io_result read(transaction_handle handle, unsigned char* data,
                               std::size_t capacity, std::size_t& count) {
        if (failed("read")) return transaction_io_result::io_error;
        std::map<transaction_handle, open_value>::iterator it = open_.find(handle);
        if (it == open_.end() || it->second.writing) return transaction_io_result::io_error;
        const std::string& source = files_[it->second.path].bytes;
        if (it->second.offset == source.size()) {
            count = 0;
            return transaction_io_result::end_of_file;
        }
        count = source.size() - it->second.offset;
        if (count > capacity) count = capacity;
        std::memcpy(data, source.data() + it->second.offset, count);
        it->second.offset += count;
        return transaction_io_result::ok;
    }
    transaction_io_result write(transaction_handle handle, const unsigned char* data,
                                std::size_t size, std::size_t& count) {
        if (failed("write")) return transaction_io_result::io_error;
        std::map<transaction_handle, open_value>::iterator it = open_.find(handle);
        if (it == open_.end() || !it->second.writing) return transaction_io_result::io_error;
        files_[it->second.path].bytes.append(reinterpret_cast<const char*>(data), size);
        count = size;
        return transaction_io_result::ok;
    }
    transaction_io_result flush(transaction_handle handle) {
        if (failed("flush")) return transaction_io_result::io_error;
        return open_.find(handle) == open_.end() ? transaction_io_result::io_error
                                                 : transaction_io_result::ok;
    }
    transaction_io_result close(transaction_handle handle) {
        if (failed("close")) return transaction_io_result::io_error;
        return open_.erase(handle) == 1 ? transaction_io_result::ok
                                        : transaction_io_result::io_error;
    }
    transaction_io_result set_mode(const std::string& path, unsigned int mode) {
        if (failed("set_mode")) return transaction_io_result::io_error;
        if (!has(path)) return transaction_io_result::missing;
        files_[path].mode = mode;
        return transaction_io_result::ok;
    }
    transaction_io_result rename_no_replace(const std::string& from, const std::string& to) {
        if (failed("rename_no_replace")) return transaction_io_result::io_error;
        if (!has(from)) return transaction_io_result::missing;
        if (has(to)) return transaction_io_result::exists;
        files_[to] = files_[from]; files_.erase(from);
        return transaction_io_result::ok;
    }
    transaction_io_result rename_replace(const std::string& from, const std::string& to) {
        if (failed("rename_replace")) return transaction_io_result::io_error;
        if (!has(from)) return transaction_io_result::missing;
        files_[to] = files_[from]; files_.erase(from);
        return transaction_io_result::ok;
    }
    transaction_io_result remove(const std::string& path) {
        if (failed("remove")) return transaction_io_result::io_error;
        return files_.erase(path) == 1 ? transaction_io_result::ok : transaction_io_result::missing;
    }
    transaction_io_result flush_parent(const std::string&) {
        return failed("flush_parent") ? transaction_io_result::io_error
                                       : transaction_io_result::ok;
    }

private:
    bool failed(const std::string& operation) {
        operations_.push_back(operation);
        calls_++;
        return fail_call_ != 0 && calls_ == fail_call_;
    }
    std::map<std::string, file_value> files_;
    std::map<transaction_handle, open_value> open_;
    transaction_handle next_handle_;
    std::size_t fail_call_;
    std::size_t calls_;
    std::vector<std::string> operations_;
};

std::string pe(const std::string& payload) {
    std::string bytes(256, '\0');
    bytes[0] = 'M'; bytes[1] = 'Z'; bytes[0x3c] = static_cast<char>(0x80);
    bytes[0x80] = 'P'; bytes[0x81] = 'E';
    bytes[0x84] = static_cast<char>(0x64); bytes[0x85] = static_cast<char>(0x86);
    bytes += payload;
    return bytes;
}

install_request request_for(const std::string& original, const std::string& artifact) {
    install_request request;
    request.transaction_id = "11111111111111111111111111111111";
    request.release_id = "v0.1.0-alpha.1";
    request.target_path = "/game/EOSSDK-Win64-Shipping.dll";
    request.artifact.path = "/release/EOSSDK-Win64-Shipping.dll";
    request.artifact.kind = eos_binary_kind::windows_x86_64;
    request.artifact.bytes = artifact.size();
    request.artifact.sha256 = sha256_hex(artifact);
    request.backup_path = request.target_path + ".eosr-original";
    request.journal_path = request.target_path + ".eosr-journal.json";
    request.descriptor_path = "/game/eosr-bootstrap.json";
    request.record_path = "/manager/targets/target.json";
    request.data_dir = "/manager/games/game/instances/alice/data";
    (void)original;
    return request;
}

void seed(memory_transaction_filesystem& fs, const install_request& request,
          const std::string& original, const std::string& artifact) {
    fs.file(request.target_path, original);
    fs.file(request.artifact.path, artifact);
}

installation_probe probe_for(const install_request& request) {
    installation_probe probe;
    probe.target_path = request.target_path;
    probe.record_path = request.record_path;
    probe.journal_path = request.journal_path;
    probe.selected_artifact = request.artifact;
    probe.known_reimagined_sha256.push_back(request.artifact.sha256);
    probe.ambiguous = false;
    return probe;
}

TEST_CASE("packaged version-at-commit release identities are accepted by transactions") {
    const std::string original = pe("original");
    const std::string artifact = pe("reimagined");
    memory_transaction_filesystem fs;
    install_request request = request_for(original, artifact);
    request.release_id = "v0.1.0-alpha.1@0123456789abcdef";
    seed(fs, request, original, artifact);
    CHECK(install_release(request, fs).code == install_result_code::installed);
}

update_request update_for(const install_request& installed, const std::string& artifact_path,
                          const std::string& artifact) {
    update_request update;
    update.transaction_id = "44444444444444444444444444444444";
    update.release_id = "v0.1.0-alpha.2";
    update.record_path = installed.record_path;
    update.artifact.path = artifact_path;
    update.artifact.kind = eos_binary_kind::windows_x86_64;
    update.artifact.bytes = artifact.size();
    update.artifact.sha256 = sha256_hex(artifact);
    return update;
}

} // namespace

TEST_CASE("install and restore are hash-owned reversible transactions") {
    const std::string original = pe("original");
    const std::string artifact = pe("reimagined");
    const install_request request = request_for(original, artifact);
    memory_transaction_filesystem fs;
    seed(fs, request, original, artifact);

    const install_result installed = install_release(request, fs);
    REQUIRE(installed.code == install_result_code::installed);
    CHECK(fs.bytes(request.target_path) == artifact);
    CHECK(fs.bytes(request.backup_path) == original);
    CHECK(fs.has(request.journal_path));
    CHECK(fs.has(request.descriptor_path));
    CHECK(fs.has(request.record_path));

    const install_result restored = restore_installation(request.record_path, fs);
    CHECK(restored.code == install_result_code::restored);
    CHECK(fs.bytes(request.target_path) == original);
    CHECK_FALSE(fs.has(request.backup_path));
    CHECK_FALSE(fs.has(request.descriptor_path));
    CHECK_FALSE(fs.has(request.journal_path));
    CHECK_FALSE(fs.has(request.record_path));
}

TEST_CASE("install never adopts an unowned older EOS Reimagined library as the original") {
    const std::string external = pe("older build EOSR_DATA_DIR");
    const std::string artifact = pe("current reimagined");
    const install_request request = request_for(external, artifact);
    memory_transaction_filesystem fs;
    seed(fs, request, external, artifact);

    const install_result refused = install_release(request, fs);
    CHECK(refused.code == install_result_code::externally_changed);
    CHECK(refused.target_is_reimagined);
    CHECK(refused.detail.find("Steam Verify Installed Files") != std::string::npos);
    CHECK(fs.bytes(request.target_path) == external);
    CHECK_FALSE(fs.has(request.backup_path));
    CHECK_FALSE(fs.has(request.journal_path));
    CHECK_FALSE(fs.has(request.record_path));
}

TEST_CASE("EOS Reimagined recognition survives a streaming hash buffer boundary") {
    const std::size_t prefix_bytes = 256;
    const std::size_t marker_offset = 65530;
    const std::string external = pe(std::string(marker_offset - prefix_bytes, 'x') +
                                    "EOSR_DATA_DIR older build");
    const std::string artifact = pe("current reimagined");
    const install_request request = request_for(external, artifact);
    memory_transaction_filesystem fs;
    seed(fs, request, external, artifact);

    CHECK(inspect_installation(probe_for(request), fs).state ==
          installation_state::original_unknown);
    CHECK(install_release(request, fs).code == install_result_code::externally_changed);
    CHECK(fs.bytes(request.target_path) == external);
    CHECK_FALSE(fs.has(request.backup_path));
}

TEST_CASE("restore never overwrites an externally changed target") {
    const std::string original = pe("original");
    const std::string artifact = pe("reimagined");
    const install_request request = request_for(original, artifact);
    memory_transaction_filesystem fs;
    seed(fs, request, original, artifact);
    REQUIRE(install_release(request, fs).code == install_result_code::installed);
    fs.file(request.target_path, pe("steam-update"));

    const install_result restored = restore_installation(request.record_path, fs);
    CHECK(restored.code == install_result_code::externally_changed);
    CHECK(fs.bytes(request.target_path) == pe("steam-update"));
    CHECK(fs.bytes(request.backup_path) == original);
}

TEST_CASE("an interrupted transaction with a durable journal resumes deterministically") {
    const std::string original = pe("original");
    const std::string artifact = pe("reimagined");
    const install_request request = request_for(original, artifact);
    memory_transaction_filesystem probe;
    seed(probe, request, original, artifact);
    REQUIRE(install_release(request, probe).code == install_result_code::installed);
    const std::vector<std::string> operations = probe.operations();
    std::size_t replacement_call = 0;
    for (std::size_t i = 0; i < operations.size(); i++) {
        if (operations[i] == "rename_replace") {
            replacement_call = i + 2;
            break;
        }
    }
    REQUIRE(replacement_call != 0);

    memory_transaction_filesystem fs;
    seed(fs, request, original, artifact);
    fs.fail_on_call(replacement_call);
    const install_result interrupted = install_release(request, fs);
    CHECK(interrupted.code != install_result_code::installed);
    REQUIRE(fs.has(request.journal_path));
    fs.clear_failure();
    const install_result recovered = recover_installation(request.journal_path, fs);
    CHECK(recovered.code == install_result_code::installed);
    CHECK(fs.bytes(request.target_path) == artifact);
    CHECK(fs.has(request.record_path));
}

TEST_CASE("install fault injection leaves only an original or verified staged target") {
    const std::string original = pe("original");
    const std::string artifact = pe("reimagined");
    const install_request request = request_for(original, artifact);
    memory_transaction_filesystem baseline;
    seed(baseline, request, original, artifact);
    REQUIRE(install_release(request, baseline).code == install_result_code::installed);
    const std::size_t operation_count = baseline.calls();
    REQUIRE(operation_count > 30);

    std::set<std::string> required_operations;
    required_operations.insert("create");
    required_operations.insert("write");
    required_operations.insert("flush");
    required_operations.insert("close");
    required_operations.insert("set_mode");
    required_operations.insert("rename_no_replace");
    required_operations.insert("rename_replace");
    required_operations.insert("flush_parent");
    for (std::set<std::string>::const_iterator it = required_operations.begin();
         it != required_operations.end(); ++it) {
        CHECK(std::find(baseline.operations().begin(), baseline.operations().end(), *it) !=
              baseline.operations().end());
    }

    for (std::size_t call = 1; call <= operation_count; call++) {
        memory_transaction_filesystem fs;
        seed(fs, request, original, artifact);
        fs.fail_on_call(call);
        const install_result interrupted = install_release(request, fs);
        CAPTURE(call);
        CAPTURE(interrupted.detail);
        const bool target_safe = fs.bytes(request.target_path) == original ||
                                 fs.bytes(request.target_path) == artifact;
        CHECK(target_safe);
        if (fs.has(request.backup_path)) {
            const bool backup_safe = fs.bytes(request.backup_path).empty() ||
                                     fs.bytes(request.backup_path) == original;
            CHECK(backup_safe);
        }
        if (fs.has(request.descriptor_path)) {
            CHECK(fs.bytes(request.descriptor_path).find(request.data_dir) != std::string::npos);
        }
        if (interrupted.code != install_result_code::installed && fs.has(request.journal_path)) {
            fs.clear_failure();
            const install_result recovered = recover_installation(request.journal_path, fs);
            CAPTURE(recovered.detail);
            const bool recovery_explained = recovered.code == install_result_code::installed ||
                recovered.code == install_result_code::recovery_required ||
                recovered.code == install_result_code::stage_failed ||
                recovered.code == install_result_code::descriptor_failed ||
                recovered.code == install_result_code::record_failed;
            CHECK(recovery_explained);
            const bool recovered_target_safe = fs.bytes(request.target_path) == original ||
                                               fs.bytes(request.target_path) == artifact;
            CHECK(recovered_target_safe);
        }
    }
}

TEST_CASE("restore fault injection is retryable and never substitutes unknown bytes") {
    const std::string original = pe("original");
    const std::string artifact = pe("reimagined");
    const install_request request = request_for(original, artifact);
    memory_transaction_filesystem baseline;
    seed(baseline, request, original, artifact);
    REQUIRE(install_release(request, baseline).code == install_result_code::installed);
    baseline.clear_failure();
    REQUIRE(restore_installation(request.record_path, baseline).code ==
            install_result_code::restored);
    const std::size_t operation_count = baseline.calls();
    REQUIRE(operation_count > 20);
    CHECK(std::find(baseline.operations().begin(), baseline.operations().end(), "remove") !=
          baseline.operations().end());

    for (std::size_t call = 1; call <= operation_count; call++) {
        memory_transaction_filesystem fs;
        seed(fs, request, original, artifact);
        REQUIRE(install_release(request, fs).code == install_result_code::installed);
        fs.fail_on_call(call);
        const install_result interrupted = restore_installation(request.record_path, fs);
        CAPTURE(call);
        CAPTURE(interrupted.detail);
        const bool target_safe = fs.bytes(request.target_path) == original ||
                                 fs.bytes(request.target_path) == artifact;
        CHECK(target_safe);
        fs.clear_failure();
        if (fs.has(request.record_path)) {
            const install_result retried = restore_installation(request.record_path, fs);
            CAPTURE(retried.detail);
            const bool retry_explained = retried.code == install_result_code::restored ||
                retried.code == install_result_code::cleanup_incomplete ||
                retried.code == install_result_code::recovery_required ||
                retried.code == install_result_code::stage_failed;
            CHECK(retry_explained);
            const bool retried_target_safe = fs.bytes(request.target_path) == original ||
                                             fs.bytes(request.target_path) == artifact;
            CHECK(retried_target_safe);
        } else {
            CHECK(fs.bytes(request.target_path) == original);
        }
    }
}

TEST_CASE("unknown sidecars and changed backups are never adopted or overwritten") {
    const std::string original = pe("original");
    const std::string artifact = pe("reimagined");
    const install_request request = request_for(original, artifact);
    memory_transaction_filesystem fs;
    seed(fs, request, original, artifact);
    fs.file(request.backup_path, "somebody else's backup");
    const install_result refused = install_release(request, fs);
    CHECK(refused.code == install_result_code::sidecar_exists);
    CHECK(fs.bytes(request.target_path) == original);
    CHECK(fs.bytes(request.backup_path) == "somebody else's backup");

    memory_transaction_filesystem changed;
    seed(changed, request, original, artifact);
    REQUIRE(install_release(request, changed).code == install_result_code::installed);
    changed.file(request.backup_path, "changed backup");
    const install_result restore = restore_installation(request.record_path, changed);
    CHECK(restore.code == install_result_code::backup_invalid);
    CHECK(changed.bytes(request.target_path) == artifact);
    CHECK(changed.bytes(request.backup_path) == "changed backup");
}

TEST_CASE("installation health exposes every safety-relevant visible state") {
    const std::string original = pe("original");
    const std::string artifact = pe("reimagined");
    const std::string next_artifact = pe("next-build");
    const install_request request = request_for(original, artifact);
    installation_probe probe = probe_for(request);

    memory_transaction_filesystem original_fs;
    seed(original_fs, request, original, artifact);
    CHECK(inspect_installation(probe, original_fs).state == installation_state::original);

    memory_transaction_filesystem installed_fs;
    seed(installed_fs, request, original, artifact);
    REQUIRE(install_release(request, installed_fs).code == install_result_code::installed);
    CHECK(inspect_installation(probe, installed_fs).state ==
          installation_state::installed_current);
    probe.selected_artifact.sha256 = sha256_hex(next_artifact);
    CHECK(inspect_installation(probe, installed_fs).state ==
          installation_state::installed_other_build);
    probe = probe_for(request);

    installed_fs.file(request.target_path, pe("steam-update"));
    CHECK(inspect_installation(probe, installed_fs).state ==
          installation_state::externally_changed);

    memory_transaction_filesystem recovery_fs;
    seed(recovery_fs, request, original, artifact);
    REQUIRE(install_release(request, recovery_fs).code == install_result_code::installed);
    REQUIRE(recovery_fs.remove(request.record_path) == transaction_io_result::ok);
    CHECK(inspect_installation(probe, recovery_fs).state ==
          installation_state::recovery_required);

    memory_transaction_filesystem unknown_fs;
    seed(unknown_fs, request, artifact, artifact);
    const installation_health unknown_health = inspect_installation(probe, unknown_fs);
    CHECK(unknown_health.state == installation_state::original_unknown);
    CHECK(unknown_health.detail.find("Steam Verify Installed Files") != std::string::npos);

    memory_transaction_filesystem older_external_fs;
    const std::string older_external = pe("older EOSR build EOSR_DATA_DIR");
    seed(older_external_fs, request, older_external, artifact);
    const installation_health older_external_health =
        inspect_installation(probe, older_external_fs);
    CHECK(older_external_health.state == installation_state::original_unknown);
    CHECK(older_external_health.live_sha256 == sha256_hex(older_external));

    memory_transaction_filesystem missing_fs;
    CHECK(inspect_installation(probe, missing_fs).state == installation_state::missing);

    memory_transaction_filesystem unsafe_fs;
    seed(unsafe_fs, request, original, artifact);
    unsafe_fs.symlink(request.target_path, true);
    CHECK(inspect_installation(probe, unsafe_fs).state == installation_state::unwritable);
    unsafe_fs.symlink(request.target_path, false);
    unsafe_fs.writable(request.target_path, false);
    CHECK(inspect_installation(probe, unsafe_fs).state == installation_state::unwritable);

    probe.ambiguous = true;
    CHECK(inspect_installation(probe, original_fs).state == installation_state::ambiguous);
}

TEST_CASE("an installed release updates without replacing its original backup") {
    const std::string original = pe("original");
    const std::string first = pe("reimagined-one");
    const std::string second = pe("reimagined-two");
    const install_request install = request_for(original, first);
    const update_request update = update_for(install, "/release/EOSSDK-Win64-Shipping-v2.dll",
                                             second);
    memory_transaction_filesystem fs;
    seed(fs, install, original, first);
    fs.file(update.artifact.path, second);
    REQUIRE(install_release(install, fs).code == install_result_code::installed);

    const install_result updated = update_installation(update, fs);
    CAPTURE(updated.detail);
    REQUIRE(updated.code == install_result_code::installed);
    CHECK(fs.bytes(install.target_path) == second);
    CHECK(fs.bytes(install.backup_path) == original);
    CHECK_FALSE(fs.has(install.journal_path));
    CHECK(fs.has(update.record_path + ".update-" + update.transaction_id + ".json"));

    installation_probe probe = probe_for(install);
    probe.selected_artifact = update.artifact;
    CHECK(inspect_installation(probe, fs).state == installation_state::installed_current);
    const install_result restored = restore_installation(install.record_path, fs);
    CAPTURE(restored.detail);
    CHECK(restored.code == install_result_code::restored);
    CHECK(fs.bytes(install.target_path) == original);
}

TEST_CASE("interrupted updates recover from their own durable journal") {
    const std::string original = pe("original");
    const std::string first = pe("reimagined-one");
    const std::string second = pe("reimagined-two");
    const install_request install = request_for(original, first);
    const update_request update = update_for(install, "/release/EOSSDK-Win64-Shipping-v2.dll",
                                             second);
    memory_transaction_filesystem baseline;
    seed(baseline, install, original, first);
    baseline.file(update.artifact.path, second);
    REQUIRE(install_release(install, baseline).code == install_result_code::installed);
    baseline.clear_failure();
    REQUIRE(update_installation(update, baseline).code == install_result_code::installed);
    const std::size_t operations = baseline.calls();
    REQUIRE(operations > 30);
    const std::string update_journal = update.record_path + ".update-" +
                                       update.transaction_id + ".json";

    for (std::size_t call = 1; call <= operations; call++) {
        memory_transaction_filesystem fs;
        seed(fs, install, original, first);
        fs.file(update.artifact.path, second);
        REQUIRE(install_release(install, fs).code == install_result_code::installed);
        fs.fail_on_call(call);
        const install_result interrupted = update_installation(update, fs);
        CAPTURE(call);
        CAPTURE(interrupted.detail);
        const bool target_safe = fs.bytes(install.target_path) == first ||
                                 fs.bytes(install.target_path) == second;
        CHECK(target_safe);
        if (interrupted.code != install_result_code::installed && fs.has(update_journal)) {
            fs.clear_failure();
            const install_result recovered = recover_installation(update_journal, fs);
            CAPTURE(recovered.detail);
            const bool explained = recovered.code == install_result_code::installed ||
                recovered.code == install_result_code::cleanup_incomplete ||
                recovered.code == install_result_code::recovery_required ||
                recovered.code == install_result_code::stage_failed ||
                recovered.code == install_result_code::descriptor_failed ||
                recovered.code == install_result_code::record_failed;
            CHECK(explained);
            const bool recovered_safe = fs.bytes(install.target_path) == first ||
                                        fs.bytes(install.target_path) == second;
            CHECK(recovered_safe);
        }
        CHECK(fs.bytes(install.backup_path) == original);
    }
}

TEST_CASE("configuration save preserves unknown keys and backs up imported JSON") {
    const std::string original = "{\"display_name\":\"Old\",\"future\":{\"kept\":true}}\n";
    manager_configuration configuration;
    std::string error;
    REQUIRE(parse_manager_configuration(original, configuration, error));
    configuration.display_name = "Alice";
    configuration.trace_level = "lifecycle";
    configuration_save_request request;
    request.path = "/instance/data/eosr.json";
    request.temporary_path = request.path + ".manager-new";
    request.backup_path = request.path + ".pre-manager.bak";
    request.expected_sha256 = sha256_hex(original);

    memory_transaction_filesystem fs;
    fs.file(request.path, original);
    const configuration_save_result saved = save_manager_configuration(request, configuration, fs);
    CAPTURE(saved.detail);
    REQUIRE(saved.code == configuration_save_code::saved);
    CHECK(fs.bytes(request.backup_path) == original);
    CHECK(fs.bytes(request.path).find("\"future\"") != std::string::npos);
    CHECK(fs.bytes(request.path).find("\"display_name\":\"Alice\"") != std::string::npos);
    CHECK(saved.backup_sha256 == request.expected_sha256);
}

TEST_CASE("configuration save requires confirmation for malformed input and rejects races") {
    manager_configuration configuration = default_manager_configuration();
    configuration_save_request request;
    request.path = "/instance/data/eosr.json";
    request.temporary_path = request.path + ".manager-new";
    request.backup_path = request.path + ".pre-manager.bak";
    request.expected_sha256 = sha256_hex(std::string("malformed"));
    request.existing_was_malformed = true;
    memory_transaction_filesystem fs;
    fs.file(request.path, "malformed");
    CHECK(save_manager_configuration(request, configuration, fs).code ==
          configuration_save_code::malformed_confirmation_required);
    CHECK(fs.bytes(request.path) == "malformed");

    request.replace_confirmed = true;
    fs.file(request.path, "changed after review");
    CHECK(save_manager_configuration(request, configuration, fs).code ==
          configuration_save_code::externally_changed);
    CHECK(fs.bytes(request.path) == "changed after review");
    CHECK_FALSE(fs.has(request.backup_path));
}

TEST_CASE("configuration fault injection never publishes partial JSON") {
    const std::string original = "{\"display_name\":\"Old\",\"unknown\":7}\n";
    manager_configuration configuration;
    std::string error;
    REQUIRE(parse_manager_configuration(original, configuration, error));
    configuration.display_name = "New";
    configuration_save_request request;
    request.path = "/instance/data/eosr.json";
    request.temporary_path = request.path + ".manager-new";
    request.backup_path = request.path + ".pre-manager.bak";
    request.expected_sha256 = sha256_hex(original);

    memory_transaction_filesystem baseline;
    baseline.file(request.path, original);
    REQUIRE(save_manager_configuration(request, configuration, baseline).code ==
            configuration_save_code::saved);
    const std::string expected = baseline.bytes(request.path);
    const std::size_t operations = baseline.calls();
    REQUIRE(operations > 15);

    for (std::size_t call = 1; call <= operations; call++) {
        memory_transaction_filesystem fs;
        fs.file(request.path, original);
        fs.fail_on_call(call);
        const configuration_save_result result = save_manager_configuration(request,
                                                                              configuration, fs);
        CAPTURE(call);
        CAPTURE(result.detail);
        const bool safe = fs.bytes(request.path) == original ||
                          fs.bytes(request.path) == expected;
        CHECK(safe);
        if (fs.has(request.backup_path)) {
            const bool backup_safe = fs.bytes(request.backup_path).empty() ||
                                     fs.bytes(request.backup_path) == original;
            CHECK(backup_safe);
        }
    }
}

TEST_CASE("manager-owned state writes are hash-conditional atomic and reloadable") {
    manager_index index;
    index.selected_release_id = "alpha-one";
    state_save_request request;
    request.path = "/manager/manager.json";
    request.temporary_path = "/manager/.manager.json.stage";
    memory_transaction_filesystem fs;
    const state_save_result initial = save_manager_index(request, index, fs);
    CAPTURE(initial.detail);
    REQUIRE(initial.code == state_save_code::saved);
    manager_index_load_result loaded = load_manager_index(request.path, fs);
    REQUIRE(loaded.code == state_load_code::loaded);
    CHECK(loaded.state.selected_release_id == "alpha-one");
    CHECK(loaded.sha256 == initial.saved_sha256);

    index.selected_release_id = "alpha-two";
    request.expected_sha256 = loaded.sha256;
    REQUIRE(save_manager_index(request, index, fs).code == state_save_code::saved);
    loaded = load_manager_index(request.path, fs);
    REQUIRE(loaded.code == state_load_code::loaded);
    CHECK(loaded.state.selected_release_id == "alpha-two");

    request.expected_sha256 = sha256_hex(std::string("stale"));
    CHECK(save_manager_index(request, index, fs).code == state_save_code::externally_changed);
    CHECK(load_manager_index(request.path, fs).state.selected_release_id == "alpha-two");
}

TEST_CASE("state fault injection never publishes a partial manager document") {
    manager_index old_index;
    old_index.selected_release_id = "old";
    manager_index new_index;
    new_index.selected_release_id = "new";
    const std::string old_bytes = serialize_manager_index(old_index);
    const std::string new_bytes = serialize_manager_index(new_index);
    state_save_request request;
    request.path = "/manager/manager.json";
    request.temporary_path = "/manager/.manager.json.stage";
    request.expected_sha256 = sha256_hex(old_bytes);

    memory_transaction_filesystem baseline;
    baseline.file(request.path, old_bytes);
    REQUIRE(save_manager_index(request, new_index, baseline).code == state_save_code::saved);
    const std::size_t calls = baseline.calls();
    REQUIRE(calls > 10);
    for (std::size_t call = 1; call <= calls; call++) {
        memory_transaction_filesystem fs;
        fs.file(request.path, old_bytes);
        fs.fail_on_call(call);
        const state_save_result saved = save_manager_index(request, new_index, fs);
        CAPTURE(call);
        CAPTURE(saved.detail);
        CHECK((fs.bytes(request.path) == old_bytes || fs.bytes(request.path) == new_bytes));
    }
}
