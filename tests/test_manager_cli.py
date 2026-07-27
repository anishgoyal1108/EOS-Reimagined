#!/usr/bin/env python3
"""Exercise the packaged CLI adapter against an isolated fake game, never a Steam library."""

import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile
import unittest


def pe(payload):
    data = bytearray(4096)
    data[0:2] = b"MZ"
    data[0x3C:0x40] = (0x80).to_bytes(4, "little")
    data[0x80:0x84] = b"PE\0\0"
    data[0x84:0x86] = (0x8664).to_bytes(2, "little")
    return bytes(data) + payload


class manager_cli_workflow(unittest.TestCase):
    cli = None

    def command(self, *args, expected=0):
        result = subprocess.run(
            [str(self.cli), *map(str, args)], check=False, capture_output=True, text=True
        )
        self.assertEqual(result.returncode, expected, result.stderr + result.stdout)
        return json.loads(result.stdout)

    def test_install_status_runs_bundle_and_restore(self):
        with tempfile.TemporaryDirectory(prefix="eosr-manager-cli-") as temporary:
            root = Path(temporary)
            game = root / "game"
            release = root / "release"
            state = root / "state"
            data = root / "instances" / "alice" / "data"
            for directory in (game, release, state, data):
                directory.mkdir(parents=True, exist_ok=True)
            original = pe(b"original")
            replacement = pe(b"reimagined")
            target = game / "EOSSDK-Win64-Shipping.dll"
            artifact = release / "EOSSDK-Win64-Shipping.dll"
            target.write_bytes(original)
            artifact.write_bytes(replacement)
            digest = hashlib.sha256(replacement).hexdigest()
            backup = Path(str(target) + ".eosr-original")
            journal = Path(str(target) + ".eosr-journal.json")
            descriptor = game / "eosr-bootstrap.json"
            record = state / "target.json"

            installed = self.command(
                "install", "11111111111111111111111111111111", "fixture-release",
                target, artifact, "windows", len(replacement), digest, backup, journal,
                descriptor, record, data,
            )
            self.assertEqual(installed["code"], "installed")
            self.assertEqual(target.read_bytes(), replacement)
            self.assertEqual(backup.read_bytes(), original)
            self.assertEqual(json.loads(descriptor.read_text())["data_dir"], str(data))

            status = self.command(
                "status", target, record, journal, "windows", artifact,
                len(replacement), digest,
            )
            self.assertEqual(status["state"], "installed_current")
            scan = self.command("target-scan", game)
            self.assertEqual(scan["targets"][0]["kind"], "windows_x86_64")
            self.assertTrue(scan["targets"][0]["recommended"])
            self.assertEqual(
                scan["targets"][0]["recommendation_evidence"],
                "only_safe_compatible_target",
            )

            traces = data / "traces"
            run = traces / "run-fixture"
            run.mkdir(parents=True)
            runtime = {
                "schema_version": 1,
                "emulator_build": "fixture-build",
                "created_utc": "2026-07-15T00:00:00Z",
                "run_id": "run-fixture",
                "instance_label": "alice",
                "os": {"name": "windows", "version": "10", "wine": "wine"},
                "config": {"trace_level": "lifecycle", "display_name": "Private Name"},
            }
            (run / "runtime.json").write_text(json.dumps(runtime))
            complete = json.dumps({
                "v": 1, "seq": 1, "t": 1, "pid": 2, "inst": "alice", "tid": "t#0",
                "kind": "meta", "event": "run_start",
            }) + "\n"
            (run / "trace.jsonl").write_bytes(complete.encode() + b'{"v":1')
            (run / "profile.key").write_text("never-share-this")
            indexed = self.command("runs", traces)
            self.assertEqual(indexed["runs"][0]["emulator_build"], "fixture-build")
            bundle = traces / "run-fixture-support"
            bundled = self.command(
                "bundle", run, bundle, "2026-07-15T00:01:00Z"
            )
            self.assertEqual(bundled["code"], "created")
            self.assertTrue(bundled["shareable"])
            summary = (bundle / "summary.json").read_text()
            self.assertNotIn("Private Name", summary)
            self.assertNotIn("never-share-this", summary)
            support_trace = (bundle / "trace.jsonl").read_text()
            self.assertNotIn("\n\n", support_trace)
            support_records = support_trace.splitlines()
            self.assertEqual(len(support_records), 1)
            support_record = json.loads(support_records[0])
            self.assertEqual(support_record["inst"], "instance#0")
            self.assertEqual(support_record["event"], "run_start")

            restored = self.command("restore", record)
            self.assertEqual(restored["code"], "restored")
            self.assertEqual(target.read_bytes(), original)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--cli", required=True, type=Path)
    args, remaining = parser.parse_known_args()
    manager_cli_workflow.cli = args.cli.resolve()
    unittest.main(argv=[__file__, *remaining])


if __name__ == "__main__":
    main()
