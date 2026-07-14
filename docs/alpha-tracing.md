# Operational alpha: config, run files, and trace schema

This is the contract the alpha tooling agrees on. Five things read or write it — the in-library
**config loader** and **trace sink**, and the out-of-library **C-ABI probe**, **`inspect_game`**, and
**alpha runner** — so it is defined once here and referenced everywhere else. Nothing here is
implemented yet; it is the design to build against.

Settled decisions:

- **Config is a small JSON file plus environment overrides.** `EOSR_*` variables override individual
  fields; a run with no file and no variables still works on defaults, and `EOSR_DATA_DIR` (which
  already exists) keeps its meaning.
- **Tracing is off by default with ordered verbosity levels** — `off < errors < lifecycle < full`.
- The trace is **versioned JSON Lines** from an **independent file sink**. It does not depend on the
  game calling `EOS_Logging_SetCallback`: the sink is ours, not the game's.
- **Every producer owns exactly one immutable file.** No file is read-modify-written by two processes.

Two hard constraints run through the whole thing, both from the ABI reality:

- **Nothing the trace needs may depend on the profile.** Tracing opens at `EOS_Initialize`; the
  key-derived profile is not loaded until `EOS_Platform_Create`. So the run directory, and every field
  used to open it, must be available before there is a profile.
- **`off` means no files at all** — no run directory, no trace, nothing on disk.

---

## 1. Run identity and directory layout

A run is identified at `EOS_Initialize` by a **`run_id`** that needs no profile: a filesystem-safe UTC
stamp, the PID, and a short suffix — e.g. `run-20260713T004500Z-48213-a1`. The suffix exists so two
copies started in the same second under the same clock cannot collide.

The run directory has **two ownership modes**, so the runner and a manual run never contend over who
created it:

- **Manual** (`EOSR_RUN_DIR` unset): the library owns the directory. It creates `<trace_dir>/<run_id>`
  with exclusive semantics — on a name collision it bumps the suffix and retries — and writes its own
  files into it.
- **Runner** (`EOSR_RUN_DIR` set): the runner has already created that directory exclusively, and may
  have written `launch.json` and opened `stdout.log` before launching the game. The library does
  **not** create the directory; it requires it to exist and exclusively creates only its own files
  (`runtime.json`, `trace.jsonl`). If `EOSR_RUN_DIR` is set but missing, tracing degrades to `off` with
  a best-effort logger diagnostic — the library never creates a directory the runner was meant to.

The directory is never named from `instance_label` or anything profile-derived — those are metadata,
emitted into the files, never used to open the run. And "`off` means no files" is precisely "**the
library** creates nothing": no run directory in manual mode, and none of the library's own files in
runner mode. The runner's `launch.json` and `stdout.log` are its to manage regardless of the library's
trace level.

```
<run-dir>/
  runtime.json       # library-owned, written once at startup
  trace.jsonl        # library-owned event stream; rotates to trace.1.jsonl, trace.2.jsonl, …
  launch.json        # runner-owned (only when the runner launched the game)
  inspection.json    # inspect_game-owned (only when inspection ran)
  stdout.log         # runner-owned game stdout/stderr
```

Each of `runtime.json` / `launch.json` / `inspection.json` has **one writer and is written once**, so
there is no read-modify-write race around game startup. The diagnostic bundle is a *later* step that
reads these owned files and produces a single merged, sanitized summary (§5); it never edits them in
place.

---

## 2. Config

Resolved at `EOS_Initialize`. Precedence per field, highest first: **environment → file → default**.
An override that fails validation at any layer is discarded and resolution falls through to the next
layer (an invalid `EOSR_TRACE` does not win, and does not force the default either — it yields to the
file value, then the default). An environment variable set to the empty string counts as **unset**.

The config file is `$EOSR_CONFIG` if set, else `<data_dir>/eosr.json`. A relative `EOSR_CONFIG`
resolves against `data_dir`, not the process cwd (which varies between launchers), so one setting
means one file everywhere. `data_dir` is resolved from
`EOSR_DATA_DIR` or the platform default *before* the file is read, so locating the file never depends
on the file; `data_dir` is the one field the file cannot set.

