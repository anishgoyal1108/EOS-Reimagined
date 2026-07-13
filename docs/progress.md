# Reverse-Engineering Progress Manifest

Target: `EOSSDK-Win64-Shipping.dll` (the reference EOS emulator, ~EOS SDK 1.17.1).
Legend — **Named**: impl methods auto-named from `__func__`. **Typed**: flat API prototyped + impl struct recovered. **Doc**: `docs/<module>.md` written & cross-checked.

## Foundation (done)
- [x] EOS SDK 1.19 types imported (all interfaces) → Ghidra category `/eos_full.h`
- [x] 628/630 flat API functions prototyped
- [x] 705 impl methods auto-named across 42 classes (`NameImplMethodsFromFunc.py`)
- [x] `docs/architecture.md`, `docs/protocol.md` written from source mapping
- [x] Windows syslibs + WinHTTP/Winsock detours labeled

## Module status

Tier A = source-assisted (2020 GPL source). Tier B = binary + 1.19 headers only. Sub-handles indented.

| Module | Methods | Tier | Named | Typed | Doc |
|---|---:|---|:--:|:--:|:--:|
| **Phase 0 — Infra** | | | | | |
| EOSSDK_Client | 19 | A | ✅ | ◑ | ✅ |
| EOSSDK_Platform | 11 | A | ✅ | ◑ | ✅ |
| (Callback_Manager) | — | A | | | ✅ (architecture.md) |
| (Network) | — | A | | | ✅ (protocol.md) |
| (FrameResult / Settings) | — | A | | | ✅ (architecture.md) |
| **Phase 1 — Identity** | | | | | |
| EOSSDK_Connect | 32 | A | ✅ | ◑ | ✅ |
| EOSSDK_Auth | 28 | A | ✅ | ◑ | ✅ |
| **Phase 2 — Social/state** | | | | | |
| EOSSDK_Presence | 18 | A | ✅ | ◑ | ✅ |
| &nbsp;&nbsp;EOSSDK_PresenceModification | 6 | A | ✅ | ◑ | ✅ |
| EOSSDK_Friends | 16 | A | ✅ | ◑ | ✅ |
| EOSSDK_UserInfo | 12 | A | ✅ | ◑ | ✅ |
| **Phase 3 — Multiplayer core** | | | | | |
| EOSSDK_P2P | 37 | A | ✅ | ◑ | ✅ |
| EOSSDK_Sessions | 59 | A | ✅ | ◑ | ✅ |
| &nbsp;&nbsp;EOSSDK_SessionSearch | 14 | A | ✅ | ◑ | ✅ |
| &nbsp;&nbsp;EOSSDK_SessionDetails | 5 | A | ✅ | ◑ | ✅ |
| &nbsp;&nbsp;EOSSDK_SessionModification | 10 | A | ✅ | ◑ | ✅ |
| &nbsp;&nbsp;EOSSDK_ActiveSession | 4 | A | ✅ | ◑ | ✅ |
| EOSSDK_Lobby | 73 | A | ✅ | ◑ | ✅ |
| &nbsp;&nbsp;EOSSDK_LobbySearch | 14 | A | ✅ | ◑ | ✅ |
| &nbsp;&nbsp;EOSSDK_LobbyDetails | 12 | A | ✅ | ◑ | ✅ |
| &nbsp;&nbsp;EOSSDK_LobbyModification | 10 | A | ✅ | ◑ | ✅ |
| **Phase 4 — Content/economy** | | | | | |
| EOSSDK_Ecom | 31 | A | ✅ | ◑ | ✅ (ecom.md) |
| EOSSDK_Achievements | 22 | A | ✅ | ◑ | ✅ (achievements-stats-leaderboards.md) |
| EOSSDK_Stats | 6 | A | ✅ | ◑ | ✅ (achievements-stats-leaderboards.md) |
| EOSSDK_Leaderboards | 14 | A | ✅ | ◑ | ✅ (achievements-stats-leaderboards.md) |
| EOSSDK_PlayerDataStorage | 12 | A | ✅ | ◑ | ✅ (storage.md) |
| &nbsp;&nbsp;EOSSDK_PlayerDataStorageFileTransferRequest | 4 | A | ✅ | ◑ | ✅ (storage.md) |
| EOSSDK_TitleStorage | 9 | A | ✅ | ◑ | ✅ (storage.md) |
| &nbsp;&nbsp;EOSSDK_TitleStorageFileTransferRequest | 4 | A | ✅ | ◑ | ✅ (storage.md) |
| EOSSDK_Metrics | 4 | A | ✅ | ◑ | ✅ (metrics.md) |
| **Phase 5 — Tier-B (no source)** | | | | | |
| EOSSDK_RTC | 13 | B | ✅ | ◑ | ✅ (rtc.md) |
| EOSSDK_RTCAudio | 38 | B | ✅ | ◑ | ✅ (rtc.md) |
| EOSSDK_RTCData | 9 | B | ✅ | ◑ | ✅ (rtc.md) |
| EOSSDK_RTCAdmin | 7 | B | ✅ | ◑ | ✅ (rtc.md) |
| EOSSDK_AntiCheatClient | 23 | B | ✅ | ◑ | ✅ (anticheat.md) |
| EOSSDK_AntiCheatServer | 30 | B | ✅ | ◑ | ✅ (anticheat.md) |
| EOSSDK_Sanctions | 6 | B | ✅ | ◑ | ✅ (misc-services.md) |
| EOSSDK_Reports | 3 | B | ✅ | ◑ | ✅ (misc-services.md) |
| EOSSDK_Mods | 7 | B | ✅ | ◑ | ✅ (misc-services.md) |
| EOSSDK_KWS | 12 | B | ✅ | ◑ | ✅ (misc-services.md) |
| EOSSDK_CustomInvites | 27 | B | ✅ | ◑ | ✅ (custominvites.md) |
| EOSSDK_IntegratedPlatform | 8 | B | ✅ | ◑ | ✅ (integratedplatform-ui-overlay.md) |
| &nbsp;&nbsp;EOSSDK_IntegratedPlatformContainer | 4 | B | ✅ | ◑ | ✅ (integratedplatform-ui-overlay.md) |
| EOSSDK_ProgressionSnapshot | 7 | B | ✅ | ◑ | ✅ (misc-services.md) |
| EOSSDK_UI | 26 | B | ✅ | ◑ | ✅ (integratedplatform-ui-overlay.md) |
| (Overlay) | — | B | | | ◑ (integratedplatform-ui-overlay.md; render out of scope) |

