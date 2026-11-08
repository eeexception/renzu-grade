# SPDX-License-Identifier: GPL-3.0-only
"""Create or extend a version release using verified assets from one source commit."""
import argparse
import json
from pathlib import Path
import re
import shutil
import subprocess
import tarfile
import tempfile
import zipfile

import release


def command(*args):
    result = subprocess.run(args, capture_output=True, text=True)
    if result.returncode:
        raise RuntimeError(result.stderr.strip() or f"Command failed: {args[0]}")
    return result.stdout


class GitHub:
    def __init__(self, repository):
        if not re.fullmatch(r"[\w.-]+/[\w.-]+", repository):
            raise ValueError("Repository must be OWNER/NAME")
        self.repository = repository

    def lookup(self, tag):
        pages = json.loads(command("gh", "api", "--paginate", "--slurp",
                                   f"repos/{self.repository}/releases?per_page=100"))
        return next((item for page in pages for item in page if item["tag_name"] == tag), None)

    def download(self, tag, name, directory):
        command("gh", "release", "download", tag, "--repo", self.repository,
                "--pattern", name, "--dir", str(directory))

    def create(self, info, prerelease):
        args = ["gh", "release", "create", info["tag"], "--repo", self.repository,
                "--target", info["commit"], "--title", "Renzu Grade " + info["tag"],
                "--draft", "--generate-notes"]
        if prerelease:
            args.append("--prerelease")
        command(*args)

    def upload(self, tag, paths):
        # No --clobber: previously uploaded files must never be replaced.
        command("gh", "release", "upload", tag, "--repo", self.repository,
                *(str(path) for path in paths))

    def publish(self, tag):
        command("gh", "release", "edit", tag, "--repo", self.repository, "--draft=false")


def check_existing(info, existing, tag_commit):
    if tag_commit and tag_commit != info["commit"]:
        raise ValueError("The version tag points to a different source commit")
    if existing and not tag_commit and existing["target_commitish"] != info["commit"]:
        raise ValueError("Existing release does not identify this source commit")


def prepare_assets(client, info, directory, existing):
    directory = Path(directory)
    release.validate_assets(directory, info)
    remote_names = {asset["name"] for asset in existing["assets"]} if existing else set()
    expected = release.distributions(info["version"], info["platform"])
    with tempfile.TemporaryDirectory(prefix="renzu-existing-release-") as temporary:
        cache = Path(temporary)

        def download(name):
            path = cache / name
            if not path.exists():
                client.download(info["tag"], name, cache)
            return path

        for name in expected:
            if name not in remote_names:
                continue
            if name + ".sha256" not in remote_names:
                raise ValueError(f"Existing archive has no checksum: {name}")
            archive = download(name)
            checksum = download(name + ".sha256")
            archive_hash = release.digest(archive)
            # A manifest from an earlier platform run can authenticate that
            # platform when a later All run assembles the complete release.
            proven = archive_hash == release.digest(directory / name)
            for platform in ("all",) + release.PLATFORMS:
                manifest_name, _ = release.manifest_names(platform)
                if manifest_name not in remote_names:
                    continue
                manifest = json.loads(download(manifest_name).read_text(encoding="utf-8"))
                same_source = all(manifest.get(key) == info[key]
                                  for key in ("tag", "version", "commit"))
                if same_source and any(item.get("file") == name and item.get("sha256") == archive_hash
                                       for item in manifest.get("assets", [])):
                    proven = True
            if not proven:
                raise ValueError(f"Cannot verify the source commit of existing asset: {name}")
            # Preserve the remote bytes; archives can have different timestamps
            # across builds of the same source. This only replaces local copies.
            shutil.copy2(archive, directory / name)
            shutil.copy2(checksum, directory / (name + ".sha256"))

        manifest = release.validate_assets(directory, info)
        metadata_paths = release.write_manifests(directory, manifest)
        paths = [directory / name for archive in expected for name in (archive, archive + ".sha256")]
        paths.extend(metadata_paths)
        pending = []
        for path in paths:
            if path.name not in remote_names:
                pending.append(path)
            elif release.digest(path) != release.digest(download(path.name)):
                raise ValueError(f"Refusing to overwrite existing release asset: {path.name}")
        return pending


def publish(client, info, directory, existing=None, tag_commit=None,
            keep_draft=True, prerelease=False, dry_run=False):
    check_existing(info, existing, tag_commit)
    pending = prepare_assets(client, info, directory, existing)
    if existing and existing.get("immutable") and pending:
        raise ValueError("This release is immutable and cannot accept more assets")
    result = dict(info, dry_run=dry_run, create=existing is None,
                  uploads=[path.name for path in pending])
    if not dry_run:
        if existing is None:
            client.create(info, prerelease)
        if pending:
            client.upload(info["tag"], pending)
        if not keep_draft and (existing is None or existing["draft"]):
            client.publish(info["tag"])
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, default=Path(__file__).resolve().parent.parent)
    parser.add_argument("--repo", required=True)
    parser.add_argument("--tag", required=True)
    parser.add_argument("--commit", required=True)
    parser.add_argument("--platform", choices=("all",) + release.PLATFORMS, default="all")
    parser.add_argument("--directory", type=Path)
    parser.add_argument("--check", action="store_true", help="Read-only metadata and existing-release check")
    parser.add_argument("--publish", action="store_true", help="Publish the draft after a complete upload")
    parser.add_argument("--prerelease", action="store_true", help="Mark a newly created release as a prerelease")
    parser.add_argument("--dry-run", action="store_true", help="Prepare assets without release/tag mutations")
    args = parser.parse_args()
    try:
        info = release.metadata(args.source, args.tag, args.commit, args.platform)
        head = command("git", "-C", str(args.source), "rev-parse", "HEAD").strip()
        if head != args.commit:
            raise ValueError("Checkout does not match the release commit")
        ref = subprocess.run(["git", "-C", str(args.source), "rev-parse", "-q", "--verify",
                              f"refs/tags/{args.tag}^{{commit}}"], capture_output=True, text=True)
        tag_commit = ref.stdout.strip() if ref.returncode == 0 else None
        client = GitHub(args.repo)
        existing = client.lookup(args.tag)
        check_existing(info, existing, tag_commit)
        if args.check:
            print(json.dumps(info, indent=2))
            return
        if args.directory is None:
            raise ValueError("Publication/preview requires --directory")
        result = publish(client, info, args.directory, existing, tag_commit,
                         keep_draft=not args.publish, prerelease=args.prerelease, dry_run=args.dry_run)
        print(json.dumps(result, indent=2))
    except (ValueError, OSError, KeyError, RuntimeError, tarfile.TarError, zipfile.BadZipFile) as error:
        parser.exit(1, f"Release failed: {error}\n")


if __name__ == "__main__":
    main()
