# SPDX-License-Identifier: GPL-3.0-only
"""Validate release metadata and distributions; never publish or create tags."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import tarfile
import zipfile


def project_version(text):
    match = re.search(r"(?m)^\s*project\(RenzuGrade\s+VERSION\s+(\d+\.\d+\.\d+)(?:\s|\))", text)
    if not match:
        raise ValueError("Cannot read RenzuGrade VERSION from CMakeLists.txt")
    return match.group(1)


PLATFORMS = ("macos", "windows", "linux")


def selected_platforms(platform):
    if platform == "all":
        return PLATFORMS
    if platform in PLATFORMS:
        return (platform,)
    raise ValueError(f"Unknown release platform: {platform}")


def metadata(source, tag, commit, platform="all"):
    selected_platforms(platform)
    version = project_version((Path(source) / "CMakeLists.txt").read_text(encoding="utf-8"))
    if not re.fullmatch(r"v(?:0|[1-9]\d*)\.(?:0|[1-9]\d*)\.(?:0|[1-9]\d*)", tag):
        raise ValueError("Tag must have the form vMAJOR.MINOR.PATCH")
    if tag != "v" + version:
        raise ValueError(f"Tag {tag} does not match project version v{version}")
    if not re.fullmatch(r"[0-9a-f]{40}", commit):
        raise ValueError("Release commit must be a full GitHub commit SHA")
    return {"version": version, "tag": tag, "commit": commit, "platform": platform}


def distributions(version, platform="all"):
    prefix = "renzu-grade-" + version
    formats = {
        "macos": ("-macos-arm64.zip", "MacOS"),
        "windows": ("-windows-x86_64-cpu-only.zip", "Win64"),
        "linux": ("-linux-x86_64-cpu-only.tar.gz", "Linux-x86-64"),
    }
    return {prefix + formats[p][0]: formats[p][1] for p in selected_platforms(platform)}


def manifest_names(platform):
    selected_platforms(platform)
    suffix = "" if platform == "all" else "-" + platform
    return "release-manifest" + suffix + ".json", "SHA256SUMS" + suffix


def write_manifests(directory, info):
    directory = Path(directory)
    manifest, checksums = manifest_names(info.get("platform", "all"))
    (directory / manifest).write_text(json.dumps(info, indent=2) + "\n", encoding="utf-8")
    (directory / checksums).write_text(
        "".join(f'{item["sha256"]}  {item["file"]}\n' for item in info["assets"]), encoding="utf-8")
    return directory / manifest, directory / checksums


def digest(path):
    result = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            result.update(block)
    return result.hexdigest()


def check_archive(path, architecture, version):
    name = path.name
    root = name[:-7] if name.endswith(".tar.gz") else name[:-4]
    required = (
        "INSTALL.txt", "LICENSE", "OPENFX-LICENSE.md",
        f"RenzuGrade.ofx.bundle/Contents/{architecture}/RenzuGrade.ofx",
        "Source/renzu-grade/CMakeLists.txt",
    )
    source_name = root + "/Source/renzu-grade/CMakeLists.txt"
    if name.endswith(".zip"):
        with zipfile.ZipFile(path) as archive:
            for entry in required:
                member = archive.getinfo(root + "/" + entry)
                if member.is_dir() or member.file_size == 0:
                    raise ValueError(f"Empty packaged file: {entry}")
            if archive.getinfo(source_name).file_size > 128 * 1024:
                raise ValueError("Packaged CMakeLists.txt exceeds size limit")
            source = archive.read(source_name).decode("utf-8")
    else:
        with tarfile.open(path, "r:gz") as archive:
            for entry in required:
                member = archive.getmember(root + "/" + entry)
                if not member.isfile() or member.size == 0:
                    raise ValueError(f"Invalid packaged file: {entry}")
            if archive.getmember(source_name).size > 128 * 1024:
                raise ValueError("Packaged CMakeLists.txt exceeds size limit")
            with archive.extractfile(source_name) as stream:
                source = stream.read().decode("utf-8")
    if project_version(source) != version:
        raise ValueError(f"Packaged source version mismatch: {name}")


def validate_assets(directory, info):
    directory = Path(directory)
    platform = info.get("platform", "all")
    expected = distributions(info["version"], platform)
    required = set(expected) | {name + ".sha256" for name in expected}
    allowed = required | set(manifest_names(platform))
    actual = {path.name for path in directory.iterdir()}
    if actual - allowed or required - actual:
        raise ValueError(f"Release asset set mismatch: missing={sorted(required - actual)}, "
                         f"unexpected={sorted(actual - allowed)}")
    assets = []
    for name, architecture in expected.items():
        archive = directory / name
        sidecar = directory / (name + ".sha256")
        if archive.is_symlink() or sidecar.is_symlink():
            raise ValueError("Release assets must not be symbolic links")
        checksum = sidecar.read_text(encoding="utf-8").split()
        if len(checksum) != 2 or not re.fullmatch(r"[0-9a-fA-F]{64}", checksum[0]) or checksum[1] != name:
            raise ValueError(f"Invalid checksum sidecar: {sidecar.name}")
        sha256 = digest(archive)
        if sha256 != checksum[0].lower():
            raise ValueError(f"Archive checksum mismatch: {name}")
        check_archive(archive, architecture, info["version"])
        assets.append({"file": name, "sha256": sha256, "bytes": archive.stat().st_size})
    return dict(info, assets=assets)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mode", choices=("metadata", "assets"))
    parser.add_argument("--source", type=Path, default=Path(__file__).resolve().parent.parent)
    parser.add_argument("--tag", required=True)
    parser.add_argument("--commit", required=True)
    parser.add_argument("--platform", choices=("all",) + PLATFORMS, default="all")
    parser.add_argument("--directory", type=Path)
    args = parser.parse_args()
    try:
        info = metadata(args.source, args.tag, args.commit, args.platform)
        if args.mode == "assets":
            if args.directory is None:
                raise ValueError("assets requires --directory")
            info = validate_assets(args.directory, info)
            write_manifests(args.directory, info)
        print(json.dumps(info, indent=2))
    except (ValueError, OSError, KeyError, tarfile.TarError, zipfile.BadZipFile) as error:
        parser.exit(1, f"Release validation failed: {error}\n")


if __name__ == "__main__":
    main()
