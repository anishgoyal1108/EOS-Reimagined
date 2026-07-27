#include "manager/ui/application.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <map>
#include <sstream>

#include <FL/Fl.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Check_Button.H>
#include <FL/Fl_Choice.H>
#include <FL/Fl_Group.H>
#include <FL/Fl_Hold_Browser.H>
#include <FL/Fl_Input.H>
#include <FL/Fl_Int_Input.H>
#include <FL/Fl_Multiline_Input.H>
#include <FL/Fl_Native_File_Chooser.H>
#include <FL/Fl_Scroll.H>
#include <FL/Fl_Tabs.H>
#include <FL/Fl_Text_Buffer.H>
#include <FL/Fl_Text_Display.H>
#include <FL/Fl_Wizard.H>
#include <FL/fl_ask.H>
#include <FL/fl_draw.H>

#include "manager/configuration_model.h"
#include "manager/instance_service.h"
#include "manager/install_transaction.h"
#include "manager/json.h"
#include "manager/manager_info.h"
#include "manager/operation_log.h"
#include "manager/sha256.h"
#include "manager/state_store.h"
#include "manager/steam_launch.h"
#include "manager/ui_policy.h"
#include "platform/manager_application.h"
#include "platform/proton_paths.h"

namespace eosr {
namespace manager {
namespace ui {

namespace {

const Fl_Color canvas = fl_rgb_color(238, 242, 239);
const Fl_Color paper = fl_rgb_color(250, 251, 247);
const Fl_Color ink = fl_rgb_color(26, 48, 50);
const Fl_Color muted = fl_rgb_color(83, 101, 99);
const Fl_Color petrol = fl_rgb_color(25, 73, 76);
const Fl_Color orange = fl_rgb_color(211, 103, 54);
const Fl_Color moss = fl_rgb_color(66, 111, 86);
const Fl_Color danger = fl_rgb_color(154, 55, 48);

std::string join_path(const std::string& left, const std::string& right) {
    if (left.empty()) return right;
    const bool windows = left.find('\\') != std::string::npos && left.find('/') == std::string::npos;
    const char separator = windows ? '\\' : '/';
    const char last = left[left.size() - 1];
    return last == '/' || last == '\\' ? left + right : left + separator + right;
}

std::string parent_path(const std::string& path) {
    const std::string::size_type at = path.find_last_of("/\\");
    if (at == std::string::npos) return std::string();
    return at == 0 ? path.substr(0, 1) : path.substr(0, at);
}

std::string base_name(const std::string& path) {
    const std::string::size_type at = path.find_last_of("/\\");
    return at == std::string::npos ? path : path.substr(at + 1);
}

std::string safe_slug(const std::string& display) {
    std::string out;
    bool dash = false;
    for (std::size_t i = 0; i < display.size() && out.size() < 24; i++) {
        char c = display[i];
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
            out.push_back(c);
            dash = false;
        } else if (!out.empty() && !dash) {
            out.push_back('-');
            dash = true;
        }
    }
    while (!out.empty() && out[out.size() - 1] == '-') out.erase(out.size() - 1);
    return out.empty() ? std::string("player") : out;
}

const char* kind_name(eos_binary_kind kind) {
    if (kind == eos_binary_kind::windows_x86_64) return "Windows x64 DLL";
    if (kind == eos_binary_kind::linux_x86_64) return "Linux x86_64 shared object";
    return "Unknown binary";
}

const char* target_evidence_name(const std::string& evidence) {
    if (evidence == "unity_x86_64_plugin_directory")
        return "Unity architecture-specific Plugins/x86_64 directory";
    if (evidence == "only_safe_compatible_target")
        return "only safe compatible target for this platform";
    if (evidence == "multiple_unity_x86_64_plugin_directories")
        return "multiple architecture-specific candidates; runtime evidence is required";
    if (evidence == "multiple_safe_compatible_targets")
        return "multiple compatible candidates; runtime evidence is required";
    if (evidence == "no_safe_compatible_target")
        return "no safe non-symlink candidate";
    if (evidence == "stronger_compatible_candidate_exists")
        return "a stronger architecture/path candidate exists";
    return "no unique recommendation evidence";
}

const char* install_state_name(installation_state state) {
    switch (state) {
        case installation_state::original: return "Original";
        case installation_state::installed_current: return "Installed (current)";
        case installation_state::installed_other_build: return "Installed (other build)";
        case installation_state::externally_changed: return "Externally changed";
        case installation_state::recovery_required: return "Recovery required";
        case installation_state::original_unknown: return "Original unknown";
        case installation_state::missing: return "Missing";
        case installation_state::ambiguous: return "Ambiguous";
        case installation_state::unwritable: return "Unwritable";
    }
    return "Unknown";
}

const char* run_state_name(diagnostic_run_status state) {
    if (state == diagnostic_run_status::active) return "Active";
    if (state == diagnostic_run_status::completed) return "Completed";
    return "Incomplete";
}

std::string effective_source_label(const run_summary& run, const char* field) {
    const std::map<std::string, std::string>::const_iterator found =
        run.effective_sources.find(field);
    return found == run.effective_sources.end() ? "  [source unavailable]" :
                                                  "  [source: " + found->second + "]";
}

const char* install_code_name(install_result_code code) {
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

const char* config_code_name(configuration_save_code code) {
    switch (code) {
        case configuration_save_code::saved: return "saved";
        case configuration_save_code::invalid_configuration: return "invalid_configuration";
        case configuration_save_code::invalid_request: return "invalid_request";
        case configuration_save_code::malformed_confirmation_required:
            return "malformed_confirmation_required";
        case configuration_save_code::externally_changed: return "externally_changed";
        case configuration_save_code::backup_exists: return "backup_exists";
        case configuration_save_code::backup_failed: return "backup_failed";
        case configuration_save_code::temporary_exists: return "temporary_exists";
        case configuration_save_code::write_failed: return "write_failed";
        case configuration_save_code::replace_failed: return "replace_failed";
    }
    return "unknown";
}

const char* identity_code_name(identity_export_code code) {
    switch (code) {
        case identity_export_code::exported: return "exported";
        case identity_export_code::invalid_request: return "invalid_request";
        case identity_export_code::source_missing: return "source_missing";
        case identity_export_code::source_unsafe: return "source_unsafe";
        case identity_export_code::destination_exists: return "destination_exists";
        case identity_export_code::copy_failed: return "copy_failed";
        case identity_export_code::source_changed: return "source_changed";
        case identity_export_code::verification_failed: return "verification_failed";
    }
    return "unknown";
}

bool parse_integer(const char* text, i64& out) {
    if (text == 0 || *text == '\0') return false;
    char* end = 0;
    const long long value = std::strtoll(text, &end, 10);
    if (end == text || *end != '\0') return false;
    out = static_cast<i64>(value);
    return true;
}

std::vector<std::string> split_seeds(const std::string& text) {
    std::vector<std::string> values;
    std::string current;
    for (std::size_t i = 0; i <= text.size(); i++) {
        const char c = i == text.size() ? ',' : text[i];
        if (c == ',' || c == '\n' || c == '\r' || c == ' ' || c == '\t') {
            if (!current.empty()) {
                values.push_back(current);
                current.clear();
            }
        } else current.push_back(c);
    }
    return values;
}

std::string join_seeds(const std::vector<std::string>& values) {
    std::string out;
    for (std::size_t i = 0; i < values.size(); i++) {
        if (i != 0) out += "\n";
        out += values[i];
    }
    return out;
}

struct dialog_state {
    dialog_state() : accepted(false) {}
    bool accepted;
    Fl_Double_Window* window;
};

void accept_dialog(Fl_Widget*, void* value) {
    dialog_state* state = static_cast<dialog_state*>(value);
    state->accepted = true;
    state->window->hide();
}

void cancel_dialog(Fl_Widget*, void* value) {
    static_cast<dialog_state*>(value)->window->hide();
}

struct config_control {
    configuration_field field;
    Fl_Widget* first;
    Fl_Widget* second;
};

int choice_index(Fl_Choice* choice, const std::string& value) {
    for (int i = 0; i < choice->size(); i++) {
        const Fl_Menu_Item* item = choice->menu() + i;
        if (item->text != 0 && value == item->text) return i;
    }
    return 0;
}

struct config_reset_state {
    std::vector<config_control>* controls;
    manager_configuration defaults;
};

void reset_configuration_controls(Fl_Widget*, void* value) {
    config_reset_state* state = static_cast<config_reset_state*>(value);
    for (std::size_t i = 0; i < state->controls->size(); i++) {
        config_control& control = (*state->controls)[i];
        const std::string& key = control.field.key;
        if (control.field.kind == configuration_field_kind::boolean) {
            bool enabled = false;
            if (key == "enable_lan") enabled = state->defaults.enable_lan;
            else if (key == "enable_overlay") enabled = state->defaults.enable_overlay;
            else if (key == "unlock_dlcs") enabled = state->defaults.unlock_dlcs;
            static_cast<Fl_Check_Button*>(control.first)->value(enabled ? 1 : 0);
        } else if (control.field.kind == configuration_field_kind::choice) {
            Fl_Choice* choice = static_cast<Fl_Choice*>(control.first);
            const std::string selected = key == "log_level" ? state->defaults.log_level :
                                                               state->defaults.trace_level;
            choice->value(choice_index(choice, selected));
        } else if (control.field.kind == configuration_field_kind::port_range) {
            static_cast<Fl_Input*>(control.first)->value(
                std::to_string(state->defaults.discovery_first).c_str());
            static_cast<Fl_Input*>(control.second)->value(
                std::to_string(state->defaults.discovery_last).c_str());
        } else if (control.field.kind == configuration_field_kind::string_list) {
            static_cast<Fl_Input*>(control.first)->value(
                join_seeds(state->defaults.peer_seeds).c_str());
        } else {
            std::string selected;
            if (key == "display_name") selected = state->defaults.display_name;
            else if (key == "locale") selected = state->defaults.locale;
            else if (key == "trace_dir") selected = state->defaults.trace_dir;
            else if (key == "trace_max_bytes")
                selected = std::to_string(state->defaults.trace_max_bytes);
            else if (key == "trace_max_rotated_files")
                selected = std::to_string(state->defaults.trace_max_rotated_files);
            else if (key == "instance_label") selected = state->defaults.instance_label;
            static_cast<Fl_Input*>(control.first)->value(selected.c_str());
        }
    }
}

bool configuration_dialog(manager_configuration& configuration, bool malformed,
                          bool& replace_confirmed) {
    Fl_Double_Window window(720, 650, "Configure EOS Reimagined instance");
    window.color(canvas);
    Fl_Box title(24, 16, 672, 28, "Instance configuration");
    title.align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    title.labelfont(FL_HELVETICA_BOLD);
    title.labelsize(20);
    title.labelcolor(ink);
    Fl_Box warning(24, 47, 672, 42,
        "display_name is EOS Reimagined's network name; the game/Steam nickname is separate. "
        "EOSR_* variables override this file, and runtime.json is authoritative after launch.");
    warning.align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE | FL_ALIGN_WRAP);
    warning.labelcolor(muted);
    warning.labelsize(12);
    Fl_Scroll scroll(20, 94, 680, 490);
    scroll.box(FL_FLAT_BOX);
    scroll.color(paper);
    std::vector<config_control> controls;
    const std::vector<configuration_field>& fields = configuration_fields();
    int y = 108;
    std::string last_group;
    for (std::size_t i = 0; i < fields.size(); i++) {
        if (fields[i].group != last_group) {
            Fl_Box* group = new Fl_Box(34, y, 630, 24, fields[i].group.c_str());
            group->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
            group->labelfont(FL_HELVETICA_BOLD);
            group->labelcolor(petrol);
            y += 29;
            last_group = fields[i].group;
        }
        std::string label = fields[i].key;
        if (fields[i].key == "display_name") label = "display_name  (EOSR network name)";
        else if (fields[i].key == "instance_label")
            label = "instance_label  (diagnostic slug)";
        if (!fields[i].available) label += "  (unavailable in this SDK)";
        Fl_Box* field_label = new Fl_Box(38, y, 238, 28, label.c_str());
        field_label->copy_label(label.c_str());
        field_label->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
        field_label->labelcolor(fields[i].available ? ink : muted);
        config_control control;
        control.field = fields[i];
        control.first = 0;
        control.second = 0;
        if (fields[i].kind == configuration_field_kind::boolean) {
            Fl_Check_Button* input = new Fl_Check_Button(286, y, 350, 28, "Enabled");
            bool value = false;
            if (fields[i].key == "enable_lan") value = configuration.enable_lan;
            else if (fields[i].key == "enable_overlay") value = configuration.enable_overlay;
            else if (fields[i].key == "unlock_dlcs") value = configuration.unlock_dlcs;
            input->value(value ? 1 : 0);
            control.first = input;
        } else if (fields[i].kind == configuration_field_kind::choice) {
            Fl_Choice* input = new Fl_Choice(286, y, 350, 28);
            const std::string value = fields[i].key == "log_level" ? configuration.log_level :
                                                                         configuration.trace_level;
            if (fields[i].key == "log_level") input->add("off|fatal|err|warn|info|debug|trace");
            else input->add("off|errors|lifecycle|full");
            input->value(choice_index(input, value));
            control.first = input;
        } else if (fields[i].kind == configuration_field_kind::port_range) {
            Fl_Int_Input* first = new Fl_Int_Input(286, y, 165, 28);
            Fl_Int_Input* last = new Fl_Int_Input(471, y, 165, 28);
            first->value(std::to_string(configuration.discovery_first).c_str());
            last->value(std::to_string(configuration.discovery_last).c_str());
            control.first = first; control.second = last;
        } else if (fields[i].kind == configuration_field_kind::string_list) {
            Fl_Multiline_Input* input = new Fl_Multiline_Input(286, y, 350, 58);
            input->value(join_seeds(configuration.peer_seeds).c_str());
            control.first = input;
            y += 30;
        } else {
            Fl_Input* input = fields[i].kind == configuration_field_kind::integer ?
                static_cast<Fl_Input*>(new Fl_Int_Input(286, y, 350, 28)) :
                new Fl_Input(286, y, 350, 28);
            std::string value;
            if (fields[i].key == "display_name") value = configuration.display_name;
            else if (fields[i].key == "locale") value = configuration.locale;
            else if (fields[i].key == "trace_dir") value = configuration.trace_dir;
            else if (fields[i].key == "trace_max_bytes")
                value = std::to_string(configuration.trace_max_bytes);
            else if (fields[i].key == "trace_max_rotated_files")
                value = std::to_string(configuration.trace_max_rotated_files);
            else if (fields[i].key == "instance_label") value = configuration.instance_label;
            input->value(value.c_str());
            if (fields[i].key == "instance_label") input->deactivate();
            control.first = input;
        }
        if (!fields[i].available && control.first != 0) control.first->deactivate();
        controls.push_back(control);
        y += 36;
    }
    scroll.end();
    dialog_state state;
    state.window = &window;
    config_reset_state reset_state;
    reset_state.controls = &controls;
    reset_state.defaults = new_instance_configuration(
        configuration.display_name, configuration.instance_label, true);
    Fl_Button reset(348, 602, 140, 32, "Reset form defaults");
    Fl_Button cancel(500, 602, 92, 32, "Cancel");
    Fl_Button save(604, 602, 92, 32, "Review Save");
    cancel.callback(cancel_dialog, &state);
    reset.callback(reset_configuration_controls, &reset_state);
    save.callback(accept_dialog, &state);
    save.color(petrol); save.labelcolor(FL_WHITE);
    window.end();
    window.set_modal();
    window.show();
    while (window.shown()) Fl::wait();
    if (!state.accepted) return false;

    manager_configuration edited = configuration;
    for (std::size_t i = 0; i < controls.size(); i++) {
        const std::string& key = controls[i].field.key;
        if (controls[i].field.kind == configuration_field_kind::boolean) {
            const bool value = static_cast<Fl_Check_Button*>(controls[i].first)->value() != 0;
            if (key == "enable_lan") edited.enable_lan = value;
            else if (key == "enable_overlay") edited.enable_overlay = value;
            else if (key == "unlock_dlcs") edited.unlock_dlcs = value;
        } else if (controls[i].field.kind == configuration_field_kind::choice) {
            Fl_Choice* choice = static_cast<Fl_Choice*>(controls[i].first);
            const char* value = choice->text();
            if (key == "log_level") edited.log_level = value != 0 ? value : "off";
            else edited.trace_level = value != 0 ? value : "off";
        } else if (controls[i].field.kind == configuration_field_kind::port_range) {
            i64 first = 0; i64 last = 0;
            if (!parse_integer(static_cast<Fl_Input*>(controls[i].first)->value(), first) ||
                !parse_integer(static_cast<Fl_Input*>(controls[i].second)->value(), last) ||
                first < 0 || first > 65535 || last < 0 || last > 65535) {
                fl_alert("Discovery ports must be decimal values from 1 through 65535.");
                return false;
            }
            edited.discovery_first = static_cast<u16>(first);
            edited.discovery_last = static_cast<u16>(last);
        } else if (controls[i].field.kind == configuration_field_kind::string_list) {
            edited.peer_seeds = split_seeds(static_cast<Fl_Input*>(controls[i].first)->value());
        } else {
            const std::string value = static_cast<Fl_Input*>(controls[i].first)->value();
            if (key == "display_name") edited.display_name = value;
            else if (key == "locale") edited.locale = value;
            else if (key == "trace_dir") edited.trace_dir = value;
            else if (key == "trace_max_bytes") {
                if (!parse_integer(value.c_str(), edited.trace_max_bytes)) {
                    fl_alert("Trace file cap must be a decimal integer."); return false;
                }
            } else if (key == "trace_max_rotated_files") {
                if (!parse_integer(value.c_str(), edited.trace_max_rotated_files)) {
                    fl_alert("Trace rotation count must be a decimal integer."); return false;
                }
            }
        }
    }
    const configuration_validation validation = validate_manager_configuration(edited);
    if (!validation.valid) {
        std::string message = "Configuration has rejected values:\n";
        for (std::size_t i = 0; i < validation.diagnostics.size(); i++)
            if (validation.diagnostics[i].action == "reject")
                message += "\n• " + validation.diagnostics[i].field + ": " +
                           validation.diagnostics[i].reason;
        fl_alert("%s", message.c_str());
        return false;
    }
    if (!validation.diagnostics.empty()) {
        std::string message = "The SDK will normalize these values before save:\n";
        for (std::size_t i = 0; i < validation.diagnostics.size(); i++)
            message += "\n• " + validation.diagnostics[i].field + " — " +
                       validation.diagnostics[i].action;
        if (fl_choice("%s", "Cancel", "Apply", 0, message.c_str()) != 1) return false;
    }
    if (malformed && fl_choice(
            "The existing eosr.json is malformed. Replace it with the reviewed configuration? "
            "The original will be preserved as a verified backup.",
            "Cancel", "Replace", 0) != 1) return false;
    configuration = validation.normalized;
    replace_confirmed = malformed;
    return true;
}

} // namespace

manager_window::manager_window(bool smoke_test)
    : Fl_Double_Window(1080, 720), smoke_test_(smoke_test),
      ready_(false), catalog_valid_(false), run_worker_job_(run_worker_job::none),
      run_worker_running_(false), run_refresh_requested_(false), tabs_(0),
      games_browser_(0), game_detail_(0),
      game_detail_buffer_(new Fl_Text_Buffer()), install_button_(0), restore_button_(0),
      launch_button_(0), instances_browser_(0), instance_detail_(0),
      instance_detail_buffer_(new Fl_Text_Buffer()), runs_browser_(0), run_detail_(0),
      run_detail_buffer_(new Fl_Text_Buffer()), health_detail_(0),
      health_detail_buffer_(new Fl_Text_Buffer()), status_(0), selected_game_(0),
      selected_instance_(0), selected_run_(0), worker_job_(worker_job::none),
      worker_running_(false), pending_catalog_valid_(false),
      pending_launch_after_install_(false), pending_switch_descriptor_changed_(false),
      wizard_window_(0), wizard_(0), wizard_games_(0),
      wizard_targets_(0), wizard_name_(0), wizard_slug_(0), wizard_review_(0),
      wizard_review_buffer_(new Fl_Text_Buffer()), wizard_status_(0), wizard_back_(0),
      wizard_next_(0), wizard_step_(0), wizard_manual_game_(false), launch_watch_ticks_(0) {
    copy_label(application_name().c_str());
    color(canvas);
    build_shell();
    end();
    resizable(tabs_);
    if (smoke_test_) {
        ready_ = true;
        refresh_all();
        return;
    }
    ready_ = initialize_storage();
    if (ready_) {
        load_state();
        load_release_catalog();
        refresh_all();
        if (games_.empty()) Fl::add_timeout(0.15, [](void* value) {
            static_cast<manager_window*>(value)->show_first_run_wizard(false);
        }, this);
    }
}

manager_window::~manager_window() {
    Fl::remove_timeout(launch_watch_cb, this);
    if (worker_.joinable()) worker_.join();
    if (run_worker_.joinable()) run_worker_.join();
    close_wizard();
    if (game_detail_ != 0) game_detail_->buffer(0);
    if (instance_detail_ != 0) instance_detail_->buffer(0);
    if (run_detail_ != 0) run_detail_->buffer(0);
    if (health_detail_ != 0) health_detail_->buffer(0);
    delete game_detail_buffer_;
    delete instance_detail_buffer_;
    delete run_detail_buffer_;
    delete health_detail_buffer_;
    delete wizard_review_buffer_;
}

void manager_window::build_shell() {
    Fl_Box* rail = new Fl_Box(0, 0, 10, h());
    rail->box(FL_FLAT_BOX); rail->color(orange);
    Fl_Box* title = new Fl_Box(30, 16, 700, 32, "EOS Reimagined Manager");
    title->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    title->labelfont(FL_HELVETICA_BOLD); title->labelsize(24); title->labelcolor(ink);
    Fl_Box* subtitle = new Fl_Box(31, 49, 850, 22,
        "Steam-safe installation · isolated identities · evidence-first diagnostics");
    subtitle->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE); subtitle->labelcolor(muted);
    subtitle->labelsize(13);
    tabs_ = new Fl_Tabs(22, 82, 1040, 584);
    tabs_->color(canvas); tabs_->selection_color(paper);
    build_games_view(22, 106, 1040, 560);
    build_instances_view(22, 106, 1040, 560);
    build_runs_view(22, 106, 1040, 560);
    build_health_view(22, 106, 1040, 560);
    tabs_->end();
    status_ = new Fl_Box(30, 677, 1020, 26, "SAFE STATE · no operation in progress");
    status_->box(FL_FLAT_BOX); status_->color(canvas); status_->labelcolor(muted);
    status_->labelfont(FL_COURIER_BOLD); status_->labelsize(11);
    status_->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
}

void manager_window::build_games_view(int x, int y, int w, int h) {
    Fl_Group* page = new Fl_Group(x, y, w, h, "Games"); page->color(paper);
    games_browser_ = new Fl_Hold_Browser(x + 16, y + 20, 330, h - 100);
    games_browser_->format_char(0);
    games_browser_->callback(games_select_cb, this);
    Fl_Button* discover = new Fl_Button(x + 16, y + h - 66, 156, 34, "Discover Steam");
    discover->callback(discover_cb, this);
    Fl_Button* manual = new Fl_Button(x + 184, y + h - 66, 162, 34, "Add game folder…");
    manual->callback(add_manual_cb, this);
    game_detail_ = new Fl_Text_Display(x + 368, y + 20, w - 386, h - 126);
    game_detail_->buffer(game_detail_buffer_); game_detail_->textfont(FL_COURIER);
    game_detail_->textsize(12); game_detail_->color(fl_rgb_color(247, 249, 245));
    install_button_ = new Fl_Button(x + 368, y + h - 92, 134, 34, "Install / Update");
    install_button_->callback(install_cb, this); install_button_->color(petrol);
    install_button_->labelcolor(FL_WHITE);
    restore_button_ = new Fl_Button(x + 512, y + h - 92, 102, 34, "Restore");
    restore_button_->callback(restore_cb, this);
    launch_button_ = new Fl_Button(x + 624, y + h - 92, 102, 34, "Launch");
    launch_button_->callback(launch_cb, this); launch_button_->color(moss);
    launch_button_->labelcolor(FL_WHITE);
    Fl_Button* configure = new Fl_Button(x + 736, y + h - 92, 112, 34, "Configure");
    configure->callback(configure_cb, this);
    Fl_Button* open = new Fl_Button(x + 858, y + h - 92, 150, 34, "Open game folder");
    open->callback(open_game_cb, this);
    page->end();
}

void manager_window::build_instances_view(int x, int y, int w, int h) {
    Fl_Group* page = new Fl_Group(x, y, w, h, "Instances"); page->color(paper);
    instances_browser_ = new Fl_Hold_Browser(x + 16, y + 20, 330, h - 100);
    instances_browser_->format_char(0);
    instances_browser_->callback(instances_select_cb, this);
    instance_detail_ = new Fl_Text_Display(x + 368, y + 20, w - 386, h - 126);
    instance_detail_->buffer(instance_detail_buffer_); instance_detail_->textfont(FL_COURIER);
    instance_detail_->textsize(12);
    const int by = y + h - 92;
    Fl_Button* create = new Fl_Button(x + 16, by, 106, 34, "New…");
    create->callback(create_instance_cb, this);
    Fl_Button* select = new Fl_Button(x + 132, by, 106, 34, "Use selected");
    select->callback(select_instance_cb, this); select->color(petrol); select->labelcolor(FL_WHITE);
    Fl_Button* rename = new Fl_Button(x + 248, by, 98, 34, "Edit label…");
    rename->callback(rename_instance_cb, this);
    Fl_Button* export_key = new Fl_Button(x + 368, by, 142, 34, "Export identity…");
    export_key->callback(export_identity_cb, this);
    Fl_Button* remove = new Fl_Button(x + 520, by, 112, 34, "Delete…");
    remove->callback(delete_instance_cb, this);
    Fl_Button* open = new Fl_Button(x + 642, by, 142, 34, "Open data folder");
    open->callback(open_instance_cb, this);
    page->end();
}

void manager_window::build_runs_view(int x, int y, int w, int h) {
    Fl_Group* page = new Fl_Group(x, y, w, h, "Runs"); page->color(paper);
    runs_browser_ = new Fl_Hold_Browser(x + 16, y + 20, 360, h - 100);
    runs_browser_->format_char(0);
    runs_browser_->callback(runs_select_cb, this);
    run_detail_ = new Fl_Text_Display(x + 398, y + 20, w - 416, h - 126);
    run_detail_->buffer(run_detail_buffer_); run_detail_->textfont(FL_COURIER);
    run_detail_->textsize(12);
    const int by = y + h - 92;
    Fl_Button* refresh = new Fl_Button(x + 16, by, 106, 34, "Refresh");
    refresh->callback(refresh_runs_cb, this);
    Fl_Button* open = new Fl_Button(x + 398, by, 112, 34, "Open folder");
    open->callback(open_run_cb, this);
    Fl_Button* bundle = new Fl_Button(x + 520, by, 174, 34, "Build support bundle");
    bundle->callback(bundle_cb, this); bundle->color(petrol); bundle->labelcolor(FL_WHITE);
    Fl_Button* remove = new Fl_Button(x + 704, by, 104, 34, "Delete…");
    remove->callback(delete_run_cb, this);
    page->end();
}

void manager_window::build_health_view(int x, int y, int w, int h) {
    Fl_Group* page = new Fl_Group(x, y, w, h, "Settings / Health"); page->color(paper);
    health_detail_ = new Fl_Text_Display(x + 16, y + 20, w - 32, h - 112);
    health_detail_->buffer(health_detail_buffer_); health_detail_->textfont(FL_COURIER);
    health_detail_->textsize(12);
    const int by = y + h - 76;
    Fl_Button* refresh = new Fl_Button(x + 16, by, 112, 34, "Recheck");
    refresh->callback(health_refresh_cb, this);
    Fl_Button* copy = new Fl_Button(x + 138, by, 142, 34, "Copy system report");
    copy->callback(health_copy_cb, this);
    Fl_Button* open = new Fl_Button(x + 290, by, 166, 34, "Open manager data");
    open->callback(health_open_cb, this);
    page->end();
}

bool manager_window::initialize_storage() {
    root_ = platform::manager_data_root();
    if (root_.empty()) {
        set_status("ERROR · platform data directory is unavailable", true);
        return false;
    }
    std::string error;
    const char* children[] = {"", "games", "logs"};
    for (std::size_t i = 0; i < sizeof(children) / sizeof(children[0]); i++) {
        const std::string path = children[i][0] == '\0' ? root_ : join_path(root_, children[i]);
        if (!platform::manager_ensure_private_directory(path, error)) {
            set_status("ERROR · " + error, true);
            return false;
        }
    }
    index_path_ = join_path(root_, "manager.json");
    return true;
}

void manager_window::load_state() {
    games_.clear();
    health_diagnostics_.clear();
    const manager_index_load_result loaded = load_manager_index(index_path_, transaction_files_);
    if (loaded.code == state_load_code::missing) {
        index_ = manager_index(); index_sha256_.clear(); return;
    }
    if (loaded.code != state_load_code::loaded) {
        index_ = manager_index();
        health_diagnostics_.push_back("manager.json: " + loaded.detail);
        set_status("ERROR · manager state is invalid; no file was overwritten", true);
        return;
    }
    index_ = loaded.state; index_sha256_ = loaded.sha256;
    for (std::size_t i = 0; i < index_.games.size(); i++) {
        const game_state_load_result game =
            load_game_state(index_.games[i].state_path, transaction_files_);
        if (game.code != state_load_code::loaded || game.state.id != index_.games[i].id) {
            health_diagnostics_.push_back(index_.games[i].state_path + ": " + game.detail);
            continue;
        }
        stored_game stored;
        stored.state = game.state; stored.state_path = index_.games[i].state_path;
        stored.sha256 = game.sha256; games_.push_back(stored);
    }
}

void manager_window::load_release_catalog() {
    installation_health_cache_.clear();
    catalog_valid_ = false; catalog_error_.clear(); catalog_ = release_catalog();
    const std::string executable = platform::manager_executable_path();
    const std::string package_root = parent_path(executable);
    std::string bytes;
    const run_io_result read = run_files_.read_file(join_path(package_root, "MANIFEST.json"),
                                                    1024 * 1024, bytes);
    if (read != run_io_result::ok ||
        !parse_release_catalog(bytes, package_root, catalog_, catalog_error_)) {
        if (catalog_error_.empty())
            catalog_error_ = "MANIFEST.json is not present (development build or damaged package)";
        return;
    }
    if (manager_job_running()) {
        catalog_error_ = "release verification deferred while another manager job is running";
        return;
    }
    if (worker_.joinable()) worker_.join();
    const release_catalog candidate = catalog_;
    {
        std::lock_guard<std::mutex> lock(worker_mutex_);
        worker_job_ = worker_job::release_verification;
        worker_running_ = true;
    }
    set_status("VERIFYING RELEASE · packaged SDK hashes on a bounded worker");
    worker_ = std::thread([this, candidate]() {
        bool valid = true;
        std::string error;
        for (std::size_t i = 0; i < candidate.artifacts.size(); i++) {
            const artifact_verification verified =
                verify_release_artifact(candidate.artifacts[i], discovery_files_);
            if (verified.code != artifact_verification_code::verified) {
                valid = false;
                error = "packaged SDK artifact failed size/hash/binary verification";
                break;
            }
        }
        {
            std::lock_guard<std::mutex> lock(worker_mutex_);
            pending_catalog_valid_ = valid;
            pending_catalog_error_ = error;
            worker_running_ = false;
        }
        Fl::awake(worker_awake, this);
    });
}

void manager_window::set_status(const std::string& text, bool error) {
    status_->copy_label(text.c_str()); status_->labelcolor(error ? danger : muted); redraw();
}

bool manager_window::manager_job_running() {
    {
        std::lock_guard<std::mutex> lock(worker_mutex_);
        if (worker_running_ || worker_job_ != worker_job::none) return true;
    }
    std::lock_guard<std::mutex> run_lock(run_worker_mutex_);
    return run_worker_running_ || run_worker_job_ != run_worker_job::none;
}

bool manager_window::run_job_running() {
    std::lock_guard<std::mutex> lock(run_worker_mutex_);
    return run_worker_running_ || run_worker_job_ != run_worker_job::none;
}

void manager_window::record_operation(const std::string& action, const std::string& object_id,
                                      const std::string& code, bool success) {
    if (root_.empty()) return;
    std::string random_error;
    const std::string id = platform::manager_random_id(random_error);
    if (id.empty()) {
        health_diagnostics_.push_back("operation log id unavailable");
        return;
    }
    operation_log_entry entry;
    entry.created_utc = platform::manager_utc_now();
    entry.action = action;
    entry.object_id = object_id;
    entry.code = code;
    entry.success = success;
    const operation_log_result written = write_operation_log(
        join_path(join_path(root_, "logs"), "operation-" + id + ".json"), entry,
        transaction_files_);
    if (written.code != operation_log_code::written)
        health_diagnostics_.push_back("operation log: " + written.detail);
}

void manager_window::refresh_all() {
    refresh_games(); refresh_instances(); refresh_runs(); refresh_health();
}

void manager_window::refresh_games() {
    games_browser_->clear();
    for (std::size_t i = 0; i < games_.size(); i++) {
        std::string label = games_[i].state.display_name;
        if (!games_[i].state.steam_app_id.empty()) label += "  ·  " + games_[i].state.steam_app_id;
        games_browser_->add(browser_item_label(label).c_str());
    }
    if (!games_.empty()) {
        if (selected_game_ >= games_.size()) selected_game_ = 0;
        games_browser_->value(static_cast<int>(selected_game_ + 1));
    }
    refresh_game_detail();
}

manager_window::stored_game* manager_window::selected_game() {
    return selected_game_ < games_.size() ? &games_[selected_game_] : 0;
}

const manager_window::stored_game* manager_window::selected_game() const {
    return selected_game_ < games_.size() ? &games_[selected_game_] : 0;
}

managed_target* manager_window::active_target(stored_game& game) {
    for (std::size_t i = 0; i < game.state.targets.size(); i++)
        if (game.state.targets[i].id == game.state.active_target_id) return &game.state.targets[i];
    return 0;
}

managed_instance* manager_window::active_instance(stored_game& game) {
    for (std::size_t i = 0; i < game.state.instances.size(); i++)
        if (game.state.instances[i].id == game.state.active_instance_id)
            return &game.state.instances[i];
    return 0;
}

std::string manager_window::game_root(const game_state& game) const {
    return join_path(join_path(root_, "games"), game.id);
}

std::string manager_window::instance_root_for_game(const game_state& game,
                                                   std::string& error) const {
    error.clear();
#ifdef _WIN32
    return join_path(game_root(game), "instances");
#else
    if (game.install_kind != steam_install_kind::flatpak_linux)
        return join_path(game_root(game), "instances");
    platform::flatpak_steam_path_roots roots;
    platform::proton_path_error path_error = platform::proton_path_error::none;
    if (!platform::flatpak_steam_manager_roots(game.steam_root, roots, path_error)) {
        error = "Flatpak Steam root cannot be proven for isolated runtime storage";
        return std::string();
    }
    return join_path(join_path(join_path(roots.manager_host_root, "games"), game.id),
                     "instances");
#endif
}

std::string manager_window::target_record_path(const game_state& game,
                                               const managed_target& target) const {
    return join_path(join_path(game_root(game), "targets"), target.id + ".json");
}

void manager_window::refresh_game_detail() {
    stored_game* game = selected_game();
    if (game == 0) {
        game_detail_buffer_->text(
            "FIRST RUN\n\nDiscover Steam libraries (including Flatpak Steam), or add a game "
            "folder manually. No game file changes until the final review.");
        install_button_->deactivate(); restore_button_->deactivate(); launch_button_->deactivate();
        return;
    }
    managed_target* target = active_target(*game);
    std::ostringstream text;
    text << game->state.display_name << "\n\nInstall root\n  " << game->state.install_root << "\n";
    text << "Source\n  " << (game->state.manual ? "Manual folder" :
        (game->state.install_kind == steam_install_kind::flatpak_linux ? "Flatpak Steam" :
                                                                        "Native Steam")) << "\n";
    if (target == 0) {
        text << "\nNo selected EOS target. Re-add or inspect this game manually.\n";
        install_button_->deactivate(); restore_button_->deactivate(); launch_button_->deactivate();
    } else {
        installation_probe probe;
        probe.target_path = target->path;
        probe.record_path = target_record_path(game->state, *target);
        probe.journal_path = target->path + ".eosr-journal.json";
        release_artifact artifact;
        const bool have_artifact = catalog_valid_ && catalog_artifact(catalog_, target->kind, artifact);
        if (have_artifact) probe.selected_artifact = artifact;
        for (std::size_t i = 0; i < catalog_.artifacts.size(); i++)
            if (catalog_.artifacts[i].kind == target->kind)
                probe.known_reimagined_sha256.push_back(catalog_.artifacts[i].sha256);
        probe.ambiguous = false;
        const std::map<std::string, installation_health>::const_iterator cached =
            installation_health_cache_.find(target->id);
        if (cached == installation_health_cache_.end()) {
            text << "\nSelected EOS target\n  " << target->path << "\n  " << kind_name(target->kind)
                 << "\n\nInstallation health\n  Inspecting on a bounded worker…"
                 << "\n\nRelease artifact\n  " <<
                    (have_artifact ? catalog_.release_id : catalog_error_);
            install_button_->deactivate();
            restore_button_->deactivate();
            launch_button_->deactivate();
            game_detail_buffer_->text(text.str().c_str());
            start_installation_inspection(target->id, probe);
            return;
        }
        const installation_health& health = cached->second;
        const game_action_policy actions =
            game_actions(health.state, have_artifact, !game->state.manual);
        text << "\nSelected EOS target\n  " << target->path << "\n  " << kind_name(target->kind)
             << "\n\nInstallation health\n  " << install_state_name(health.state);
        if (!health.detail.empty()) text << "\n  " << health.detail;
        if (!actions.next_action.empty()) text << "\n\nNext action\n  " << actions.next_action;
        text << "\n\nRelease artifact\n  " << (have_artifact ? catalog_.release_id : catalog_error_);
        install_button_->copy_label(actions.install_label.c_str());
        if (actions.install_update_enabled) install_button_->activate();
        else install_button_->deactivate();
        if (actions.restore_enabled) restore_button_->activate();
        else restore_button_->deactivate();
        if (actions.launch_enabled) launch_button_->activate();
        else launch_button_->deactivate();
    }
    game_detail_buffer_->text(text.str().c_str());
}

void manager_window::refresh_instances() {
    instances_browser_->clear();
    stored_game* game = selected_game();
    if (game == 0) {
        instance_detail_buffer_->text("Choose a managed game to create or select an instance.");
        return;
    }
    for (std::size_t i = 0; i < game->state.instances.size(); i++) {
        std::string label = game->state.instances[i].id == game->state.active_instance_id ?
            "● " : "  ";
        label += game->state.instances[i].display_name + "  ·  " + game->state.instances[i].slug;
        instances_browser_->add(browser_item_label(label).c_str());
    }
    if (!game->state.instances.empty()) {
        if (selected_instance_ >= game->state.instances.size()) selected_instance_ = 0;
        instances_browser_->value(static_cast<int>(selected_instance_ + 1));
        const managed_instance& instance = game->state.instances[selected_instance_];
        std::ostringstream text;
        text << "Manager label\n  " << instance.display_name
             << "\n  Cosmetic in this app; Edit label does not change eosr.json."
             << "\n\nEOS Reimagined network name\n  Configure → display_name"
             << "\n  The latest Run's runtime.json is authoritative."
             << "\n\nGame / Steam nickname\n  Separate; this manager never reads or writes it."
             << "\n\nStable manager id\n  " << instance.id
             << "\n\nInstance slug (diagnostic metadata)\n  " << instance.slug
             << "\n\nHost data directory\n  " << instance.data_dir
             << "\n\nTarget-runtime data directory\n  " << instance.target_data_dir
             << "\n\nIdentity\n  profile.key is never copied by presets or support bundles.";
        if (!instance.proton_prefix.empty())
            text << "\n\nVerified Proton mapping\n  " << instance.proton_mapping_name << " → "
                 << instance.proton_mapped_host_root << "\n  prefix: " << instance.proton_prefix;
        instance_detail_buffer_->text(text.str().c_str());
    } else instance_detail_buffer_->text("This game has no managed instance.");
}

void manager_window::refresh_runs() {
    {
        std::lock_guard<std::mutex> worker_lock(worker_mutex_);
        if (worker_job_ == worker_job::installation_transaction &&
            pending_launch_after_install_)
            return;
    }
    {
        std::lock_guard<std::mutex> lock(run_worker_mutex_);
        if (run_worker_running_ || run_worker_job_ != run_worker_job::none) {
            run_refresh_requested_ = true;
            return;
        }
    }
    if (run_worker_.joinable()) run_worker_.join();
    std::vector<std::pair<std::string, std::string> > sources;
    for (std::size_t g = 0; g < games_.size(); g++) {
        for (std::size_t i = 0; i < games_[g].state.instances.size(); i++) {
            const managed_instance& instance = games_[g].state.instances[i];
            manager_configuration config;
            std::string bytes; std::string error; bool malformed = false;
            std::string trace_root = join_path(instance.data_dir, "traces");
            if (load_instance_configuration(instance, config, bytes, malformed, error) &&
                !config.trace_dir.empty())
                trace_root = manager_absolute_path(config.trace_dir) ? config.trace_dir :
                    join_path(instance.data_dir, config.trace_dir);
            sources.push_back(std::make_pair(instance.id, trace_root));
        }
    }
    if (sources.empty()) {
        runs_.clear();
        runs_browser_->clear();
        refresh_run_detail();
        return;
    }
    run_summary_cache cache = run_cache_;
    {
        std::lock_guard<std::mutex> lock(run_worker_mutex_);
        run_worker_job_ = run_worker_job::index;
        run_worker_running_ = true;
        run_refresh_requested_ = false;
    }
    set_status("INDEXING RUNS · bounded streaming and hashes on a worker");
    run_worker_ = std::thread([this, sources, cache]() mutable {
        std::vector<run_summary> collected;
        for (std::size_t i = 0; i < sources.size(); i++) {
            const run_index_result indexed =
                index_diagnostic_runs(sources[i].second, run_files_, cache);
            for (std::size_t r = 0; r < indexed.runs.size(); r++) {
                run_summary summary = indexed.runs[r];
                summary.manager_instance_id = sources[i].first;
                collected.push_back(summary);
            }
        }
        {
            std::lock_guard<std::mutex> lock(run_worker_mutex_);
            pending_runs_ = collected;
            pending_run_cache_ = cache;
            run_worker_running_ = false;
        }
        Fl::awake(run_worker_awake, this);
    });
}

void manager_window::apply_run_worker_result() {
    run_worker_job completed = run_worker_job::none;
    std::vector<run_summary> indexed;
    run_summary_cache cache;
    support_bundle_result bundle;
    owned_tree_remove_result removed;
    std::string run_id;
    std::string remove_directory;
    bool refresh_again = false;
    {
        std::lock_guard<std::mutex> lock(run_worker_mutex_);
        if (run_worker_running_) return;
        completed = run_worker_job_;
        indexed = pending_runs_;
        cache = pending_run_cache_;
        bundle = pending_bundle_result_;
        removed = pending_remove_result_;
        run_id = pending_run_id_;
        remove_directory = pending_remove_directory_;
        refresh_again = run_refresh_requested_;
        run_refresh_requested_ = false;
        run_worker_job_ = run_worker_job::none;
    }
    if (run_worker_.joinable()) run_worker_.join();
    if (completed == run_worker_job::index) {
        run_cache_ = cache;
        runs_ = indexed;
        bool active_run_present = false;
        for (std::size_t i = 0; i < runs_.size(); i++) {
            if (active_runs_.count(runs_[i].run_id) != 0) {
                if (runs_[i].status == diagnostic_run_status::completed)
                    active_runs_.erase(runs_[i].run_id);
                else {
                    runs_[i].status = diagnostic_run_status::active;
                    active_run_present = true;
                }
            }
        }
        std::sort(runs_.begin(), runs_.end(), [](const run_summary& a, const run_summary& b) {
            if (a.created_utc != b.created_utc) return a.created_utc > b.created_utc;
            return a.run_id > b.run_id;
        });
        runs_browser_->clear();
        for (std::size_t i = 0; i < runs_.size(); i++) {
            const std::string label = std::string(run_state_name(runs_[i].status)) + "  ·  " +
                                      runs_[i].run_id + "  ·  " + runs_[i].emulator_build;
            runs_browser_->add(browser_item_label(label).c_str());
        }
        if (!runs_.empty()) {
            if (selected_run_ >= runs_.size()) selected_run_ = 0;
            runs_browser_->value(static_cast<int>(selected_run_ + 1));
        }
        if (!pending_observed_run_id_.empty()) {
            for (std::size_t i = 0; i < runs_.size(); i++) {
                if (runs_[i].run_id == pending_observed_run_id_) {
                    selected_run_ = i;
                    runs_browser_->value(static_cast<int>(i + 1));
                    break;
                }
            }
            pending_observed_run_id_.clear();
        }
        refresh_run_detail();
        if (run_index_completion_status(active_run_present) == run_index_status::running)
            set_status("RUNNING · runtime.json proves the selected SDK and instance loaded");
        else
            set_status("RUNS INDEXED · bounded summaries and cache updated");
    } else if (completed == run_worker_job::prelaunch_snapshot) {
        prelaunch_runs_.clear();
        for (std::size_t i = 0; i < indexed.size(); i++)
            prelaunch_runs_.insert(indexed[i].run_id);
        dispatch_steam_launch();
    } else if (completed == run_worker_job::launch_watch) {
        std::string observed;
        for (std::size_t i = 0; i < indexed.size(); i++) {
            if (prelaunch_runs_.count(indexed[i].run_id) == 0 &&
                indexed[i].sdk_initialized) {
                observed = indexed[i].run_id;
                break;
            }
        }
        if (!observed.empty()) {
            active_runs_.insert(observed);
            pending_observed_run_id_ = observed;
            launch_watch_ticks_ = 0;
            launch_submission_.reset();
            watched_trace_root_.clear();
            refresh_runs();
            set_status("RUNNING · runtime.json proves the selected SDK and instance loaded");
        } else if (launch_watch_ticks_ >= 45) {
            launch_watch_ticks_ = 0;
            launch_submission_.reset();
            watched_trace_root_.clear();
            set_status("LAUNCH WARNING · no new runtime.json after 45 seconds — open game and trace "
                       "folders, confirm lifecycle tracing, or use Steam Verify Installed Files",
                       true);
        } else {
            launch_watch_ticks_++;
            Fl::remove_timeout(launch_watch_cb, this);
            Fl::add_timeout(1.0, launch_watch_cb, this);
        }
    } else if (completed == run_worker_job::support_bundle) {
        const bool created = bundle.code == support_bundle_code::created && bundle.shareable;
        record_operation("support_bundle", run_id, created ? "created" : "create_failed", created);
        if (!created) {
            set_status("BUNDLE NOT SHAREABLE · " + bundle.detail, true);
        } else {
            std::ostringstream message;
            message << "Sanitized support bundle created.\n\nIncluded trace files: " <<
                bundle.included_trace_files.size() << "\nExcluded files: " <<
                bundle.excluded_files.size() << "\nTorn final records dropped: " <<
                bundle.torn_tail_records_dropped <<
                "\n\nReview summary.json before sharing.";
            fl_message("%s", message.str().c_str());
            set_status("SHAREABLE AFTER REVIEW · deny-by-default support bundle created");
            const platform::manager_action_result opened =
                platform::manager_open_location(bundle.directory, false);
            if (!opened.ok)
                set_status("BUNDLE CREATED · folder opener failed: " + opened.detail, true);
        }
    } else if (completed == run_worker_job::remove_run) {
        const bool removed_ok = removed.code == owned_tree_remove_code::removed ||
                                removed.code == owned_tree_remove_code::missing;
        record_operation("run_delete", run_id,
                         removed.code == owned_tree_remove_code::removed ? "removed" :
                         (removed.code == owned_tree_remove_code::missing ? "missing" :
                                                                           "cleanup_incomplete"),
                         removed_ok);
        if (removed_ok) {
            set_status("DELETED · diagnostic run removed after complete preflight");
            selected_run_ = 0;
            refresh_runs();
        } else {
            set_status("DELETE REFUSED / INCOMPLETE · " + removed.detail, true);
        }
    } else if (completed == run_worker_job::remove_instance) {
        const bool removed_ok = removed.code == owned_tree_remove_code::removed ||
                                removed.code == owned_tree_remove_code::missing;
        record_operation("instance_delete", run_id,
                         removed.code == owned_tree_remove_code::removed ? "removed" :
                         (removed.code == owned_tree_remove_code::missing ? "missing" :
                                                                           "cleanup_incomplete"),
                         removed_ok);
        if (removed_ok) {
            set_status("DELETED · managed instance and owned directory removed");
        } else {
            health_diagnostics_.push_back(remove_directory + ": " + removed.detail);
            set_status("INSTANCE REMOVED · directory cleanup incomplete; see Health", true);
        }
        selected_instance_ = 0;
        refresh_instances();
        refresh_runs();
        refresh_health();
    }
    if (installation_inspection_retry_.take()) refresh_game_detail();
    if (refresh_again) refresh_runs();
}

void manager_window::run_worker_awake(void* self) {
    static_cast<manager_window*>(self)->apply_run_worker_result();
}

void manager_window::refresh_run_detail() {
    if (selected_run_ >= runs_.size()) {
        run_detail_buffer_->text("No diagnostic runs found. Enable lifecycle tracing and launch "
                                 "through Steam; runtime.json proves the SDK loaded.");
        return;
    }
    const run_summary& run = runs_[selected_run_];
    std::ostringstream text;
    text << run.run_id << "\n\nStatus\n  " << run_state_name(run.status)
         << "\nBuild\n  " << run.emulator_build << "\nCreated\n  " << run.created_utc
         << "\nOS\n  " << run.os_name;
    if (!run.wine_version.empty()) text << " / " << run.wine_version;
    text << "\nTrace level\n  " << run.trace_level
         << "\nSDK initialized\n  " << (run.sdk_initialized ? "yes" : "no")
         << "\nDiscovery port\n  " << run.discovery_port
         << "\n\nMesh lifecycle\n  discover " << run.discover_count << " · handshake "
         << run.handshake_count << " · adopt " << run.adopt_count << " · drop " << run.drop_count
         << "\nP2P\n  open " << run.p2p_open_count << " · close " << run.p2p_close_count
         << " · sent " << run.p2p_bytes_sent << " B · received " << run.p2p_bytes_received << " B";
    if (run.effective_config_present) {
        text << "\n\nEffective configuration (runtime.json)"
             << "\n  display_name (EOSR network): " << run.effective_display_name <<
                effective_source_label(run, "display_name")
             << "\n  instance_label (diagnostic slug): " << run.instance_label <<
                effective_source_label(run, "instance_label")
             << "\n  locale: " << run.effective_locale << effective_source_label(run, "locale")
             << "\n  log_level: " << run.effective_log_level <<
                effective_source_label(run, "log_level")
             << "\n  trace_level: " << run.trace_level <<
                effective_source_label(run, "trace_level")
             << "\n  trace_dir: " << run.effective_trace_dir <<
                effective_source_label(run, "trace_dir")
             << "\n  trace_max_bytes: " << run.effective_trace_max_bytes <<
                effective_source_label(run, "trace_max_bytes")
             << "\n  trace_max_rotated_files: " << run.effective_trace_max_rotated_files <<
                effective_source_label(run, "trace_max_rotated_files")
             << "\n  enable_lan: " << (run.effective_enable_lan ? "true" : "false") <<
                effective_source_label(run, "enable_lan")
             << "\n  discovery_ports: " << run.effective_discovery_first << "–" <<
                run.effective_discovery_last << effective_source_label(run, "discovery_ports")
             << "\n  peer_seed_count: " << run.effective_peer_seed_count <<
                effective_source_label(run, "peer_seeds")
             << "\n  enable_overlay: " << (run.effective_enable_overlay ? "true" : "false") <<
                effective_source_label(run, "enable_overlay")
             << "\n  unlock_dlcs: " << (run.effective_unlock_dlcs ? "true" : "false") <<
                effective_source_label(run, "unlock_dlcs");
        bool found_saved = false;
        manager_configuration configured;
        for (std::size_t g = 0; g < games_.size() && !found_saved; g++) {
            for (std::size_t i = 0; i < games_[g].state.instances.size(); i++) {
                const managed_instance& instance = games_[g].state.instances[i];
                if (instance.id != run.manager_instance_id) continue;
                std::string bytes;
                std::string error;
                bool malformed = false;
                found_saved = load_instance_configuration(instance, configured, bytes, malformed,
                                                           error) && !malformed;
                if (found_saved) break;
            }
        }
        if (found_saved) {
            const display_name_evidence name_evidence = display_name_status(
                configured.display_name, run.effective_display_name, true);
            text << "\n\nSaved configuration (eosr.json)"
                 << "\n  display_name (EOSR network): " << configured.display_name
                 << "\n  instance_label (diagnostic slug): " << configured.instance_label
                 << "\n  locale: " << configured.locale
                 << "\n  log_level: " << configured.log_level
                 << "\n  trace_level: " << configured.trace_level
                 << "\n  trace_dir: " << configured.trace_dir
                 << "\n  trace_max_bytes: " << configured.trace_max_bytes
                 << "\n  trace_max_rotated_files: " << configured.trace_max_rotated_files
                 << "\n  enable_lan: " << (configured.enable_lan ? "true" : "false")
                 << "\n  discovery_ports: " << configured.discovery_first << "–" <<
                    configured.discovery_last
                 << "\n  peer_seed_count: " << configured.peer_seeds.size()
                 << "\n  enable_overlay: " << (configured.enable_overlay ? "true" : "false")
                 << "\n  unlock_dlcs: " << (configured.unlock_dlcs ? "true" : "false")
                 << "\n\nName evidence\n  " << name_evidence.next_action;
        }
        text << "\n\nGame / Steam nickname\n  Separate from display_name and never synchronized "
                "by EOS Reimagined.";
    }
    if (!run.operations.empty()) {
        text << "\n\nSession / Lobby outcomes";
        for (std::size_t i = 0; i < run.operations.size(); i++)
            text << "\n  " << run.operations[i].function << ": " << run.operations[i].successes
                 << " success, " << run.operations[i].failures << " failure";
    }
    if (!run.stubbed_functions.empty()) {
        text << "\n\nStubbed functions invoked";
        for (std::size_t i = 0; i < run.stubbed_functions.size(); i++)
            text << "\n  " << run.stubbed_functions[i];
    }
    if (run.torn_tail_records != 0)
        text << "\n\nTorn final records ignored\n  " << run.torn_tail_records;
    if (run.trace_invalid) text << "\n\nTRACE INVALID\n  " << run.diagnostic;
    run_detail_buffer_->text(text.str().c_str());
}

void manager_window::refresh_health() {
    std::ostringstream text;
    text << application_name() << "\n" << build_id() << "\n\nManager data\n  " <<
        (root_.empty() ? "unavailable" : root_) << "\nExecutable\n  " <<
        platform::manager_executable_path() << "\n\nRelease catalog\n  " <<
        (catalog_valid_ ? catalog_.release_id : catalog_error_);
    if (catalog_valid_) {
        for (std::size_t i = 0; i < catalog_.artifacts.size(); i++)
            text << "\n  verified: " << kind_name(catalog_.artifacts[i].kind) << " · "
                 << catalog_.artifacts[i].sha256;
    }
    text << "\n\nManaged games\n  " << games_.size()
         << "\nDiscovered Steam games (last scan)\n  " << discovered_games_.size();
    if (!discovered_steam_.empty()) {
        text << "\n\nSteam installations and libraries";
        for (std::size_t i = 0; i < discovered_steam_.size(); i++) {
            text << "\n  " << discovered_steam_[i].root;
            for (std::size_t j = 0; j < discovered_steam_[i].libraries.size(); j++)
                text << "\n    library: " << discovered_steam_[i].libraries[j];
        }
    }
    if (!root_.empty()) text << "\n\nManager log directory\n  " << join_path(root_, "logs");
    text << "\n\nSafety policy\n  localconfig.vdf is never edited\n  no undocumented Steam IPC\n"
            "  unknown backups and changed files are never overwritten\n"
            "  support bundles are shareable only after deny-by-default sanitization";
    if (!health_diagnostics_.empty()) {
        text << "\n\nState diagnostics";
        for (std::size_t i = 0; i < health_diagnostics_.size(); i++)
            text << "\n  " << health_diagnostics_[i];
    }
    health_detail_buffer_->text(text.str().c_str());
}

bool manager_window::save_index() {
    manager_index next = index_;
    next.games.clear();
    if (catalog_valid_) next.selected_release_id = catalog_.release_id;
    for (std::size_t i = 0; i < games_.size(); i++) {
        game_reference reference;
        reference.id = games_[i].state.id;
        reference.state_path = games_[i].state_path;
        next.games.push_back(reference);
    }
    std::string random_error;
    const std::string transaction = platform::manager_random_id(random_error);
    if (transaction.empty()) {
        set_status("ERROR · manager state cannot be staged: " + random_error, true);
        return false;
    }
    state_save_request request;
    request.path = index_path_;
    request.temporary_path = index_path_ + ".eosr-stage-" + transaction;
    request.expected_sha256 = index_sha256_;
    const state_save_result saved =
        save_manager_index(request, next, transaction_files_);
    if (saved.code != state_save_code::saved) {
        set_status("ERROR · manager.json was not changed: " + saved.detail, true);
        return false;
    }
    index_ = next;
    index_sha256_ = saved.saved_sha256;
    return true;
}

bool manager_window::save_game(stored_game& game) {
    std::string random_error;
    const std::string transaction = platform::manager_random_id(random_error);
    if (transaction.empty()) {
        set_status("ERROR · game state cannot be staged: " + random_error, true);
        return false;
    }
    state_save_request request;
    request.path = game.state_path;
    request.temporary_path = game.state_path + ".eosr-stage-" + transaction;
    request.expected_sha256 = game.sha256;
    const state_save_result saved =
        save_game_state(request, game.state, transaction_files_);
    if (saved.code != state_save_code::saved) {
        set_status("ERROR · game state was not changed: " + saved.detail, true);
        return false;
    }
    game.sha256 = saved.saved_sha256;
    return true;
}

bool manager_window::configure_runtime_path(game_state& game, managed_target& target,
                                            managed_instance& instance, std::string& error) {
    error.clear();
#ifdef _WIN32
    (void)game;
    (void)target;
    instance.target_data_dir = instance.data_dir;
    instance.proton_prefix.clear();
    instance.proton_mapping_name.clear();
    instance.proton_mapped_host_root.clear();
    return true;
#else
    if (game.install_kind == steam_install_kind::flatpak_linux &&
        target.kind != eos_binary_kind::windows_x86_64) {
        platform::proton_path_error path_error = platform::proton_path_error::none;
        std::string runtime_path;
        if (!platform::flatpak_steam_runtime_path(game.steam_root, instance.data_dir,
                                                  runtime_path, path_error)) {
            error = "Flatpak Steam cannot map this host instance directory into its sandbox";
            return false;
        }
        instance.target_data_dir = runtime_path;
        instance.proton_prefix.clear();
        instance.proton_mapping_name.clear();
        instance.proton_mapped_host_root.clear();
        return true;
    }
    if (target.kind != eos_binary_kind::windows_x86_64) {
        instance.target_data_dir = instance.data_dir;
        instance.proton_prefix.clear();
        instance.proton_mapping_name.clear();
        instance.proton_mapped_host_root.clear();
        return true;
    }
    if (game.steam_app_id.empty() || game.library_root.empty()) {
        error = "Windows targets on Linux need a discovered Steam app and verified Proton prefix";
        return false;
    }
    const std::string prefix = join_path(join_path(join_path(game.library_root, "steamapps"),
                                                   "compatdata"),
                                         join_path(game.steam_app_id, "pfx"));
    if (!instance.proton_prefix.empty()) {
        platform::proton_path_translation stored;
        stored.prefix_path = instance.proton_prefix;
        stored.host_path = instance.data_dir;
        stored.windows_path = instance.target_data_dir;
        stored.mapped_host_root = instance.proton_mapped_host_root;
        stored.mapping_name = instance.proton_mapping_name;
        stored.drive = stored.windows_path.empty() ? 0 : stored.windows_path[0];
        platform::proton_path_error mapping_error = platform::proton_path_error::none;
        const bool valid = game.install_kind == steam_install_kind::flatpak_linux ?
            platform::validate_flatpak_proton_path_translation(
                game.steam_root, prefix, stored, mapping_error) :
            platform::validate_proton_path_translation(prefix, stored, mapping_error);
        if (prefix != instance.proton_prefix || !valid) {
            error = "Proton drive mapping changed; instance selection is disabled until the "
                    "prefix and runtime namespace mapping are inspected again";
            return false;
        }
        return true;
    }
    platform::proton_path_translation translated;
    platform::proton_path_error mapping_error = platform::proton_path_error::none;
    const bool translated_ok = game.install_kind == steam_install_kind::flatpak_linux ?
        platform::translate_flatpak_host_path_for_proton(
            game.steam_root, prefix, instance.data_dir, translated, mapping_error) :
        platform::translate_host_path_for_proton(
            prefix, instance.data_dir, translated, mapping_error);
    if (!translated_ok) {
        error = "No Proton and runtime-namespace mapping safely round-trips to this managed "
                "instance directory";
        return false;
    }
    instance.target_data_dir = translated.windows_path;
    instance.proton_prefix = translated.prefix_path;
    instance.proton_mapping_name = translated.mapping_name;
    instance.proton_mapped_host_root = translated.mapped_host_root;
    return true;
#endif
}

bool manager_window::load_instance_configuration(const managed_instance& instance,
                                                 manager_configuration& configuration,
                                                 std::string& bytes, bool& malformed,
                                                 std::string& error) {
    malformed = false;
    error.clear();
    const std::string path = join_path(instance.data_dir, "eosr.json");
    const run_io_result read = run_files_.read_file(path, 1024 * 1024, bytes);
    if (read != run_io_result::ok) {
        error = read == run_io_result::missing ? "eosr.json is missing" :
                                                "eosr.json cannot be read safely";
        return false;
    }
    if (!parse_manager_configuration(bytes, configuration, error)) {
        malformed = true;
        configuration = new_instance_configuration(instance.display_name, instance.slug, true);
        return true;
    }
    return true;
}

bool manager_window::edit_configuration(manager_configuration& configuration, bool malformed,
                                        bool& replace_confirmed) {
    replace_confirmed = false;
    return configuration_dialog(configuration, malformed, replace_confirmed);
}

bool manager_window::save_instance_configuration(
    stored_game& game, managed_instance& instance, const manager_configuration& configuration,
    const std::string& expected_sha256, bool malformed, bool replace_confirmed) {
    std::string random_error;
    const std::string transaction = platform::manager_random_id(random_error);
    if (transaction.empty()) {
        set_status("ERROR · configuration cannot be staged: " + random_error, true);
        return false;
    }
    const std::string path = join_path(instance.data_dir, "eosr.json");
    configuration_save_request request;
    request.path = path;
    request.temporary_path = path + ".eosr-stage-" + transaction;
    request.backup_path = path + ".eosr-import-backup";
    request.expected_sha256 = expected_sha256;
    request.owned_backup_sha256 = instance.config_backup_sha256;
    request.existing_was_malformed = malformed;
    request.replace_confirmed = replace_confirmed;
    const configuration_save_result saved =
        save_manager_configuration(request, configuration, transaction_files_);
    if (saved.code != configuration_save_code::saved) {
        record_operation("configure", instance.id, config_code_name(saved.code), false);
        set_status("ERROR · eosr.json was not changed: " + saved.detail, true);
        return false;
    }
    apply_saved_configuration_metadata(instance, saved);
    if (!save_game(game)) {
        record_operation("configure", instance.id, "state_commit_failed", false);
        health_diagnostics_.push_back(
            "eosr.json was saved, but its ownership hash could not be committed to game state");
        refresh_health();
        return false;
    }
    record_operation("configure", instance.id, "saved", true);
    set_status("SAVED · configuration committed atomically");
    return true;
}

void manager_window::configure_instance() {
    if (manager_job_running()) {
        set_status("WAIT · configuration is locked while a bounded manager job runs");
        return;
    }
    stored_game* game = selected_game();
    managed_instance* instance = game == 0 ? 0 : active_instance(*game);
    if (game == 0 || instance == 0) {
        set_status("Choose a managed game and active instance first", true);
        return;
    }
    manager_configuration configuration;
    std::string bytes;
    std::string error;
    bool malformed = false;
    if (!load_instance_configuration(*instance, configuration, bytes, malformed, error)) {
        set_status("ERROR · " + error + " — open the instance folder", true);
        return;
    }
    if (!configuration.import_issues.empty()) {
        fl_message("The existing configuration contains %lu known field(s) with the wrong type. "
                   "Their SDK defaults are shown; unknown valid keys remain preserved.",
                   static_cast<unsigned long>(configuration.import_issues.size()));
    }
    configuration.instance_label = instance->slug;
    bool replace_confirmed = false;
    if (!edit_configuration(configuration, malformed, replace_confirmed)) return;
    configuration.instance_label = instance->slug;
    const std::string expected = sha256_hex(bytes);
    if (save_instance_configuration(*game, *instance, configuration, expected, malformed,
                                    replace_confirmed)) {
        refresh_all();
    }
}

bool manager_window::install_or_update(bool launch_after) {
    stored_game* game = selected_game();
    managed_target* target = game == 0 ? 0 : active_target(*game);
    managed_instance* instance = game == 0 ? 0 : active_instance(*game);
    if (game == 0 || target == 0 || instance == 0) {
        set_status("Choose a game target and active instance first", true);
        return false;
    }
    release_artifact artifact;
    if (!catalog_valid_ || !catalog_artifact(catalog_, target->kind, artifact)) {
        set_status("ERROR · packaged release artifact is unavailable or unverified", true);
        return false;
    }
    if (manager_job_running()) {
        set_status("WAIT · another bounded manager job is still running");
        return false;
    }
    std::string path_error;
    if (!configure_runtime_path(game->state, *target, *instance, path_error)) {
        set_status("ERROR · " + path_error, true);
        return false;
    }
    if (!save_game(*game)) return false;

    installation_probe probe;
    probe.target_path = target->path;
    probe.record_path = target_record_path(game->state, *target);
    probe.journal_path = target->path + ".eosr-journal.json";
    probe.selected_artifact = artifact;
    probe.known_reimagined_sha256.push_back(artifact.sha256);
    probe.ambiguous = false;
    std::string random_error;
    const std::string transaction = platform::manager_random_id(random_error);
    if (transaction.empty()) {
        set_status("ERROR · transaction id unavailable: " + random_error, true);
        return false;
    }
    install_request install;
    install.transaction_id = transaction;
    install.release_id = catalog_.release_id;
    install.target_path = target->path;
    install.artifact = artifact;
    install.backup_path = target->path + ".eosr-original";
    install.journal_path = probe.journal_path;
    install.descriptor_path = join_path(parent_path(target->path), "eosr-bootstrap.json");
    install.record_path = probe.record_path;
    install.data_dir = instance->target_data_dir;
    update_request update;
    update.transaction_id = transaction;
    update.release_id = catalog_.release_id;
    update.record_path = probe.record_path;
    update.artifact = artifact;
    update.data_dir = instance->target_data_dir;

    if (worker_.joinable()) worker_.join();
    {
        std::lock_guard<std::mutex> lock(worker_mutex_);
        worker_job_ = worker_job::installation_transaction;
        worker_running_ = true;
        pending_target_id_ = target->id;
        pending_launch_after_install_ = launch_after;
        pending_launch_game_id_ = game->state.id;
        pending_launch_target_id_ = target->id;
        pending_launch_instance_id_ = instance->id;
    }
    installation_health_cache_.erase(target->id);
    install_button_->deactivate(); restore_button_->deactivate(); launch_button_->deactivate();
    set_status("TRANSACTION RUNNING · inspection, hashes, journal, and replacement are bounded");
    worker_ = std::thread([this, probe, install, update]() {
        const installation_health health = inspect_installation(probe, transaction_files_);
        install_result result;
        std::string action;
        if (health.state == installation_state::recovery_required) {
            action = "recover";
            result = recover_installation(probe.journal_path, transaction_files_);
        } else if (health.state == installation_state::original) {
            action = "install";
            result = install_release(install, transaction_files_);
        } else if (health.state == installation_state::installed_current ||
                   health.state == installation_state::installed_other_build) {
            action = "update";
            result = update_installation(update, transaction_files_);
        } else {
            action = "install";
            result.code = install_result_code::externally_changed;
            result.detail = health.detail.empty() ?
                "installation state has no safe automatic transition" : health.detail;
            result.target_is_reimagined =
                health.state == installation_state::original_unknown;
        }
        {
            std::lock_guard<std::mutex> lock(worker_mutex_);
            pending_installation_health_ = health;
            pending_install_result_ = result;
            pending_operation_action_ = action;
            worker_running_ = false;
        }
        Fl::awake(worker_awake, this);
    });
    return true;
}

void manager_window::restore_target() {
    stored_game* game = selected_game();
    managed_target* target = game == 0 ? 0 : active_target(*game);
    if (game == 0 || target == 0) {
        set_status("Choose a managed EOS target first", true);
        return;
    }
    if (manager_job_running()) {
        set_status("WAIT · another bounded manager job is still running");
        return;
    }
    if (fl_choice("Restore the hash-verified original EOS library? Changed or unknown files will "
                  "be left untouched.", "Cancel", "Restore", 0) != 1) return;
    const std::string record_path = target_record_path(game->state, *target);
    const std::string target_id = target->id;
    if (worker_.joinable()) worker_.join();
    {
        std::lock_guard<std::mutex> lock(worker_mutex_);
        worker_job_ = worker_job::restore_transaction;
        worker_running_ = true;
        pending_target_id_ = target_id;
        pending_operation_action_ = "restore";
        pending_launch_after_install_ = false;
    }
    installation_health_cache_.erase(target_id);
    install_button_->deactivate(); restore_button_->deactivate(); launch_button_->deactivate();
    set_status("RESTORING · live, backup, journal, and descriptor hashes are being verified");
    worker_ = std::thread([this, record_path]() {
        const install_result restored = restore_installation(record_path, transaction_files_);
        {
            std::lock_guard<std::mutex> lock(worker_mutex_);
            pending_install_result_ = restored;
            worker_running_ = false;
        }
        Fl::awake(worker_awake, this);
    });
}

void manager_window::create_instance() {
    if (manager_job_running()) {
        set_status("WAIT · instance changes are locked while a bounded manager job runs");
        return;
    }
    stored_game* game = selected_game();
    if (game == 0) {
        set_status("Choose a managed game first", true);
        return;
    }
    const char* name_value = fl_input(
        "EOS Reimagined network display name (the game/Steam nickname is separate):", "Player");
    if (name_value == 0) return;
    const std::string display_name = name_value;
    const std::string suggested = safe_slug(display_name);
    const char* slug_value = fl_input("Short local instance label (letters, digits, hyphen):",
                                      suggested.c_str());
    if (slug_value == 0) return;
    std::string random_error;
    const std::string id = platform::manager_random_id(random_error);
    if (id.empty()) {
        set_status("ERROR · instance identity unavailable: " + random_error, true);
        return;
    }
    std::string storage_error;
    const std::string instances_root = instance_root_for_game(game->state, storage_error);
    if (instances_root.empty()) {
        set_status("ERROR · " + storage_error, true);
        return;
    }
    std::string directory_error;
    const std::string storage_manager_root =
        parent_path(parent_path(parent_path(instances_root)));
    if (storage_manager_root.empty() ||
        !platform::manager_ensure_private_directory(storage_manager_root, directory_error) ||
        !platform::manager_ensure_private_directory(instances_root, directory_error)) {
        set_status("ERROR · instance root is unavailable: " + directory_error, true);
        return;
    }
    instance_provision_request request;
    request.model.id = id;
    request.model.slug = slug_value;
    request.model.display_name = display_name;
    request.model.instances_root = instances_root;
    request.configuration = new_instance_configuration(display_name, slug_value, true);
    const instance_provision_result provisioned =
        provision_instance(game->state, request, run_files_, transaction_files_);
    if (provisioned.code != instance_provision_code::provisioned) {
        set_status("INSTANCE NOT CREATED · " + provisioned.detail, true);
        return;
    }
    managed_target* target = active_target(*game);
    managed_instance* instance = active_instance(*game);
    std::string mapping_error;
    if (target != 0 && instance != 0 &&
        !configure_runtime_path(game->state, *target, *instance, mapping_error)) {
        health_diagnostics_.push_back(instance->display_name + ": " + mapping_error);
    }
    if (!save_game(*game)) {
        health_diagnostics_.push_back("new instance directory exists but game state was not saved");
        return;
    }
    record_operation("instance_create", id, "created", true);
    selected_instance_ = game->state.instances.size() - 1;
    set_status("CREATED · isolated profile, configuration, and trace directory");
    refresh_all();
}

void manager_window::select_instance() {
    if (manager_job_running()) {
        set_status("WAIT · instance changes are locked while a bounded manager job runs");
        return;
    }
    stored_game* game = selected_game();
    if (game == 0 || selected_instance_ >= game->state.instances.size()) {
        set_status("Choose an instance first", true);
        return;
    }
    managed_target* target = active_target(*game);
    if (target == 0) {
        set_status("The game has no selected EOS target", true);
        return;
    }
    bool in_use = false;
    if (transaction_files_.in_use(target->path, in_use) != transaction_io_result::ok || in_use) {
        set_status("REFUSED · game target is running or its use cannot be ruled out", true);
        return;
    }
    const game_state previous = game->state;
    managed_instance candidate = game->state.instances[selected_instance_];
    std::string mapping_error;
    if (!configure_runtime_path(game->state, *target, candidate, mapping_error)) {
        set_status("INSTANCE DISABLED · " + mapping_error, true);
        return;
    }
    release_artifact artifact;
    const bool have_artifact = catalog_valid_ && catalog_artifact(catalog_, target->kind, artifact);
    installation_probe probe;
    probe.target_path = target->path;
    probe.record_path = target_record_path(game->state, *target);
    probe.journal_path = target->path + ".eosr-journal.json";
    probe.selected_artifact = artifact;
    if (have_artifact) probe.known_reimagined_sha256.push_back(artifact.sha256);
    probe.ambiguous = false;
    std::string random_error;
    update_request update;
    update.transaction_id = platform::manager_random_id(random_error);
    update.release_id = catalog_.release_id;
    update.record_path = probe.record_path;
    update.artifact = artifact;
    update.data_dir = candidate.target_data_dir;
    update_request rollback = update;
    rollback.transaction_id = platform::manager_random_id(random_error);
    for (std::size_t i = 0; i < previous.instances.size(); i++)
        if (previous.instances[i].id == previous.active_instance_id)
            rollback.data_dir = previous.instances[i].target_data_dir;
    if (update.transaction_id.empty() || rollback.transaction_id.empty()) {
        set_status("ERROR · instance switch transaction unavailable: " + random_error, true);
        return;
    }
    if (worker_.joinable()) worker_.join();
    {
        std::lock_guard<std::mutex> lock(worker_mutex_);
        worker_job_ = worker_job::instance_switch_transaction;
        worker_running_ = true;
        pending_target_id_ = target->id;
        pending_switch_previous_ = previous;
        pending_switch_candidate_ = candidate;
        pending_switch_rollback_ = rollback;
        pending_switch_descriptor_changed_ = false;
    }
    set_status("SWITCHING INSTANCE · target hashes and descriptor update run on a worker");
    worker_ = std::thread([this, probe, update, have_artifact]() {
        const installation_health health = inspect_installation(probe, transaction_files_);
        const instance_switch_action action = instance_switch_for(health.state, have_artifact);
        install_result result;
        bool descriptor_changed = false;
        if (action == instance_switch_action::update_owned_descriptor) {
            result = update_installation(update, transaction_files_);
            descriptor_changed = result.code == install_result_code::installed ||
                (result.code == install_result_code::cleanup_incomplete &&
                 result.target_is_reimagined);
        } else if (action == instance_switch_action::commit_state_only) {
            result.code = install_result_code::installed;
        } else {
            result.code = install_result_code::externally_changed;
            result.detail = health.detail.empty() ?
                "installation health must be repaired before switching instances" : health.detail;
        }
        {
            std::lock_guard<std::mutex> lock(worker_mutex_);
            pending_installation_health_ = health;
            pending_install_result_ = result;
            pending_switch_descriptor_changed_ = descriptor_changed;
            worker_running_ = false;
        }
        Fl::awake(worker_awake, this);
    });
}

void manager_window::rename_instance() {
    if (manager_job_running()) {
        set_status("WAIT · instance changes are locked while a bounded manager job runs");
        return;
    }
    stored_game* game = selected_game();
    if (game == 0 || selected_instance_ >= game->state.instances.size()) return;
    const managed_instance& instance = game->state.instances[selected_instance_];
    const char* value = fl_input(
        "New manager label (does not change eosr.json, profile.key, paths, or the game nickname):",
                                 instance.display_name.c_str());
    if (value == 0) return;
    const game_state previous = game->state;
    const instance_operation renamed = rename_instance_model(game->state, instance.id, value);
    if (renamed.code != instance_operation_code::renamed || !save_game(*game)) {
        game->state = previous;
        set_status("RENAME REFUSED · state was not changed", true);
        return;
    }
    record_operation("instance_rename", instance.id, "renamed", true);
    set_status("RENAMED · stable id, slug, and data directory unchanged");
    refresh_all();
}

void manager_window::export_identity() {
    if (manager_job_running()) {
        set_status("WAIT · identity export is locked while a bounded manager job runs");
        return;
    }
    stored_game* game = selected_game();
    if (game == 0 || selected_instance_ >= game->state.instances.size()) return;
    const managed_instance& instance = game->state.instances[selected_instance_];
    Fl_Native_File_Chooser chooser;
    chooser.title("Export private profile identity backup");
    chooser.type(Fl_Native_File_Chooser::BROWSE_SAVE_FILE);
    chooser.options(Fl_Native_File_Chooser::SAVEAS_CONFIRM);
    chooser.preset_file((instance.slug + "-profile.key").c_str());
    if (chooser.show() != 0 || chooser.filename() == 0) return;
    identity_export_request request;
    request.source_path = join_path(instance.data_dir, "profile.key");
    request.destination_path = chooser.filename();
    const identity_export_result exported = export_instance_identity(request, transaction_files_);
    record_operation("identity_export", instance.id, identity_code_name(exported.code),
                     exported.code == identity_export_code::exported);
    if (exported.code == identity_export_code::exported)
        set_status("EXPORTED · private identity copy verified (SHA-256 " + exported.sha256 + ")");
    else
        set_status("EXPORT REFUSED · " + exported.detail, true);
}

void manager_window::delete_instance() {
    if (manager_job_running()) {
        set_status("WAIT · instance changes are locked while a bounded manager job runs");
        return;
    }
    stored_game* game = selected_game();
    if (game == 0 || selected_instance_ >= game->state.instances.size()) return;
    const managed_instance instance = game->state.instances[selected_instance_];
    transaction_file_info profile;
    const bool profile_exists = transaction_files_.inspect(
        join_path(instance.data_dir, "profile.key"), profile) == transaction_io_result::ok;
    identity_disposition disposition = identity_disposition::destroy_confirmed;
    if (profile_exists) {
        const int choice = fl_choice(
            "Deleting this instance can permanently lose its player identity. Retain leaves the "
            "directory untouched; Export verifies a private copy before deletion.",
            "Cancel", "Retain data", "Export + delete");
        if (choice == 0) return;
        if (choice == 1) disposition = identity_disposition::retain_directory;
        if (choice == 2) {
            Fl_Native_File_Chooser chooser;
            chooser.title("Export identity before deleting instance");
            chooser.type(Fl_Native_File_Chooser::BROWSE_SAVE_FILE);
            chooser.options(Fl_Native_File_Chooser::SAVEAS_CONFIRM);
            chooser.preset_file((instance.slug + "-profile.key").c_str());
            if (chooser.show() != 0 || chooser.filename() == 0) return;
            identity_export_request export_request;
            export_request.source_path = join_path(instance.data_dir, "profile.key");
            export_request.destination_path = chooser.filename();
            const identity_export_result exported =
                export_instance_identity(export_request, transaction_files_);
            record_operation("identity_export", instance.id, identity_code_name(exported.code),
                             exported.code == identity_export_code::exported);
            if (exported.code != identity_export_code::exported) {
                set_status("DELETE STOPPED · identity export failed: " + exported.detail, true);
                return;
            }
            disposition = identity_disposition::exported_then_destroy;
        }
    } else if (fl_choice("Delete this managed instance and its owned data directory?",
                         "Cancel", "Delete", 0) != 1) return;

    const game_state previous = game->state;
    instance_delete_request request;
    request.id = instance.id;
    request.confirmed = true;
    request.profile_key_exists = profile_exists;
    request.identity = disposition;
    const instance_operation removed = delete_instance_model(game->state, request);
    if ((removed.code != instance_operation_code::removed &&
         removed.code != instance_operation_code::removed_retained) || !save_game(*game)) {
        game->state = previous;
        set_status("DELETE REFUSED · managed state was not changed", true);
        return;
    }
    const instance_delete_commit_action next = instance_delete_after_commit(
        disposition == identity_disposition::retain_directory);
    selected_instance_ = 0;
    if (next == instance_delete_commit_action::start_cleanup_worker) {
        const std::string instance_directory = parent_path(instance.data_dir);
        owned_tree_remove_request tree;
        tree.parent_directory = parent_path(instance_directory);
        tree.directory = instance_directory;
        if (run_worker_.joinable()) run_worker_.join();
        {
            std::lock_guard<std::mutex> lock(run_worker_mutex_);
            run_worker_job_ = run_worker_job::remove_instance;
            run_worker_running_ = true;
            pending_run_id_ = instance.id;
            pending_remove_directory_ = instance_directory;
        }
        set_status("DELETING INSTANCE · complete bounded preflight before any removal");
        run_worker_ = std::thread([this, tree]() {
            const owned_tree_remove_result deleted = remove_owned_tree(tree, run_files_);
            {
                std::lock_guard<std::mutex> lock(run_worker_mutex_);
                pending_remove_result_ = deleted;
                run_worker_running_ = false;
            }
            Fl::awake(run_worker_awake, this);
        });
        refresh_instances();
        refresh_health();
    } else {
        record_operation("instance_delete", instance.id, "retained", true);
        set_status("REMOVED · identity directory retained by request");
        refresh_all();
    }
}

void manager_window::show_first_run_wizard(bool start_discovery) {
    if (wizard_window_ != 0) {
        wizard_window_->show();
        if (start_discovery) start_steam_discovery();
        return;
    }
    wizard_window_ = new Fl_Double_Window(760, 560, "Add a game safely");
    wizard_window_->callback(wizard_cancel_cb, this);
    wizard_window_->color(canvas);
    Fl_Box* title = new Fl_Box(24, 14, 712, 34, "First-run setup");
    title->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    title->labelfont(FL_HELVETICA_BOLD); title->labelsize(22); title->labelcolor(ink);
    wizard_ = new Fl_Wizard(20, 58, 720, 410);

    Fl_Group* choose = new Fl_Group(20, 58, 720, 410);
    Fl_Box* choose_note = new Fl_Box(38, 72, 680, 48,
        "Choose a discovered Steam game. Discovery reads public library metadata only; it never "
        "edits localconfig.vdf or Steam launch options.");
    choose_note->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE | FL_ALIGN_WRAP);
    wizard_games_ = new Fl_Hold_Browser(38, 130, 680, 246);
    wizard_games_->format_char(0);
    Fl_Button* rescan = new Fl_Button(38, 390, 146, 34, "Discover Steam");
    rescan->callback(discover_cb, this);
    Fl_Button* manual = new Fl_Button(194, 390, 164, 34, "Choose folder…");
    manual->callback(wizard_manual_cb, this);
    choose->end(); wizard_pages_.push_back(choose);

