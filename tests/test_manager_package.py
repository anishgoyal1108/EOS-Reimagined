#!/usr/bin/env python3

import importlib.util
import hashlib
import json
from pathlib import Path
import struct
import tarfile
import tempfile
import unittest
import zipfile


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "package_manager", ROOT / "tools" / "package_manager.py"
)
package_manager = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(package_manager)


def elf64_with_dynamic_tag(tag):
    data = bytearray(160)
    data[0:16] = b"\x7fELF\x02\x01\x01" + bytes(9)
    struct.pack_into("<HHIQQQIHHHHHH", data, 16, 3, 62, 1, 0, 64, 0, 0,
                     64, 56, 1, 0, 0, 0)
    struct.pack_into("<IIQQQQQQ", data, 64, 2, 4, 120, 0, 0, 32, 32, 8)
    struct.pack_into("<qQqQ", data, 120, tag, 1, 0, 0)
    return bytes(data)


class manager_package_tests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        self.linux_manager = self.root / "eos-reimagined-manager"
        self.windows_manager = self.root / "EOSReimaginedManager.exe"
        self.linux_sdk = self.root / "libEOSSDK-Linux-Shipping.so"
        self.windows_sdk = self.root / "EOSSDK-Win64-Shipping.dll"
        self.linux_manager.write_bytes(b"linux manager")
        self.windows_manager.write_bytes(b"windows manager")
        self.linux_sdk.write_bytes(b"linux sdk")
        self.windows_sdk.write_bytes(b"windows sdk")
        self.version = "v0.1.0-alpha.1"
        self.revision = "0123456789abcdef0123456789abcdef01234567"

    def tearDown(self):
        self.tmp.cleanup()

    def build(self, output):
        return package_manager.build_manager_release(
            self.version, self.revision, self.linux_manager, self.windows_manager,
            self.linux_sdk, self.windows_sdk, output
        )

    def test_packages_are_manifested_and_contain_no_development_runtime(self):
        output = self.build(self.root / "dist")
        package_manager.verify_manager_release(output)

        linux_root = "eos-reimagined-manager-%s-linux-x86_64" % self.version
        with tarfile.open(output / (linux_root + ".tar.gz"), "r:gz") as archive:
            names = archive.getnames()
            self.assertIn(linux_root + "/eos-reimagined-manager", names)
            self.assertIn(linux_root + "/artifacts/libEOSSDK-Linux-Shipping.so", names)
            self.assertIn(linux_root + "/artifacts/EOSSDK-Win64-Shipping.dll", names)
            self.assertFalse(any(name.endswith((".py", ".js")) for name in names))

        guide = (ROOT / "release" / "MANAGER-QUICKSTART.md").read_text("utf-8")
        self.assertNotIn("Phase 0", guide)
        self.assertIn("Discover Steam", guide)
        self.assertIn("Build support bundle", guide)
        self.assertIn("Restore", guide)
        self.assertIn("localconfig.vdf", guide)

        windows_root = "eos-reimagined-manager-%s-windows-x86_64" % self.version
        with zipfile.ZipFile(output / (windows_root + ".zip")) as archive:
            names = archive.namelist()
            self.assertIn(windows_root + "/EOSReimaginedManager.exe", names)
            self.assertIn(windows_root + "/artifacts/EOSSDK-Win64-Shipping.dll", names)
            self.assertFalse(any(name.endswith((".py", ".js")) for name in names))

    def test_same_inputs_produce_identical_manager_packages(self):
        first = self.build(self.root / "first")
        second = self.build(self.root / "second")
        for name in (
            "eos-reimagined-manager-%s-linux-x86_64.tar.gz" % self.version,
            "eos-reimagined-manager-%s-windows-x86_64.zip" % self.version,
            "manager-release-manifest.json",
            "SHA256SUMS.txt",
        ):
            self.assertEqual((first / name).read_bytes(), (second / name).read_bytes())

    def test_tampering_fails_outer_hash_verification(self):
        output = self.build(self.root / "tampered")
        manifest = json.loads((output / "manager-release-manifest.json").read_text())
        path = output / manifest["assets"][0]["name"]
        path.write_bytes(path.read_bytes() + b"tampered")
        with self.assertRaises(package_manager.package_error):
            package_manager.verify_manager_release(output)

    def test_wrong_binary_name_is_rejected(self):
        wrong = self.root / "manager.exe"
        wrong.write_bytes(b"wrong")
        with self.assertRaises(package_manager.package_error):
            package_manager.build_manager_release(
                self.version, self.revision, self.linux_manager, wrong, self.linux_sdk,
                self.windows_sdk, self.root / "wrong-package"
            )

    def test_linux_manager_with_runtime_search_path_is_rejected(self):
        for tag in (15, 29):  # DT_RPATH / DT_RUNPATH
            with self.subTest(tag=tag):
                self.linux_manager.write_bytes(elf64_with_dynamic_tag(tag))
                with self.assertRaises(package_manager.package_error):
                    self.build(self.root / ("runtime-path-%d" % tag))

    def test_embedded_manifest_cannot_omit_required_package_members(self):
        files = {"eos-reimagined-manager": b"linux manager"}
        data = files["eos-reimagined-manager"]
        manifest = {
            "schema_version": 1,
            "version": self.version,
            "commit": self.revision,
            "platform": "linux",
            "architecture": "x86_64",
            "files": [{
                "path": "eos-reimagined-manager",
                "bytes": len(data),
                "sha256": hashlib.sha256(data).hexdigest(),
                "mode": "0755",
            }],
        }
        manifest_bytes = (json.dumps(manifest) + "\n").encode("utf-8")
        with self.assertRaises(package_manager.package_error):
            package_manager._verify_embedded(files, manifest_bytes, "linux")


if __name__ == "__main__":
    unittest.main()
