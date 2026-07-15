#!/usr/bin/env python3

import importlib.util
import json
from pathlib import Path
import tarfile
import tempfile
import unittest
import warnings
import zipfile


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "package_release", ROOT / "tools" / "package_release.py"
)
package_release = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(package_release)


class release_package_tests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        self.linux = self.root / "libEOSSDK-Linux-Shipping.so"
        self.windows = self.root / "EOSSDK-Win64-Shipping.dll"
        self.linux.write_bytes(b"linux-artifact")
        self.windows.write_bytes(b"windows-artifact")
        self.version = "v0.1.0-alpha.1"
        self.revision = "0123456789abcdef0123456789abcdef01234567"

    def tearDown(self):
        self.tmp.cleanup()

    def build(self, output):
        return package_release.build_release(
            self.version, self.revision, self.linux, self.windows, output
        )

    def test_version_and_revision_are_strict(self):
        with self.assertRaises(package_release.package_error):
            package_release.build_release(
                "0.1.0-alpha.1", self.revision, self.linux, self.windows, self.root / "bad-version"
            )
        with self.assertRaises(package_release.package_error):
            package_release.build_release(
                self.version, "foundation", self.linux, self.windows, self.root / "bad-revision"
            )
        with self.assertRaises(package_release.package_error):
            package_release.build_release(
                self.version + "\n", self.revision, self.linux, self.windows,
                self.root / "newline-version"
            )

    def test_archives_are_complete_and_self_describing(self):
        output = self.root / "dist"
        self.build(output)
        package_release.verify_release(output)

        manifest = json.loads((output / "release-manifest.json").read_text())
        self.assertEqual(manifest["version"], self.version)
        self.assertEqual(manifest["commit"], self.revision)
        self.assertEqual(len(manifest["assets"]), 2)

        linux_name = "eos-reimagined-%s-linux-x86_64" % self.version
        with tarfile.open(output / (linux_name + ".tar.gz"), "r:gz") as archive:
            names = archive.getnames()
            self.assertIn(linux_name + "/libEOSSDK-Linux-Shipping.so", names)
            self.assertIn(linux_name + "/tools/alpha_runner.py", names)
            self.assertIn(linux_name + "/tools/inspect_game.py", names)
            self.assertIn(linux_name + "/licenses/monocypher-LICENSE.md", names)
            release = json.loads(archive.extractfile(linux_name + "/RELEASE.json").read())
            self.assertEqual(release["artifact"]["sha256"], package_release.sha256_file(self.linux))

        windows_name = "eos-reimagined-%s-windows-x86_64" % self.version
        with zipfile.ZipFile(output / (windows_name + ".zip")) as archive:
            names = archive.namelist()
            self.assertIn(windows_name + "/EOSSDK-Win64-Shipping.dll", names)
            self.assertFalse(any(name.startswith("/") or "../" in name for name in names))

        sums = (output / "SHA256SUMS.txt").read_text().splitlines()
        self.assertEqual(len(sums), 2)
        self.assertTrue(all("  eos-reimagined-" in line for line in sums))

    def test_same_inputs_produce_identical_packages(self):
        first = self.root / "first"
        second = self.root / "second"
        self.build(first)
        self.build(second)
        for name in (
            "eos-reimagined-%s-linux-x86_64.tar.gz" % self.version,
            "eos-reimagined-%s-windows-x86_64.zip" % self.version,
            "SHA256SUMS.txt",
            "release-manifest.json",
        ):
            self.assertEqual((first / name).read_bytes(), (second / name).read_bytes())

    def test_tampered_asset_fails_verification(self):
        output = self.root / "tampered"
        self.build(output)
        archive = output / ("eos-reimagined-%s-windows-x86_64.zip" % self.version)
        archive.write_bytes(archive.read_bytes() + b"tampered")
        with self.assertRaises(package_release.package_error):
            package_release.verify_release(output)

    def test_duplicate_archive_member_is_rejected(self):
        output = self.root / "duplicate"
        self.build(output)
        root = "eos-reimagined-%s-windows-x86_64" % self.version
        archive_path = output / (root + ".zip")
        with warnings.catch_warnings():
            warnings.simplefilter("ignore", UserWarning)
            with zipfile.ZipFile(archive_path, "a") as archive:
                archive.writestr(root + "/RELEASE.json", b"{}")
        manifest = json.loads((output / "release-manifest.json").read_text())
        with self.assertRaises(package_release.package_error):
            package_release._verify_zip(archive_path, root, manifest)


if __name__ == "__main__":
    unittest.main()
