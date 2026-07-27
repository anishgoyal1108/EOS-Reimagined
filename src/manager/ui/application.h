#ifndef EOSR_MANAGER_UI_APPLICATION_H
#define EOSR_MANAGER_UI_APPLICATION_H

#include <map>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include <FL/Fl_Double_Window.H>

#include "manager/configuration_model.h"
#include "manager/install_transaction.h"
#include "manager/manager_state.h"
#include "manager/release_catalog.h"
#include "manager/run_service.h"
#include "manager/steam_discovery.h"
#include "manager/target_inspection.h"
#include "manager/ui_policy.h"
#include "platform/manager_discovery.h"
#include "platform/manager_runs.h"
#include "platform/manager_transactions.h"

class Fl_Box;
class Fl_Browser;
class Fl_Button;
class Fl_Group;
class Fl_Hold_Browser;
class Fl_Input;
class Fl_Text_Buffer;
class Fl_Text_Display;
class Fl_Tabs;
class Fl_Wizard;

namespace eosr {
namespace manager {
namespace ui {

class manager_window : public Fl_Double_Window {
public:
    explicit manager_window(bool smoke_test);
    ~manager_window();

    bool ready() const { return ready_; }

private:
    struct stored_game {
        game_state state;
        std::string state_path;
        std::string sha256;
    };

    enum class worker_job {
        none,
        release_verification,
        steam_discovery,
        target_scan,
        installation_inspection,
        installation_transaction,
        restore_transaction,
        instance_switch_transaction,
        instance_switch_rollback
    };

    enum class run_worker_job {
        none,
        index,
        prelaunch_snapshot,
        launch_watch,
        support_bundle,
        remove_run,
        remove_instance
    };

    void build_shell();
    void build_games_view(int x, int y, int w, int h);
    void build_instances_view(int x, int y, int w, int h);
    void build_runs_view(int x, int y, int w, int h);
    void build_health_view(int x, int y, int w, int h);
    bool initialize_storage();
    void load_state();
    void load_release_catalog();
    void refresh_all();
    void refresh_games();
    void refresh_game_detail();
    void refresh_instances();
    void refresh_runs();
    void apply_run_worker_result();
    void refresh_run_detail();
    void refresh_health();
    void set_status(const std::string& text, bool error = false);
    bool manager_job_running();
    bool run_job_running();
    void record_operation(const std::string& action, const std::string& object_id,
                          const std::string& code, bool success);

    void show_first_run_wizard(bool start_discovery);
    void close_wizard();
    void wizard_back();
    void wizard_next();
    void wizard_choose_manual();
    void wizard_review();
    void wizard_finish();
    void populate_wizard_games();
    void populate_wizard_targets();

    void start_steam_discovery();
    void start_target_scan(const std::string& root);
    void start_installation_inspection(const std::string& target_id,
                                       const installation_probe& probe);
    void apply_worker_result();
    static void worker_awake(void* self);
    static void run_worker_awake(void* self);

    stored_game* selected_game();
    const stored_game* selected_game() const;
    managed_target* active_target(stored_game& game);
    managed_instance* active_instance(stored_game& game);
    std::string game_root(const game_state& game) const;
    std::string instance_root_for_game(const game_state& game, std::string& error) const;
    std::string target_record_path(const game_state& game, const managed_target& target) const;
    bool save_index();
    bool save_game(stored_game& game);
    bool configure_runtime_path(game_state& game, managed_target& target,
                                managed_instance& instance, std::string& error);

    void add_manual_game();
    bool install_or_update(bool launch_after = false);
    void restore_target();
    void launch_game();
    void launch_after_install();
    void configure_instance();
    void create_instance();
    void select_instance();
    void rename_instance();
    void export_identity();
    void delete_instance();
    void build_support_bundle();
    void delete_run();
    void open_game_folder();
    void open_instance_folder();
    void open_run_folder();
    void copy_health_report();

    bool load_instance_configuration(const managed_instance& instance,
                                     manager_configuration& configuration,
                                     std::string& bytes, bool& malformed,
                                     std::string& error);
    bool edit_configuration(manager_configuration& configuration, bool malformed,
                            bool& replace_confirmed);
    bool save_instance_configuration(stored_game& game, managed_instance& instance,
                                     const manager_configuration& configuration,
                                     const std::string& expected_sha256, bool malformed,
                                     bool replace_confirmed);

    bool begin_launch_watch(const managed_instance& instance);
    void poll_launch_watch();
    void dispatch_steam_launch();
    static void launch_watch_cb(void* self);

