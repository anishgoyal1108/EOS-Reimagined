#ifndef EOSR_MANAGER_TARGET_INSPECTION_H
#define EOSR_MANAGER_TARGET_INSPECTION_H

#include <cstddef>
#include <string>
#include <vector>

#include "manager/steam_discovery.h"

namespace eosr {
namespace manager {

enum class eos_binary_kind {
    unknown,
    windows_x86_64,
    linux_x86_64
};

enum class target_entry_kind {
    file,
    directory,
    symlink,
    other
};

struct target_directory_entry {
    std::string name;
    target_entry_kind kind;
};

class target_filesystem {
public:
    virtual ~target_filesystem() {}
    virtual bool list_directory(const std::string& path,
                                std::vector<target_directory_entry>& out) = 0;
    virtual discovery_read read_prefix(const std::string& path, std::size_t max_bytes,
                                       std::string& out) = 0;
    virtual discovery_read read_file(const std::string& path, std::size_t max_bytes,
                                     std::string& out) = 0;
    virtual bool canonical_file(const std::string& path, std::string& out) = 0;
};

struct target_scan_limits {
    target_scan_limits();
    std::size_t max_depth;
    std::size_t max_entries;
    std::size_t max_directories;
};

struct eos_target {
    std::string path;
    std::string canonical_path;
    eos_binary_kind kind;
    bool is_symlink;
};

struct target_scan_diagnostic {
    std::string code;
    std::string path;
    std::string detail;
};

struct target_scan_result {
    std::vector<eos_target> targets;
    std::vector<target_scan_diagnostic> diagnostics;
};

enum class target_recommendation_code {
    none,
    unique,
    ambiguous
};

struct target_recommendation {
    target_recommendation();
    target_recommendation_code code;
    std::size_t target_index;
    std::string evidence;
};

eos_binary_kind classify_eos_binary(const std::string& bytes);
target_scan_result scan_eos_targets(const std::string& game_root, target_filesystem& filesystem,
                                    const target_scan_limits& limits = target_scan_limits());
target_recommendation recommend_eos_target(const std::vector<eos_target>& targets,
                                           eos_binary_kind kind);

struct release_artifact {
    std::string path;
    eos_binary_kind kind;
    std::size_t bytes;
    std::string sha256;
};

enum class artifact_verification_code {
    verified,
    invalid_manifest,
    missing,
    unreadable,
    too_large,
    size_mismatch,
    kind_mismatch,
    hash_mismatch
};

struct artifact_verification {
    artifact_verification_code code;
    std::size_t actual_bytes;
    std::string actual_sha256;
    eos_binary_kind actual_kind;
};

artifact_verification verify_release_artifact(const release_artifact& artifact,
                                              target_filesystem& filesystem);

} // namespace manager
} // namespace eosr

#endif