    Fl_Group* targets = new Fl_Group(20, 58, 720, 410);
    Fl_Box* target_note = new Fl_Box(38, 72, 680, 60,
        "Select the exact EOS library used by this game. Files are classified from PE/ELF bytes, "
        "so a Proton game correctly selects its Windows DLL. Nothing is changed on this page.");
    target_note->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE | FL_ALIGN_WRAP);
    wizard_targets_ = new Fl_Hold_Browser(38, 142, 680, 260);
    wizard_targets_->format_char(0);
    targets->end(); wizard_pages_.push_back(targets);

    Fl_Group* identity = new Fl_Group(20, 58, 720, 410);
    Fl_Box* identity_note = new Fl_Box(38, 76, 680, 74,
        "Create one isolated local identity. profile.key—not any name—is the persistent identity. "
        "This form seeds EOS Reimagined's network name and a cosmetic manager label; the game's "
        "own nickname may differ. Alpha instances default to lifecycle tracing.");
    identity_note->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE | FL_ALIGN_WRAP);
    Fl_Box* name_label = new Fl_Box(38, 174, 190, 30, "EOSR network name");
    name_label->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    wizard_name_ = new Fl_Input(238, 174, 420, 30); wizard_name_->value("Player");
    Fl_Box* slug_label = new Fl_Box(38, 222, 190, 30, "Local instance label");
    slug_label->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    wizard_slug_ = new Fl_Input(238, 222, 420, 30); wizard_slug_->value("player");
    Fl_Box* identity_hint = new Fl_Box(238, 258, 420, 52,
        "The short label becomes a stable directory name. Use lowercase letters, digits, and "
        "hyphens; it cannot be renamed later.");
    identity_hint->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE | FL_ALIGN_WRAP);
    identity_hint->labelcolor(muted);
    identity->end(); wizard_pages_.push_back(identity);

    Fl_Group* review = new Fl_Group(20, 58, 720, 410);
    wizard_review_ = new Fl_Text_Display(38, 74, 680, 330);
    wizard_review_->buffer(wizard_review_buffer_);
    wizard_review_->textfont(FL_COURIER); wizard_review_->textsize(12);
    review->end(); wizard_pages_.push_back(review);
    wizard_->end();

    wizard_status_ = new Fl_Box(24, 478, 430, 26, "SAFE STATE · no game file changes yet");
    wizard_status_->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    wizard_status_->labelfont(FL_COURIER_BOLD); wizard_status_->labelsize(11);
    wizard_status_->labelcolor(muted);
    wizard_back_ = new Fl_Button(464, 478, 82, 34, "Back");
    wizard_back_->callback(wizard_back_cb, this);
    Fl_Button* cancel = new Fl_Button(556, 478, 82, 34, "Cancel");
    cancel->callback(wizard_cancel_cb, this);
    wizard_next_ = new Fl_Button(648, 478, 82, 34, "Next");
    wizard_next_->callback(wizard_next_cb, this);
    wizard_next_->color(petrol); wizard_next_->labelcolor(FL_WHITE);
    wizard_window_->end();
    wizard_window_->set_modal();
    wizard_step_ = 0;
    wizard_manual_game_ = false;
    wizard_->value(wizard_pages_[0]);
    wizard_back_->deactivate();
    populate_wizard_games();
    wizard_window_->show();
    if (start_discovery || discovered_games_.empty()) start_steam_discovery();
}

