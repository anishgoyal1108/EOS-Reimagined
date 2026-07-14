#ifndef EOSR_INTERFACES_SESSIONS_H
#define EOSR_INTERFACES_SESSIONS_H

#include <chrono>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "eos_common.h"
#include "eos_sessions_types.h"

#include "common/byte_buffer.h"
#include "common/handle_store.h"
#include "common/types.h"
#include "core/frame_result.h"
#include "core/i_run_callback.h"
#include "core/i_run_network.h"
#include "net/messages.h"

namespace eosr {

class sdk_settings;
class callback_manager;
class message_router;
class sdk_connect;

// The Sessions interface: how a game is advertised and found. A host describes a session and
// leaves it sitting there; a searcher asks every peer what it has, and the hosts answer out of
// their own session list. Nothing is broadcast — discovery is a question, not an announcement.
// Spec: EOSSDK_Sessions (wiki/developers/internals/sessions.qmd), session search (wiki/developers/internals/protocol.qmd)
class sdk_sessions : public i_run_callback, public i_run_network {
public:
    sdk_sessions(sdk_settings& settings, callback_manager& callbacks, message_router& network,
                 sdk_connect& connect);
    ~sdk_sessions();

    sdk_sessions(const sdk_sessions&) = delete;
    sdk_sessions& operator=(const sdk_sessions&) = delete;

    void emu_init();
    void emu_deinit();

    // --- Sessions ---
    EOS_EResult create_session_modification(const EOS_Sessions_CreateSessionModificationOptions* options,
                                            EOS_HSessionModification* out);
    EOS_EResult update_session_modification(const EOS_Sessions_UpdateSessionModificationOptions* options,
                                            EOS_HSessionModification* out);
    void update_session(const EOS_Sessions_UpdateSessionOptions* options, void* client_data,
                        EOS_Sessions_OnUpdateSessionCallback delegate);
    void destroy_session(const EOS_Sessions_DestroySessionOptions* options, void* client_data,
                         EOS_Sessions_OnDestroySessionCallback delegate);
    void join_session(const EOS_Sessions_JoinSessionOptions* options, void* client_data,
                      EOS_Sessions_OnJoinSessionCallback delegate);
    void start_session(const EOS_Sessions_StartSessionOptions* options, void* client_data,
                       EOS_Sessions_OnStartSessionCallback delegate);
    void end_session(const EOS_Sessions_EndSessionOptions* options, void* client_data,
                     EOS_Sessions_OnEndSessionCallback delegate);
    void register_players(const EOS_Sessions_RegisterPlayersOptions* options, void* client_data,
                          EOS_Sessions_OnRegisterPlayersCallback delegate);
    void unregister_players(const EOS_Sessions_UnregisterPlayersOptions* options, void* client_data,
                            EOS_Sessions_OnUnregisterPlayersCallback delegate);
    EOS_EResult create_session_search(const EOS_Sessions_CreateSessionSearchOptions* options,
                                      EOS_HSessionSearch* out);
    EOS_EResult copy_active_session_handle(const EOS_Sessions_CopyActiveSessionHandleOptions* options,
                                           EOS_HActiveSession* out);
    EOS_EResult is_user_in_session(const EOS_Sessions_IsUserInSessionOptions* options) const;
    EOS_EResult dump_session_state(const EOS_Sessions_DumpSessionStateOptions* options) const;

    // --- SessionModification (the handle is the first argument) ---
    EOS_EResult modification_set_bucket_id(void* handle,
                                           const EOS_SessionModification_SetBucketIdOptions* options);
    EOS_EResult modification_set_host_address(void* handle,
                                              const EOS_SessionModification_SetHostAddressOptions* options);
    EOS_EResult modification_set_permission_level(
        void* handle, const EOS_SessionModification_SetPermissionLevelOptions* options);
    EOS_EResult modification_set_join_in_progress(
        void* handle, const EOS_SessionModification_SetJoinInProgressAllowedOptions* options);
    EOS_EResult modification_set_max_players(void* handle,
                                             const EOS_SessionModification_SetMaxPlayersOptions* options);
    EOS_EResult modification_set_invites_allowed(
        void* handle, const EOS_SessionModification_SetInvitesAllowedOptions* options);
    EOS_EResult modification_set_allowed_platform_ids(
        void* handle, const EOS_SessionModification_SetAllowedPlatformIdsOptions* options);
    EOS_EResult modification_add_attribute(void* handle,
                                           const EOS_SessionModification_AddAttributeOptions* options);
    EOS_EResult modification_remove_attribute(
        void* handle, const EOS_SessionModification_RemoveAttributeOptions* options);
    void modification_release(void* handle);

