#include "doctest.h"

#include <fstream>
#include <string>

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include "manager/install_transaction.h"
#include "manager/manager_state.h"
#include "manager/run_service.h"
#include "manager/sha256.h"
#include "platform/manager_runs.h"
#include "platform/manager_application.h"
#include "platform/manager_transactions.h"
#include "platform/paths.h"

using namespace eosr;

namespace {

std::string platform_pe(const std::string& payload) {
    std::string bytes(4096, '\0');
    bytes[0] = 'M';
    bytes[1] = 'Z';
    bytes[0x3c] = static_cast<char>(0x80);
    bytes[0x80] = 'P';
    bytes[0x81] = 'E';
    bytes[0x84] = static_cast<char>(0x64);
    bytes[0x85] = static_cast<char>(0x86);
    bytes += payload;
    return bytes;
}

void platform_write(const std::string& path, const std::string& bytes) {
    std::ofstream stream(path.c_str(), std::ios::binary | std::ios::trunc);
    REQUIRE(stream.good());
    stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    REQUIRE(stream.good());
}

manager::install_request platform_request(const std::string& root,
                                          const std::string& artifact_bytes) {
    manager::install_request request;
    request.transaction_id = "22222222222222222222222222222222";
    request.release_id = "platform-test";
    request.target_path = root + "/game/EOSSDK-Win64-Shipping.dll";
    request.artifact.path = root + "/release/EOSSDK-Win64-Shipping.dll";
    request.artifact.kind = manager::eos_binary_kind::windows_x86_64;
    request.artifact.bytes = artifact_bytes.size();
    request.artifact.sha256 = manager::sha256_hex(artifact_bytes);
    request.backup_path = request.target_path + ".eosr-original";
    request.journal_path = request.target_path + ".eosr-journal.json";
    request.descriptor_path = root + "/game/eosr-bootstrap.json";
    request.record_path = root + "/manager/targets/target.json";
    request.data_dir = root + "/manager/instances/alice/data";
    return request;
}

} // namespace

TEST_CASE("the POSIX transaction adapter installs and restores inside an isolated fixture") {
    const std::string root = std::string(EOSR_MANAGER_TEST_DIR) + "/transaction-" +
                             std::to_string(static_cast<unsigned long long>(getpid()));
    REQUIRE(platform::make_directories(root + "/game"));
    REQUIRE(platform::make_directories(root + "/release"));
    REQUIRE(platform::make_directories(root + "/manager/targets"));
    REQUIRE(platform::make_directories(root + "/manager/instances/alice/data"));
    const std::string original = platform_pe("original");
    const std::string artifact = platform_pe("reimagined");
    const manager::install_request request = platform_request(root, artifact);
    platform_write(request.target_path, original);
    platform_write(request.artifact.path, artifact);

    platform::manager_transaction_filesystem filesystem;
    const manager::install_result installed = manager::install_release(request, filesystem);
    CAPTURE(installed.detail);
    REQUIRE(installed.code == manager::install_result_code::installed);
    std::string live;
    REQUIRE(platform::read_file_capped(request.target_path, 8192, live) == platform::file_read::ok);
    CHECK(live == artifact);

    const manager::install_result restored = manager::restore_installation(request.record_path,
                                                                            filesystem);
    CAPTURE(restored.detail);
    REQUIRE(restored.code == manager::install_result_code::restored);
    REQUIRE(platform::read_file_capped(request.target_path, 8192, live) == platform::file_read::ok);
    CHECK(live == original);
}

TEST_CASE("the POSIX adapter detects a mapped target as in use") {
    const std::string root = std::string(EOSR_MANAGER_TEST_DIR) + "/mapped-" +
                             std::to_string(static_cast<unsigned long long>(getpid()));
    REQUIRE(platform::make_directories(root + "/game"));
    REQUIRE(platform::make_directories(root + "/release"));
    REQUIRE(platform::make_directories(root + "/manager/targets"));
    const std::string original = platform_pe("original");
    const std::string artifact = platform_pe("reimagined");
    const manager::install_request request = platform_request(root, artifact);
    platform_write(request.target_path, original);
    platform_write(request.artifact.path, artifact);

    const int file = open(request.target_path.c_str(), O_RDONLY);
    REQUIRE(file >= 0);
    void* mapping = mmap(0, original.size(), PROT_READ, MAP_PRIVATE, file, 0);
    REQUIRE(mapping != MAP_FAILED);
    platform::manager_transaction_filesystem filesystem;
    const manager::install_result result = manager::install_release(request, filesystem);
    CHECK(result.code == manager::install_result_code::target_in_use);
    CHECK(munmap(mapping, original.size()) == 0);
    CHECK(close(file) == 0);
}

