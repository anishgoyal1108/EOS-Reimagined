// Flat C ABI trampolines for the Sessions interface and its four sub-handles.
//
// The sub-handle calls (EOS_SessionModification_*, EOS_SessionSearch_*, EOS_SessionDetails_*,
// EOS_ActiveSession_*) take the sub-handle as their first argument, not the platform's Sessions
// handle, so there is no slot to compare them against. They resolve through the live platform and
// are validated against the store that minted them, which is what makes an unknown or stale handle
// a rejection instead of a dereference.
#include "eos_sessions.h"

#include "common/eos_names.h"
#include "core/platform.h"
#include "core/runtime.h"
#include "core/tracer.h"
#include "interfaces/sessions.h"

namespace {

eosr::sdk_sessions* live_sessions() {
    eosr::sdk_platform* platform = eosr::platform_current();
    if (platform == 0 || !platform->is_created()) {
        return 0;
    }
    return &platform->sessions();
}

// The Sessions handle itself is the platform's slot, so it can be checked exactly.
eosr::sdk_sessions* checked_sessions(EOS_HSessions handle) {
    eosr::sdk_platform* platform = eosr::platform_current();
    if (platform == 0 || !platform->is_created()) {
        return 0;
    }
    if (handle != reinterpret_cast<EOS_HSessions>(platform->interface_handle(eosr::if_sessions))) {
        return 0;
    }
    return &platform->sessions();
}

template <class delegate_type>
void stub_async(EOS_HSessions handle, void* client_data, delegate_type delegate,
                std::size_t info_size) {
    eosr::sdk_sessions* sessions = checked_sessions(handle);
    if (sessions != 0) {
        sessions->queue_stub_result(client_data,
                                    reinterpret_cast<eosr::completion_delegate>(delegate), info_size);
    }
}

template <class delegate_type>
EOS_NotificationId stub_notify(EOS_HSessions handle, void* client_data, delegate_type delegate,
                               std::size_t info_size, const char* event) {
    eosr::sdk_sessions* sessions = checked_sessions(handle);
    if (sessions == 0) {
        return EOS_INVALID_NOTIFICATIONID;
    }
    return sessions->add_stub_notification(
        client_data, reinterpret_cast<eosr::completion_delegate>(delegate), info_size, event);
}

template <class options_type>
i32 api_version(const options_type* options) {
    return options != 0 ? options->ApiVersion : 0;
}

EOS_EResult traced_out_result(eosr::trace_scope& scope, eosr::tracer& trace,
                              EOS_EResult result, const void* out, eosr::field_id field,
                              eosr::label_kind kind) {
    eosr::trace_return value =
        eosr::return_result(static_cast<i32>(result), eosr::result_name(result));
    if (result == EOS_EResult::EOS_Success && out != 0) {
        value.out.push_back(eosr::make_field(
            field, eosr::tv_label(trace.label_pointer(kind, out))));
    }
    scope.returns(value);
    return result;
}

} // namespace

// --- Sessions ---