void manager_window::close_wizard() {
    if (wizard_window_ == 0) return;
    wizard_window_->hide();
    if (wizard_review_ != 0) wizard_review_->buffer(0);
    delete wizard_window_;
    wizard_window_ = 0; wizard_ = 0; wizard_pages_.clear(); wizard_games_ = 0;
    wizard_targets_ = 0; wizard_name_ = 0; wizard_slug_ = 0; wizard_review_ = 0;
    wizard_status_ = 0; wizard_back_ = 0; wizard_next_ = 0; wizard_step_ = 0;
}

void manager_window::populate_wizard_games() {
    if (wizard_games_ == 0) return;
    wizard_games_->clear();
    for (std::size_t i = 0; i < discovered_games_.size(); i++) {
        std::string label = discovered_games_[i].name + "  ·  " + discovered_games_[i].app_id;
        if (discovered_games_[i].kind == steam_install_kind::flatpak_linux)
            label += "  ·  Flatpak Steam";
        label += "\n    " + discovered_games_[i].install_root;
        wizard_games_->add(browser_item_label(label).c_str());
    }
    if (!discovered_games_.empty()) wizard_games_->value(1);
}

void manager_window::populate_wizard_targets() {
    if (wizard_targets_ == 0) return;
    wizard_targets_->clear();
    const target_recommendation windows =
        recommend_eos_target(wizard_scan_.targets, eos_binary_kind::windows_x86_64);
    const target_recommendation linux =
        recommend_eos_target(wizard_scan_.targets, eos_binary_kind::linux_x86_64);
    std::vector<std::size_t> recommended;
    if (windows.code == target_recommendation_code::unique)
        recommended.push_back(windows.target_index);
    if (linux.code == target_recommendation_code::unique)
        recommended.push_back(linux.target_index);
    for (std::size_t i = 0; i < wizard_scan_.targets.size(); i++) {
        const eos_target& target = wizard_scan_.targets[i];
        const target_recommendation& recommendation =
            target.kind == eos_binary_kind::windows_x86_64 ? windows : linux;
        const bool is_recommended =
            recommendation.code == target_recommendation_code::unique &&
            recommendation.target_index == i;
        const std::string candidate_evidence = is_recommended ? recommendation.evidence :
            (recommendation.code == target_recommendation_code::unique ?
                "stronger_compatible_candidate_exists" : recommendation.evidence);
        const std::string label =
            (target.is_symlink ? "UNSAFE SYMLINK · " :
             (is_recommended ? "RECOMMENDED · " : "REQUIRES REVIEW · ")) +
            std::string(kind_name(target.kind)) + "\n    " + target.canonical_path +
            "\n    " + (target.is_symlink ? "Transactions refuse symlink targets" :
                target_evidence_name(candidate_evidence));
        wizard_targets_->add(browser_item_label(label).c_str());
    }
    wizard_targets_->value(recommended.size() == 1 ?
        static_cast<int>(recommended[0] + 1) : 0);
}