| Field | `eosr.json` key | Env override | Default | Notes |
|---|---|---|---|---|
| Display name | `display_name` | `EOSR_DISPLAY_NAME` | `"Player"` | Valid UTF-8, ≤ 16 characters **and** ≤ 64 bytes (both EOS caps); over-long is truncated to the tighter bound on a codepoint boundary. |
| Data directory | *(n/a)* | `EOSR_DATA_DIR` | platform default | Profile/key storage. Env or default only. |
| Run directory | *(n/a)* | `EOSR_RUN_DIR` | `<trace_dir>/<run_id>` | Runner override. Created exclusively. |
| Trace directory | `trace_dir` | `EOSR_TRACE_DIR` | `<data_dir>/traces` | A relative path resolves against `data_dir`, not the cwd. No `~`/variable expansion — the value is a literal path. |
| Trace level | `trace_level` | `EOSR_TRACE` | `"off"` | `off` / `errors` / `lifecycle` / `full`. |
| Max file bytes | `trace_max_bytes` | `EOSR_TRACE_MAX_BYTES` | `67108864` (64 MiB) | Per-file cap before rotation. Minimum `65536` (64 KiB) so one full record plus the rotate record always fits; a smaller value is clamped up with a diagnostic. Maximum `1073741824` (1 GiB). |
| Max rotated files | `trace_max_rotated_files` | `EOSR_TRACE_MAX_ROTATED` | `8` | Rotated `trace.N.jsonl` files kept, **not** counting the live `trace.jsonl`; total on disk is this + 1. Range 0–64; `0` keeps only the live file (rotation discards the previous one). |
| Discovery ports | `discovery_ports` | `EOSR_DISCOVERY_PORTS` | existing default range | `[first, last]` in JSON; `"first-last"` in env. |
| Instance label | `instance_label` | `EOSR_INSTANCE_LABEL` | `""` (unset) | Optional human label, metadata only; a bounded path-safe slug (see below). Never required to open the trace. |

```json
{
  "display_name": "Marlowe",
  "trace_dir": "traces",
  "trace_level": "lifecycle",
  "discovery_ports": [45700, 45703],
  "instance_label": "alice"
}
```

### Validation

- **`instance_label`** — 1–32 chars from `[A-Za-z0-9._-]`, and never `.` or `..`; anything else (a
  separator, a dot segment, an over-long value) is rejected back to unset. It is metadata and *may* be
  echoed by tooling, so it is slug-checked even though the library no longer puts it in a path.
- **Paths** (`trace_dir`, `EOSR_RUN_DIR`, `EOSR_CONFIG`) are literal: the library performs no `~` or
  environment expansion. A relative `trace_dir` or `EOSR_CONFIG` is taken against `data_dir`.
- **`display_name`** — must be valid UTF-8; invalid sequences reject the value to the default. It is
  bounded by **both** EOS caps — 16 displayable characters (`EOS_USERINFO_MAX_DISPLAYNAME_CHARACTERS`)
  and 64 UTF-8 bytes (`EOS_USERINFO_MAX_DISPLAYNAME_UTF8_LENGTH`) — and truncated to whichever bound
  bites first, on a codepoint boundary. (16 codepoints is at most 64 bytes, so the character cap is the
  binding one for well-formed text, but both are enforced so a 17–64-character ASCII name cannot slip
  through.)
- **Discovery ports** — `first` and `last` nonzero, `first <= last`, and the span bounded (≤ 64 ports)
  so a typo cannot open thousands of sockets.

### Config diagnostics honour `off`

A malformed field never prevents `EOS_Initialize` from succeeding. A diagnostic about it is recorded
as a `meta`/`config` trace record **only if the resolved sink turned out enabled and writable**;
otherwise it is best-effort through the ordinary log callback. With `trace_level` `off` — including the
case where `off` is itself the fallback from an unparseable level — **no run directory or trace file is
created**, and the diagnostic goes to the logger or nowhere. The sink can never be asked to record that
it could not be opened.

---

## 3. Owned files

### `runtime.json` (library, written once at startup)