EOS_DECLARE_FUNC(EOS_EResult) EOS_Sessions_CreateSessionModification(EOS_HSessions Handle, const EOS_Sessions_CreateSessionModificationOptions* Options, EOS_HSessionModification* OutSessionModificationHandle) {
    eosr::tracer& trace = eosr::global_tracer();
    eosr::trace_scope eosr_trace(trace, "EOS_Sessions_CreateSessionModification",
                                 api_version(Options), eosr::call_mode::sync);
    eosr::sdk_sessions* sessions = checked_sessions(Handle);
    if (OutSessionModificationHandle != 0) {
        *OutSessionModificationHandle = 0;
    }
    const EOS_EResult result =
        (sessions != 0) ? sessions->create_session_modification(Options, OutSessionModificationHandle)
                        : EOS_EResult::EOS_InvalidParameters;
    return traced_out_result(eosr_trace, trace, result,
                             OutSessionModificationHandle != 0
                                 ? *OutSessionModificationHandle : 0,
                             eosr::field_id::handle, eosr::label_kind::handle);
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_Sessions_UpdateSessionModification(EOS_HSessions Handle, const EOS_Sessions_UpdateSessionModificationOptions* Options, EOS_HSessionModification* OutSessionModificationHandle) {
    eosr::tracer& trace = eosr::global_tracer();
    eosr::trace_scope eosr_trace(trace, "EOS_Sessions_UpdateSessionModification",
                                 api_version(Options), eosr::call_mode::sync);
    eosr::sdk_sessions* sessions = checked_sessions(Handle);
    if (OutSessionModificationHandle != 0) {
        *OutSessionModificationHandle = 0;
    }
    const EOS_EResult result =
        (sessions != 0) ? sessions->update_session_modification(Options, OutSessionModificationHandle)
                        : EOS_EResult::EOS_InvalidParameters;
    return traced_out_result(eosr_trace, trace, result,
                             OutSessionModificationHandle != 0
                                 ? *OutSessionModificationHandle : 0,
                             eosr::field_id::handle, eosr::label_kind::handle);
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_Sessions_CreateSessionSearch(EOS_HSessions Handle, const EOS_Sessions_CreateSessionSearchOptions* Options, EOS_HSessionSearch* OutSessionSearchHandle) {
    eosr::tracer& trace = eosr::global_tracer();
    eosr::trace_scope eosr_trace(trace, "EOS_Sessions_CreateSessionSearch",
                                 api_version(Options), eosr::call_mode::sync);
    eosr::sdk_sessions* sessions = checked_sessions(Handle);
    if (OutSessionSearchHandle != 0) {
        *OutSessionSearchHandle = 0;
    }
    const EOS_EResult result =
        (sessions != 0) ? sessions->create_session_search(Options, OutSessionSearchHandle)
                        : EOS_EResult::EOS_InvalidParameters;
    return traced_out_result(eosr_trace, trace, result,
                             OutSessionSearchHandle != 0 ? *OutSessionSearchHandle : 0,
                             eosr::field_id::handle, eosr::label_kind::handle);
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_Sessions_CopyActiveSessionHandle(EOS_HSessions Handle, const EOS_Sessions_CopyActiveSessionHandleOptions* Options, EOS_HActiveSession* OutSessionHandle) {
    eosr::tracer& trace = eosr::global_tracer();
    eosr::trace_scope eosr_trace(trace, "EOS_Sessions_CopyActiveSessionHandle",
                                 api_version(Options), eosr::call_mode::sync);
    eosr::sdk_sessions* sessions = checked_sessions(Handle);
    if (OutSessionHandle != 0) {
        *OutSessionHandle = 0;
    }
    const EOS_EResult result =
        (sessions != 0) ? sessions->copy_active_session_handle(Options, OutSessionHandle)
                        : EOS_EResult::EOS_InvalidParameters;
    return traced_out_result(eosr_trace, trace, result,
                             OutSessionHandle != 0 ? *OutSessionHandle : 0,
                             eosr::field_id::session, eosr::label_kind::session);
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_Sessions_IsUserInSession(EOS_HSessions Handle, const EOS_Sessions_IsUserInSessionOptions* Options) {
    eosr::trace_scope eosr_trace(eosr::global_tracer(), "EOS_Sessions_IsUserInSession",
                                 api_version(Options), eosr::call_mode::sync);
    eosr::sdk_sessions* sessions = checked_sessions(Handle);
    const EOS_EResult result =
        (sessions != 0) ? sessions->is_user_in_session(Options)
                        : EOS_EResult::EOS_InvalidParameters;
    return eosr::traced_result(eosr_trace, result);
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_Sessions_DumpSessionState(EOS_HSessions Handle, const EOS_Sessions_DumpSessionStateOptions* Options) {
    eosr::trace_scope eosr_trace(eosr::global_tracer(), "EOS_Sessions_DumpSessionState",
                                 api_version(Options), eosr::call_mode::sync);
    eosr::sdk_sessions* sessions = checked_sessions(Handle);
    const EOS_EResult result =
        (sessions != 0) ? sessions->dump_session_state(Options)
                        : EOS_EResult::EOS_InvalidParameters;
    return eosr::traced_result(eosr_trace, result);
}

EOS_DECLARE_FUNC(void) EOS_Sessions_UpdateSession(EOS_HSessions Handle, const EOS_Sessions_UpdateSessionOptions* Options,
                                              void* ClientData, const EOS_Sessions_OnUpdateSessionCallback CompletionDelegate) {
    eosr::trace_scope eosr_trace(eosr::global_tracer(), "EOS_Sessions_UpdateSession",
                                 api_version(Options), eosr::call_mode::async);
    eosr::sdk_sessions* sessions = checked_sessions(Handle);
    if (sessions != 0) {
        sessions->update_session(Options, ClientData, CompletionDelegate);
    }
}

EOS_DECLARE_FUNC(void) EOS_Sessions_DestroySession(EOS_HSessions Handle, const EOS_Sessions_DestroySessionOptions* Options,
                                              void* ClientData, const EOS_Sessions_OnDestroySessionCallback CompletionDelegate) {
    eosr::trace_scope eosr_trace(eosr::global_tracer(), "EOS_Sessions_DestroySession",
                                 api_version(Options), eosr::call_mode::async);
    eosr::sdk_sessions* sessions = checked_sessions(Handle);
    if (sessions != 0) {
        sessions->destroy_session(Options, ClientData, CompletionDelegate);
    }
}

EOS_DECLARE_FUNC(void) EOS_Sessions_JoinSession(EOS_HSessions Handle, const EOS_Sessions_JoinSessionOptions* Options,
                                              void* ClientData, const EOS_Sessions_OnJoinSessionCallback CompletionDelegate) {
    eosr::trace_scope eosr_trace(eosr::global_tracer(), "EOS_Sessions_JoinSession",
                                 api_version(Options), eosr::call_mode::async);
    eosr::sdk_sessions* sessions = checked_sessions(Handle);
    if (sessions != 0) {
        sessions->join_session(Options, ClientData, CompletionDelegate);
    }
}

EOS_DECLARE_FUNC(void) EOS_Sessions_StartSession(EOS_HSessions Handle, const EOS_Sessions_StartSessionOptions* Options,
                                              void* ClientData, const EOS_Sessions_OnStartSessionCallback CompletionDelegate) {
    eosr::trace_scope eosr_trace(eosr::global_tracer(), "EOS_Sessions_StartSession",
                                 api_version(Options), eosr::call_mode::async);
    eosr::sdk_sessions* sessions = checked_sessions(Handle);
    if (sessions != 0) {
        sessions->start_session(Options, ClientData, CompletionDelegate);
    }
}

EOS_DECLARE_FUNC(void) EOS_Sessions_EndSession(EOS_HSessions Handle, const EOS_Sessions_EndSessionOptions* Options,
                                              void* ClientData, const EOS_Sessions_OnEndSessionCallback CompletionDelegate) {
    eosr::trace_scope eosr_trace(eosr::global_tracer(), "EOS_Sessions_EndSession",
                                 api_version(Options), eosr::call_mode::async);
    eosr::sdk_sessions* sessions = checked_sessions(Handle);
    if (sessions != 0) {
        sessions->end_session(Options, ClientData, CompletionDelegate);
    }
}

EOS_DECLARE_FUNC(void) EOS_Sessions_RegisterPlayers(EOS_HSessions Handle, const EOS_Sessions_RegisterPlayersOptions* Options,
                                              void* ClientData, const EOS_Sessions_OnRegisterPlayersCallback CompletionDelegate) {
    eosr::trace_scope eosr_trace(eosr::global_tracer(), "EOS_Sessions_RegisterPlayers",
                                 api_version(Options), eosr::call_mode::async);
    eosr::sdk_sessions* sessions = checked_sessions(Handle);
    if (sessions != 0) {
        sessions->register_players(Options, ClientData, CompletionDelegate);
    }
}

EOS_DECLARE_FUNC(void) EOS_Sessions_UnregisterPlayers(EOS_HSessions Handle, const EOS_Sessions_UnregisterPlayersOptions* Options,
                                              void* ClientData, const EOS_Sessions_OnUnregisterPlayersCallback CompletionDelegate) {
    eosr::trace_scope eosr_trace(eosr::global_tracer(), "EOS_Sessions_UnregisterPlayers",
                                 api_version(Options), eosr::call_mode::async);
    eosr::sdk_sessions* sessions = checked_sessions(Handle);
    if (sessions != 0) {
        sessions->unregister_players(Options, ClientData, CompletionDelegate);
    }
}

// --- SessionModification ---

EOS_DECLARE_FUNC(EOS_EResult) EOS_SessionModification_SetBucketId(EOS_HSessionModification Handle,
                                                                 const EOS_SessionModification_SetBucketIdOptions* Options) {
    eosr::trace_scope eosr_trace(eosr::global_tracer(),
                                 "EOS_SessionModification_SetBucketId",
                                 api_version(Options), eosr::call_mode::sync);
    eosr::sdk_sessions* sessions = live_sessions();
    const EOS_EResult result =
        (sessions != 0) ? sessions->modification_set_bucket_id(Handle, Options)
                        : EOS_EResult::EOS_InvalidParameters;
    return eosr::traced_result(eosr_trace, result);
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_SessionModification_SetHostAddress(EOS_HSessionModification Handle,
                                                                 const EOS_SessionModification_SetHostAddressOptions* Options) {
    eosr::trace_scope eosr_trace(eosr::global_tracer(),
                                 "EOS_SessionModification_SetHostAddress",
                                 api_version(Options), eosr::call_mode::sync);
    eosr::sdk_sessions* sessions = live_sessions();
    const EOS_EResult result =
        (sessions != 0) ? sessions->modification_set_host_address(Handle, Options)
                        : EOS_EResult::EOS_InvalidParameters;
    return eosr::traced_result(eosr_trace, result);
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_SessionModification_SetPermissionLevel(EOS_HSessionModification Handle,
                                                                 const EOS_SessionModification_SetPermissionLevelOptions* Options) {
    eosr::trace_scope eosr_trace(eosr::global_tracer(),
                                 "EOS_SessionModification_SetPermissionLevel",
                                 api_version(Options), eosr::call_mode::sync);
    eosr::sdk_sessions* sessions = live_sessions();
    const EOS_EResult result =
        (sessions != 0) ? sessions->modification_set_permission_level(Handle, Options)
                        : EOS_EResult::EOS_InvalidParameters;
    return eosr::traced_result(eosr_trace, result);
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_SessionModification_SetJoinInProgressAllowed(EOS_HSessionModification Handle,
                                                                 const EOS_SessionModification_SetJoinInProgressAllowedOptions* Options) {
    eosr::trace_scope eosr_trace(eosr::global_tracer(),
                                 "EOS_SessionModification_SetJoinInProgressAllowed",
                                 api_version(Options), eosr::call_mode::sync);
    eosr::sdk_sessions* sessions = live_sessions();
    const EOS_EResult result =
        (sessions != 0) ? sessions->modification_set_join_in_progress(Handle, Options)
                        : EOS_EResult::EOS_InvalidParameters;
    return eosr::traced_result(eosr_trace, result);
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_SessionModification_SetMaxPlayers(EOS_HSessionModification Handle,
                                                                 const EOS_SessionModification_SetMaxPlayersOptions* Options) {
    eosr::trace_scope eosr_trace(eosr::global_tracer(),
                                 "EOS_SessionModification_SetMaxPlayers",
                                 api_version(Options), eosr::call_mode::sync);
    eosr::sdk_sessions* sessions = live_sessions();
    const EOS_EResult result =
        (sessions != 0) ? sessions->modification_set_max_players(Handle, Options)
                        : EOS_EResult::EOS_InvalidParameters;
    return eosr::traced_result(eosr_trace, result);
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_SessionModification_SetInvitesAllowed(EOS_HSessionModification Handle,
                                                                 const EOS_SessionModification_SetInvitesAllowedOptions* Options) {
    eosr::trace_scope eosr_trace(eosr::global_tracer(),
                                 "EOS_SessionModification_SetInvitesAllowed",
                                 api_version(Options), eosr::call_mode::sync);
    eosr::sdk_sessions* sessions = live_sessions();
    const EOS_EResult result =
        (sessions != 0) ? sessions->modification_set_invites_allowed(Handle, Options)
                        : EOS_EResult::EOS_InvalidParameters;
    return eosr::traced_result(eosr_trace, result);
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_SessionModification_AddAttribute(EOS_HSessionModification Handle,
                                                                 const EOS_SessionModification_AddAttributeOptions* Options) {
    eosr::trace_scope eosr_trace(eosr::global_tracer(),
                                 "EOS_SessionModification_AddAttribute",
                                 api_version(Options), eosr::call_mode::sync);
    eosr::sdk_sessions* sessions = live_sessions();
    const EOS_EResult result =
        (sessions != 0) ? sessions->modification_add_attribute(Handle, Options)
                        : EOS_EResult::EOS_InvalidParameters;
    return eosr::traced_result(eosr_trace, result);
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_SessionModification_RemoveAttribute(EOS_HSessionModification Handle,
                                                                 const EOS_SessionModification_RemoveAttributeOptions* Options) {
    eosr::trace_scope eosr_trace(eosr::global_tracer(),
                                 "EOS_SessionModification_RemoveAttribute",
                                 api_version(Options), eosr::call_mode::sync);
    eosr::sdk_sessions* sessions = live_sessions();
    const EOS_EResult result =
        (sessions != 0) ? sessions->modification_remove_attribute(Handle, Options)
                        : EOS_EResult::EOS_InvalidParameters;
    return eosr::traced_result(eosr_trace, result);
}

// --- SessionSearch ---

EOS_DECLARE_FUNC(EOS_EResult) EOS_SessionSearch_SetSessionId(EOS_HSessionSearch Handle,
                                                           const EOS_SessionSearch_SetSessionIdOptions* Options) {
    eosr::trace_scope eosr_trace(eosr::global_tracer(), "EOS_SessionSearch_SetSessionId",
                                 api_version(Options), eosr::call_mode::sync);
    eosr::sdk_sessions* sessions = live_sessions();
    const EOS_EResult result =
        (sessions != 0) ? sessions->search_set_session_id(Handle, Options)
                        : EOS_EResult::EOS_InvalidParameters;
    return eosr::traced_result(eosr_trace, result);
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_SessionSearch_SetTargetUserId(EOS_HSessionSearch Handle,
                                                           const EOS_SessionSearch_SetTargetUserIdOptions* Options) {
    eosr::trace_scope eosr_trace(eosr::global_tracer(),
                                 "EOS_SessionSearch_SetTargetUserId",
                                 api_version(Options), eosr::call_mode::sync);
    eosr::sdk_sessions* sessions = live_sessions();
    const EOS_EResult result =
        (sessions != 0) ? sessions->search_set_target_user(Handle, Options)
                        : EOS_EResult::EOS_InvalidParameters;
    return eosr::traced_result(eosr_trace, result);
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_SessionSearch_SetParameter(EOS_HSessionSearch Handle,
                                                           const EOS_SessionSearch_SetParameterOptions* Options) {
    eosr::trace_scope eosr_trace(eosr::global_tracer(), "EOS_SessionSearch_SetParameter",
                                 api_version(Options), eosr::call_mode::sync);
    eosr::sdk_sessions* sessions = live_sessions();
    const EOS_EResult result =
        (sessions != 0) ? sessions->search_set_parameter(Handle, Options)
                        : EOS_EResult::EOS_InvalidParameters;
    return eosr::traced_result(eosr_trace, result);
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_SessionSearch_RemoveParameter(EOS_HSessionSearch Handle,
                                                           const EOS_SessionSearch_RemoveParameterOptions* Options) {
    eosr::trace_scope eosr_trace(eosr::global_tracer(), "EOS_SessionSearch_RemoveParameter",
                                 api_version(Options), eosr::call_mode::sync);
    eosr::sdk_sessions* sessions = live_sessions();
    const EOS_EResult result =
        (sessions != 0) ? sessions->search_remove_parameter(Handle, Options)
                        : EOS_EResult::EOS_InvalidParameters;
    return eosr::traced_result(eosr_trace, result);
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_SessionSearch_SetMaxResults(EOS_HSessionSearch Handle,
                                                           const EOS_SessionSearch_SetMaxResultsOptions* Options) {
    eosr::trace_scope eosr_trace(eosr::global_tracer(), "EOS_SessionSearch_SetMaxResults",
                                 api_version(Options), eosr::call_mode::sync);
    eosr::sdk_sessions* sessions = live_sessions();
    const EOS_EResult result =
        (sessions != 0) ? sessions->search_set_max_results(Handle, Options)
                        : EOS_EResult::EOS_InvalidParameters;
    return eosr::traced_result(eosr_trace, result);
}

EOS_DECLARE_FUNC(void) EOS_SessionSearch_Find(EOS_HSessionSearch Handle, const EOS_SessionSearch_FindOptions* Options,
                                             void* ClientData, const EOS_SessionSearch_OnFindCallback CompletionDelegate) {
    eosr::trace_scope eosr_trace(eosr::global_tracer(), "EOS_SessionSearch_Find",
                                 api_version(Options), eosr::call_mode::async);
    eosr::sdk_sessions* sessions = live_sessions();
    if (sessions != 0) {
        sessions->search_find(Handle, Options, ClientData, CompletionDelegate);
    }
}

EOS_DECLARE_FUNC(uint32_t) EOS_SessionSearch_GetSearchResultCount(EOS_HSessionSearch Handle,
                                                                  const EOS_SessionSearch_GetSearchResultCountOptions* Options) {
    eosr::trace_scope eosr_trace(eosr::global_tracer(),
                                 "EOS_SessionSearch_GetSearchResultCount",
                                 api_version(Options), eosr::call_mode::sync);
    eosr::sdk_sessions* sessions = live_sessions();
    const u32 result = (sessions != 0) ? sessions->search_result_count(Handle) : 0;
    return eosr::traced_count(eosr_trace, result);
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_SessionSearch_CopySearchResultByIndex(EOS_HSessionSearch Handle,
                                                                        const EOS_SessionSearch_CopySearchResultByIndexOptions* Options,
                                                                        EOS_HSessionDetails* OutSessionHandle) {
    eosr::tracer& trace = eosr::global_tracer();
    eosr::trace_scope eosr_trace(trace, "EOS_SessionSearch_CopySearchResultByIndex",
                                 api_version(Options), eosr::call_mode::sync);
    eosr::sdk_sessions* sessions = live_sessions();
    if (OutSessionHandle != 0) {
        *OutSessionHandle = 0;
    }
    const EOS_EResult result =
        (sessions != 0) ? sessions->search_copy_result(Handle, Options, OutSessionHandle)
                        : EOS_EResult::EOS_InvalidParameters;
    return traced_out_result(eosr_trace, trace, result,
                             OutSessionHandle != 0 ? *OutSessionHandle : 0,
                             eosr::field_id::session, eosr::label_kind::session);
}

// --- SessionDetails ---

EOS_DECLARE_FUNC(EOS_EResult) EOS_SessionDetails_CopyInfo(EOS_HSessionDetails Handle,
                                                          const EOS_SessionDetails_CopyInfoOptions* Options,
                                                          EOS_SessionDetails_Info** OutSessionInfo) {
    eosr::tracer& trace = eosr::global_tracer();
    eosr::trace_scope eosr_trace(trace, "EOS_SessionDetails_CopyInfo",
                                 api_version(Options), eosr::call_mode::sync);
    eosr::sdk_sessions* sessions = live_sessions();
    if (OutSessionInfo != 0) {
        *OutSessionInfo = 0;
    }
    const EOS_EResult result =
        (sessions != 0) ? sessions->details_copy_info(Handle, OutSessionInfo)
                        : EOS_EResult::EOS_InvalidParameters;
    return traced_out_result(eosr_trace, trace, result,
                             OutSessionInfo != 0 ? *OutSessionInfo : 0,
                             eosr::field_id::handle, eosr::label_kind::handle);
}

EOS_DECLARE_FUNC(uint32_t) EOS_SessionDetails_GetSessionAttributeCount(EOS_HSessionDetails Handle,
                                                                       const EOS_SessionDetails_GetSessionAttributeCountOptions* Options) {
    eosr::trace_scope eosr_trace(eosr::global_tracer(),
                                 "EOS_SessionDetails_GetSessionAttributeCount",
                                 api_version(Options), eosr::call_mode::sync);
    eosr::sdk_sessions* sessions = live_sessions();
    const u32 result = (sessions != 0) ? sessions->details_attribute_count(Handle) : 0;
    return eosr::traced_count(eosr_trace, result);
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_SessionDetails_CopySessionAttributeByIndex(EOS_HSessionDetails Handle,
                                                                             const EOS_SessionDetails_CopySessionAttributeByIndexOptions* Options,
                                                                             EOS_SessionDetails_Attribute** OutSessionAttribute) {
    eosr::tracer& trace = eosr::global_tracer();
    eosr::trace_scope eosr_trace(trace,
                                 "EOS_SessionDetails_CopySessionAttributeByIndex",
                                 api_version(Options), eosr::call_mode::sync);
    eosr::sdk_sessions* sessions = live_sessions();
    if (OutSessionAttribute != 0) {
        *OutSessionAttribute = 0;
    }
    const EOS_EResult result =
        (sessions != 0)
            ? sessions->details_copy_attribute_by_index(Handle, Options, OutSessionAttribute)
            : EOS_EResult::EOS_InvalidParameters;
    return traced_out_result(eosr_trace, trace, result,
                             OutSessionAttribute != 0 ? *OutSessionAttribute : 0,
                             eosr::field_id::handle, eosr::label_kind::handle);
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_SessionDetails_CopySessionAttributeByKey(EOS_HSessionDetails Handle,
                                                                           const EOS_SessionDetails_CopySessionAttributeByKeyOptions* Options,
                                                                           EOS_SessionDetails_Attribute** OutSessionAttribute) {
    eosr::tracer& trace = eosr::global_tracer();
    eosr::trace_scope eosr_trace(trace,
                                 "EOS_SessionDetails_CopySessionAttributeByKey",
                                 api_version(Options), eosr::call_mode::sync);
    eosr::sdk_sessions* sessions = live_sessions();
    if (OutSessionAttribute != 0) {
        *OutSessionAttribute = 0;
    }
    const EOS_EResult result =
        (sessions != 0)
            ? sessions->details_copy_attribute_by_key(Handle, Options, OutSessionAttribute)
            : EOS_EResult::EOS_InvalidParameters;
    return traced_out_result(eosr_trace, trace, result,
                             OutSessionAttribute != 0 ? *OutSessionAttribute : 0,
                             eosr::field_id::handle, eosr::label_kind::handle);
}

// --- ActiveSession ---

EOS_DECLARE_FUNC(EOS_EResult) EOS_ActiveSession_CopyInfo(EOS_HActiveSession Handle,
                                                         const EOS_ActiveSession_CopyInfoOptions* Options,
                                                         EOS_ActiveSession_Info** OutActiveSessionInfo) {
    eosr::tracer& trace = eosr::global_tracer();
    eosr::trace_scope eosr_trace(trace, "EOS_ActiveSession_CopyInfo",
                                 api_version(Options), eosr::call_mode::sync);
    eosr::sdk_sessions* sessions = live_sessions();
    if (OutActiveSessionInfo != 0) {
        *OutActiveSessionInfo = 0;
    }
    const EOS_EResult result =
        (sessions != 0) ? sessions->active_copy_info(Handle, OutActiveSessionInfo)
                        : EOS_EResult::EOS_InvalidParameters;
    return traced_out_result(eosr_trace, trace, result,
                             OutActiveSessionInfo != 0 ? *OutActiveSessionInfo : 0,
                             eosr::field_id::handle, eosr::label_kind::handle);
}

EOS_DECLARE_FUNC(uint32_t) EOS_ActiveSession_GetRegisteredPlayerCount(EOS_HActiveSession Handle,
                                                                      const EOS_ActiveSession_GetRegisteredPlayerCountOptions* Options) {
    eosr::trace_scope eosr_trace(eosr::global_tracer(),
                                 "EOS_ActiveSession_GetRegisteredPlayerCount",
                                 api_version(Options), eosr::call_mode::sync);
    eosr::sdk_sessions* sessions = live_sessions();
    const u32 result = (sessions != 0) ? sessions->active_registered_count(Handle) : 0;
    return eosr::traced_count(eosr_trace, result);
}

EOS_DECLARE_FUNC(EOS_ProductUserId) EOS_ActiveSession_GetRegisteredPlayerByIndex(EOS_HActiveSession Handle,
                                                                                 const EOS_ActiveSession_GetRegisteredPlayerByIndexOptions* Options) {
    eosr::trace_scope eosr_trace(eosr::global_tracer(),
                                 "EOS_ActiveSession_GetRegisteredPlayerByIndex",
                                 api_version(Options), eosr::call_mode::sync);
    eosr::sdk_sessions* sessions = live_sessions();
    const EOS_ProductUserId result =
        (sessions != 0) ? sessions->active_registered_by_index(Handle, Options) : 0;
    return eosr::traced_handle(eosr_trace, result, eosr::label_kind::puid);
}

// --- Release: the handles, and the structs CopyInfo hands out ---

EOS_DECLARE_FUNC(void) EOS_SessionModification_Release(EOS_HSessionModification SessionModificationHandle) {
    eosr::trace_scope eosr_trace(eosr::global_tracer(),
                                 "EOS_SessionModification_Release", 0,
                                 eosr::call_mode::sync);
    eosr::sdk_sessions* sessions = live_sessions();
    if (sessions != 0) {
        sessions->modification_release(SessionModificationHandle);
    }
}

EOS_DECLARE_FUNC(void) EOS_SessionSearch_Release(EOS_HSessionSearch SessionSearchHandle) {
    eosr::trace_scope eosr_trace(eosr::global_tracer(), "EOS_SessionSearch_Release", 0,
                                 eosr::call_mode::sync);
    eosr::sdk_sessions* sessions = live_sessions();
    if (sessions != 0) {
        sessions->search_release(SessionSearchHandle);
    }
}

EOS_DECLARE_FUNC(void) EOS_SessionDetails_Release(EOS_HSessionDetails SessionHandle) {
    eosr::trace_scope eosr_trace(eosr::global_tracer(), "EOS_SessionDetails_Release", 0,
                                 eosr::call_mode::sync);
    eosr::sdk_sessions* sessions = live_sessions();
    if (sessions != 0) {
        sessions->details_release(SessionHandle);
    }
}

EOS_DECLARE_FUNC(void) EOS_ActiveSession_Release(EOS_HActiveSession ActiveSessionHandle) {
    eosr::trace_scope eosr_trace(eosr::global_tracer(), "EOS_ActiveSession_Release", 0,
                                 eosr::call_mode::sync);
    eosr::sdk_sessions* sessions = live_sessions();
    if (sessions != 0) {
        sessions->active_release(ActiveSessionHandle);
    }
}

EOS_DECLARE_FUNC(void) EOS_SessionDetails_Info_Release(EOS_SessionDetails_Info* SessionInfo) {
    eosr::trace_scope eosr_trace(eosr::global_tracer(),
                                 "EOS_SessionDetails_Info_Release", 0,
                                 eosr::call_mode::sync);
    eosr::release_session_details_info(SessionInfo);
}

EOS_DECLARE_FUNC(void) EOS_ActiveSession_Info_Release(EOS_ActiveSession_Info* ActiveSessionInfo) {
    eosr::trace_scope eosr_trace(eosr::global_tracer(),
                                 "EOS_ActiveSession_Info_Release", 0,
                                 eosr::call_mode::sync);
    eosr::release_active_session_info(ActiveSessionInfo);
}

EOS_DECLARE_FUNC(void) EOS_SessionDetails_Attribute_Release(EOS_SessionDetails_Attribute* SessionAttribute) {
    eosr::trace_scope eosr_trace(eosr::global_tracer(),
                                 "EOS_SessionDetails_Attribute_Release", 0,
                                 eosr::call_mode::sync);
    eosr::release_session_details_attribute(SessionAttribute);
}

// --- Deferred: the invite path and the overlay hooks it needs ---

EOS_DECLARE_FUNC(void) EOS_Sessions_SendInvite(EOS_HSessions Handle, const EOS_Sessions_SendInviteOptions* Options,
                                              void* ClientData, const EOS_Sessions_OnSendInviteCallback CompletionDelegate) {
    eosr::trace_scope eosr_trace(eosr::global_tracer(), "EOS_Sessions_SendInvite",
                                 api_version(Options), eosr::call_mode::async);
    stub_async(Handle, ClientData, CompletionDelegate,
               sizeof(EOS_Sessions_SendInviteCallbackInfo));
}

EOS_DECLARE_FUNC(void) EOS_Sessions_RejectInvite(EOS_HSessions Handle, const EOS_Sessions_RejectInviteOptions* Options,
                                              void* ClientData, const EOS_Sessions_OnRejectInviteCallback CompletionDelegate) {
    eosr::trace_scope eosr_trace(eosr::global_tracer(), "EOS_Sessions_RejectInvite",
                                 api_version(Options), eosr::call_mode::async);
    stub_async(Handle, ClientData, CompletionDelegate,
               sizeof(EOS_Sessions_RejectInviteCallbackInfo));
}

EOS_DECLARE_FUNC(void) EOS_Sessions_QueryInvites(EOS_HSessions Handle, const EOS_Sessions_QueryInvitesOptions* Options,
                                              void* ClientData, const EOS_Sessions_OnQueryInvitesCallback CompletionDelegate) {
    eosr::trace_scope eosr_trace(eosr::global_tracer(), "EOS_Sessions_QueryInvites",
                                 api_version(Options), eosr::call_mode::async);
    stub_async(Handle, ClientData, CompletionDelegate,
               sizeof(EOS_Sessions_QueryInvitesCallbackInfo));
}

EOS_DECLARE_FUNC(uint32_t) EOS_Sessions_GetInviteCount(EOS_HSessions Handle, const EOS_Sessions_GetInviteCountOptions* Options) {
    eosr::trace_scope eosr_trace(eosr::global_tracer(), "EOS_Sessions_GetInviteCount",
                                 api_version(Options), eosr::call_mode::sync);
    (void)checked_sessions(Handle);
    return eosr::traced_count(eosr_trace, static_cast<u32>(0));
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_Sessions_GetInviteIdByIndex(EOS_HSessions Handle, const EOS_Sessions_GetInviteIdByIndexOptions* Options,
                                                              char* OutBuffer, int32_t* InOutBufferLength) {
    eosr::trace_scope eosr_trace(eosr::global_tracer(),
                                 "EOS_Sessions_GetInviteIdByIndex",
                                 api_version(Options), eosr::call_mode::sync);
    (void)OutBuffer;
    const EOS_EResult result =
        (checked_sessions(Handle) != 0 && Options != 0 && InOutBufferLength != 0)
            ? EOS_EResult::EOS_NotFound : EOS_EResult::EOS_InvalidParameters;
    return eosr::traced_result(eosr_trace, result);
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_Sessions_CopySessionHandleByInviteId(EOS_HSessions Handle, const EOS_Sessions_CopySessionHandleByInviteIdOptions* Options,
                                                                       EOS_HSessionDetails* OutSessionHandle) {
    eosr::trace_scope eosr_trace(eosr::global_tracer(),
                                 "EOS_Sessions_CopySessionHandleByInviteId",
                                 api_version(Options), eosr::call_mode::sync);
    if (OutSessionHandle != 0) {
        *OutSessionHandle = 0;
    }
    const EOS_EResult result =
        (checked_sessions(Handle) != 0 && Options != 0 && OutSessionHandle != 0)
            ? EOS_EResult::EOS_NotFound : EOS_EResult::EOS_InvalidParameters;
    return eosr::traced_result(eosr_trace, result);
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_Sessions_CopySessionHandleByUiEventId(EOS_HSessions Handle, const EOS_Sessions_CopySessionHandleByUiEventIdOptions* Options,
                                                                        EOS_HSessionDetails* OutSessionHandle) {
    eosr::trace_scope eosr_trace(eosr::global_tracer(),
                                 "EOS_Sessions_CopySessionHandleByUiEventId",
                                 api_version(Options), eosr::call_mode::sync);
    if (OutSessionHandle != 0) {
        *OutSessionHandle = 0;
    }
    const EOS_EResult result =
        (checked_sessions(Handle) != 0 && Options != 0 && OutSessionHandle != 0)
            ? EOS_EResult::EOS_NotFound : EOS_EResult::EOS_InvalidParameters;
    return eosr::traced_result(eosr_trace, result);
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_Sessions_CopySessionHandleForPresence(EOS_HSessions Handle, const EOS_Sessions_CopySessionHandleForPresenceOptions* Options,
                                                                        EOS_HSessionDetails* OutSessionHandle) {
    eosr::trace_scope eosr_trace(eosr::global_tracer(),
                                 "EOS_Sessions_CopySessionHandleForPresence",
                                 api_version(Options), eosr::call_mode::sync);
    if (OutSessionHandle != 0) {
        *OutSessionHandle = 0;
    }
    const EOS_EResult result =
        (checked_sessions(Handle) != 0 && Options != 0 && OutSessionHandle != 0)
            ? EOS_EResult::EOS_NotFound : EOS_EResult::EOS_InvalidParameters;
    return eosr::traced_result(eosr_trace, result);
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_SessionModification_SetAllowedPlatformIds(EOS_HSessionModification Handle,
                                                                            const EOS_SessionModification_SetAllowedPlatformIdsOptions* Options) {
    eosr::trace_scope eosr_trace(eosr::global_tracer(),
                                 "EOS_SessionModification_SetAllowedPlatformIds",
                                 api_version(Options), eosr::call_mode::sync);
    eosr::sdk_sessions* sessions = live_sessions();
    const EOS_EResult result =
        (sessions != 0) ? sessions->modification_set_allowed_platform_ids(Handle, Options)
                        : EOS_EResult::EOS_InvalidParameters;
    return eosr::traced_result(eosr_trace, result);
}

EOS_DECLARE_FUNC(EOS_NotificationId) EOS_Sessions_AddNotifySessionInviteReceived(EOS_HSessions Handle, const EOS_Sessions_AddNotifySessionInviteReceivedOptions* Options,
                                                                       void* ClientData, const EOS_Sessions_OnSessionInviteReceivedCallback Handler) {
    eosr::trace_scope eosr_trace(eosr::global_tracer(),
                                 "EOS_Sessions_AddNotifySessionInviteReceived",
                                 api_version(Options), eosr::call_mode::sync);
    const EOS_NotificationId result = stub_notify(
        Handle, ClientData, Handler, sizeof(EOS_Sessions_SessionInviteReceivedCallbackInfo),
        "SessionInviteReceived");
    return eosr::traced_notification(eosr_trace, result);
}

EOS_DECLARE_FUNC(void) EOS_Sessions_RemoveNotifySessionInviteReceived(EOS_HSessions Handle, EOS_NotificationId InId) {
    eosr::trace_scope eosr_trace(eosr::global_tracer(),
                                 "EOS_Sessions_RemoveNotifySessionInviteReceived", 0,
                                 eosr::call_mode::sync);
    eosr::sdk_sessions* sessions = checked_sessions(Handle);
    if (sessions != 0) {
        sessions->remove_notification(InId);
    }
}

EOS_DECLARE_FUNC(EOS_NotificationId) EOS_Sessions_AddNotifySessionInviteAccepted(EOS_HSessions Handle, const EOS_Sessions_AddNotifySessionInviteAcceptedOptions* Options,
                                                                       void* ClientData, const EOS_Sessions_OnSessionInviteAcceptedCallback Handler) {
    eosr::trace_scope eosr_trace(eosr::global_tracer(),
                                 "EOS_Sessions_AddNotifySessionInviteAccepted",
                                 api_version(Options), eosr::call_mode::sync);
    const EOS_NotificationId result = stub_notify(
        Handle, ClientData, Handler, sizeof(EOS_Sessions_SessionInviteAcceptedCallbackInfo),
        "SessionInviteAccepted");
    return eosr::traced_notification(eosr_trace, result);
}

EOS_DECLARE_FUNC(void) EOS_Sessions_RemoveNotifySessionInviteAccepted(EOS_HSessions Handle, EOS_NotificationId InId) {
    eosr::trace_scope eosr_trace(eosr::global_tracer(),
                                 "EOS_Sessions_RemoveNotifySessionInviteAccepted", 0,
                                 eosr::call_mode::sync);
    eosr::sdk_sessions* sessions = checked_sessions(Handle);
    if (sessions != 0) {
        sessions->remove_notification(InId);
    }
}

EOS_DECLARE_FUNC(EOS_NotificationId) EOS_Sessions_AddNotifySessionInviteRejected(EOS_HSessions Handle, const EOS_Sessions_AddNotifySessionInviteRejectedOptions* Options,
                                                                       void* ClientData, const EOS_Sessions_OnSessionInviteRejectedCallback Handler) {
    eosr::trace_scope eosr_trace(eosr::global_tracer(),
                                 "EOS_Sessions_AddNotifySessionInviteRejected",
                                 api_version(Options), eosr::call_mode::sync);
    const EOS_NotificationId result = stub_notify(
        Handle, ClientData, Handler, sizeof(EOS_Sessions_SessionInviteRejectedCallbackInfo),
        "SessionInviteRejected");
    return eosr::traced_notification(eosr_trace, result);
}

EOS_DECLARE_FUNC(void) EOS_Sessions_RemoveNotifySessionInviteRejected(EOS_HSessions Handle, EOS_NotificationId InId) {
    eosr::trace_scope eosr_trace(eosr::global_tracer(),
                                 "EOS_Sessions_RemoveNotifySessionInviteRejected", 0,
                                 eosr::call_mode::sync);
    eosr::sdk_sessions* sessions = checked_sessions(Handle);
    if (sessions != 0) {
        sessions->remove_notification(InId);
    }
}

EOS_DECLARE_FUNC(EOS_NotificationId) EOS_Sessions_AddNotifyJoinSessionAccepted(EOS_HSessions Handle, const EOS_Sessions_AddNotifyJoinSessionAcceptedOptions* Options,
                                                                       void* ClientData, const EOS_Sessions_OnJoinSessionAcceptedCallback Handler) {
    eosr::trace_scope eosr_trace(eosr::global_tracer(),
                                 "EOS_Sessions_AddNotifyJoinSessionAccepted",
                                 api_version(Options), eosr::call_mode::sync);
    const EOS_NotificationId result = stub_notify(
        Handle, ClientData, Handler, sizeof(EOS_Sessions_JoinSessionAcceptedCallbackInfo),
        "JoinSessionAccepted");
    return eosr::traced_notification(eosr_trace, result);
}

EOS_DECLARE_FUNC(void) EOS_Sessions_RemoveNotifyJoinSessionAccepted(EOS_HSessions Handle, EOS_NotificationId InId) {
    eosr::trace_scope eosr_trace(eosr::global_tracer(),
                                 "EOS_Sessions_RemoveNotifyJoinSessionAccepted", 0,
                                 eosr::call_mode::sync);
    eosr::sdk_sessions* sessions = checked_sessions(Handle);
    if (sessions != 0) {
        sessions->remove_notification(InId);
    }
}

EOS_DECLARE_FUNC(EOS_NotificationId) EOS_Sessions_AddNotifyLeaveSessionRequested(EOS_HSessions Handle, const EOS_Sessions_AddNotifyLeaveSessionRequestedOptions* Options,
                                                                       void* ClientData, const EOS_Sessions_OnLeaveSessionRequestedCallback Handler) {
    eosr::trace_scope eosr_trace(eosr::global_tracer(),
                                 "EOS_Sessions_AddNotifyLeaveSessionRequested",
                                 api_version(Options), eosr::call_mode::sync);
    const EOS_NotificationId result = stub_notify(
        Handle, ClientData, Handler, sizeof(EOS_Sessions_LeaveSessionRequestedCallbackInfo),
        "LeaveSessionRequested");
    return eosr::traced_notification(eosr_trace, result);
}

EOS_DECLARE_FUNC(void) EOS_Sessions_RemoveNotifyLeaveSessionRequested(EOS_HSessions Handle, EOS_NotificationId InId) {
    eosr::trace_scope eosr_trace(eosr::global_tracer(),
                                 "EOS_Sessions_RemoveNotifyLeaveSessionRequested", 0,
                                 eosr::call_mode::sync);
    eosr::sdk_sessions* sessions = checked_sessions(Handle);
    if (sessions != 0) {
        sessions->remove_notification(InId);
    }
}

EOS_DECLARE_FUNC(EOS_NotificationId) EOS_Sessions_AddNotifySendSessionNativeInviteRequested(EOS_HSessions Handle, const EOS_Sessions_AddNotifySendSessionNativeInviteRequestedOptions* Options,
                                                                       void* ClientData, const EOS_Sessions_OnSendSessionNativeInviteRequestedCallback Handler) {
    eosr::trace_scope eosr_trace(
        eosr::global_tracer(), "EOS_Sessions_AddNotifySendSessionNativeInviteRequested",
        api_version(Options), eosr::call_mode::sync);
    const EOS_NotificationId result = stub_notify(
        Handle, ClientData, Handler,
        sizeof(EOS_Sessions_SendSessionNativeInviteRequestedCallbackInfo),
        "SendSessionNativeInviteRequested");
    return eosr::traced_notification(eosr_trace, result);
}

EOS_DECLARE_FUNC(void) EOS_Sessions_RemoveNotifySendSessionNativeInviteRequested(EOS_HSessions Handle, EOS_NotificationId InId) {
    eosr::trace_scope eosr_trace(
        eosr::global_tracer(), "EOS_Sessions_RemoveNotifySendSessionNativeInviteRequested", 0,
        eosr::call_mode::sync);
    eosr::sdk_sessions* sessions = checked_sessions(Handle);
    if (sessions != 0) {
        sessions->remove_notification(InId);
    }
}
