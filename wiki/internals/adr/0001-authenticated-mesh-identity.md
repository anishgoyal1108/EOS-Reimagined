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
- **The existing `EncryptionKey`** (a per-game secret every client knows) can gate a game's mesh but can **never** be the identity root — every copy of the game holds it, so it cannot distinguish players. When gating is enabled the protocol is exactly **`Noise_XXpsk0_25519_ChaChaPoly_SHA256`**: `psk0` places a `MixKeyAndHash(psk)` before the first message and adds a `MixKey` on every `e` token, per the Noise spec's PSK rules. The `psk` is the 32 bytes decoded from the `EncryptionKey`, which is validated as **exactly 64 lowercase hex characters** (→ 32 bytes); any other value is rejected with a clear error rather than silently truncated or ignored. **Phase 1 ships plain `Noise_XX` (no PSK)**; enabling gating is a later opt-in that changes the protocol name, so a psk and a non-psk peer simply fail to handshake (different protocol name in the transcript) rather than interoperate insecurely.

## 4. Identity derivation

A profile is a persistent X25519 keypair `(s_priv, s_pub)`. Ids are derived so the **format is unchanged** from today (32 lowercase hex = 16 bytes), which means `id_registry`, the wire, and every interface are untouched by the *format*; only the *source* of the bytes changes.

Every hashed input is a **canonical length-prefixed encoding**, never a raw concatenation of variable-length fields — otherwise `(product,sandbox,deployment) = ("ab","c","")` and `("a","bc","")` would hash to the same PUID. Define:

```
lp(x)          = u32_be( byte_length(x) ) || x            // one length-prefixed field
enc(a, b, ...) = lp(a) || lp(b) || ...                    // unambiguous ordered concatenation
```

so `enc("ab","c","")` = `00000002 "ab" 00000001 "c" 00000000` differs from `enc("a","bc","")`. Then:

```
profile = SHA256( enc("eosr-profile-v1", s_pub) )                 // 32 bytes, domain-separated

EAID = hex( first_16_bytes( SHA256( enc("eosr-eaid-v1", profile) ) ) )
PUID = hex( first_16_bytes( SHA256( enc("eosr-puid-v1", profile,
                                        product_id, sandbox_id, deployment_id) ) ) )
```

- `PUID` folds in product/sandbox/deployment (each its own length-prefixed field) so the same profile is a distinct product-user across titles/deployments, matching EOS semantics.
- `EAID` is product-independent (one Epic-account-like identity per profile), matching how Presence keys on it.
- The `eosr-*-v1` domain strings keep the three hashes independent and versioned; a future change bumps the suffix. `enc()` is the single canonical encoder used everywhere an id or the prologue (Section 5) is derived.
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