Total: **42 classes, 705 methods named.**

## Notes / findings log
- Newer build: per-interface `OnNetworkMessage` (2020 had a single `RunNetwork`).
- Flat trampolines dispatch via vtable slot (2020 was non-virtual direct call).
- `EOSSDK_IntegratedPlatformContainer` + `EOSSDK_IntegratedPlatform` are new (no 2020 source).
- RTCAudio is large (38 methods) — voice capture/render + audio device management.
- **Auth**: real external-ticket login backends `_EpicLogin`/`_SteamLogin`/`_GoGLogin` (parse actual Steam app/session + GoG session tickets; dispatch on EOS_EExternalCredentialType 0/1/5/0x12). 2020 faked login. Single-user ("Multiple login not implemented yet"). `EmuInit`/`EmuDeinit` lifecycle; `*Old` ABI dupes via set_eos_compat.
- **Connect**: roster backbone; `_users` map (index0=myself); `_On/SendConnectRequest/Response(ToAll)` peer-info handshake; cross-interface peer connect/disconnect fan-out; large `CopyIdToken` (JWT).
- Network handler naming refactored: 2020 `on_*`/`send_*` → newer `_On<X>Request`/`_On<X>Response`/`_Send<X>Response`/`_Send<X>ResponseToAll`.
- **Phase 3**: P2P real UDP data path (SendPacket→data_message, _OnP2PData→queue+ack; control via TCP). Lobby gained RTC voice-room integration. Sessions/Lobby search = app-level attribute-match query/response.
- **Phase 4**: all content/economy interfaces LOCAL-ONLY. Ecom fakes ownership (unlock_dlcs). Stats→Achievements bridge (CheckAchievementsStatTriggers). PDS r/w vs TitleStorage read-only, savepath-backed. Leaderboards/Metrics stubbed.
- **Phase 5 (Tier-B) key findings**: AntiCheat `ProtectMessage` = **CRC32-C checksum** (poly 0x82f63b78), NOT encryption; Register* records peers → fires faked auth-success; Server Log* = no-ops. **CustomInvites is genuinely networked** (only Tier-B iface w/ OnNetworkMessage; new protocol msg). RTC room/participant mgmt real but **SendAudio/SendData discard payloads** (voice/data not carried). Sanctions=clean(0), Reports=discard, KWS=permitted/adult, Mods=empty, Metrics=no-op, ProgressionSnapshot=accept. UI/Overlay = social overlay (render out of scope for headless lib).

