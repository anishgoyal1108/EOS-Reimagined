# Module: `EOSSDK_CustomInvites`

**Tier B (no 2020 source).** A **genuinely networked** invite/request-to-join system — the only Tier-B interface with its own `OnNetworkMessage`. 27 methods.

## 1. Identity & handle
`EOS_HCustomInvites` via `EOS_Platform_GetCustomInvitesInterface`. `IRunCallback` + `IRunNetwork` (`OnNetworkMessage` @`0x1800bb770`).

## 2–4. State & lifecycle
- Holds the current custom-invite payload (`SetCustomInvite`) + received invites + pending request-to-join state. `EmuInit` @`0x1800b38b0` / `EmuDeinit` @`0x1800b74c0` register callbacks + a network listener.
- **New network path:** dispatched via `OnNetworkMessage` → `_OnCustomInviteReceived` @`0x1800bb920` (stores invite, fires `CustomInviteReceived` notifications). Sender `_SendCustomInvite` @`0x1800b7880`. This is a protocol message **not present in the 2020 proto** — a new envelope `oneof` case (to confirm/add in `protocol.md`).

## 5–6. API
- **Invites:** `SetCustomInvite` @`0x1800b3b60` (stage payload), `SendCustomInvite` @`0x1800b3df0` (1.4 KB — send to targets over network), `FinalizeInvite` @`0x1800b4ed0`.
- **Request-to-join (newer EOS feature):** `SendRequestToJoin` @`0x1800b5240`, `AcceptRequestToJoin` @`0x1800b6670`, `RejectRequestToJoin` @`0x1800b6a60`.

## 7. Notifications
`CustomInviteReceived`/`Accepted`/`Rejected`; `RequestToJoinReceived`/`Accepted`/`Rejected`/`ResponseReceived`; `SendCustomNativeInviteRequested` (overlay/native invite bridge) + removers. Callback-infos `EOS_CustomInvites_On*CallbackInfo`.

## 8. Network protocol — RESOLVED (new `EpicCustomInvites_pb`)
Wire: **`EpicCustomInvites_pb{ CustomInvites_Invite_pb{id, payload} }`** — `id` = sender ProductUserId, `payload` = the opaque custom-invite string the game set via `SetCustomInvite`. Inbound `_OnCustomInviteReceived` upserts + fires `CustomInviteReceived`; request-to-join flows send/accept/reject over the mesh (TCP). New envelope, not in the 2020 proto. See `protocol.md`.

## Reimpl notes / follow-ups
- Reimpl: a networked payload-invite + request-to-join relay with the notification set above; store the local custom payload; deliver to peers over the mesh.
- Follow-ups: exact wire message (decompile `_SendCustomInvite`/`SendCustomInvite` → `protocol.md` new case); how the payload string is carried; interaction with the overlay `SendCustomNativeInviteRequested`.
