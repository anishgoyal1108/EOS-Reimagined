#ifndef EOSR_INTERFACES_LOBBY_H
#define EOSR_INTERFACES_LOBBY_H

#include <chrono>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "eos_common.h"
#include "eos_lobby_types.h"

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

// The Lobby interface: persistent, owner-run groups a player joins and stays in, with a member list
// and per-member attributes. Structurally the Sessions interface plus members and an owner who has
// authority over the roster -- authority that now rests on the connection-bound source id, so only
// the real host can promote, kick, or admit. RTC voice rooms are named but not carried.
// Spec: EOSSDK_Lobby (docs/lobby.md), lobby protocol (docs/protocol.md)
class sdk_lobby : public i_run_callback, public i_run_network {
public:
    sdk_lobby(sdk_settings& settings, callback_manager& callbacks, message_router& network,
              sdk_connect& connect);
    ~sdk_lobby();

    sdk_lobby(const sdk_lobby&) = delete;
    sdk_lobby& operator=(const sdk_lobby&) = delete;

    void emu_init();
    void emu_deinit();

    // --- Lobby ---
    void create_lobby(const EOS_Lobby_CreateLobbyOptions* options, void* client_data,
                      EOS_Lobby_OnCreateLobbyCallback delegate);
    void destroy_lobby(const EOS_Lobby_DestroyLobbyOptions* options, void* client_data,
                       EOS_Lobby_OnDestroyLobbyCallback delegate);
    void join_lobby(const EOS_Lobby_JoinLobbyOptions* options, void* client_data,
                    EOS_Lobby_OnJoinLobbyCallback delegate);
    void join_lobby_by_id(const EOS_Lobby_JoinLobbyByIdOptions* options, void* client_data,
                          EOS_Lobby_OnJoinLobbyByIdCallback delegate);
    void leave_lobby(const EOS_Lobby_LeaveLobbyOptions* options, void* client_data,
                     EOS_Lobby_OnLeaveLobbyCallback delegate);
    EOS_EResult update_lobby_modification(const EOS_Lobby_UpdateLobbyModificationOptions* options,
                                          EOS_HLobbyModification* out);
    void update_lobby(const EOS_Lobby_UpdateLobbyOptions* options, void* client_data,
                      EOS_Lobby_OnUpdateLobbyCallback delegate);
    void promote_member(const EOS_Lobby_PromoteMemberOptions* options, void* client_data,
                        EOS_Lobby_OnPromoteMemberCallback delegate);
    void kick_member(const EOS_Lobby_KickMemberOptions* options, void* client_data,
                     EOS_Lobby_OnKickMemberCallback delegate);
    EOS_EResult create_lobby_search(const EOS_Lobby_CreateLobbySearchOptions* options,
                                    EOS_HLobbySearch* out);
    EOS_EResult copy_lobby_details_handle(const EOS_Lobby_CopyLobbyDetailsHandleOptions* options,
                                          EOS_HLobbyDetails* out);
    EOS_EResult get_rtc_room_name(const EOS_Lobby_GetRTCRoomNameOptions* options, char* out_buffer,
                                  u32* inout_buffer_length) const;
    EOS_Bool is_rtc_room_connected(const EOS_Lobby_IsRTCRoomConnectedOptions* options) const;

    EOS_NotificationId add_notify_lobby_update_received(
        void* client_data, EOS_Lobby_OnLobbyUpdateReceivedCallback delegate);
    EOS_NotificationId add_notify_lobby_member_update_received(
        void* client_data, EOS_Lobby_OnLobbyMemberUpdateReceivedCallback delegate);
    EOS_NotificationId add_notify_lobby_member_status_received(
        void* client_data, EOS_Lobby_OnLobbyMemberStatusReceivedCallback delegate);
    void remove_notify(EOS_NotificationId id);

    // Register a notification that never fires (invites, RTC, overlay -- deferred), so a game that
    // subscribes still gets a valid id and does not break.
    EOS_NotificationId add_stub_notification(void* client_data, completion_delegate delegate,
                                             std::size_t info_size);
    // Report a stubbed async completion (invites, RTC) as EOS_NotImplemented.
    void queue_stub_result(void* client_data, completion_delegate delegate, std::size_t info_size);

    // --- LobbyModification sub-handle ---
    EOS_EResult modification_set_bucket_id(void* handle,
                                           const EOS_LobbyModification_SetBucketIdOptions* options);
    EOS_EResult modification_set_permission_level(
        void* handle, const EOS_LobbyModification_SetPermissionLevelOptions* options);
    EOS_EResult modification_set_max_members(
        void* handle, const EOS_LobbyModification_SetMaxMembersOptions* options);
    EOS_EResult modification_set_invites_allowed(
        void* handle, const EOS_LobbyModification_SetInvitesAllowedOptions* options);
    EOS_EResult modification_set_allowed_platform_ids(
        void* handle, const EOS_LobbyModification_SetAllowedPlatformIdsOptions* options);
    EOS_EResult modification_add_attribute(void* handle,
                                           const EOS_LobbyModification_AddAttributeOptions* options);
    EOS_EResult modification_remove_attribute(
        void* handle, const EOS_LobbyModification_RemoveAttributeOptions* options);
    EOS_EResult modification_add_member_attribute(
        void* handle, const EOS_LobbyModification_AddMemberAttributeOptions* options);
    EOS_EResult modification_remove_member_attribute(
        void* handle, const EOS_LobbyModification_RemoveMemberAttributeOptions* options);
    void modification_release(void* handle);

