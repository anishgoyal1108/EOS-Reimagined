#include "manager/release_catalog.h"

#include <limits>
#include <set>

#include "manager/json.h"
#include "manager/manager_state.h"

namespace eosr {
namespace manager {

namespace {

const std::size_t max_manifest_bytes = 1024 * 1024;

bool decimal_component(const std::string& value, std::size_t& at) {
    const std::size_t start = at;
    while (at < value.size() && value[at] >= '0' && value[at] <= '9') at++;
    return at != start;
}

bool release_version(const std::string& value) {
    std::size_t at = 0;
    if (value.empty() || value[at++] != 'v' || !decimal_component(value, at) ||
        at >= value.size() || value[at++] != '.' || !decimal_component(value, at) ||
        at >= value.size() || value[at++] != '.' || !decimal_component(value, at)) return false;
    const std::string marker = "-alpha.";
    if (value.compare(at, marker.size(), marker) != 0) return false;
    at += marker.size();
    return decimal_component(value, at) && at == value.size();
}

bool lower_hex(const std::string& value, std::size_t size) {
    if (value.size() != size) return false;
    for (std::size_t i = 0; i < value.size(); i++)
        if (!((value[i] >= '0' && value[i] <= '9') ||
              (value[i] >= 'a' && value[i] <= 'f'))) return false;
    return true;
}

bool safe_relative(const std::string& value) {
    if (value.empty() || value.size() > 1024 || value[0] == '/' || value[0] == '\\' ||
        value.find('\\') != std::string::npos || value.find(':') != std::string::npos ||
        value.find('\0') != std::string::npos) return false;
    std::size_t start = 0;
    while (start <= value.size()) {
        const std::size_t end = value.find('/', start);
        const std::string part = value.substr(start, end == std::string::npos ?
                                                       std::string::npos : end - start);
        if (part.empty() || part == "." || part == "..") return false;
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return true;
}

bool mode_string(const std::string& value) {
    if (value.size() != 4) return false;
    for (std::size_t i = 0; i < value.size(); i++)
        if (value[i] < '0' || value[i] > '7') return false;
    return true;
}

bool string_member(const json_value& object, const char* name, std::string& out) {
    const json_value* value = json_member(object, name);
    if (value == 0 || value->kind != json_kind::string) return false;
    out = value->text;
    return true;
}

std::string join_path(const std::string& root, const std::string& relative) {
    if (root.empty()) return relative;
    const bool windows = root.find('\\') != std::string::npos && root.find('/') == std::string::npos;
    const char separator = windows ? '\\' : '/';
    const char last = root[root.size() - 1];
    std::string native = relative;
    if (windows) {
        for (std::size_t i = 0; i < native.size(); i++)
            if (native[i] == '/') native[i] = '\\';
    }
    return last == '/' || last == '\\' ? root + native : root + separator + native;
}

} // namespace

bool parse_release_catalog(const std::string& bytes, const std::string& package_root,
                           release_catalog& out, std::string& error) {
    out = release_catalog();
    error.clear();
    if (bytes.size() > max_manifest_bytes || !manager_absolute_path(package_root)) {
        error = "package manifest is oversized or package root is not absolute";
        return false;
    }
    json_value root;
    if (!parse_json(bytes, root, error) || root.kind != json_kind::object) {
        if (error.empty()) error = "package manifest is not an object";
        return false;
    }
    const json_value* schema = json_member(root, "schema_version");
    const json_value* files = json_member(root, "files");
    if (schema == 0 || schema->kind != json_kind::integer || schema->integer != 1 ||
        !string_member(root, "version", out.version) || !release_version(out.version) ||
        !string_member(root, "commit", out.commit) || !lower_hex(out.commit, 40) ||
        !string_member(root, "platform", out.platform) ||
        (out.platform != "linux" && out.platform != "windows") ||
        !string_member(root, "architecture", out.architecture) ||
        out.architecture != "x86_64" || files == 0 || files->kind != json_kind::array ||
        files->elements.empty() || files->elements.size() > 256) {
        error = "package manifest does not match schema version 1";
        return false;
    }
    std::set<std::string> seen;
    for (std::size_t i = 0; i < files->elements.size(); i++) {
        const json_value& record = files->elements[i];
        std::string path;
        std::string hash;
        std::string mode;
        const json_value* size = json_member(record, "bytes");
        if (record.kind != json_kind::object || !string_member(record, "path", path) ||
            !safe_relative(path) || !seen.insert(path).second ||
            size == 0 || size->kind != json_kind::integer || size->integer < 0 ||
            static_cast<u64>(size->integer) >
                static_cast<u64>((std::numeric_limits<std::size_t>::max)()) ||
            !string_member(record, "sha256", hash) || !lower_hex(hash, 64) ||
            !string_member(record, "mode", mode) || !mode_string(mode)) {
            error = "package manifest contains an invalid or duplicate file record";
            return false;
        }
        eos_binary_kind kind = eos_binary_kind::unknown;
        if (path == "artifacts/EOSSDK-Win64-Shipping.dll")
            kind = eos_binary_kind::windows_x86_64;
        else if (path == "artifacts/libEOSSDK-Linux-Shipping.so")
            kind = eos_binary_kind::linux_x86_64;
        if (kind != eos_binary_kind::unknown) {
            release_artifact artifact;
            artifact.path = join_path(package_root, path);
            artifact.kind = kind;
            artifact.bytes = static_cast<std::size_t>(size->integer);
            artifact.sha256 = hash;
            out.artifacts.push_back(artifact);
        }
    }
    if (out.artifacts.empty()) {
        error = "package manifest contains no recognized SDK artifact";
        return false;
    }
    out.release_id = out.version + "@" + out.commit;
    return true;
}

bool catalog_artifact(const release_catalog& catalog, eos_binary_kind kind,
                      release_artifact& out) {
    for (std::size_t i = 0; i < catalog.artifacts.size(); i++) {
        if (catalog.artifacts[i].kind == kind) {
            out = catalog.artifacts[i];
            return true;
        }
    }
    return false;
}

} // namespace manager
} // namespace eosr
