#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

#include "manager/install_transaction.h"
#include "manager/json.h"
#include "manager/manager_info.h"
#include "manager/run_service.h"
#include "manager/steam_discovery.h"
#include "manager/target_inspection.h"
#include "platform/manager_application.h"
#include "platform/manager_discovery.h"
#include "platform/manager_runs.h"
#include "platform/manager_transactions.h"

namespace {

using namespace eosr;
using namespace eosr::manager;

int usage(const char* detail = 0) {
    if (detail != 0) std::cerr << "error: " << detail << "\n\n";
    std::cerr <<
        "EOS Reimagined Manager headless adapter\n"
        "usage:\n"
        "  eosr-manager-cli version\n"
        "  eosr-manager-cli steam-scan\n"
        "  eosr-manager-cli target-scan <game-root>\n"
        "  eosr-manager-cli artifact-verify <windows|linux> <path> <bytes> <sha256>\n"
        "  eosr-manager-cli status <target> <record> <journal> <windows|linux> "
            "<artifact> <bytes> <sha256> [known-sha256...]\n"
        "  eosr-manager-cli install <tx-id> <release-id> <target> <artifact> "
            "<windows|linux> <bytes> <sha256> <backup> <journal> <descriptor> <record> <data-dir>\n"
        "  eosr-manager-cli update <tx-id> <release-id> <record> <artifact> "
            "<windows|linux> <bytes> <sha256> <data-dir>\n"
        "  eosr-manager-cli recover <journal>\n"
        "  eosr-manager-cli restore <record>\n"
        "  eosr-manager-cli runs <trace-root>\n"
        "  eosr-manager-cli bundle <run-directory> <new-sibling-directory> [generated-utc]\n";
    return 64;
}

bool decimal_size(const std::string& text, std::size_t& out) {
    if (text.empty()) return false;
    u64 value = 0;
    for (std::size_t i = 0; i < text.size(); i++) {
        if (text[i] < '0' || text[i] > '9') return false;
        const u64 digit = static_cast<u64>(text[i] - '0');
        if (value > (std::numeric_limits<u64>::max() - digit) / 10) return false;
        value = value * 10 + digit;
    }
    if (value > static_cast<u64>((std::numeric_limits<std::size_t>::max)())) return false;
    out = static_cast<std::size_t>(value);
    return true;
}

bool binary_kind(const std::string& text, eos_binary_kind& out) {
    if (text == "windows") out = eos_binary_kind::windows_x86_64;
    else if (text == "linux") out = eos_binary_kind::linux_x86_64;
    else return false;
    return true;
}

const char* kind_name(eos_binary_kind kind) {
    if (kind == eos_binary_kind::windows_x86_64) return "windows_x86_64";
    if (kind == eos_binary_kind::linux_x86_64) return "linux_x86_64";
    return "unknown";
}

const char* install_code(install_result_code code) {
    switch (code) {
        case install_result_code::installed: return "installed";
        case install_result_code::restored: return "restored";
        case install_result_code::invalid_request: return "invalid_request";
        case install_result_code::target_missing: return "target_missing";
        case install_result_code::target_unsafe: return "target_unsafe";
        case install_result_code::target_unwritable: return "target_unwritable";
        case install_result_code::target_in_use: return "target_in_use";
        case install_result_code::artifact_invalid: return "artifact_invalid";
        case install_result_code::sidecar_exists: return "sidecar_exists";
        case install_result_code::backup_failed: return "backup_failed";
        case install_result_code::journal_failed: return "journal_failed";
        case install_result_code::stage_failed: return "stage_failed";
        case install_result_code::target_changed: return "target_changed";
        case install_result_code::replace_failed: return "replace_failed";
        case install_result_code::descriptor_failed: return "descriptor_failed";
        case install_result_code::record_failed: return "record_failed";
        case install_result_code::state_invalid: return "state_invalid";
        case install_result_code::externally_changed: return "externally_changed";
        case install_result_code::backup_invalid: return "backup_invalid";
        case install_result_code::recovery_required: return "recovery_required";
        case install_result_code::cleanup_incomplete: return "cleanup_incomplete";
    }
    return "unknown";
}

const char* state_name(installation_state state) {
    switch (state) {
        case installation_state::original: return "original";
        case installation_state::installed_current: return "installed_current";
        case installation_state::installed_other_build: return "installed_other_build";
        case installation_state::externally_changed: return "externally_changed";
        case installation_state::recovery_required: return "recovery_required";
        case installation_state::original_unknown: return "original_unknown";
        case installation_state::missing: return "missing";
        case installation_state::ambiguous: return "ambiguous";
        case installation_state::unwritable: return "unwritable";
    }
    return "unknown";
}

const char* artifact_code(artifact_verification_code code) {
    switch (code) {
        case artifact_verification_code::verified: return "verified";
        case artifact_verification_code::invalid_manifest: return "invalid_manifest";
        case artifact_verification_code::missing: return "missing";
        case artifact_verification_code::unreadable: return "unreadable";
        case artifact_verification_code::too_large: return "too_large";
        case artifact_verification_code::size_mismatch: return "size_mismatch";
        case artifact_verification_code::kind_mismatch: return "kind_mismatch";
        case artifact_verification_code::hash_mismatch: return "hash_mismatch";
    }
    return "unknown";
}

const char* run_status(diagnostic_run_status status) {
    if (status == diagnostic_run_status::active) return "active";
    if (status == diagnostic_run_status::completed) return "completed";
    return "incomplete";
}

const char* bundle_code_name(support_bundle_code code) {
    switch (code) {
        case support_bundle_code::created: return "created";
        case support_bundle_code::invalid_request: return "invalid_request";
        case support_bundle_code::run_missing: return "run_missing";
        case support_bundle_code::bundle_exists: return "bundle_exists";
        case support_bundle_code::source_invalid: return "source_invalid";
        case support_bundle_code::create_failed: return "create_failed";
        case support_bundle_code::write_failed: return "write_failed";
        case support_bundle_code::cleanup_incomplete: return "cleanup_incomplete";
    }
    return "unknown";
}

int emit(const json_value& value, int code = 0) {
    std::cout << serialize_json(value) << "\n";
    return code;
}

json_value install_json(const install_result& result) {
    json_value out = json_object();
    out.members["code"] = json_string(install_code(result.code));
    out.members["detail"] = result.detail.empty() ? json_value() : json_string(result.detail);
    out.members["target_is_reimagined"] = json_bool(result.target_is_reimagined);
    return out;
}

release_artifact artifact_from(char** argv, int kind_at, int path_at, int size_at, int hash_at,
                               bool& ok) {
    release_artifact artifact;
    ok = binary_kind(argv[kind_at], artifact.kind) &&
         decimal_size(argv[size_at], artifact.bytes);
    artifact.path = argv[path_at];
    artifact.sha256 = argv[hash_at];
    return artifact;
}

int steam_scan() {
    platform::manager_discovery_filesystem filesystem;
    const steam_discovery_result found =
        discover_steam_games(platform::platform_steam_root_candidates(), filesystem);
    json_value out = json_object();
    json_value games = json_array();
    for (std::size_t i = 0; i < found.games.size(); i++) {
        json_value game = json_object();
        game.members["app_id"] = json_string(found.games[i].app_id);
        game.members["name"] = json_string(found.games[i].name);
        game.members["install_root"] = json_string(found.games[i].install_root);
        game.members["steam_root"] = json_string(found.games[i].steam_root);
        game.members["flatpak"] = json_bool(found.games[i].kind == steam_install_kind::flatpak_linux);
        games.elements.push_back(game);
    }
    out.members["games"] = games;
    json_value diagnostics = json_array();
    for (std::size_t i = 0; i < found.diagnostics.size(); i++) {
        json_value item = json_object();
        item.members["code"] = json_string(found.diagnostics[i].code);
        item.members["path"] = json_string(found.diagnostics[i].path);
        item.members["detail"] = json_string(found.diagnostics[i].detail);
        diagnostics.elements.push_back(item);
    }
    out.members["diagnostics"] = diagnostics;
    return emit(out);
}

int target_scan(const std::string& root) {
    platform::manager_discovery_filesystem filesystem;
    const target_scan_result found = scan_eos_targets(root, filesystem);
    json_value out = json_object();
    json_value targets = json_array();
    for (std::size_t i = 0; i < found.targets.size(); i++) {
        const target_recommendation recommendation =
            recommend_eos_target(found.targets, found.targets[i].kind);
        json_value item = json_object();
        item.members["path"] = json_string(found.targets[i].path);
        item.members["canonical_path"] = json_string(found.targets[i].canonical_path);
        item.members["kind"] = json_string(kind_name(found.targets[i].kind));
        item.members["symlink"] = json_bool(found.targets[i].is_symlink);
        const bool recommended = recommendation.code == target_recommendation_code::unique &&
                                 recommendation.target_index == i;
        item.members["recommended"] = json_bool(recommended);
        item.members["recommendation_evidence"] =
            json_string(recommended ? recommendation.evidence :
                (recommendation.code == target_recommendation_code::unique ?
                    "stronger_compatible_candidate_exists" : recommendation.evidence));
        targets.elements.push_back(item);
    }
    out.members["targets"] = targets;
    json_value diagnostics = json_array();
    for (std::size_t i = 0; i < found.diagnostics.size(); i++) {
        json_value item = json_object();
        item.members["code"] = json_string(found.diagnostics[i].code);
        item.members["path"] = json_string(found.diagnostics[i].path);
        item.members["detail"] = json_string(found.diagnostics[i].detail);
        diagnostics.elements.push_back(item);
    }
    out.members["diagnostics"] = diagnostics;
    return emit(out, found.targets.empty() ? 3 : 0);
}

int runs(const std::string& root) {
    platform::manager_run_filesystem filesystem;
    run_summary_cache cache;
    const run_index_result indexed = index_diagnostic_runs(root, filesystem, cache);
    json_value out = json_object();
    json_value values = json_array();
    for (std::size_t i = 0; i < indexed.runs.size(); i++) {
        const run_summary& source = indexed.runs[i];
        json_value run = json_object();
        run.members["run_id"] = json_string(source.run_id);
        run.members["directory"] = json_string(source.directory);
        run.members["status"] = json_string(run_status(source.status));
        run.members["emulator_build"] = json_string(source.emulator_build);
        run.members["sdk_initialized"] = json_bool(source.sdk_initialized);
        run.members["trace_invalid"] = json_bool(source.trace_invalid);
        run.members["discovery_port"] = json_int(static_cast<i64>(source.discovery_port));
        run.members["discover_count"] = json_int(static_cast<i64>(source.discover_count));
        run.members["handshake_count"] = json_int(static_cast<i64>(source.handshake_count));
        run.members["adopt_count"] = json_int(static_cast<i64>(source.adopt_count));
        run.members["drop_count"] = json_int(static_cast<i64>(source.drop_count));
        run.members["p2p_bytes_sent"] = json_int(static_cast<i64>(source.p2p_bytes_sent));
        run.members["p2p_bytes_received"] = json_int(static_cast<i64>(source.p2p_bytes_received));
        values.elements.push_back(run);
    }
    out.members["runs"] = values;
    out.members["diagnostic_count"] = json_int(static_cast<i64>(indexed.diagnostics.size()));
    return emit(out);
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) return usage();
    const std::string command = argv[1];
    if (command == "version" && argc == 2) {
        json_value out = json_object();
        out.members["application"] = json_string(application_name());
        out.members["build"] = json_string(build_id());
        return emit(out);
    }
    if (command == "steam-scan" && argc == 2) return steam_scan();
    if (command == "target-scan" && argc == 3) return target_scan(argv[2]);

