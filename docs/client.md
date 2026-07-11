# Module: Bootstrap Infrastructure — `EOSSDK_Client` + `EOSSDK_Platform`

Tier A (2020 source: `eos_client_api.*`, `eossdk_platform.*`, `eos_flat_platform.cpp`). Covers global SDK init, the platform singleton, interface ownership, tick, and the emu's own peer-info handshake. See also `architecture.md` (async engine) and `protocol.md`.

## 1. Identity & handles
- `EOS_HPlatform` == `&EOSSDK_Platform::Inst()` (singleton address). No separate "client" handle — `EOSSDK_Client` is a hidden global.
- Flat init/lifecycle exports: `EOS_Initialize`, `EOS_Shutdown`, `EOS_Platform_Create`, `EOS_Platform_Release`, `EOS_Platform_Tick`, `EOS_Platform_Get<If>Interface` (×~30).
- **Newer-build dispatch:** `EOS_Platform_Get<If>Interface(Handle)` null-checks then tail-jumps through the **platform vtable** (`(*Handle)[slot]`). Confirmed slots: Connect `+0x40`, P2P `+0x90`, Sessions `+0xd0`. (2020 called a non-virtual getter; the newer build virtualized them.)

## 2. `EOSSDK_Client` (global SDK state)
Singleton (`Inst()`), 19 methods recovered. Responsibilities:
- **`Initialize(this, EOS_InitializeOptions* Options)` → `EOS_EResult`** (`0x1800326e0`): guard `_sdk_initialized` (`this+0x1b2`) → `EOS_AlreadyConfigured`; null Options → `EOS_InvalidParameters`; calls **`set_eos_compat(ApiVersion)`** (`0x18001df10`) which uses mini_detour to swap the exported `EOS_Auth_CopyUserAuthToken` between the 3-arg "Old" and 4-arg "New" ABI (fatal if the patch fails — hence the 2 undefined `EOS_Auth_*Old` exports). Version-cascade `switch` (API 4→3→2→1) logs OverrideThreadAffinity / SystemInitializeOptions / Reserved and copies memory funcs + ProductName/Version.
- **`SetupLogs`** (`0x180030340`), **ctor** (`0x180020870`).
- ID interning + validation: `EpicAccountId_IsValid`/`ToString`, `ProductUserId_IsValid`/`ToString` (linear-search the `_epicuserids`/`_productuserids` maps), `get_epicuserid`/`get_productuserid` (lazy `new` of detail objects).
- **Interface factory:** `GetEOSInterface` (`0x180032cb0`), `GetEOSInterfaceFromPlugin` (`0x1800510f0`, **NEW** plugin system), `InitializeClientInterfaces` (`0x1800535a0`) / `InitializeGameServerInterfaces` (client-vs-gameserver split, **NEW** vs 2020).
- **Frame + networking:** `CBRunFrame` (`0x180056430`), `OnNetworkMessage` (`0x180054a30`), `_OnPeerConnect`/`_OnPeerDisconnect`.
- **Emu-info handshake (NEW, no 2020 equivalent) — RESOLVED:** `_SendEmuInfosRequest`, `_OnEmuInfosRequest`, `_SendEmuInfosResponse`, `_OnEmuInfosResponse` exchange `EpicEmu_pb` → `EpicEmu_Infos_pb{emulator (version), appid, country, language, username}`. This is how peers learn each other's **appid** (same-game filtering), locale, and username on connect. Documented in `protocol.md`.

## 3. `EOSSDK_Platform` (interface owner + lifecycle)
Singleton; 11 methods. `EOS_HPlatform` = `&Inst()`.
- **`Init(this, EOS_Platform_Options* Options, ...)`** (`0x18011eed0`, 6.3 KB): `_platform_init` guard (`this+8`); stores `api_version` (`+0x68`), `TickBudgetInMilliseconds` (`+0x1c0`), **`TaskNetworkTimeoutSeconds` (`+0x1c8`, NEW field vs 2020)**; version-cascade `switch` (API 0xe…1) copies product_id/sandbox_id/client_id/client_secret/encryption_key/override country+locale/deployment_id/flags/cache_directory and iterates the IntegratedPlatformOptionsContainer. Then `new`s all interface objects + `Callback_Manager` + `Network`, `setup_myself()` for presence/userinfo.
- **`Tick`**: sets max tick budget, `Callback_Manager::tick()` → `run_frames()` (each `CBRunFrame`, then `Network::CBRunFrame`) → `run_callbacks()`. (See `architecture.md §5`.)
- **`Release`**: deletes interfaces, clears `_platform_init`.
- **Getters** (virtual, one per interface): return the stored `EOSSDK_<If>*` cast to `EOS_H<If>`.

Member layout is **conceptual** for the portable reimpl (exact offsets not needed): api_version, the option strings, `flags`, tick budget, task-network-timeout, `Callback_Manager*`, `Network*`, and one pointer per interface (16 in 2020 + the 13 newer ones: rtc*, anticheat*, sanctions, reports, mods, kws, custominvites, integratedplatform(+container), progressionsnapshot).

## 4. Lifecycle & ownership
`EOS_Initialize` → `EOSSDK_Client::Initialize` (global state only, no interfaces). `EOS_Platform_Create` → `EOSSDK_Platform::Init` (creates `Callback_Manager`, `Network`, and every interface object; seeds self presence/userinfo). Interfaces live for the platform's lifetime; `EOS_Platform_Release` tears them down. The game must call `EOS_Platform_Tick` regularly — it is the sole driver of async callback delivery and network dispatch.

## 5–9. (Sync/async/notifications/network/frame)
Infra classes have no EOS async API of their own; they host the machinery documented in `architecture.md`. `EOSSDK_Client::CBRunFrame` participates in the frame loop (emu-info housekeeping); `OnNetworkMessage` is the top-level inbound entry that routes to per-interface handlers.

## 10. RE cross-reference & reimplementation notes
- **Flat→impl map (Platform):** each `EOS_Platform_Get<If>Interface` → platform-vtable slot → `EOSSDK_Platform::Get<If>Interface` → returns interface member. (Slots to be dumped by `WireFlatToImpl.py`.)
- **`set_eos_compat` / mini_detour** is emu-runtime plumbing; the portable library can instead expose both `EOS_Auth_CopyUserAuthToken` ABIs directly (no self-patching) or pick by `ApiVersion` at the flat layer.
- **Reimpl shape:** a `Platform` object owning a `CallbackManager`, a `Network`, and one object per interface; a `Client`/global holding allocators + id-interning maps + product info. Tick pumps callbacks + network.
- **Deltas vs 2020:** virtualized interface getters; `TaskNetworkTimeoutSeconds`; client-vs-gameserver interface split; plugin interface source (`GetEOSInterfaceFromPlugin`); emu-info handshake; the 13 added interfaces.

### Follow-ups (open)
- Read `_SendEmuInfosRequest`/`_On*` bodies → document the emu-info wire message in `protocol.md`.
- Dump the platform vtable to complete the flat↔impl getter map.
- Confirm `InitializeClientInterfaces` vs `InitializeGameServerInterfaces` interface sets (which interfaces exist in gameserver mode).
