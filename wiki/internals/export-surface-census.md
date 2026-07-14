# Export-surface census

Our exported EOS surface against the real Epic SDK. Produced by `tools/inspect_game.py`.

- Reference: **EOSSDK-Win64-Shipping.dll v1.17.1.3**, sha256 `2e4814de835eda01...`, **630** `EOS_*` exports.
- Ours: **356** exports.

A game *statically* importing any symbol we do not export **fails at load** -- the process never
starts, and no amount of tracing helps, because our library is never given control.

## The multiplayer core is complete

Every family a LAN co-op session actually needs is at full parity:

`ActiveSession`, `ByteArray`, `Common`, `Connect`, `ContinuanceToken`, `ENetworkStatus`, `EResult`, `EpicAccountId`, `Friends`, `IntegratedPlatform`, `IntegratedPlatformOptionsContainer`, `Lobby`, `LobbyDetails`, `LobbyModification`, `LobbySearch`, `Logging`, `P2P`, `Platform`, `Presence`, `PresenceModification`, `ProductUserId`, `SessionDetails`, `SessionModification`, `SessionSearch`, `Sessions`, `UI`, `UserInfo`

Notably: Connect 28/28, Lobby 46/46, Sessions 33/33, P2P 25/25, Platform 42/42.

## Entirely absent -- 22 families, 278 symbols

| Family | Symbols | Why a game imports it |
|---|---:|---|
| `EOS_Ecom_*` | 39 | DLC / entitlement / ownership checks |
| `EOS_RTCAudio_*` | 38 | voice chat |
| `EOS_AntiCheatServer_*` | 28 | anti-cheat |
| `EOS_AntiCheatClient_*` | 22 | anti-cheat |
| `EOS_CustomInvites_*` | 22 | invite flow |
| `EOS_Achievements_*` | 21 | achievements |
| `EOS_Leaderboards_*` | 16 | leaderboards |
| `EOS_RTC_*` | 13 | voice chat |
| `EOS_KWS_*` | 11 | child-account gating |
| `EOS_PlayerDataStorage_*` | 11 | cloud saves |
| `EOS_TitleStorage_*` | 8 | title-side config/content |
| `EOS_RTCData_*` | 7 | voice chat |
| `EOS_Mods_*` | 6 | mod.io integration |
| `EOS_RTCAdmin_*` | 6 | voice chat |
| `EOS_Stats_*` | 6 | stats backing achievements/leaderboards |
| `EOS_ProgressionSnapshot_*` | 5 | progression sync |
| `EOS_Sanctions_*` | 5 | ban checks |
| `EOS_PlayerDataStorageFileTransferRequest_*` | 4 | cloud-save transfers |
| `EOS_TitleStorageFileTransferRequest_*` | 4 | title-storage transfers |
| `EOS_AccountId_*` | 3 | handle-free id helpers -- **any** game may call these |
| `EOS_Metrics_*` | 2 | UE's plugin calls these on session start/end |
| `EOS_Reports_*` | 1 | player reporting |

## Partial

- `EOS_Auth_*` -- 19/21. Missing: `EOS_Auth_AddNotifyLoginStatusChangedOld`, `EOS_Auth_CopyUserAuthTokenOld`

## What this means

The loader-fatal surface is not one family. It is **278 symbols across 22 families**, and two of
them -- `EOS_AccountId_*` (id helpers) and `EOS_Metrics_*` (which Unreal's EOS plugin calls on
every session) -- are things an ordinary game touches without asking for any of the features
above. Shelling only the family one candidate happens to need would have to be redone for the
next candidate. Every one of these has a vendored header, so an honest `EOS_NotImplemented`
shell is generatable for all of them at once, which retires loader-fatality as a category.