TEST_CASE("the POSIX run adapter indexes and bundles only no-follow owned files") {
    const std::string root = std::string(EOSR_MANAGER_TEST_DIR) + "/runs-" +
                             std::to_string(static_cast<unsigned long long>(getpid()));
    const std::string runs = root + "/traces";
    const std::string run = runs + "/run-native";
    REQUIRE(platform::make_directories(run));
    platform_write(run + "/runtime.json",
        "{\"schema_version\":1,\"emulator_build\":\"native-build\","
        "\"created_utc\":\"2026-07-15T00:00:00Z\",\"run_id\":\"run-native\","
        "\"instance_label\":null,\"os\":{\"name\":\"linux\",\"version\":null,"
        "\"wine\":null},\"config\":{\"trace_level\":\"lifecycle\"}}");
    platform_write(run + "/trace.jsonl",
        "{\"v\":1,\"kind\":\"meta\",\"event\":\"run_start\"}\n{\"v\":1");
    platform_write(run + "/profile.key", "must-not-copy");
    REQUIRE(symlink((run + "/profile.key").c_str(), (run + "/trace.1.jsonl").c_str()) == 0);

    platform::manager_run_filesystem filesystem;
    manager::run_summary_cache cache;
    const manager::run_index_result index = manager::index_diagnostic_runs(runs, filesystem, cache);
    REQUIRE(index.runs.size() == 1);
    CHECK(index.runs[0].sdk_initialized);
    CHECK(index.runs[0].torn_tail_records == 1);

    manager::support_bundle_request request;
    request.run_directory = run;
    request.bundle_directory = runs + "/run-native-support";
    request.generated_utc = "2026-07-15T00:01:00Z";
    const manager::support_bundle_result bundle =
        manager::create_support_bundle(request, filesystem);
    CAPTURE(bundle.detail);
    REQUIRE(bundle.code == manager::support_bundle_code::created);
    struct stat directory_info;
    struct stat summary_info;
    REQUIRE(stat(request.bundle_directory.c_str(), &directory_info) == 0);
    REQUIRE(stat((request.bundle_directory + "/summary.json").c_str(), &summary_info) == 0);
    CHECK((directory_info.st_mode & 0777) == 0700);
    CHECK((summary_info.st_mode & 0777) == 0600);
    std::string summary;
    REQUIRE(platform::read_file_capped(request.bundle_directory + "/summary.json", 1024 * 1024,
                                       summary) == platform::file_read::ok);
    CHECK(summary.find("must-not-copy") == std::string::npos);
    CHECK(summary.find("symlink_not_followed") != std::string::npos);
}

TEST_CASE("the POSIX application adapter provides private state identity and argv launch") {
    const std::string root = std::string(EOSR_MANAGER_TEST_DIR) + "/app-" +
                             std::to_string(static_cast<unsigned long long>(getpid()));
    std::string error;
    REQUIRE(platform::manager_ensure_private_directory(root, error));
    struct stat info;
    REQUIRE(stat(root.c_str(), &info) == 0);
    CHECK((info.st_mode & 0777) == 0700);
    const std::string first = platform::manager_random_id(error);
    const std::string second = platform::manager_random_id(error);
    CHECK(manager::valid_manager_id(first));
    CHECK(manager::valid_manager_id(second));
    CHECK(first != second);
    CHECK_FALSE(platform::manager_data_root().empty());
    CHECK_FALSE(platform::manager_executable_path().empty());
    CHECK(platform::manager_utc_now().size() == 20);

    manager::steam_launch_request request;
    request.executable = "/bin/true";
    const platform::manager_action_result launched = platform::manager_launch_steam(request);
    CAPTURE(launched.detail);
    CHECK(launched.ok);
    CHECK(launched.code == "request_accepted");
}
