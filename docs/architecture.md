# Core Runtime Architecture

> Reverse-engineered from `EOSSDK-Win64-Shipping.dll` (nemirtingas_epic_emu, ~EOS SDK 1.17.1), cross-checked against the surviving 2020 GPL source (`~/re-eos-reference/nemirtingas_epic_emu_2020/`). This document is the behavioral spec for the portable FOSS reimplementation.

## 1. Handle model (fundamental)

Every `EOS_H*` handle is a **raw C++ object pointer** reinterpret_cast to the opaque type — no wrapper/indirection:

- `EOS_HPlatform` == `&EOSSDK_Platform::Inst()`
- `EOS_H<If>` == the `EOSSDK_<If>*` member stored in the platform
- Ancillary handles (`EOS_HSessionSearch`, `EOS_HPresenceModification`, …) == heap `EOSSDK_*` sub-objects

Flat C exports are thin trampolines: null-check the handle, cast back to the class, invoke the member. **Newer-build note:** in the 2020 source the member call is *non-virtual*; in this binary the trampoline dispatches through a **vtable slot** (e.g. `EOS_Sessions_CreateSessionModification` → `(*(void***)Handle)[1]`). The flat→impl wiring step resolves the actual slot per function.

## 2. Singletons (Meyers function-local statics)

| Singleton | Role |
|---|---|
| `EOSSDK_Client` | Global SDK state: memory allocators, `_sdk_initialized`, ID interning maps, product name/version, network message entry (`OnNetworkMessage`), peer connect/disconnect, emu-info exchange |
| `EOSSDK_Platform` | Owns all interface objects + `Callback_Manager` + `Network`; bootstrap/tick/release |
| `Settings` | Parsed `NemirtingasEpicEmu.json` |

`EOSSDK_Client` interns `std::map<std::string, EOS_EpicAccountId>` `_epicuserids` and `_productuserids` (lazy `new` of `EOS_EpicAccountIdDetails*`/`EOS_ProductUserIdDetails*`). The `EOS_EpicAccountId_From/IsValid/ToString` flat functions validate by linear-searching these maps (Ghidra tell: `std::map` iteration comparing `.second == AccountId`).

Id detail types: `EOS_EpicAccountIdDetails` / `EOS_ProductUserIdDetails` = `{ mutex; std::string _idstr; bool _valid; }`. Null id = 32-zero hex string (`NULL_USER_ID`).

## 3. Bootstrap

- **`EOS_Initialize`** → fills `EOSSDK_Client` (memory funcs, `ApiVersion`, ProductName/Version via version-cascading `switch` on `API_003→002→001`); if `disable_online_networking` set, installs the WinHTTP/Winsock detours (see `helper_funcs`); may runtime-patch the `EOS_Auth_CopyUserAuthToken`/`AddNotifyLoginStatusChanged` between 3-arg "Old" and 4-arg "New" ABI via mini_detour depending on `ApiVersion==1`. Sets `_sdk_initialized=true`.
- **`EOS_Platform_Create`** → `EOSSDK_Platform::Init(Options)` (version-cascading switch copies platform options into string members), `new`s all interface objects, calls `_presence->setup_myself()` + `_userinfo->setup_myself()`, `_platform_init=true`. Returns `reinterpret_cast<EOS_HPlatform>(&Inst())`.
- **`EOS_Platform_GetXInterface`** → cast platform handle → `EOSSDK_Platform::GetXInterface()` → `reinterpret_cast<EOS_HX>(_x)`. Uniform two-hop; bulk-labelable.

`EOSSDK_Platform` member order (from 2020 `eossdk_platform.h`; verify offsets in binary): api_version, reserved, then strings product_id/sandbox_id/client_id/client_secret, is_server, encryption_key, override_country/locale, deployment_id, flags(u64), cache_directory, ticket_budget_ms, then `Callback_Manager* _cb_manager`, `Network* _network`, then the 16 interface pointers (metrics, auth, connect, ecom, ui, friends, presence, sessions, lobby, userinfo, p2p, playerdatastorage, achievements, stats, titlestorage, leaderboards). **Newer build has more interfaces** (rtc, anticheat, sanctions, reports, mods, kws, custominvites, integratedplatform, progressionsnapshot) — recover the real layout from the binary ctor.

## 4. Interface base classes / vtable

Two pure-virtual mixins are the **only** virtuals on an interface:

- `IRunCallback`: `CBRunFrame()`, `RunCallbacks(pFrameResult_t)`, `FreeCallback(pFrameResult_t)`
- `IRunNetwork`: `RunNetwork(Network_Message_pb const&)` — **newer build:** appears as per-interface `OnNetworkMessage` (each of Auth/Connect/CustomInvites/… has one; the 2020 single `RunNetwork` was refactored).

