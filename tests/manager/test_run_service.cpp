#include "doctest.h"

#include <map>
#include <string>
#include <vector>

#include "manager/run_service.h"

using namespace eosr::manager;

namespace {

struct memory_entry {
    memory_entry() : kind(run_entry_kind::file), modified(1) {}
    run_entry_kind kind;
    std::string bytes;
    u64 modified;
};

class memory_run_filesystem : public run_filesystem {
public:
    memory_run_filesystem() : next_(1), open_count(0), fail_writes(false) {}

    void directory(const std::string& path) {
        memory_entry entry;
        entry.kind = run_entry_kind::directory;
        entries[path] = entry;
    }

    void file(const std::string& path, const std::string& bytes, u64 modified = 1) {
        memory_entry entry;
        entry.bytes = bytes;
        entry.modified = modified;
        entries[path] = entry;
    }

    run_io_result list_directory(const std::string& path, std::vector<run_entry>& out) {
        out.clear();
        const std::string prefix = path + "/";
        for (std::map<std::string, memory_entry>::const_iterator it = entries.begin();
             it != entries.end(); ++it) {
            if (it->first.compare(0, prefix.size(), prefix) != 0) {
                continue;
            }
            const std::string rest = it->first.substr(prefix.size());
            if (rest.empty() || rest.find('/') != std::string::npos) {
                continue;
            }
            run_entry value;
            value.name = rest;
            value.kind = it->second.kind;
            value.size = static_cast<u64>(it->second.bytes.size());
            value.modified = it->second.modified;
            out.push_back(value);
        }
        return entries.find(path) == entries.end() ? run_io_result::missing : run_io_result::ok;
    }

    run_io_result read_file(const std::string& path, std::size_t cap, std::string& out) {
        const std::map<std::string, memory_entry>::const_iterator it = entries.find(path);
        if (it == entries.end()) return run_io_result::missing;
        if (it->second.kind != run_entry_kind::file) return run_io_result::io_error;
        if (it->second.bytes.size() > cap) return run_io_result::too_large;
        out = it->second.bytes;
        return run_io_result::ok;
    }

    run_io_result open_read(const std::string& path, run_handle& handle) {
        const std::map<std::string, memory_entry>::const_iterator it = entries.find(path);
        if (it == entries.end() || it->second.kind != run_entry_kind::file)
            return run_io_result::missing;
        handle = next_++;
        reads[handle] = std::make_pair(path, static_cast<std::size_t>(0));
        open_count++;
        return run_io_result::ok;
    }

    run_io_result create_private_directory(const std::string& path) {
        if (entries.find(path) != entries.end()) return run_io_result::exists;
        directory(path);
        return run_io_result::ok;
    }

    run_io_result create_new(const std::string& path, run_handle& handle) {
        if (entries.find(path) != entries.end()) return run_io_result::exists;
        file(path, std::string());
        handle = next_++;
        writes[handle] = path;
        return run_io_result::ok;
    }

    run_io_result read(run_handle handle, unsigned char* data, std::size_t capacity,
                       std::size_t& count) {
        count = 0;
        std::map<run_handle, std::pair<std::string, std::size_t> >::iterator it = reads.find(handle);
        if (it == reads.end()) return run_io_result::io_error;
        const std::string& bytes = entries[it->second.first].bytes;
        if (it->second.second == bytes.size()) return run_io_result::end_of_file;
        count = bytes.size() - it->second.second;
        if (count > capacity) count = capacity;
        for (std::size_t i = 0; i < count; i++) data[i] =
            static_cast<unsigned char>(bytes[it->second.second + i]);
        it->second.second += count;
        return run_io_result::ok;
    }

    run_io_result write(run_handle handle, const unsigned char* data, std::size_t size,
                        std::size_t& count) {
        count = 0;
        std::map<run_handle, std::string>::const_iterator it = writes.find(handle);
        if (it == writes.end() || fail_writes) return run_io_result::io_error;
        entries[it->second].bytes.append(reinterpret_cast<const char*>(data), size);
        count = size;
        return run_io_result::ok;
    }

    run_io_result flush(run_handle handle) {
        return writes.find(handle) != writes.end() ? run_io_result::ok : run_io_result::io_error;
    }

