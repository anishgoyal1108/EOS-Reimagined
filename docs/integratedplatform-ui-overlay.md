# Modules: `EOSSDK_IntegratedPlatform` (+ Container), `EOSSDK_UI`, Overlay

**Tier B (no 2020 source).** Platform-integration bridge + the social overlay control surface. 8 + 4 + 26 methods (+ the `overlay/EpicOverlay.cpp` module).

## `EOSSDK_IntegratedPlatform` (8) + `EOSSDK_IntegratedPlatformContainer` (4)
Handle `EOS_HIntegratedPlatform`. Bridges the SDK to a host platform (Steam/etc.) integration. Local state only.
- `SetUserLoginStatus` @`0x1800cfcb0`, `AddNotifyUserLoginStatusChanged` @`0x1800cfd70` + remover.
- Deferred logout: `SetUserPreLogoutCallback` @`0x1800d0090`, `ClearUserPreLogoutCallback` @`0x1800d0150`, `FinalizeDeferredUserLogout` @`0x1800d0210`.
- **Container** (options container, passed to `EOS_Platform_Create`; see `client.md` Init reading the IntegratedPlatformOptionsContainer): `Add` @`0x1800ceaf0`, `GetEntry` @`0x1800cee70`, `GetEntryCount` @`0x1800cede0`, `Release` @`0x1800cf010`. Created via `EOS_IntegratedPlatformOptionsContainer_*` flat API.

## `EOSSDK_UI` (26) — social overlay control
Handle `EOS_HUI` via `EOS_Platform_GetUIInterface`. `IRunCallback`. `EmuInit` @`0x18018c030`. Controls the emulator's overlay (`overlay/EpicOverlay.cpp`, gated by `Settings.enable_overlay`).
- **Show/hide:** `ShowFriends` @`0x18018c210`, `HideFriends` @`0x18018c3c0`, `ShowNativeProfile` @`0x18018d720`, `ShowReportPlayer` @`0x18018d100`, `ShowBlockPlayer` @`0x18018cf30`.
- **State:** `GetFriendsVisible`, `GetFriendsExclusiveInput`, `IsSocialOverlayPaused`, `PauseSocialOverlay`, `AcknowledgeEventId`.
- **Input config:** `Get/SetToggleFriendsKey`/`Button`, `IsValidKey/ButtonCombination`, `Get/SetNotificationLocationPreference`, `SetDisplayPreference`.
- **Overlay hooks:** `PrePresent` @`0x18018ceb0`, `ReportInputState` @`0x18018ce40` (render/input pump — where the overlay draws, tied to the DWMAPI/GDI imports + the D3D/Vulkan overlay). Notifications `DisplaySettingsUpdated`, `MemoryMonitor`.

## Overlay (`overlay/EpicOverlay.cpp`)
The in-game overlay renderer (many methods are internal, not EOS-flat). Hooks the game's present/input (relates to `PrePresent`/`ReportInputState` and the WinHTTP/graphics imports). Gated by `enable_overlay`. Largely a rendering/input concern — **out of scope for a headless library reimpl** (can be stubbed: overlay never visible, `GetFriendsVisible=false`).

## Reimpl notes / follow-ups
- Reimpl: IntegratedPlatform = local login-status state + pre-logout callback plumbing; UI = report overlay not-visible/paused and accept show/hide as no-ops unless an overlay is implemented; the options container is a simple keyed list consumed at platform create.
- Follow-ups: which integrated-platform types are recognized; overlay render backend (D3D11/12/Vulkan/GL hooks) if overlay parity is ever wanted.