```json
{
  "schema_version": 1,
  "emulator_build": "eosr 0.x (<git-sha>)",
  "created_utc": "2026-07-13T00:45:00Z",
  "run_id": "run-20260713T004500Z-48213-a1",
  "instance_label": "alice",
  "os": { "name": "linux", "version": "6.x", "wine": null },
  "config": { "display_name": "Marlowe", "trace_level": "lifecycle", "discovery_ports": [45700, 45703] }
}
```

The profile is not loaded yet at startup, so the local identity's pseudonymous fingerprint (§4) is not
here — it is emitted as a `meta`/`profile` trace record once `EOS_Platform_Create` acquires the
profile. Fields the writer cannot determine are `null`, never omitted, so the shape is stable.

### `launch.json` (runner, written once)

The runner owns what the library cannot see: which artifact replaced what, and how the game was
launched.

```json
{
  "schema_version": 1,
  "artifact": { "path": ".../EOSSDK-Win64-Shipping.dll", "sha256": "<ours>",
                "replaced_original_sha256": "<the game's Epic DLL>" },
  "launch": { "argv_redacted": ["<game>"], "cwd": "...", "exit_code": 0 }
}
```

### `inspection.json` (`inspect_game`, written once)

The import/export compatibility manifest `inspect_game` produces. Specified with that tool.

---

## 4. Trace stream (`trace.jsonl`)

One JSON object per line. Every record shares an **envelope** and adds a **body** keyed by `kind`.

### Envelope

| Field | Meaning |
|---|---|
| `v` | trace schema version (integer; `1` to start) |
| `seq` | per-process monotonic counter, from 0 |
| `t` | monotonic timestamp, nanoseconds from an arbitrary process epoch |
| `pid` | OS process id |
| `inst` | instance label, or `null` |
| `tid` | **logical** thread label (`t#0`, `t#1`, …) mapped from `std::thread::id` on first sight — not an OS TID, so no OS call is needed outside the platform shim |
| `kind` | `meta` / `call` / `return` / `callback` / `notify` / `net` |

### Bodies by kind

- **`meta`** — sink and lifecycle events: `run_start`, `rotate` (carries `dropped_files` /
  `dropped_bytes`), `profile` (the local pseudonymous `peer_fp` once the profile is loaded), `config`
  (a config diagnostic), `shutdown`.
- **`call`** — `fn`, `api` (the `ApiVersion` the game supplied), `corr` (for an async call, else
  absent), and `args`: a nested object of allow-listed scalars only — lengths, flags, labelled ids —
  never buffers, free text, or pointers.
- **`return`** — `fn`, `corr`, and whichever of these the function's signature actually produces:
  - `result` `{ code, name }` for an `EOS_EResult` return;
  - `value` `{ type, v }` for a scalar/enum/handle/bool/count/notification-id return — e.g.
    `{type:"bool", v:true}`, `{type:"count", v:5}`, `{type:"handle", v:"session#3"}`,
    `{type:"enum", v:"EOS_UNL_BottomRight"}`, `{type:"notification_id", v:"notif#2"}`;
  - `out` for allow-listed out-parameters (e.g. a `Copy*` writing `{ handle:"userinfo#4" }`, or an
    out-buffer's `len`);
  - `void: true` when the function returns nothing.
  A `Copy*` carries both `result` and `out`; a getter carries `value`; `EOS_Platform_Tick` carries
  `void`. There is always exactly one of `result` / `value` / `void`, optionally with `out`.
- **`callback`** — `fn` (the operation), `corr` (links back to its `call`), `result`, and `payload`: a
  nested object of allow-listed scalars, the same shape as `call`'s `args`.
- **`notify`** — `event` (e.g. `FriendsUpdate`), `action` (`register` / `remove` / `fire`), `id`
  (notification id), and allow-listed scalars. Registration, removal, and delivery are distinct events,
  never conflated.
- **`net`** — `event` ∈ `{discover, handshake, adopt, drop, search, p2p_open, p2p_close, …}`, with the
  peer's labelled id, its cross-process `peer_fp` on adopt/drop, byte lengths, and packet metadata — no
  payloads, keys, tokens, or raw ids.

Body fields are not free-form. A field is emitted only if its key is on the allow-list of known field
names (never a reserved envelope/body key like `seq`, `kind`, `event`, `fn`, or `result`), so a record
always has one unambiguous set of schema fields; every string value is bounded (≤ 512 bytes) and every
body's field count is bounded (≤ 32). Those caps put a hard ceiling on one record — comfortably under
the 64 KiB sink minimum, which must hold a full record *and* the rotate record — so no single event can
outgrow the sink.

### Correlation

An async `call` mints a `corr` id (per-process counter). Its synchronous `return` and its later
`callback` carry the same `corr`, so a reader stitches a request to its completion across the ticks
between them.

### Labels and the cross-process fingerprint

Handles and ids never appear raw. Each distinct handle/id gets a stable **local** label from a per-type
counter — `session#3`, `puid#7`, `eaid#2` — assigned on first sight and reused, so one object is
followable through its whole life without a pointer or a real id in the file.

Local labels are private to one process: Alice's `puid#2` and Bob's `puid#1` may be the same peer, and
nothing in either file proves it. So peer lifecycle records (`net` adopt/drop, and the local
`meta`/`profile`) also carry a **`peer_fp`** built by one exact construction, so two independent tools
compute the same value:

- **Input** — the peer's **product user id** (the mesh's primary key, present on every peer record) as
  its 32-character lowercase-hex string, taken as ASCII bytes.