    platform::manager_discovery_filesystem discovery_files;
    platform::manager_transaction_filesystem transaction_files;
    if (command == "artifact-verify" && argc == 6) {
        bool valid = false;
        const release_artifact artifact = artifact_from(argv, 2, 3, 4, 5, valid);
        if (!valid) return usage("artifact kind or byte count is invalid");
        const artifact_verification verified = verify_release_artifact(artifact, discovery_files);
        json_value out = json_object();
        out.members["code"] = json_string(artifact_code(verified.code));
        out.members["actual_bytes"] = json_int(static_cast<i64>(verified.actual_bytes));
        out.members["actual_sha256"] = json_string(verified.actual_sha256);
        out.members["actual_kind"] = json_string(kind_name(verified.actual_kind));
        return emit(out, verified.code == artifact_verification_code::verified ? 0 : 2);
    }
    if (command == "status" && argc >= 9) {
        installation_probe probe;
        probe.target_path = argv[2];
        probe.record_path = argv[3];
        probe.journal_path = argv[4];
        bool valid = false;
        probe.selected_artifact = artifact_from(argv, 5, 6, 7, 8, valid);
        if (!valid) return usage("artifact kind or byte count is invalid");
        probe.known_reimagined_sha256.push_back(probe.selected_artifact.sha256);
        for (int i = 9; i < argc; i++) probe.known_reimagined_sha256.push_back(argv[i]);
        probe.ambiguous = false;
        const installation_health health = inspect_installation(probe, transaction_files);
        json_value out = json_object();
        out.members["state"] = json_string(state_name(health.state));
        out.members["live_sha256"] = json_string(health.live_sha256);
        out.members["detail"] = json_string(health.detail);
        return emit(out);
    }
    if (command == "install" && argc == 14) {
        install_request request;
        request.transaction_id = argv[2]; request.release_id = argv[3];
        request.target_path = argv[4];
        bool valid = false;
        request.artifact = artifact_from(argv, 6, 5, 7, 8, valid);
        if (!valid) return usage("artifact kind or byte count is invalid");
        request.backup_path = argv[9]; request.journal_path = argv[10];
        request.descriptor_path = argv[11]; request.record_path = argv[12];
        request.data_dir = argv[13];
        const install_result installed = install_release(request, transaction_files);
        return emit(install_json(installed), installed.code == install_result_code::installed ? 0 : 2);
    }
    if (command == "update" && argc == 10) {
        update_request request;
        request.transaction_id = argv[2]; request.release_id = argv[3];
        request.record_path = argv[4];
        bool valid = false;
        request.artifact = artifact_from(argv, 6, 5, 7, 8, valid);
        if (!valid) return usage("artifact kind or byte count is invalid");
        request.data_dir = argv[9];
        const install_result updated = update_installation(request, transaction_files);
        return emit(install_json(updated), updated.code == install_result_code::installed ? 0 : 2);
    }
    if (command == "recover" && argc == 3) {
        const install_result recovered = recover_installation(argv[2], transaction_files);
        return emit(install_json(recovered), recovered.code == install_result_code::installed ? 0 : 2);
    }
    if (command == "restore" && argc == 3) {
        const install_result restored = restore_installation(argv[2], transaction_files);
        return emit(install_json(restored), restored.code == install_result_code::restored ? 0 : 2);
    }
    if (command == "runs" && argc == 3) return runs(argv[2]);
    if (command == "bundle" && (argc == 4 || argc == 5)) {
        platform::manager_run_filesystem filesystem;
        support_bundle_request request;
        request.run_directory = argv[2]; request.bundle_directory = argv[3];
        request.generated_utc = argc == 5 ? argv[4] : platform::manager_utc_now();
        const support_bundle_result bundled = create_support_bundle(request, filesystem);
        json_value out = json_object();
        out.members["code"] = json_string(bundle_code_name(bundled.code));
        out.members["shareable"] = json_bool(bundled.shareable);
        out.members["directory"] = json_string(bundled.directory);
        out.members["detail"] = json_string(bundled.detail);
        return emit(out, bundled.code == support_bundle_code::created ? 0 : 2);
    }
    return usage("unknown command or wrong argument count");
}
