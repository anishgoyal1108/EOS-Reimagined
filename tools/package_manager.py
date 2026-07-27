#!/usr/bin/env python3
"""Build and verify deterministic, self-contained manager archives."""

import argparse
import hashlib
import io
import json
from pathlib import Path, PurePosixPath
import shutil
import struct
import sys
import tarfile
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parent))
import package_release


ROOT = Path(__file__).resolve().parents[1]
SCHEMA_VERSION = 1
MAX_MANIFEST_BYTES = 1024 * 1024

COMMON_FILES = (
    ("README.md", ROOT / "README.md", 0o644),
    ("LICENSE", ROOT / "LICENSE", 0o644),
    ("QUICKSTART.md", ROOT / "release" / "MANAGER-QUICKSTART.md", 0o644),
    ("licenses/fltk-COPYING", ROOT / "third_party" / "fltk" / "COPYING", 0o644),
    ("licenses/fltk-UPSTREAM.md", ROOT / "third_party" / "fltk" / "UPSTREAM.md", 0o644),
    (
        "licenses/monocypher-LICENSE.md",
        ROOT / "third_party" / "monocypher" / "LICENCE.md",
        0o644,
    ),
    (
        "licenses/monocypher-UPSTREAM.md",
        ROOT / "third_party" / "monocypher" / "UPSTREAM.md",
        0o644,
    ),
)


class package_error(Exception):
    pass


def _read(source):
    return source if isinstance(source, bytes) else Path(source).read_bytes()


def _file_record(relative, source, mode):
    data = _read(source)
    return {
        "path": relative,
        "bytes": len(data),
        "sha256": hashlib.sha256(data).hexdigest(),
        "mode": "%04o" % mode,
    }


def _verify_elf_has_no_runtime_path(data):
    if not data.startswith(b"\x7fELF"):
        return
    if len(data) < 64 or data[4] != 2 or data[5] not in (1, 2):
        raise package_error("Linux manager is not a valid ELF64 executable")
    endian = "<" if data[5] == 1 else ">"
    try:
        header = struct.unpack_from(endian + "HHIQQQIHHHHHH", data, 16)
    except struct.error as exc:
        raise package_error("Linux manager has a truncated ELF header") from exc
    if header[1] != 62 or header[2] != 1:
        raise package_error("Linux manager is not an x86_64 ELF executable")
    program_offset = header[4]
    program_size = header[8]
    program_count = header[9]
    if program_size < 56 or program_count > 1024 or \
       program_offset + program_size * program_count > len(data):
        raise package_error("Linux manager has an invalid ELF program table")
    for index in range(program_count):
        offset = program_offset + index * program_size
        try:
            program = struct.unpack_from(endian + "IIQQQQQQ", data, offset)
        except struct.error as exc:
            raise package_error("Linux manager has a truncated ELF program entry") from exc
        if program[0] != 2:  # PT_DYNAMIC
            continue
        dynamic_offset = program[2]
        dynamic_size = program[5]
        if dynamic_size % 16 != 0 or dynamic_offset + dynamic_size > len(data):
            raise package_error("Linux manager has an invalid ELF dynamic table")
        for entry in range(dynamic_offset, dynamic_offset + dynamic_size, 16):
            tag, _ = struct.unpack_from(endian + "qQ", data, entry)
            if tag in (15, 29):  # DT_RPATH / DT_RUNPATH
                raise package_error("Linux manager contains a runtime library search path")
            if tag == 0:  # DT_NULL
                break


def _validate_inputs(version, revision, linux_manager, windows_manager, linux_sdk, windows_sdk):
    if not package_release.VERSION_RE.fullmatch(version):
        raise package_error("version must match vMAJOR.MINOR.PATCH-alpha.NUMBER")
    if not package_release.REVISION_RE.fullmatch(revision):
        raise package_error("revision must be a full 40-character lowercase Git SHA")
    expected = (
        (linux_manager, "eos-reimagined-manager"),
        (windows_manager, "EOSReimaginedManager.exe"),
        (linux_sdk, "libEOSSDK-Linux-Shipping.so"),
        (windows_sdk, "EOSSDK-Win64-Shipping.dll"),
    )
    for path, name in expected:
        path = Path(path)
        if not path.is_file() or path.name != name:
            raise package_error("missing canonical manager input: %s" % name)
    _verify_elf_has_no_runtime_path(Path(linux_manager).read_bytes())
    for _, path, _ in COMMON_FILES:
        if not path.is_file():
            raise package_error("manager package input is missing: %s" % path.relative_to(ROOT))


