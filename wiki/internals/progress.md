# Reverse-Engineering Progress Manifest

Target: `EOSSDK-Win64-Shipping.dll` (the reference EOS emulator, ~EOS SDK 1.17.1).
Legend — **Named**: impl methods auto-named from `__func__`. **Typed**: flat API prototyped + impl struct recovered. **Doc**: `<module>.md` written & cross-checked.

## Foundation (done)
- [x] EOS SDK 1.19 types imported (all interfaces) → Ghidra category `/eos_full.h`
- [x] 628/630 flat API functions prototyped
- [x] 705 impl methods auto-named across 42 classes (`NameImplMethodsFromFunc.py`)
- [x] `architecture.md`, `protocol.md` written from source mapping
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
| EOSSDK_UI | 27 | B | ✅ | ◑ | ✅ (integratedplatform-ui-overlay.md) |
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

## Foundation milestone: FROZEN (2026-07-13)
All planned foundation interfaces are implemented over the peer mesh, each adversarially reviewed and hardened, and the mesh identity is bound to the connection. This is the baseline the Authenticated Mesh Identity milestone (see below) builds on; treat it as stable.
- **Interfaces live:** Connect, Auth, P2P, Sessions, Presence, Lobby (+ Platform/Client bootstrap). Every other `EOS_Platform_Get<X>Interface` returns a non-null stub. *(Since the freeze: UI and IntegratedPlatform are real objects too — see the compatibility-shell note below.)*
- **276 EOS_* exports**, 0 non-EOS symbols exported; builds both targets (Linux `.so` + Windows `.dll` via MinGW/Wine).
- **Tests:** 204 unit cases / 11.3k+ assertions, integration (loads the real built lib, incl. ABI out-param contracts), export-surface, and real-mesh e2e (2- and 3-instance) — green on both targets, clean under ASan + UBSan.
- **Review discipline:** each interface got adversarial review(s) (verify against headers + 2020 source, push back, fix with regression tests). Serious issues found and fixed: P2P delivery/validation; networked-discovery identity; Sessions forged-verdict + reserve-amplification + search-drop; Presence forge/overwrite + silent-cap; Lobby (round 1) Find-callback heap-overflow + lobby-state hijack + JoinById-timeout; Lobby (round 2) host migration now implemented (deterministic heir election on owner leave/disconnect when enabled), search results bound to the responding host, join-by-id made opt-in end to end, member notifications completed (MemberUpdate on attribute change, MemberStatus JOINED/LEFT/PROMOTED on roster diff, KICKED vs CLOSED distinguished on the wire), flat out-handles nulled on bad parent handle, search option validation (max-results cap, version codes, mutually-exclusive Find modes); plus the connection-bound-identity hardening.
- **Superseded by:** the Authenticated Mesh Identity milestone, now **complete** (see below).

