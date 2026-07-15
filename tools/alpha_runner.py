#!/usr/bin/env python3
"""Stage EOS Reimagined for one game run and build a shareable diagnostic bundle."""

import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path, PureWindowsPath
import re
import secrets
import shlex
import shutil
import stat
import subprocess
import sys
import tempfile


SCHEMA_VERSION = 1
BACKUP_SUFFIX = ".eosr-original"
STATE_SUFFIX = ".eosr-stage.json"
MAX_JSON_BYTES = 16 * 1024 * 1024
MAX_TRACE_LINE_BYTES = 64 * 1024
TRACE_LEVELS = ("errors", "lifecycle", "full")
SDK_NAMES = {
    ".dll": "EOSSDK-Win64-Shipping.dll",
    ".so": "libEOSSDK-Linux-Shipping.so",
}


class runner_error(Exception):
    pass


def sha256_file(path):
    digest = hashlib.sha256()
    with Path(path).open("rb") as stream:
        while True:
            block = stream.read(1024 * 1024)
            if not block:
                break
            digest.update(block)
    return digest.hexdigest()


def backup_path(sdk_path):
    sdk = Path(sdk_path)
    return sdk.with_name(sdk.name + BACKUP_SUFFIX)


def state_path(sdk_path):
    sdk = Path(sdk_path)
    return sdk.with_name(sdk.name + STATE_SUFFIX)


def _write_new_json(path, value, mode=0o600):
    path = Path(path)
    data = (json.dumps(value, indent=2, sort_keys=True) + "\n").encode("utf-8")
    try:
        with path.open("xb") as stream:
            stream.write(data)
            stream.flush()
            os.fsync(stream.fileno())
        os.chmod(str(path), mode)
    except Exception:
        try:
            path.unlink()
        except OSError:
            pass
        raise


def _read_json(path, cap=MAX_JSON_BYTES):
    path = Path(path)
    size = path.stat().st_size
    if size > cap:
        raise runner_error("%s exceeds the %d-byte limit" % (path.name, cap))
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, ValueError) as exc:
        raise runner_error("cannot read %s: %s" % (path.name, exc))
    if not isinstance(value, dict):
        raise runner_error("%s must contain a JSON object" % path.name)
    return value


def _copy_new(source, target, mode):
    source = Path(source)
    target = Path(target)
    try:
        with source.open("rb") as src, target.open("xb") as dst:
            shutil.copyfileobj(src, dst, 1024 * 1024)
            dst.flush()
            os.fsync(dst.fileno())
        os.chmod(str(target), mode)
    except Exception:
        try:
            target.unlink()
        except OSError:
            pass
        raise


def _replace_from(source, target, mode):
    source = Path(source)
    target = Path(target)
    fd, tmp_name = tempfile.mkstemp(prefix=".%s.eosr-" % target.name, dir=str(target.parent))
    tmp = Path(tmp_name)
    try:
        with source.open("rb") as src, os.fdopen(fd, "wb") as dst:
            shutil.copyfileobj(src, dst, 1024 * 1024)
            dst.flush()
            os.fsync(dst.fileno())
        os.chmod(str(tmp), mode)
        os.replace(str(tmp), str(target))
    except Exception:
        try:
            os.close(fd)
        except OSError:
            pass
        try:
            tmp.unlink()
        except OSError:
            pass
        raise


def stage_sdk(sdk_path, artifact_path):
    sdk_input = Path(sdk_path)
    artifact = Path(artifact_path).resolve()
    if sdk_input.is_symlink():
        raise runner_error("refusing to replace a symlinked SDK: %s" % sdk_input)
    sdk = sdk_input.resolve()
    if not sdk.is_file() or not artifact.is_file():
        raise runner_error("both the game SDK and emulator artifact must be regular files")
    if sdk == artifact:
        raise runner_error("the emulator artifact and game SDK are the same file")

    backup = backup_path(sdk)
    state_file = state_path(sdk)
    if os.path.lexists(str(backup)) or os.path.lexists(str(state_file)):
        raise runner_error(
            "an earlier staging transaction exists; restore it first with: "
            "%s restore --sdk %s" % (Path(__file__).name, shlex.quote(str(sdk)))
        )

    original_hash = sha256_file(sdk)
    staged_hash = sha256_file(artifact)
    if original_hash == staged_hash:
        raise runner_error("the selected artifact is already installed")
    original_mode = stat.S_IMODE(sdk.stat().st_mode)
    state = {
        "schema_version": SCHEMA_VERSION,
        "sdk": sdk.name,
        "original_sha256": original_hash,
        "staged_sha256": staged_hash,
        "original_mode": original_mode,
    }

    try:
        _write_new_json(state_file, state)
        _copy_new(sdk, backup, original_mode)
        if sha256_file(backup) != original_hash:
            raise runner_error("the SDK backup failed hash verification")
        _replace_from(artifact, sdk, original_mode)
        if sha256_file(sdk) != staged_hash:
            raise runner_error("the staged artifact failed hash verification")
    except Exception as exc:
        try:
            if sdk.is_file() and sha256_file(sdk) == original_hash:
                if backup.exists():
                    backup.unlink()
                if state_file.exists():
                    state_file.unlink()
        except OSError:
            pass
        if isinstance(exc, runner_error):
            raise
        raise runner_error("could not stage the emulator: %s" % exc)
    return state