void manager_window::start_steam_discovery() {
    {
        std::lock_guard<std::mutex> lock(worker_mutex_);
        if (worker_running_ || worker_job_ != worker_job::none) {
            set_status("WAIT · a bounded discovery job is already running");
            return;
        }
    }
    if (worker_.joinable()) worker_.join();
    {
        std::lock_guard<std::mutex> lock(worker_mutex_);
        worker_job_ = worker_job::steam_discovery;
        worker_running_ = true;
    }
    set_status("DISCOVERING · native and Flatpak Steam libraries");
    if (wizard_status_ != 0) wizard_status_->copy_label("DISCOVERING · bounded VDF scan");
    worker_ = std::thread([this]() {
        const steam_discovery_result found = discover_steam_games(
            platform::platform_steam_root_candidates(), discovery_files_);
        {
            std::lock_guard<std::mutex> lock(worker_mutex_);
            pending_discovery_ = found;
            worker_running_ = false;
        }
        Fl::awake(worker_awake, this);
    });
}

void manager_window::start_target_scan(const std::string& root) {
    {
        std::lock_guard<std::mutex> lock(worker_mutex_);
        if (worker_running_ || worker_job_ != worker_job::none) {
            set_status("WAIT · discovery is still running");
            return;
        }
    }
    if (worker_.joinable()) worker_.join();
    {
        std::lock_guard<std::mutex> lock(worker_mutex_);
        worker_job_ = worker_job::target_scan;
        worker_running_ = true;
    }
    set_status("INSPECTING · bounded EOS target scan");
    if (wizard_status_ != 0) wizard_status_->copy_label("INSPECTING · PE/ELF classification");
    worker_ = std::thread([this, root]() {
        const target_scan_result found = scan_eos_targets(root, discovery_files_);
        {
            std::lock_guard<std::mutex> lock(worker_mutex_);
            pending_targets_ = found;
            worker_running_ = false;
        }
        Fl::awake(worker_awake, this);
    });
}

