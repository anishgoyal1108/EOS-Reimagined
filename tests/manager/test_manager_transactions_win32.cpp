#include "doctest.h"

#include <string>
#include <vector>

#include <windows.h>

#include "manager/install_transaction.h"
#include "manager/manager_state.h"
#include "manager/run_service.h"
#include "manager/sha256.h"
#include "platform/manager_runs.h"
#include "platform/manager_application.h"
#include "platform/manager_transactions.h"

using namespace eosr;

namespace {

std::wstring to_wide(const std::string& text) {
    const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                                          static_cast<int>(text.size()), 0, 0);
    REQUIRE(count > 0);
    std::wstring out(static_cast<std::size_t>(count), L'\0');
    REQUIRE(MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                                static_cast<int>(text.size()), &out[0], count) == count);
    return out;
}

std::string to_utf8(const std::wstring& text) {
    const int count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(),
                                          static_cast<int>(text.size()), 0, 0, 0, 0);
    REQUIRE(count > 0);
    std::string out(static_cast<std::size_t>(count), '\0');
    REQUIRE(WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(),
                                static_cast<int>(text.size()), &out[0], count, 0, 0) == count);
    return out;
}

void make_directory(const std::string& path) {
    const std::wstring wide = to_wide(path);
    const bool created = CreateDirectoryW(wide.c_str(), 0) != 0 ||
                         GetLastError() == ERROR_ALREADY_EXISTS;
    CHECK(created);
}

void write_file(const std::string& path, const std::string& bytes) {
    const std::wstring wide = to_wide(path);
    const HANDLE file = CreateFileW(wide.c_str(), GENERIC_WRITE, 0, 0, CREATE_ALWAYS,
                                    FILE_ATTRIBUTE_NORMAL, 0);
    REQUIRE(file != INVALID_HANDLE_VALUE);
    DWORD written = 0;
    REQUIRE(WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &written, 0));
    REQUIRE(written == bytes.size());
    REQUIRE(CloseHandle(file));
}

std::string read_file(const std::string& path) {
    const std::wstring wide = to_wide(path);
    const HANDLE file = CreateFileW(wide.c_str(), GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING,
                                    FILE_ATTRIBUTE_NORMAL, 0);
    REQUIRE(file != INVALID_HANDLE_VALUE);
    LARGE_INTEGER size;
    REQUIRE(GetFileSizeEx(file, &size));
    const bool size_valid = size.QuadPart >= 0 && size.QuadPart < 1024 * 1024;
    REQUIRE(size_valid);
    std::string bytes(static_cast<std::size_t>(size.QuadPart), '\0');
    DWORD read = 0;
    REQUIRE(ReadFile(file, bytes.empty() ? 0 : &bytes[0], static_cast<DWORD>(bytes.size()),
                     &read, 0));
    REQUIRE(read == bytes.size());
    REQUIRE(CloseHandle(file));
    return bytes;
}

std::string pe_bytes(const std::string& payload) {
    std::string bytes(4096, '\0');
    bytes[0] = 'M'; bytes[1] = 'Z'; bytes[0x3c] = static_cast<char>(0x80);
    bytes[0x80] = 'P'; bytes[0x81] = 'E';
    bytes[0x84] = static_cast<char>(0x64); bytes[0x85] = static_cast<char>(0x86);
    return bytes + payload;
}

struct windows_fixture {
    std::string temporary_root;
    std::string root;
    std::string original;
    std::string artifact;
    manager::install_request request;

    windows_fixture() {
        std::vector<wchar_t> temporary(MAX_PATH + 1, 0);
        REQUIRE(GetTempPathW(static_cast<DWORD>(temporary.size()), &temporary[0]) > 0);
        std::wstring wide_root = &temporary[0];
        while (wide_root.size() > 3 &&
               (wide_root[wide_root.size() - 1] == L'\\' ||
                wide_root[wide_root.size() - 1] == L'/')) wide_root.erase(wide_root.size() - 1);
        temporary_root = to_utf8(wide_root);
        std::string random_error;
        const std::string random = platform::manager_random_id(random_error);
        REQUIRE_FALSE(random.empty());
        wide_root += L"\\eosr-manager-\u4ea4\u6613-" + to_wide(random);
        root = to_utf8(wide_root);
        make_directory(root);
        make_directory(root + "\\game");
        make_directory(root + "\\release");
        make_directory(root + "\\manager");
        make_directory(root + "\\manager\\targets");
        make_directory(root + "\\manager\\instances");
        make_directory(root + "\\manager\\instances\\alice");
        make_directory(root + "\\manager\\instances\\alice\\data");
        original = pe_bytes("original");
        artifact = pe_bytes("reimagined");
        request.transaction_id = "33333333333333333333333333333333";
        request.release_id = "windows-platform-test";
        request.target_path = root + "\\game\\EOSSDK-Win64-Shipping.dll";
        request.artifact.path = root + "\\release\\EOSSDK-Win64-Shipping.dll";
        request.artifact.kind = manager::eos_binary_kind::windows_x86_64;
        request.artifact.bytes = artifact.size();
        request.artifact.sha256 = manager::sha256_hex(artifact);
        request.backup_path = request.target_path + ".eosr-original";
        request.journal_path = request.target_path + ".eosr-journal.json";
        request.descriptor_path = root + "\\game\\eosr-bootstrap.json";
        request.record_path = root + "\\manager\\targets\\target.json";
        request.data_dir = root + "\\manager\\instances\\alice\\data";
        write_file(request.target_path, original);
        write_file(request.artifact.path, artifact);
    }