- **DH:** X25519, via the `crypto` wrapper's `x25519_shared` (which rejects the all-zero low-order result), never raw Monocypher.
- **Cipher:** IETF ChaCha20-Poly1305 (RFC 8439) with the 96-bit nonce = `0x00000000 || le64(counter)` exactly as Noise specifies, via the wrapper's `aead_encrypt`/`aead_decrypt` (built on Monocypher's `crypto_aead_init_ietf` + `crypto_aead_write`/`read`). **Not** `crypto_aead_lock`/`unlock` — those are the 24-byte-nonce XChaCha20 construction and would misread a 12-byte Noise nonce. The wrapper is KAT-verified against RFC 8439 §2.8.2.
- **Hash:** SHA-256 (ours). HKDF is `HMAC-SHA256`-based (the wrapper's `hkdf_sha256`), matching Noise's `HKDF`.

**Prologue** (bound into the transcript via the initial `MixHash`, so any disagreement makes the handshake fail closed). It uses the same canonical `enc()` from Section 4, with the wire version as a fixed `u8`, so no two distinct contexts share a transcript:
```
prologue = enc("eosr-noise-v1") || u8(wire_protocol_version = 2)
           || enc(product_id, sandbox_id, deployment_id)
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

The P2P data path uses UDP and is currently unauthenticated. After the TCP handshake completes, UDP packets to that peer are protected with keys **derived separately from the TCP transport keys**, so the two transports never share a `(key, nonce)`:

```
(c_i2r, c_r2i)      = Split()                                    // the two Noise cipher states — TCP ONLY
udp_i2r, udp_r2i    = HKDF( salt = ck_final,                     // the SECRET Noise chaining key
                            ikm  = enc("eosr-udp-subkey-v1"), 2 )
```

- **The UDP keys are derived from `ck_final`, the secret Noise chaining key at the end of the handshake — never from `handshake_hash`.** `handshake_hash` is a hash of *observable* transcript data (public keys and ciphertext), so an eavesdropper could reproduce any key derived from it; the chaining key mixes in every DH shared secret and is never transmitted. `secure_channel` derives these inside `split()` (which holds `ck_`) before the handshake state is discarded, alongside the two TCP cipher states.
- TCP framing (Section 6) uses `c_i2r`/`c_r2i` directly (standard Noise transport). UDP uses `udp_i2r`/`udp_r2i` — independent keys under a distinct label, so a UDP sequence starting at 0 can never collide with a TCP counter at 0.
- **A `session_generation` counter was specified here and then dropped when task 54 landed.** Its job was to make a reconnect derive fresh UDP keys so an old datagram could not be replayed into a new session. `ck_final` already does that: every DH in XX mixes in at least one freshly generated ephemeral, so no two handshakes produce the same chaining key, and therefore no two sessions produce the same UDP keys. A datagram from an old session cannot open under a new session's key regardless. A generation counter would add a value *both sides must agree on* — a synchronization problem — in exchange for a freshness guarantee we already hold. It is left out of the `ikm` and out of the associated data.
- Each of the four keys has its **own** 64-bit counter/sequence starting at 0; a direction's key is used with a strictly increasing counter and torn down on exhaustion.
- Each UDP datagram is `AEAD(udp_key_dir, le64(seq), plaintext, ad)` with `ad = enc(`verified sender id, dest id, `u64(seq)`)`. **The channel and socket name are not in the `ad`** as originally specified: they live *inside* the sealed plaintext (they are fields of the `p2p_data` message), so they are already unforgeable, and putting them in the `ad` would mean the receiver had to know them *before* decrypting — which it cannot. Keeping them sealed is both simpler and better: a datagram's channel and socket are not visible on the wire at all.
- On the wire a datagram is `[kind][u32 tag][u64 seq][sealed]`. The **tag** is a digest of the key the sender seals with, which only the two ends hold, so it says which peer a datagram is from without anyone writing a name in the clear — and a datagram tagged for us that will not open is simply not from the peer it claims. The **kind** byte separates a sealed datagram from a plaintext discovery broadcast on the same socket, so neither has to be guessed at from its shape.
- A per-peer **replay window** (sliding 64-bit bitmap) rejects duplicate or too-old sequence numbers. It is spent **only on a datagram that authenticated** — otherwise anyone able to send us a packet could shove the window to the far end of the sequence space with a forged number and take every datagram still in flight down with it.
- **Where a peer's datagrams go** is not learned from the broadcast. The address is the far end of the TCP connection whose handshake authenticated the peer (nobody without the key could have been there), and the port comes from an advertisement that peer sealed over the mesh. Neither is anyone else's to redirect.
- **A datagram never tears down a session.** Unlike a mesh frame, one that fails to authenticate is dropped in silence: a peer who can send us rubbish over UDP must not be able to end a working connection by doing so.
- **The mesh is the fallback.** Between meeting a peer and its first sealed advertisement we do not yet know where to aim, so an unreliable packet takes the mesh. Arriving reliably when unreliable was asked for is a promise kept too well, never one broken.
- UDP discovery (`net_advertise`) remains an **untrusted hint**: it only triggers a dial; the authenticated TCP handshake is what establishes identity. (Signing advertisements is a possible later refinement; not required, since discovery grants no trust on its own.)

## 8. Wire protocol version & interoperability

`wire_protocol_version` **1 → 2**. A v2 peer speaks the Noise handshake; a v1 peer speaks the plaintext `net_advertise` handshake.

- **v2 ↔ v2:** authenticated, as above.
- **v2 ↔ v1:** **refused.** A v1 peer cannot prove an identity, and there are no v1 profiles in the world to be compatible with (Section 9), so there is no downgrade path to keep open. The prologue pins the version into the transcript, so a v1 peer simply fails the handshake rather than being talked out of one.

## 9. Migration & modes

**Amended when task 53 landed. There is no legacy profile to migrate, so there is no legacy mode.**

The original plan here was: keep an existing username-derived or random id, generate a key for it, and pin `(id → key)` on first contact (TOFU), with a v2 peer able to accept a v1 peer as *unverified*. Implementing it revealed the premise was wrong. `settings::derive_identity` re-derived its ids **in memory on every run and never wrote them anywhere** — the emulator had no persistence at all. No identity has ever survived a process exit, so no user holds one, and nothing exists to preserve. A legacy/TOFU mode would be dead code whose only effect would be to keep an unauthenticated acceptance path alive in the router.

So the mode is dropped. Authenticated is the only mode:

- **Every profile is a key.** An X25519 profile key is minted on first run, persisted, and the ids are derived from it (Section 4). Losing the file loses the identity; there is no authority that can reissue it.
- **No downgrade, no unverified peer.** A v1 peer is refused outright (Section 8). A peer is verified or it is not connected.
- **Profile export/import:** the profile file *is* the export format — 64 hex characters. Copying it to another machine makes you the same player there, which is the portability the username derivation used to give (and gave to anyone else who typed the same name).
- **A profile slot per local instance.** Several copies of one game on one machine must be several *players*. If they all read one profile file they would derive one id, each would see the other's advertisement as its own, and couch co-op would never mesh — the exact failure the old random-id mint was avoiding. Each instance therefore takes an **exclusive profile slot** (`profile.key`, `profile-1.key`, …), held by an OS advisory lock for the life of the process, in the same way and for the same reason each instance takes an exclusive discovery port. The lock dies with the process, so a crash never strands a slot, and a given instance lands on the same profile every run.
- **Orchestrator provisioning:** a launcher (e.g. Nucleus) can instead point each instance at its own directory via `EOSR_DATA_DIR`, which is also how the alpha test tooling runs two isolated players on one machine.
- **No writable directory** is not fatal: the instance keeps the ephemeral key it started with and logs that the identity will not outlive the run. The mesh still works; only persistence is lost.

## 10. Code layout

```
third_party/monocypher/        monocypher.c, monocypher.h, LICENSE.md, UPSTREAM.md (version + checksums)
src/common/crypto.{h,cpp}      + X25519, ChaCha20-Poly1305 (thin wrappers; Monocypher API never escapes),
                               HKDF, canonical_encoder (the enc()/lp() of §4)
src/net/secure_channel.{h,cpp} Noise XX SymmetricState/HandshakeState + transport cipher states
src/core/identity.{h,cpp}      profile key load/generate/persist, id derivation, per-instance profile
                               slot, hex export/import
src/net/message_router.*       runs the handshake, frames over the channel, exposes the verified id
src/platform/paths.*           data directory (EOSR_DATA_DIR override), mkdir -p, owner-only file
                               write, and the advisory file_lock the profile slot is held with
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
- **Encoding non-ambiguity:** `enc("ab","c","")` and `enc("a","bc","")` produce **different** PUIDs and different prologues (the length-prefix regression from the review).
- **Transport-key separation:** the TCP counter-0 and UDP sequence-0 tuples never share a `(key, nonce)` — the UDP keys are derived under their own label and differ from the Split outputs.
- **PSK (when gating is enabled):** the `Noise_XXpsk0_25519_ChaChaPoly_SHA256` path matches its official vectors, and a non-64-hex or otherwise malformed `EncryptionKey` is rejected explicitly, not silently ignored.
- **No downgrade:** a v1 peer is refused; there is no code path that adopts an unverified peer.
- **Profile slots:** two instances sharing one profile directory take two slots and derive two different identities; each keeps its slot across runs; a crashed instance's slot is free again; a corrupt or all-zero profile is replaced rather than adopted.
- **Sanitizers + fuzz:** ASan/UBSan across the suite; a malformed-frame fuzz target on the channel decoder. (Note: the integration/dynamic-library LSan tier has a **pre-existing** process-lifetime leak in the load/unload path, present since `foundation-frozen` and unrelated to crypto — tracked separately; the unit tier is leak-clean.)

## 12. Implementation phases (each committed green on both targets, ASan-clean)

1. ~~Vendor Monocypher (fetch + verify + CMake target).~~ *(task 50 — done)*
2. ~~crypto wrapper: X25519, ChaCha20-Poly1305, HKDF + KATs.~~ *(task 51 — done)*
3. ~~`secure_channel`: Noise XX + Noise vectors + transcript-equality.~~ *(task 52 — done)*
4. ~~Self-certifying identity + profile key + per-instance slot + export/import.~~ *(task 53 — done; legacy/TOFU dropped, see §9)*
5. ~~Wire v2: router runs the handshake, frames over the channel, exposes the verified id; authenticated UDP + replay; interfaces consume the verified id; full regression + authenticated-mesh e2e.~~ *(task 54 — done, in two parts: 54a the authenticated TCP mesh, 54b the authenticated UDP data path. `session_generation` dropped, see §7.)*

The milestone is complete. `EOS_EPacketReliability` now selects a transport rather than being ignored, which was the part of this that a game can actually feel.

## 13. What this does not change

- Id **format** (32 hex) — so `id_registry`, interfaces, and higher-level wire structs are untouched.
- The single-threaded-tick engine model.
- The deferred/stub interface surface.
- It does **not** attest an Epic account or game ownership (G2). That remains impossible without Epic and is stated plainly.
