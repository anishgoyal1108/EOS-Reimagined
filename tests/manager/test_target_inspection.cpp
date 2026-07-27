#include "doctest.h"

#include <map>
#include <string>
#include <vector>

#include "manager/target_inspection.h"
#include "manager/sha256.h"

using namespace eosr::manager;

namespace {

class fake_target_filesystem : public target_filesystem {
public:
    void directory(const std::string& path, const std::vector<target_directory_entry>& entries) {
        directories_[path] = entries;
    }

    void file(const std::string& path, const std::string& bytes,
              const std::string& canonical = std::string()) {
        files_[path] = bytes;
        canonical_[path] = canonical.empty() ? path : canonical;
    }

    bool list_directory(const std::string& path, std::vector<target_directory_entry>& out) {
        std::map<std::string, std::vector<target_directory_entry> >::const_iterator it =
            directories_.find(path);
        if (it == directories_.end()) {
            return false;
        }
        out = it->second;
        return true;
    }

    discovery_read read_prefix(const std::string& path, std::size_t max_bytes, std::string& out) {
        out.clear();
        std::map<std::string, std::string>::const_iterator it = files_.find(path);
        if (it == files_.end()) {
            return discovery_read::missing;
        }
        out.assign(it->second, 0, max_bytes);
        return discovery_read::ok;
    }

    discovery_read read_file(const std::string& path, std::size_t max_bytes, std::string& out) {
        out.clear();
        std::map<std::string, std::string>::const_iterator it = files_.find(path);
        if (it == files_.end()) {
            return discovery_read::missing;
        }
        if (it->second.size() > max_bytes) {
            return discovery_read::too_large;
        }
        out = it->second;
        return discovery_read::ok;
    }

    bool canonical_file(const std::string& path, std::string& out) {
        std::map<std::string, std::string>::const_iterator it = canonical_.find(path);
        if (it == canonical_.end()) {
            return false;
        }
        out = it->second;
        return true;
    }

private:
    std::map<std::string, std::vector<target_directory_entry> > directories_;
    std::map<std::string, std::string> files_;
    std::map<std::string, std::string> canonical_;
};

target_directory_entry entry(const std::string& name, target_entry_kind kind) {
    target_directory_entry value;
    value.name = name;
    value.kind = kind;
    return value;
}

std::string pe64() {
    std::string bytes(256, '\0');
    bytes[0] = 'M';
    bytes[1] = 'Z';
    bytes[0x3c] = static_cast<char>(0x80);
    bytes[0x80] = 'P';
    bytes[0x81] = 'E';
    bytes[0x84] = static_cast<char>(0x64);
    bytes[0x85] = static_cast<char>(0x86);
    return bytes;
}

std::string elf64() {
    std::string bytes(64, '\0');
    bytes[0] = static_cast<char>(0x7f);
    bytes[1] = 'E';
    bytes[2] = 'L';
    bytes[3] = 'F';
    bytes[4] = 2;
    bytes[5] = 1;
    bytes[18] = static_cast<char>(0x3e);
    return bytes;
}

bool has_code(const target_scan_result& result, const std::string& code) {
    for (std::size_t i = 0; i < result.diagnostics.size(); i++) {
        if (result.diagnostics[i].code == code) {
            return true;
        }
    }
    return false;
}

} // namespace

TEST_CASE("target discovery classifies the file format instead of the manager host") {
    fake_target_filesystem fs;
    std::vector<target_directory_entry> root;
    root.push_back(entry("native", target_entry_kind::directory));
    root.push_back(entry("proton", target_entry_kind::directory));
    fs.directory("/game", root);
    fs.directory("/game/native", std::vector<target_directory_entry>(1,
        entry("libEOSSDK-Linux-Shipping.so", target_entry_kind::file)));
    fs.directory("/game/proton", std::vector<target_directory_entry>(1,
        entry("EOSSDK-Win64-Shipping.dll", target_entry_kind::file)));
    fs.file("/game/native/libEOSSDK-Linux-Shipping.so", elf64());
    fs.file("/game/proton/EOSSDK-Win64-Shipping.dll", pe64());

    const target_scan_result result = scan_eos_targets("/game", fs);
    REQUIRE(result.targets.size() == 2);
    CHECK(result.targets[0].kind == eos_binary_kind::linux_x86_64);
    CHECK(result.targets[1].kind == eos_binary_kind::windows_x86_64);
}