- **Framing** — the ASCII bytes of the domain string `eosr-trace-peer-v1` (18 bytes) immediately
  followed by those 32 id bytes. Both parts are fixed length, so the 50-byte concatenation is
  unambiguous; there is no separator and no length prefix.
- **Digest** — SHA-256 (FIPS 180-4) of that 50-byte input.
- **Truncation** — the first 8 bytes of the 32-byte digest.
- **Encoding** — those 8 bytes as 16 **lowercase** hex characters.

Both ends compute the same `peer_fp` for a given peer, so two trace files join on it, while the
truncated, domain-separated digest does not hand the id back in a shared bundle. **Golden vector:**
product user id `00112233445566778899aabbccddeeff` yields `peer_fp` `ebf65ed621ba531b`.

### Verbosity levels

| Level | Includes |
|---|---|
| `off` | nothing (default); no files created |
| `errors` | `return`/`callback` with non-`Success` `result`; `net` `drop`; `meta`/`config` |
| `lifecycle` | `errors`, plus `meta` lifecycle, async `call`/`callback` pairs, `notify`, and `net` `discover`/`handshake`/`adopt`/`drop`/`search`/`p2p_open`/`p2p_close` |
| `full` | every `call` and `return`, for every EOS function |

Each level is a strict superset of the one before it, so raising the level only adds records.

### Writing, flushing, rotation, truncation

- **One line at a time under a mutex**, so concurrent threads never tear or interleave a record.
- **Buffered, not fsync-per-call.** The sink flushes at tick boundaries, on any `errors`-level record,
  on rotation, and at shutdown — not on every EOS call, so `full` tracing does not stall a game on the
  hot path.
- **Rotation** fires when `trace.jsonl` would exceed `trace_max_bytes` (whose enforced minimum
  guarantees a full record plus the rotate record always fit, so a record is never too big to write).
  With `N = trace_max_rotated_files`: delete `trace.<N>.jsonl` if present, rename `trace.<k>.jsonl` →
  `trace.<k+1>.jsonl` for `k` from `N-1` down to `1`, rename `trace.jsonl` → `trace.1.jsonl`, open a
  fresh `trace.jsonl`, and write a `meta`/`rotate` record at its head noting the discarded files/bytes.
  When `N` is `0` there are no numbered files: the previous `trace.jsonl` is discarded and only the
  fresh one is kept. The head `rotate` record is written after the rename and does not itself re-trigger
  the size check, so rotation cannot recurse.
- **Torn tail.** Each line is a complete write. A reader that meets a truncated final line (process
  killed mid-write) discards exactly that one partial record and keeps everything before it — the
  format is line-delimited precisely so one torn tail costs one record, not the file.

---

## 5. Redaction and the shareable bundle

The **trace stream** is structurally clean: at no level does it contain private keys or key material,
credential/continuance tokens, message payloads (lengths and metadata only), raw account ids or handle
pointers (only the labels and `peer_fp` of §4), or free-text user content in `args` (length only). The
sink is handed lengths, labels, and fingerprints — never the bytes — so there is nothing to scrub.

