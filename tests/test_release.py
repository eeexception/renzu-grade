# SPDX-License-Identifier: GPL-3.0-only
import io
from pathlib import Path
import sys
import shutil
import tarfile
import tempfile
import unittest
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
import release
import publish_release


class ReleaseFixture(unittest.TestCase):
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


class ReleaseTests(ReleaseFixture):
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

    def test_selected_platform_requires_only_its_archive(self):
        for platform in release.PLATFORMS:
            with self.subTest(platform=platform), tempfile.TemporaryDirectory() as temporary:
                directory = Path(temporary)
                info = release.metadata(self.root, "v0.3.0", "a" * 40, platform)
                for name in release.distributions(self.version, platform):
                    for filename in (name, name + ".sha256"):
                        shutil.copy2(self.assets / filename, directory / filename)
                result = release.validate_assets(directory, info)
                self.assertEqual(len(result["assets"]), 1)
                self.assertEqual(result["platform"], platform)
                paths = release.write_manifests(directory, result)
                self.assertEqual({p.name for p in paths}, set(release.manifest_names(platform)))

    def test_unknown_platform_rejected(self):
        with self.assertRaisesRegex(ValueError, "Unknown release platform"):
            release.metadata(self.root, "v0.3.0", "a" * 40, "other")

    def test_extra_platform_rejected_for_single_platform(self):
        info = release.metadata(self.root, "v0.3.0", "a" * 40, "macos")
        with self.assertRaisesRegex(ValueError, "asset set mismatch"):
            release.validate_assets(self.assets, info)


class FakeGitHub:
    """An in-memory release server; these tests never contact GitHub."""
    def __init__(self):
        self.files = {}
        self.events = []
        self.state = None
        self.fail_upload = False

    def existing(self):
        return dict(self.state, assets=[{"name": name} for name in self.files])

    def download(self, tag, name, directory):
        (directory / name).write_bytes(self.files[name])

    def create(self, info, prerelease):
        self.events.append("create")
        self.state = {"draft": True, "target_commitish": info["commit"],
                      "prerelease": prerelease, "body": "Existing notes", "immutable": False}

    def upload(self, tag, paths):
        self.events.append("upload")
        if self.fail_upload:
            raise RuntimeError("Upload failed")
        for path in paths:
            if path.name in self.files:
                raise AssertionError("Attempted to replace an existing asset")
            self.files[path.name] = path.read_bytes()

    def publish(self, tag):
        self.events.append("publish")
        self.state["draft"] = False