    // --- SessionSearch ---
    EOS_EResult search_set_session_id(void* handle, const EOS_SessionSearch_SetSessionIdOptions* options);
    EOS_EResult search_set_target_user(void* handle,
                                       const EOS_SessionSearch_SetTargetUserIdOptions* options);
    EOS_EResult search_set_parameter(void* handle, const EOS_SessionSearch_SetParameterOptions* options);
    EOS_EResult search_remove_parameter(void* handle,
                                        const EOS_SessionSearch_RemoveParameterOptions* options);
    EOS_EResult search_set_max_results(void* handle,
                                       const EOS_SessionSearch_SetMaxResultsOptions* options);
    void search_find(void* handle, const EOS_SessionSearch_FindOptions* options, void* client_data,
                     EOS_SessionSearch_OnFindCallback delegate);
    u32 search_result_count(void* handle) const;
    EOS_EResult search_copy_result(void* handle,
                                   const EOS_SessionSearch_CopySearchResultByIndexOptions* options,
                                   EOS_HSessionDetails* out);
    void search_release(void* handle);

    // --- SessionDetails ---
    EOS_EResult details_copy_info(void* handle, EOS_SessionDetails_Info** out);
    u32 details_attribute_count(void* handle) const;
    EOS_EResult details_copy_attribute_by_index(
        void* handle, const EOS_SessionDetails_CopySessionAttributeByIndexOptions* options,
        EOS_SessionDetails_Attribute** out);
    EOS_EResult details_copy_attribute_by_key(
        void* handle, const EOS_SessionDetails_CopySessionAttributeByKeyOptions* options,
        EOS_SessionDetails_Attribute** out);
    void details_release(void* handle);

    // --- ActiveSession ---
    EOS_EResult active_copy_info(void* handle, EOS_ActiveSession_Info** out);
    u32 active_registered_count(void* handle) const;
    EOS_ProductUserId active_registered_by_index(
        void* handle, const EOS_ActiveSession_GetRegisteredPlayerByIndexOptions* options) const;
    void active_release(void* handle);

    // Notifications the invite path needs. They register and never fire until invites land.
    EOS_NotificationId add_stub_notification(void* client_data, completion_delegate delegate,
                                             std::size_t info_size, const char* event);
    void remove_notification(EOS_NotificationId id);
    void queue_stub_result(void* client_data, completion_delegate delegate, std::size_t info_size);

    // --- i_run_callback ---
    bool cb_run_frame();
    bool run_callbacks(frame_result& result);
    void free_callback(frame_result& result);

    // --- i_run_network ---
    bool on_network_message(const net_envelope& message);

private:
    // A session we host or joined. There is one map for both: which it is shows in `local_state`.
    struct session {
        enum local_state_kind { hosting, joining, joined };
        session_infos infos;
        std::string local_user;
        local_state_kind local_state;
    };

    struct modification_object {
        std::string session_name;
        std::string local_user;
        bool creating;
        session_infos infos;
    };

    struct search_object {
        session_search query;
        std::vector<session_infos> results;
        u32 max_results;
        bool searching;
        // The peers we asked and have not heard back from. Every peer answers, even with nothing,
        // so a search finishes as soon as this empties instead of waiting out its deadline.
        std::set<std::string> awaiting;
        std::chrono::steady_clock::time_point deadline;
    };

    struct details_object {
        session_infos infos;
    };

    // Unlike a search result, an active session is a live view: a game copies the handle once and
    // then polls it to draw its player list, so it has to see people arrive and leave. We keep only
    // the name and resolve the session on every call.
    struct active_object {
        std::string session_name;
    };

    session* find_by_name(const std::string& name);
    const session* find_by_name(const std::string& name) const;
    session* find_by_id(const std::string& session_id);
    const session* find_by_id(const std::string& session_id) const;

    void send_to(const std::string& peer, message_type type, const byte_writer& payload);
    void send_to_members(const session& entry, message_type type, const byte_writer& payload,
                         const std::string& except);
    void broadcast_session(const session& entry);

    bool session_matches(const session_infos& infos, const session_search& query) const;
    EOS_EResult emit_attribute(const session_attribute& from, EOS_SessionDetails_Attribute** out);
    void deliver(callback_type_id type, std::size_t info_size, completion_delegate delegate,
                 void* client_data, EOS_EResult code);

    sdk_settings& settings_;
    callback_manager& callbacks_;
    message_router& network_;
    sdk_connect& connect_;

    std::map<std::string, session> sessions_;

    handle_store<modification_object> modifications_;
    handle_store<search_object> searches_;
    handle_store<details_object> details_;
    handle_store<active_object> actives_;

    // A Find that is waiting on peers, and a JoinSession waiting on the host's verdict. Both are
    // keyed by the result the callback manager owns, so the tick can tell when they are ready.
    std::map<frame_result*, void*> pending_finds_;
    struct pending_join {
        std::string session_id;
        std::chrono::steady_clock::time_point deadline;
    };
    std::map<frame_result*, pending_join> pending_joins_;

    u64 next_search_id_;
    bool registered_;
};

// The three structs CopyInfo and CopyAttribute hand out. They carry no handle, so the game frees
// them through these, and the objects behind them live outside any one platform.
//
// Contract, matching the EOS headers: release each exactly once. A null pointer, or one we never
// issued, is ignored rather than freed. Using or re-releasing one after it has been freed is
// undefined, exactly as the SDK specifies — we do not pretend otherwise.
void release_session_details_info(EOS_SessionDetails_Info* info);
void release_active_session_info(EOS_ActiveSession_Info* info);
void release_session_details_attribute(EOS_SessionDetails_Attribute* attribute);

} // namespace eosr

#endif
