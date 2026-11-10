"""Release gates reject missing, corrupt or incorrectly labelled platform builds."""
import hashlib
import io
import json
from pathlib import Path
import tarfile
import tempfile
import unittest
import zipfile

from verify_release_packages import verify_packages


class ReleasePackagesTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="nextdayz-release-test-")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.version = "nextdayz-v1.2.3"
        for role in ("server", "client"):
            for platform in ("win64", "linux64"):
                self.write_package(role, platform)

    def write_package(self, role, platform, **overrides):
        system, suffix = ("windows", ".zip") if platform == "win64" else ("linux", ".tar.gz")
        manifest = {"formatVersion": 1, "preset": f"nextdayz-{role}", "version": self.version, "os": system, "arch": "amd64"}
        manifest.update(overrides)
        payload = json.dumps(manifest).encode()
        name = f"NextDayz-{role}_{platform}_{self.version}{suffix}"
        path = self.root / name
        marker = f"nextdayz-{role}.json"
        if platform == "win64":
            with zipfile.ZipFile(path, "w") as package:
                package.writestr(marker, payload)
        else:
            with tarfile.open(path, "w:gz") as package:
                info = tarfile.TarInfo(marker)
                info.size = len(payload)
                package.addfile(info, io.BytesIO(payload))
        digest = hashlib.sha256(path.read_bytes()).hexdigest()
        Path(str(path) + ".sha256").write_text(f"{digest}  {name}\n", encoding="utf-8")
        return path

    def test_accepts_complete_matching_release(self):
        self.assertEqual(len(verify_packages(self.root, self.version)), 8)

    def test_rejects_missing_platform(self):
        for path in self.root.glob("*linux64*"):
            path.unlink()
        with self.assertRaisesRegex(ValueError, "incomplete Release"):
            verify_packages(self.root, self.version)

    def test_rejects_corrupt_archive(self):
        path = self.write_package("client", "win64")
        path.write_bytes(b"damaged archive")
        with self.assertRaisesRegex(ValueError, "SHA-256 mismatch"):
            verify_packages(self.root, self.version)

    def test_rejects_mislabeled_build(self):
        for metadata in ({"os": "windows"}, {"version": "old-build"}, {"preset": "nextdayz-client"}, {"arch": "arm64"}):
            with self.subTest(metadata=metadata):
                self.write_package("server", "linux64", **metadata)
                with self.assertRaises(ValueError):
                    verify_packages(self.root, self.version)

    def test_rejects_stale_extra_assets(self):
        (self.root / "old-package.zip").write_bytes(b"stale")
        with self.assertRaisesRegex(ValueError, "unexpected"):
            verify_packages(self.root, self.version)

    def test_rejects_checksum_for_different_name(self):
        path = self.write_package("client", "win64")
        sidecar = Path(str(path) + ".sha256")
        sidecar.write_text(sidecar.read_text().replace(path.name, "other.zip"), encoding="utf-8")
        with self.assertRaisesRegex(ValueError, "invalid SHA-256 sidecar"):
            verify_packages(self.root, self.version)


if __name__ == "__main__":
    unittest.main()
