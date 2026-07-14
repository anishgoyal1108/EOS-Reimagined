# Operational alpha: config, run manifest, and trace schema

This is the contract the alpha tooling agrees on. Five things read or write it — the in-library
**config loader** and **trace sink**, and the out-of-library **C-ABI probe**, **`inspect_game`**, and
**alpha runner** — so it is defined once here and referenced everywhere else. Nothing in this document
is implemented yet; it is the design to build against.

Two decisions shape it, both settled:

- **Config is a small JSON file plus environment overrides.** `EOSR_*` variables override individual
  fields; a run with no file and no variables still works on defaults, and `EOSR_DATA_DIR` (which
  already exists) keeps its meaning.
- **Tracing is off by default and has ordered verbosity levels** — `off < errors < lifecycle < full` —
  rather than category flags. One knob, and "more detail" is a single step up.

The trace is a **versioned JSON Lines** stream written by an **independent file sink**. It does *not*
depend on the game calling `EOS_Logging_SetCallback`: a game that never wires up logging still
produces a full trace, because the sink is ours, not the game's.

---

## 1. Run directory layout

One run writes one directory, so every tool points at the same place:

```
<run-dir>/
  manifest.json      # who/what/where for this run (written once, at startup)
  trace.jsonl        # the event stream; rotates to trace.1.jsonl, trace.2.jsonl, …
  stdout.log         # game stdout/stderr, only when the runner launched the game
```

`<run-dir>` defaults to `<trace_dir>/<instance_label>-<startup-stamp>`. The runner picks it; a manual
tester gets one under the configured `trace_dir`.

---

## 2. Config

Resolved at `EOS_Initialize` time. Precedence, highest first: **environment variable → config file →
built-in default**.

The config file is located at `$EOSR_CONFIG` if set, otherwise `<data_dir>/eosr.json`. `data_dir`
itself is resolved from `EOSR_DATA_DIR` or the platform default *before* the file is read, so locating
the file never depends on the file — `data_dir` is the one field the file cannot set.

| Field | `eosr.json` key | Env override | Default | Notes |
|---|---|---|---|---|
| Display name | `display_name` | `EOSR_DISPLAY_NAME` | `"Player"` | What Friends/UserInfo advertise. Today's `DefaultName` blocker. |
| Data directory | *(n/a)* | `EOSR_DATA_DIR` | platform default | Profile/key storage. Env or default only. |
| Trace directory | `trace_dir` | `EOSR_TRACE_DIR` | `<data_dir>/traces` | Where run directories are created. |
| Trace level | `trace_level` | `EOSR_TRACE` | `"off"` | `off` / `errors` / `lifecycle` / `full`. |
| Discovery ports | `discovery_ports` | `EOSR_DISCOVERY_PORTS` | existing default range | `[first, last]` in JSON; `"first-last"` in env. |
| Instance label | `instance_label` | `EOSR_INSTANCE_LABEL` | generated | Distinguishes several local copies; see below. |

```json
{
  "display_name": "Marlowe",
  "trace_dir": "~/eosr-runs",
  "trace_level": "lifecycle",
  "discovery_ports": [45700, 45703],
  "instance_label": "alice"
}
```

**Instance label.** When unset, generate a short stable label so multiple copies on one machine are
distinguishable in traces without the user naming each. It is a display/label convenience only — it is
*not* identity (the key-derived ids remain the only identity) and never appears where a peer could rely
on it. Derive it deterministically from the profile so a given copy keeps the same label across runs.

**Malformed config is non-fatal.** A missing file is normal (defaults apply). A present-but-unparseable
file, an unknown `trace_level`, or an out-of-range port must degrade to the default for that field and
emit one `errors`-level trace record naming the problem — never abort `EOS_Initialize`. A game must
still launch with a broken config.

---

## 3. Run manifest

`manifest.json`, one object, written once at startup. It is the header the diagnostic bundle is built
around, and what `inspect_game` and the runner fill in on the tooling side.

```json
{
  "schema_version": 1,
  "emulator_build": "eosr 0.x (<git-sha>)",
  "created_utc": "2026-07-13T00:00:00Z",
  "instance_label": "alice",
  "os": { "name": "linux", "version": "6.x", "wine": null },
  "config": { "display_name": "Marlowe", "trace_level": "lifecycle", "discovery_ports": [45700, 45703] },
  "artifact": {
    "path": ".../EOSSDK-Win64-Shipping.dll",
    "sha256": "<ours>",
    "replaced_original_sha256": "<the game's Epic DLL we backed up>"
  }
}
```

The library writes what it knows (build, os, config). The runner fills `artifact` and any launch
detail, because the library cannot see what replaced what. Fields the writer cannot determine are
`null`, never omitted, so the shape is stable for parsers.

---

## 4. Trace stream (`trace.jsonl`)

One JSON object per line. Every record shares an **envelope** and adds a **body** keyed by `kind`.

### Envelope (every record)

| Field | Meaning |
|---|---|
| `v` | trace schema version (integer; `1` to start) |
| `seq` | per-process monotonic counter, from 0 |
| `t` | monotonic timestamp, nanoseconds since an arbitrary process epoch |
| `pid` | OS process id |
| `inst` | instance label |
| `tid` | OS thread id — **always present** (see §6) |
| `kind` | `call` / `return` / `callback` / `notify` / `net` |

