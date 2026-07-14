#ifndef EOSR_INTERFACES_CONNECT_H
#define EOSR_INTERFACES_CONNECT_H

#include <map>
#include <string>
#include <vector>

#include "eos_common.h"
#include "eos_connect_types.h"

#include <cstddef>

#include "common/types.h"
#include "core/frame_result.h"
#include "core/i_run_callback.h"
#include "core/i_run_network.h"

namespace eosr {

class sdk_settings;
class callback_manager;
class message_router;
class frame_result;
struct net_envelope;

// The Connect interface: the roster backbone of the SDK. It owns the local logged-in user and
// the identity everything else keys on, the ProductUserId. Login derives a stable local
// ProductUserId from the configured user and completes asynchronously; the peer roster is built
// from Connect messages arriving over the network. Every other networked interface depends on
// the connect/disconnect fan-out this maintains.
// Spec: EOSSDK_Connect (wiki/developers/internals/connect.qmd), the roster backbone (wiki/developers/internals/architecture.qmd)
class sdk_connect : public i_run_callback, public i_run_network {
public:
    sdk_connect(sdk_settings& settings, callback_manager& callbacks, message_router& network);
    ~sdk_connect();

    sdk_connect(const sdk_connect&) = delete;
    sdk_connect& operator=(const sdk_connect&) = delete;

    // Register with the callback and network layers and start with an empty roster. Paired with
    // emu_deinit, so a platform create/release cycle re-registers cleanly.
    void emu_init();
    void emu_deinit();

    // --- Flat API surface (called by the trampolines in flat/eos_connect_flat.cpp) ---

    void login(const EOS_Connect_LoginOptions* options, void* client_data,
               EOS_Connect_OnLoginCallback delegate);
    void logout(const EOS_Connect_LogoutOptions* options, void* client_data,
                EOS_Connect_OnLogoutCallback delegate);

    i32 logged_in_users_count() const;
    EOS_ProductUserId logged_in_user_by_index(i32 index) const;
    EOS_ELoginStatus login_status(EOS_ProductUserId local_user_id) const;

    void query_product_user_id_mappings(const EOS_Connect_QueryProductUserIdMappingsOptions* options,
                                        void* client_data,
                                        EOS_Connect_OnQueryProductUserIdMappingsCallback delegate);
    EOS_EResult get_product_user_id_mapping(const EOS_Connect_GetProductUserIdMappingOptions* options,
                                            char* out_buffer, i32* in_out_buffer_length) const;

    // The number of remote peers currently in the roster. Peers are learned from inbound Connect
    // messages; this lets callers (and tests) observe the roster the network path builds.
    std::size_t known_peer_count() const;

    // Whether this player is one we have actually met on the mesh. A host uses this to refuse a
    // join from someone it has never seen.
    bool is_known_peer(const std::string& product_user_id) const;

    // The display name a peer announced over the authenticated roster, or empty if unknown. UserInfo
    // resolves a peer's name through this rather than trusting a name in a payload: the roster is
    // keyed on the connection-proven product user id, so the name is the one everyone else sees too.
    std::string peer_display_name(const std::string& product_user_id) const;

    EOS_NotificationId add_notify_login_status_changed(
        void* client_data, EOS_Connect_OnLoginStatusChangedCallback delegate);
    void remove_notify_login_status_changed(EOS_NotificationId id);

    // Emu tokens never expire, so this registers a notification that simply never fires; it
    // exists so a game gets a valid id and can pair a Remove call with it.
    EOS_NotificationId add_notify_auth_expiration(
        void* client_data, EOS_Connect_OnAuthExpirationCallback delegate);
    void remove_notify_auth_expiration(EOS_NotificationId id);

    // Queue a stubbed async completion reporting EOS_NotImplemented, for methods not yet built.
    // `info_size` is the size of the specific EOS_*CallbackInfo the delegate expects; they all
    // begin with { EOS_EResult ResultCode; void* ClientData; }, which is all we fill.
    void queue_stub_result(void* client_data, completion_delegate delegate, std::size_t info_size);

    // --- i_run_callback ---
    bool cb_run_frame();
    bool run_callbacks(frame_result& result);
    void free_callback(frame_result& result);

    // --- i_run_network ---
    bool on_network_message(const net_envelope& message);

private:
    // A queued login-status transition, fired to the registered notifications on the next frame
    // so delivery happens during Tick rather than inside the API call.
    struct status_transition {
        EOS_ProductUserId user;
        EOS_ELoginStatus previous;
        EOS_ELoginStatus current;
    };

    bool is_logged_in() const { return !local_users_.empty(); }
    // Tell one peer who we are, so its roster can name us.
    void announce_to(const std::string& peer_id);
    EOS_ProductUserId local_user() const;
    void deliver_login_result(EOS_EResult result_code, EOS_ProductUserId user, void* client_data,
                              EOS_Connect_OnLoginCallback delegate);

    sdk_settings& settings_;
    callback_manager& callbacks_;
    message_router& network_;

    // Local logged-in users, self at index 0. Empty means not logged in. Distinct from the peer
    // roster: EOS_Connect_GetLoggedInUsersCount counts local users only.
    std::vector<EOS_ProductUserId> local_users_;
    // Known remote peers: ProductUserId string -> display name, built from inbound Connect
    // messages. This is the roster used to resolve id mappings for peers.
    std::map<std::string, std::string> peers_;
    std::vector<status_transition> pending_status_changes_;
    bool registered_;
};

} // namespace eosr

#endif