def _payload(platform, linux_manager, windows_manager, linux_sdk, windows_sdk):
    values = list(COMMON_FILES)
    if platform == "linux":
        values.extend((
            ("eos-reimagined-manager", linux_manager, 0o755),
            ("artifacts/libEOSSDK-Linux-Shipping.so", linux_sdk, 0o755),
            ("artifacts/EOSSDK-Win64-Shipping.dll", windows_sdk, 0o644),
        ))
    else:
        values.extend((
            ("EOSReimaginedManager.exe", windows_manager, 0o755),
            ("artifacts/EOSSDK-Win64-Shipping.dll", windows_sdk, 0o644),
        ))
    return sorted(values, key=lambda value: value[0])


def _required_modes(platform):
    modes = {relative: mode for relative, _, mode in COMMON_FILES}
    if platform == "linux":
        modes.update({
            "eos-reimagined-manager": 0o755,
            "artifacts/libEOSSDK-Linux-Shipping.so": 0o755,
            "artifacts/EOSSDK-Win64-Shipping.dll": 0o644,
        })
    elif platform == "windows":
        modes.update({
            "EOSReimaginedManager.exe": 0o755,
            "artifacts/EOSSDK-Win64-Shipping.dll": 0o644,
        })
    else:
        raise package_error("unsupported manager package platform")
    return modes


def _entries(version, revision, platform, linux_manager, windows_manager, linux_sdk, windows_sdk):
    payload = _payload(platform, linux_manager, windows_manager, linux_sdk, windows_sdk)
    manifest = {
        "schema_version": SCHEMA_VERSION,
        "version": version,
        "commit": revision,
        "platform": platform,
        "architecture": "x86_64",
        "files": [_file_record(*value) for value in payload],
    }
    values = list(payload)
    values.append(("MANIFEST.json", package_release._json_bytes(manifest), 0o644))
    return sorted(values, key=lambda value: value[0])


def build_manager_release(version, revision, linux_manager_path, windows_manager_path,
                          linux_sdk_path, windows_sdk_path, output_dir):
    linux_manager = Path(linux_manager_path).resolve()
    windows_manager = Path(windows_manager_path).resolve()
    linux_sdk = Path(linux_sdk_path).resolve()
    windows_sdk = Path(windows_sdk_path).resolve()
    _validate_inputs(version, revision, linux_manager, windows_manager, linux_sdk, windows_sdk)

    output = Path(output_dir).resolve()
    try:
        output.mkdir(parents=True)
    except FileExistsError:
        raise package_error("output directory already exists: %s" % output)
    except OSError as exc:
        raise package_error("cannot create output directory: %s" % exc)

    linux_root = "eos-reimagined-manager-%s-linux-x86_64" % version
    windows_root = "eos-reimagined-manager-%s-windows-x86_64" % version
    linux_archive = output / (linux_root + ".tar.gz")
    windows_archive = output / (windows_root + ".zip")
    try:
        package_release._write_tar(
            linux_archive, linux_root,
            _entries(version, revision, "linux", linux_manager, windows_manager,
                     linux_sdk, windows_sdk),
        )
        package_release._write_zip(
            windows_archive, windows_root,
            _entries(version, revision, "windows", linux_manager, windows_manager,
                     linux_sdk, windows_sdk),
        )
        assets = []
        for archive, platform in ((linux_archive, "linux"), (windows_archive, "windows")):
            assets.append({
                "name": archive.name,
                "platform": platform,
                "bytes": archive.stat().st_size,
                "sha256": package_release.sha256_file(archive),
            })
        release = {
            "schema_version": SCHEMA_VERSION,
            "version": version,
            "commit": revision,
            "assets": assets,
        }
        package_release._write_new(
            output / "manager-release-manifest.json", package_release._json_bytes(release)
        )
        sums = "".join("%s  %s\n" % (item["sha256"], item["name"]) for item in assets)
        package_release._write_new(output / "SHA256SUMS.txt", sums.encode("ascii"))
        verify_manager_release(output)
    except Exception:
        shutil.rmtree(str(output), ignore_errors=True)
        raise
    return output


