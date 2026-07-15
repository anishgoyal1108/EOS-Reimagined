#!/usr/bin/env python3

import importlib.util
import json
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("alpha_runner", ROOT / "tools" / "alpha_runner.py")
alpha_runner = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(alpha_runner)


class alpha_runner_tests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)

    def tearDown(self):
        self.tmp.cleanup()

    def write(self, path, data):
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
        return path

    def test_discovery_requires_one_matching_sdk(self):
        game = self.root / "game"
        artifact = self.write(self.root / "EOSSDK-Win64-Shipping.dll", b"ours")
        first = self.write(game / "one" / artifact.name, b"one")
        self.assertEqual(alpha_runner.discover_sdk(game, artifact), first.resolve())

        self.write(game / "two" / artifact.name, b"two")
        with self.assertRaises(alpha_runner.runner_error):
            alpha_runner.discover_sdk(game, artifact)

    def test_staging_is_exact_and_unknown_target_is_not_overwritten(self):
        sdk = self.write(self.root / "game" / "EOSSDK-Win64-Shipping.dll", b"original-sdk")
        artifact = self.write(self.root / "build" / sdk.name, b"emulator-sdk")

        alpha_runner.stage_sdk(sdk, artifact)
        self.assertEqual(sdk.read_bytes(), b"emulator-sdk")
        self.assertTrue(alpha_runner.backup_path(sdk).is_file())
        self.assertTrue(alpha_runner.state_path(sdk).is_file())

        sdk.write_bytes(b"game-update-or-user-change")
        with self.assertRaises(alpha_runner.runner_error):
            alpha_runner.restore_sdk(sdk)
        self.assertEqual(sdk.read_bytes(), b"game-update-or-user-change")

        sdk.write_bytes(b"emulator-sdk")
        alpha_runner.restore_sdk(sdk)
        self.assertEqual(sdk.read_bytes(), b"original-sdk")
        self.assertFalse(alpha_runner.backup_path(sdk).exists())
        self.assertFalse(alpha_runner.state_path(sdk).exists())

    def test_nonzero_game_exit_still_restores_and_builds_private_bundle(self):
        game = self.root / "Game With Spaces"
        sdk = self.write(game / "EOSSDK-Win64-Shipping.dll", b"original-sdk")
        artifact = self.write(self.root / "build" / sdk.name, b"emulator-sdk")
        data_dir = self.root / "private-profile"
        runs_dir = self.root / "runs"
        fake_game = self.root / "fake_game.py"
        fake_game.write_text(
            "import json, os, pathlib, sys\n"
            "run = pathlib.Path(os.environ['EOSR_RUN_DIR'])\n"
            "runtime = {'schema_version': 1, 'instance_label': 'alice', "
            "'config': {'display_name': 'Private Player', 'trace_dir': str(run.parent)}}\n"
            "(run / 'runtime.json').write_text(json.dumps(runtime))\n"
            "record = {'v': 1, 'seq': 0, 't': 1, 'pid': 2, 'inst': 'alice', "
            "'tid': 't#0', 'kind': 'meta', 'event': 'run_start'}\n"
            "(run / 'trace.jsonl').write_text(json.dumps(record) + '\\n')\n"
            "print('stdout-private-marker')\n"
            "sys.exit(7)\n"
        )

        result = alpha_runner.main([
            "run", str(game), "--artifact", str(artifact), "--sdk", str(sdk),
            "--data-dir", str(data_dir), "--runs-dir", str(runs_dir),
            "--skip-inspection", "--", sys.executable, str(fake_game), "secret-argument",
        ])
        self.assertEqual(result, 7)
        self.assertEqual(sdk.read_bytes(), b"original-sdk")
        self.assertFalse(alpha_runner.backup_path(sdk).exists())

        run_dirs = [p for p in runs_dir.iterdir() if p.is_dir() and not p.name.endswith("-bundle")]
        self.assertEqual(len(run_dirs), 1)
        run_dir = run_dirs[0]
        launch = json.loads((run_dir / "launch.json").read_text())
        self.assertEqual(launch["launch"]["exit_code"], 7)
        self.assertEqual(launch["launch"]["argv_redacted"], [Path(sys.executable).name, "<arguments-redacted>"])
        self.assertIn("stdout-private-marker", (run_dir / "stdout.log").read_text())

        bundle = Path(str(run_dir) + "-bundle")
        summary_text = (bundle / "summary.json").read_text()
        self.assertNotIn("Private Player", summary_text)
        self.assertNotIn(str(self.root), summary_text)
        self.assertNotIn("stdout-private-marker", summary_text)
        self.assertNotIn("secret-argument", summary_text)
        self.assertEqual(sorted(p.name for p in bundle.iterdir()), ["summary.json", "trace.jsonl"])

    def test_bundle_drops_only_a_torn_tail_and_never_copies_owned_files(self):
        run = self.root / "run"
        run.mkdir()
        (run / "runtime.json").write_text(json.dumps({
            "schema_version": 1,
            "config": {"display_name": "Marlowe", "trace_dir": str(self.root / "traces")},
        }))
        (run / "launch.json").write_text(json.dumps({
            "schema_version": 1,
            "artifact": {"path": r"C:\\Games\\EOSSDK-Win64-Shipping.dll"},
            "launch": {"cwd": str(self.root), "argv_redacted": ["game.exe"]},
        }))
        (run / "inspection.json").write_text(json.dumps({
            "schema_version": 1, "target": str(self.root / "game"),
        }))
        complete = json.dumps({
            "v": 1, "seq": 0, "t": 1, "pid": 2, "inst": None,
            "tid": "t#0", "kind": "meta", "event": "run_start",
        })
        (run / "trace.jsonl").write_bytes((complete + "\n{\"v\":1").encode("utf-8"))
        (run / "stdout.log").write_text("not-shareable")
        (run / "profile.key").write_text("private-key")

        bundle = self.root / "bundle"
        alpha_runner.create_bundle(run, bundle)
        self.assertEqual((bundle / "trace.jsonl").read_text(), complete + "\n")
        summary_text = (bundle / "summary.json").read_text()
        self.assertNotIn("Marlowe", summary_text)
        self.assertNotIn(str(self.root), summary_text)
        self.assertNotIn("private-key", summary_text)
        self.assertNotIn("not-shareable", summary_text)
        self.assertIn("EOSSDK-Win64-Shipping.dll", summary_text)
        self.assertEqual(json.loads(summary_text)["trace"]["torn_tail_records_dropped"], 1)

    def test_start_failure_restores_before_reporting_error(self):
        game = self.root / "game"
        sdk = self.write(game / "EOSSDK-Win64-Shipping.dll", b"original-sdk")
        artifact = self.write(self.root / "build" / sdk.name, b"emulator-sdk")

        result = alpha_runner.main([
            "run", str(game), "--artifact", str(artifact), "--sdk", str(sdk),
            "--data-dir", str(self.root / "data"), "--runs-dir", str(self.root / "runs"),
            "--skip-inspection", "--", str(self.root / "does-not-exist"),
        ])
        self.assertEqual(result, 2)
        self.assertEqual(sdk.read_bytes(), b"original-sdk")
        self.assertFalse(alpha_runner.backup_path(sdk).exists())

    def test_manual_interrupt_restores_before_returning(self):
        game = self.root / "game"
        sdk = self.write(game / "EOSSDK-Win64-Shipping.dll", b"original-sdk")
        artifact = self.write(self.root / "build" / sdk.name, b"emulator-sdk")

        with mock.patch("builtins.input", side_effect=KeyboardInterrupt):
            result = alpha_runner.main([
                "run", str(game), "--artifact", str(artifact), "--sdk", str(sdk),
                "--data-dir", str(self.root / "data"), "--runs-dir", str(self.root / "runs"),
                "--skip-inspection", "--manual",
            ])

        self.assertEqual(result, 130)
        self.assertEqual(sdk.read_bytes(), b"original-sdk")
        self.assertFalse(alpha_runner.backup_path(sdk).exists())

    def test_invalid_complete_trace_is_not_labelled_shareable(self):
        run = self.root / "run-invalid"
        run.mkdir()
        (run / "trace.jsonl").write_bytes(b'{"v":1}\nnot-json\n')
        bundle = self.root / "bundle-invalid"

        with self.assertRaises(alpha_runner.runner_error):
            alpha_runner.create_bundle(run, bundle)
        self.assertFalse(bundle.exists())

    def test_failure_after_replacement_runs_verified_recovery(self):
        game = self.root / "game"
        sdk = self.write(game / "EOSSDK-Win64-Shipping.dll", b"original-sdk")
        artifact = self.write(self.root / "build" / sdk.name, b"emulator-sdk")
        real_replace = alpha_runner._replace_from
        calls = []

        def fail_after_first_replace(source, target, mode):
            real_replace(source, target, mode)
            calls.append(str(source))
            if len(calls) == 1:
                raise OSError("simulated post-replacement failure")

        with mock.patch.object(alpha_runner, "_replace_from", fail_after_first_replace):
            result = alpha_runner.main([
                "run", str(game), "--artifact", str(artifact), "--sdk", str(sdk),
                "--data-dir", str(self.root / "data"), "--runs-dir", str(self.root / "runs"),
                "--skip-inspection", "--", sys.executable, "-c", "pass",
            ])

        self.assertEqual(result, 2)
        self.assertEqual(sdk.read_bytes(), b"original-sdk")
        self.assertFalse(alpha_runner.backup_path(sdk).exists())
        self.assertFalse(alpha_runner.state_path(sdk).exists())


if __name__ == "__main__":
    unittest.main()