TEST_CASE("manager SHA-256 supports standard and incremental vectors") {
    CHECK(sha256_hex(std::string()) ==
          "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    CHECK(sha256_hex(std::string("abc")) ==
          "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    sha256_hasher hasher;
    hasher.update(std::string("a"));
    hasher.update(std::string("bc"));
    CHECK(hasher.final_hex() ==
          "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CHECK(hasher.final_hex().empty());
}

TEST_CASE("canonical names with the wrong or malformed format are diagnostics, not targets") {
    fake_target_filesystem fs;
    std::vector<target_directory_entry> entries;
    entries.push_back(entry("EOSSDK-Win64-Shipping.dll", target_entry_kind::file));
    entries.push_back(entry("libEOSSDK-Linux-Shipping.so", target_entry_kind::file));
    fs.directory("/game", entries);
    fs.file("/game/EOSSDK-Win64-Shipping.dll", elf64());
    fs.file("/game/libEOSSDK-Linux-Shipping.so", "not an ELF");

    const target_scan_result result = scan_eos_targets("/game", fs);
    CHECK(result.targets.empty());
    CHECK(has_code(result, "target_kind_mismatch"));
    CHECK(has_code(result, "target_binary_invalid"));
}

TEST_CASE("symlinked EOS targets are presented but marked unsafe for transactions") {
    fake_target_filesystem fs;
    fs.directory("/game", std::vector<target_directory_entry>(1,
        entry("EOSSDK-Win64-Shipping.dll", target_entry_kind::symlink)));
    fs.file("/game/EOSSDK-Win64-Shipping.dll", pe64(), "/shared/EOSSDK-Win64-Shipping.dll");

    const target_scan_result result = scan_eos_targets("/game", fs);
    REQUIRE(result.targets.size() == 1);
    CHECK(result.targets[0].is_symlink);
    CHECK(result.targets[0].canonical_path == "/shared/EOSSDK-Win64-Shipping.dll");
}

TEST_CASE("Unity x86_64 plugin directory is recommended over a legacy root duplicate") {
    std::vector<eos_target> targets(2);
    targets[0].path = "/game/Game_Data/Plugins/EOSSDK-Win64-Shipping.dll";
    targets[0].canonical_path = targets[0].path;
    targets[0].kind = eos_binary_kind::windows_x86_64;
    targets[0].is_symlink = false;
    targets[1].path =
        "/game/Game_Data/Plugins/x86_64/EOSSDK-Win64-Shipping.dll";
    targets[1].canonical_path = targets[1].path;
    targets[1].kind = eos_binary_kind::windows_x86_64;
    targets[1].is_symlink = false;

    const target_recommendation recommendation =
        recommend_eos_target(targets, eos_binary_kind::windows_x86_64);
    CHECK(recommendation.code == target_recommendation_code::unique);
    CHECK(recommendation.target_index == 1);
    CHECK(recommendation.evidence == "unity_x86_64_plugin_directory");
}

TEST_CASE("target recommendation remains ambiguous when architecture evidence is not unique") {
    std::vector<eos_target> targets(3);
    targets[0].path = "/game/A/Plugins/x86_64/EOSSDK-Win64-Shipping.dll";
    targets[1].path = "/game/B/Plugins/X86_64/EOSSDK-Win64-Shipping.dll";
    targets[2].path = "/game/C/EOSSDK-Win64-Shipping.dll";
    for (std::size_t i = 0; i < targets.size(); i++) {
        targets[i].canonical_path = targets[i].path;
        targets[i].kind = eos_binary_kind::windows_x86_64;
        targets[i].is_symlink = false;
    }

    const target_recommendation recommendation =
        recommend_eos_target(targets, eos_binary_kind::windows_x86_64);
    CHECK(recommendation.code == target_recommendation_code::ambiguous);
    CHECK(recommendation.evidence == "multiple_unity_x86_64_plugin_directories");
}

TEST_CASE("symlink evidence is never sufficient for a target recommendation") {
    std::vector<eos_target> targets(2);
    targets[0].path = "/game/Plugins/x86_64/EOSSDK-Win64-Shipping.dll";
    targets[0].canonical_path = "/outside/EOSSDK-Win64-Shipping.dll";
    targets[0].kind = eos_binary_kind::windows_x86_64;
    targets[0].is_symlink = true;
    targets[1].path = "/game/Plugins/EOSSDK-Win64-Shipping.dll";
    targets[1].canonical_path = targets[1].path;
    targets[1].kind = eos_binary_kind::windows_x86_64;
    targets[1].is_symlink = false;

    const target_recommendation recommendation =
        recommend_eos_target(targets, eos_binary_kind::windows_x86_64);
    CHECK(recommendation.code == target_recommendation_code::unique);
    CHECK(recommendation.target_index == 1);
    CHECK(recommendation.evidence == "only_safe_compatible_target");
}

TEST_CASE("hostile names and traversal beyond bounded depth are never followed") {
    fake_target_filesystem fs;
    std::vector<target_directory_entry> root;
    root.push_back(entry("../outside", target_entry_kind::directory));
    root.push_back(entry("a", target_entry_kind::directory));
    fs.directory("/game", root);
    std::string path = "/game/a";
    for (int i = 0; i < 10; i++) {
        fs.directory(path, std::vector<target_directory_entry>(1,
            entry("next", target_entry_kind::directory)));
        path += "/next";
    }

    target_scan_limits limits;
    limits.max_depth = 3;
    const target_scan_result result = scan_eos_targets("/game", fs, limits);
    CHECK(result.targets.empty());
    CHECK(has_code(result, "scan_depth_limit"));
    CHECK(has_code(result, "unsafe_directory_entry"));
}

TEST_CASE("release artifacts require an exact kind size and SHA-256") {
    fake_target_filesystem fs;
    fs.file("/release/EOSSDK-Win64-Shipping.dll", pe64());
    release_artifact expected;
    expected.path = "/release/EOSSDK-Win64-Shipping.dll";
    expected.kind = eos_binary_kind::windows_x86_64;
    expected.bytes = pe64().size();
    expected.sha256 = "6eee691dc769f52f8974f88253e9088ee22b288bcc059f9e39fe582f76aa6214";

    artifact_verification result = verify_release_artifact(expected, fs);
    CHECK(result.code == artifact_verification_code::verified);
    CHECK(result.actual_sha256 == expected.sha256);

    expected.sha256[0] = '0';
    result = verify_release_artifact(expected, fs);
    CHECK(result.code == artifact_verification_code::hash_mismatch);
}
