# ADR 0001 — Authenticated Mesh Identity

Status: **accepted, in progress** (2026-07-13). Supersedes the "self-asserted identity" limitation recorded in `progress.md` under the frozen foundation.

## 1. Problem

The foundation mesh authenticates a peer's `product_user_id` only for the *duration of one connection* (commit `7e74309`: the router stamps each inbound frame with the id the socket was adopted under). That is enough to stop a connected peer from *switching* identity per message, so the owner/participant/awaited checks on Sessions and Lobby are real between connected peers. Two gaps remain:

- **First contact is self-asserted.** The dialer trusts the `product_user_id` in whoever answers at the advertised address. A peer can announce another peer's id and, if it wins the race or the real peer is absent, be adopted under it.
- **Epic-account id is a payload field.** Presence keys on `epic_account_id`, which travels inside the presence message, so a peer can claim another account's Epic id from its *own* authenticated `product_user_id` (presence first-writer-wins persists for the Epic key).

Both reduce to: *there is no cryptographic binding between a mesh identity and any secret only its rightful owner holds.*

## 2. The two guarantees (and which one we solve)

- **G1 — "this connection consistently controls this emulator identity."** Solvable with no backend, by cryptography. **This ADR delivers G1.**
- **G2 — "this identity belongs to a real Epic account / legitimate game owner."** Requires Epic or another trusted authority. **Out of scope, permanently, without a backend.** We will never claim it.

Honest end-state wording (for README/docs once shipped):
> EOS Reimagined cryptographically authenticates emulator *profiles* to one another. These identities are self-issued and do **not** attest an Epic account, game ownership, or Epic-service membership.

## 3. Decision (locked)

- **Vendor Monocypher 4.0.3 core** for the elliptic-curve and AEAD primitives (X25519, IETF ChaCha20-Poly1305). We do **not** hand-roll curve or AEAD math. This is consistent with CLAUDE.md ("prefer vendoring something small and portable") and how we already vendor `doctest`. Monocypher's core is one `.c`/`.h` pair, < 2000 lines, dependency-free, C99/C++-compilable, CC0-1.0 with a 2-clause-BSD fallback. It is compiled in its own target with the project's `-Werror`/warning set **not** applied to the vendored source.
- **Reuse our existing from-scratch SHA-256 / HMAC-SHA256** (`src/common/crypto.*`, already KAT-verified against FIPS/RFC vectors) for the Noise hash function and HKDF. No new hash implementation.
- **Handshake: standards-exact `Noise_XX_25519_ChaChaPoly_SHA256`.** Not a "Noise-style" approximation — the real transcript, checked against the published Noise test vectors. Rationale for **XX**: peers know neither the other's static key at first contact (so a pre-shared-key or known-responder pattern does not fit), and XX transmits both static keys *encrypted* and proves possession of both, producing mutually-authenticated transport keys with forward secrecy.
- **Self-certifying identities:** `ProductUserId` and `EpicAccountId` are *derived from the peer's static X25519 public key*. The receiver **recomputes** them from the key it authenticated in the handshake and never trusts a claimed id. Ed25519 is **not** needed initially — the X25519 static key already is the persistent identity, and Noise proves possession of its private half.
- **The router exposes ONE verified transport identity.** After this milestone, no interface decides who sent a frame from an envelope string; it reads the verified id the secure channel provides.
- **The existing `EncryptionKey`** (a per-game secret every client knows) MAY be mixed into the handshake as an optional Noise **PSK** to gate a game's mesh, but it can **never** be the identity root — every copy of the game holds it, so it cannot distinguish players.

## 4. Identity derivation

A profile is a persistent X25519 keypair `(s_priv, s_pub)`. Ids are derived so the **format is unchanged** from today (32 lowercase hex = 16 bytes), which means `id_registry`, the wire, and every interface are untouched by the *format*; only the *source* of the bytes changes.

```
profile = SHA256("eosr-profile-v1" || s_pub)                    // 32 bytes, domain-separated

EAID = hex( first_16_bytes( SHA256("eosr-eaid-v1" || profile) ) )
PUID = hex( first_16_bytes( SHA256("eosr-puid-v1" || profile
                                    || product_id || sandbox_id || deployment_id) ) )
```