void manager_window::start_installation_inspection(const std::string& target_id,
                                                   const installation_probe& probe) {
    if (!installation_inspection_retry_.request(run_job_running())) return;
    {
        std::lock_guard<std::mutex> lock(worker_mutex_);
        if (worker_running_ || worker_job_ != worker_job::none) {
            installation_inspection_retry_.request(true);
            return;
        }
    }
    if (worker_.joinable()) worker_.join();
    {
        std::lock_guard<std::mutex> lock(worker_mutex_);
        worker_job_ = worker_job::installation_inspection;
        worker_running_ = true;
        pending_target_id_ = target_id;
    }
    set_status("INSPECTING · target ownership and hashes on a bounded worker");
    worker_ = std::thread([this, probe]() {
        const installation_health health = inspect_installation(probe, transaction_files_);
        {
            std::lock_guard<std::mutex> lock(worker_mutex_);
            pending_installation_health_ = health;
            worker_running_ = false;
        }
        Fl::awake(worker_awake, this);
    });
}

void manager_window::apply_worker_result() {
    worker_job completed = worker_job::none;
    steam_discovery_result discovery;
    target_scan_result targets;
    installation_health health;
    install_result operation_result;
    std::string target_id;
    std::string operation_action;
    bool launch_after = false;
    bool catalog_valid = false;
    std::string catalog_error;
    game_state switch_previous;
    managed_instance switch_candidate;
    update_request switch_rollback;
    bool switch_descriptor_changed = false;
    {
        std::lock_guard<std::mutex> lock(worker_mutex_);
        if (worker_running_) return;
        completed = worker_job_;
        discovery = pending_discovery_;
        targets = pending_targets_;
        health = pending_installation_health_;
        operation_result = pending_install_result_;
        target_id = pending_target_id_;
        operation_action = pending_operation_action_;
        launch_after = pending_launch_after_install_;
        catalog_valid = pending_catalog_valid_;
        catalog_error = pending_catalog_error_;
        switch_previous = pending_switch_previous_;
        switch_candidate = pending_switch_candidate_;
        switch_rollback = pending_switch_rollback_;
        switch_descriptor_changed = pending_switch_descriptor_changed_;
        pending_launch_after_install_ = false;
        worker_job_ = worker_job::none;
    }
    if (worker_.joinable()) worker_.join();
    if (completed == worker_job::release_verification) {
        catalog_valid_ = catalog_valid;
        catalog_error_ = catalog_error;
        if (catalog_valid_) {
            if (index_.selected_release_id.empty())
                index_.selected_release_id = catalog_.release_id;
            set_status("VERIFIED RELEASE · every packaged SDK hash and binary kind matches");
        } else {
            set_status("RELEASE REFUSED · " + catalog_error_, true);
        }
        refresh_all();
    } else if (completed == worker_job::steam_discovery) {
        discovered_games_ = discovery.games;
        discovered_steam_ = discovery.installations;
        for (std::size_t i = 0; i < discovery.diagnostics.size(); i++)
            health_diagnostics_.push_back(discovery.diagnostics[i].code + ": " +
                                          discovery.diagnostics[i].detail);
        populate_wizard_games();
        set_status("DISCOVERED · " + std::to_string(discovered_games_.size()) +
                   " Steam game installation(s)");
        if (wizard_status_ != 0)
            wizard_status_->copy_label(discovered_games_.empty() ?
                "NO GAMES FOUND · choose a folder manually" : "SAFE STATE · choose a game");
    } else if (completed == worker_job::target_scan) {
        wizard_scan_ = targets;
        populate_wizard_targets();
        set_status("INSPECTED · " + std::to_string(wizard_scan_.targets.size()) +
                   " EOS target(s)");
        if (wizard_status_ != 0) {
            if (wizard_scan_.targets.empty())
                wizard_status_->copy_label("NO EOS TARGET · choose another game folder");
            else if (wizard_targets_->value() > 0)
                wizard_status_->copy_label(
                    "RECOMMENDED TARGET SELECTED · verify the evidence and exact path");
            else
                wizard_status_->copy_label(
                    "AMBIGUOUS TARGETS · none selected automatically; inspect before choosing");
        }
    } else if (completed == worker_job::installation_inspection) {
        installation_health_cache_[target_id] = health;
        set_status("INSPECTED · target ownership and every required hash classified");
        refresh_game_detail();
    } else if (completed == worker_job::installation_transaction) {
        const bool installed = operation_result.code == install_result_code::installed ||
            (operation_result.code == install_result_code::cleanup_incomplete &&
             operation_result.target_is_reimagined);
        record_operation(operation_action, target_id, install_code_name(operation_result.code),
                         installed);
        if (installed) {
            installation_health verified;
            verified.state = installation_state::installed_current;
            installation_health_cache_[target_id] = verified;
        } else {
            installation_health_cache_[target_id] = health;
        }
        if (operation_result.code == install_result_code::installed)
            set_status("INSTALLED · verified artifact and active instance descriptor committed");
        else if (installed)
            set_status("INSTALLED WITH WARNING · " + operation_result.detail, true);
        else
            set_status("REFUSED / RECOVERABLE · " + operation_result.detail, true);
        if (launch_after && installed) launch_after_install();
        else if (launch_after) launch_submission_.reset();
        refresh_all();
    } else if (completed == worker_job::restore_transaction) {
        const bool restored = operation_result.code == install_result_code::restored;
        record_operation("restore", target_id, install_code_name(operation_result.code), restored);
        installation_health_cache_.erase(target_id);
        set_status(restored ? "RESTORED · original EOS library verified" :
            "RESTORE REFUSED · " + operation_result.detail +
            " — close the game or use Steam Verify Installed Files", !restored);
        refresh_all();
    } else if (completed == worker_job::instance_switch_transaction) {
        const bool prepared = operation_result.code == install_result_code::installed ||
            (operation_result.code == install_result_code::cleanup_incomplete &&
             operation_result.target_is_reimagined);
        stored_game* switch_game = 0;
        for (std::size_t i = 0; i < games_.size(); i++)
            if (games_[i].state.id == switch_previous.id) switch_game = &games_[i];
        if (!prepared || switch_game == 0) {
            record_operation("instance_select", switch_candidate.id, "switch_refused", false);
            installation_health_cache_[target_id] = health;
            set_status("INSTANCE SWITCH REFUSED · " +
                       (operation_result.detail.empty() ?
                            "installation health has no safe switch transition" :
                            operation_result.detail), true);
            refresh_all();
        } else {
            switch_game->state = switch_previous;
            for (std::size_t i = 0; i < switch_game->state.instances.size(); i++)
                if (switch_game->state.instances[i].id == switch_candidate.id)
                    switch_game->state.instances[i] = switch_candidate;
            const instance_operation selected =
                select_instance_model(switch_game->state, switch_candidate.id);
            if (selected.code == instance_operation_code::selected && save_game(*switch_game)) {
                record_operation("instance_select", switch_candidate.id, "selected", true);
                installation_health_cache_.erase(target_id);
                set_status("ACTIVE · bootstrap descriptor and instance state agree");
                refresh_all();
            } else {
                switch_game->state = switch_previous;
                record_operation("instance_select", switch_candidate.id,
                                 "state_commit_failed", false);
                if (!switch_descriptor_changed || switch_rollback.data_dir.empty()) {
                    if (switch_descriptor_changed)
                        health_diagnostics_.push_back(
                            "critical: instance state save failed and rollback data is unavailable");
                    set_status("INSTANCE SWITCH NOT SAVED · previous state retained", true);
                    refresh_all();
                } else {
                    {
                        std::lock_guard<std::mutex> lock(worker_mutex_);
                        worker_job_ = worker_job::instance_switch_rollback;
                        worker_running_ = true;
                        pending_target_id_ = target_id;
                    }
                    set_status("ROLLING BACK INSTANCE · restoring previous bootstrap descriptor");
                    worker_ = std::thread([this, switch_rollback]() {
                        const install_result rolled_back =
                            update_installation(switch_rollback, transaction_files_);
                        {
                            std::lock_guard<std::mutex> lock(worker_mutex_);
                            pending_install_result_ = rolled_back;
                            worker_running_ = false;
                        }
                        Fl::awake(worker_awake, this);
                    });
                }
            }
        }
    } else if (completed == worker_job::instance_switch_rollback) {
        const bool rolled_back = operation_result.code == install_result_code::installed ||
            (operation_result.code == install_result_code::cleanup_incomplete &&
             operation_result.target_is_reimagined);
        if (!rolled_back) {
            health_diagnostics_.push_back(
                "critical: instance state save and bootstrap rollback both failed");
            set_status("RECOVERY REQUIRED · bootstrap rollback failed: " +
                       operation_result.detail, true);
        } else {
            set_status("INSTANCE SWITCH NOT SAVED · previous bootstrap descriptor restored", true);
        }
        installation_health_cache_.erase(target_id);
        refresh_all();
    }
    if (installation_inspection_retry_.take()) refresh_game_detail();
    refresh_health();
}