## Authenticated Mesh Identity milestone: COMPLETE (2026-07-13)
Self-certifying, key-derived identities over a standards-exact Noise XX channel. Spec: [`adr/0001`](adr/0001-authenticated-mesh-identity.md). This closes both gaps the foundation left open, and makes `EOS_EPacketReliability` mean something.
- **A profile is a key.** An identity is an X25519 keypair; the ids are derived from the public half and the private half proves them. The username is now only a display name — it used to seed the identity, which meant anyone who typed a name answered to that player's id.
- **A peer proves who it is before it is anyone.** Every connection runs Noise XX before it becomes a peer, and is adopted under the id *recomputed* from the key it proved. There is no field in which a peer says who it is, so first contact is no longer self-asserted.
- **The Epic-account id took a second pass.** The line above originally claimed it too, and it was not true: Presence still read the epic id out of the *payload* and bound it first-writer-wins, so a peer could still claim any unclaimed account. The router now hands each peer's key-derived epic id to the interfaces on `peer_connected`, and a presence naming any other account is refused — no TOFU, no race to win. Connect's roster had the same shape of hole: it keyed on the product user id *inside* the message rather than the one the connection proved, so a peer could write another player's roster entry, which is that player's display name as everyone sees it.
- **Every mesh frame is sealed** under that key with a counter for a nonce: a forged, tampered, replayed, reordered, or truncated frame does not open, and the connection ends rather than guess. The title and wire version are bound into the transcript, so another game — or another version — cannot complete a handshake at all, which is what refusing a v1 peer amounts to.
- **P2P gameplay data goes over authenticated UDP.** An unreliable packet is sealed under keys derived from the handshake's secret chaining key and sent as a datagram; a reliable one takes the mesh. Sequence-as-nonce plus a sliding replay window; the window is only spent on a datagram that authenticated. Packets used to ride the reliable mesh whatever the game asked for, so one lost segment held up every packet behind it — a game replicating movement feels that.
- **Two local copies are two players.** Each takes an exclusive profile slot, as each already takes a discovery slot: sharing one profile would give them one id and couch co-op would never mesh. `EOSR_DATA_DIR` lets a launcher provision them instead.
- **Dropped from the ADR, with reasons recorded in it:** legacy/TOFU mode (§9 — nothing was ever persisted, so there is no legacy profile in the world to migrate, and the mode would only have kept an unauthenticated acceptance path alive) and the `session_generation` counter (§7 — every handshake mixes in a fresh ephemeral, so the chaining key is already unique per session).
- **Tests:** 255 unit cases / 11.7k+ assertions, green on Linux and Windows/Wine, clean under ASan + UBSan. The mesh e2e now runs entirely over the authenticated channel; a separate e2e watches the transport counters to prove an unreliable packet really does take the datagram path and a reliable one really does not.

## Compatibility shell (2026-07-13)
A game resolves every `EOS_*` symbol it imports when the library loads; one it cannot find kills the process in the loader, before a line of the SDK runs and without saying why. So the surface comes before the substance.
- **Common helpers** (`c4646fc`): `EOS_GetVersion`, `EOS_EResult_IsOperationComplete`, `EOS_ByteArray_ToString`, `EOS_ContinuanceToken_ToString`, the two status `ToString`s, and eleven Platform status/country/locale/crossplay calls.
- **UI** (`911e2bc`, hardened in `03c322c`): all 27 exports, headless. Nothing is ever shown, so the overlay is never visible and never takes exclusive input — a game told it opened would wait forever for a player to close it. Settings round-trip; the two notifications deliver the current state on the next tick, as the header requires, and never again; `ReportInputState`/`PrePresent` are console-only and return `NotImplemented`; `ShowBlockPlayer`/`ShowReportPlayer`/`ShowNativeProfile` return `NotConfigured` (the labeled reference does too — they describe a flow the *player* completes, and Success would claim they completed it); `AcknowledgeEventId` knows no events until the companion mints them.
- **IntegratedPlatform** (`bd4cc3f`): all 9 exports. The options container is a real, process-global owned object — a game builds it *before* the platform, creates the platform from it, and releases it while the platform runs, so creation copies rather than holds. Anything that would have to come *from* Steam or a console never happens and says so (`NotConfigured`, `InvalidUser`), but `SetUserLoginStatus` runs the other way — the application telling the SDK a platform user's status — and that is honoured properly, with the change notification firing on the tick.
- **Lifecycle hardening** (`bbd4f9b`): three ways a handle or registration could outlive what it named, each a crash or wrong callback rather than a wrong value. The container handle was the container's own address, so a released one could alias the next `Create`'s slot — handles are process-unique tokens now (`handle_store`), like every other sub-handle. The container mutex guarded the map but not the *use*: the lookup returned a raw pointer dereferenced with the lock dropped, so platform creation could read entries a concurrent `Release` freed — now add-entry and copy-entries do their work under the lock and copy out, and no pointer escapes. The login-status notification wrote its payload back to null *after* firing, into storage a self-removing callback had already freed (ASan use-after-free) — the write is gone. And the UI's promised initial callback was an independent one-shot, so `RemoveNotify` before the tick cancelled the registration but not the pending call, firing through freed client data — it is tied to the notification id now and fires only if the registration is still live.
- **Friends + UserInfo** (this commit): 27 exports (Friends 13, UserInfo 14), both derived from the authenticated peer mesh, neither with a network path of its own. The friends list *is* the roster: every meshed peer is a friend under the key-derived Epic account id the rest of the SDK already trusts, appearing on `peer_connected` and leaving on `peer_disconnected`, with the FriendsUpdate notification firing that transition on the tick. An invite to a meshed peer succeeds with nothing to send (they are already a friend), but an invite to an account we have never met is `NotFound` rather than a false "sent"; the blocked list is always empty and its notification never fires. UserInfo caches one datum per peer — the display name — resolved through Connect's roster (keyed on the connection-proven product user id, so a peer cannot forge a name); our own entry is seeded from settings (username, country, locale). `QueryUserInfo` settles from cache (Success/NotFound, never a hang); external accounts do not exist here (`GetExternalUserInfoCount` = 0, Copy* = NotFound, and a null out-pointer is `InvalidParameters` per the ABI); `CopyBestDisplayNameWithPlatform` for a non-Epic platform is `BestDisplayNameIndeterminate` — we have no linked account and will not relabel the Epic name as Steam; the Copy* calls report `IncompatibleVersion` distinctly from `InvalidParameters`; `GetLocalPlatformType` = `EOS_OPT_Epic` (our only identity — to be re-checked against the reference/alpha trace). Copy-outs use the Presence holder-map idiom with matching `EOS_UserInfo_Release` / `EOS_UserInfo_BestDisplayName_Release`.
- **Exports: 276 → 356.** Still missing: Ecom, RTC*, AntiCheat*, Achievements, Stats, Leaderboards, storage, and the rest.

