#ifndef EOSR_INTERFACES_INTEGRATEDPLATFORM_H
#define EOSR_INTERFACES_INTEGRATEDPLATFORM_H

#include <string>
#include <vector>

#include "eos_common.h"
#include "eos_integratedplatform_types.h"

#include "common/types.h"
#include "core/frame_result.h"
#include "core/i_run_callback.h"

namespace eosr {

class callback_manager;

// One integrated platform a game asked for. `type` is the SDK's own string (EOS_IPT_Steam is
// "STEAM"), and the flags say how the game wants it managed.
struct integrated_platform_entry {
    std::string type;
    EOS_EIntegratedPlatformManagementFlags flags;
};

// The options container.
//
// A game creates this *before* the platform exists, adds one entry per integrated platform, hands it
// to EOS_Platform_Create, and then releases it -- so the container cannot live on the platform, and
// its store is process-global for exactly that reason. Platform creation copies what it needs, which
// is what makes releasing it afterwards safe.
class integrated_platform_container {
public:
    EOS_EResult add(const EOS_IntegratedPlatformOptionsContainer_AddOptions* options);
    const std::vector<integrated_platform_entry>& entries() const { return entries_; }

private:
    std::vector<integrated_platform_entry> entries_;
};

// The container store. A handle is only ever one we minted and have not yet released, so a stale,
// double, or foreign release is a no-op rather than a free of someone else's memory.
EOS_EResult create_integrated_platform_container(
    const EOS_IntegratedPlatform_CreateIntegratedPlatformOptionsContainerOptions* options,
    EOS_HIntegratedPlatformOptionsContainer* out_handle);
integrated_platform_container* find_integrated_platform_container(
    EOS_HIntegratedPlatformOptionsContainer handle);
void release_integrated_platform_container(EOS_HIntegratedPlatformOptionsContainer handle);

// The IntegratedPlatform interface: the bridge to Steam, a console's native account system, and the
// like.
//
// There is no bridge here. We do not load Steam, mirror presence into it, or hear a word from a
// platform's account system -- so anything that would have to come *from* an integrated platform
// never happens, and we say so rather than pretending: the pre-logout handler never fires, because
// no system ever tells us a user signed out, and a deferred logout can never be waiting to finalize.
//
// But the half of this interface that runs the other way is genuinely local, and we implement it
// properly. SetUserLoginStatus is the *application* telling the SDK what an integrated-platform
// user's login status now is; that needs no Steam, only somewhere to keep it and a notification to
// fire when it changes. So a game that manages its own platform identity gets real behaviour, and a
// game that expects the SDK to talk to Steam gets an honest NotConfigured.
// Spec: EOSSDK_IntegratedPlatform (docs/integratedplatform-ui-overlay.md)
class sdk_integrated_platform : public i_run_callback {
public:
    explicit sdk_integrated_platform(callback_manager& callbacks);
    ~sdk_integrated_platform();

    sdk_integrated_platform(const sdk_integrated_platform&) = delete;
    sdk_integrated_platform& operator=(const sdk_integrated_platform&) = delete;

    void emu_init();
    void emu_deinit();

    // What the game registered in the container it passed to EOS_Platform_Create. Nothing here is
    // loaded or hooked; the list is what tells the difference between "you never asked for Steam"
    // (NotConfigured) and "you asked, and this is how you said you would manage it".
    void configure(const std::vector<integrated_platform_entry>& entries);

    // --- Flat API surface ---

    EOS_EResult set_user_login_status(const EOS_IntegratedPlatform_SetUserLoginStatusOptions* options);
    EOS_EResult finalize_deferred_user_logout(
        const EOS_IntegratedPlatform_FinalizeDeferredUserLogoutOptions* options);

    EOS_NotificationId add_notify_user_login_status_changed(
        const EOS_IntegratedPlatform_AddNotifyUserLoginStatusChangedOptions* options,
        void* client_data, EOS_IntegratedPlatform_OnUserLoginStatusChangedCallback delegate);
    void remove_notify_user_login_status_changed(EOS_NotificationId id);

    EOS_EResult set_user_pre_logout_callback(
        const EOS_IntegratedPlatform_SetUserPreLogoutCallbackOptions* options, void* client_data,
        EOS_IntegratedPlatform_OnUserPreLogoutCallback delegate);
    void clear_user_pre_logout_callback(
        const EOS_IntegratedPlatform_ClearUserPreLogoutCallbackOptions* options);

    // i_run_callback
    bool cb_run_frame();
    bool run_callbacks(frame_result& result);
    void free_callback(frame_result& result);

private:
    // A login-status change we owe the game, delivered on the tick like every other callback.
    struct status_change {
        std::string type;
        std::string platform_user;
        EOS_ELoginStatus previous;
        EOS_ELoginStatus current;
    };

    const integrated_platform_entry* find_entry(const std::string& type) const;

    callback_manager& callbacks_;
    std::vector<integrated_platform_entry> entries_;
    // The login status the game last told us each (platform, user) is in.
    std::vector<status_change> statuses_;
    std::vector<status_change> pending_changes_;
    bool pre_logout_bound_;
    bool registered_;
};

} // namespace eosr

#endif
