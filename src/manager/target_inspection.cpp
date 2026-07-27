#include "manager/target_inspection.h"

#include <algorithm>
#include <limits>
#include <set>

#include "manager/sha256.h"

namespace eosr {
namespace manager {

namespace {

const std::size_t max_binary_bytes = 256 * 1024 * 1024;

std::string join_path(const std::string& parent, const std::string& child) {
    if (parent.empty()) {
        return child;
    }
    const char last = parent[parent.size() - 1];
    return (last == '/' || last == '\\') ? parent + child : parent + "/" + child;
}

bool ascii_equal_case_insensitive(const std::string& left, const char* right) {
    const std::string expected = right;
    if (left.size() != expected.size()) {
        return false;
    }
    for (std::size_t i = 0; i < left.size(); i++) {
        char a = left[i];
        char b = expected[i];
        if (a >= 'A' && a <= 'Z') {
            a = static_cast<char>(a - 'A' + 'a');
        }
        if (b >= 'A' && b <= 'Z') {
            b = static_cast<char>(b - 'A' + 'a');
        }
        if (a != b) {
            return false;
        }
    }
    return true;
}

eos_binary_kind expected_kind_for_name(const std::string& name) {
    if (ascii_equal_case_insensitive(name, "EOSSDK-Win64-Shipping.dll")) {
        return eos_binary_kind::windows_x86_64;
    }
    if (ascii_equal_case_insensitive(name, "libEOSSDK-Linux-Shipping.so")) {
        return eos_binary_kind::linux_x86_64;
    }
    return eos_binary_kind::unknown;
}

bool safe_leaf_name(const std::string& name) {
    if (name.empty() || name == "." || name == "..") {
        return false;
    }
    for (std::size_t i = 0; i < name.size(); i++) {
        const unsigned char value = static_cast<unsigned char>(name[i]);
        if (name[i] == '/' || name[i] == '\\' || value == 0 || value < 0x20) {
            return false;
        }
    }
    return true;
}

std::vector<std::string> path_components(const std::string& path) {
    std::vector<std::string> components;
    std::string component;
    for (std::size_t i = 0; i <= path.size(); i++) {
        if (i == path.size() || path[i] == '/' || path[i] == '\\') {
            if (!component.empty()) {
                components.push_back(component);
                component.clear();
            }
        } else {
            component.push_back(path[i]);
        }
    }
    return components;
}

bool is_unity_x86_64_plugin_path(const std::string& path) {
    const std::vector<std::string> components = path_components(path);
    return components.size() >= 3 &&
           ascii_equal_case_insensitive(components[components.size() - 3], "Plugins") &&
           ascii_equal_case_insensitive(components[components.size() - 2], "x86_64");
}

void add_diagnostic(target_scan_result& result, const char* code, const std::string& path,
                    const std::string& detail = std::string()) {
    target_scan_diagnostic diagnostic;
    diagnostic.code = code;
    diagnostic.path = path;
    diagnostic.detail = detail;
    result.diagnostics.push_back(diagnostic);
}

unsigned int little_u32(const std::string& bytes, std::size_t offset) {
    return static_cast<unsigned int>(static_cast<unsigned char>(bytes[offset])) |
           (static_cast<unsigned int>(static_cast<unsigned char>(bytes[offset + 1])) << 8) |
           (static_cast<unsigned int>(static_cast<unsigned char>(bytes[offset + 2])) << 16) |
           (static_cast<unsigned int>(static_cast<unsigned char>(bytes[offset + 3])) << 24);
}

void scan_directory(const std::string& path, std::size_t depth, target_filesystem& filesystem,
                    const target_scan_limits& limits, std::size_t& entries_seen,
                    std::size_t& directories_seen, std::set<std::string>& seen_targets,
                    target_scan_result& result) {
    if (++directories_seen > limits.max_directories) {
        add_diagnostic(result, "scan_directory_limit", path);
        return;
    }
    std::vector<target_directory_entry> entries;
    if (!filesystem.list_directory(path, entries)) {
        add_diagnostic(result, "directory_unreadable", path);
        return;
    }
    std::sort(entries.begin(), entries.end(),
              [](const target_directory_entry& left, const target_directory_entry& right) {
                  return left.name < right.name;
              });
    for (std::size_t i = 0; i < entries.size(); i++) {
        if (++entries_seen > limits.max_entries) {
            add_diagnostic(result, "scan_entry_limit", path);
            return;
        }
        if (!safe_leaf_name(entries[i].name)) {
            add_diagnostic(result, "unsafe_directory_entry", path, entries[i].name);
            continue;
        }
        const std::string child = join_path(path, entries[i].name);
        if (entries[i].kind == target_entry_kind::directory) {
            if (depth >= limits.max_depth) {
                add_diagnostic(result, "scan_depth_limit", child);
            } else {
                scan_directory(child, depth + 1, filesystem, limits, entries_seen,
                               directories_seen, seen_targets, result);
            }
            continue;
        }
        const eos_binary_kind expected = expected_kind_for_name(entries[i].name);
        if (expected == eos_binary_kind::unknown ||
            (entries[i].kind != target_entry_kind::file &&
             entries[i].kind != target_entry_kind::symlink)) {
            continue;
        }
        std::string bytes;
        const discovery_read read = filesystem.read_prefix(child, 4096, bytes);
        if (read != discovery_read::ok) {
            add_diagnostic(result, "target_unreadable", child);
            continue;
        }
        const eos_binary_kind actual = classify_eos_binary(bytes);
        if (actual == eos_binary_kind::unknown) {
            add_diagnostic(result, "target_binary_invalid", child);
            continue;
        }
        if (actual != expected) {
            add_diagnostic(result, "target_kind_mismatch", child);
            continue;
        }
        std::string canonical;
        if (!filesystem.canonical_file(child, canonical)) {
            add_diagnostic(result, "target_unreadable", child);
            continue;
        }
        if (!seen_targets.insert(canonical).second) {
            continue;
        }
        eos_target target;
        target.path = child;
        target.canonical_path = canonical;
        target.kind = actual;
        target.is_symlink = entries[i].kind == target_entry_kind::symlink;
        result.targets.push_back(target);
    }
}

bool valid_hash(const std::string& hash) {
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

} // namespace

target_scan_limits::target_scan_limits()
    : max_depth(8), max_entries(50000), max_directories(10000) {}

target_recommendation::target_recommendation()
    : code(target_recommendation_code::none),
      target_index(std::numeric_limits<std::size_t>::max()) {}

eos_binary_kind classify_eos_binary(const std::string& bytes) {
    if (bytes.size() >= 0x40 && bytes[0] == 'M' && bytes[1] == 'Z') {
        const unsigned int pe_offset = little_u32(bytes, 0x3c);
        if (pe_offset <= bytes.size() && bytes.size() - pe_offset >= 6 &&
            bytes[pe_offset] == 'P' && bytes[pe_offset + 1] == 'E' &&
            bytes[pe_offset + 2] == 0 && bytes[pe_offset + 3] == 0 &&
            static_cast<unsigned char>(bytes[pe_offset + 4]) == 0x64 &&
            static_cast<unsigned char>(bytes[pe_offset + 5]) == 0x86) {
            return eos_binary_kind::windows_x86_64;
        }
    }
    if (bytes.size() >= 20 && static_cast<unsigned char>(bytes[0]) == 0x7f &&
        bytes[1] == 'E' && bytes[2] == 'L' && bytes[3] == 'F' &&
        static_cast<unsigned char>(bytes[4]) == 2 &&
        static_cast<unsigned char>(bytes[5]) == 1 &&
        static_cast<unsigned char>(bytes[18]) == 0x3e && bytes[19] == 0) {
        return eos_binary_kind::linux_x86_64;
    }
    return eos_binary_kind::unknown;
}

target_scan_result scan_eos_targets(const std::string& game_root, target_filesystem& filesystem,
                                    const target_scan_limits& limits) {
    target_scan_result result;
    if (game_root.empty() || limits.max_entries == 0 || limits.max_directories == 0) {
        add_diagnostic(result, "scan_invalid", game_root);
        return result;
    }
    std::size_t entries_seen = 0;
    std::size_t directories_seen = 0;
    std::set<std::string> seen_targets;
    scan_directory(game_root, 0, filesystem, limits, entries_seen, directories_seen,
                   seen_targets, result);
    return result;
}

target_recommendation recommend_eos_target(const std::vector<eos_target>& targets,
                                           eos_binary_kind kind) {
    target_recommendation result;
    if (kind == eos_binary_kind::unknown) {
        result.evidence = "unsupported_binary_kind";
        return result;
    }

    std::vector<std::size_t> compatible;
    std::vector<std::size_t> architecture_specific;
    for (std::size_t i = 0; i < targets.size(); i++) {
        if (targets[i].kind != kind || targets[i].is_symlink) {
            continue;
        }
        compatible.push_back(i);
        if (is_unity_x86_64_plugin_path(targets[i].path)) {
            architecture_specific.push_back(i);
        }
    }

    if (architecture_specific.size() == 1) {
        result.code = target_recommendation_code::unique;
        result.target_index = architecture_specific[0];
        result.evidence = "unity_x86_64_plugin_directory";
    } else if (architecture_specific.size() > 1) {
        result.code = target_recommendation_code::ambiguous;
        result.evidence = "multiple_unity_x86_64_plugin_directories";
    } else if (compatible.size() == 1) {
        result.code = target_recommendation_code::unique;
        result.target_index = compatible[0];
        result.evidence = "only_safe_compatible_target";
    } else if (compatible.size() > 1) {
        result.code = target_recommendation_code::ambiguous;
        result.evidence = "multiple_safe_compatible_targets";
    } else {
        result.evidence = "no_safe_compatible_target";
    }
    return result;
}

artifact_verification verify_release_artifact(const release_artifact& artifact,
                                              target_filesystem& filesystem) {
    artifact_verification result;
    result.code = artifact_verification_code::invalid_manifest;
    result.actual_bytes = 0;
    result.actual_kind = eos_binary_kind::unknown;
    if (artifact.path.empty() || artifact.kind == eos_binary_kind::unknown ||
        artifact.bytes > max_binary_bytes || !valid_hash(artifact.sha256)) {
        return result;
    }
    std::string bytes;
    const discovery_read read = filesystem.read_file(artifact.path, max_binary_bytes, bytes);
    if (read != discovery_read::ok) {
        result.code = read == discovery_read::missing ? artifact_verification_code::missing :
                      read == discovery_read::too_large ? artifact_verification_code::too_large :
                      artifact_verification_code::unreadable;
        return result;
    }
    result.actual_bytes = bytes.size();
    result.actual_kind = classify_eos_binary(bytes);
    result.actual_sha256 = sha256_hex(bytes);
    if (result.actual_bytes != artifact.bytes) {
        result.code = artifact_verification_code::size_mismatch;
    } else if (result.actual_kind != artifact.kind) {
        result.code = artifact_verification_code::kind_mismatch;
    } else if (result.actual_sha256 != artifact.sha256) {
        result.code = artifact_verification_code::hash_mismatch;
    } else {
        result.code = artifact_verification_code::verified;
    }
    return result;
}

} // namespace manager
} // namespace eosr