## Reimplementation (the from-scratch build under src/)
- **Sessions** (2026-07-13): implemented over the mesh. One roster per session (leave frees the seat); active-session handle is a live view, not a snapshot. Only a session's host is surfaced as a search result — a peer answers a search, and seeds its own local results, only for sessions it hosts, so a member never echoes back a session it merely joined. A search response is taken only from a peer we actually asked, and every returned session is re-matched against our own query, so a peer cannot plant an unrequested or non-matching result. The roster is the host's to manage: an inbound register/unregister is honoured from the owner for anyone, and from a participant only to remove itself — so a participant cannot evict the host, drop others, or pad past capacity. Inbound `session_join_response` is accepted only from the owner and only while a join we started is pending. Register/Unregister callbacks report the players they changed. Wire list lengths are bounded to the ABI maxima before reserve.
- **Presence** (2026-07-13): implemented over the mesh, keyed by Epic account id. Own presence seeded at init and broadcast on change and on peer-connect; a query for a known account resolves from cache, otherwise the account's owner answers. An inbound presence is validated before it is cached or bound to an owner, so a malformed announcement leaves no trace. `GetJoinInfo` honours the header's LimitExceeded buffer contract.
- **Lobby** (2026-07-13): the last foundation interface, implemented over the mesh. Sessions-shaped, plus members with per-member attributes and an owner with authority the connection-bound identity makes real: only the host promotes, kicks, or admits, and only a member's own attribute/leave changes are accepted from it. The host is the source of truth and broadcasts the whole lobby on any change. Search surfaces only hosted, publicly-advertised lobbies. RTC voice rooms are named (`GetRTCRoomName`) but not carried (`IsRTCRoomConnected` = false); invites and the overlay are deferred (async → NotImplemented, notifications register but never fire), the same split as Sessions. 79 exports.
- **Sub-handles** (`handle_store`): a handle is a process-unique id, freed outright on release, so memory tracks what a game currently holds rather than everything it ever held, while a stale/double/foreign release stays a no-op.
- **Connection-bound identity (2026-07-13):** the mesh now attributes every inbound frame to the id the socket was adopted under, not the `source_id` the sender writes. A peer joins only by a first-frame handshake whose nested id agrees with its envelope and game; a second connection claiming an already-connected id is refused rather than allowed to replace the live one; and a frame for a different game, or addressed to another peer, is dropped before dispatch. This makes **product-user-id authenticated for the duration of a connection** — the owner/participant/awaited checks on Sessions now rest on the connection, so a peer genuinely cannot forge a verdict, hijack a roster, or plant a search result as someone else.
- **What connection-binding did not cover, and what closed it:** first contact was self-asserted, the presence epic-account id travelled in a payload, and P2P packets were unauthenticated. All three are closed by the Authenticated Mesh Identity milestone above. What remains true, permanently: none of this attests a real Epic account or game ownership. That is impossible without Epic, and we do not claim it. What EOS Reimagined authenticates is that a peer holds the key its identity is derived from — which is what stops one player wearing another's identity, and is the whole of what a backendless emulator can honestly offer.
- **Known gaps / deferred (tracked, not bugs):**
  - Thread-safety: the whole engine is single-threaded-tick by design (only the info-struct free registries are mutex-guarded, because those frees are bare C entry points). A game calling an interface off its tick thread is not yet serialized; if we commit to the SDK's any-thread contract it needs one platform-wide lock across all interfaces, not a per-interface patch.
  - The three overlay-originated join notifications have no trigger yet: Presence
    `JoinGameAccepted`, Lobby `JoinLobbyAccepted`, and Sessions `JoinSessionAccepted` register but
    never fire. This is not permanently tied to an injected renderer. The planned external social
    companion supplies the click over a loopback control channel and lets the SDK fire the normal
    callback on the game tick; see [companion-client.md](companion-client.md).

