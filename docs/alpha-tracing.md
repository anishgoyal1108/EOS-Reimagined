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
stamp, the PID, and an exclusive suffix — e.g. `run-20260713T004500Z-48213-a1`. The suffix exists so
two copies started in the same second under the same clock cannot collide: the directory is created
with exclusive semantics, and on collision the suffix is bumped and creation retried.

The run directory is `$EOSR_RUN_DIR` when the runner set it (the runner picks the path and tells the
library through that variable), otherwise `<trace_dir>/<run_id>`. It is **not** named from
`instance_label` or anything profile-derived — those are metadata, emitted into the files, never used
to open them.

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

The config file is `$EOSR_CONFIG` if set, else `<data_dir>/eosr.json`. `data_dir` is resolved from
`EOSR_DATA_DIR` or the platform default *before* the file is read, so locating the file never depends
on the file; `data_dir` is the one field the file cannot set.

| Field | `eosr.json` key | Env override | Default | Notes |
|---|---|---|---|---|
| Display name | `display_name` | `EOSR_DISPLAY_NAME` | `"Player"` | Valid UTF-8, ≤ 64 bytes (the EOS display-name cap); over-long is truncated on a UTF-8 boundary. |
| Data directory | *(n/a)* | `EOSR_DATA_DIR` | platform default | Profile/key storage. Env or default only. |
| Run directory | *(n/a)* | `EOSR_RUN_DIR` | `<trace_dir>/<run_id>` | Runner override. Created exclusively. |
| Trace directory | `trace_dir` | `EOSR_TRACE_DIR` | `<data_dir>/traces` | A relative path resolves against `data_dir`, not the cwd. No `~`/variable expansion — the value is a literal path. |
| Trace level | `trace_level` | `EOSR_TRACE` | `"off"` | `off` / `errors` / `lifecycle` / `full`. |
| Max file bytes | `trace_max_bytes` | `EOSR_TRACE_MAX_BYTES` | `67108864` (64 MiB) | Per-file cap before rotation. |
| Max files kept | `trace_max_files` | `EOSR_TRACE_MAX_FILES` | `8` | Rotated files retained. |
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
  environment expansion. A relative `trace_dir` is taken against `data_dir`.
- **`display_name`** — must be valid UTF-8; invalid sequences reject the value to the default. Length
  is bounded to the EOS UTF-8 cap and truncated on a codepoint boundary.
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
  absent), `args` (allow-listed scalars only — lengths, flags, labelled ids — never buffers, free
  text, or pointers).
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
- **`callback`** — `fn` (the operation), `corr` (links back to its `call`), `result`, and allow-listed
  payload scalars.
- **`notify`** — `event` (e.g. `FriendsUpdate`), `action` (`register` / `remove` / `fire`), `id`
  (notification id), and allow-listed scalars. Registration, removal, and delivery are distinct events,
  never conflated.
- **`net`** — `event` ∈ `{discover, handshake, adopt, drop, search, p2p_open, p2p_close, …}`, with the
  peer's labelled id, its cross-process `peer_fp` on adopt/drop, byte lengths, and packet metadata — no
  payloads, keys, tokens, or raw ids.

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
`meta`/`profile`) also carry a **`peer_fp`**: a domain-separated, truncated hash of the peer's
high-entropy key-derived id (e.g. the first 8 bytes of `SHA-256("eosr-trace-peer-v1" || id)`, hex).
Both ends compute the same `peer_fp` for a given peer, so two trace files can be joined — while the
truncated, domain-separated digest does not hand back the id itself in a shared bundle.

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
- **Rotation** fires when `trace.jsonl` would exceed `trace_max_bytes`: delete `trace.<max>.jsonl` if
  present, rename `trace.<k>.jsonl` → `trace.<k+1>.jsonl` for `k` from `max-1` down to `1`, rename
  `trace.jsonl` → `trace.1.jsonl`, open a fresh `trace.jsonl`, and write a `meta`/`rotate` record at
  its head noting the discarded files/bytes. That head record is written after the rotation and does
  not itself re-trigger the size check, so rotation cannot recurse.
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