- `PUID` folds in product/sandbox/deployment so the same profile is a distinct product-user across titles/deployments, matching EOS semantics.
- `EAID` is product-independent (one Epic-account-like identity per profile), matching how Presence keys on it.
- Domain-separation strings (`eosr-*-v1`) keep the three hashes independent and versioned; a future change bumps the suffix.
- SHA-256 is our from-scratch implementation; the derivation is pure and identical on every platform.

The private key lives in a profile file under the platform config dir (`platform/paths`), never on the wire. Losing it loses the identity (there is no recovery authority — that is inherent).

## 5. Handshake — `Noise_XX_25519_ChaChaPoly_SHA256`

Standard Noise XX, initiator = the dialer (lower id, per the existing dedup), responder = the accepter.

```
XX:
  -> e
  <- e, ee, s, es
  -> s, se
```

We implement the full `SymmetricState`/`HandshakeState`: `MixHash`, `MixKey`, `MixKeyAndHash` (for the optional PSK), `EncryptAndHash`, `DecryptAndHash`, `Split`. The concrete function map:

- **DH:** X25519 (Monocypher `crypto_x25519`).
- **Cipher:** ChaCha20-Poly1305, IETF/RFC 8439 construction with the 96-bit nonce = `0^32 || le64(counter)` exactly as Noise specifies (Monocypher `crypto_aead_lock`/`unlock`).
- **Hash:** SHA-256 (ours). HKDF is `HMAC-SHA256`-based, matching Noise's `HKDF`.

**Prologue** (bound into the transcript via the initial `MixHash`, so any disagreement makes the handshake fail closed):
```
prologue = "eosr-noise-v1" || wire_protocol_version(=2) || product_id || sandbox_id || deployment_id
```
This ties the session to the game and the protocol version; a peer of a different game or version cannot complete the handshake.

**Identity verification.** After `-> s, se` (initiator static received) and `<- e, ee, s, es` (responder static received), each side has the other's authenticated static public key. Each **recomputes** the peer's `PUID`/`EAID` from that key (Section 4) and adopts the connection under the *recomputed* id — never a claimed one. If a later frame or advertisement names a different id, it is a mismatch and the connection is dropped.

**Transport.** `Split()` yields two directional cipher states (`c1`, `c2`); each direction has its own key and a strictly increasing 64-bit counter (the AEAD nonce). Counter exhaustion (approaching 2^64) tears the session down rather than reusing a nonce. All handshake secrets and the DH scratch are wiped (`crypto_wipe`) after use. Authentication is fail-closed: any MAC failure, decrypt failure, or transcript mismatch aborts the connection.

## 6. Transport framing (TCP)

Post-handshake, every mesh frame is `AEAD(key_dir, counter++, plaintext=serialized net_envelope, ad = frame_header)`. The 4-byte big-endian length prefix stays (it frames the ciphertext). The router:

- runs the handshake as the first thing on a new TCP connection (replacing the current "first net_advertise names the peer");
- adopts the peer under the **recomputed** id once the handshake completes;
- decrypts each inbound frame, and the **decrypted, verified** `source_id` is guaranteed to equal the connection's recomputed id (we still stamp, but now it is cryptographically backed, not merely socket-scoped);
- drops the connection on any decrypt/auth failure, replay (counter regression), or truncated/reordered frame.

## 7. Transport framing (UDP / P2P)

The P2P data path uses UDP and is currently unauthenticated. After the TCP handshake establishes per-peer keys, UDP packets to that peer are protected too:

- Each UDP datagram is `AEAD(udp_key_dir, seq, plaintext, ad)`, `ad =` verified sender id, dest id, channel, socket name, session generation, and the 64-bit sequence number.
- A per-peer **replay window** (sliding bitmap) rejects duplicate or too-old sequence numbers.
- UDP discovery (`net_advertise`) remains an **untrusted hint**: it only triggers a dial; the authenticated TCP handshake is what establishes identity. (Signing advertisements is a possible later refinement; not required, since discovery grants no trust on its own.)

## 8. Wire protocol version & interoperability

`wire_protocol_version` **1 → 2**. A v2 peer speaks the Noise handshake; a v1 peer speaks the plaintext `net_advertise` handshake.

- **v2 ↔ v2:** authenticated, as above.
- **v2 ↔ v1:** governed by the peer's mode (Section 9). In *authenticated mode* a v2 peer refuses a v1 peer (no silent downgrade). In *legacy/compatibility mode* it may accept v1 with the old connection-bound (unauthenticated-first-contact) semantics, and the peer is surfaced as **unverified**.

