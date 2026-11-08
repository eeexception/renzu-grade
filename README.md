# Renzu Grade

**Renzu Grade for DaVinci Resolve** is an independent GPLv3 OpenFX photographic
grading plugin. Version **0.3.0** runs inside Resolve's native effects panel,
without another app, DCTL, cloud service or runtime internet access.

## The panel

The controls follow this order:

1. **Auto** — one-shot white balance, exposure, contrast, HDR, vibrance and saturation.
2. **WB** — Pick WB, As Shot / Auto / Custom, Temp (K), Tint.
3. **Exposure** — scene-linear stops (EV).
4. **Contrast** — -100 to +100, zero is neutral.
5. **Match / Reference** — named references and independent field checkboxes.
6. **HDR** — Highlights, Shadows, Whites, Blacks (-100 to +100).
7. **Vibrance** (-100 to +100).
8. **Saturation** (-100 to +100, zero is neutral).

Status and the selected reference are read-only. Native Resolve controls determine
the visual styling; this does not reproduce Adobe's proprietary UI or algorithms.
There is no Advanced group, coordinate entry, or separate Capture/Apply panel.

**WB:** As Shot resets this effect's WB to its neutral defaults (6504 K, Tint 0),
preserving the incoming image. It does not read camera RAW metadata. Auto estimates
WB from the current frame once. Editing Temp or Tint selects Custom. Temperature
uses a defined correlated-temperature illuminant model normalized at D65; Tint
is an additional normalized magenta/green correction. Pick WB prepares the current
frame; clicking a neutral area in the OpenFX viewer overlay applies WB immediately.

**Auto:** stop on a representative frame and press Auto. All calculated values
remain editable and fixed as the playhead moves. It uses robust luminance statistics,
conservative color targets and confidence-gated WB. Repeated Auto analyzes input,
without accumulating the previous result. Unreliable WB is preserved with a status.

**HDR:** the four smooth luminance zones are ordered and named like photographic
basic tone controls: Highlights, Shadows, Whites, Blacks. Whites/Blacks affect the
brightest/darkest zones; Highlights/Shadows affect the adjacent broad tonal zones.
They are not independent per-channel gains. The -100..100 scale maps to ±3 EV-equivalent.

## Matching between clips

1. On the reference clip, stop at the desired frame. Enter a **Name** and press
   **Save Reference**. The snapshot includes this effect's current grade.
2. On the target clip, select a reference and press **Load Reference** to refresh it.
3. Check any combination of **White Balance**, **Exposure**, **Contrast**, and
   **Color (Vibrance + Saturation)**, then press **Match**.
4. Refine with **Amount (%)** or the ordinary sliders.