class PublicationTests(ReleaseFixture):
    def candidates(self, platform):
        info = release.metadata(self.root, "v0.3.0", "a" * 40, platform)
        directory = Path(tempfile.mkdtemp(dir=self.root))
        for name in release.distributions(self.version, platform):
            for filename in (name, name + ".sha256"):
                shutil.copy2(self.assets / filename, directory / filename)
        return info, directory

    def seed(self, client, platform="macos", **options):
        info, directory = self.candidates(platform)
        publish_release.publish(client, info, directory, **options)

    def change_macos_archive(self, directory):
        name = next(name for name in release.distributions(self.version) if "macos" in name)
        with zipfile.ZipFile(directory / name, "a") as archive:
            archive.comment = b"Different packaging timestamp, same source"
        (directory / (name + ".sha256")).write_text(release.digest(directory / name) + "  " + name)
        return name

    def test_new_release_contains_selected_platform(self):
        client = FakeGitHub()
        self.seed(client)
        self.assertEqual(len(client.files), 4)
        self.assertTrue(client.state["draft"])
        self.assertFalse(any("windows" in name or "linux" in name for name in client.files))

    def test_add_windows_preserves_macos_files(self):
        client = FakeGitHub()
        self.seed(client)
        original = dict(client.files)
        info, directory = self.candidates("windows")
        publish_release.publish(client, info, directory, client.existing(), "a" * 40)
        self.assertEqual({name: client.files[name] for name in original}, original)
        self.assertEqual(client.events.count("create"), 1)
        self.assertEqual(len(client.files), 8)

    def test_all_reuses_existing_archive_bytes_and_updates_global_manifest(self):
        client = FakeGitHub()
        self.seed(client)
        info, directory = self.candidates("all")
        name = self.change_macos_archive(directory)
        original = client.files[name]
        self.assertNotEqual((directory / name).read_bytes(), original)
        publish_release.publish(client, info, directory, client.existing(), "a" * 40)
        self.assertEqual(client.files[name], original)
        import json
        manifest = json.loads(client.files["release-manifest.json"])
        self.assertEqual(len(manifest["assets"]), 3)
        item = next(item for item in manifest["assets"] if item["file"] == name)
        self.assertEqual(item["sha256"], release.digest(directory / name))

    def test_repeated_platform_run_is_idempotent(self):
        client = FakeGitHub()
        self.seed(client)
        original = dict(client.files)
        events = list(client.events)
        info, directory = self.candidates("macos")
        self.change_macos_archive(directory)
        result = publish_release.publish(client, info, directory, client.existing(), "a" * 40)
        self.assertEqual(result["uploads"], [])
        self.assertEqual(client.files, original)
        self.assertEqual(client.events, events)

    def test_different_commit_rejected_before_writes(self):
        client = FakeGitHub()
        self.seed(client)
        events = list(client.events)
        info, directory = self.candidates("windows")
        with self.assertRaisesRegex(ValueError, "different source commit"):
            publish_release.publish(client, info, directory, client.existing(), "b" * 40)
        self.assertEqual(client.events, events)

    def test_new_release_dry_run_has_no_remote_mutations(self):
        client = FakeGitHub()
        info, directory = self.candidates("linux")
        result = publish_release.publish(client, info, directory, dry_run=True)
        self.assertEqual(len(result["uploads"]), 4)
        self.assertEqual(client.events, [])
        self.assertIsNone(client.state)

    def test_existing_release_dry_run_has_no_remote_mutations(self):
        client = FakeGitHub()
        self.seed(client)
        original, events = dict(client.files), list(client.events)
        info, directory = self.candidates("windows")
        publish_release.publish(client, info, directory, client.existing(), "a" * 40,
                                dry_run=True, keep_draft=False)
        self.assertEqual(client.files, original)
        self.assertEqual(client.events, events)
        self.assertTrue(client.state["draft"])

    def test_failed_upload_keeps_new_release_unpublished(self):
        client = FakeGitHub()
        client.fail_upload = True
        with self.assertRaisesRegex(RuntimeError, "Upload failed"):
            self.seed(client, keep_draft=False)
        self.assertTrue(client.state["draft"])
        self.assertNotIn("publish", client.events)

    def test_existing_published_release_keeps_status_notes_and_prerelease(self):
        client = FakeGitHub()
        self.seed(client, keep_draft=False, prerelease=True)
        info, directory = self.candidates("windows")
        publish_release.publish(client, info, directory, client.existing(), "a" * 40)
        self.assertFalse(client.state["draft"])
        self.assertTrue(client.state["prerelease"])
        self.assertEqual(client.state["body"], "Existing notes")
        self.assertEqual(client.events.count("publish"), 1)

    def test_immutable_release_rejects_new_assets(self):
        client = FakeGitHub()
        self.seed(client)
        client.state["immutable"] = True
        events = list(client.events)
        info, directory = self.candidates("windows")
        with self.assertRaisesRegex(ValueError, "immutable"):
            publish_release.publish(client, info, directory, client.existing(), "a" * 40)
        self.assertEqual(client.events, events)

    def test_unverified_existing_bytes_rejected(self):
        client = FakeGitHub()
        self.seed(client)
        client.files["release-manifest-macos.json"] = client.files["release-manifest-macos.json"].replace(
            ("a" * 40).encode(), ("b" * 40).encode())
        events = list(client.events)
        info, directory = self.candidates("macos")
        self.change_macos_archive(directory)
        with self.assertRaisesRegex(ValueError, "Cannot verify the source commit"):
            publish_release.publish(client, info, directory, client.existing(), "a" * 40)
        self.assertEqual(client.events, events)

    def test_draft_without_tag_requires_full_matching_commit(self):
        client = FakeGitHub()
        self.seed(client)
        existing = client.existing()
        existing["target_commitish"] = "main"
        info, directory = self.candidates("windows")
        with self.assertRaisesRegex(ValueError, "does not identify this source commit"):
            publish_release.publish(client, info, directory, existing)


if __name__ == "__main__":
    unittest.main()