def restore_sdk(sdk_path):
    sdk_input = Path(sdk_path)
    if sdk_input.is_symlink():
        raise runner_error("refusing to restore through a symlinked SDK: %s" % sdk_input)
    sdk = sdk_input.resolve()
    backup = backup_path(sdk)
    state_file = state_path(sdk)
    if state_file.is_symlink() or backup.is_symlink():
        raise runner_error("the staging state or backup is a symlink; refusing recovery")
    if not state_file.exists():
        if backup.exists():
            raise runner_error("an SDK backup exists without its verification state; refusing to overwrite")
        raise runner_error("no active staging transaction exists for %s" % sdk)

    state = _read_json(state_file, 64 * 1024)
    required = ("original_sha256", "staged_sha256", "original_mode")
    if state.get("schema_version") != SCHEMA_VERSION or any(key not in state for key in required):
        raise runner_error("the staging state is incompatible or incomplete")
    valid_hash = re.compile(r"^[0-9a-f]{64}$")
    if (
        state.get("sdk") != sdk.name or
        not isinstance(state["original_sha256"], str) or
        not isinstance(state["staged_sha256"], str) or
        not valid_hash.match(state["original_sha256"]) or
        not valid_hash.match(state["staged_sha256"]) or
        not isinstance(state["original_mode"], int) or
        not 0 <= state["original_mode"] <= 0o7777
    ):
        raise runner_error("the staging state contains invalid verification data")
    if not sdk.is_file():
        raise runner_error("the staged SDK path no longer contains a regular file")

    current_hash = sha256_file(sdk)
    original_hash = state["original_sha256"]
    staged_hash = state["staged_sha256"]
    if current_hash not in (original_hash, staged_hash):
        raise runner_error(
            "the SDK changed after staging; refusing to overwrite it (restore manually after inspection)"
        )
    if current_hash == staged_hash:
        if not backup.is_file() or sha256_file(backup) != original_hash:
            raise runner_error("the original SDK backup is missing or failed hash verification")
        try:
            _replace_from(backup, sdk, int(state["original_mode"]))
        except Exception as exc:
            raise runner_error("could not restore the original SDK: %s" % exc)
        if sha256_file(sdk) != original_hash:
            raise runner_error("the restored SDK failed hash verification")

    try:
        if backup.exists():
            if sha256_file(backup) != original_hash:
                raise runner_error("the SDK backup changed before cleanup")
            backup.unlink()
        state_file.unlink()
    except OSError as exc:
        raise runner_error("the SDK was restored, but staging cleanup failed: %s" % exc)
    return True


def _sdk_name(artifact):
    artifact = Path(artifact)
    suffix = artifact.suffix.lower()
    if suffix not in SDK_NAMES:
        raise runner_error("the emulator artifact must be a Windows .dll or native Linux .so")
    return SDK_NAMES[suffix]


def discover_sdk(game_root, artifact_path):
    root = Path(game_root).resolve()
    if not root.is_dir():
        raise runner_error("the game root is not a directory: %s" % root)
    expected = _sdk_name(artifact_path).lower()
    candidates = sorted(
        path.resolve() for path in root.rglob("*")
        if path.is_file() and path.name.lower() == expected
    )
    if not candidates:
        raise runner_error("no %s was found under %s" % (_sdk_name(artifact_path), root))
    if len(candidates) != 1:
        names = "\n  ".join(str(path) for path in candidates)
        raise runner_error("multiple EOS SDKs were found; select one with --sdk:\n  %s" % names)
    return candidates[0]


def _inside(root, path):
    try:
        return os.path.commonpath([str(root), str(path)]) == str(root)
    except ValueError:
        return False


