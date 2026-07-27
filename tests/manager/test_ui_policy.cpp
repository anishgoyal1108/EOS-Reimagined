#include "doctest.h"

#include "manager/ui_policy.h"

using namespace eosr::manager;

TEST_CASE("game action policy enables only safe state-machine transitions") {
    game_action_policy policy =
        game_actions(installation_state::original, true, true);
    CHECK(policy.install_update_enabled);
    CHECK(policy.install_label == "Install");
    CHECK_FALSE(policy.restore_enabled);
    CHECK_FALSE(policy.launch_enabled);

    policy = game_actions(installation_state::installed_current, true, true);
    CHECK(policy.install_update_enabled);
    CHECK(policy.install_label == "Update / Repair");
    CHECK(policy.restore_enabled);
    CHECK(policy.launch_enabled);

    policy = game_actions(installation_state::recovery_required, true, true);
    CHECK(policy.install_update_enabled);
    CHECK(policy.install_label == "Recover");
    CHECK(policy.restore_enabled);
    CHECK_FALSE(policy.launch_enabled);

    policy = game_actions(installation_state::original_unknown, true, true);
    CHECK_FALSE(policy.install_update_enabled);
    CHECK_FALSE(policy.restore_enabled);
    CHECK_FALSE(policy.launch_enabled);
    CHECK(policy.next_action.find("Steam Verify Installed Files") != std::string::npos);

    policy = game_actions(installation_state::ambiguous, true, true);
    CHECK_FALSE(policy.install_update_enabled);
    CHECK(policy.next_action.find("exact EOS target") != std::string::npos);
}

TEST_CASE("artifact and launch capabilities independently gate actions") {
    game_action_policy policy =
        game_actions(installation_state::original, false, true);
    CHECK_FALSE(policy.install_update_enabled);
    CHECK(policy.next_action.find("packaged release artifact") != std::string::npos);

    policy = game_actions(installation_state::installed_current, true, false);
    CHECK(policy.install_update_enabled);
    CHECK_FALSE(policy.launch_enabled);
}

TEST_CASE("a committed first-run game starts install before any refresh worker") {
    CHECK(first_run_after_commit(true) ==
          first_run_commit_action::start_install_transaction);
    CHECK(first_run_after_commit(false) ==
          first_run_commit_action::show_paused_game);
}

TEST_CASE("instance switching updates owned descriptors and refuses inconsistent recovery states") {
    CHECK(instance_switch_for(installation_state::installed_current, true) ==
          instance_switch_action::update_owned_descriptor);
    CHECK(instance_switch_for(installation_state::installed_other_build, true) ==
          instance_switch_action::update_owned_descriptor);
    CHECK(instance_switch_for(installation_state::original, true) ==
          instance_switch_action::commit_state_only);
    CHECK(instance_switch_for(installation_state::original_unknown, true) ==
          instance_switch_action::commit_state_only);
    CHECK(instance_switch_for(installation_state::recovery_required, true) ==
          instance_switch_action::refuse);
    CHECK(instance_switch_for(installation_state::externally_changed, true) ==
          instance_switch_action::refuse);
    CHECK(instance_switch_for(installation_state::installed_current, false) ==
          instance_switch_action::refuse);
}

TEST_CASE("committed instance deletion schedules tree cleanup off the event thread") {
    CHECK(instance_delete_after_commit(false) ==
          instance_delete_commit_action::start_cleanup_worker);
    CHECK(instance_delete_after_commit(true) ==
          instance_delete_commit_action::finish_retained);
}

TEST_CASE("display name evidence distinguishes saved configuration from runtime authority") {
    display_name_evidence evidence = display_name_status("bob", std::string(), false);
    CHECK(evidence.code == display_name_evidence_code::awaiting_runtime);
    CHECK_FALSE(evidence.runtime_authoritative);
    CHECK(evidence.next_action.find("launch") != std::string::npos);

    evidence = display_name_status("bob", "bob", true);
    CHECK(evidence.code == display_name_evidence_code::matches_saved);
    CHECK(evidence.runtime_authoritative);

    evidence = display_name_status("bob", "Player", true);
    CHECK(evidence.code == display_name_evidence_code::overridden);
    CHECK(evidence.runtime_authoritative);
    CHECK(evidence.next_action.find("EOSR_DISPLAY_NAME") != std::string::npos);
    CHECK(evidence.next_action.find("data directory") != std::string::npos);
}

TEST_CASE("browser item labels remain one physical FLTK row") {
    std::string raw = "  Risk\nof Rain 2\t · \r\n /games/ror2";
    raw.push_back('\0');
    raw += "hidden";
    const std::string label = browser_item_label(raw);

    CHECK(label == "Risk of Rain 2 · /games/ror2 hidden");
    CHECK(label.find('\n') == std::string::npos);
    CHECK(label.find('\r') == std::string::npos);
    CHECK(label.find('\t') == std::string::npos);
    CHECK(label.find('\0') == std::string::npos);
}

TEST_CASE("a launch lifecycle submits to Steam at most once") {
    launch_submission_guard guard;

    CHECK(guard.phase() == launch_submission_phase::idle);
    CHECK(guard.begin());
    CHECK(guard.phase() == launch_submission_phase::transaction);
    CHECK_FALSE(guard.begin());
    CHECK_FALSE(guard.try_submit());

    CHECK(guard.begin_prelaunch_snapshot());
    CHECK(guard.phase() == launch_submission_phase::prelaunch_snapshot);
    CHECK(guard.try_submit());
    CHECK(guard.phase() == launch_submission_phase::submitted);
    CHECK_FALSE(guard.try_submit());
    CHECK_FALSE(guard.begin_prelaunch_snapshot());

    CHECK(guard.begin_watch());
    CHECK(guard.phase() == launch_submission_phase::watching);
    CHECK_FALSE(guard.begin());
    CHECK_FALSE(guard.try_submit());

    guard.reset();
    CHECK(guard.phase() == launch_submission_phase::idle);
    CHECK(guard.begin());
}

TEST_CASE("an indexed active diagnostic run keeps the Running status") {
    CHECK(run_index_completion_status(true) == run_index_status::running);
    CHECK(run_index_completion_status(false) == run_index_status::indexed);
}

TEST_CASE("installation inspection skipped by a run worker is retried once") {
    inspection_retry_latch retry;

    CHECK_FALSE(retry.request(true));
    CHECK(retry.take());
    CHECK_FALSE(retry.take());
    CHECK(retry.request(false));
}
