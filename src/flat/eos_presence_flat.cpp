// Flat C ABI trampolines for the Presence interface and its PresenceModification sub-handle.
//
// The modification calls (EOS_PresenceModification_*) take the sub-handle as their first argument,
// not the platform's Presence handle, so there is no slot to compare them against. They resolve
// through the live platform and are validated against the store that minted them, which makes an
// unknown or stale handle a rejection instead of a dereference.
#include "eos_presence.h"

#include "common/eos_names.h"
#include "core/platform.h"
#include "core/runtime.h"
#include "core/tracer.h"
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
    eosr::trace_scope eosr_trace(eosr::global_tracer(), "EOS_Presence_QueryPresence",
                                 Options != 0 ? Options->ApiVersion : 0,
                                 eosr::call_mode::async);
    eosr::sdk_presence* presence = checked_presence(Handle);
    if (presence != 0) {
        presence->query_presence(Options, ClientData, CompletionDelegate);
    }
}

EOS_DECLARE_FUNC(EOS_Bool) EOS_Presence_HasPresence(EOS_HPresence Handle, const EOS_Presence_HasPresenceOptions* Options) {
    eosr::trace_scope eosr_trace(eosr::global_tracer(), "EOS_Presence_HasPresence",
                                 Options != 0 ? Options->ApiVersion : 0,
                                 eosr::call_mode::sync);
    eosr::sdk_presence* presence = checked_presence(Handle);
    const EOS_Bool result = (presence != 0) ? presence->has_presence(Options) : EOS_FALSE;
    return eosr::traced_bool(eosr_trace, result);
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_Presence_CopyPresence(EOS_HPresence Handle, const EOS_Presence_CopyPresenceOptions* Options, EOS_Presence_Info** OutPresence) {
    eosr::tracer& trace = eosr::global_tracer();
    eosr::trace_scope eosr_trace(trace, "EOS_Presence_CopyPresence",
                                 Options != 0 ? Options->ApiVersion : 0,
                                 eosr::call_mode::sync);
    eosr::sdk_presence* presence = checked_presence(Handle);
    if (presence == 0) {
        // The header promises OutPresence is NULL on any non-success return.
        if (OutPresence != 0) {
            *OutPresence = 0;
        }
        return eosr::traced_result(eosr_trace, EOS_EResult::EOS_InvalidParameters);
    }
    const EOS_EResult result = presence->copy_presence(Options, OutPresence);
    eosr::trace_return value =
        eosr::return_result(static_cast<i32>(result), eosr::result_name(result));
    if (result == EOS_EResult::EOS_Success && OutPresence != 0 && *OutPresence != 0) {
        value.out.push_back(eosr::make_field(
            eosr::field_id::handle,
            eosr::tv_label(trace.label_pointer(eosr::label_kind::handle, *OutPresence))));
    }
    eosr_trace.returns(value);
    return result;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_Presence_CreatePresenceModification(EOS_HPresence Handle, const EOS_Presence_CreatePresenceModificationOptions* Options, EOS_HPresenceModification* OutPresenceModificationHandle) {
    eosr::tracer& trace = eosr::global_tracer();
    eosr::trace_scope eosr_trace(trace, "EOS_Presence_CreatePresenceModification",
                                 Options != 0 ? Options->ApiVersion : 0,
                                 eosr::call_mode::sync);
    eosr::sdk_presence* presence = checked_presence(Handle);
    if (presence == 0) {
        if (OutPresenceModificationHandle != 0) {
            *OutPresenceModificationHandle = 0;
        }
        return eosr::traced_result(eosr_trace, EOS_EResult::EOS_InvalidParameters);
    }
    const EOS_EResult result =
        presence->create_presence_modification(Options, OutPresenceModificationHandle);
    eosr::trace_return value =
        eosr::return_result(static_cast<i32>(result), eosr::result_name(result));
    if (result == EOS_EResult::EOS_Success && OutPresenceModificationHandle != 0 &&
        *OutPresenceModificationHandle != 0) {
        value.out.push_back(eosr::make_field(
            eosr::field_id::handle,
            eosr::tv_label(trace.label_pointer(eosr::label_kind::handle,
                                               *OutPresenceModificationHandle))));
    }
    eosr_trace.returns(value);
    return result;
}

EOS_DECLARE_FUNC(void) EOS_Presence_SetPresence(EOS_HPresence Handle, const EOS_Presence_SetPresenceOptions* Options,
                                                void* ClientData, const EOS_Presence_SetPresenceCompleteCallback CompletionDelegate) {
    eosr::trace_scope eosr_trace(eosr::global_tracer(), "EOS_Presence_SetPresence",
                                 Options != 0 ? Options->ApiVersion : 0,
                                 eosr::call_mode::async);
    eosr::sdk_presence* presence = checked_presence(Handle);
    if (presence != 0) {
        presence->set_presence(Options, ClientData, CompletionDelegate);
    }
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_Presence_GetJoinInfo(EOS_HPresence Handle, const EOS_Presence_GetJoinInfoOptions* Options, char* OutBuffer, int32_t* InOutBufferLength) {
    eosr::trace_scope eosr_trace(eosr::global_tracer(), "EOS_Presence_GetJoinInfo",
                                 Options != 0 ? Options->ApiVersion : 0,
                                 eosr::call_mode::sync);
    eosr::sdk_presence* presence = checked_presence(Handle);
    const EOS_EResult result =
        (presence != 0) ? presence->get_join_info(Options, OutBuffer, InOutBufferLength)
                        : EOS_EResult::EOS_InvalidParameters;
    eosr::trace_return value =
        eosr::return_result(static_cast<i32>(result), eosr::result_name(result));
    if ((result == EOS_EResult::EOS_Success || result == EOS_EResult::EOS_LimitExceeded) &&
        InOutBufferLength != 0) {
        value.out.push_back(eosr::make_field(
            eosr::field_id::len,
            eosr::tv_uint(static_cast<u64>(*InOutBufferLength < 0 ? 0 : *InOutBufferLength))));
    }
    eosr_trace.returns(value);
    return result;
}

EOS_DECLARE_FUNC(EOS_NotificationId) EOS_Presence_AddNotifyOnPresenceChanged(EOS_HPresence Handle, const EOS_Presence_AddNotifyOnPresenceChangedOptions* Options,
                                                                            void* ClientData, const EOS_Presence_OnPresenceChangedCallback NotificationHandler) {
    eosr::trace_scope eosr_trace(eosr::global_tracer(),
                                 "EOS_Presence_AddNotifyOnPresenceChanged",
                                 Options != 0 ? Options->ApiVersion : 0,
                                 eosr::call_mode::sync);
    eosr::sdk_presence* presence = checked_presence(Handle);
    const EOS_NotificationId result =
        (presence != 0)
            ? presence->add_notify_on_presence_changed(ClientData, NotificationHandler)
            : EOS_INVALID_NOTIFICATIONID;
    return eosr::traced_notification(eosr_trace, result);
}

EOS_DECLARE_FUNC(void) EOS_Presence_RemoveNotifyOnPresenceChanged(EOS_HPresence Handle, EOS_NotificationId NotificationId) {
    eosr::trace_scope eosr_trace(eosr::global_tracer(),
                                 "EOS_Presence_RemoveNotifyOnPresenceChanged", 0,
                                 eosr::call_mode::sync);
    (void)Handle;
    eosr::sdk_presence* presence = live_presence();
    if (presence != 0) {
        presence->remove_notify_on_presence_changed(NotificationId);
    }
}

EOS_DECLARE_FUNC(EOS_NotificationId) EOS_Presence_AddNotifyJoinGameAccepted(EOS_HPresence Handle, const EOS_Presence_AddNotifyJoinGameAcceptedOptions* Options,
                                                                           void* ClientData, const EOS_Presence_OnJoinGameAcceptedCallback NotificationFn) {
    eosr::trace_scope eosr_trace(eosr::global_tracer(),
                                 "EOS_Presence_AddNotifyJoinGameAccepted",
                                 Options != 0 ? Options->ApiVersion : 0,
                                 eosr::call_mode::sync);
    eosr::sdk_presence* presence = checked_presence(Handle);
    const EOS_NotificationId result =
        (presence != 0)
            ? presence->add_notify_join_game_accepted(ClientData, NotificationFn)
            : EOS_INVALID_NOTIFICATIONID;
    return eosr::traced_notification(eosr_trace, result);
}

EOS_DECLARE_FUNC(void) EOS_Presence_RemoveNotifyJoinGameAccepted(EOS_HPresence Handle, EOS_NotificationId InId) {
    eosr::trace_scope eosr_trace(eosr::global_tracer(),
                                 "EOS_Presence_RemoveNotifyJoinGameAccepted", 0,
                                 eosr::call_mode::sync);
    (void)Handle;
    eosr::sdk_presence* presence = live_presence();
    if (presence != 0) {
        presence->remove_notify_join_game_accepted(InId);
    }
}

EOS_DECLARE_FUNC(void) EOS_Presence_Info_Release(EOS_Presence_Info* PresenceInfo) {
    eosr::trace_scope eosr_trace(eosr::global_tracer(), "EOS_Presence_Info_Release", 0,
                                 eosr::call_mode::sync);
    eosr::release_presence_info(PresenceInfo);
}

// --- PresenceModification ---

EOS_DECLARE_FUNC(EOS_EResult) EOS_PresenceModification_SetStatus(EOS_HPresenceModification Handle, const EOS_PresenceModification_SetStatusOptions* Options) {
    eosr::trace_scope eosr_trace(eosr::global_tracer(),
                                 "EOS_PresenceModification_SetStatus",
                                 Options != 0 ? Options->ApiVersion : 0,
                                 eosr::call_mode::sync);
    eosr::sdk_presence* presence = live_presence();
    const EOS_EResult result =
        (presence != 0) ? presence->modification_set_status(Handle, Options)
                        : EOS_EResult::EOS_InvalidParameters;
    return eosr::traced_result(eosr_trace, result);
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_PresenceModification_SetRawRichText(EOS_HPresenceModification Handle, const EOS_PresenceModification_SetRawRichTextOptions* Options) {
    eosr::trace_scope eosr_trace(eosr::global_tracer(),
                                 "EOS_PresenceModification_SetRawRichText",
                                 Options != 0 ? Options->ApiVersion : 0,
                                 eosr::call_mode::sync);
    eosr::sdk_presence* presence = live_presence();
    const EOS_EResult result =
        (presence != 0) ? presence->modification_set_raw_rich_text(Handle, Options)
                        : EOS_EResult::EOS_InvalidParameters;
    return eosr::traced_result(eosr_trace, result);
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_PresenceModification_SetData(EOS_HPresenceModification Handle, const EOS_PresenceModification_SetDataOptions* Options) {
    eosr::trace_scope eosr_trace(eosr::global_tracer(), "EOS_PresenceModification_SetData",
                                 Options != 0 ? Options->ApiVersion : 0,
                                 eosr::call_mode::sync);
    eosr::sdk_presence* presence = live_presence();
    const EOS_EResult result =
        (presence != 0) ? presence->modification_set_data(Handle, Options)
                        : EOS_EResult::EOS_InvalidParameters;
    return eosr::traced_result(eosr_trace, result);
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_PresenceModification_DeleteData(EOS_HPresenceModification Handle, const EOS_PresenceModification_DeleteDataOptions* Options) {
    eosr::trace_scope eosr_trace(eosr::global_tracer(),
                                 "EOS_PresenceModification_DeleteData",
                                 Options != 0 ? Options->ApiVersion : 0,
                                 eosr::call_mode::sync);
    eosr::sdk_presence* presence = live_presence();
    const EOS_EResult result =
        (presence != 0) ? presence->modification_delete_data(Handle, Options)
                        : EOS_EResult::EOS_InvalidParameters;
    return eosr::traced_result(eosr_trace, result);
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_PresenceModification_SetJoinInfo(EOS_HPresenceModification Handle, const EOS_PresenceModification_SetJoinInfoOptions* Options) {
    eosr::trace_scope eosr_trace(eosr::global_tracer(),
                                 "EOS_PresenceModification_SetJoinInfo",
                                 Options != 0 ? Options->ApiVersion : 0,
                                 eosr::call_mode::sync);
    eosr::sdk_presence* presence = live_presence();
    const EOS_EResult result =
        (presence != 0) ? presence->modification_set_join_info(Handle, Options)
                        : EOS_EResult::EOS_InvalidParameters;
    return eosr::traced_result(eosr_trace, result);
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_PresenceModification_SetTemplateId(EOS_HPresenceModification Handle, const EOS_PresenceModification_SetTemplateIdOptions* Options) {
    eosr::trace_scope eosr_trace(eosr::global_tracer(),
                                 "EOS_PresenceModification_SetTemplateId",
                                 Options != 0 ? Options->ApiVersion : 0,
                                 eosr::call_mode::sync);
    eosr::sdk_presence* presence = live_presence();
    const EOS_EResult result =
        (presence != 0) ? presence->modification_set_template_id(Handle, Options)
                        : EOS_EResult::EOS_InvalidParameters;
    return eosr::traced_result(eosr_trace, result);
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_PresenceModification_SetTemplateData(EOS_HPresenceModification Handle, const EOS_PresenceModification_SetTemplateDataOptions* Options) {
    eosr::trace_scope eosr_trace(eosr::global_tracer(),
                                 "EOS_PresenceModification_SetTemplateData",
                                 Options != 0 ? Options->ApiVersion : 0,
                                 eosr::call_mode::sync);
    eosr::sdk_presence* presence = live_presence();
    const EOS_EResult result =
        (presence != 0) ? presence->modification_set_template_data(Handle, Options)
                        : EOS_EResult::EOS_InvalidParameters;
    return eosr::traced_result(eosr_trace, result);
}

EOS_DECLARE_FUNC(void) EOS_PresenceModification_Release(EOS_HPresenceModification PresenceModificationHandle) {
    eosr::trace_scope eosr_trace(eosr::global_tracer(), "EOS_PresenceModification_Release", 0,
                                 eosr::call_mode::sync);
    eosr::sdk_presence* presence = live_presence();
    if (presence != 0) {
        presence->modification_release(PresenceModificationHandle);
    }
}
