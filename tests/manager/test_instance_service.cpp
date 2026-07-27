#include "doctest.h"

#include <map>
#include <set>
#include <string>

#include "manager/instance_service.h"
#include "manager/operation_log.h"

using namespace eosr::manager;

namespace {

struct instance_store {
    std::set<std::string> directories;
    std::map<std::string, std::string> files;
};

class instance_directory_fake : public run_filesystem {
public:
    explicit instance_directory_fake(instance_store& store) : store_(store), fail_create(false) {}
    run_io_result list_directory(const std::string& path, std::vector<run_entry>& out) {
        out.clear();
        if (store_.directories.count(path) == 0) return run_io_result::missing;
        const std::string prefix = path + "/";
        for (std::set<std::string>::const_iterator it = store_.directories.begin();
             it != store_.directories.end(); ++it) {
            if (it->compare(0, prefix.size(), prefix) == 0) {
                const std::string rest = it->substr(prefix.size());
                if (!rest.empty() && rest.find('/') == std::string::npos) {
                    run_entry entry;
                    entry.name = rest;
                    entry.kind = run_entry_kind::directory;
                    out.push_back(entry);
                }
            }
        }
        return run_io_result::ok;
    }
    run_io_result create_private_directory(const std::string& path) {
        if (fail_create) return run_io_result::denied;
        return store_.directories.insert(path).second ? run_io_result::ok : run_io_result::exists;
    }
    run_io_result remove_empty_directory(const std::string& path) {
        const std::string prefix = path + "/";
        for (std::set<std::string>::const_iterator it = store_.directories.begin();
             it != store_.directories.end(); ++it)
            if (it->compare(0, prefix.size(), prefix) == 0) return run_io_result::denied;
        for (std::map<std::string, std::string>::const_iterator it = store_.files.begin();
             it != store_.files.end(); ++it)
            if (it->first.compare(0, prefix.size(), prefix) == 0) return run_io_result::denied;
        return store_.directories.erase(path) ? run_io_result::ok : run_io_result::missing;
    }
    run_io_result flush_parent(const std::string&) { return run_io_result::ok; }
    run_io_result read_file(const std::string&, std::size_t, std::string&) {
        return run_io_result::io_error;
    }
    run_io_result open_read(const std::string&, run_handle&) { return run_io_result::io_error; }
    run_io_result create_new(const std::string&, run_handle&) { return run_io_result::io_error; }
    run_io_result read(run_handle, unsigned char*, std::size_t, std::size_t&) {
        return run_io_result::io_error;
    }
    run_io_result write(run_handle, const unsigned char*, std::size_t, std::size_t&) {
        return run_io_result::io_error;
    }
    run_io_result flush(run_handle) { return run_io_result::io_error; }
    run_io_result close(run_handle) { return run_io_result::io_error; }
    run_io_result remove_file(const std::string&) { return run_io_result::io_error; }
    instance_store& store_;
    bool fail_create;
};

class instance_file_fake : public transaction_filesystem {
public:
    explicit instance_file_fake(instance_store& store)
        : store_(store), next_(1), fail_write(false) {}
    transaction_io_result inspect(const std::string& path, transaction_file_info& out) {
        out = transaction_file_info();
        if (store_.files.count(path) == 0) return transaction_io_result::missing;
        out.exists = true; out.regular = true; out.writable = true; out.mode = 0600;
        return transaction_io_result::ok;
    }
    transaction_io_result open_read(const std::string& path, transaction_handle& out) {
        if (store_.files.count(path) == 0) return transaction_io_result::missing;
        out = next_++; reads_[out] = std::make_pair(path, static_cast<std::size_t>(0));
        return transaction_io_result::ok;
    }
    transaction_io_result create_new(const std::string& path, transaction_handle& out) {
        if (store_.files.count(path) != 0) return transaction_io_result::exists;
        store_.files[path] = std::string();
        out = next_++; writes_[out] = path;
        return transaction_io_result::ok;
    }
    transaction_io_result read(transaction_handle handle, unsigned char* data,
                               std::size_t capacity, std::size_t& count) {
        count = 0;
        std::map<transaction_handle, std::pair<std::string, std::size_t> >::iterator it =
            reads_.find(handle);
        if (it == reads_.end()) return transaction_io_result::io_error;
        const std::string& bytes = store_.files[it->second.first];
        if (it->second.second == bytes.size()) return transaction_io_result::end_of_file;
        count = bytes.size() - it->second.second;
        if (count > capacity) count = capacity;
        for (std::size_t i = 0; i < count; i++)
            data[i] = static_cast<unsigned char>(bytes[it->second.second + i]);
        it->second.second += count;
        return transaction_io_result::ok;
    }
    transaction_io_result write(transaction_handle handle, const unsigned char* data,
                                std::size_t size, std::size_t& count) {
        count = 0;
        if (fail_write) return transaction_io_result::io_error;
        const std::map<transaction_handle, std::string>::const_iterator it = writes_.find(handle);
        if (it == writes_.end()) return transaction_io_result::io_error;
        store_.files[it->second].append(reinterpret_cast<const char*>(data), size);
        count = size;
        return transaction_io_result::ok;
    }
    transaction_io_result flush(transaction_handle handle) {
        return writes_.count(handle) ? transaction_io_result::ok : transaction_io_result::io_error;
    }
    transaction_io_result close(transaction_handle handle) {
        if (reads_.erase(handle) || writes_.erase(handle)) return transaction_io_result::ok;
        return transaction_io_result::io_error;
    }
    transaction_io_result set_mode(const std::string& path, unsigned int) {
        return store_.files.count(path) ? transaction_io_result::ok : transaction_io_result::missing;
    }
    transaction_io_result rename_no_replace(const std::string& from, const std::string& to) {
        if (store_.files.count(from) == 0) return transaction_io_result::missing;
        if (store_.files.count(to) != 0) return transaction_io_result::exists;
        store_.files[to] = store_.files[from]; store_.files.erase(from);
        return transaction_io_result::ok;
    }
    transaction_io_result rename_replace(const std::string& from, const std::string& to) {
        if (store_.files.count(from) == 0) return transaction_io_result::missing;
        store_.files[to] = store_.files[from]; store_.files.erase(from);
        return transaction_io_result::ok;
    }
    transaction_io_result remove(const std::string& path) {
        return store_.files.erase(path) ? transaction_io_result::ok : transaction_io_result::missing;
    }
    transaction_io_result flush_parent(const std::string&) { return transaction_io_result::ok; }
    transaction_io_result canonical_file(const std::string&, std::string&) {
        return transaction_io_result::io_error;
    }
    transaction_io_result in_use(const std::string&, bool&) {
        return transaction_io_result::io_error;
    }
    instance_store& store_;
    transaction_handle next_;
    bool fail_write;
    std::map<transaction_handle, std::pair<std::string, std::size_t> > reads_;
    std::map<transaction_handle, std::string> writes_;
};

game_state empty_game() {
    game_state game;
    game.id = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
    game.steam_app_id = "632360";
    game.display_name = "Risk of Rain 2";
    game.install_root = "/games/Risk of Rain 2";
    game.install_kind = steam_install_kind::native_linux;
    return game;
}

instance_create_request create_request(const std::string& id, const std::string& slug,
                                       const std::string& name) {
    instance_create_request request;
    request.id = id;
    request.slug = slug;
    request.display_name = name;
    request.instances_root = "/manager/games/aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa/instances";
    return request;
}

} // namespace