    static void games_select_cb(Fl_Widget*, void* self);
    static void instances_select_cb(Fl_Widget*, void* self);
    static void runs_select_cb(Fl_Widget*, void* self);
    static void discover_cb(Fl_Widget*, void* self);
    static void add_manual_cb(Fl_Widget*, void* self);
    static void install_cb(Fl_Widget*, void* self);
    static void restore_cb(Fl_Widget*, void* self);
    static void launch_cb(Fl_Widget*, void* self);
    static void configure_cb(Fl_Widget*, void* self);
    static void open_game_cb(Fl_Widget*, void* self);
    static void create_instance_cb(Fl_Widget*, void* self);
    static void select_instance_cb(Fl_Widget*, void* self);
    static void rename_instance_cb(Fl_Widget*, void* self);
    static void export_identity_cb(Fl_Widget*, void* self);
    static void delete_instance_cb(Fl_Widget*, void* self);
    static void open_instance_cb(Fl_Widget*, void* self);
    static void refresh_runs_cb(Fl_Widget*, void* self);
    static void bundle_cb(Fl_Widget*, void* self);
    static void delete_run_cb(Fl_Widget*, void* self);
    static void open_run_cb(Fl_Widget*, void* self);
    static void health_refresh_cb(Fl_Widget*, void* self);
    static void health_copy_cb(Fl_Widget*, void* self);
    static void health_open_cb(Fl_Widget*, void* self);
    static void wizard_back_cb(Fl_Widget*, void* self);
    static void wizard_next_cb(Fl_Widget*, void* self);
    static void wizard_cancel_cb(Fl_Widget*, void* self);
    static void wizard_manual_cb(Fl_Widget*, void* self);

    bool smoke_test_;
    bool ready_;
    std::string root_;
    std::string index_path_;
    manager_index index_;
    std::string index_sha256_;
    std::vector<stored_game> games_;
    std::vector<std::string> health_diagnostics_;
    release_catalog catalog_;
    bool catalog_valid_;
    std::string catalog_error_;

    platform::manager_discovery_filesystem discovery_files_;
    platform::manager_transaction_filesystem transaction_files_;
    platform::manager_run_filesystem run_files_;
    run_summary_cache run_cache_;
    std::vector<run_summary> runs_;
    std::set<std::string> active_runs_;
    std::vector<steam_installation> discovered_steam_;

    std::thread run_worker_;
    std::mutex run_worker_mutex_;
    run_worker_job run_worker_job_;
    bool run_worker_running_;
    bool run_refresh_requested_;
    inspection_retry_latch installation_inspection_retry_;
    std::vector<run_summary> pending_runs_;
    run_summary_cache pending_run_cache_;
    support_bundle_result pending_bundle_result_;
    owned_tree_remove_result pending_remove_result_;
    std::string pending_run_id_;
    std::string pending_remove_directory_;
    std::string pending_observed_run_id_;

    Fl_Tabs* tabs_;
    Fl_Hold_Browser* games_browser_;
    Fl_Text_Display* game_detail_;
    Fl_Text_Buffer* game_detail_buffer_;
    Fl_Button* install_button_;
    Fl_Button* restore_button_;
    Fl_Button* launch_button_;
    Fl_Hold_Browser* instances_browser_;
    Fl_Text_Display* instance_detail_;
    Fl_Text_Buffer* instance_detail_buffer_;
    Fl_Hold_Browser* runs_browser_;
    Fl_Text_Display* run_detail_;
    Fl_Text_Buffer* run_detail_buffer_;
    Fl_Text_Display* health_detail_;
    Fl_Text_Buffer* health_detail_buffer_;
    Fl_Box* status_;

    std::size_t selected_game_;
    std::size_t selected_instance_;
    std::size_t selected_run_;

    std::thread worker_;
    std::mutex worker_mutex_;
    worker_job worker_job_;
    bool worker_running_;
    steam_discovery_result pending_discovery_;
    target_scan_result pending_targets_;
    bool pending_catalog_valid_;
    std::string pending_catalog_error_;
    installation_health pending_installation_health_;
    install_result pending_install_result_;
    std::string pending_target_id_;
    std::string pending_operation_action_;
    std::map<std::string, installation_health> installation_health_cache_;
    bool pending_launch_after_install_;
    std::string pending_launch_game_id_;
    std::string pending_launch_target_id_;
    std::string pending_launch_instance_id_;
    game_state pending_switch_previous_;
    managed_instance pending_switch_candidate_;
    update_request pending_switch_rollback_;
    bool pending_switch_descriptor_changed_;
    std::vector<steam_game_installation> discovered_games_;

    Fl_Double_Window* wizard_window_;
    Fl_Wizard* wizard_;
    std::vector<Fl_Group*> wizard_pages_;
    Fl_Hold_Browser* wizard_games_;
    Fl_Hold_Browser* wizard_targets_;
    Fl_Input* wizard_name_;
    Fl_Input* wizard_slug_;
    Fl_Text_Display* wizard_review_;
    Fl_Text_Buffer* wizard_review_buffer_;
    Fl_Box* wizard_status_;
    Fl_Button* wizard_back_;
    Fl_Button* wizard_next_;
    int wizard_step_;
    bool wizard_manual_game_;
    steam_game_installation wizard_game_;
    target_scan_result wizard_scan_;

    std::set<std::string> prelaunch_runs_;
    std::string watched_trace_root_;
    int launch_watch_ticks_;
    launch_submission_guard launch_submission_;
};

} // namespace ui
} // namespace manager
} // namespace eosr

#endif
