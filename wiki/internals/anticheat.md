# Modules: `EOSSDK_AntiCheatClient` + `EOSSDK_AntiCheatServer`

**Tier B (no 2020 source).** Recovered from binary + 1.19 headers. The emulator implements anti-cheat as a **transparent passthrough that fakes success** so EAC-gated games run. 23 + 30 methods.

## Key finding: "protection" is a CRC32-C checksum, not encryption
`AntiCheatClient::ProtectMessage` @`0x180075530` (and the server's @`0x18007a740`) compute **CRC-32C (Castagnoli, reversed poly `0x82f63b78`)** over the input and append the 4-byte checksum big-endian: `OutBytesWritten = InSize + 4`. `GetProtectMessageOutputLength` returns `InSize + 4`. `UnprotectMessage` @`0x1800756b0` strips/verifies the trailing CRC. There is **no real Easy Anti-Cheat cryptography** — messages pass through intact plus a checksum. This is what lets protected game traffic round-trip without an EAC backend.

## Auth flow = faked success
- `RegisterPeer` @`0x180075770` (client) / `RegisterClient` @`0x18007a110` (server) genuinely record the peer/client (handle, type, platform string) into a vector. This enables the interface to then fire `PeerAuthStatusChanged` / `ClientAuthStatusChanged` notifications reporting the peer/client as **authenticated/valid** — so the game treats everyone as passing anti-cheat.
- `BeginSession`/`EndSession` (136 B) and `ReceiveMessageFrom*` (129 B) are thin stubs.

## Server logging = no-ops
`AntiCheatServer` has a large family of 129-byte stubs that accept and discard: `LogGameRoundStart/End`, `LogPlayerSpawn/Despawn/Revive/Tick`, `LogPlayerTakeDamage`, `LogPlayerUseWeapon/UseAbility`, `LogEvent`, `RegisterEvent`, `SetClientDetails/NetworkState/GameSessionId`. Telemetry for cheat detection — irrelevant without a backend.

## API surface
- **Client (23):** `BeginSession`/`EndSession`, `RegisterPeer`/`UnregisterPeer`, `ProtectMessage`/`UnprotectMessage`/`GetProtectMessageOutputLength`, `ReceiveMessageFromPeer`/`FromServer`, `AddExternalIntegrityCatalog`, `Reserved01`; notifications `MessageToServer`/`MessageToPeer`/`PeerActionRequired`/`PeerAuthStatusChanged`/`ClientIntegrityViolated`.
- **Server (30):** `RegisterClient`/`UnregisterClient`, `ReceiveMessageFromClient`, `ProtectMessage`/`UnprotectMessage`, the `Log*` family, `SetClient*`, notifications `MessageToClient`/`ClientActionRequired`/`ClientAuthStatusChanged`.

## Reimpl notes / follow-ups
- Reimpl: `ProtectMessage` = append CRC32-C; `UnprotectMessage` = verify+strip; registration records peers; immediately fire auth-status-changed = authenticated; all `Log*`/`Set*`/`Receive*` = accept + no-op. This exactly matches games' happy path.
- The `MessageToServer`/`MessageToPeer`/`MessageToClient` notifications carry the "protected" messages the game must relay between peers/server — confirm whether the emu actually round-trips these (likely yes, so P2P anti-cheat handshakes complete). Follow-up: read `RegisterClient` auth-status emission + the message-relay notifications.
