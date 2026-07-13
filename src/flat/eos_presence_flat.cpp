// Flat C ABI trampolines for the Presence interface and its PresenceModification sub-handle.
//
// The modification calls (EOS_PresenceModification_*) take the sub-handle as their first argument,
// not the platform's Presence handle, so there is no slot to compare them against. They resolve
// through the live platform and are validated against the store that minted them, which makes an
// unknown or stale handle a rejection instead of a dereference.
#include "eos_presence.h"

#include "core/platform.h"
#include "core/runtime.h"
#include "interfaces/presence.h"

namespace {

eosr::sdk_presence* live_presence() {
    eosr::sdk_platform* platform = eosr::platform_current();
    if (platform == 0 || !platform->is_created()) {
        return 0;
    }
    return &platform->presence();
}

// The Presence handle itself is the platform's slot, so it can be checked exactly.
eosr::sdk_presence* checked_presence(EOS_HPresence handle) {
    eosr::sdk_platform* platform = eosr::platform_current();
    if (platform == 0 || !platform->is_created()) {
        return 0;
    }
    if (handle != reinterpret_cast<EOS_HPresence>(platform->interface_handle(eosr::if_presence))) {
        return 0;
    }
    return &platform->presence();
}

} // namespace

// --- Presence ---

EOS_DECLARE_FUNC(void) EOS_Presence_QueryPresence(EOS_HPresence Handle, const EOS_Presence_QueryPresenceOptions* Options,
                                                  void* ClientData, const EOS_Presence_OnQueryPresenceCompleteCallback CompletionDelegate) {
    eosr::sdk_presence* presence = checked_presence(Handle);
    if (presence != 0) {
        presence->query_presence(Options, ClientData, CompletionDelegate);
    }
}