    run_io_result close(run_handle handle) {
        if (reads.erase(handle) != 0) return run_io_result::ok;
        if (writes.erase(handle) != 0) return run_io_result::ok;
        return run_io_result::io_error;
    }

    run_io_result remove_file(const std::string& path) {
        if (path == fail_remove_path) return run_io_result::denied;
        return entries.erase(path) != 0 ? run_io_result::ok : run_io_result::missing;
    }

    run_io_result remove_empty_directory(const std::string& path) {
        const std::string prefix = path + "/";
        for (std::map<std::string, memory_entry>::const_iterator it = entries.begin();
             it != entries.end(); ++it) {
            if (it->first.compare(0, prefix.size(), prefix) == 0) return run_io_result::denied;
        }
        return entries.erase(path) != 0 ? run_io_result::ok : run_io_result::missing;
    }

    run_io_result flush_parent(const std::string&) { return run_io_result::ok; }

    std::map<std::string, memory_entry> entries;
    std::map<run_handle, std::pair<std::string, std::size_t> > reads;
    std::map<run_handle, std::string> writes;
    run_handle next_;
    std::size_t open_count;
    bool fail_writes;
    std::string fail_remove_path;
};

std::string runtime(const std::string& id) {
    return "{\"schema_version\":1,\"emulator_build\":\"eosr 0.1 (abc)\","
           "\"created_utc\":\"2026-07-15T12:00:00Z\",\"run_id\":\"" + id +
           "\",\"instance_label\":\"alice\",\"os\":{\"name\":\"windows\","
           "\"version\":\"10\",\"wine\":\"wine-10\"},\"config\":{"
           "\"display_name\":\"Alice\",\"locale\":\"en\",\"trace_level\":"
           "\"lifecycle\",\"log_level\":\"info\",\"enable_lan\":true,"
           "\"enable_overlay\":false,\"unlock_dlcs\":false,"
           "\"trace_dir\":\"C:\\\\eosr\\\\traces\",\"trace_max_bytes\":65536,"
           "\"trace_max_rotated_files\":8,\"discovery_ports\":[55789,55798],"
           "\"peer_seed_count\":0,\"sources\":{\"display_name\":\"file\","
           "\"locale\":\"default\",\"trace_level\":\"file\","
           "\"log_level\":\"environment\",\"enable_lan\":\"file\","
           "\"trace_dir\":\"default\",\"trace_max_bytes\":\"file\","
           "\"trace_max_rotated_files\":\"file\","
           "\"discovery_ports\":\"default\",\"peer_seeds\":\"default\","
           "\"instance_label\":\"file\",\"enable_overlay\":\"default\","
           "\"unlock_dlcs\":\"default\"}}}";
}

std::string record(const std::string& body, u64 seq) {
    return "{\"v\":1,\"seq\":" + std::to_string(seq) +
           ",\"t\":1,\"pid\":42,\"inst\":\"alice\",\"tid\":\"t#0\"," + body + "}\n";
}

void make_run(memory_run_filesystem& fs, const std::string& id, const std::string& trace) {
    fs.directory("/runs");
    fs.directory("/runs/" + id);
    fs.file("/runs/" + id + "/runtime.json", runtime(id));
    fs.file("/runs/" + id + "/trace.jsonl", trace);
}

} // namespace

