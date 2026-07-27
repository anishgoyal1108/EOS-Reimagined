#ifndef EOSR_MANAGER_RUN_SERVICE_H
#define EOSR_MANAGER_RUN_SERVICE_H

#include <cstddef>
#include <map>
#include <string>
#include <vector>

#include "common/types.h"

namespace eosr {
namespace manager {

typedef u64 run_handle;

enum class run_io_result {
    ok,
    missing,
    exists,
    denied,
    end_of_file,
    too_large,
    io_error
};

enum class run_entry_kind { file, directory, symlink, other };

struct run_entry {
    run_entry();
    std::string name;
    run_entry_kind kind;
    u64 size;
    u64 modified;
};

// A deliberately small, injectable filesystem surface. Trace input and bundle output both use
// bounded streams; no implementation is allowed to turn a diagnostic run into one giant string.
class run_filesystem {
public:
    virtual ~run_filesystem() {}
    virtual run_io_result list_directory(const std::string& path,
                                         std::vector<run_entry>& out) = 0;
    virtual run_io_result read_file(const std::string& path, std::size_t cap,
                                    std::string& out) = 0;
    virtual run_io_result open_read(const std::string& path, run_handle& out) = 0;
    virtual run_io_result create_private_directory(const std::string& path) = 0;
    virtual run_io_result create_new(const std::string& path, run_handle& out) = 0;
    virtual run_io_result read(run_handle handle, unsigned char* data, std::size_t capacity,
                               std::size_t& count) = 0;
    virtual run_io_result write(run_handle handle, const unsigned char* data, std::size_t size,
                                std::size_t& count) = 0;
    virtual run_io_result flush(run_handle handle) = 0;
    virtual run_io_result close(run_handle handle) = 0;
    virtual run_io_result remove_file(const std::string& path) = 0;
    virtual run_io_result remove_empty_directory(const std::string& path) = 0;
    virtual run_io_result flush_parent(const std::string& path) = 0;
};

enum class diagnostic_run_status { active, completed, incomplete };

struct operation_outcome {
    operation_outcome();
    std::string function;
    u64 total;
    u64 successes;
    u64 failures;
    std::map<std::string, u64> results;
};

struct trace_file_signature {
    std::string name;
    u64 size;
    u64 modified;
    std::string sha256;
    u64 records;
};

struct run_summary {
    run_summary();
    std::string run_id;
    std::string directory;
    std::string manager_instance_id;
    std::string emulator_build;
    std::string created_utc;
    std::string instance_label;
    std::string os_name;
    std::string os_version;
    std::string wine_version;
    std::string trace_level;
    std::string effective_display_name;
    std::string effective_locale;
    std::string effective_log_level;
    std::string effective_trace_dir;
    u64 effective_trace_max_bytes;
    u64 effective_trace_max_rotated_files;
    bool effective_enable_lan;
    bool effective_enable_overlay;
    bool effective_unlock_dlcs;
    u64 effective_discovery_first;
    u64 effective_discovery_last;
    u64 effective_peer_seed_count;
    std::map<std::string, std::string> effective_sources;
    bool effective_config_present;
    diagnostic_run_status status;
    bool sdk_initialized;
    bool trace_invalid;
    u64 discovery_port;
    u64 discover_count;
    u64 handshake_count;
    u64 adopt_count;
    u64 drop_count;
    std::map<std::string, u64> handshake_reasons;
    std::map<std::string, u64> drop_reasons;
    u64 p2p_open_count;
    u64 p2p_close_count;
    u64 p2p_bytes_sent;
    u64 p2p_bytes_received;
    u64 torn_tail_records;
    std::vector<operation_outcome> operations;
    std::vector<std::string> stubbed_functions;
    std::vector<trace_file_signature> trace_files;
    std::string diagnostic;
};

struct run_index_diagnostic {
    std::string code;
    std::string path;
    std::string detail;
};

struct run_index_result {
    std::vector<run_summary> runs;
    std::vector<run_index_diagnostic> diagnostics;
    u64 cache_hits;
};

struct run_cache_entry {
    std::string directory;
    std::vector<run_entry> metadata;
    run_summary summary;
};

struct run_summary_cache {
    std::vector<run_cache_entry> entries;
};

run_index_result index_diagnostic_runs(const std::string& trace_root, run_filesystem& filesystem,
                                       run_summary_cache& cache,
                                       const std::vector<std::string>& active_run_ids =
                                           std::vector<std::string>());

struct support_bundle_request {
    std::string run_directory;
    std::string bundle_directory;
    std::string generated_utc;
};

enum class support_bundle_code {
    created,
    invalid_request,
    run_missing,
    bundle_exists,
    source_invalid,
    create_failed,
    write_failed,
    cleanup_incomplete
};

struct bundle_file_result {
    std::string name;
    u64 records;
    std::string sha256;
};

struct bundle_exclusion {
    std::string name;
    std::string reason;
};

struct support_bundle_result {
    support_bundle_result();
    support_bundle_code code;
    bool shareable;
    std::string directory;
    std::vector<bundle_file_result> included_trace_files;
    std::vector<bundle_exclusion> excluded_files;
    u64 torn_tail_records_dropped;
    std::string detail;
};

support_bundle_result create_support_bundle(const support_bundle_request& request,
                                            run_filesystem& filesystem);

struct owned_tree_remove_request {
    owned_tree_remove_request();
    std::string parent_directory;
    std::string directory;
    std::size_t max_depth;
    std::size_t max_entries;
};

enum class owned_tree_remove_code {
    removed,
    invalid_request,
    missing,
    unsafe_entry,
    bounds_exceeded,
    preflight_failed,
    cleanup_incomplete
};

struct owned_tree_remove_result {
    owned_tree_remove_result();
    owned_tree_remove_code code;
    std::size_t files_removed;
    std::size_t directories_removed;
    std::string detail;
};

// Removes only a direct child of parent_directory. The entire bounded tree is preflighted before
// mutation, and any symlink or non-file/non-directory entry rejects the operation without changes.
owned_tree_remove_result remove_owned_tree(const owned_tree_remove_request& request,
                                           run_filesystem& filesystem);

} // namespace manager
} // namespace eosr

#endif
