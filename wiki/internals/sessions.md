# Module: `EOSSDK_Sessions` (+ Search / Details / Modification / ActiveSession)

Tier A (2020 `eossdk_sessions*`). Advertised game sessions discovered over the peer mesh via app-level search. 59 methods + 4 sub-handles.

## 1. Identity & handle
`EOS_HSessions` via `EOS_Platform_GetSessionsInterface` (platform vtable +0xd0). Sub-handles: `EOS_HSessionModification`, `EOS_HSessionSearch`, `EOS_HSessionDetails`, `EOS_HActiveSession`.

## 2. Class shape & base mixins
`IRunCallback` + `IRunNetwork` (`OnNetworkMessage` @`0x180175230`, has an explicit `RunCallbacks` @`0x180155ec0`) + `OnUserEvent` @`0x180175090` (peer loss from Connect).

## 3. Member state
- `_sessions`: name → `session_state_t{state: created/joined/joining, Session_Infos_pb}`.
- `_sessions_join`: session_id → pending join `pFrameResult_t` (join timeout ~5 s).
- `_session_invites`: received invites; `_session_searchs`: owned `EOSSDK_SessionSearch*` (reaped in `CBRunFrame`).
- `EmuInit` @`0x1801554c0` / `EmuDeinit` @`0x18015ebc0`.

## 4. Lifecycle
`EmuInit` registers callbacks + frame + network listeners (`kSession`, `kSessionsSearch`) + Connect peer-event hook.

## 5. Synchronous / handle-producing
`CreateSessionModification` @`0x180156490`, `UpdateSessionModification` @`0x180156b40`, `CreateSessionSearch` @`0x18015b450`, `CopyActiveSessionHandle` @`0x18015b5f0`, `CopySessionHandleByInviteId/ByUiEventId/ForPresence` (@`0x18015c910`/`0x18015cb30`/`0x18015cbd0`), `GetInviteCount`/`GetInviteIdByIndex`, `IsUserInSession` @`0x18015cf70`, `DumpSessionState` @`0x18015d300`.

## 6. Async operations
`UpdateSession` @`0x1801571b0` (2.2 KB — create/update + advertise), `DestroySession` @`0x180157c20`, `JoinSession` @`0x180158410`, `StartSession` @`0x180158c10`, `EndSession` @`0x1801591c0`, `RegisterPlayers` @`0x1801596b0`, `UnregisterPlayers` @`0x180159c90`, `SendInvite` @`0x18015a290`, `RejectInvite` @`0x18015a9e0`, `QueryInvites` @`0x18015ad00`. Callback-infos `EOS_Sessions_*CallbackInfo`.

## 7. Notifications
Invite lifecycle: `AddNotifySessionInviteReceived/Accepted/Rejected` (@`0x18015bc00`/`0x18015c280`/`0x18015bf40`), `AddNotifyJoinSessionAccepted` @`0x18015c5d0`, `AddNotifyLeaveSessionRequested` @`0x18015d600`, `AddNotifySendSessionNativeInviteRequested` @`0x18015d940` (NEW — native/overlay invite) + removers.

## 8. Network protocol
Envelope `kSession` + `kSessionsSearch`. Sub `Session_Message_pb{ sessions_request | session_infos | session_destroy | session_join_request | session_join_response | session_invite | session_invite_response | session_register | session_unregister }`; `Session_Infos_pb` (see protocol.md).
- Handlers: `_OnSessionInfoRequest` @`0x180176540`, `_OnSessionInfo` @`0x180176830`, `_OnSessionDestroy` @`0x180176ad0`, `_OnSessionJoinRequest` @`0x180176dd0`, `_OnSessionJoinResponse` @`0x1801779d0`, `_OnSessionInvite` @`0x180178640`, `_OnSessionRegister`/`_OnSessionUnregister`, `_OnSessionsSearch` @`0x180175940`.
- Senders: `_SendSessionInfo`, `_SendSessionDestroy`, `_SendSessionJoinRequest/Response`, `_SendSessionInvite`, `_SendSessionRegister/Unregister`, `_SendSessionsSearchResponse`, `_SendToAllMembers` @`0x180167d40`.
- **Search**: `_OnSessionsSearch` matches local sessions via `_SessionMatchFromAttributes` @`0x180179900` / `_GetSessionsFromAttributes` @`0x18017a090` and replies. Advertise = unicast to members (TCP mesh); discovery = query/response, not broadcast.

## 9. Frame/tick
`CBRunFrame` reaps released searches, times out `_sessions_join`. `RunCallbacks` @`0x180155ec0` (explicit override).

## Sub-handles
- **`EOSSDK_SessionModification`** (10): `SetBucketId`/`SetHostAddress`/`SetMaxPlayers`/`SetPermissionLevel`/`SetJoinInProgressAllowed`/`SetInvitesAllowed`/`SetAllowedPlatformIds`/`AddAttribute`/`RemoveAttribute`/`Release`. Staged mutation applied by `UpdateSession`.
- **`EOSSDK_SessionSearch`** (14): `SetParameter`/`RemoveParameter`/`SetMaxResults`/`SetSessionId`/`SetTargetUserId`/`Find` (async) → `_SendSessionsSearch` @`0x180170040`; inbound `_OnSessionsSearchResponse` @`0x180170a40`; `GetSearchResultCount`/`CopySearchResultByIndex`/`Release`. Own network listener (`OnNetworkMessage` @`0x180170890`).
- **`EOSSDK_SessionDetails`** (5): `CopyInfo`/`GetSessionAttributeCount`/`CopySessionAttributeByIndex`/`ByKey`/`Release`.
- **`EOSSDK_ActiveSession`** (4): `CopyInfo`/`GetRegisteredPlayerCount`/`GetRegisteredPlayerByIndex`/`Release`.

## Reimpl notes / follow-ups
- Reimpl: session store keyed by name; advertise `Session_Infos` to members; answer searches by attribute-matching local sessions; join = request/response with a pending FrameResult (5 s timeout).
- Follow-ups: attribute comparison ops in `_SessionMatchFromAttributes`; `SendSessionNativeInviteRequested` (overlay/native invite integration).