void manager_window::worker_awake(void* self) {
    static_cast<manager_window*>(self)->apply_worker_result();
}

void manager_window::wizard_choose_manual() {
    Fl_Native_File_Chooser chooser;
    chooser.title("Choose the game installation folder");
    chooser.type(Fl_Native_File_Chooser::BROWSE_DIRECTORY);
    if (chooser.show() != 0 || chooser.filename() == 0) return;
    std::string canonical;
    if (!discovery_files_.canonical_directory(chooser.filename(), canonical)) {
        wizard_status_->copy_label("ERROR · folder is missing or unreadable");
        return;
    }
    wizard_manual_game_ = true;
    wizard_game_ = steam_game_installation();
    wizard_game_.name = base_name(canonical);
    wizard_game_.install_root = canonical;
#ifdef _WIN32
    wizard_game_.kind = steam_install_kind::native_windows;
#else
    wizard_game_.kind = steam_install_kind::native_linux;
#endif
    wizard_step_ = 1;
    wizard_->value(wizard_pages_[1]);
    wizard_back_->activate();
    start_target_scan(canonical);
}

void manager_window::wizard_back() {
    if (wizard_step_ <= 0) return;
    wizard_step_--;
    wizard_->value(wizard_pages_[static_cast<std::size_t>(wizard_step_)]);
    wizard_next_->copy_label("Next");
    if (wizard_step_ == 0) wizard_back_->deactivate(); else wizard_back_->activate();
    wizard_status_->copy_label("SAFE STATE · no game file changes yet");
}

