#!/usr/bin/env python3
"""Build and verify the two deterministic EOS Reimagined alpha release archives."""

import argparse
import gzip
import hashlib
import io
import json
from pathlib import Path, PurePosixPath
import re
import shutil
import sys
import tarfile
import zipfile


ROOT = Path(__file__).resolve().parents[1]
SCHEMA_VERSION = 1
VERSION_RE = re.compile(r"^v[0-9]+\.[0-9]+\.[0-9]+-alpha\.[0-9]+$")
REVISION_RE = re.compile(r"^[0-9a-f]{40}$")
MAX_MANIFEST_BYTES = 1024 * 1024

COMMON_FILES = (
    ("README.md", ROOT / "README.md", 0o644),
    ("LICENSE", ROOT / "LICENSE", 0o644),
    ("QUICKSTART.md", ROOT / "release" / "QUICKSTART.md", 0o644),
    ("tools/alpha_runner.py", ROOT / "tools" / "alpha_runner.py", 0o755),
    ("tools/inspect_game.py", ROOT / "tools" / "inspect_game.py", 0o755),
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


def sha256_file(path):
    digest = hashlib.sha256()
    with Path(path).open("rb") as stream:
        while True:
            block = stream.read(1024 * 1024)
            if not block:
                break
            digest.update(block)
    return digest.hexdigest()


def _json_bytes(value):
    return (json.dumps(value, indent=2, sort_keys=True) + "\n").encode("utf-8")


def _write_new(path, data):
    try:
        with Path(path).open("xb") as stream:
            stream.write(data)
    except OSError as exc:
        raise package_error("cannot write %s: %s" % (Path(path).name, exc))


def _validate_inputs(version, revision, linux, windows):
    if not VERSION_RE.fullmatch(version):
        raise package_error("version must match vMAJOR.MINOR.PATCH-alpha.NUMBER")
    if not REVISION_RE.fullmatch(revision):
        raise package_error("revision must be a full 40-character lowercase Git SHA")
    for path, expected in (
        (Path(linux), "libEOSSDK-Linux-Shipping.so"),
        (Path(windows), "EOSSDK-Win64-Shipping.dll"),
    ):
        if not path.is_file() or path.name != expected:
            raise package_error("missing canonical release artifact: %s" % expected)
    for _, path, _ in COMMON_FILES:
        if not path.is_file():
            raise package_error("release input is missing: %s" % path.relative_to(ROOT))


def _release_record(version, revision, platform, artifact):
    return {
        "schema_version": SCHEMA_VERSION,
        "version": version,
        "commit": revision,
        "platform": platform,
        "architecture": "x86_64",
        "artifact": {
            "name": artifact.name,
            "bytes": artifact.stat().st_size,
            "sha256": sha256_file(artifact),
        },
    }


def _entries(version, revision, platform, artifact):
    release = _release_record(version, revision, platform, artifact)
    values = list(COMMON_FILES)
    values.append((artifact.name, artifact, 0o755 if platform == "linux" else 0o644))
    values.append(("RELEASE.json", _json_bytes(release), 0o644))
    return sorted(values, key=lambda value: value[0])


def _entry_size(source):
    return len(source) if isinstance(source, bytes) else source.stat().st_size


def _entry_stream(source):
    return io.BytesIO(source) if isinstance(source, bytes) else source.open("rb")


def _write_tar(path, root_name, entries):
    try:
        with Path(path).open("xb") as raw:
            with gzip.GzipFile(filename="", mode="wb", fileobj=raw, mtime=0) as compressed:
                with tarfile.open(fileobj=compressed, mode="w", format=tarfile.PAX_FORMAT) as archive:
                    for relative, source, mode in entries:
                        info = tarfile.TarInfo("%s/%s" % (root_name, relative))
                        info.size = _entry_size(source)
                        info.mode = mode
                        info.mtime = 0
                        info.uid = 0
                        info.gid = 0
                        info.uname = ""
                        info.gname = ""
                        with _entry_stream(source) as stream:
                            archive.addfile(info, stream)
    except (OSError, tarfile.TarError) as exc:
        raise package_error("cannot build %s: %s" % (Path(path).name, exc))


def _write_zip(path, root_name, entries):
    try:
        with zipfile.ZipFile(path, "x", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
            for relative, source, mode in entries:
                info = zipfile.ZipInfo("%s/%s" % (root_name, relative), (1980, 1, 1, 0, 0, 0))
                info.compress_type = zipfile.ZIP_DEFLATED
                info.create_system = 3
                info.external_attr = (mode & 0xFFFF) << 16
                with archive.open(info, "w", force_zip64=True) as target:
                    with _entry_stream(source) as stream:
                        shutil.copyfileobj(stream, target, 1024 * 1024)
    except (OSError, zipfile.BadZipFile) as exc:
        raise package_error("cannot build %s: %s" % (Path(path).name, exc))


def build_release(version, revision, linux_path, windows_path, output_dir):
    linux = Path(linux_path).resolve()
    windows = Path(windows_path).resolve()
    _validate_inputs(version, revision, linux, windows)
    output = Path(output_dir).resolve()
    try:
        output.mkdir(parents=True)
    except FileExistsError:
        raise package_error("output directory already exists: %s" % output)
    except OSError as exc:
        raise package_error("cannot create output directory: %s" % exc)

    linux_root = "eos-reimagined-%s-linux-x86_64" % version
    windows_root = "eos-reimagined-%s-windows-x86_64" % version
    linux_archive = output / (linux_root + ".tar.gz")
    windows_archive = output / (windows_root + ".zip")
    try:
        _write_tar(linux_archive, linux_root, _entries(version, revision, "linux", linux))
        _write_zip(windows_archive, windows_root, _entries(version, revision, "windows", windows))
        assets = []
        for archive, platform in ((linux_archive, "linux"), (windows_archive, "windows")):
            assets.append({
                "name": archive.name,
                "platform": platform,
                "bytes": archive.stat().st_size,
                "sha256": sha256_file(archive),
            })
        manifest = {
            "schema_version": SCHEMA_VERSION,
            "version": version,
            "commit": revision,
            "assets": assets,
        }
        _write_new(output / "release-manifest.json", _json_bytes(manifest))
        sums = "".join("%s  %s\n" % (asset["sha256"], asset["name"]) for asset in assets)
        _write_new(output / "SHA256SUMS.txt", sums.encode("ascii"))
        verify_release(output)
    except Exception:
        shutil.rmtree(str(output), ignore_errors=True)
        raise
    return output


def _safe_archive_name(name, root):
    path = PurePosixPath(name)
    return (
        not path.is_absolute() and
        ".." not in path.parts and
        len(path.parts) >= 2 and
        path.parts[0] == root
    )


def _expected_members(root, artifact):
    values = {"%s/%s" % (root, relative) for relative, _, _ in COMMON_FILES}
    values.add("%s/%s" % (root, artifact))
    values.add("%s/RELEASE.json" % root)
    return values


def _verify_release_record(data, manifest, platform, artifact_name, artifact_data):
    try:
        record = json.loads(data.decode("utf-8"))
    except (UnicodeError, ValueError) as exc:
        raise package_error("invalid embedded RELEASE.json: %s" % exc)
    artifact = record.get("artifact") if isinstance(record, dict) else None
    expected_hash = hashlib.sha256(artifact_data).hexdigest()
    if (
        not isinstance(record, dict) or
        not isinstance(artifact, dict) or
        record.get("schema_version") != SCHEMA_VERSION or
        record.get("version") != manifest["version"] or
        record.get("commit") != manifest["commit"] or
        record.get("platform") != platform or
        record.get("architecture") != "x86_64" or
        artifact.get("name") != artifact_name or
        artifact.get("bytes") != len(artifact_data) or
        artifact.get("sha256") != expected_hash
    ):
        raise package_error("embedded release metadata does not match its archive")


def _verify_tar(path, root, manifest):
    expected = _expected_members(root, "libEOSSDK-Linux-Shipping.so")
    try:
        with tarfile.open(path, "r:gz") as archive:
            members = archive.getmembers()
            names = {member.name for member in members}
            if (
                len(members) != len(expected) or
                names != expected or
                any(not member.isfile() for member in members)
            ):
                raise package_error("Linux archive contents do not match the release contract")
            if any(not _safe_archive_name(member.name, root) for member in members):
                raise package_error("Linux archive contains an unsafe path")
            artifact = archive.extractfile(root + "/libEOSSDK-Linux-Shipping.so").read()
            release = archive.extractfile(root + "/RELEASE.json").read()
    except (OSError, tarfile.TarError, KeyError) as exc:
        raise package_error("cannot verify Linux archive: %s" % exc)
    _verify_release_record(release, manifest, "linux", "libEOSSDK-Linux-Shipping.so", artifact)


def _verify_zip(path, root, manifest):
    expected = _expected_members(root, "EOSSDK-Win64-Shipping.dll")
    try:
        with zipfile.ZipFile(path) as archive:
            entries = archive.infolist()
            names = {entry.filename for entry in entries}
            if len(entries) != len(expected) or names != expected:
                raise package_error("Windows archive contents do not match the release contract")
            if any(not _safe_archive_name(name, root) for name in names):
                raise package_error("Windows archive contains an unsafe path")
            artifact = archive.read(root + "/EOSSDK-Win64-Shipping.dll")
            release = archive.read(root + "/RELEASE.json")
    except (OSError, zipfile.BadZipFile, KeyError) as exc:
        raise package_error("cannot verify Windows archive: %s" % exc)
    _verify_release_record(release, manifest, "windows", "EOSSDK-Win64-Shipping.dll", artifact)


def verify_release(output_dir):
    output = Path(output_dir).resolve()
    manifest_path = output / "release-manifest.json"
    try:
        if manifest_path.stat().st_size > MAX_MANIFEST_BYTES:
            raise package_error("release manifest is oversized")
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, ValueError) as exc:
        raise package_error("cannot read release manifest: %s" % exc)
    if (
        not isinstance(manifest, dict) or
        manifest.get("schema_version") != SCHEMA_VERSION or
        not VERSION_RE.fullmatch(str(manifest.get("version", ""))) or
        not REVISION_RE.fullmatch(str(manifest.get("commit", ""))) or
        not isinstance(manifest.get("assets"), list) or
        len(manifest["assets"]) != 2
    ):
        raise package_error("release manifest does not match schema version 1")

    expected_sums = []
    platforms = set()
    for asset in manifest["assets"]:
        if not isinstance(asset, dict):
            raise package_error("release manifest contains an invalid asset")
        name = asset.get("name", "")
        path = output / name
        if Path(name).name != name or not path.is_file():
            raise package_error("release asset is missing or has an unsafe name")
        if asset.get("bytes") != path.stat().st_size or asset.get("sha256") != sha256_file(path):
            raise package_error("release asset failed size or SHA-256 verification: %s" % name)
        expected_sums.append("%s  %s" % (asset["sha256"], name))
        platforms.add(asset.get("platform"))
    if platforms != {"linux", "windows"}:
        raise package_error("release manifest must contain one Linux and one Windows asset")

    try:
        sums = (output / "SHA256SUMS.txt").read_text(encoding="ascii").splitlines()
    except (OSError, UnicodeError) as exc:
        raise package_error("cannot read SHA256SUMS.txt: %s" % exc)
    if sums != expected_sums:
        raise package_error("SHA256SUMS.txt does not match the release manifest")

    version = manifest["version"]
    linux_root = "eos-reimagined-%s-linux-x86_64" % version
    windows_root = "eos-reimagined-%s-windows-x86_64" % version
    _verify_tar(output / (linux_root + ".tar.gz"), linux_root, manifest)
    _verify_zip(output / (windows_root + ".zip"), windows_root, manifest)
    return manifest


def _build_parser():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="operation", required=True)
    build = sub.add_parser("build")
    build.add_argument("--version", required=True)
    build.add_argument("--revision", required=True)
    build.add_argument("--linux", required=True)
    build.add_argument("--windows", required=True)
    build.add_argument("--out", required=True)
    verify = sub.add_parser("verify")
    verify.add_argument("directory")
    return parser


def main(argv=None):
    args = _build_parser().parse_args(argv)
    try:
        if args.operation == "build":
            output = build_release(
                args.version, args.revision, args.linux, args.windows, args.out
            )
            print("release packages: %s" % output)
        else:
            verify_release(args.directory)
            print("release packages verified: %s" % Path(args.directory).resolve())
        return 0
    except package_error as exc:
        print("package_release: %s" % exc, file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