Color fits vibrance and saturation together because multiple combinations can
produce a similar image. Unchecked fields retain their values. There are 16 stable
named slots; Latest creates a new entry, and saving to a numbered slot explicitly
replaces it. Loaded snapshots are embedded in project parameters, so grades and
re-matching can survive moving a project without the shared library. The library
lives at `~/Library/Application Support/Renzu Grade/References/` on macOS,
`%LOCALAPPDATA%\Renzu Grade\References\` on Windows, and
`$XDG_DATA_HOME/renzu-grade/references/` on Linux (default:
`~/.local/share/renzu-grade/references/`).

If the host declines immediate frame access, Status asks you to let the viewer
update and press the **same button again**. WB Auto similarly asks you to select
Auto again. No technical capture buttons are needed. This remains one-shot:
rendering never changes sliders or saves library files.

See [matching](docs/matching.md) for details and limitations. Global matching cannot
identify the same subject across unrelated compositions or copy a complete look.

## Color management

**New instances expect DaVinci Wide Gamut / DaVinci Intermediate.** Use that working
space in a managed project, before its output transform. For other workflows,
use Resolve CSTs to enter and leave DWG/Intermediate around this effect. The panel
has no input-space menu. Existing projects retain their saved encoding, including
Rec.709 gamma 2.4 or scene-linear Rec.709 from earlier versions.

The engine decodes before exposure/WB and outputs the same representation. It is
not a camera decoder, display rendering transform, gamut mapper or LUT inverter.
References and targets must be captured at equivalent grading points. Downstream
nodes and the viewer display transform are outside a saved reference.

## Build, test and install

| Platform | Architecture | Rendering | Build instructions |
| --- | --- | --- | --- |
| macOS 13+ | Apple Silicon, Intel, universal | Metal + CPU | [macOS](docs/build-macos.md) |
| Windows | x64 | CPU only | [Windows](docs/build-windows.md) |
| Linux with glibc 2.28+ | x86_64 | CPU only | [Linux](docs/build-linux.md) |

Windows/Linux builds include the same photographic controls, Auto, WB picker and
Match. They do not include CUDA/OpenCL acceleration. CPU processing can be slower,
and Resolve's own GPU/system requirements still apply. Host acceptance on each
system remains separate from standalone testing.

**Windows/Linux downloads:** open the repository's **Actions** tab, select a
successful **Build** run, and download the
appropriate distribution under **Artifacts**. Extract the artifact, verify the
included SHA256 checksum if desired, then extract the inner `.zip` (Windows) or
`.tar.gz` (Linux). Follow its `INSTALL.txt`; copy the entire `.ofx.bundle` folder,
not just the `.ofx` binary. Archives include corresponding source and build scripts.

Quick source builds (CMake 3.24+ and the platform compiler):

```powershell
# Windows x64: Visual Studio 2022 Build Tools, Desktop development with C++.
./scripts/build-windows.ps1 -RunTests -Package
```

```bash
# Linux x86_64: C++17 compiler and make.
./scripts/build-linux.sh --test --package
```

Packages appear in `build-windows/dist/` or `build-linux/dist/` with SHA256 files.
The **Tests** workflow runs CPU/OpenFX suites on both systems for pushes and pull
requests, plus macOS CPU/Metal and Linux ASan/UBSan checks. **Build** independently
produces all four platform archives, verifies their structure/checksums and builds
the accompanying source with test targets disabled. Published Linux binaries are built against glibc
2.28 and bundle their C++ runtime statically; a local build on a newer distribution
may require its newer glibc. There are no Linux ARM or Windows ARM builds.

**Make Release** is a separate manual workflow. It validates a tag matching the
project version (for example `v0.3.0`), runs Tests, then Build, and creates a GitHub
Release only after both succeed for that same commit. It defaults to a draft;
disable **draft** to publish, or enable **dry_run** to validate without creating a
tag/release. See [GitHub workflows and releases](docs/github-actions.md).

**macOS:** Apple Silicon is the primary development platform; Intel/universal builds are available.
Requirements: full Xcode and Metal compiler, CMake 3.24+, Ninja and a C++17 compiler.
Pinned official OpenFX headers are included; no Python or Conan is used.

```bash
# If the Metal compiler is missing:
xcodebuild -downloadComponent MetalToolchain
./scripts/bootstrap_macos.sh
./scripts/build.sh --release
./scripts/test.sh --release
```

Output: `build/RenzuGrade.ofx.bundle`, containing the binary, Info.plist, compiled
Metal library and ad-hoc signature. Metal is enabled by default; missing tools are
a build error. CPU-only development builds must be explicitly requested.

```bash
./scripts/build.sh --release --arch universal --build-dir build-universal
./scripts/test.sh --cpu-only --debug --build-dir build-cpu
```

Quit Resolve before installing:

```bash
./scripts/install.sh
# Restart Resolve, then find Renzu Grade under Effects / Color Grading.
./scripts/uninstall.sh
# Optional archive, with no publishing:
./scripts/package.sh
```

The installer uses `/Library/OFX/Plugins/RenzuGrade.ofx.bundle` and requests sudo
only where necessary. Previous versions and uninstalls are preserved outside the
scan tree under `/Library/OFX/RenzuGrade-Backups/`. Development builds are ad-hoc
signed, not Developer ID signed/notarized. See [build instructions](docs/build-macos.md).

## Compatibility and verification

Target host: Resolve 21.x. A successful standalone build is not host certification.
Real Resolve UI presentation, overlay routing, undo/redo, project persistence,
playback/export, Windows/Linux host loading, and Intel GPU behavior require the
[manual checklist](docs/manual-test-plan.md).

- Real Metal buffer/texture rendering and a multithreaded CPU backend share pixel math.
- RGB/RGBA buffers support byte, short, half and float; textures use negotiated RGBA formats.
- Render windows, signed row strides, alpha and extended-range floats are handled.
- Integer/half RGB is limited to storage range; float safety bounds are ±1e12.
  Non-finite RGB becomes zero. Alpha is preserved independently.
- Tiles are not advertised. No playback-time auto correction, keyframes, face detection,
  local masks, clarity, texture, dehaze or spatial enhancement is included.
- Prior grading parameter identifiers remain hidden and persistent for old projects.
  This preserves stored grades while the new visible controls use photographic scales.

GPU tests execute actual Metal and report skipped when no device is accessible;
inspect CTest output. [Validation records](docs/validation.md) distinguish automated
checks from host verification. No host screenshots are fabricated; actual examples
can be added after manual verification with permission to show the footage.

## Development and license

[Architecture](docs/architecture.md) · [Color science](docs/color-science.md) ·
[Auto algorithms](docs/auto-correction.md) · [Matching](docs/matching.md) ·
[Contributing](CONTRIBUTING.md) · [Changelog](CHANGELOG.md)

Project code is [GNU GPL version 3 only](LICENSE) (`GPL-3.0-only`). The official vendored OpenFX headers are BSD-3-Clause.
Distributable archives include the corresponding source and build scripts under
`Source/renzu-grade/`. The OpenFX license and notices remain intact.
See [NOTICE](NOTICE). DaVinci Resolve/DWG belong to Blackmagic Design, Lightroom/
Adobe to Adobe, and macOS/Metal/Xcode to Apple. This independent project is not
sponsored, endorsed or certified by those companies.