EOS_DECLARE_FUNC(EOS_Bool) EOS_Presence_HasPresence(EOS_HPresence Handle, const EOS_Presence_HasPresenceOptions* Options) {
    eosr::sdk_presence* presence = checked_presence(Handle);
    return (presence != 0) ? presence->has_presence(Options) : EOS_FALSE;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_Presence_CopyPresence(EOS_HPresence Handle, const EOS_Presence_CopyPresenceOptions* Options, EOS_Presence_Info** OutPresence) {
    eosr::sdk_presence* presence = checked_presence(Handle);
    if (presence == 0) {
        // The header promises OutPresence is NULL on any non-success return.
        if (OutPresence != 0) {
            *OutPresence = 0;
        }
        return EOS_EResult::EOS_InvalidParameters;
    }
    return presence->copy_presence(Options, OutPresence);
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_Presence_CreatePresenceModification(EOS_HPresence Handle, const EOS_Presence_CreatePresenceModificationOptions* Options, EOS_HPresenceModification* OutPresenceModificationHandle) {
    eosr::sdk_presence* presence = checked_presence(Handle);
    if (presence == 0) {
        if (OutPresenceModificationHandle != 0) {
            *OutPresenceModificationHandle = 0;
        }
        return EOS_EResult::EOS_InvalidParameters;
    }
    return presence->create_presence_modification(Options, OutPresenceModificationHandle);
}

EOS_DECLARE_FUNC(void) EOS_Presence_SetPresence(EOS_HPresence Handle, const EOS_Presence_SetPresenceOptions* Options,
                                                void* ClientData, const EOS_Presence_SetPresenceCompleteCallback CompletionDelegate) {
    eosr::sdk_presence* presence = checked_presence(Handle);
    if (presence != 0) {
        presence->set_presence(Options, ClientData, CompletionDelegate);
    }
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_Presence_GetJoinInfo(EOS_HPresence Handle, const EOS_Presence_GetJoinInfoOptions* Options, char* OutBuffer, int32_t* InOutBufferLength) {
    eosr::sdk_presence* presence = checked_presence(Handle);
    return (presence != 0) ? presence->get_join_info(Options, OutBuffer, InOutBufferLength) : EOS_EResult::EOS_InvalidParameters;
}

EOS_DECLARE_FUNC(EOS_NotificationId) EOS_Presence_AddNotifyOnPresenceChanged(EOS_HPresence Handle, const EOS_Presence_AddNotifyOnPresenceChangedOptions* Options,
                                                                            void* ClientData, const EOS_Presence_OnPresenceChangedCallback NotificationHandler) {
    (void)Options;
    eosr::sdk_presence* presence = checked_presence(Handle);
    return (presence != 0) ? presence->add_notify_on_presence_changed(ClientData, NotificationHandler) : EOS_INVALID_NOTIFICATIONID;
}

EOS_DECLARE_FUNC(void) EOS_Presence_RemoveNotifyOnPresenceChanged(EOS_HPresence Handle, EOS_NotificationId NotificationId) {
    (void)Handle;
    eosr::sdk_presence* presence = live_presence();
    if (presence != 0) {
        presence->remove_notify_on_presence_changed(NotificationId);
    }
}

EOS_DECLARE_FUNC(EOS_NotificationId) EOS_Presence_AddNotifyJoinGameAccepted(EOS_HPresence Handle, const EOS_Presence_AddNotifyJoinGameAcceptedOptions* Options,
                                                                           void* ClientData, const EOS_Presence_OnJoinGameAcceptedCallback NotificationFn) {
    (void)Options;
    eosr::sdk_presence* presence = checked_presence(Handle);
    return (presence != 0) ? presence->add_notify_join_game_accepted(ClientData, NotificationFn) : EOS_INVALID_NOTIFICATIONID;
}

EOS_DECLARE_FUNC(void) EOS_Presence_RemoveNotifyJoinGameAccepted(EOS_HPresence Handle, EOS_NotificationId InId) {
    (void)Handle;
    eosr::sdk_presence* presence = live_presence();
    if (presence != 0) {
        presence->remove_notify_join_game_accepted(InId);
    }
}

EOS_DECLARE_FUNC(void) EOS_Presence_Info_Release(EOS_Presence_Info* PresenceInfo) {
    eosr::release_presence_info(PresenceInfo);
}

// --- PresenceModification ---

EOS_DECLARE_FUNC(EOS_EResult) EOS_PresenceModification_SetStatus(EOS_HPresenceModification Handle, const EOS_PresenceModification_SetStatusOptions* Options) {
    eosr::sdk_presence* presence = live_presence();
    return (presence != 0) ? presence->modification_set_status(Handle, Options) : EOS_EResult::EOS_InvalidParameters;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_PresenceModification_SetRawRichText(EOS_HPresenceModification Handle, const EOS_PresenceModification_SetRawRichTextOptions* Options) {
    eosr::sdk_presence* presence = live_presence();
    return (presence != 0) ? presence->modification_set_raw_rich_text(Handle, Options) : EOS_EResult::EOS_InvalidParameters;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_PresenceModification_SetData(EOS_HPresenceModification Handle, const EOS_PresenceModification_SetDataOptions* Options) {
    eosr::sdk_presence* presence = live_presence();
    return (presence != 0) ? presence->modification_set_data(Handle, Options) : EOS_EResult::EOS_InvalidParameters;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_PresenceModification_DeleteData(EOS_HPresenceModification Handle, const EOS_PresenceModification_DeleteDataOptions* Options) {
    eosr::sdk_presence* presence = live_presence();
    return (presence != 0) ? presence->modification_delete_data(Handle, Options) : EOS_EResult::EOS_InvalidParameters;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_PresenceModification_SetJoinInfo(EOS_HPresenceModification Handle, const EOS_PresenceModification_SetJoinInfoOptions* Options) {
    eosr::sdk_presence* presence = live_presence();
    return (presence != 0) ? presence->modification_set_join_info(Handle, Options) : EOS_EResult::EOS_InvalidParameters;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_PresenceModification_SetTemplateId(EOS_HPresenceModification Handle, const EOS_PresenceModification_SetTemplateIdOptions* Options) {
    eosr::sdk_presence* presence = live_presence();
    return (presence != 0) ? presence->modification_set_template_id(Handle, Options) : EOS_EResult::EOS_InvalidParameters;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_PresenceModification_SetTemplateData(EOS_HPresenceModification Handle, const EOS_PresenceModification_SetTemplateDataOptions* Options) {
    eosr::sdk_presence* presence = live_presence();
    return (presence != 0) ? presence->modification_set_template_data(Handle, Options) : EOS_EResult::EOS_InvalidParameters;
}

EOS_DECLARE_FUNC(void) EOS_PresenceModification_Release(EOS_HPresenceModification PresenceModificationHandle) {
    eosr::sdk_presence* presence = live_presence();
    if (presence != 0) {
        presence->modification_release(PresenceModificationHandle);
    }
}