## 9. Migration & modes

Existing profiles derive ids deterministically from username, or randomly when unconfigured (`settings::derive_identity`). We cannot both *preserve an arbitrary legacy id* and *authenticate it on first contact* — the legacy id is not bound to any key. So:

- **New profile (default going forward):** generate an X25519 profile key on first run, derive self-certifying ids (Section 4), run in **authenticated mode**.
- **Legacy profile:** keep the username/random id, generate a key, and pin `(id → key)` on first contact — **TOFU**. Strong continuity *after* first meeting; first meeting is still self-asserted. Such peers are labeled **unverified** to the game/UI and never silently upgrade a claim.
- **Profile export/import:** a small file so the same person keeps one identity across machines (replacing the "derive from username" portability we lose).
- **Orchestrator provisioning:** allow a launcher (e.g. Nucleus) to provision a **distinct profile key per local instance**, so N local copies are N distinct authenticated identities.
- **No silent downgrade:** authenticated mode never falls back to unverified for the same peer within a session; a peer is either verified (key-proven) or explicitly unverified.

The mode is a platform setting; default **authenticated** for new profiles, **legacy/TOFU** when an existing username/random identity is detected, with a clear log line either way.

## 10. Code layout

```
third_party/monocypher/        monocypher.c, monocypher.h, LICENSE.md, UPSTREAM.md (version + checksums)
src/common/crypto.{h,cpp}      + X25519, ChaCha20-Poly1305 (thin wrappers; Monocypher API never escapes), HKDF
src/net/secure_channel.{h,cpp} Noise XX SymmetricState/HandshakeState + transport cipher states
src/core/identity.{h,cpp}      profile key load/generate, id derivation, verified-identity registry, mode/TOFU
src/net/message_router.*       runs the handshake, frames over the channel, exposes the verified id
src/platform/paths.*           profile-key file location (already have the config-dir shim)
```

The Monocypher header is included **only** inside `src/common/crypto.cpp` (and the vendored target). Nothing else in the tree sees it, so a future primitive swap touches one file.

## 11. Acceptance tests (gate before authenticated mode is default)

- **KATs:** Monocypher self-tests + our X25519/ChaCha20-Poly1305 wrappers against published RFC 7748 / RFC 8439 vectors; SHA-256/HMAC KATs already exist.
- **Noise vectors:** the handshake checked against the official `Noise_XX_25519_ChaChaPoly_SHA256` test vectors.
- **Transcript equality:** initiator and responder derive identical handshake hash and transport keys.
- **Tamper rejection:** a flipped bit in any handshake field aborts; a modified/reordered/duplicated/truncated transport frame is rejected.
- **Identity binding:** a peer with the wrong static key cannot be adopted under another peer's derived id; a replayed handshake is rejected.
- **Router invariant:** the id an interface sees always comes from the secure channel, never the envelope.
- **Nonce discipline:** counters never repeat across directions or reconnects; exhaustion tears down.
- **Cross-platform:** Linux and Windows produce identical transcript/derivation vectors.
- **Legacy:** in authenticated mode a v1/legacy peer is refused without downgrade; in legacy mode it is accepted and surfaced unverified.
- **Sanitizers + fuzz:** ASan/UBSan across the suite; a malformed-frame fuzz target on the channel decoder.

## 12. Implementation phases (each committed green on both targets, ASan-clean)

1. Vendor Monocypher (fetch + verify + CMake target). *(task 50)*
2. crypto wrapper: X25519, ChaCha20-Poly1305, HKDF + KATs. *(task 51)*
3. `secure_channel`: Noise XX + Noise vectors + transcript-equality. *(task 52)*
4. Self-certifying identity + profile key + legacy/TOFU + export/import. *(task 53)*
5. Wire v2: router runs the handshake, frames over the channel, exposes the verified id; authenticated UDP + replay; interfaces consume the verified id; full regression + authenticated-mesh e2e. *(task 54)*

## 13. What this does not change

- Id **format** (32 hex) — so `id_registry`, interfaces, and higher-level wire structs are untouched.
- The single-threaded-tick engine model.
- The deferred/stub interface surface.
- It does **not** attest an Epic account or game ownership (G2). That remains impossible without Epic and is stated plainly.
