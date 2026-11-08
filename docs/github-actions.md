# GitHub workflows and releases

The Actions tab has three workflows:

| Workflow | Trigger | Responsibility | Artifacts |
| --- | --- | --- | --- |
| **Tests** | Push, pull request, manual, Make Release | CPU/OpenFX tests on Windows/Linux; CPU/Metal on both Macs; Linux ASan/UBSan; release-tool tests | Test diagnostics |
| **Build** | Push, pull request, manual, Make Release | Release compilation with test targets disabled; packaging; binary/layout/checksum checks; build corresponding source | Four platform distributions and their SHA256 files |
| **Make Release** | Manual | Validate version/commit, run Tests, then Build, validate all assets and create a GitHub Release | Release files plus checksum/commit manifest |

Tests compiles the plugin and test harnesses as needed to execute checks. Build
creates distributions for macOS arm64, macOS x86_64, Windows x64 CPU, and Linux
x86_64 CPU. These two workflows run independently on ordinary pushes and PRs.
A successful Build alone does not certify tests or Resolve host acceptance.

Make Release reuses the exact same Tests and Build workflows, in that order,
using one immutable source commit. Only its final publishing job has
`contents: write`; test/build jobs have read permission. It downloads distribution
artifacts from its own run, so it does not select an unrelated "latest" build.

## Make a release

The workflow must be merged into the default branch to appear as a manual action
in the GitHub UI.

1. Commit the intended version in `CMakeLists.txt` and the corresponding release
   notes/documentation. Keep the OpenFX major/minor version in `Plugin.cpp` in sync
   when changing those components. The release action validates the CMake version
   and does not rewrite source files or bump the version for you.
2. Open **Actions -> Make Release -> Run workflow** and select the source branch/tag.
3. Enter **tag**, for example `v0.3.0` when CMake VERSION is `0.3.0`.
4. Choose the options:
   - **draft** defaults to true: upload a draft for review. Disable it to publish
     automatically after all files have uploaded successfully.
   - **prerelease** marks the release as a prerelease.
   - **dry_run** runs validation/tests/builds and produces the release manifest,
     without creating a tag or release.
5. Run the workflow. The selected source commit is fixed for the entire run.

An existing tag must point to the selected commit. An existing release is never
overwritten. When the tag is absent, GitHub creates it at that commit only in the
final release-creation step, after tests and all platform builds succeed.
If an upload fails, the partially uploaded release remains a draft. Inspect and
remove that incomplete draft before retrying; the workflow will not clobber it.
Concurrent release attempts for the same tag are serialized.

## Release contents and validation

Every release requires all four distributions, each with its `.sha256` file:

- `renzu-grade-<version>-macos-arm64.zip`
- `renzu-grade-<version>-macos-x86_64.zip`
- `renzu-grade-<version>-windows-x86_64-cpu-only.zip`
- `renzu-grade-<version>-linux-x86_64-cpu-only.tar.gz`

The publisher rejects missing/extra assets, incorrect checksums, wrong OFX bundle
locations, missing license/source files, and a different packaged source version.
It adds `SHA256SUMS` and `release-manifest.json` with the source commit and archive
hashes. Preview copies are saved as the `release-preview` Actions artifact.
Python 3.9+ and GitHub CLI are used by release automation; plugin builds still
require neither Python nor runtime internet access.

The macOS archives remain ad-hoc signed. Windows/Linux rendering is CPU only.
All archives include corresponding GPLv3 source/build instructions. Complete the
[Resolve checklist](manual-test-plan.md) for actual host acceptance.

## Local checks

```bash
python3 -B -m unittest discover -s tests -p 'test_release.py' -v
```

`scripts/release.py` validates metadata/assets and writes manifests; it has no
publishing or tag-creation capability. GitHub CLI publication is confined to the
final Make Release job. To validate real downloaded distributions locally, put
their eight archive/checksum files in one directory and run:

```bash
python3 scripts/release.py assets --tag v0.3.0 --commit <full-commit-sha> --directory release-assets
```

The known Intel macOS `metal_cpu_parity` failure also appears in Tests. Make Release
will remain blocked while any required test is failing; it does not skip that check.