def _resolve_sdk(game_root, artifact, explicit):
    root = Path(game_root).resolve()
    sdk = Path(explicit).resolve() if explicit else discover_sdk(root, artifact)
    if not _inside(root, sdk):
        raise runner_error("the selected SDK is outside the game root")
    if sdk.suffix.lower() != Path(artifact).suffix.lower():
        raise runner_error("the game SDK and emulator artifact target different platforms")
    return sdk


def default_data_dir(game_root):
    return Path(game_root).resolve() / ".eosr-alpha"


def create_run_dir(runs_dir):
    runs = Path(runs_dir).resolve()
    runs.mkdir(parents=True, exist_ok=True)
    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    for _ in range(32):
        run_id = "run-%s-%d-%s" % (stamp, os.getpid(), secrets.token_hex(2))
        path = runs / run_id
        try:
            path.mkdir(mode=0o700)
            return path
        except FileExistsError:
            continue
    raise runner_error("could not allocate a unique run directory")


def _absolute_path(value):
    if not isinstance(value, str) or not value:
        return False
    return value.startswith("/") or value.startswith("\\\\") or bool(
        re.match(r"^[A-Za-z]:[\\/]", value)
    )


def _basename(value):
    if re.match(r"^[A-Za-z]:[\\/]", value) or value.startswith("\\\\"):
        return PureWindowsPath(value).name
    return Path(value).name


def _sanitize_owned(value):
    if isinstance(value, dict):
        clean = {}
        for key, child in value.items():
            if str(key).lower() in ("display_name", "username", "message", "error"):
                continue
            clean[key] = _sanitize_owned(child)
        return clean
    if isinstance(value, list):
        return [_sanitize_owned(child) for child in value]
    if isinstance(value, str) and _absolute_path(value):
        return _basename(value)
    return value


def _copy_trace(source, target):
    records = 0
    torn = 0
    digest = hashlib.sha256()
    with Path(source).open("rb") as src, Path(target).open("xb") as dst:
        while True:
            line = src.readline(MAX_TRACE_LINE_BYTES + 1)
            if not line:
                break
            if len(line) > MAX_TRACE_LINE_BYTES:
                raise runner_error("%s contains an oversized trace record" % Path(source).name)
            if not line.endswith(b"\n"):
                torn += 1
                break
            try:
                record = json.loads(line.decode("utf-8"))
            except (UnicodeError, ValueError):
                raise runner_error("%s contains an invalid complete trace record" % Path(source).name)
            if not isinstance(record, dict):
                raise runner_error("%s contains a non-object trace record" % Path(source).name)
            dst.write(line)
            digest.update(line)
            records += 1
        dst.flush()
        os.fsync(dst.fileno())
    return {"name": Path(source).name, "records": records, "sha256": digest.hexdigest()}, torn


def _trace_sort_key(path):
    if path.name == "trace.jsonl":
        return (0, 0)
    match = re.match(r"^trace\.(\d+)\.jsonl$", path.name)
    return (1, int(match.group(1))) if match else (2, path.name)


def create_bundle(run_dir, bundle_dir):
    run = Path(run_dir).resolve()
    bundle = Path(bundle_dir).resolve()
    if not run.is_dir():
        raise runner_error("the run directory does not exist: %s" % run)
    try:
        bundle.mkdir(mode=0o700)
    except FileExistsError:
        raise runner_error("the bundle path already exists: %s" % bundle)

    try:
        owned = {}
        invalid_owned = []
        for filename, key in (
            ("runtime.json", "runtime"),
            ("launch.json", "launch"),
            ("inspection.json", "inspection"),
        ):
            path = run / filename
            if not path.exists():
                owned[key] = None
                continue
            try:
                owned[key] = _sanitize_owned(_read_json(path))
            except runner_error:
                owned[key] = None
                invalid_owned.append(filename)

        trace_files = []
        torn = 0
        sources = [path for path in run.iterdir() if re.match(r"^trace(?:\.\d+)?\.jsonl$", path.name)]
        for source in sorted(sources, key=_trace_sort_key):
            record, dropped = _copy_trace(source, bundle / source.name)
            trace_files.append(record)
            torn += dropped

        stdout = run / "stdout.log"
        stdout_info = {
            "included": False,
            "bytes": stdout.stat().st_size if stdout.is_file() else 0,
            "sha256": sha256_file(stdout) if stdout.is_file() else None,
        }
        summary = {
            "schema_version": SCHEMA_VERSION,
            "generated_utc": datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
            "runtime": owned["runtime"],
            "launch": owned["launch"],
            "inspection": owned["inspection"],
            "invalid_owned_files": invalid_owned,
            "stdout": stdout_info,
            "trace": {
                "files": trace_files,
                "torn_tail_records_dropped": torn,
            },
        }
        _write_new_json(bundle / "summary.json", summary, 0o600)
    except Exception:
        shutil.rmtree(str(bundle), ignore_errors=True)
        raise
    return bundle