TEST_CASE("run indexing streams lifecycle outcomes and ignores only a torn final record") {
    memory_run_filesystem fs;
    std::string trace;
    trace += record("\"kind\":\"meta\",\"event\":\"run_start\"", 1);
    trace += record("\"kind\":\"net\",\"event\":\"listen\",\"port\":55791", 2);
    trace += record("\"kind\":\"net\",\"event\":\"discover\",\"peer\":\"puid#0\"", 3);
    trace += record("\"kind\":\"net\",\"event\":\"handshake\",\"reason\":\"complete\"", 4);
    trace += record("\"kind\":\"net\",\"event\":\"adopt\"", 5);
    trace += record("\"kind\":\"net\",\"event\":\"drop\",\"reason\":\"timeout\"", 6);
    trace += record("\"kind\":\"net\",\"event\":\"p2p_open\"", 7);
    trace += record("\"kind\":\"net\",\"event\":\"p2p_send\",\"bytes\":128", 8);
    trace += record("\"kind\":\"net\",\"event\":\"p2p_receive\",\"bytes\":64", 9);
    trace += record("\"kind\":\"net\",\"event\":\"p2p_close\"", 10);
    trace += record("\"kind\":\"callback\",\"fn\":\"EOS_LobbySearch_Find\","
                    "\"corr\":\"c#1\",\"result\":{\"code\":0,\"name\":\"EOS_Success\"},"
                    "\"payload\":{}", 11);
    trace += record("\"kind\":\"return\",\"fn\":\"EOS_Achievements_QueryDefinitions\","
                    "\"result\":{\"code\":3,\"name\":\"EOS_NotImplemented\"}", 12);
    trace += "{\"v\":1";
    make_run(fs, "run-a", trace);

    run_summary_cache cache;
    const run_index_result indexed = index_diagnostic_runs("/runs", fs, cache);
    REQUIRE(indexed.runs.size() == 1);
    const run_summary& run = indexed.runs[0];
    CHECK(run.sdk_initialized);
    CHECK(run.emulator_build == "eosr 0.1 (abc)");
    CHECK(run.effective_config_present);
    CHECK(run.effective_display_name == "Alice");
    CHECK(run.effective_locale == "en");
    CHECK(run.effective_log_level == "info");
    CHECK(run.effective_trace_dir == "C:\\eosr\\traces");
    CHECK(run.effective_trace_max_bytes == 65536);
    CHECK(run.effective_trace_max_rotated_files == 8);
    REQUIRE(run.effective_sources.find("display_name") != run.effective_sources.end());
    REQUIRE(run.effective_sources.find("log_level") != run.effective_sources.end());
    CHECK(run.effective_sources.find("display_name")->second == "file");
    CHECK(run.effective_sources.find("log_level")->second == "environment");
    CHECK(run.effective_enable_lan);
    CHECK(run.effective_discovery_first == 55789);
    CHECK(run.effective_discovery_last == 55798);
    CHECK(run.discovery_port == 55791);
    CHECK(run.discover_count == 1);
    CHECK(run.handshake_count == 1);
    CHECK(run.adopt_count == 1);
    CHECK(run.drop_count == 1);
    CHECK(run.drop_reasons.find("timeout")->second == 1);
    CHECK(run.p2p_open_count == 1);
    CHECK(run.p2p_close_count == 1);
    CHECK(run.p2p_bytes_sent == 128);
    CHECK(run.p2p_bytes_received == 64);
    REQUIRE(run.operations.size() == 1);
    CHECK(run.operations[0].function == "EOS_LobbySearch_Find");
    CHECK(run.operations[0].successes == 1);
    REQUIRE(run.stubbed_functions.size() == 1);
    CHECK(run.stubbed_functions[0] == "EOS_Achievements_QueryDefinitions");
    CHECK(run.torn_tail_records == 1);
    CHECK(run.status == diagnostic_run_status::incomplete);
}

TEST_CASE("completed runs are cached by file metadata and invalidated when it changes") {
    memory_run_filesystem fs;
    make_run(fs, "run-b", record("\"kind\":\"meta\",\"event\":\"run_start\"", 1) +
                              record("\"kind\":\"meta\",\"event\":\"shutdown\"", 2));
    run_summary_cache cache;
    run_index_result first = index_diagnostic_runs("/runs", fs, cache);
    REQUIRE(first.runs.size() == 1);
    CHECK(first.runs[0].status == diagnostic_run_status::completed);
    const std::size_t opened = fs.open_count;
    run_index_result second = index_diagnostic_runs("/runs", fs, cache);
    CHECK(second.runs.size() == 1);
    CHECK(fs.open_count == opened);
    fs.entries["/runs/run-b/trace.jsonl"].modified++;
    index_diagnostic_runs("/runs", fs, cache);
    CHECK(fs.open_count > opened);
}