The **owned files are local-first**: `runtime.json` may hold the display name and `launch.json` an
absolute artifact path, which are fine on the tester's own disk but must not leak from a "redacted"
bundle. So the diagnostic bundle is an explicit **sanitization** step, not a `tar` of the directory: it
merges the owned files into one summary with the display name dropped and absolute paths reduced to
basenames, and copies the trace (already clean) alongside. Raw owned files never leave the machine
unless the tester opts in.

---

## 6. Failure and lifecycle

**A sink failure is never a game failure.** If a write, flush, or rename fails after the sink opened,
the sink disables itself: it stops writing, emits **one** best-effort diagnostic through the ordinary
logger, and every later call is a silent no-op. It never retries per call, never throws into the game,
and — the load-bearing part — a failure inside the sink does not itself generate a trace event, so a
full disk cannot cause an unbounded storm of failure records.

**Per-run state resets on every `EOS_Initialize`.** The `run_id`, the `seq` counter, the `corr`
counter, the logical thread-label map, the handle/id label registries, and the resolved config are all
per-run and start fresh each time:

- **Failed `EOS_Initialize`** closes any sink it opened cleanly, leaving no half-open run.
- **A second `EOS_Initialize` without shutdown** is `AlreadyConfigured` at the EOS layer; the tracer
  keeps the existing run and does not open a second.
- **`EOS_Shutdown`** flushes, writes a `meta`/`shutdown` record, and closes the sink. A later
  `EOS_Initialize` opens a wholly new run with fresh identity and counters — a trace never spans two
  init/shutdown cycles.

**Label registries are bounded.** A handle/id label is keyed by the opaque token behind it. The
library's own sub-handles are process-unique monotonic tokens that are never reused (see
`handle_store`), so a label maps one-to-one to an object for its whole life and a released token never
aliases a later one. The per-type registry is nonetheless capped and evicts least-recently-seen
entries, so an all-day session that browses thousands of sessions cannot grow it without bound; an
evicted id seen again simply gets a fresh label.

---

## 7. Why thread labels are always traced

The engine is single-threaded-by-tick, and a platform-wide any-thread lock is deliberately deferred.
A thread label on every record lets a real game's trace answer the question that gates that work: does
the game call EOS off the tick thread? We measure before we lock — if traces show only the tick thread,
the lock stays deferred; if they show others, we have the evidence and the exact call sites. The label
is logical (mapped from `std::thread::id`), so observing it needs no OS thread API.

---

## 8. Dependencies and platform boundary

- **No third-party JSON dependency.** We *write* the trace and owned files with a small internal
  `json_writer` (defined string escaping; output guaranteed valid UTF-8). Reading `eosr.json` uses a
  small hand-written reader for the flat object of strings/numbers/arrays the config is, with explicit
  bounds so a malformed or hostile file cannot exhaust memory: **input ≤ 64 KiB, any string/number
  token ≤ 4 KiB, ≤ 64 array elements, nesting depth ≤ 8**, each overflow a parse error. Grammar rules,
  all fixed here so parser tests do not freeze accidental behaviour: a leading UTF-8 BOM is skipped and
  no other encoding is accepted; a **duplicate key makes the whole file a parse error** (every field
  then falls through to env/default) rather than silently choosing one; **unknown keys are ignored**
  for forward-compatibility; a known key whose value is the wrong shape fails that one field's
  validation and falls through; and an **integer-typed field** (ports, byte caps) requires integer
  syntax, so a fraction or exponent is invalid for it. Any parse error leaves the file treated as
  absent, with a best-effort diagnostic. Both live in `src/common/` and are C++11.
- **OS-specific pieces stay in `src/platform/`**: directory creation with exclusive semantics, the PID,
  the UTC timestamp, and any atomic rename used by rotation. There is no C++11 filesystem API, so these
  go behind the existing platform shim with Windows and POSIX implementations, never in portable code.

---

## 9. Test matrix

Written **before** the loader/sink, against an injectable environment/file source so no real files or
env are needed:

- **Bootstrap without a profile** — the trace captures `EOS_Initialize` with no profile loaded.
- **Run directory** — `EOSR_RUN_DIR` is honoured; the auto path is created exclusively; two runs with
  identical `instance_label` (or same-second start) cannot collide.
