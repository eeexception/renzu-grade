# GitHub workflows and releases

The Actions sidebar provides a launcher for each platform and an All launcher:

| Platform | Tests | Build | Release |
| --- | --- | --- | --- |
| macOS arm64 | **Run macOS Tests** | **Run macOS Build** | **Run macOS Release** |
| Windows x64 | **Run Windows Tests** | **Run Windows Build** | **Run Windows Release** |
| Linux x86_64 | **Run Linux Tests** | **Run Linux Build** | **Run Linux Release** |
| All | **Run All Tests** | **Run All Builds** | **Run All Releases** |

All 12 support **Run workflow**. The All Tests/Builds workflows also run on pushes
and pull requests. The workflows must be merged into the default branch for the
new manual launchers to appear in the GitHub UI. Older runs retain their historical
workflow names.

## Tests and builds

A platform test launcher compiles/runs that platform's CPU/OpenFX suites. macOS
also runs Metal parity; Linux also runs ASan/UBSan. Shared release-tool tests run
for every selection. All Tests runs the complete set.

A platform Build launcher creates that platform's archive and SHA256 file, checks
layout/dependencies/checksums and builds the included corresponding source with
test targets disabled. All Builds creates all three distributions. Test logs and
build distributions are separate artifacts. A Build pass alone does not certify
tests or Resolve host acceptance.

The All workflows are also reusable implementations. The nine platform launchers
pass a fixed `platform` value (`macos`, `windows`, `linux`); a scope job validates
it before any platform jobs run. This keeps compiler/test commands shared while
providing separate Actions sidebar entries. Release workflows reuse those same
implementations for one immutable source commit.

## Create or extend a release

1. Commit the intended version in `CMakeLists.txt` and release documentation. Keep
   the OpenFX major/minor version in `Plugin.cpp` in sync when those components
   change. Workflows validate the version and do not rewrite/bump it.
2. Open the desired **Run macOS/Windows/Linux Release** or **Run All Releases**.
   Click **Run workflow** and select the source branch/tag.
3. Enter **tag**, such as `v0.3.0` for CMake VERSION `0.3.0`.
4. Choose the options:
   - **draft** defaults to true: keep a new/existing draft unpublished. Disable it
     to publish after successful uploads. An already published release stays published.
   - **prerelease** marks a newly created release; an existing release keeps its marking.
   - **dry_run** checks metadata, runs selected tests/builds and prepares/verifies
     assets without creating/updating tags, releases or release assets.
5. Run the workflow. Its source commit is fixed for validation, tests and builds.

A platform release validates and uploads only its distribution. All Releases
requires all three. Each release uses the common version tag, allowing sequential
platform uploads: macOS first, then Windows/Linux for that same commit. Notes and
existing asset bytes are preserved. A later All run reuses verified uploaded
archives and adds the missing platforms; it does not replace older ZIP bytes
merely because a new build has different packaging timestamps.

A tag must point to the selected source commit. A draft without an existing tag
must identify that full commit directly. Different source commits for one version
are rejected. Existing archives are checked against their checksum and recorded
commit/hash manifests (or an identical freshly built candidate). No `--clobber`
uploads are used. Repeating a completed platform release preserves its files.

New releases start as drafts, and publishing happens after all uploads succeed.
An upload failure leaves a draft unpublished. An interrupted upload with files
that cannot be verified needs inspection/cleanup before retrying. Releases marked
immutable cannot accept additional files; assemble all desired platforms before
publishing such a release. Concurrent release runs for the same tag are serialized.

Only the final publisher job has write permission. It downloads build artifacts
from its own run and uses GitHub CLI to create/extend the version release.
The validation and plugin test/build jobs have read permission.

## Release files

| Platform | Distribution |
| --- | --- |
| macOS arm64 | `renzu-grade-<version>-macos-arm64.zip` |
| Windows x64 CPU | `renzu-grade-<version>-windows-x86_64-cpu-only.zip` |
| Linux x86_64 CPU | `renzu-grade-<version>-linux-x86_64-cpu-only.tar.gz` |

Each distribution has a `.sha256` sidecar. Individual platform releases add
`release-manifest-macos.json` / `SHA256SUMS-macos` (or `windows` / `linux`). All
Releases adds `release-manifest.json` and `SHA256SUMS` for the complete set.
Manifests identify the source commit and archive hashes; their preview copies are
saved as the `release-preview` Actions artifact.

Validation rejects missing/extra selected assets, incorrect checksums, wrong
OFX locations, missing license/source files and a different packaged source version.
macOS is arm64 only and remains ad-hoc signed. Windows/Linux process on CPU.
All distributions include corresponding GPLv3 source/build instructions.
Complete the [Resolve checklist](manual-test-plan.md) for actual host acceptance.

## Local release-tool checks

Python 3.9+ and GitHub CLI are used by release automation; plugin compilation and
runtime require neither Python nor runtime internet access.

```bash
python3 -B -m unittest discover -s tests -p 'test_release.py' -v
```

The publication tests use an in-memory server and do not create real releases.
`scripts/release.py` only validates metadata/assets and writes manifests. Put the
selected archives and their sidecars in one directory to validate them locally:

```bash
# All: three archives and three sidecars.
python3 scripts/release.py assets --tag v0.3.0 --commit <full-sha> --directory release-assets
# macOS only: one archive and its sidecar.
python3 scripts/release.py assets --platform macos --tag v0.3.0 --commit <full-sha> --directory macos-assets
```

`scripts/publish_release.py` handles existing-release checks and publication. It
requires a checkout of the selected commit and GitHub CLI authentication. `--check`
is read-only; `--dry-run` prepares/validates without remote mutations. Publication
requires `--directory`; new releases stay drafts unless `--publish` is passed.
`--prerelease` affects only newly created releases. All configured tests/builds for
selected platforms must pass before the release job can run.