TEST_CASE("named instances use stable ids distinct slugs and isolated data directories") {
    game_state game = empty_game();
    const instance_operation first = create_instance_model(
        game, create_request("11111111111111111111111111111111", "alice", "Alice"));
    REQUIRE(first.code == instance_operation_code::created);
    const instance_operation second = create_instance_model(
        game, create_request("22222222222222222222222222222222", "bob", "Bob"));
    REQUIRE(second.code == instance_operation_code::created);
    REQUIRE(game.instances.size() == 2);
    CHECK(game.instances[0].data_dir != game.instances[1].data_dir);
    CHECK(game.instances[0].data_dir.find("/alice/data") != std::string::npos);
    CHECK(game.instances[1].data_dir.find("/bob/data") != std::string::npos);
    CHECK(game.active_instance_id == "11111111111111111111111111111111");
}

TEST_CASE("provisioning publishes a private isolated configuration before changing the model") {
    game_state game = empty_game();
    instance_store store;
    store.directories.insert("/manager/games/aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa/instances");
    instance_directory_fake directories(store);
    instance_file_fake files(store);
    instance_provision_request request;
    request.model = create_request("11111111111111111111111111111111", "alice", "Alice");
    request.configuration = new_instance_configuration("Alice", "alice", true);
    const instance_provision_result result =
        provision_instance(game, request, directories, files);
    CAPTURE(result.detail);
    REQUIRE(result.code == instance_provision_code::provisioned);
    REQUIRE(game.instances.size() == 1);
    CHECK(store.directories.count("/manager/games/aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa/instances/alice") == 1);
    CHECK(store.directories.count(game.instances[0].data_dir) == 1);
    CHECK(store.files.count(game.instances[0].data_dir + "/eosr.json") == 1);
    CHECK(store.files[game.instances[0].data_dir + "/eosr.json"].find(
              "\"trace_level\":\"lifecycle\"") != std::string::npos);
}

