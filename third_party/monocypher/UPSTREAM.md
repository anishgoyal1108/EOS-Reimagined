# Monocypher — vendored upstream

Vendored for the elliptic-curve (X25519) and AEAD (ChaCha20-Poly1305) primitives used by the
Authenticated Mesh Identity milestone (`wiki/developers/internals/adr/0001-authenticated-mesh-identity.qmd`). We do not
hand-roll these; see the ADR for the rationale.

- **Version:** 4.0.3
- **Upstream:** https://monocypher.org/  (author: Loup Vaillant)
- **Release page:** https://github.com/LoupVaillant/Monocypher/releases/tag/4.0.3
- **Fetched:** 2026-07-13, from https://monocypher.org/download/monocypher-4.0.3.tar.gz
- **Licence:** dual BSD-2-Clause / CC0-1.0 (see `LICENCE.md`). We use it under either.

## Integrity

The downloaded tarball was verified byte-for-byte against the checksum monocypher.org publishes
alongside it (`monocypher-4.0.3.tar.gz.sha512`):

```
tarball  sha512  40904ada5c7ee4f7741733e38b69a30a4b0561cbffba5ffe7c2dce16136d540251ec0d9056ff606510d3b5b708fb8a40db7e0870d4a0b2dc17ba2bfb880f8965
tarball  sha256  8cc9bc341a66249016db9bd70e9142d8d0aef9945973744b1ac05dbc55d8ee66
```

Vendored files (only the core pair + licence are kept; tests/build files are not):

```
monocypher.c   sha256  57eb914fc88136119bd41655cccb8c250048bf54d470540625186f8ab16f64be
monocypher.h   sha256  c494da712122da7ff679fdcf318a5317e84972b6c950fe9d896212947797facd
LICENCE.md     sha256  5f8360e4c06ddcc584bdb4b210c6af824c4bb301e6a9a521869b6d90795ca4b3
```

To re-verify from a clean checkout:

```
sha256sum third_party/monocypher/monocypher.c third_party/monocypher/monocypher.h
```

**Provenance note (recommended before trusting in production):** the checksum above confirms the
download matches what monocypher.org publishes. For full provenance, independently verify the
upstream GPG signature (Loup Vaillant's key) or cross-check the tarball checksum against the GitHub
release, since 4.0.3 post-dates this project's build and was not audited as the current artifact
(Cure53 audited an earlier version in 2020). Monitor upstream advisories; 4.0.3 itself fixed a
signature-timing leak over 4.0.2.

## Local modifications

**None.** The files are the unmodified upstream `src/monocypher.{c,h}`. Do not edit them; to update,
replace both files from a verified upstream tarball and update the checksums above. The only
integration is the CMake target (`third_party/monocypher/CMakeLists.txt` / the root `CMakeLists`),
which compiles this pair without the project's `-Werror` warning set.

The Monocypher header is included **only** from `src/common/crypto.cpp`; nothing else in the tree
sees the vendor API.