- **Runner-owned directory lifecycle** — the runner can create/open its owned files before process
  launch without making the library reject `EOSR_RUN_DIR`; manual and runner-created directory modes
  have explicit, distinct ownership and collision tests.
- **Path safety** — `../`, path separators, dot segments, reserved Windows names, and over-long labels
  are all rejected and cannot escape `trace_dir`.
- **Precedence** — an invalid environment override falls through to the file value, then the default;
  an empty environment variable is unset.
- **Bounded config input** — over-sized files, strings, arrays, numeric tokens, duplicate keys, unknown
  nested values, a UTF-8 BOM, and fractional/exponent forms for integer fields all have defined,
  bounded outcomes without preventing `EOS_Initialize`.
- **Display-name boundaries** — 16 ASCII codepoints, 17 ASCII codepoints, and 16 four-byte UTF-8
  codepoints exercise both the character and byte caps without splitting a codepoint.
- **Off means nothing** — invalid config with tracing `off` creates no run directory or files.
- **Config diagnostics** — a config problem is recorded only when the sink is enabled and writable,
  else routed to the logger, never to an unopened sink.
- **Return shapes** — `void`, `EOS_EResult`, enum, count, bool, handle, notification-id, and
  out-parameter returns each serialize to their defined form.
- **Notification actions** — `register`, `remove`, and `fire` are distinct records.
- **Correlation** — an async call and its later callback share a `corr`.
- **Cross-process join** — the same peer's `peer_fp` matches across two process traces; the raw id
  never appears.
- **Fingerprint golden vector** — a fixed canonical identity produces one specified `peer_fp` in both
  implementations and processes; EAID/PUID input choice, encoding, truncation, and hex case cannot
  drift independently.
- **Concurrency** — records from multiple threads never tear or interleave a line.
- **Rotation** — a run past the cap rotates with deterministic ordering, keeps the file count bounded,
  records the drop, and cannot recursively rotate while writing that drop record.
- **Rotation limits** — zero, one, minimum, maximum, and a cap smaller than the rotate record have
  defined validation/fallback behaviour, and `trace_max_rotated_files` yields an exact total of
  rotated-plus-live files on disk.
- **Torn tail** — a file whose last line is a partial write parses as every complete record before it.
- **JSON output** — escaping is correct and output is always valid UTF-8 even for invalid-UTF-8 input.
- **No re-entrancy** — a sink write failure cannot itself generate more trace events.
- **Mid-run sink failure** — write, flush, and rename failures remain non-fatal, disable or degrade the
  sink exactly once, and do not retry or log recursively on every later EOS call.
- **Lifecycle reset** — failed/double initialize, shutdown, and initialize-after-shutdown close the old
  sink and reset run identity, sequence/correlation counters, logical thread labels, and config state.
- **Label churn** — releasing and reusing handle addresses never aliases two live objects in the trace,
  and high-volume create/release cycles do not grow the label registry without bound.
- **Bundle redaction** — the merged bundle contains no display name and no local absolute path.

---

## 10. What consumes this, later

Designed against this contract but built after it:

- **C-ABI probe** — two processes, distinct `data_dir`s, loading the real `.so`/`.dll` and calling only
  exported EOS functions; produces the same owned files and trace a game run does.
- **`inspect_game`** — reads a game's imports against the emulator's exports and writes
  `inspection.json` (inspect only; staging is a separate explicit action).
- **Alpha runner** — sets `EOSR_RUN_DIR`, selects the artifact, backs up and verifies the original,
  writes `launch.json`, launches or prints exact manual steps, builds the sanitized bundle from the
  owned files and trace, and restores the original.

## 11. Build sequence

1. Config-resolution tests against an injectable environment/file source.
2. A side-effect-free `resolved_config`.
3. The event model and `json_writer` with golden tests.
4. The bounded file sink with rotation.
5. Instrument one vertical slice first — `EOS_Initialize` → `EOS_Platform_Create` → one asynchronous
   Connect login and its callback — and validate correlation and output there before instrumenting the
   full export surface.
6. The two-process C-ABI probe as the first full consumer.
