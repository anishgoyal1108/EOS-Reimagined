# Modules: `EOSSDK_RTC` (+ `RTCAudio`, `RTCData`, `RTCAdmin`)

**Tier B (no 2020 source).** Real-time comms (voice/data rooms). **Room + participant management is real; media transport is stubbed** (audio/data payloads are accepted and discarded). 13 + 38 + 9 + 7 methods.

## `EOSSDK_RTC` (13) — room control
Handle `EOS_HRTC` via `EOS_Platform_GetRTCInterface`. `EmuInit` @`0x18013fa70`.
- `JoinRoom` @`0x180140460`, `LeaveRoom` @`0x1801406b0`, `BlockParticipant` @`0x180140900`.
- Sub-interface getters: `GetAudioInterface` @`0x1801402a0`, `GetDataInterface` @`0x180140380`.
- Notifications: `Disconnected`, `ParticipantStatusChanged` (join/leave), `RoomStatisticsUpdated` + removers.
- Rooms integrate with **Lobby** (`Lobby::JoinRTCRoom`, see `lobby.md`).

## `EOSSDK_RTCAudio` (38) — voice
Handle `EOS_HRTCAudio`. `EmuInit` @`0x18014ee50`.
- **Media stub:** `SendAudio` @`0x1801421d0` validates `EOS_RTCAudio_SendAudioOptions` then returns `EOS_Success` and **discards the buffer** — no voice is transmitted. `UpdateSending/Receiving`(+`Volume`), `UpdateParticipantVolume` update local state only.
- **Devices:** `QueryInput/OutputDevicesInformation`, `Get/CopyInput/OutputDevice*`, `GetAudioInput/OutputDevicesCount`, `SetAudioInput/OutputSettings`, `SetInput/OutputDeviceSettings` — device enumeration (may return host devices or empty).
- **Platform users:** `RegisterPlatformUser`/`Unregister`, `RegisterPlatformAudioUser`/`Unregister`.
- Notifications: `ParticipantUpdated`, `AudioBeforeSend`/`BeforeRender` (audio hooks), `AudioInput/OutputState`, `AudioDevicesChanged`.

## `EOSSDK_RTCData` (9) — data channel
Handle `EOS_HRTCData`. `EmuInit` @`0x18014efd0`.
- **Media stub:** `SendData` @`0x18014af90` (112 B — validate + discard). `UpdateSending/Receiving`. Notifications `DataReceived`, `ParticipantUpdated`.

## `EOSSDK_RTCAdmin` (7) — server-side admin
Handle `EOS_HRTCAdmin`. `EmuInit` @`0x18014f540`.
- `QueryJoinRoomToken` @`0x18014f720`, `CopyUserTokenByIndex`/`ByUserId`, `Kick` @`0x18014fa90`, `SetParticipantHardMute` @`0x18014fc50`. Token issuance for room join; kick/mute.

## Reimpl notes / follow-ups
- Reimpl (minimal-parity): implement room membership + participant status notifications (so lobby voice-room UI works); accept `SendAudio`/`SendData` and drop; report a plausible device list. Full voice would need Opus + an RTC data path over the P2P/UDP transport — out of scope for parity.
- Follow-ups: whether `RTCData` `SendData` could be wired to the real P2P transport (it's the closest to functional); device enumeration source; `RoomStatisticsUpdated` fields.