    ~windows_fixture() {
        platform::manager_run_filesystem filesystem;
        manager::owned_tree_remove_request removal;
        removal.parent_directory = temporary_root;
        removal.directory = root;
        manager::remove_owned_tree(removal, filesystem);
    }
};

} // namespace

TEST_CASE("the Win32 UTF-16 transaction adapter installs and restores a Unicode fixture") {
    windows_fixture fixture;
    platform::manager_transaction_filesystem filesystem;
    const manager::install_result installed = manager::install_release(fixture.request, filesystem);
    CAPTURE(installed.detail);
    REQUIRE(installed.code == manager::install_result_code::installed);
    CHECK(read_file(fixture.request.target_path) == fixture.artifact);

    const manager::install_result restored = manager::restore_installation(
        fixture.request.record_path, filesystem);
    CAPTURE(restored.detail);
    REQUIRE(restored.code == manager::install_result_code::restored);
    CHECK(read_file(fixture.request.target_path) == fixture.original);
}

TEST_CASE("the Win32 adapter refuses a sharing-locked DLL") {
    windows_fixture fixture;
    const std::wstring target = to_wide(fixture.request.target_path);
    const HANDLE held = CreateFileW(target.c_str(), GENERIC_READ, FILE_SHARE_READ, 0,
                                    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
    REQUIRE(held != INVALID_HANDLE_VALUE);
    platform::manager_transaction_filesystem filesystem;
    const manager::install_result result = manager::install_release(fixture.request, filesystem);
    CHECK(result.code == manager::install_result_code::target_in_use);
    CHECK(CloseHandle(held));
}

TEST_CASE("the Win32 UTF-16 run adapter creates a sanitized sibling bundle") {
    windows_fixture fixture;
    const std::string runs = fixture.root + "\\traces";
    const std::string run = runs + "\\run-windows";
    make_directory(runs);
    make_directory(run);
    write_file(run + "\\runtime.json",
        "{\"schema_version\":1,\"emulator_build\":\"windows-build\","
        "\"created_utc\":\"2026-07-15T00:00:00Z\",\"run_id\":\"run-windows\","
        "\"instance_label\":null,\"os\":{\"name\":\"windows\",\"version\":\"10\","
        "\"wine\":\"wine\"},\"config\":{\"trace_level\":\"lifecycle\"}}");
    write_file(run + "\\trace.jsonl",
        "{\"v\":1,\"kind\":\"meta\",\"event\":\"run_start\"}\n");
    write_file(run + "\\profile.key", "must-not-copy");

    platform::manager_run_filesystem filesystem;
    manager::run_summary_cache cache;
    const manager::run_index_result index = manager::index_diagnostic_runs(runs, filesystem, cache);
    REQUIRE(index.runs.size() == 1);
    CHECK(index.runs[0].emulator_build == "windows-build");
    manager::support_bundle_request request;
    request.run_directory = run;
    request.bundle_directory = runs + "\\run-windows-support";
    request.generated_utc = "2026-07-15T00:01:00Z";
    const manager::support_bundle_result bundle =
        manager::create_support_bundle(request, filesystem);
    CAPTURE(bundle.detail);
    REQUIRE(bundle.code == manager::support_bundle_code::created);
    const std::string summary = read_file(request.bundle_directory + "\\summary.json");
    CHECK(summary.find("must-not-copy") == std::string::npos);
    CHECK(summary.find("identity_credential") != std::string::npos);
}

TEST_CASE("the Win32 application adapter provides Unicode state paths and random manager ids") {
    windows_fixture fixture;
    std::string error;
    const std::string private_dir = fixture.root + "\\private-state";
    REQUIRE(platform::manager_ensure_private_directory(private_dir, error));
    CHECK(GetFileAttributesW(to_wide(private_dir).c_str()) != INVALID_FILE_ATTRIBUTES);
    const std::string first = platform::manager_random_id(error);
    const std::string second = platform::manager_random_id(error);
    CHECK(manager::valid_manager_id(first));
    CHECK(manager::valid_manager_id(second));
    CHECK(first != second);
    CHECK_FALSE(platform::manager_data_root().empty());
    CHECK_FALSE(platform::manager_executable_path().empty());
    CHECK(platform::manager_utc_now().size() == 20);
}