## Reimplementation (the from-scratch build under src/)
- **Sessions** (2026-07-13): implemented over the mesh. One roster per session (leave frees the seat); active-session handle is a live view, not a snapshot. Only a session's host is surfaced as a search result — a peer answers a search, and seeds its own local results, only for sessions it hosts, so a member never echoes back a session it merely joined. A search response is taken only from a peer we actually asked, and every returned session is re-matched against our own query, so a peer cannot plant an unrequested or non-matching result. The roster is the host's to manage: an inbound register/unregister is honoured from the owner for anyone, and from a participant only to remove itself — so a participant cannot evict the host, drop others, or pad past capacity. Inbound `session_join_response` is accepted only from the owner and only while a join we started is pending. Register/Unregister callbacks report the players they changed. Wire list lengths are bounded to the ABI maxima before reserve.
- **Presence** (2026-07-13): implemented over the mesh, keyed by Epic account id. Own presence seeded at init and broadcast on change and on peer-connect; a query for a known account resolves from cache, otherwise the account's owner answers. An inbound presence is validated before it is cached or bound to an owner, so a malformed announcement leaves no trace. `GetJoinInfo` honours the header's LimitExceeded buffer contract.
- **Sub-handles** (`handle_store`): a handle is a process-unique id, freed outright on release, so memory tracks what a game currently holds rather than everything it ever held, while a stale/double/foreign release stays a no-op.
- **Connection-bound identity (2026-07-13):** the mesh now attributes every inbound frame to the id the socket was adopted under, not the `source_id` the sender writes. A peer joins only by a first-frame handshake whose nested id agrees with its envelope and game; a second connection claiming an already-connected id is refused rather than allowed to replace the live one; and a frame for a different game, or addressed to another peer, is dropped before dispatch. This makes **product-user-id authenticated for the duration of a connection** — the owner/participant/awaited checks on Sessions now rest on the connection, so a peer genuinely cannot forge a verdict, hijack a roster, or plant a search result as someone else.
- **What connection-binding does not yet cover:** *first contact* is still self-asserted (the dialer trusts the advertised id of whoever answers at that address), and a presence **epic-account id travels in the payload**, so a peer can still claim another account's epic id from its own authenticated source (presence first-writer-wins persists for the epic key). Closing both requires self-certifying, key-derived ids — see the Authenticated Mesh Identity milestone. UDP P2P packets are likewise unauthenticated (that path needs per-packet AEAD). None of this attests a real Epic account or game ownership, which is impossible without Epic.
- **Known gaps / deferred (tracked, not bugs):**
  - Thread-safety: the whole engine is single-threaded-tick by design (only the info-struct free registries are mutex-guarded, because those frees are bare C entry points). A game calling an interface off its tick thread is not yet serialized; if we commit to the SDK's any-thread contract it needs one platform-wide lock across all interfaces, not a per-interface patch.
  - Presence `JoinGameAccepted` notification has no trigger without the social overlay (registered, never fires).

## Milestone: all 42 classes across all 6 phases labeled + documented (2026-07-10).
- **Follow-ups resolved (2026-07-10):** full newer-build wire protocol recovered from embedded protobuf descriptors → `protocol.md` "RECOVERED newer-build schema". Confirmed UserInfo/Friends have NO network path (username via `EpicEmu_Infos_pb`); documented new `EpicAuth_pb`, `EpicCustomInvites_pb{payload}`, `EpicEmu_pb` handshake, `EpicOverlay_pb`; new fields (Presence `integratedplatform`, Lobby `rtc_room_name`, Sessions `owner_server_id`, search `app_id`).
