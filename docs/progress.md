# Reverse-Engineering Progress Manifest

Target: `EOSSDK-Win64-Shipping.dll` (nemirtingas_epic_emu, ~EOS SDK 1.17.1).
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

## Milestone: all 42 classes across all 6 phases labeled + documented (2026-07-10).
- **Follow-ups resolved (2026-07-10):** full newer-build wire protocol recovered from embedded protobuf descriptors → `protocol.md` "RECOVERED newer-build schema". Confirmed UserInfo/Friends have NO network path (username via `EpicEmu_Infos_pb`); documented new `EpicAuth_pb`, `EpicCustomInvites_pb{payload}`, `EpicEmu_pb` handshake, `EpicOverlay_pb`; new fields (Presence `integratedplatform`, Lobby `rtc_room_name`, Sessions `owner_server_id`, search `app_id`).
