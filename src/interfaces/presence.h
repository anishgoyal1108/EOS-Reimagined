#ifndef EOSR_INTERFACES_PRESENCE_H
#define EOSR_INTERFACES_PRESENCE_H

#include <chrono>
#include <map>
#include <string>
#include <vector>

#include "eos_common.h"
#include "eos_presence_localized_types.h"
#include "eos_presence_types.h"

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

// The Presence interface: the status line a game shows next to a friend's name, and the opaque
// "join" string that lets you drop into whatever they are playing. Each peer knows its own presence
// and tells the others; a query is a question answered by whoever owns that account.
// Presence is keyed by Epic account id (the Auth identity), not the product user id.
// Spec: EOSSDK_Presence (docs/presence.md), presence protocol (docs/protocol.md)
class sdk_presence : public i_run_callback, public i_run_network {
public:
    sdk_presence(sdk_settings& settings, callback_manager& callbacks, message_router& network);
    ~sdk_presence();

    sdk_presence(const sdk_presence&) = delete;
    sdk_presence& operator=(const sdk_presence&) = delete;

    void emu_init();
    void emu_deinit();

    // --- Presence ---
    void query_presence(const EOS_Presence_QueryPresenceOptions* options, void* client_data,
                        EOS_Presence_OnQueryPresenceCompleteCallback delegate);
    EOS_Bool has_presence(const EOS_Presence_HasPresenceOptions* options) const;
    EOS_EResult copy_presence(const EOS_Presence_CopyPresenceOptions* options,
                              EOS_Presence_Info** out) const;
    EOS_EResult create_presence_modification(
        const EOS_Presence_CreatePresenceModificationOptions* options,
        EOS_HPresenceModification* out);
    void set_presence(const EOS_Presence_SetPresenceOptions* options, void* client_data,
                      EOS_Presence_SetPresenceCompleteCallback delegate);
    EOS_EResult get_join_info(const EOS_Presence_GetJoinInfoOptions* options, char* out_buffer,
                              i32* inout_buffer_length) const;

    EOS_NotificationId add_notify_on_presence_changed(
        void* client_data, EOS_Presence_OnPresenceChangedCallback delegate);
    void remove_notify_on_presence_changed(EOS_NotificationId id);
    EOS_NotificationId add_notify_join_game_accepted(
        void* client_data, EOS_Presence_OnJoinGameAcceptedCallback delegate);
    void remove_notify_join_game_accepted(EOS_NotificationId id);

    // --- PresenceModification sub-handle ---
    EOS_EResult modification_set_status(void* handle,
                                        const EOS_PresenceModification_SetStatusOptions* options);
    EOS_EResult modification_set_raw_rich_text(
        void* handle, const EOS_PresenceModification_SetRawRichTextOptions* options);
    EOS_EResult modification_set_data(void* handle,
                                      const EOS_PresenceModification_SetDataOptions* options);
    EOS_EResult modification_delete_data(void* handle,
                                         const EOS_PresenceModification_DeleteDataOptions* options);
    EOS_EResult modification_set_join_info(
        void* handle, const EOS_PresenceModification_SetJoinInfoOptions* options);
    EOS_EResult modification_set_template_id(
        void* handle, const EOS_PresenceModification_SetTemplateIdOptions* options);
    EOS_EResult modification_set_template_data(
        void* handle, const EOS_PresenceModification_SetTemplateDataOptions* options);
    void modification_release(void* handle);

    // --- i_run_callback ---
    bool cb_run_frame();
    bool run_callbacks(frame_result& result);
    void free_callback(frame_result& result);

    // --- i_run_network ---
    bool on_network_message(const net_envelope& message);

private:
    // A staged set of changes to our own presence, applied all at once by SetPresence. Each field
    // is only touched if the game asked for it, so applying one change does not blank the rest.
    struct modification_object {
        bool set_status;
        i32 status;
        bool set_rich_text;
        std::string rich_text;
        bool set_join_info;
        std::string join_info;
        std::vector<presence_data_record> data_set;    // records to add or overwrite
        std::vector<std::string> data_deleted;         // keys to remove
    };

    void seed_myself();
    void broadcast_my_presence(const std::string& to_peer);
    void deliver_query(void* client_data, EOS_Presence_OnQueryPresenceCompleteCallback delegate,
                       const std::string& local, const std::string& target, EOS_EResult code);
    void fire_presence_changed(const std::string& changed_epic_id);
    presence_info* find_presence(const std::string& epic_id);
    const presence_info* find_presence(const std::string& epic_id) const;

    sdk_settings& settings_;
    callback_manager& callbacks_;
    message_router& network_;

    // Every account we know a presence for, our own included, keyed by Epic account id.
    std::map<std::string, presence_info> presences_;

    // Which mesh peer (product user id) owns each Epic account id, learned from the first peer to
    // announce it. A later announcement from anyone else is refused, so no peer can speak for an
    // account it does not own.
    std::map<std::string, std::string> epic_owner_;

    handle_store<modification_object> modifications_;

    // A QueryPresence waiting on the owning peer's answer, keyed by the result the callback manager
    // owns so the tick can time it out.
    struct pending_query {
        std::string local_id;
        std::string target_id;
        std::chrono::steady_clock::time_point deadline;
    };
    std::map<frame_result*, pending_query> pending_queries_;

    bool registered_;
};

// The struct CopyPresence hands the game. It carries no handle, so the game frees it through this,
// and the object behind it lives outside any one platform. Release it exactly once; a null pointer,
// or one we never issued, is ignored.
void release_presence_info(EOS_Presence_Info* info);

} // namespace eosr

#endif