void manager_window::wizard_next() {
    {
        std::lock_guard<std::mutex> lock(worker_mutex_);
        if (worker_running_ || worker_job_ != worker_job::none) {
            wizard_status_->copy_label("WAIT · current scan has not completed");
            return;
        }
    }
    if (wizard_step_ == 0) {
        const int selected = wizard_games_->value() - 1;
        if (selected < 0 || static_cast<std::size_t>(selected) >= discovered_games_.size()) {
            wizard_status_->copy_label("CHOOSE A GAME · or use Choose folder");
            return;
        }
        wizard_manual_game_ = false;
        wizard_game_ = discovered_games_[static_cast<std::size_t>(selected)];
        wizard_step_ = 1;
        wizard_->value(wizard_pages_[1]); wizard_back_->activate();
        start_target_scan(wizard_game_.install_root);
        return;
    }
    if (wizard_step_ == 1) {
        const int selected = wizard_targets_->value() - 1;
        if (selected < 0 || static_cast<std::size_t>(selected) >= wizard_scan_.targets.size()) {
            wizard_status_->copy_label("CHOOSE THE EOS TARGET · no file has changed");
            return;
        }
        if (wizard_scan_.targets[static_cast<std::size_t>(selected)].is_symlink) {
            wizard_status_->copy_label(
                "UNSAFE TARGET · symlinked libraries cannot be managed");
            return;
        }
        wizard_step_ = 2; wizard_->value(wizard_pages_[2]);
        wizard_status_->copy_label("IDENTITY · private and isolated");
        return;
    }
    if (wizard_step_ == 2) {
        const std::string name = wizard_name_->value();
        const std::string slug = wizard_slug_->value();
        if (name.empty() || name.size() > 128 || !valid_instance_slug(slug)) {
            wizard_status_->copy_label("INVALID IDENTITY · check display name and short label");
            return;
        }
        wizard_step_ = 3; wizard_review(); wizard_->value(wizard_pages_[3]);
        wizard_next_->copy_label("Install");
        return;
    }
    wizard_finish();
}

void manager_window::wizard_review() {
    const int target_index = wizard_targets_->value() - 1;
    if (target_index < 0 || static_cast<std::size_t>(target_index) >= wizard_scan_.targets.size())
        return;
    const eos_target& target = wizard_scan_.targets[static_cast<std::size_t>(target_index)];
    const target_recommendation recommendation =
        recommend_eos_target(wizard_scan_.targets, target.kind);
    const bool recommended = recommendation.code == target_recommendation_code::unique &&
                             recommendation.target_index ==
                                 static_cast<std::size_t>(target_index);
    const std::string candidate_evidence = recommended ? recommendation.evidence :
        (recommendation.code == target_recommendation_code::unique ?
            "stronger_compatible_candidate_exists" : recommendation.evidence);
    std::ostringstream text;
    text << "FINAL REVIEW — game files change only after Install\n\nGame\n  " <<
        wizard_game_.name << "\n  " << wizard_game_.install_root << "\n\nEOS target\n  " <<
        target.canonical_path << "\n  " << kind_name(target.kind) <<
        "\n  " << (recommended ? "Recommended: " : "Manual choice: ") <<
        target_evidence_name(candidate_evidence) <<
        "\n\nOriginal backup\n  " << target.canonical_path << ".eosr-original" <<
        "\n\nEOS Reimagined network name\n  " << wizard_name_->value() <<
        "\nManager label / diagnostic slug\n  " << wizard_name_->value() << " / " <<
        wizard_slug_->value() <<
        "\nGame / Steam nickname\n  separate and not synchronized"
        "\nIdentity\n  isolated profile.key (names do not change it)"
        "\n\nDiagnostics\n  lifecycle tracing "
        "enabled by default\n\nRelease\n  " << (catalog_valid_ ? catalog_.release_id : catalog_error_);
    wizard_review_buffer_->text(text.str().c_str());
    wizard_status_->copy_label(catalog_valid_ ? "READY · reviewed recoverable transaction" :
                                               "STOPPED · packaged artifact is not verified");
}

void manager_window::wizard_finish() {
    if (manager_job_running()) {
        wizard_status_->copy_label("WAIT · another bounded manager job is still running");
        return;
    }
    if (!catalog_valid_) {
        wizard_status_->copy_label("STOPPED · install requires a verified packaged artifact");
        return;
    }
    const int selected = wizard_targets_->value() - 1;
    if (selected < 0 || static_cast<std::size_t>(selected) >= wizard_scan_.targets.size()) return;
    for (std::size_t i = 0; i < games_.size(); i++) {
        if (games_[i].state.install_root == wizard_game_.install_root &&
            games_[i].state.steam_app_id == wizard_game_.app_id) {
            wizard_status_->copy_label("ALREADY MANAGED · choose it in Games");
            return;
        }
    }
    std::string random_error;
    game_state state;
    state.id = platform::manager_random_id(random_error);
    if (state.id.empty()) { wizard_status_->copy_label("ERROR · random id unavailable"); return; }
    state.steam_app_id = wizard_game_.app_id;
    state.display_name = wizard_game_.name.empty() ? base_name(wizard_game_.install_root) :
                                                    wizard_game_.name;
    state.install_root = wizard_game_.install_root;
    state.steam_root = wizard_game_.steam_root;
    state.library_root = wizard_game_.library_root;
    state.install_kind = wizard_game_.kind;
    state.manual = wizard_manual_game_;
    for (std::size_t i = 0; i < wizard_scan_.targets.size(); i++) {
        managed_target target;
        target.id = platform::manager_random_id(random_error);
        if (target.id.empty()) { wizard_status_->copy_label("ERROR · target id unavailable"); return; }
        target.path = wizard_scan_.targets[i].canonical_path;
        target.kind = wizard_scan_.targets[i].kind;
        state.targets.push_back(target);
        if (i == static_cast<std::size_t>(selected)) state.active_target_id = target.id;
    }
    const std::string root = game_root(state);
    const char* children[] = {"", "targets"};
    for (std::size_t i = 0; i < sizeof(children) / sizeof(children[0]); i++) {
        std::string error;
        const std::string path = children[i][0] == '\0' ? root : join_path(root, children[i]);
        if (!platform::manager_ensure_private_directory(path, error)) {
            wizard_status_->copy_label(("ERROR · " + error).c_str());
            return;
        }
    }
    std::string storage_error;
    const std::string instances_root = instance_root_for_game(state, storage_error);
    if (instances_root.empty()) {
        wizard_status_->copy_label(("ERROR · " + storage_error).c_str());
        return;
    }
    std::string directory_error;
    const std::string storage_manager_root =
        parent_path(parent_path(parent_path(instances_root)));
    if (storage_manager_root.empty() ||
        !platform::manager_ensure_private_directory(storage_manager_root, directory_error) ||
        !platform::manager_ensure_private_directory(instances_root, directory_error)) {
        wizard_status_->copy_label(("ERROR · " + directory_error).c_str());
        return;
    }
    instance_provision_request provision;
    provision.model.id = platform::manager_random_id(random_error);
    provision.model.slug = wizard_slug_->value();
    provision.model.display_name = wizard_name_->value();
    provision.model.instances_root = instances_root;
    provision.configuration = new_instance_configuration(provision.model.display_name,
                                                         provision.model.slug, true);
    if (provision.model.id.empty()) {
        wizard_status_->copy_label("ERROR · instance id unavailable"); return;
    }
    const instance_provision_result provisioned =
        provision_instance(state, provision, run_files_, transaction_files_);
    if (provisioned.code != instance_provision_code::provisioned) {
        wizard_status_->copy_label(("ERROR · " + provisioned.detail).c_str());
        return;
    }
    stored_game stored;
    stored.state = state;
    stored.state_path = join_path(root, "game.json");
    games_.push_back(stored);
    selected_game_ = games_.size() - 1;
    selected_instance_ = 0;
    managed_target* target = active_target(games_[selected_game_]);
    managed_instance* instance = active_instance(games_[selected_game_]);
    std::string mapping_error;
    const bool mapped = target != 0 && instance != 0 &&
        configure_runtime_path(games_[selected_game_].state, *target, *instance, mapping_error);
    if (!save_game(games_[selected_game_]) || !save_index()) {
        wizard_status_->copy_label("ERROR · manager state could not be committed; game untouched");
        return;
    }
    record_operation("game_add", games_[selected_game_].state.id, "created", true);
    close_wizard();
    const first_run_commit_action next = first_run_after_commit(mapped);
    if (next == first_run_commit_action::show_paused_game) {
        refresh_all();
        set_status("GAME ADDED, INSTALL PAUSED · " + mapping_error, true);
        return;
    }
    install_or_update();
}

void manager_window::add_manual_game() {
    show_first_run_wizard(false);
    wizard_choose_manual();
}

bool manager_window::begin_launch_watch(const managed_instance& instance) {
    if (run_job_running()) return false;
    if (run_worker_.joinable()) run_worker_.join();
    manager_configuration configuration;
    std::string bytes;
    std::string error;
    bool malformed = false;
    watched_trace_root_ = join_path(instance.data_dir, "traces");
    if (load_instance_configuration(instance, configuration, bytes, malformed, error) &&
        !malformed && !configuration.trace_dir.empty()) {
        watched_trace_root_ = manager_absolute_path(configuration.trace_dir) ?
            configuration.trace_dir : join_path(instance.data_dir, configuration.trace_dir);
    }
    const std::string trace_root = watched_trace_root_;
    {
        std::lock_guard<std::mutex> lock(run_worker_mutex_);
        run_worker_job_ = run_worker_job::prelaunch_snapshot;
        run_worker_running_ = true;
    }
    set_status("PREPARING LAUNCH · snapshotting existing diagnostic runs on a worker");
    run_worker_ = std::thread([this, trace_root]() {
        run_summary_cache cache;
        const run_index_result indexed =
            index_diagnostic_runs(trace_root, run_files_, cache);
        {
            std::lock_guard<std::mutex> lock(run_worker_mutex_);
            pending_runs_ = indexed.runs;
            pending_run_cache_ = cache;
            run_worker_running_ = false;
        }
        Fl::awake(run_worker_awake, this);
    });
    return true;
}

void manager_window::poll_launch_watch() {
    if (launch_watch_ticks_ <= 0 || watched_trace_root_.empty()) return;
    if (run_job_running()) {
        Fl::repeat_timeout(1.0, launch_watch_cb, this);
        return;
    }
    if (run_worker_.joinable()) run_worker_.join();
    const std::string trace_root = watched_trace_root_;
    {
        std::lock_guard<std::mutex> lock(run_worker_mutex_);
        run_worker_job_ = run_worker_job::launch_watch;
        run_worker_running_ = true;
    }
    run_worker_ = std::thread([this, trace_root]() {
        run_summary_cache cache;
        const run_index_result indexed =
            index_diagnostic_runs(trace_root, run_files_, cache);
        {
            std::lock_guard<std::mutex> lock(run_worker_mutex_);
            pending_runs_ = indexed.runs;
            pending_run_cache_ = cache;
            run_worker_running_ = false;
        }
        Fl::awake(run_worker_awake, this);
    });
}

void manager_window::launch_watch_cb(void* self) {
    static_cast<manager_window*>(self)->poll_launch_watch();
}