## Long-term milestone: external social companion (planned)

The social overlay is both a renderer and the source of join actions. Rendering inside arbitrary
games remains out of scope, but the action path is feasible without it: a standalone companion can
show authenticated LAN peers and tell the SDK inside the selected game process which candidate the
local player accepted.

- **Local, not injected:** a versioned binary control channel binds only to `127.0.0.1` and is polled
  from `EOS_Platform_Tick`. The SDK, not the companion, owns and fires the game's callback.
- **Presence first:** expose the networked join string and fire
  `EOS_Presence_JoinGameAcceptedCallbackInfo` with the authenticated local/target ids.
- **Real UI events:** add process-unique `EOS_UI_EventId` state and
  `EOS_UI_AcknowledgeEventId`; implement Session/Lobby `Copy*ByUiEventId` over immutable candidate
  snapshots before enabling their accepted notifications.
- **Automation before GUI:** ship a small CLI first so the full path is testable and scriptable. A
  graphical or local-web client is a later shell over the same protocol.
- **Acceptance:** real C-ABI callback-to-copy-to-join-to-ack tests over two and three mesh instances
  on Linux, Windows, Wine, and a native-Linux-companion-to-Wine scenario.
- **Still not promised:** overlay rendering/input, platform-native invitations, commerce/account UI,
  Internet traversal, or compatibility with games that never register a supported join callback.

Detailed design and test matrix: [companion-client.md](companion-client.md).

## Milestone: all 42 classes across all 6 phases labeled + documented (2026-07-10).
- **Follow-ups resolved (2026-07-10):** full newer-build wire protocol recovered from embedded protobuf descriptors → `protocol.md` "RECOVERED newer-build schema". Confirmed UserInfo/Friends have NO network path (username via `EpicEmu_Infos_pb`); documented new `EpicAuth_pb`, `EpicCustomInvites_pb{payload}`, `EpicEmu_pb` handshake, `EpicOverlay_pb`; new fields (Presence `integratedplatform`, Lobby `rtc_room_name`, Sessions `owner_server_id`, search `app_id`).
