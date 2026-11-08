# Renzu Grade

**Renzu Grade for DaVinci Resolve** is an independent GPLv3 OpenFX photographic
color grading plugin. Version **0.3.0** provides white balance, exposure, tonal
controls, full-frame Auto and reference matching inside Resolve's native effects
panel. It works locally, without another app, DCTL or runtime internet access.

## Installation

Download the archive for your system from
[GitHub Releases](https://github.com/eeexception/renzu-grade/releases). Development
builds are also available under **Actions -> Run All Builds** (or your platform’s Build workflow) **-> a successful run -> Artifacts**.
Actions adds an outer ZIP: extract that first, then extract the distribution inside
it. Each distribution contains `RenzuGrade.ofx.bundle`, `INSTALL.txt`, licenses,
documentation and corresponding source.

| System | Required architecture | Download filename | Plugin rendering |
| --- | --- | --- | --- |
| macOS 13+ | Apple Silicon, arm64 (M-series Macs) | `renzu-grade-<version>-macos-arm64.zip` | Metal + CPU |
| Windows | x64 | `renzu-grade-<version>-windows-x86_64-cpu-only.zip` | CPU |
| Linux, glibc 2.28+ | x86_64 | `renzu-grade-<version>-linux-x86_64-cpu-only.tar.gz` | CPU |

macOS builds target Apple Silicon only. Windows/Linux use the same grading, Auto,
WB picker and Match algorithms on CPU; playback speed depends on the processor.
Resolve still needs its own supported GPU, drivers and operating system.
The target host is DaVinci Resolve 21.x; see [compatibility](#compatibility).

**Quit DaVinci Resolve before installing, updating or uninstalling the plugin.**
Copy the entire `RenzuGrade.ofx.bundle` folder, preserving its internal structure.
When replacing an installation manually, move the previous bundle outside the OFX
Plugins directory first, so only one version is scanned.

### macOS — Apple Silicon

Extract the archive. In Terminal, open the extracted directory containing
`RenzuGrade.ofx.bundle` and `scripts`, then run:

```bash
./scripts/install.sh ./RenzuGrade.ofx.bundle
```

The installer verifies the arm64 bundle and places it at:

```text
/Library/OFX/Plugins/RenzuGrade.ofx.bundle
```

It requests administrator access when needed and preserves an existing version
under `/Library/OFX/RenzuGrade-Backups/`. These builds are ad-hoc signed, rather
than Developer ID signed/notarized. For loading diagnostics, see
[macOS installation and troubleshooting](docs/build-macos.md).

To uninstall from the extracted distribution directory:

```bash
./scripts/uninstall.sh
```

Uninstall moves the plugin into the backup directory and preserves saved references.

### Windows — x64, CPU

Extract the ZIP and copy `RenzuGrade.ofx.bundle` into:

```text
C:\Program Files\Common Files\OFX\Plugins\
```

Administrator permission may be required. The final binary path must be:

```text
C:\Program Files\Common Files\OFX\Plugins\RenzuGrade.ofx.bundle\Contents\Win64\RenzuGrade.ofx
```

The plugin uses a static Visual C++ runtime, so a separate Visual C++
redistributable is not required for this plugin. To uninstall, close Resolve and
move only `RenzuGrade.ofx.bundle` outside Plugins. Saved references remain available.
See [Windows instructions](docs/build-windows.md).

### Linux — x86_64, CPU

Extract the `.tar.gz`. From the extracted distribution directory, run:

```bash
sudo mkdir -p /usr/OFX/Plugins
sudo cp -a RenzuGrade.ofx.bundle /usr/OFX/Plugins/
```

The final binary path must be:

```text
/usr/OFX/Plugins/RenzuGrade.ofx.bundle/Contents/Linux-x86-64/RenzuGrade.ofx
```

GitHub builds use a glibc 2.28 baseline and a private static C++ runtime. To check
for unavailable system libraries, run `ldd` on the installed `.ofx` file. To
uninstall, close Resolve and move only this bundle outside Plugins; references
are preserved. See [Linux instructions](docs/build-linux.md).

### Enable the effect in Resolve

Restart Resolve after installation. On the **Color** page, select a clip and a
serial node, open **Effects**, and search for **Renzu Grade** under **Color Grading**.
Drag the effect onto the node, then open its Settings panel.

If it is missing, check **Preferences -> System -> Video Plugins**, confirm it is
enabled, check the bundle path/architecture and restart Resolve. A "Loaded
successfully" entry alone does not confirm successful effect registration; inspect
Resolve's plugin-load log if the effect still does not appear.

## Set up your Resolve project

**New Renzu Grade instances expect DaVinci Wide Gamut / DaVinci Intermediate.**
Configure the image entering the effect accordingly. The panel has no input-space
selector; changing a project label alone does not transform incorrectly tagged media.

### Resolve Color Management

For a managed project, open **Project Settings -> Color Management** and configure:

| Setting | Value |
| --- | --- |
| Color science | DaVinci YRGB Color Managed |
| Automatic color management | Off, to expose the manual settings |
| Color processing mode | Custom |
| Timeline color space | DaVinci WG/Intermediate |
| Output color space | Your delivery/display target; for an SDR Rec.709 Gamma 2.4 workflow, choose that output |
| Clip input color space | The actual camera/media gamut and gamma, set correctly for each clip |

Place Renzu Grade in the timeline working space, before the output transform.
Resolve handles the configured input/output conversions. Check any per-timeline or
per-node overrides as well. See Blackmagic Design's
[Colorist Guide](https://documents.blackmagicdesign.com/UserManuals/DaVinci-Resolve-20-Colorist-Guide.pdf)
for color-management fundamentals.

### Manual Color Space Transform workflow

In an unmanaged **DaVinci YRGB** project, use CST nodes around the plugin:

```text
Camera/media input
  -> Input CST: actual source gamut/gamma to DaVinci Wide Gamut / DaVinci Intermediate
  -> Renzu Grade
  -> Output CST: DaVinci Wide Gamut / DaVinci Intermediate to your delivery space
```

Set both the gamut and gamma in each CST. Apply the required input/output
conversions once; account for transforms already supplied by project management
or other nodes. References and targets should reach Renzu Grade at equivalent
points in their grading pipelines.

The engine outputs the same encoding it receives. It does not decode camera RAW,
invert LUTs, or replace an output transform. Existing projects retain their saved
plugin encoding, including older Rec.709 or scene-linear settings. More detail:
[color science](docs/color-science.md) and [matching](docs/matching.md).

## Using the plugin

| Control | Behavior |
| --- | --- |
| Auto | One-shot WB, exposure, contrast, HDR, vibrance and saturation correction |
| WB | Pick WB, As Shot / Auto / Custom, Temp (K), Tint |
| Exposure | Scene-linear stops (EV) |
| Contrast | -100 to +100; zero is neutral |
| Match / Reference | Named references, selectable matching fields, Amount (%) |
| HDR | Highlights, Shadows, Whites, Blacks; -100 to +100 |
| Vibrance | -100 to +100 |
| Saturation | -100 to +100; zero is neutral |

Stop on a representative frame before using Auto or matching. Calculated values
remain editable and fixed as the playhead moves. Repeating Auto analyzes the input
without accumulating the previous result. Unreliable WB is preserved with a status
message. Status and the selected reference are read-only.

**WB:** As Shot resets this effect's WB to neutral defaults (6504 K, Tint 0),
preserving the incoming image; it does not read camera RAW metadata. Auto estimates
WB once. Editing Temp or Tint selects Custom. Pick WB prepares the current frame;
with the host OpenFX viewer overlay enabled, click a neutral area to apply WB.

**HDR:** Whites/Blacks affect the brightest/darkest luminance zones;
Highlights/Shadows affect the adjacent broad zones. The photographic -100..100
scale maps to ±3 EV-equivalent. These controls adjust smooth tonal regions.

### Match between clips

1. On the reference clip, stop at the desired frame. Enter a **Name** and press
   **Save Reference**. The snapshot includes this effect's current grade.
2. On the target clip, choose the reference and press **Load Reference**.
3. Select any combination of **White Balance**, **Exposure**, **Contrast** and
   **Color (Vibrance + Saturation)**, then press **Match**.
4. Refine **Amount (%)** or the ordinary sliders.

Color fits vibrance and saturation together. Unchecked fields retain their values.
There are 16 named slots: saving with Latest selected allocates a free slot;
saving to a numbered slot explicitly replaces it. A loaded snapshot is embedded in
project parameters, so it can survive moving the project without its shared library.

| System | Shared reference library |
| --- | --- |
| macOS | `~/Library/Application Support/Renzu Grade/References/` |
| Windows | `%LOCALAPPDATA%\Renzu Grade\References\` |
| Linux | `$XDG_DATA_HOME/renzu-grade/references/`, normally `~/.local/share/renzu-grade/references/` |

If a button reports that it is preparing a frame, let the viewer update and press
the **same button again**. For deferred WB Auto, select Auto again. Rendering never
changes sliders or saves library files. Downstream nodes and the viewer transform
are outside a reference; matching cannot identify the same subject across unrelated
compositions or copy a complete look. See [matching details](docs/matching.md).

## For developers

### Get the source and prerequisites

```bash
git clone https://github.com/eeexception/renzu-grade.git
cd renzu-grade
```

Distribution archives also include corresponding source under `Source/renzu-grade/`.
Run build commands from that source root. Official OpenFX headers are vendored in
`third_party/ofx`; no separate SDK download, Conan or Python is needed to compile
the plugin. Builds and standalone tests do not require Resolve to be installed.

| Platform | Build requirements |
| --- | --- |
| macOS arm64 | Apple Silicon, macOS 13+, full Xcode with its macOS SDK and Metal compiler, CMake 3.24+, Ninja, Git |
| Windows x64 | Visual Studio 2022 / Build Tools 2022 with **Desktop development with C++**, MSVC v143, Windows SDK, CMake 3.24+ on PATH, PowerShell 5.1+ |
| Linux x86_64 | C++17 GCC/Clang with static libstdc++/libgcc archives, CMake 3.24+, GNU make, binutils for verification |
| Release automation | Python 3.9+; GitHub CLI for the publishing job; neither is a plugin runtime dependency |

### Build and test — macOS arm64

Select the intended full Xcode installation with `xcode-select`. If its Metal
compiler is missing, install the component, then check prerequisites:

```bash
xcodebuild -downloadComponent MetalToolchain
./scripts/bootstrap_macos.sh
./scripts/test.sh --release
./scripts/package.sh
```

`test.sh` configures/builds and runs CTest. The bundle appears at
`build/RenzuGrade.ofx.bundle`; `package.sh` creates a ZIP and SHA256 file under
`dist/`. It does not publish anything. Packaging refuses to overwrite an existing
archive; move it aside before packaging the same version again.

For a plugin-only build, or CPU-only debugging on Apple Silicon:

```bash
./scripts/build.sh --release --without-tests --build-dir build-arm64
./scripts/test.sh --cpu-only --debug --build-dir build-cpu
./scripts/verify_bundle.sh build-cpu/RenzuGrade.ofx.bundle --cpu-only
```

Metal is enabled by default and its compiler is required. CMake and the bundle
verifier accept only arm64; Intel and universal macOS targets are excluded.
See [macOS build details](docs/build-macos.md).

### Build and test — Windows x64

```powershell
./scripts/build-windows.ps1 -RunTests -Package
# Separate Debug build:
./scripts/build-windows.ps1 -Configuration Debug -BuildDirectory build-windows-debug -RunTests
# Build/package only, matching the Build workflow:
./scripts/build-windows.ps1 -WithoutTests -Package
```

The script selects Visual Studio 2022 x64 and disables Metal. Output:
`build-windows/RenzuGrade.ofx.bundle/Contents/Win64/RenzuGrade.ofx`, with ZIP/SHA256
files in `build-windows/dist/`. Release uses the static MSVC runtime.
`-RunTests` executes tests; `-Package` creates the archive. See
[Windows build details](docs/build-windows.md) for equivalent direct CMake commands.

### Build and test — Linux x86_64

```bash
./scripts/build-linux.sh --test --package
# Separate sanitizer build:
./scripts/build-linux.sh --debug --sanitize --test --build-dir build-linux-asan
# Build/package only, matching the Build workflow:
./scripts/build-linux.sh --without-tests --package
./scripts/verify-linux.sh build-linux/RenzuGrade.ofx.bundle
```

Output: `build-linux/RenzuGrade.ofx.bundle/Contents/Linux-x86-64/RenzuGrade.ofx`,
with `.tar.gz`/SHA256 files in `build-linux/dist/`. `--test` executes tests;
`--package` creates the archive. Sanitizer builds cannot be packaged by this script.
GitHub distribution builds use a glibc 2.28 container. A local build on a newer
system may require that system's newer glibc. See [Linux build details](docs/build-linux.md).

### Configure the build

Use separate build directories when changing platform, generator, architecture
or sanitizer configuration. Main CMake settings:

| Setting | Default / purpose |
| --- | --- |
| `RENZU_ENABLE_METAL` | ON on macOS, OFF on Windows/Linux; Metal requires macOS |
| `RENZU_BUILD_TESTS` | ON; builds the standalone test targets |
| `RENZU_SANITIZE` | OFF; enables ASan + UBSan with GCC/Clang, unsupported with MSVC |
| `OFX_SDK_ROOT` | `third_party/ofx`; alternate SDK must contain `include/ofxImageEffect.h` |
| `CMAKE_BUILD_TYPE` | Release/Debug for single-configuration generators; scripts default to Release |
| `CMAKE_OSX_ARCHITECTURES` | `arm64` on macOS; other values are rejected |
| `CMAKE_OSX_DEPLOYMENT_TARGET` | `13.0` by default |

macOS/Linux build scripts also read `OFX_SDK_ROOT` and `RENZU_BUILD_DIR` environment
variables; an explicit `--build-dir` takes precedence over `RENZU_BUILD_DIR`.
Windows uses `-BuildDirectory`. For an alternate SDK or additional options on
Windows, configure directly with CMake, as shown in the platform build guide.

The project version is set in `project(RenzuGrade VERSION ...)` in
[CMakeLists.txt](CMakeLists.txt). Update the OpenFX major/minor version in
[src/ofx/Plugin.cpp](src/ofx/Plugin.cpp) when those components change, and update
release documentation/notices. Release workflows validate the version; they do not bump it.

### Script reference

All paths below are relative to the repository or packaged source root.
`bundle` means the entire `RenzuGrade.ofx.bundle` directory.

| Script | Platform | Purpose and invocation |
| --- | --- | --- |
| [bootstrap_macos.sh](scripts/bootstrap_macos.sh) | macOS | `./scripts/bootstrap_macos.sh` checks Xcode, Metal, CMake, Ninja, Git and OpenFX headers; it does not install tools |
| [build.sh](scripts/build.sh) | macOS arm64 | `./scripts/build.sh [options]` configures, compiles and verifies the bundle |
| [test.sh](scripts/test.sh) | macOS arm64 | `./scripts/test.sh [build options]` builds and runs CPU/OpenFX/Metal CTest suites |
| [verify_bundle.sh](scripts/verify_bundle.sh) | macOS | `./scripts/verify_bundle.sh [bundle] [--cpu-only]` checks arm64 layout, plist, exported entry points, dependencies, shader presence and signature; default bundle is `build/RenzuGrade.ofx.bundle` |
| [package.sh](scripts/package.sh) | macOS | `./scripts/package.sh [bundle] [--cpu-only]` verifies and packages the default/custom bundle, licenses, source and docs into `dist/`; use the explicit bundle argument before `--cpu-only` |
| [install.sh](scripts/install.sh) | macOS | `./scripts/install.sh [bundle] [--cpu-only]` verifies and installs the default/custom bundle with backup; extracted distributions must pass `./RenzuGrade.ofx.bundle` explicitly |
| [uninstall.sh](scripts/uninstall.sh) | macOS | `./scripts/uninstall.sh` moves the installed plugin outside the scan tree, preserving a recovery copy |
| [build-windows.ps1](scripts/build-windows.ps1) | Windows x64 | `./scripts/build-windows.ps1 [options]` configures/compiles, optionally runs tests and packages with CPack |
| [build-linux.sh](scripts/build-linux.sh) | Linux x86_64 | `./scripts/build-linux.sh [options]` configures/compiles, optionally runs tests and packages with CPack |
| [verify-linux.sh](scripts/verify-linux.sh) | Linux | `./scripts/verify-linux.sh [bundle] [max-glibc]` checks ELF architecture, exports, dependencies and glibc requirement; defaults to the `build-linux` bundle; CI passes `2.28` as the maximum |
| [release.py](scripts/release.py) | Python 3.9+ | Validates release metadata or the complete archive/checksum set and writes release manifests; commands below |
| [publish_release.py](scripts/publish_release.py) | Python 3.9+ / GitHub CLI | Checks or creates/extends a version release for the selected platforms, preserves existing files and enforces the source commit; `--check` is read-only, `--dry-run` previews uploads |
| [common.sh](scripts/common.sh) | Bash helper | Sourced by shell scripts; supplies project-root lookup, prerequisite/error helpers and administrator-write handling; not a standalone command |

Build-script options:

| macOS `build.sh` / `test.sh` | Meaning |
| --- | --- |
| `--release` / `--debug` | Build configuration; Release is the default |
| `--arch arm64` | Explicit architecture; arm64 is also the default and the only supported value |
| `--build-dir <path>` | Custom build directory; defaults to `build/` |
| `--cpu-only` | Disable Metal for CPU development/testing on arm64 |
| `--without-tests` | Build only the plugin; accepted by `build.sh`, rejected by `test.sh` |

| Windows `build-windows.ps1` | Meaning |
| --- | --- |
| `-Configuration Release` / `Debug` | Release is the default; packaging requires Release |
| `-BuildDirectory <path>` | Custom build directory; defaults to `build-windows/` |
| `-RunTests` | Execute CTest after compilation |
| `-WithoutTests` | Disable test targets; cannot be combined with `-RunTests` |
| `-Package` | Create a ZIP and SHA256 sidecar with CPack |

| Linux `build-linux.sh` | Meaning |
| --- | --- |
| `--release` / `--debug` | Release is the default; packaging requires Release |
| `--build-dir <path>` | Custom build directory; defaults to `build-linux/` |
| `--test` | Execute CTest after compilation |
| `--without-tests` | Disable test targets; cannot be combined with `--test` |
| `--package` | Create a `.tar.gz` and SHA256 sidecar with CPack |
| `--sanitize` | Enable ASan/UBSan; use a separate Debug directory; cannot be packaged |

Release-tool examples:

```bash
python3 -B -m unittest discover -s tests -p 'test_release.py' -v
# Validate this version and source commit:
python3 scripts/release.py metadata --tag v0.3.0 --commit "$(git rev-parse HEAD)"
# Put the three archives and their .sha256 sidecars in release-assets first:
python3 scripts/release.py assets --tag v0.3.0 --commit "$(git rev-parse HEAD)" --directory release-assets
```

`release.py` accepts `metadata` or `assets`, required `--tag` and `--commit`,
`--platform all` / `macos` / `windows` / `linux` (default `all`),
optional `--source <source-root>` (defaults to its own source root), and
`--directory <asset-directory>` for assets mode. Metadata is printed as JSON.
Assets mode writes `SHA256SUMS` and `release-manifest.json` for All, or
platform-specific names such as `SHA256SUMS-macos` and `release-manifest-macos.json`. The tag must
match the CMake version and the commit must be a full SHA. This script does not
create tags or publish releases.

### GitHub Actions and releases

The Actions sidebar provides 12 separate manual launchers:

| Platform | Tests | Build | Release |
| --- | --- | --- | --- |
| macOS arm64 | [Run macOS Tests](.github/workflows/run-macos-tests.yml) | [Run macOS Build](.github/workflows/run-macos-build.yml) | [Run macOS Release](.github/workflows/run-macos-release.yml) |
| Windows x64 | [Run Windows Tests](.github/workflows/run-windows-tests.yml) | [Run Windows Build](.github/workflows/run-windows-build.yml) | [Run Windows Release](.github/workflows/run-windows-release.yml) |
| Linux x86_64 | [Run Linux Tests](.github/workflows/run-linux-tests.yml) | [Run Linux Build](.github/workflows/run-linux-build.yml) | [Run Linux Release](.github/workflows/run-linux-release.yml) |
| All | [Run All Tests](.github/workflows/tests.yml) | [Run All Builds](.github/workflows/build.yml) | [Run All Releases](.github/workflows/make-release.yml) |

The All Tests/Builds workflows also run automatically on pushes and pull requests.
Platform launchers run only their platform's plugin jobs; Linux Tests includes
ASan/UBSan. Shared release-tool checks run with every test selection. Build creates
archives with test targets disabled, verifies packages and builds their source.

After merging the workflows into the default branch, choose the desired launcher
in Actions and click **Run workflow**. Release launchers accept a tag matching
CMake VERSION, such as `v0.3.0`, and run the selected tests and builds first.
**draft** defaults to true; disable it to publish after uploading. **prerelease**
marks a newly created release. **dry_run** previews without changing a tag/release.

Platform releases share one version tag. For example, run macOS Release first,
then Windows Release for the same source commit to add Windows files. Existing
assets are verified and preserved; All Releases can complete that release using
its already-uploaded files. Release notes, prerelease marking and an existing
published state are preserved when extending a release. Different source commits
for the same tag are rejected. See [workflow and release details](docs/github-actions.md).

`scripts/publish_release.py` requires `--repo OWNER/NAME`, `--tag`, `--commit`, and
optionally `--platform`, `--source`, `--directory`. Use `--check` for metadata and
existing-tag checks; uploads require `--directory`. It creates a draft by default;
`--publish` publishes a new/existing draft after successful uploads, `--prerelease`
applies to a new release, and `--dry-run` prepares/validates without remote writes.
GitHub authentication is required for release lookups/downloads/publication.

## Compatibility

The multithreaded CPU and Metal backends share per-pixel math. RGB/RGBA buffers
support byte, short, half and float; Metal textures use negotiated RGBA formats.
Rendering handles signed row strides, nonzero bounds, alpha and extended-range
floats. Integer/half RGB is limited to its storage range; float safety bounds are
±1e12, and non-finite RGB becomes zero. Alpha is preserved independently.

No playback-time adaptation, keyframes, local masks, face detection, clarity,
texture, dehaze or spatial enhancement is included. Earlier grading parameter
identifiers remain hidden and persistent to retain stored grades.

Standalone/CI tests verify algorithms and the OpenFX contract. Actual Resolve
loading, native UI, picker routing, undo/redo, project persistence and playback/
export require the [manual acceptance checklist](docs/manual-test-plan.md) on the
intended host. Metal tests report skipped when no GPU device is accessible;
inspect CTest output. [Validation records](docs/validation.md) include historical
build results; the current macOS target is arm64 only.

## Further documentation and license

[Architecture](docs/architecture.md) · [Color science](docs/color-science.md) ·
[Auto algorithms](docs/auto-correction.md) · [Matching](docs/matching.md) ·
[Contributing](CONTRIBUTING.md) · [Changelog](CHANGELOG.md)

Project code is [GNU GPL version 3 only](LICENSE) (`GPL-3.0-only`). The official
vendored OpenFX headers are BSD-3-Clause. Distribution archives include the
corresponding source/build scripts under `Source/renzu-grade/` and retain the
OpenFX notices. See [NOTICE](NOTICE).

DaVinci Resolve/DWG belong to Blackmagic Design, Lightroom/Adobe to Adobe, and
macOS/Metal/Xcode to Apple. This independent project is not sponsored, endorsed
or certified by those companies.
