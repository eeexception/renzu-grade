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


def metadata(source, tag, commit):
    version = project_version((Path(source) / "CMakeLists.txt").read_text(encoding="utf-8"))
    if not re.fullmatch(r"v(?:0|[1-9]\d*)\.(?:0|[1-9]\d*)\.(?:0|[1-9]\d*)", tag):
        raise ValueError("Tag must have the form vMAJOR.MINOR.PATCH")
    if tag != "v" + version:
        raise ValueError(f"Tag {tag} does not match project version v{version}")
    if not re.fullmatch(r"[0-9a-f]{40}", commit):
        raise ValueError("Release commit must be a full GitHub commit SHA")
    return {"version": version, "tag": tag, "commit": commit}


def distributions(version):
    prefix = "renzu-grade-" + version
    return {
        prefix + "-macos-arm64.zip": "MacOS",
        prefix + "-windows-x86_64-cpu-only.zip": "Win64",
        prefix + "-linux-x86_64-cpu-only.tar.gz": "Linux-x86-64",
    }


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
    expected = distributions(info["version"])
    required = set(expected) | {name + ".sha256" for name in expected}
    allowed = required | {"SHA256SUMS", "release-manifest.json"}
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
    parser.add_argument("--directory", type=Path)
    args = parser.parse_args()
    try:
        info = metadata(args.source, args.tag, args.commit)
        if args.mode == "assets":
            if args.directory is None:
                raise ValueError("assets requires --directory")
            info = validate_assets(args.directory, info)
            (args.directory / "release-manifest.json").write_text(
                json.dumps(info, indent=2) + "\n", encoding="utf-8")
            (args.directory / "SHA256SUMS").write_text(
                "".join(f'{item["sha256"]}  {item["file"]}\n' for item in info["assets"]),
                encoding="utf-8")
        print(json.dumps(info, indent=2))
    except (ValueError, OSError, KeyError, tarfile.TarError, zipfile.BadZipFile) as error:
        parser.exit(1, f"Release validation failed: {error}\n")


if __name__ == "__main__":
    main()