def run_inspection(game_root, artifact, sdk, output):
    command = [
        sys.executable,
        str(Path(__file__).with_name("inspect_game.py")),
        str(game_root),
        "--our-library", str(artifact),
        "--out", str(output),
    ]
    try:
        with Path(sdk).open("rb") as stream:
            is_pe = stream.read(2) == b"MZ"
        if is_pe:
            command.extend(("--reference-sdk", str(sdk)))
        result = subprocess.run(command, cwd=str(Path(__file__).resolve().parents[1]))
    except OSError as exc:
        raise runner_error("could not run the import census: %s" % exc)
    if result.returncode:
        raise runner_error("the import census found a loader-fatal gap; staging was not attempted")


def _manual_instructions(data_dir, run_dir, trace_level):
    values = {
        "EOSR_DATA_DIR": str(data_dir),
        "EOSR_RUN_DIR": str(run_dir),
        "EOSR_TRACE": trace_level,
    }
    print("\nSet these variables in the process that launches the game:")
    for key in sorted(values):
        print("  %s=%s" % (key, values[key]))
    print("\nSteam launch options (Linux/Proton):")
    print("  %s %%command%%" % " ".join(
        "%s=%s" % (key, shlex.quote(values[key])) for key in sorted(values)
    ))
    print("\nKeep this runner open. Launch and fully exit the game before continuing.")


def _environment_path(path, cwd):
    path = Path(path).resolve()
    cwd = Path(cwd).resolve()
    return os.path.relpath(str(path), str(cwd)) if _inside(cwd, path) else str(path)


def _launch_record(sdk, staged_hash, original_hash, command, cwd, exit_code, status, restored):
    argv = ["<manual>"] if not command else [_basename(command[0])]
    if command and len(command) > 1:
        argv.append("<arguments-redacted>")
    return {
        "schema_version": SCHEMA_VERSION,
        "artifact": {
            "path": str(sdk),
            "sha256": staged_hash,
            "replaced_original_sha256": original_hash,
            "restored": restored,
        },
        "launch": {
            "argv_redacted": argv,
            "cwd": str(cwd),
            "exit_code": exit_code,
            "status": status,
        },
    }


def run_alpha(args):
    game_root = Path(args.game_root).resolve()
    artifact = Path(args.artifact).resolve()
    if not game_root.is_dir() or not artifact.is_file():
        raise runner_error("the game root and emulator artifact must exist")
    sdk = _resolve_sdk(game_root, artifact, args.sdk)
    data_dir = Path(args.data_dir).resolve() if args.data_dir else default_data_dir(game_root).resolve()
    runs_dir = Path(args.runs_dir).resolve() if args.runs_dir else data_dir / "runs"
    cwd = Path(args.cwd).resolve() if args.cwd else game_root
    if not cwd.is_dir():
        raise runner_error("the launch working directory does not exist")
    command = list(args.command)
    if command and command[0] == "--":
        command.pop(0)
    if not args.manual and not command:
        raise runner_error("give the game command after --, or use --manual")
    if args.manual and command:
        raise runner_error("--manual and an explicit launch command are mutually exclusive")
    if args.instance_label and (
        not re.match(r"^[A-Za-z0-9._-]{1,32}$", args.instance_label) or
        args.instance_label in (".", "..")
    ):
        raise runner_error("--instance-label must be a 1-32 character path-safe label")

    data_dir.mkdir(parents=True, exist_ok=True, mode=0o700)
    run_dir = create_run_dir(runs_dir)
    stdout_path = run_dir / "stdout.log"
    with stdout_path.open("xb"):
        pass
    if not args.skip_inspection:
        run_inspection(game_root, artifact, sdk, run_dir / "inspection.json")

    original_hash = sha256_file(sdk)
    staged_hash = sha256_file(artifact)
    status = "not_started"
    exit_code = None
    restored = False
    start_error = None
    interrupted = False
    process = None
    try:
        stage_sdk(sdk, artifact)
    except runner_error as stage_problem:
        if state_path(sdk).exists():
            try:
                restore_sdk(sdk)
            except runner_error as restore_problem:
                raise runner_error(
                    "%s; automatic recovery also failed: %s" % (stage_problem, restore_problem)
                )
        raise
    restore_problem = None
    try:
        environment = os.environ.copy()
        environment["EOSR_DATA_DIR"] = _environment_path(data_dir, cwd)
        environment["EOSR_RUN_DIR"] = _environment_path(run_dir, cwd)
        environment["EOSR_TRACE"] = args.trace_level
        if args.instance_label:
            environment["EOSR_INSTANCE_LABEL"] = args.instance_label

        if args.manual:
            status = "manual"
            _manual_instructions(
                environment["EOSR_DATA_DIR"], environment["EOSR_RUN_DIR"], args.trace_level
            )
            try:
                input("Press Enter after the game has fully exited to restore the original SDK: ")
                status = "manual_complete"
            except (EOFError, KeyboardInterrupt):
                status = "interrupted"
                interrupted = True
        else:
            try:
                with stdout_path.open("ab", buffering=0) as output:
                    process = subprocess.Popen(
                        command,
                        cwd=str(cwd),
                        env=environment,
                        stdout=output,
                        stderr=subprocess.STDOUT,
                        shell=False,
                    )
                    status = "running"
                    try:
                        exit_code = process.wait()
                        status = "exited"
                    except KeyboardInterrupt:
                        interrupted = True
                        status = "interrupted"
                        process.terminate()
                        try:
                            exit_code = process.wait(timeout=10)
                        except subprocess.TimeoutExpired:
                            process.kill()
                            exit_code = process.wait()
            except OSError as exc:
                status = "start_failed"
                start_error = exc
    finally:
        try:
            restore_sdk(sdk)
            restored = True
        except runner_error as exc:
            restore_problem = exc

    launch = _launch_record(
        sdk, staged_hash, original_hash, command, cwd, exit_code, status, restored
    )
    _write_new_json(run_dir / "launch.json", launch)
    bundle = None
    if not args.no_bundle:
        bundle = create_bundle(run_dir, args.bundle_dir or (str(run_dir) + "-bundle"))

    print("run directory    : %s" % run_dir)
    if bundle:
        print("shareable bundle: %s" % bundle)
    if restore_problem:
        raise runner_error(
            "%s; recovery state was kept, so retry: %s restore --sdk %s" % (
                restore_problem,
                Path(__file__).name,
                shlex.quote(str(sdk)),
            )
        )
    if start_error:
        raise runner_error("the game command could not start: %s" % start_error)
    if interrupted:
        return 130
    return exit_code if isinstance(exit_code, int) else 0


