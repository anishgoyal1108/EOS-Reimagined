# Module: `EOSSDK_Lobby` (+ Search / Details / Modification)

Tier A (2020 `eossdk_lobby*`), **extended with RTC voice rooms**. Structural mirror of Sessions but with members + owner + RTC. 73 methods (largest interface) + 3 sub-handles.

## 1. Identity & handle
`EOS_HLobby` via `EOS_Platform_GetLobbyInterface`. Sub-handles `EOS_HLobbyModification`, `EOS_HLobbySearch`, `EOS_HLobbyDetails`.

## 2. Class shape & base mixins
`IRunCallback` + `IRunNetwork` (`OnNetworkMessage` @`0x180102cd0`) + `OnUserEvent` @`0x1801027f0`.

## 3. Member state
- `_lobbies`: lobby_id → `Lobby_Infos_pb{lobby_id,max_lobby_member,permission_level,attributes,owner_id, map<string,Lobby_Member_Infos_pb> members}`.
- `_lobbies_searchs`, `_lobby_invites`, `_joins_requests` (pending joins). `EmuInit` @`0x1800dcd10` / `EmuDeinit` @`0x1800e8340`.

## 4. Lifecycle
`EmuInit` registers callbacks + frame + network listeners (`kLobby`, `kLobbiesSearch`) + Connect peer-event hook.

## 5. Handle-producing / sync
`CreateLobby` @`0x1800dd9f0`, `UpdateLobbyModification` @`0x1800e03b0`, `CreateLobbySearch` @`0x1800e3da0`, `CopyLobbyDetailsHandle`/`ByInviteId`/`ByUiEventId` (@`0x1800e5500`/`0x1800e5090`/`0x1800e5460`), `GetInviteCount`/`GetInviteIdByIndex`, `GetConnectString` @`0x1800e6ba0` / `ParseConnectString` @`0x1800e6dd0` (connect-string round-trip for invites).

## 6. Async operations
`CreateLobby`, `DestroyLobby` @`0x1800de4c0`, `JoinLobby` @`0x1800deb90`, `JoinLobbyById` @`0x1800df3f0` (NEW), `LeaveLobby` @`0x1800dfce0`, `UpdateLobby` @`0x1800e0780`, `PromoteMember` @`0x1800e0eb0`, `KickMember` @`0x1800e1550`, `HardMuteMember` @`0x1800e1b90` (NEW, RTC), `SendInvite` @`0x1800e2b50`, `RejectInvite` @`0x1800e3440`, `QueryInvites` @`0x1800e3910`.

### RTC voice-room integration (NEW vs 2020)
`JoinRTCRoom` @`0x1800e5bf0`, `LeaveRTCRoom` @`0x1800e6100`, `GetRTCRoomName` @`0x1800e58b0`, `IsRTCRoomConnected` @`0x1800e6600`, `AddNotifyRTCRoomConnectionChanged` @`0x1800e6850`. Lobbies can carry a bundled RTC voice room — ties into the RTC interfaces (see `rtc.md`). Reimpl: optional; can stub RTC-connected=false if voice unimplemented.

## 7. Notifications
`AddNotifyLobbyUpdateReceived` (@`0x1800e2160`), `LobbyMemberUpdateReceived` (@`0x1800e24b0`), `LobbyMemberStatusReceived` (@`0x1800e2800`), `LobbyInviteReceived/Accepted/Rejected`, `JoinLobbyAccepted`, `LeaveLobbyRequested`, `RTCRoomConnectionChanged`, `SendLobbyNativeInviteRequested` + removers.

## 8. Network protocol
Envelope `kLobby` + `kLobbiesSearch`. Sub `Lobby_Message_pb{ lobby_update | lobby_join_request | lobby_join_response | lobby_invite | member_update | member_join | member_leave | member_promote }`.
- Handlers: `_OnLobbyUpdate` @`0x1801033e0`, `_OnLobbyJoinRequest` @`0x1801036d0`, `_OnLobbyJoinResponse` @`0x180104020`, `_OnLobbyInvite` @`0x180104570`, `_OnLobbyDestroy` @`0x180105000`, `_OnLobbyMemberJoin`/`Leave`/`Update`/`Promote` (@`0x1801058f0`/`0x180105ca0`/`0x180105340`/`0x1800e60d0`... `0x1801060d0`), `_OnLobbiesSearch` @`0x1801063d0`.
- Senders: `_SendLobbyUpdate`, `_SendLobbyJoinRequest/Response`, `_SendLobbyInvite`, `_SendLobbyMemberJoin/Leave/Update/Promote`, `_SendLobbyDestroy`, `_SendLobbiesSearchResponse`, `_SendToAllMembers` @`0x1800ece50`, `_SendToAllMembersOrOwner` @`0x1800eeeb0`.
- Match: `_LobbyMatchFromAttributes` @`0x180108e80` / `_GetLobbiesFromAttributes` @`0x180108b00`.

## 9. Frame/tick
`CBRunFrame` reaps searches, times out pending joins.

## Sub-handles
- **`EOSSDK_LobbyModification`** (10): `SetBucketId`/`SetPermissionLevel`/`SetMaxMembers`/`SetInvitesAllowed`/`SetAllowedPlatformIds`/`AddAttribute`/`RemoveAttribute`/`AddMemberAttribute`/`RemoveMemberAttribute`/`Release`.
- **`EOSSDK_LobbySearch`** (14): `SetParameter`/`RemoveParameter`/`SetMaxResults`/`SetLobbyId`/`SetTargetUserId`/`Find` → `_SendLobbiesSearch` @`0x1800fb300`; inbound `_OnLobbySearchResponse` @`0x1800fe100`; results + `Release`. Own listener (`OnNetworkMessage` @`0x1800fdf40`).
- **`EOSSDK_LobbyDetails`** (12): `CopyInfo`/`GetLobbyOwner`/attribute + member-attribute copies/`GetMemberCount`/`GetMemberByIndex`/`Release`.

## Reimpl notes / follow-ups
- Reimpl: lobby store with members map + owner; member join/leave/promote propagation; owner authority; attribute matching for search. RTC room optional.
- Follow-ups: owner-migration on owner leave; `HardMuteMember`/RTC coupling; connect-string format.
