#ifndef EOSR_MANAGER_UI_POLICY_H
#define EOSR_MANAGER_UI_POLICY_H

#include <string>

#include "manager/install_transaction.h"

namespace eosr {
namespace manager {

struct game_action_policy {
    game_action_policy();
    bool install_update_enabled;
    bool restore_enabled;
    bool launch_enabled;
    std::string install_label;
    std::string next_action;
};

game_action_policy game_actions(installation_state state, bool verified_artifact,
                                bool standard_steam_launch);

enum class first_run_commit_action {
    start_install_transaction,
    show_paused_game
};

first_run_commit_action first_run_after_commit(bool runtime_path_mapped);

enum class instance_switch_action {
    commit_state_only,
    update_owned_descriptor,
    refuse
};

instance_switch_action instance_switch_for(installation_state state,
                                           bool verified_artifact);

enum class instance_delete_commit_action {
    start_cleanup_worker,
    finish_retained
};

instance_delete_commit_action instance_delete_after_commit(bool retain_directory);

enum class display_name_evidence_code {
    awaiting_runtime,
    matches_saved,
    overridden
};

struct display_name_evidence {
    display_name_evidence();
    display_name_evidence_code code;
    bool runtime_authoritative;
    std::string next_action;
};

display_name_evidence display_name_status(const std::string& saved_name,
                                          const std::string& effective_name,
                                          bool runtime_present);

enum class run_index_status {
    indexed,
    running
};

run_index_status run_index_completion_status(bool active_run_present);

class inspection_retry_latch {
public:
    inspection_retry_latch();

    // Return true when inspection may start now. A blocked request is retained until take().
    bool request(bool worker_blocked);
    bool take();

private:
    bool pending_;
};

std::string browser_item_label(const std::string& text);

enum class launch_submission_phase {
    idle,
    transaction,
    prelaunch_snapshot,
    submitted,
    watching
};

// A UI callback can be delivered more than once before the asynchronous install/snapshot chain
// reaches its watch timer. This small state machine makes the external Steam submission a
// one-shot transition even if callbacks are re-entered or an old worker wakeup is delivered.
class launch_submission_guard {
public:
    launch_submission_guard();

    bool begin();
    bool begin_prelaunch_snapshot();
    bool try_submit();
    bool begin_watch();
    void reset();
    launch_submission_phase phase() const;

private:
    launch_submission_phase phase_;
};

} // namespace manager
} // namespace eosr

#endif