TEST_CASE("failed instance provisioning cleans owned paths and leaves the model unchanged") {
    game_state game = empty_game();
    instance_store store;
    const std::string root = "/manager/games/aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa/instances";
    store.directories.insert(root);
    instance_directory_fake directories(store);
    instance_file_fake files(store);
    files.fail_write = true;
    instance_provision_request request;
    request.model = create_request("11111111111111111111111111111111", "alice", "Alice");
    request.configuration = new_instance_configuration("Alice", "alice", true);
    const instance_provision_result result =
        provision_instance(game, request, directories, files);
    CHECK(result.code == instance_provision_code::configuration_failed);
    CHECK(game.instances.empty());
    CHECK(store.directories.size() == 1);
    CHECK(store.files.empty());

    store.directories.insert(root + "/alice");
    files.fail_write = false;
    const instance_provision_result collision =
        provision_instance(game, request, directories, files);
    CHECK(collision.code == instance_provision_code::path_exists);
    CHECK(game.instances.empty());
    CHECK(store.directories.count(root + "/alice") == 1);
}

TEST_CASE("instance ids slugs and paths cannot collide") {
    game_state game = empty_game();
    REQUIRE(create_instance_model(
        game, create_request("11111111111111111111111111111111", "alice", "Alice")).code ==
        instance_operation_code::created);
    CHECK(create_instance_model(
        game, create_request("22222222222222222222222222222222", "alice", "Other")).code ==
        instance_operation_code::slug_exists);
    CHECK(create_instance_model(
        game, create_request("11111111111111111111111111111111", "other", "Other")).code ==
        instance_operation_code::id_exists);
    CHECK(create_instance_model(
        game, create_request("33333333333333333333333333333333", "../escape", "Other")).code ==
        instance_operation_code::invalid_request);
    CHECK(game.instances.size() == 1);
}

TEST_CASE("rename and selection never change instance identity or storage") {
    game_state game = empty_game();
    REQUIRE(create_instance_model(
        game, create_request("11111111111111111111111111111111", "alice", "Alice")).code ==
        instance_operation_code::created);
    REQUIRE(create_instance_model(
        game, create_request("22222222222222222222222222222222", "bob", "Bob")).code ==
        instance_operation_code::created);
    const std::string data_dir = game.instances[1].data_dir;
    CHECK(rename_instance_model(game, game.instances[1].id, "Bobby").code ==
          instance_operation_code::renamed);
    CHECK(game.instances[1].display_name == "Bobby");
    CHECK(game.instances[1].slug == "bob");
    CHECK(game.instances[1].data_dir == data_dir);
    CHECK(select_instance_model(game, game.instances[1].id).code ==
          instance_operation_code::selected);
    CHECK(game.active_instance_id == game.instances[1].id);
}

TEST_CASE("deleting a profile requires an explicit identity disposition") {
    game_state game = empty_game();
    REQUIRE(create_instance_model(
        game, create_request("11111111111111111111111111111111", "alice", "Alice")).code ==
        instance_operation_code::created);
    REQUIRE(create_instance_model(
        game, create_request("22222222222222222222222222222222", "bob", "Bob")).code ==
        instance_operation_code::created);
    instance_delete_request deletion;
    deletion.id = game.instances[0].id;
    deletion.profile_key_exists = true;
    deletion.confirmed = true;
    deletion.identity = identity_disposition::unspecified;
    CHECK(delete_instance_model(game, deletion).code ==
          instance_operation_code::identity_decision_required);
    CHECK(game.instances.size() == 2);

    deletion.identity = identity_disposition::retain_directory;
    REQUIRE(delete_instance_model(game, deletion).code == instance_operation_code::removed_retained);
    REQUIRE(game.instances.size() == 1);
    CHECK(game.active_instance_id == game.instances[0].id);
}