def _safe_name(name, root):
    path = PurePosixPath(name)
    return (
        not path.is_absolute() and
        ".." not in path.parts and
        len(path.parts) >= 2 and
        path.parts[0] == root
    )


def _verify_embedded(files, manifest_bytes, platform, archive_modes=None):
    if len(manifest_bytes) > MAX_MANIFEST_BYTES:
        raise package_error("embedded manager manifest is oversized")
    try:
        manifest = json.loads(manifest_bytes.decode("utf-8"))
    except (UnicodeError, ValueError) as exc:
        raise package_error("embedded manager manifest is invalid: %s" % exc)
    records = manifest.get("files") if isinstance(manifest, dict) else None
    if (
        manifest.get("schema_version") != SCHEMA_VERSION or
        manifest.get("platform") != platform or
        manifest.get("architecture") != "x86_64" or
        not package_release.VERSION_RE.fullmatch(str(manifest.get("version", ""))) or
        not package_release.REVISION_RE.fullmatch(str(manifest.get("commit", ""))) or
        not isinstance(records, list)
    ):
        raise package_error("embedded manager manifest does not match schema version 1")

    required_modes = _required_modes(platform)
    expected_paths = set(required_modes)
    if set(files) != expected_paths:
        raise package_error("manager package membership does not match the release contract")
    if archive_modes is not None and (
        set(archive_modes) != expected_paths or
        any(archive_modes[path] != required_modes[path] for path in expected_paths)
    ):
        raise package_error("manager package modes do not match the release contract")
    seen = set()
    for record in records:
        if not isinstance(record, dict) or not isinstance(record.get("path"), str):
            raise package_error("embedded manager manifest contains an invalid file record")
        path = record["path"]
        if path in seen or path not in files:
            raise package_error("embedded manager manifest membership does not match the archive")
        seen.add(path)
        data = files[path]
        if (
            record.get("bytes") != len(data) or
            record.get("sha256") != hashlib.sha256(data).hexdigest() or
            record.get("mode") != "%04o" % required_modes[path]
        ):
            raise package_error("manager package member failed hash verification: %s" % path)
    if seen != expected_paths:
        raise package_error("embedded manager manifest omits an archive member")
    if platform == "linux":
        manager = files.get("eos-reimagined-manager")
        if manager is None:
            raise package_error("Linux manager archive omits its executable")
        _verify_elf_has_no_runtime_path(manager)
    return manifest


def _verify_tar(path, root):
    try:
        with tarfile.open(path, "r:gz") as archive:
            members = archive.getmembers()
            if any(not member.isfile() or not _safe_name(member.name, root) for member in members):
                raise package_error("Linux manager archive contains an unsafe member")
            if len({member.name for member in members}) != len(members):
                raise package_error("Linux manager archive contains duplicate members")
            relative = {member.name[len(root) + 1:]: archive.extractfile(member).read()
                        for member in members}
            modes = {member.name[len(root) + 1:]: member.mode & 0o7777 for member in members}
    except (OSError, tarfile.TarError) as exc:
        raise package_error("cannot read Linux manager archive: %s" % exc)
    manifest = relative.pop("MANIFEST.json", None)
    if manifest is None:
        raise package_error("Linux manager archive has no manifest")
    modes.pop("MANIFEST.json", None)
    return _verify_embedded(relative, manifest, "linux", modes)