## 6. Why thread labels are always traced

The engine is single-threaded-by-tick, and a platform-wide any-thread lock is deliberately deferred.
A thread label on every record lets a real game's trace answer the question that gates that work: does
the game call EOS off the tick thread? We measure before we lock — if traces show only the tick thread,
the lock stays deferred; if they show others, we have the evidence and the exact call sites. The label
is logical (mapped from `std::thread::id`), so observing it needs no OS thread API.

---

## 7. Dependencies and platform boundary

- **No third-party JSON dependency.** We *write* the trace and owned files ourselves, so a small
  internal `json_writer` (with defined string escaping and guaranteed-valid UTF-8 output) covers
  output; reading `eosr.json` needs only a small hand-written reader for the flat object of
  strings/numbers/arrays the config is. Both live in `src/common/` and are C++11.
- **OS-specific pieces stay in `src/platform/`**: directory creation with exclusive semantics, the PID,
  the UTC timestamp, and any atomic rename used by rotation. There is no C++11 filesystem API, so these
  go behind the existing platform shim with Windows and POSIX implementations, never in portable code.

---

## 8. Test matrix

Written **before** the loader/sink, against an injectable environment/file source so no real files or
env are needed:

- **Bootstrap without a profile** — the trace captures `EOS_Initialize` with no profile loaded.
- **Run directory** — `EOSR_RUN_DIR` is honoured; the auto path is created exclusively; two runs with
  identical `instance_label` (or same-second start) cannot collide.
- **Path safety** — `../`, path separators, dot segments, reserved Windows names, and over-long labels
  are all rejected and cannot escape `trace_dir`.
- **Precedence** — an invalid environment override falls through to the file value, then the default;
  an empty environment variable is unset.
- **Off means nothing** — invalid config with tracing `off` creates no run directory or files.
- **Config diagnostics** — a config problem is recorded only when the sink is enabled and writable,
  else routed to the logger, never to an unopened sink.
- **Return shapes** — `void`, `EOS_EResult`, enum, count, bool, handle, notification-id, and
  out-parameter returns each serialize to their defined form.
- **Notification actions** — `register`, `remove`, and `fire` are distinct records.
- **Correlation** — an async call and its later callback share a `corr`.
- **Cross-process join** — the same peer's `peer_fp` matches across two process traces; the raw id
  never appears.
- **Concurrency** — records from multiple threads never tear or interleave a line.
- **Rotation** — a run past the cap rotates with deterministic ordering, keeps the file count bounded,
  records the drop, and cannot recursively rotate while writing that drop record.
- **Torn tail** — a file whose last line is a partial write parses as every complete record before it.
- **JSON output** — escaping is correct and output is always valid UTF-8 even for invalid-UTF-8 input.
- **No re-entrancy** — a sink write failure cannot itself generate more trace events.
- **Bundle redaction** — the merged bundle contains no display name and no local absolute path.

---

## 9. What consumes this, later

Designed against this contract but built after it:

- **C-ABI probe** — two processes, distinct `data_dir`s, loading the real `.so`/`.dll` and calling only
  exported EOS functions; produces the same owned files and trace a game run does.
- **`inspect_game`** — reads a game's imports against the emulator's exports and writes
  `inspection.json` (inspect only; staging is a separate explicit action).
- **Alpha runner** — sets `EOSR_RUN_DIR`, selects the artifact, backs up and verifies the original,
  writes `launch.json`, launches or prints exact manual steps, builds the sanitized bundle from the
  owned files and trace, and restores the original.

## 10. Build sequence

1. Config-resolution tests against an injectable environment/file source.
2. A side-effect-free `resolved_config`.
3. The event model and `json_writer` with golden tests.
4. The bounded file sink with rotation.
5. Instrument one vertical slice first — `EOS_Initialize` → `EOS_Platform_Create` → one asynchronous
   Connect login and its callback — and validate correlation and output there before instrumenting the
   full export surface.
6. The two-process C-ABI probe as the first full consumer.