TEST_CASE("configuration presets never copy display identity or instance labels") {
    manager_configuration target = default_manager_configuration();
    target.display_name = "Alice";
    target.instance_label = "alice";
    manager_configuration preset = default_manager_configuration();
    preset.display_name = "Preset Identity";
    preset.instance_label = "preset-key";
    preset.locale = "fr";
    preset.trace_level = "full";
    preset.discovery_first = 60000;
    preset.discovery_last = 60005;
    apply_configuration_preset(target, preset);
    CHECK(target.display_name == "Alice");
    CHECK(target.instance_label == "alice");
    CHECK(target.locale == "fr");
    CHECK(target.trace_level == "full");
    CHECK(target.discovery_first == 60000);
}

TEST_CASE("saving a network display name never renames the manager instance label") {
    managed_instance instance;
    instance.display_name = "David's Windows PC";
    instance.config_sha256 = "old-config";
    instance.config_backup_sha256 = "owned-backup";

    configuration_save_result saved;
    saved.saved_sha256 = "new-config";
    saved.backup_sha256 = "new-backup";
    apply_saved_configuration_metadata(instance, saved);
    CHECK(instance.display_name == "David's Windows PC");
    CHECK(instance.config_sha256 == "new-config");
    CHECK(instance.config_backup_sha256 == "new-backup");

    configuration_save_result saved_without_new_backup;
    saved_without_new_backup.saved_sha256 = "newer-config";
    apply_saved_configuration_metadata(instance, saved_without_new_backup);
    CHECK(instance.display_name == "David's Windows PC");
    CHECK(instance.config_sha256 == "newer-config");
    CHECK(instance.config_backup_sha256 == "new-backup");
}

TEST_CASE("new alpha instances default to lifecycle tracing") {
    const manager_configuration alpha = new_instance_configuration("Alice", "alice", true);
    CHECK(alpha.display_name == "Alice");
    CHECK(alpha.instance_label == "alice");
    CHECK(alpha.trace_level == "lifecycle");
    const manager_configuration stable = new_instance_configuration("Alice", "alice", false);
    CHECK(stable.trace_level == "off");
}

TEST_CASE("identity exports use exclusive verified copies and never overwrite a destination") {
    instance_store store;
    store.files["/manager/alice/profile.key"] = "private-identity";
    instance_file_fake files(store);
    identity_export_request request;
    request.source_path = "/manager/alice/profile.key";
    request.destination_path = "/backups/alice-profile.key";
    const identity_export_result exported = export_instance_identity(request, files);
    REQUIRE(exported.code == identity_export_code::exported);
    CHECK(exported.sha256.size() == 64);
    CHECK(store.files[request.destination_path] == "private-identity");

    store.files[request.destination_path] = "do-not-overwrite";
    CHECK(export_instance_identity(request, files).code ==
          identity_export_code::destination_exists);
    CHECK(store.files[request.destination_path] == "do-not-overwrite");
}

TEST_CASE("failed identity export removes only its exclusively claimed partial copy") {
    instance_store store;
    store.files["/manager/alice/profile.key"] = "private-identity";
    instance_file_fake files(store);
    files.fail_write = true;
    identity_export_request request;
    request.source_path = "/manager/alice/profile.key";
    request.destination_path = "/backups/alice-profile.key";
    CHECK(export_instance_identity(request, files).code == identity_export_code::copy_failed);
    CHECK(store.files.count(request.source_path) == 1);
    CHECK(store.files.count(request.destination_path) == 0);
}

TEST_CASE("manager operation logs are immutable structured metadata without free-form secrets") {
    instance_store store;
    instance_file_fake files(store);
    operation_log_entry entry;
    entry.created_utc = "2026-07-15T12:00:00Z";
    entry.action = "install";
    entry.object_id = "11111111111111111111111111111111";
    entry.code = "installed";
    entry.success = true;
    const std::string path = "/manager/logs/op.json";
    CHECK(write_operation_log(path, entry, files).code == operation_log_code::written);
    CHECK(store.files[path].find("\"action\":\"install\"") != std::string::npos);
    CHECK(store.files[path].find("detail") == std::string::npos);
    CHECK(write_operation_log(path, entry, files).code ==
          operation_log_code::destination_exists);
}

TEST_CASE("manager operation log write failures remove their exclusively claimed partial file") {
    instance_store store;
    instance_file_fake files(store);
    files.fail_write = true;
    operation_log_entry entry;
    entry.created_utc = "2026-07-15T12:00:00Z";
    entry.action = "restore";
    entry.object_id = "11111111111111111111111111111111";
    entry.code = "write_failed";
    entry.success = false;
    const std::string path = "/manager/logs/op.json";
    CHECK(write_operation_log(path, entry, files).code == operation_log_code::write_failed);
    CHECK(store.files.count(path) == 0);
}