def _build_parser():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="operation", required=True)

    run = sub.add_parser("run", help="stage, launch, restore, and bundle one alpha run")
    run.add_argument("game_root", help="game installation directory")
    run.add_argument("--artifact", required=True, help="built EOS Reimagined .dll or .so")
    run.add_argument("--sdk", help="game SDK path; auto-detected when exactly one exists")
    run.add_argument("--data-dir", help="persistent profile/config directory for this tester")
    run.add_argument("--runs-dir", help="parent directory for run folders")
    run.add_argument("--cwd", help="game command working directory (default: game root)")
    run.add_argument("--trace-level", choices=TRACE_LEVELS, default="full")
    run.add_argument("--instance-label")
    run.add_argument("--skip-inspection", action="store_true")
    run.add_argument("--no-bundle", action="store_true")
    run.add_argument("--bundle-dir")
    run.add_argument("--manual", action="store_true", help="print launch settings and wait")

    bundle = sub.add_parser("bundle", help="sanitize an existing run directory")
    bundle.add_argument("run_dir")
    bundle.add_argument("--out")

    restore = sub.add_parser("restore", help="recover the original SDK after an interrupted run")
    restore.add_argument("--sdk", required=True)
    restore.add_argument("--run-dir", help="also build a sanitized bundle after restoration")
    restore.add_argument("--bundle-dir")
    return parser


def main(argv=None):
    values = list(sys.argv[1:] if argv is None else argv)
    command = []
    if values and values[0] == "run" and "--" in values:
        separator = values.index("--")
        command = values[separator + 1:]
        values = values[:separator]
    args = _build_parser().parse_args(values)
    if args.operation == "run":
        args.command = command
    try:
        if args.operation == "run":
            result = run_alpha(args)
            return result if 0 <= result <= 255 else 1
        if args.operation == "bundle":
            run = Path(args.run_dir).resolve()
            bundle = create_bundle(run, args.out or (str(run) + "-bundle"))
            print("shareable bundle: %s" % bundle)
            return 0
        restore_sdk(args.sdk)
        print("restored original SDK: %s" % Path(args.sdk).resolve())
        if args.run_dir:
            run = Path(args.run_dir).resolve()
            bundle = create_bundle(run, args.bundle_dir or (str(run) + "-bundle"))
            print("shareable bundle: %s" % bundle)
        return 0
    except (runner_error, OSError) as exc:
        print("alpha_runner: %s" % exc, file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