TEST_CASE("old runs remain readable while invalid effective sources are rejected") {
    memory_run_filesystem old;
    old.directory("/runs");
    old.directory("/runs/run-old");
    std::string old_runtime = runtime("run-old");
    const std::string marker = ",\"sources\":";
    const std::string::size_type source_at = old_runtime.find(marker);
    REQUIRE(source_at != std::string::npos);
    old_runtime.erase(source_at, old_runtime.size() - source_at - 2);
    old.file("/runs/run-old/runtime.json", old_runtime);
    old.file("/runs/run-old/trace.jsonl",
             record("\"kind\":\"meta\",\"event\":\"run_start\"", 1));
    run_summary_cache old_cache;
    const run_index_result old_result = index_diagnostic_runs("/runs", old, old_cache);
    REQUIRE(old_result.runs.size() == 1);
    CHECK(old_result.runs[0].effective_sources.empty());

    memory_run_filesystem invalid;
    invalid.directory("/runs");
    invalid.directory("/runs/run-invalid-source");
    std::string invalid_runtime = runtime("run-invalid-source");
    const std::string valid = "\"display_name\":\"file\"";
    const std::string::size_type value_at = invalid_runtime.find(valid);
    REQUIRE(value_at != std::string::npos);
    invalid_runtime.replace(value_at, valid.size(), "\"display_name\":\"registry\"");
    invalid.file("/runs/run-invalid-source/runtime.json", invalid_runtime);
    invalid.file("/runs/run-invalid-source/trace.jsonl", std::string());
    run_summary_cache invalid_cache;
    const run_index_result invalid_result =
        index_diagnostic_runs("/runs", invalid, invalid_cache);
    CHECK(invalid_result.runs.empty());
    REQUIRE(invalid_result.diagnostics.size() == 1);
    CHECK(invalid_result.diagnostics[0].code == "runtime_invalid");
}

TEST_CASE("a complete invalid or oversized trace line is not mistaken for a torn tail") {
    memory_run_filesystem invalid;
    make_run(invalid, "bad-json", "not-json\n");
    run_summary_cache cache;
    run_index_result a = index_diagnostic_runs("/runs", invalid, cache);
    REQUIRE(a.runs.size() == 1);
    CHECK(a.runs[0].trace_invalid);
    CHECK(a.runs[0].torn_tail_records == 0);

    memory_run_filesystem large;
    make_run(large, "too-wide", std::string(60001, 'x') + "\n");
    run_summary_cache cache2;
    run_index_result b = index_diagnostic_runs("/runs", large, cache2);
    REQUIRE(b.runs.size() == 1);
    CHECK(b.runs[0].trace_invalid);
    CHECK(b.runs[0].diagnostic.find("oversized") != std::string::npos);
}

TEST_CASE("support bundles sanitize owned metadata copy only valid trace records and report exclusions") {
    memory_run_filesystem fs;
    make_run(fs, "run-c", record("\"kind\":\"meta\",\"event\":\"run_start\"", 1) +
                              "{\"v\":1");
    fs.file("/runs/run-c/launch.json",
            "{\"username\":\"secret\",\"executable\":\"C:\\\\Games\\\\game.exe\"}");
    fs.file("/runs/run-c/stdout.log", "private output");
    fs.file("/runs/run-c/profile.key", "private identity");
    fs.file("/runs/run-c/unknown.bin", "unknown");

    support_bundle_request request;
    request.run_directory = "/runs/run-c";
    request.bundle_directory = "/runs/run-c-support";
    request.generated_utc = "2026-07-15T13:00:00Z";
    const support_bundle_result result = create_support_bundle(request, fs);
    REQUIRE(result.code == support_bundle_code::created);
    CHECK(result.shareable);
    CHECK(fs.entries.find("/runs/run-c-support/trace.jsonl") != fs.entries.end());
    const std::string sanitized_trace =
        fs.entries["/runs/run-c-support/trace.jsonl"].bytes;
    CHECK(sanitized_trace.find("\"inst\":\"alice\"") == std::string::npos);
    CHECK(sanitized_trace.find("\"inst\":\"instance#0\"") != std::string::npos);
    CHECK(sanitized_trace.find("\"event\":\"run_start\"") != std::string::npos);
    CHECK(sanitized_trace.find("\n\n") == std::string::npos);
    REQUIRE(!sanitized_trace.empty());
    CHECK(sanitized_trace[sanitized_trace.size() - 1] == '\n');
    const std::string summary = fs.entries["/runs/run-c-support/summary.json"].bytes;
    CHECK(summary.find("secret") == std::string::npos);
    CHECK(summary.find("\"instance_label\":\"alice\"") == std::string::npos);
    CHECK(summary.find("\"instance_label\":\"instance#0\"") != std::string::npos);
    CHECK(summary.find("\"instance_label\":\"file\"") != std::string::npos);
    CHECK(summary.find("C:\\\\Games") == std::string::npos);
    CHECK(summary.find("C:\\\\eosr") == std::string::npos);
    CHECK(summary.find("game.exe") != std::string::npos);
    CHECK(summary.find("profile.key") != std::string::npos);
    CHECK(summary.find("stdout.log") != std::string::npos);
    CHECK(summary.find("unknown.bin") != std::string::npos);
    CHECK(summary.find("torn_tail_records_dropped\":1") != std::string::npos);
}

