#include "manager/ui_policy.h"

namespace eosr {
namespace manager {

game_action_policy::game_action_policy()
    : install_update_enabled(false), restore_enabled(false), launch_enabled(false),
      install_label("Install") {}

game_action_policy game_actions(installation_state state, bool verified_artifact,
                                bool standard_steam_launch) {
    game_action_policy policy;
    switch (state) {
        case installation_state::original:
            policy.install_update_enabled = true;
            policy.install_label = "Install";
            break;
        case installation_state::installed_current:
            policy.install_update_enabled = true;
            policy.restore_enabled = true;
            policy.launch_enabled = standard_steam_launch;
            policy.install_label = "Update / Repair";
            break;
        case installation_state::installed_other_build:
            policy.install_update_enabled = true;
            policy.restore_enabled = true;
            policy.install_label = "Update / Repair";
            policy.next_action = "Update to the verified packaged release or restore the original";
            break;
        case installation_state::externally_changed:
            policy.restore_enabled = true;
            policy.next_action =
                "Steam or another process changed this library; inspect it or use Steam Verify "
                "Installed Files";
            break;
        case installation_state::recovery_required:
            policy.install_update_enabled = true;
            policy.restore_enabled = true;
            policy.install_label = "Recover";
            policy.next_action = "Recover the interrupted hash-verified transaction";
            break;
        case installation_state::original_unknown:
            policy.next_action =
                "Use Steam Verify Installed Files to restore the genuine original, then recheck";
            break;
        case installation_state::missing:
            policy.next_action = "Open the game folder and use Steam Verify Installed Files";
            break;
        case installation_state::ambiguous:
            policy.next_action = "Inspect and choose the exact EOS target before installing";
            break;
        case installation_state::unwritable:
            policy.next_action = "Close the game, check folder permissions, and recheck";
            break;
    }
    if (!verified_artifact) {
        policy.install_update_enabled = false;
        policy.launch_enabled = false;
        policy.next_action = "Repair or reinstall the packaged release artifact, then recheck";
    }
    return policy;
}

first_run_commit_action first_run_after_commit(bool runtime_path_mapped) {
    return runtime_path_mapped ? first_run_commit_action::start_install_transaction :
                                 first_run_commit_action::show_paused_game;
}

instance_switch_action instance_switch_for(installation_state state,
                                           bool verified_artifact) {
    if (state == installation_state::installed_current ||
        state == installation_state::installed_other_build) {
        return verified_artifact ? instance_switch_action::update_owned_descriptor :
                                   instance_switch_action::refuse;
    }
    if (state == installation_state::original ||
        state == installation_state::original_unknown ||
        state == installation_state::missing) {
        return instance_switch_action::commit_state_only;
    }
    return instance_switch_action::refuse;
}

instance_delete_commit_action instance_delete_after_commit(bool retain_directory) {
    return retain_directory ? instance_delete_commit_action::finish_retained :
                              instance_delete_commit_action::start_cleanup_worker;
}

display_name_evidence::display_name_evidence()
    : code(display_name_evidence_code::awaiting_runtime), runtime_authoritative(false) {}

display_name_evidence display_name_status(const std::string& saved_name,
                                          const std::string& effective_name,
                                          bool runtime_present) {
    display_name_evidence evidence;
    if (!runtime_present) {
        evidence.next_action =
            "Save the EOS Reimagined network name, launch through Steam, then inspect runtime.json";
        return evidence;
    }
    evidence.runtime_authoritative = true;
    if (saved_name == effective_name) {
        evidence.code = display_name_evidence_code::matches_saved;
        evidence.next_action =
            "runtime.json proves EOS Reimagined used the saved network display name";
        return evidence;
    }
    evidence.code = display_name_evidence_code::overridden;
    evidence.next_action =
        "runtime.json is authoritative; inspect EOSR_DISPLAY_NAME and the active data directory";
    return evidence;
}

run_index_status run_index_completion_status(bool active_run_present) {
    return active_run_present ? run_index_status::running : run_index_status::indexed;
}

inspection_retry_latch::inspection_retry_latch() : pending_(false) {}

bool inspection_retry_latch::request(bool worker_blocked) {
    pending_ = worker_blocked;
    return !worker_blocked;
}

bool inspection_retry_latch::take() {
    const bool pending = pending_;
    pending_ = false;
    return pending;
}

std::string browser_item_label(const std::string& text) {
    const unsigned char ascii_control_end = 0x20;
    const unsigned char ascii_delete = 0x7f;
    std::string label;
    label.reserve(text.size());
    bool pending_space = false;
    for (std::size_t i = 0; i < text.size(); i++) {
        const unsigned char byte = static_cast<unsigned char>(text[i]);
        if (byte <= ascii_control_end || byte == ascii_delete) {
            pending_space = !label.empty();
            continue;
        }
        if (pending_space) label += ' ';
        pending_space = false;
        label += text[i];
    }
    return label;
}

launch_submission_guard::launch_submission_guard()
    : phase_(launch_submission_phase::idle) {}

bool launch_submission_guard::begin() {
    if (phase_ != launch_submission_phase::idle) return false;
    phase_ = launch_submission_phase::transaction;
    return true;
}

bool launch_submission_guard::begin_prelaunch_snapshot() {
    if (phase_ != launch_submission_phase::transaction) return false;
    phase_ = launch_submission_phase::prelaunch_snapshot;
    return true;
}

bool launch_submission_guard::try_submit() {
    if (phase_ != launch_submission_phase::prelaunch_snapshot) return false;
    phase_ = launch_submission_phase::submitted;
    return true;
}

bool launch_submission_guard::begin_watch() {
    if (phase_ != launch_submission_phase::submitted) return false;
    phase_ = launch_submission_phase::watching;
    return true;
}

void launch_submission_guard::reset() {
    phase_ = launch_submission_phase::idle;
}

launch_submission_phase launch_submission_guard::phase() const {
    return phase_;
}

} // namespace manager
} // namespace eosr
