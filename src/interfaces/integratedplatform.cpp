#include "interfaces/integratedplatform.h"

#include <memory>
#include <mutex>
#include <vector>

#include "common/handle_store.h"
#include "common/ids.h"
#include "core/callback_manager.h"

namespace eosr {

namespace {

const callback_type_id cb_login_status_changed = 1;

// The containers a game currently holds. Global because a container is created before any platform
// exists -- it is what a platform is created *from*. Guarded because Create/Add/Release are bare C
// entry points a game may call from any thread, like the other free-function registries. Handles are
// process-unique tokens (handle_store), so a released one never aliases a live container.
std::mutex g_container_mutex;
handle_store<integrated_platform_container> g_containers;

bool version_ok(i32 version, i32 latest) {
    return version > 0 && version <= latest;
}

std::string text_or_empty(const char* value) {
    return (value != 0) ? value : std::string();
}

bool login_status_is_valid(EOS_ELoginStatus status) {
    return status == EOS_ELoginStatus::EOS_LS_NotLoggedIn ||
           status == EOS_ELoginStatus::EOS_LS_UsingLocalProfile ||
           status == EOS_ELoginStatus::EOS_LS_LoggedIn;
}

// The flag that says the *application* owns this platform's identity, and so may tell us a user's
// login status. Without it, SetUserLoginStatus is not a call the game is allowed to make.
bool application_manages_identity(EOS_EIntegratedPlatformManagementFlags flags) {
    const i32 value = static_cast<i32>(flags);
    const i32 managed = static_cast<i32>(
        EOS_EIntegratedPlatformManagementFlags::EOS_IPMF_ApplicationManagedIdentityLogin);
    return (value & managed) != 0;
}

} // namespace

EOS_EResult integrated_platform_container::add(
    const EOS_IntegratedPlatformOptionsContainer_AddOptions* options) {
    if (options == 0 ||
        !version_ok(options->ApiVersion, EOS_INTEGRATEDPLATFORMOPTIONSCONTAINER_ADD_API_LATEST)) {
        return EOS_EResult::EOS_InvalidParameters;
    }
    const EOS_IntegratedPlatform_Options* entry = options->Options;
    if (entry == 0 ||
        !version_ok(entry->ApiVersion, EOS_INTEGRATEDPLATFORM_OPTIONS_API_LATEST)) {
        return EOS_EResult::EOS_InvalidParameters;
    }
    const std::string type = text_or_empty(entry->Type);
    if (type.empty()) {
        return EOS_EResult::EOS_InvalidParameters;
    }
    // One entry per platform. A second one for the same platform is not a refinement of the first,
    // it is two answers to one question, and the SDK has a word for that.
    for (std::size_t i = 0; i < entries_.size(); i++) {
        if (entries_[i].type == type) {
            return EOS_EResult::EOS_DuplicateNotAllowed;
        }
    }
    integrated_platform_entry added;
    added.type = type;
    added.flags = entry->Flags;
    entries_.push_back(added);
    return EOS_EResult::EOS_Success;
}

EOS_EResult create_integrated_platform_container(
    const EOS_IntegratedPlatform_CreateIntegratedPlatformOptionsContainerOptions* options,
    EOS_HIntegratedPlatformOptionsContainer* out_handle) {
    if (out_handle == 0) {
        return EOS_EResult::EOS_InvalidParameters;
    }
    *out_handle = 0;
    if (options == 0 ||
        !version_ok(options->ApiVersion,
                    EOS_INTEGRATEDPLATFORM_CREATEINTEGRATEDPLATFORMOPTIONSCONTAINER_API_LATEST)) {
        return EOS_EResult::EOS_InvalidParameters;
    }
    std::unique_ptr<integrated_platform_container> container(new integrated_platform_container());
    std::lock_guard<std::mutex> lock(g_container_mutex);
    *out_handle = reinterpret_cast<EOS_HIntegratedPlatformOptionsContainer>(
        g_containers.add(std::move(container)));
    return EOS_EResult::EOS_Success;
}

EOS_EResult add_container_entry(
    EOS_HIntegratedPlatformOptionsContainer handle,
    const EOS_IntegratedPlatformOptionsContainer_AddOptions* options) {
    std::lock_guard<std::mutex> lock(g_container_mutex);
    integrated_platform_container* container = g_containers.find(handle);
    return (container != 0) ? container->add(options) : EOS_EResult::EOS_InvalidParameters;
}

bool copy_container_entries(EOS_HIntegratedPlatformOptionsContainer handle,
                            std::vector<integrated_platform_entry>& out) {
    std::lock_guard<std::mutex> lock(g_container_mutex);
    integrated_platform_container* container = g_containers.find(handle);
    if (container == 0) {
        return false;
    }
    out = container->entries();
    return true;
}

bool find_integrated_platform_container(EOS_HIntegratedPlatformOptionsContainer handle) {
    std::lock_guard<std::mutex> lock(g_container_mutex);
    return g_containers.find(handle) != 0;
}

// A handle we did not mint, or already released, is not ours to free. Because handles are unique
// tokens, a released one is found by nothing, so a stale or double release changes nothing.
void release_integrated_platform_container(EOS_HIntegratedPlatformOptionsContainer handle) {
    std::lock_guard<std::mutex> lock(g_container_mutex);
    g_containers.release(handle);
}

sdk_integrated_platform::sdk_integrated_platform(callback_manager& callbacks)
    : callbacks_(callbacks), pre_logout_bound_(false), registered_(false) {
}

sdk_integrated_platform::~sdk_integrated_platform() {
    emu_deinit();
}

void sdk_integrated_platform::emu_init() {
    if (registered_) {
        return;
    }
    callbacks_.register_frame(this);
    callbacks_.register_callbacks(this);
    registered_ = true;
}

void sdk_integrated_platform::emu_deinit() {
    if (!registered_) {
        return;
    }
    callbacks_.unregister_callbacks(this);
    callbacks_.unregister_frame(this);
    entries_.clear();
    statuses_.clear();
    pending_changes_.clear();
    pre_logout_bound_ = false;
    registered_ = false;
}

void sdk_integrated_platform::configure(const std::vector<integrated_platform_entry>& entries) {
    entries_ = entries;
}

const integrated_platform_entry* sdk_integrated_platform::find_entry(
    const std::string& type) const {
    for (std::size_t i = 0; i < entries_.size(); i++) {
        if (entries_[i].type == type) {
            return &entries_[i];
        }
    }
    return 0;
}

// This one runs the way we can actually honour: the game tells us what an integrated-platform user's
// login status now is, and we remember it and tell whoever registered. Nothing about that needs
// Steam -- it needs somewhere to keep the answer.
EOS_EResult sdk_integrated_platform::set_user_login_status(
    const EOS_IntegratedPlatform_SetUserLoginStatusOptions* options) {
    if (options == 0 ||
        !version_ok(options->ApiVersion, EOS_INTEGRATEDPLATFORM_SETUSERLOGINSTATUS_API_LATEST)) {
        return EOS_EResult::EOS_InvalidParameters;
    }
    const std::string type = text_or_empty(options->PlatformType);
    if (type.empty() || !login_status_is_valid(options->CurrentLoginStatus)) {
        return EOS_EResult::EOS_InvalidParameters;
    }
    // The game never asked us for this platform at creation, so there is nothing here to set.
    const integrated_platform_entry* entry = find_entry(type);
    if (entry == 0) {
        return EOS_EResult::EOS_NotConfigured;
    }
    // It did ask, but not for an identity it manages itself -- so this is not its status to set.
    if (!application_manages_identity(entry->flags)) {
        return EOS_EResult::EOS_InvalidState;
    }
    const std::string user = text_or_empty(options->LocalPlatformUserId);
    if (user.empty()) {
        return EOS_EResult::EOS_InvalidUser;
    }

    for (std::size_t i = 0; i < statuses_.size(); i++) {
        if (statuses_[i].type != type || statuses_[i].platform_user != user) {
            continue;
        }
        // The header is explicit: an unchanged status does nothing, succeeds, and does not fire the
        // notification.
        if (statuses_[i].current == options->CurrentLoginStatus) {
            return EOS_EResult::EOS_Success;
        }
        status_change change;
        change.type = type;
        change.platform_user = user;
        change.previous = statuses_[i].current;
        change.current = options->CurrentLoginStatus;
        statuses_[i].current = options->CurrentLoginStatus;
        pending_changes_.push_back(change);
        return EOS_EResult::EOS_Success;
    }

    // A user we have not heard of before was not logged in as far as we knew.
    status_change fresh;
    fresh.type = type;
    fresh.platform_user = user;
    fresh.previous = EOS_ELoginStatus::EOS_LS_NotLoggedIn;
    fresh.current = options->CurrentLoginStatus;
    statuses_.push_back(fresh);
    if (fresh.current != fresh.previous) {
        pending_changes_.push_back(fresh);
    }
    return EOS_EResult::EOS_Success;
}

// A deferred logout is one we started because an integrated platform told us a user signed out. No
// platform ever tells us anything, so there is never one waiting, and the header already has the
// word for a finalize with nothing to finalize.
EOS_EResult sdk_integrated_platform::finalize_deferred_user_logout(
    const EOS_IntegratedPlatform_FinalizeDeferredUserLogoutOptions* options) {
    if (options == 0 ||
        !version_ok(options->ApiVersion,
                    EOS_INTEGRATEDPLATFORM_FINALIZEDEFERREDUSERLOGOUT_API_LATEST)) {
        return EOS_EResult::EOS_InvalidParameters;
    }
    const std::string type = text_or_empty(options->PlatformType);
    if (type.empty()) {
        return EOS_EResult::EOS_InvalidParameters;
    }
    if (find_entry(type) == 0) {
        return EOS_EResult::EOS_NotConfigured;
    }
    return EOS_EResult::EOS_InvalidUser;
}

EOS_NotificationId sdk_integrated_platform::add_notify_user_login_status_changed(
    const EOS_IntegratedPlatform_AddNotifyUserLoginStatusChangedOptions* options, void* client_data,
    EOS_IntegratedPlatform_OnUserLoginStatusChangedCallback delegate) {
    if (delegate == 0 || options == 0 ||
        !version_ok(options->ApiVersion,
                    EOS_INTEGRATEDPLATFORM_ADDNOTIFYUSERLOGINSTATUSCHANGED_API_LATEST)) {
        return EOS_INVALID_NOTIFICATIONID;
    }
    std::unique_ptr<frame_result> result(new frame_result());
    EOS_IntegratedPlatform_UserLoginStatusChangedCallbackInfo* info =
        static_cast<EOS_IntegratedPlatform_UserLoginStatusChangedCallbackInfo*>(
            result->create_callback(
                cb_login_status_changed,
                sizeof(EOS_IntegratedPlatform_UserLoginStatusChangedCallbackInfo),
                reinterpret_cast<completion_delegate>(delegate)));
    info->ClientData = client_data;
    info->PlatformType = 0;
    info->LocalPlatformUserId = 0;
    info->AccountId = 0;
    info->ProductUserId = 0;
    info->PreviousLoginStatus = EOS_ELoginStatus::EOS_LS_NotLoggedIn;
    info->CurrentLoginStatus = EOS_ELoginStatus::EOS_LS_NotLoggedIn;
    // Unlike the overlay's notifications, this one carries no promise of an initial call, so we do
    // not invent one.
    return callbacks_.add_notification(this, std::move(result),
                                       "IntegratedPlatformUserLoginStatusChanged");
}

void sdk_integrated_platform::remove_notify_user_login_status_changed(EOS_NotificationId id) {
    callbacks_.remove_notification(this, id);
}

// The handler exists to let a game veto a logout an integrated platform has told us about. Nothing
// ever tells us about one, so it binds, it unbinds, and it never fires -- but the binding itself is
// real, and a second one is refused, because the header says there can only ever be one.
EOS_EResult sdk_integrated_platform::set_user_pre_logout_callback(
    const EOS_IntegratedPlatform_SetUserPreLogoutCallbackOptions* options, void* client_data,
    EOS_IntegratedPlatform_OnUserPreLogoutCallback delegate) {
    (void)client_data;
    if (delegate == 0 || options == 0 ||
        !version_ok(options->ApiVersion,
                    EOS_INTEGRATEDPLATFORM_SETUSERPRELOGOUTCALLBACK_API_LATEST)) {
        return EOS_EResult::EOS_InvalidParameters;
    }
    if (pre_logout_bound_) {
        return EOS_EResult::EOS_AlreadyConfigured;
    }
    pre_logout_bound_ = true;
    return EOS_EResult::EOS_Success;
}

void sdk_integrated_platform::clear_user_pre_logout_callback(
    const EOS_IntegratedPlatform_ClearUserPreLogoutCallbackOptions* options) {
    if (options == 0 ||
        !version_ok(options->ApiVersion,
                    EOS_INTEGRATEDPLATFORM_CLEARUSERPRELOGOUTCALLBACK_API_LATEST)) {
        return;
    }
    pre_logout_bound_ = false;
}

bool sdk_integrated_platform::cb_run_frame() {
    if (pending_changes_.empty()) {
        return false;
    }
    std::vector<status_change> changes;
    changes.swap(pending_changes_);
    for (std::size_t i = 0; i < changes.size(); i++) {
        const status_change& change = changes[i];
        // Re-look-up each notification by id before firing: a fired callback may remove another, or
        // remove itself. We never touch the payload after fire() -- self-removal frees it, so a write
        // to take the strings back would be a use-after-free. The strings live in `changes` for the
        // whole frame, and the next change overwrites them before the next fire, so nothing ever
        // reads them between frames; leaving them set is harmless where taking them back is not.
        const std::vector<EOS_NotificationId> ids =
            callbacks_.notification_ids(this, cb_login_status_changed);
        for (std::size_t n = 0; n < ids.size(); n++) {
            frame_result* note = callbacks_.find_notification(this, ids[n]);
            if (note == 0) {
                continue;
            }
            EOS_IntegratedPlatform_UserLoginStatusChangedCallbackInfo* info =
                note->get_callback<EOS_IntegratedPlatform_UserLoginStatusChangedCallbackInfo>();
            info->PlatformType = change.type.c_str();
            info->LocalPlatformUserId = change.platform_user.c_str();
            info->AccountId = 0;
            info->ProductUserId = 0;
            info->PreviousLoginStatus = change.previous;
            info->CurrentLoginStatus = change.current;
            note->fire();
        }
    }
    return false;
}

bool sdk_integrated_platform::run_callbacks(frame_result&) {
    return false;
}

void sdk_integrated_platform::free_callback(frame_result&) {
}

} // namespace eosr