    // --- LobbySearch sub-handle ---
    EOS_EResult search_set_lobby_id(void* handle, const EOS_LobbySearch_SetLobbyIdOptions* options);
    EOS_EResult search_set_target_user(void* handle,
                                       const EOS_LobbySearch_SetTargetUserIdOptions* options);
    EOS_EResult search_set_parameter(void* handle,
                                     const EOS_LobbySearch_SetParameterOptions* options);
    EOS_EResult search_remove_parameter(void* handle,
                                        const EOS_LobbySearch_RemoveParameterOptions* options);
    EOS_EResult search_set_max_results(void* handle,
                                       const EOS_LobbySearch_SetMaxResultsOptions* options);
    void search_find(void* handle, const EOS_LobbySearch_FindOptions* options, void* client_data,
                     EOS_LobbySearch_OnFindCallback delegate);
    u32 search_result_count(void* handle) const;
    EOS_EResult search_copy_result(void* handle,
                                   const EOS_LobbySearch_CopySearchResultByIndexOptions* options,
                                   EOS_HLobbyDetails* out);
    void search_release(void* handle);

    // --- LobbyDetails sub-handle ---
    EOS_EResult details_copy_info(void* handle, EOS_LobbyDetails_Info** out);
    EOS_ProductUserId details_get_lobby_owner(void* handle,
                                              const EOS_LobbyDetails_GetLobbyOwnerOptions* options);
    u32 details_attribute_count(void* handle) const;
    EOS_EResult details_copy_attribute_by_index(
        void* handle, const EOS_LobbyDetails_CopyAttributeByIndexOptions* options,
        EOS_Lobby_Attribute** out);
    EOS_EResult details_copy_attribute_by_key(
        void* handle, const EOS_LobbyDetails_CopyAttributeByKeyOptions* options,
        EOS_Lobby_Attribute** out);
    u32 details_member_count(void* handle, const EOS_LobbyDetails_GetMemberCountOptions* options) const;
    EOS_ProductUserId details_member_by_index(
        void* handle, const EOS_LobbyDetails_GetMemberByIndexOptions* options) const;
    EOS_EResult details_copy_member_info(void* handle,
                                         const EOS_LobbyDetails_CopyMemberInfoOptions* options,
                                         EOS_LobbyDetails_MemberInfo** out);
    u32 details_member_attribute_count(
        void* handle, const EOS_LobbyDetails_GetMemberAttributeCountOptions* options) const;
    EOS_EResult details_copy_member_attribute_by_index(
        void* handle, const EOS_LobbyDetails_CopyMemberAttributeByIndexOptions* options,
        EOS_Lobby_Attribute** out);
    EOS_EResult details_copy_member_attribute_by_key(
        void* handle, const EOS_LobbyDetails_CopyMemberAttributeByKeyOptions* options,
        EOS_Lobby_Attribute** out);
    void details_release(void* handle);

    // --- i_run_callback ---
    bool cb_run_frame();
    bool run_callbacks(frame_result& result);
    void free_callback(frame_result& result);

    // --- i_run_network ---
    bool on_network_message(const net_envelope& message);

private:
    // A lobby we host or joined. One map for both; which it is shows in `local_state`.
    struct lobby {
        enum local_state_kind { hosting, joining, joined };
        lobby_infos infos;
        std::string local_user;
        local_state_kind local_state;
    };

    struct modification_object {
        std::string lobby_id;
        std::string local_user;
        bool creating;
        lobby_infos infos;
        // Member-attribute edits staged for the local user, applied on UpdateLobby.
        std::vector<session_attribute> member_set;
        std::vector<std::string> member_deleted;
    };

    struct search_object {
        lobby_search query;
        std::vector<lobby_infos> results;
        u32 max_results;
        bool searching;
        std::set<std::string> awaiting;
        std::chrono::steady_clock::time_point deadline;
    };

    struct details_object {
        lobby_infos infos;
    };

    lobby* find_lobby(const std::string& lobby_id);
    const lobby* find_lobby(const std::string& lobby_id) const;
    lobby_member* find_member(lobby_infos& infos, const std::string& user_id);

    void send_to(const std::string& peer, message_type type, const byte_writer& payload);
    void broadcast_to_members(const lobby& entry, message_type type, const byte_writer& payload,
                              const std::string& except);
    void broadcast_lobby(const lobby& entry);
    // Leave a lobby we host: hand it to a surviving member if migration is on, else tell the members
    // it is closing. Does not erase our own copy; the caller does.
    void close_hosted_lobby(lobby& entry);
    bool lobby_matches(const lobby_infos& infos, const lobby_search& query) const;
    u32 open_slots_of(const lobby_infos& infos) const;
    EOS_EResult emit_attribute(const session_attribute& from, EOS_Lobby_Attribute** out);
    void deliver_id(callback_type_id type, std::size_t info_size, completion_delegate delegate,
                    void* client_data, EOS_EResult code, const std::string& lobby_id);
    void deliver_result(callback_type_id type, std::size_t info_size, completion_delegate delegate,
                        void* client_data, EOS_EResult code);

    sdk_settings& settings_;
    callback_manager& callbacks_;
    message_router& network_;
    sdk_connect& connect_;

    std::map<std::string, lobby> lobbies_;

    handle_store<modification_object> modifications_;
    handle_store<search_object> searches_;
    handle_store<details_object> details_;

    std::map<frame_result*, void*> pending_finds_;
    struct pending_join {
        std::string lobby_id;
        std::chrono::steady_clock::time_point deadline;
    };
    std::map<frame_result*, pending_join> pending_joins_;

    u64 next_search_id_;
    bool registered_;
};

// The structs CopyInfo / CopyMemberInfo / CopyAttribute hand the game, freed through these.
void release_lobby_details_info(EOS_LobbyDetails_Info* info);
void release_lobby_details_member_info(EOS_LobbyDetails_MemberInfo* info);
void release_lobby_attribute(EOS_Lobby_Attribute* attribute);

} // namespace eosr

#endif
