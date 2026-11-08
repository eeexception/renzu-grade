# SPDX-License-Identifier: GPL-3.0-only
import io
from pathlib import Path
import sys
import tarfile
import tempfile
import unittest
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
import release


class ReleaseTests(unittest.TestCase):
    def setUp(self):
        self.scratch = tempfile.TemporaryDirectory()
        self.addCleanup(self.scratch.cleanup)
        self.root = Path(self.scratch.name)
        self.version = "0.3.0"
        self.cmake = "project(RenzuGrade VERSION 0.3.0 LANGUAGES CXX)\n"
        (self.root / "CMakeLists.txt").write_text(self.cmake)
        self.info = release.metadata(self.root, "v0.3.0", "a" * 40)
        self.assets = self.root / "assets"
        self.assets.mkdir()
        for name, arch in release.distributions(self.version).items():
            self.make_archive(name, arch)

    def make_archive(self, name, arch, source=None):
        path = self.assets / name
        root = name[:-7] if name.endswith(".tar.gz") else name[:-4]
        files = {
            "INSTALL.txt": b"Install the whole bundle.",
            "LICENSE": b"GPL-3.0-only",
            "OPENFX-LICENSE.md": b"BSD-3-Clause",
            f"RenzuGrade.ofx.bundle/Contents/{arch}/RenzuGrade.ofx": b"test fixture",
            "Source/renzu-grade/CMakeLists.txt": (source or self.cmake).encode(),
        }
        if name.endswith(".zip"):
            with zipfile.ZipFile(path, "w") as archive:
                for relative, contents in files.items():
                    archive.writestr(root + "/" + relative, contents)
        else:
            with tarfile.open(path, "w:gz") as archive:
                for relative, contents in files.items():
                    member = tarfile.TarInfo(root + "/" + relative)
                    member.size = len(contents)
                    archive.addfile(member, io.BytesIO(contents))
        (self.assets / (name + ".sha256")).write_bytes(
            f"{release.digest(path)}  {name}\r\n".encode())

    def test_complete_release_with_windows_line_endings(self):
        result = release.validate_assets(self.assets, self.info)
        self.assertEqual(len(result["assets"]), 3)
        self.assertEqual(result["commit"], "a" * 40)

    def test_version_mismatch(self):
        with self.assertRaisesRegex(ValueError, "does not match"):
            release.metadata(self.root, "v0.4.0", "a" * 40)

    def test_invalid_tags(self):
        for tag in ("0.3.0", "v0.3.0\ninjected", "--draft", "v00.3.0", "v0.3.0-rc1"):
            with self.subTest(tag=tag), self.assertRaises(ValueError):
                release.metadata(self.root, tag, "a" * 40)

    def test_commit_must_be_full_sha(self):
        with self.assertRaises(ValueError):
            release.metadata(self.root, "v0.3.0", "main")

    def test_missing_platform_blocks_release(self):
        next(self.assets.glob("*.tar.gz")).unlink()
        with self.assertRaisesRegex(ValueError, "asset set mismatch"):
            release.validate_assets(self.assets, self.info)

    def test_unexpected_asset_blocks_release(self):
        (self.assets / "other-version.zip").write_bytes(b"unexpected")
        with self.assertRaisesRegex(ValueError, "unexpected"):
            release.validate_assets(self.assets, self.info)

    def test_intel_macos_archive_blocks_release(self):
        self.make_archive("renzu-grade-0.3.0-macos-x86_64.zip", "MacOS")
        with self.assertRaisesRegex(ValueError, "unexpected"):
            release.validate_assets(self.assets, self.info)

    def test_corrupt_archive_blocks_release(self):
        next(self.assets.glob("*.zip")).write_bytes(b"corrupt")
        with self.assertRaisesRegex(ValueError, "checksum mismatch"):
            release.validate_assets(self.assets, self.info)

    def test_absolute_checksum_filename_blocks_release(self):
        path = next(self.assets.glob("*.sha256"))
        fields = path.read_text().split()
        path.write_text(fields[0] + "  /runner/" + fields[1])
        with self.assertRaisesRegex(ValueError, "Invalid checksum sidecar"):
            release.validate_assets(self.assets, self.info)

    def test_wrong_binary_layout_blocks_release(self):
        name = next(n for n in release.distributions(self.version) if "windows" in n)
        self.make_archive(name, "MacOS")
        with self.assertRaises(KeyError):
            release.validate_assets(self.assets, self.info)

    def test_packaged_source_version_mismatch(self):
        name, arch = next(iter(release.distributions(self.version).items()))
        self.make_archive(name, arch, source=self.cmake.replace("0.3.0", "9.0.0"))
        with self.assertRaisesRegex(ValueError, "source version mismatch"):
            release.validate_assets(self.assets, self.info)


if __name__ == "__main__":
    unittest.main()