Networked interfaces inherit both (multiple inheritance → two vptrs, `IRunCallback` first). Non-networked (metrics, ui) inherit only `IRunCallback`. **All public EOS API methods are non-virtual** — reached only via the flat trampolines, so an interface vtable holds only the 3(+1) mixin slots.

## 5. Callback / async system

`Callback_Manager` state: `_frames_to_run: set<IRunCallback*>`, `_callbacks_to_run: map<IRunCallback*, list<pFrameResult_t>>` (one-shot FIFO), `_notifications: map<IRunCallback*, map<EOS_NotificationId, pFrameResult_t>>` (persistent), recursive mutex. `EOS_NotificationId` from a `static ... = 1` counter.

`EOS_Platform_Tick` → `Callback_Manager::tick()`:
1. `run_frames()`: call every registered `CBRunFrame()`, then `Network::CBRunFrame(0)` (drains inbound packets → dispatches to listeners).
2. `run_callbacks()`: for each queued `res`, once `res->CallbackOKTimeout()` (age ≥ `ok_timeout`) **and** (`res->done` or `owner->RunCallbacks(res)`), fire `res->GetFunc()(res->GetFuncParam())` (the game delegate), then `FreeCallback`, then erase.

Registration happens in each interface's **constructor** (`register_callbacks(this)`, `register_frame(this)`, `Network::register_listener(this, channel, MessagesCase::kX)`) and reversed in the destructor.

### `FrameResult` / `CallbackMessage_t`
`CallbackMessage_t = { int callback_type_id (== T::k_iCallback); uint8_t* func_param (heap EOS_*CallbackInfo); size_t func_param_size; std::function<void(void*)> cb_func; }`.
`FrameResult = { ms ok_timeout; bool remove_on_timeout; CallbackMessage_t res; time_point created_time; bool done; }`. `CreateCallback<T>(func, ok_timeout)` allocates a `T`, stamps `k_iCallback`, returns `T&` for in-place fill. Copy ctor deep-copies `func_param`; dtor frees it. `k_iCallback` is the tag identifying which `EOS_*CallbackInfo` a buffer holds — a key oracle for typing.

### Async op recipe (uniform across interfaces)
1. `new FrameResult`
2. `CreateCallback<EOS_X_CallbackInfo>(delegate)`
3. fill `ClientData` + fields
4. resolve: immediate/error → `done=true`; networked → stash `res` in a per-target pending map + `send_*` protobuf request
5. `add_callback(this, res)` → fired later from `run_callbacks()`

### Notifications
`AddNotify*` builds a FrameResult, `add_notification` → returns `EOS_NotificationId`. Fired **synchronously at the event site** (not via run_callbacks): `get_notifications(this, EOS_X_CallbackInfo::k_iCallback)` → patch payload → invoke func pointer. `RemoveNotify*` → `remove_notification`.

## 6. Settings (`NemirtingasEpicEmu.json`)

Singleton; path = `dirname(executable)/NemirtingasEpicEmu.json`; parsed with nlohmann::json; missing keys written back with defaults. Fields: `username` ("DefaultName"), `epicid`/`productuserid` (generated from username/appid seed if missing/invalid; random if username is DefaultName), `language` ("en"), `gamename` ("DefaultGameName"), `appid` ("InvalidAppid"), `unlock_dlcs` (true), `enable_overlay` (true), `disable_online_networking` (false), `savepath` ("appdata"), `log_level` ("OFF"). Save dir derived via `FileManager::set_root_dir(savepath/userid/appid)`.

## 7. Cross-cutting Ghidra landmarks

- `GLOBAL_LOCK()` = `lock_guard<recursive_mutex>` over one process-wide mutex — opens nearly every method; a landmark, not logic.
- `TRACE_FUNC()`/`APP_LOG` — ctor/dtor `Log` object bracketing each traced function; the `__func__` string is what we used to name methods. `get_callback_name(int)` maps `k_iCallback`→string (oracle).
- Version cascades: `EOS_Initialize` and `EOSSDK_Platform::Init` reinterpret one options pointer at several struct sizes via fallthrough `switch`.

## 8. Newer-build deltas vs 2020 source (running list)
- Per-interface `OnNetworkMessage` (vs single `RunNetwork`).
- Flat trampolines dispatch via vtable slot (vs non-virtual call).
- +13 interfaces (rtc*, anticheat*, sanctions, reports, mods, kws, custominvites, integratedplatform, progressionsnapshot).
- `network/` split into submodule; `overlay/` added.
- `EOSSDK_Client` gained `_SendEmuInfosRequest/_OnEmuInfosRequest/_OnEmuInfosResponse`, `_OnPeerConnect/_OnPeerDisconnect`, `InitializeClientInterfaces/InitializeGameServerInterfaces`, `GetEOSInterfaceFromPlugin` (plugin system).