TEST_CASE("bundle failures remove every partial output and never claim shareability") {
    memory_run_filesystem fs;
    make_run(fs, "run-d", record("\"kind\":\"meta\",\"event\":\"run_start\"", 1));
    fs.fail_writes = true;
    support_bundle_request request;
    request.run_directory = "/runs/run-d";
    request.bundle_directory = "/runs/run-d-support";
    request.generated_utc = "2026-07-15T13:00:00Z";
    const support_bundle_result result = create_support_bundle(request, fs);
    CHECK(result.code != support_bundle_code::created);
    CHECK_FALSE(result.shareable);
    for (std::map<std::string, memory_entry>::const_iterator it = fs.entries.begin();
         it != fs.entries.end(); ++it) {
        CHECK(it->first.find("/runs/run-d-support") != 0);
    }
}

TEST_CASE("owned tree removal preflights the whole direct child before mutation") {
    memory_run_filesystem fs;
    fs.directory("/runs");
    fs.directory("/runs/run-e");
    fs.directory("/runs/run-e/nested");
    fs.file("/runs/run-e/runtime.json", "{}");
    fs.file("/runs/run-e/nested/trace.jsonl", "{}\n");
    owned_tree_remove_request request;
    request.parent_directory = "/runs";
    request.directory = "/runs/run-e";
    const owned_tree_remove_result removed = remove_owned_tree(request, fs);
    REQUIRE(removed.code == owned_tree_remove_code::removed);
    CHECK(removed.files_removed == 2);
    CHECK(removed.directories_removed == 2);
    CHECK(fs.entries.count("/runs") == 1);
    CHECK(fs.entries.size() == 1);
}

TEST_CASE("owned tree removal rejects escapes symlinks and bounds before deleting anything") {
    memory_run_filesystem fs;
    fs.directory("/runs");
    fs.directory("/runs/run-f");
    fs.file("/runs/run-f/trace.jsonl", "{}\n");
    memory_entry link;
    link.kind = run_entry_kind::symlink;
    fs.entries["/runs/run-f/profile.key"] = link;

    owned_tree_remove_request request;
    request.parent_directory = "/runs";
    request.directory = "/runs/run-f";
    CHECK(remove_owned_tree(request, fs).code == owned_tree_remove_code::unsafe_entry);
    CHECK(fs.entries.count("/runs/run-f/trace.jsonl") == 1);

    request.directory = "/runs/run-f/../other";
    CHECK(remove_owned_tree(request, fs).code == owned_tree_remove_code::invalid_request);
    request.directory = "/runs/run-f";
    fs.entries.erase("/runs/run-f/profile.key");
    fs.file("/runs/run-f/runtime.json", "{}");
    request.max_entries = 1;
    CHECK(remove_owned_tree(request, fs).code == owned_tree_remove_code::bounds_exceeded);
    CHECK(fs.entries.count("/runs/run-f/trace.jsonl") == 1);
}

TEST_CASE("owned tree removal reports a retryable partial cleanup after an injected failure") {
    memory_run_filesystem fs;
    fs.directory("/runs");
    fs.directory("/runs/run-g");
    fs.file("/runs/run-g/a.json", "{}");
    fs.file("/runs/run-g/b.json", "{}");
    fs.fail_remove_path = "/runs/run-g/b.json";
    owned_tree_remove_request request;
    request.parent_directory = "/runs";
    request.directory = "/runs/run-g";
    const owned_tree_remove_result removed = remove_owned_tree(request, fs);
    CHECK(removed.code == owned_tree_remove_code::cleanup_incomplete);
    CHECK(removed.files_removed == 1);
    CHECK(fs.entries.count("/runs/run-g") == 1);
    CHECK(fs.entries.count("/runs/run-g/b.json") == 1);
}