def _verify_zip(path, root):
    try:
        with zipfile.ZipFile(path) as archive:
            entries = archive.infolist()
            if any(not _safe_name(entry.filename, root) for entry in entries):
                raise package_error("Windows manager archive contains an unsafe member")
            if len({entry.filename for entry in entries}) != len(entries):
                raise package_error("Windows manager archive contains duplicate members")
            relative = {entry.filename[len(root) + 1:]: archive.read(entry) for entry in entries}
            modes = {entry.filename[len(root) + 1:]: (entry.external_attr >> 16) & 0o7777
                     for entry in entries}
    except (OSError, zipfile.BadZipFile) as exc:
        raise package_error("cannot read Windows manager archive: %s" % exc)
    manifest = relative.pop("MANIFEST.json", None)
    if manifest is None:
        raise package_error("Windows manager archive has no manifest")
    modes.pop("MANIFEST.json", None)
    return _verify_embedded(relative, manifest, "windows", modes)


def verify_manager_release(output_dir):
    output = Path(output_dir).resolve()
    try:
        release = json.loads((output / "manager-release-manifest.json").read_text("utf-8"))
    except (OSError, UnicodeError, ValueError) as exc:
        raise package_error("cannot read manager release manifest: %s" % exc)
    if (
        not isinstance(release, dict) or
        release.get("schema_version") != SCHEMA_VERSION or
        not package_release.VERSION_RE.fullmatch(str(release.get("version", ""))) or
        not package_release.REVISION_RE.fullmatch(str(release.get("commit", ""))) or
        not isinstance(release.get("assets"), list) or len(release["assets"]) != 2
    ):
        raise package_error("manager release manifest does not match schema version 1")

    sums = []
    platforms = set()
    for asset in release["assets"]:
        name = asset.get("name", "") if isinstance(asset, dict) else ""
        path = output / name
        if Path(name).name != name or not path.is_file():
            raise package_error("manager release asset is missing or unsafe")
        digest = package_release.sha256_file(path)
        if asset.get("bytes") != path.stat().st_size or asset.get("sha256") != digest:
            raise package_error("manager release asset failed verification: %s" % name)
        sums.append("%s  %s" % (digest, name))
        platforms.add(asset.get("platform"))
    if platforms != {"linux", "windows"}:
        raise package_error("manager release needs one Windows and one Linux package")
    try:
        actual_sums = (output / "SHA256SUMS.txt").read_text("ascii").splitlines()
    except (OSError, UnicodeError) as exc:
        raise package_error("cannot read manager SHA256SUMS.txt: %s" % exc)
    if actual_sums != sums:
        raise package_error("manager SHA256SUMS.txt does not match")

    version = release["version"]
    linux_root = "eos-reimagined-manager-%s-linux-x86_64" % version
    windows_root = "eos-reimagined-manager-%s-windows-x86_64" % version
    linux_manifest = _verify_tar(output / (linux_root + ".tar.gz"), linux_root)
    windows_manifest = _verify_zip(output / (windows_root + ".zip"), windows_root)
    if linux_manifest["version"] != version or windows_manifest["version"] != version:
        raise package_error("embedded manager version does not match the release")
    if linux_manifest["commit"] != release["commit"] or \
       windows_manifest["commit"] != release["commit"]:
        raise package_error("embedded manager revision does not match the release")
    return release


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    build = sub.add_parser("build")
    build.add_argument("--version", required=True)
    build.add_argument("--revision", required=True)
    build.add_argument("--linux-manager", required=True)
    build.add_argument("--windows-manager", required=True)
    build.add_argument("--linux-sdk", required=True)
    build.add_argument("--windows-sdk", required=True)
    build.add_argument("--output", required=True)
    verify = sub.add_parser("verify")
    verify.add_argument("output")
    args = parser.parse_args(argv)
    try:
        if args.command == "build":
            build_manager_release(args.version, args.revision, args.linux_manager,
                                  args.windows_manager, args.linux_sdk, args.windows_sdk,
                                  args.output)
        else:
            verify_manager_release(args.output)
    except (package_error, package_release.package_error) as exc:
        print("package_manager: %s" % exc, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