### Bodies by kind

- **`call`** — `fn` (EOS function name), `api` (the `ApiVersion` the game supplied), `corr` (correlation
  id for an async call, else absent), `args` (allow-listed scalar args only: lengths, flags, labelled
  ids — never buffers, strings that may carry names/tokens, or pointers).
- **`return`** — `fn`, `corr` (matches its `call`), `result` (`{ code: <int>, name: "EOS_Success" }`).
- **`callback`** — `fn` (the delegate's operation), `corr` (links back to the originating `call`),
  `result`, plus allow-listed payload scalars.
- **`notify`** — `event` (e.g. `FriendsUpdate`, `PeerConnected`), `id` (notification id), and
  allow-listed scalars.
- **`net`** — `event` ∈ `{discover, handshake, adopt, drop, search, p2p_open, p2p_close, …}`, with
  labelled peer id, byte lengths, and packet metadata — **no payloads, keys, tokens, or raw ids.**

### Correlation

An async `call` mints a `corr` id (per-process counter). Its `return` (the synchronous ack) and its
later `callback` (fired on a tick) both carry the same `corr`, so a reader can stitch a request to its
completion across the ticks between them. This is what makes an async-heavy trace legible.

### Opaque stable labels

Handles and user ids never appear raw. Each distinct handle/id gets a stable label from a per-type
counter — `session#3`, `lobby#1`, `puid#7`, `eaid#2` — assigned on first sight and reused. A reader
can follow one session across its whole life without the trace ever carrying a pointer value or a real
account id.

### Verbosity levels

| Level | Includes |
|---|---|
| `off` | nothing (default) |
| `errors` | `return`/`callback` with non-`Success` `result`; `net` `drop`; config problems |
| `lifecycle` | everything in `errors`, plus async `call`/`callback` pairs, `notify`, and `net` `discover`/`handshake`/`adopt`/`drop`/`search`/`p2p_open`/`p2p_close` |
| `full` | every `call` and `return`, for every EOS function |

Each level is a superset of the one before it, so raising the level only adds records.

### Rotation, bounding, and truncation

- The sink caps a single file at a configured size and rotates (`trace.jsonl` → `trace.1.jsonl` → …),
  keeping a bounded number of files, so an all-day session cannot fill a disk. What rotation dropped is
  itself recorded, so a truncated history never reads as a complete one.
- Each line is flushed whole. A reader that hits a truncated final line (process killed mid-write)
  must discard exactly that last partial record and treat everything before it as valid — the format is
  line-delimited precisely so one torn tail costs one record, not the file.

---

## 5. Redaction (deny by default)

The trace is meant to be shared in a diagnostic bundle, so it never contains, at any level:

- Private keys or any profile key material.
- Credential tokens, auth material, or continuance tokens.
- P2P or control-message **payloads** (only their lengths and metadata).
- Raw account ids or handle pointer values (only the opaque labels of §4).
- Display names or other free-text user content in `args` — length only.

Tracing is opt-in (`off` by default), and even when on, the above are structurally absent rather than
scrubbed after the fact: the sink is given lengths and labels, never the bytes.

---

## 6. Why thread ids are always traced

The engine is single-threaded-by-tick today, and a platform-wide any-thread lock is deliberately
deferred. `tid` on every record lets a real game's trace answer the question that gates that work: does
the game actually call EOS off the tick thread? We measure before we lock — if traces show only
on-tick calls, the lock stays deferred; if they show off-tick calls, we have the evidence and the exact
call sites.

---

## 7. Test matrix

The sink and config loader ship with tests covering:

- **Schema stability** — a golden record of each `kind` at each level round-trips; the version field
  is present and bumped when the shape changes.
- **Disabled by default** — with no config and no env, zero trace files are created and the hot path
  writes nothing.
- **Callback correlation** — an async call and its later callback carry the same `corr`.
- **Concurrent writes** — records from multiple threads interleave without a torn or interleaved line.
- **Invalid paths / config** — an unwritable `trace_dir`, an unparseable `eosr.json`, a bad
  `trace_level`, and an out-of-range port each degrade to default and keep `EOS_Initialize` succeeding.
- **Bounded size and rotation** — a run past the cap rotates, keeps the file count bounded, and records
  the drop.
- **Truncated final record** — a file whose last line is a partial write parses as every complete
  record before it, discarding only the torn tail.
- **Redaction** — no key, token, payload, raw id, or free-text name appears at any level.

---

## 8. What consumes this, later

Designed against this contract but built after it:

- **C-ABI probe** — two processes, distinct `data_dir`s, loading the real `.so`/`.dll` and calling only
  exported EOS functions; produces the same manifest + trace a game run does.
- **`inspect_game`** — reads a game's imports against the emulator's exports and writes a
  compatibility manifest (inspect only; staging is a separate explicit action).
- **Alpha runner** — selects the artifact, backs up and verifies the original, writes the manifest,
  launches or prints exact manual steps, collects everything into one redacted diagnostic bundle, and
  restores the original.
