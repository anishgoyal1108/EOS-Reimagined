#!/usr/bin/env python3
"""Run the real SDK through a manager-owned fake-game transaction."""

import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile
import unittest


def elf64(payload=b""):
    data = bytearray(4096)
    data[0:4] = b"\x7fELF"
    data[4] = 2
    data[5] = 1
    data[18:20] = (0x3E).to_bytes(2, "little")
    return bytes(data) + payload


class manager_loaded_sdk_workflow(unittest.TestCase):
    cli = None
    probe = None
    artifact = None

    def command(self, *args, expected=0):
        result = subprocess.run(
            [str(self.cli), *map(str, args)], check=False, capture_output=True, text=True
        )
        self.assertEqual(result.returncode, expected, result.stderr + result.stdout)
        return json.loads(result.stdout)

    def test_real_loaded_sdk_install_run_bundle_restore(self):
        with tempfile.TemporaryDirectory(prefix="eosr-manager-loaded-sdk-") as temporary:
            root = Path(temporary)
            game = root / "fake-game" / "plugins" / "x86_64"
            state = root / "manager" / "targets"
            data = root / "manager" / "instances" / "alice" / "data"
            run = data / "traces" / "run-manager-e2e"
            for directory in (game, state, data, run):
                directory.mkdir(parents=True, exist_ok=True)

            original = elf64(b"fixture original SDK")
            target = game / "libEOSSDK-Linux-Shipping.so"
            target.write_bytes(original)
            artifact_bytes = self.artifact.read_bytes()
            artifact_hash = hashlib.sha256(artifact_bytes).hexdigest()
            backup = Path(str(target) + ".eosr-original")
            journal = Path(str(target) + ".eosr-journal.json")
            descriptor = game / "eosr-bootstrap.json"
            record = state / "target.json"

            installed = self.command(
                "install", "12121212121212121212121212121212", "loaded-sdk-fixture",
                target, self.artifact, "linux", len(artifact_bytes), artifact_hash,
                backup, journal, descriptor, record, data,
            )
            self.assertEqual(installed["code"], "installed")
            self.assertEqual(hashlib.sha256(target.read_bytes()).hexdigest(), artifact_hash)
            self.assertEqual(backup.read_bytes(), original)

            loaded = subprocess.run(
                [str(self.probe), str(target), "manager", str(data), str(run)],
                check=False, capture_output=True, text=True,
            )
            self.assertEqual(loaded.returncode, 0, loaded.stderr + loaded.stdout)
            self.assertTrue((data / "profile.key").is_file())
            runtime = json.loads((run / "runtime.json").read_text())
            self.assertEqual(runtime["instance_label"], "manager-e2e")

            indexed = self.command("runs", data / "traces")
            self.assertEqual(indexed["runs"][0]["run_id"], "run-manager-e2e")
            self.assertTrue(indexed["runs"][0]["sdk_initialized"])

            bundle = data / "traces" / "run-manager-e2e-support"
            bundled = self.command("bundle", run, bundle, "2026-07-15T12:00:00Z")
            self.assertEqual(bundled["code"], "created")
            self.assertTrue(bundled["shareable"])
            bundle_bytes = b"".join(path.read_bytes() for path in bundle.rglob("*") if path.is_file())
            self.assertNotIn((data / "profile.key").read_bytes(), bundle_bytes)

            restored = self.command("restore", record)
            self.assertEqual(restored["code"], "restored")
            self.assertEqual(target.read_bytes(), original)
            self.assertFalse(backup.exists())
            self.assertFalse(descriptor.exists())
            self.assertFalse(record.exists())


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--cli", required=True, type=Path)
    parser.add_argument("--probe", required=True, type=Path)
    parser.add_argument("--artifact", required=True, type=Path)
    args, remaining = parser.parse_known_args()
    manager_loaded_sdk_workflow.cli = args.cli.resolve()
    manager_loaded_sdk_workflow.probe = args.probe.resolve()
    manager_loaded_sdk_workflow.artifact = args.artifact.resolve()
    unittest.main(argv=[__file__, *remaining])


if __name__ == "__main__":
    main()