void manager_window::launch_game() {
    stored_game* game = selected_game();
    managed_target* target = game == 0 ? 0 : active_target(*game);
    managed_instance* instance = game == 0 ? 0 : active_instance(*game);
    if (game == 0 || target == 0 || instance == 0 || game->state.manual ||
        game->state.steam_app_id.empty()) {
        set_status("LAUNCH UNAVAILABLE · standard launch requires a discovered Steam game", true);
        return;
    }
    if (launch_watch_ticks_ > 0 ||
        launch_submission_.phase() != launch_submission_phase::idle) {
        set_status("LAUNCH ALREADY PENDING · wait for run detection or its bounded timeout", true);
        return;
    }
    if (run_job_running()) {
        set_status("LAUNCH WAIT · diagnostic indexing must finish before the prelaunch snapshot");
        return;
    }
    bool in_use = false;
    if (transaction_files_.in_use(target->path, in_use) != transaction_io_result::ok || in_use) {
        set_status("REFUSED · the game is already using this EOS target; overlapping descriptor "
                   "switches are forbidden", true);
        return;
    }
    manager_configuration configuration;
    std::string config_bytes;
    std::string config_error;
    bool malformed = false;
    if (!load_instance_configuration(*instance, configuration, config_bytes, malformed,
                                     config_error) || malformed ||
        sha256_hex(config_bytes) != instance->config_sha256) {
        set_status("LAUNCH REFUSED · eosr.json is missing, malformed, or changed — open Configure",
                   true);
        return;
    }
    if (!launch_submission_.begin()) {
        set_status("LAUNCH ALREADY PENDING · duplicate launch callback refused", true);
        return;
    }
    if (!install_or_update(true)) launch_submission_.reset();
}

void manager_window::launch_after_install() {
    stored_game* game = selected_game();
    managed_target* target = game == 0 ? 0 : active_target(*game);
    managed_instance* instance = game == 0 ? 0 : active_instance(*game);
    if (game == 0 || target == 0 || instance == 0 ||
        game->state.id != pending_launch_game_id_ ||
        target->id != pending_launch_target_id_ ||
        instance->id != pending_launch_instance_id_) {
        set_status("LAUNCH PAUSED · selection changed while the verified transaction completed",
                   true);
        launch_submission_.reset();
        return;
    }
    bool in_use = false;
    if (transaction_files_.in_use(target->path, in_use) != transaction_io_result::ok || in_use) {
        set_status("LAUNCH REFUSED · game began using the target before launch dispatch", true);
        launch_submission_.reset();
        return;
    }
    if (!launch_submission_.begin_prelaunch_snapshot()) {
        set_status("LAUNCH PAUSED · launch lifecycle state changed before diagnostics", true);
        launch_submission_.reset();
        return;
    }
    if (!begin_launch_watch(*instance)) {
        set_status("LAUNCH PAUSED · diagnostic indexing is still running; try again", true);
        launch_submission_.reset();
    }
}

void manager_window::dispatch_steam_launch() {
    if (launch_submission_.phase() != launch_submission_phase::prelaunch_snapshot) {
        set_status("DUPLICATE LAUNCH CALLBACK REFUSED · Steam was not submitted again", true);
        return;
    }
    stored_game* game = selected_game();
    managed_target* target = game == 0 ? 0 : active_target(*game);
    managed_instance* instance = game == 0 ? 0 : active_instance(*game);
    if (game == 0 || target == 0 || instance == 0 ||
        game->state.id != pending_launch_game_id_ ||
        target->id != pending_launch_target_id_ ||
        instance->id != pending_launch_instance_id_) {
        watched_trace_root_.clear();
        launch_submission_.reset();
        set_status("LAUNCH PAUSED · selection changed during the prelaunch run snapshot", true);
        return;
    }
    bool in_use = false;
    if (transaction_files_.in_use(target->path, in_use) != transaction_io_result::ok || in_use) {
        watched_trace_root_.clear();
        launch_submission_.reset();
        set_status("LAUNCH REFUSED · game began using the target during prelaunch checks", true);
        return;
    }

    steam_launch_request launch;
    std::string error;
#ifdef _WIN32
    steam_launch_adapter adapter = game->state.steam_root.empty() ?
        steam_launch_adapter::windows_uri : steam_launch_adapter::windows_client;
    const std::string executable = game->state.steam_root.empty() ? std::string() :
        join_path(game->state.steam_root, "steam.exe");
#else
    steam_launch_adapter adapter = game->state.install_kind == steam_install_kind::flatpak_linux ?
        steam_launch_adapter::flatpak_linux : steam_launch_adapter::linux_client;
    const std::string executable = adapter == steam_launch_adapter::linux_client ?
        join_path(game->state.steam_root, "steam.sh") : std::string();
#endif
    if (!build_steam_launch_request(adapter, executable, game->state.steam_app_id, launch, error)) {
        launch_submission_.reset();
        set_status("LAUNCH REFUSED · " + error, true);
        return;
    }
    if (!launch_submission_.try_submit()) {
        set_status("DUPLICATE LAUNCH CALLBACK REFUSED · Steam was not submitted again", true);
        return;
    }
    platform::manager_action_result launched = platform::manager_launch_steam(launch);
#ifndef _WIN32
    if (!launched.ok && adapter == steam_launch_adapter::linux_client &&
        build_steam_launch_request(steam_launch_adapter::desktop_uri, std::string(),
                                   game->state.steam_app_id, launch, error))
        launched = platform::manager_launch_steam(launch);
#else
    if (!launched.ok && adapter == steam_launch_adapter::windows_client &&
        build_steam_launch_request(steam_launch_adapter::windows_uri, std::string(),
                                   game->state.steam_app_id, launch, error))
        launched = platform::manager_launch_steam(launch);
#endif
    if (!launched.ok) {
        Fl::remove_timeout(launch_watch_cb, this);
        launch_watch_ticks_ = 0; watched_trace_root_.clear();
        launch_submission_.reset();
        set_status("LAUNCH FAILED · " + launched.detail + " — start the game from Steam", true);
        return;
    }
    record_operation("launch", game->state.id, launched.code.empty() ? "accepted" : launched.code,
                     true);

    std::string random_error;
    const std::string attempt_id = platform::manager_random_id(random_error);
    if (!attempt_id.empty()) {
        json_value attempt = json_object();
        attempt.members["schema_version"] = json_int(1);
        attempt.members["created_utc"] = json_string(platform::manager_utc_now());
        attempt.members["game_id"] = json_string(game->state.id);
        attempt.members["steam_app_id"] = json_string(game->state.steam_app_id);
        attempt.members["instance_id"] = json_string(instance->id);
        attempt.members["target_id"] = json_string(target->id);
        attempt.members["release_id"] = json_string(catalog_.release_id);
        const std::string path = join_path(join_path(root_, "logs"),
                                           "launch-attempt-" + attempt_id + ".json");
        const std::string bytes = serialize_json(attempt);
        transaction_handle handle = 0;
        bool recorded = transaction_files_.create_new(path, handle) == transaction_io_result::ok;
        std::size_t offset = 0;
        while (recorded && offset < bytes.size()) {
            std::size_t written = 0;
            recorded = transaction_files_.write(
                handle, reinterpret_cast<const unsigned char*>(bytes.data()) + offset,
                bytes.size() - offset, written) == transaction_io_result::ok && written != 0;
            offset += recorded ? written : 0;
        }
        if (recorded) recorded = transaction_files_.flush(handle) == transaction_io_result::ok;
        if (handle != 0 && transaction_files_.close(handle) != transaction_io_result::ok)
            recorded = false;
        if (recorded) recorded = transaction_files_.set_mode(path, 0600) ==
                                 transaction_io_result::ok;
        if (recorded) recorded = transaction_files_.flush_parent(path) ==
                                 transaction_io_result::ok;
        if (!recorded) health_diagnostics_.push_back("launch succeeded but attempt log failed");
    }
    if (!launch_submission_.begin_watch()) {
        launch_submission_.reset();
        set_status("LAUNCHED THROUGH STEAM · internal run-watch guard failed", true);
        return;
    }
    launch_watch_ticks_ = 1;
    Fl::remove_timeout(launch_watch_cb, this);
    Fl::add_timeout(1.0, launch_watch_cb, this);
    set_status("LAUNCHED THROUGH STEAM · waiting up to 45 seconds for runtime.json proof");
}

void manager_window::build_support_bundle() {
    if (manager_job_running()) {
        set_status("WAIT · support bundle creation is locked during another manager job");
        return;
    }
    if (selected_run_ >= runs_.size()) return;
    const run_summary run = runs_[selected_run_];
    std::string random_error;
    const std::string id = platform::manager_random_id(random_error);
    if (id.empty()) {
        set_status("BUNDLE FAILED · random destination unavailable", true); return;
    }
    support_bundle_request request;
    request.run_directory = run.directory;
    request.bundle_directory = run.directory + "-support-" + id.substr(0, 8);
    request.generated_utc = platform::manager_utc_now();
    if (run_worker_.joinable()) run_worker_.join();
    {
        std::lock_guard<std::mutex> lock(run_worker_mutex_);
        run_worker_job_ = run_worker_job::support_bundle;
        run_worker_running_ = true;
        pending_run_id_ = run.run_id;
    }
    set_status("BUILDING SUPPORT BUNDLE · bounded sanitize/hash/write worker");
    run_worker_ = std::thread([this, request]() {
        const support_bundle_result bundle = create_support_bundle(request, run_files_);
        {
            std::lock_guard<std::mutex> lock(run_worker_mutex_);
            pending_bundle_result_ = bundle;
            run_worker_running_ = false;
        }
        Fl::awake(run_worker_awake, this);
    });
}

void manager_window::delete_run() {
    if (manager_job_running()) {
        set_status("WAIT · run deletion is locked during another manager job");
        return;
    }
    if (selected_run_ >= runs_.size()) return;
    const run_summary run = runs_[selected_run_];
    if (run.status == diagnostic_run_status::active) {
        set_status("DELETE REFUSED · diagnostic run is active", true); return;
    }
    if (fl_choice("Delete this diagnostic run directory? profile.key is never expected here, and "
                  "any symlink or unknown entry type will stop deletion.",
                  "Cancel", "Delete", 0) != 1) return;
    owned_tree_remove_request request;
    request.parent_directory = parent_path(run.directory);
    request.directory = run.directory;
    if (run_worker_.joinable()) run_worker_.join();
    {
        std::lock_guard<std::mutex> lock(run_worker_mutex_);
        run_worker_job_ = run_worker_job::remove_run;
        run_worker_running_ = true;
        pending_run_id_ = run.run_id;
    }
    set_status("DELETING RUN · complete bounded preflight before any removal");
    run_worker_ = std::thread([this, request]() {
        const owned_tree_remove_result removed = remove_owned_tree(request, run_files_);
        {
            std::lock_guard<std::mutex> lock(run_worker_mutex_);
            pending_remove_result_ = removed;
            run_worker_running_ = false;
        }
        Fl::awake(run_worker_awake, this);
    });
}

void manager_window::open_game_folder() {
    stored_game* game = selected_game();
    if (game == 0) return;
    const platform::manager_action_result opened =
        platform::manager_open_location(game->state.install_root, false);
    set_status(opened.ok ? "OPEN REQUEST ACCEPTED · game folder" :
                           "OPEN FAILED · " + opened.detail, !opened.ok);
}

void manager_window::open_instance_folder() {
    stored_game* game = selected_game();
    if (game == 0 || selected_instance_ >= game->state.instances.size()) return;
    const platform::manager_action_result opened = platform::manager_open_location(
        game->state.instances[selected_instance_].data_dir, false);
    set_status(opened.ok ? "OPEN REQUEST ACCEPTED · instance data" :
                           "OPEN FAILED · " + opened.detail, !opened.ok);
}

void manager_window::open_run_folder() {
    if (selected_run_ >= runs_.size()) return;
    const platform::manager_action_result opened =
        platform::manager_open_location(runs_[selected_run_].directory, false);
    set_status(opened.ok ? "OPEN REQUEST ACCEPTED · diagnostic run" :
                           "OPEN FAILED · " + opened.detail, !opened.ok);
}

void manager_window::copy_health_report() {
    const char* text = health_detail_buffer_->text();
    if (text == 0) return;
    Fl::copy(text, static_cast<int>(std::strlen(text)), 1);
    set_status("COPIED · system report is on the clipboard");
}

void manager_window::games_select_cb(Fl_Widget*, void* self) {
    manager_window* window = static_cast<manager_window*>(self);
    const int selected = window->games_browser_->value() - 1;
    if (selected >= 0 && static_cast<std::size_t>(selected) < window->games_.size()) {
        window->selected_game_ = static_cast<std::size_t>(selected);
        window->selected_instance_ = 0;
    }
    window->refresh_game_detail(); window->refresh_instances();
}

void manager_window::instances_select_cb(Fl_Widget*, void* self) {
    manager_window* window = static_cast<manager_window*>(self);
    const int selected = window->instances_browser_->value() - 1;
    if (selected >= 0) window->selected_instance_ = static_cast<std::size_t>(selected);
    window->refresh_instances();
}

void manager_window::runs_select_cb(Fl_Widget*, void* self) {
    manager_window* window = static_cast<manager_window*>(self);
    const int selected = window->runs_browser_->value() - 1;
    if (selected >= 0) window->selected_run_ = static_cast<std::size_t>(selected);
    window->refresh_run_detail();
}

void manager_window::discover_cb(Fl_Widget*, void* self) {
    manager_window* window = static_cast<manager_window*>(self);
    if (window->wizard_window_ == 0) window->show_first_run_wizard(true);
    else window->start_steam_discovery();
}

void manager_window::add_manual_cb(Fl_Widget*, void* self) {
    static_cast<manager_window*>(self)->add_manual_game();
}
void manager_window::install_cb(Fl_Widget*, void* self) {
    static_cast<manager_window*>(self)->install_or_update();
}
void manager_window::restore_cb(Fl_Widget*, void* self) {
    static_cast<manager_window*>(self)->restore_target();
}
void manager_window::launch_cb(Fl_Widget*, void* self) {
    static_cast<manager_window*>(self)->launch_game();
}
void manager_window::configure_cb(Fl_Widget*, void* self) {
    static_cast<manager_window*>(self)->configure_instance();
}
void manager_window::open_game_cb(Fl_Widget*, void* self) {
    static_cast<manager_window*>(self)->open_game_folder();
}
void manager_window::create_instance_cb(Fl_Widget*, void* self) {
    static_cast<manager_window*>(self)->create_instance();
}
void manager_window::select_instance_cb(Fl_Widget*, void* self) {
    static_cast<manager_window*>(self)->select_instance();
}
void manager_window::rename_instance_cb(Fl_Widget*, void* self) {
    static_cast<manager_window*>(self)->rename_instance();
}
void manager_window::export_identity_cb(Fl_Widget*, void* self) {
    static_cast<manager_window*>(self)->export_identity();
}
void manager_window::delete_instance_cb(Fl_Widget*, void* self) {
    static_cast<manager_window*>(self)->delete_instance();
}
void manager_window::open_instance_cb(Fl_Widget*, void* self) {
    static_cast<manager_window*>(self)->open_instance_folder();
}
void manager_window::refresh_runs_cb(Fl_Widget*, void* self) {
    static_cast<manager_window*>(self)->refresh_runs();
}
void manager_window::bundle_cb(Fl_Widget*, void* self) {
    static_cast<manager_window*>(self)->build_support_bundle();
}
void manager_window::delete_run_cb(Fl_Widget*, void* self) {
    static_cast<manager_window*>(self)->delete_run();
}
void manager_window::open_run_cb(Fl_Widget*, void* self) {
    static_cast<manager_window*>(self)->open_run_folder();
}
void manager_window::health_refresh_cb(Fl_Widget*, void* self) {
    manager_window* window = static_cast<manager_window*>(self);
    if (window->manager_job_running()) {
        window->set_status("WAIT · the current bounded manager job must finish before recheck");
        return;
    }
    window->load_release_catalog();
    window->refresh_all();
    if (!window->manager_job_running())
        window->set_status("RECHECKED · persisted state, packaged artifacts, and diagnostic runs");
}
void manager_window::health_copy_cb(Fl_Widget*, void* self) {
    static_cast<manager_window*>(self)->copy_health_report();
}
void manager_window::health_open_cb(Fl_Widget*, void* self) {
    manager_window* window = static_cast<manager_window*>(self);
    const platform::manager_action_result opened =
        platform::manager_open_location(window->root_, false);
    window->set_status(opened.ok ? "OPEN REQUEST ACCEPTED · manager data" :
                                  "OPEN FAILED · " + opened.detail, !opened.ok);
}
void manager_window::wizard_back_cb(Fl_Widget*, void* self) {
    static_cast<manager_window*>(self)->wizard_back();
}
void manager_window::wizard_next_cb(Fl_Widget*, void* self) {
    static_cast<manager_window*>(self)->wizard_next();
}
void manager_window::wizard_cancel_cb(Fl_Widget*, void* self) {
    static_cast<manager_window*>(self)->close_wizard();
}
void manager_window::wizard_manual_cb(Fl_Widget*, void* self) {
    static_cast<manager_window*>(self)->wizard_choose_manual();
}

} // namespace ui
} // namespace manager
} // namespace eosr
