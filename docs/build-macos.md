# Building, installation and distribution on macOS

Use macOS 13+, full Xcode (C++17 compiler and macOS SDK), CMake 3.24+ and Ninja.
Xcode must be selected via `xcode-select`. Modern Xcode may require downloading
its separate Metal component:

```bash
xcodebuild -downloadComponent MetalToolchain
./scripts/bootstrap_macos.sh
./scripts/build.sh --release
./scripts/test.sh --release
```

Bootstrap only checks the machine. The build uses vendored official OpenFX
headers; no package manager, Conan, Python or network is used at configure/runtime.
Missing Metal causes a clear configure error. Developer CPU-only configuration
must be explicit, using a separate directory:

```bash
./scripts/test.sh --cpu-only --debug --build-dir build-cpu
./scripts/build.sh --release --arch x86_64 --build-dir build-intel
./scripts/build.sh --release --arch universal --build-dir build-universal
```

A Universal Binary is compiled from the same CMake target with
`CMAKE_OSX_ARCHITECTURES=arm64;x86_64`. The metallib is a GPU resource, not a
CPU architecture slice. An Intel-only test executable on Apple Silicon may need
Rosetta; running tests on the actual target architecture is preferred. Universal
compilation alone does not validate Intel GPU execution.

Equivalent direct CMake:

```bash
cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=13.0 \
  -DCMAKE_OSX_ARCHITECTURES=arm64 \
  -DRENZU_ENABLE_METAL=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure -V
./scripts/verify_bundle.sh build/RenzuGrade.ofx.bundle
```

An alternate official SDK root must contain `include/ofxImageEffect.h`,
`ofxGPURender.h`, and the Metal texture definitions used by the project. For shell
scripts, export `OFX_SDK_ROOT=/path/to/openfx`; for direct CMake pass
`-DOFX_SDK_ROOT=/path/to/openfx`. The default pinned header subset has all needed
facilities without building upstream support libraries.

Sanitizers (CPU and OpenFX host-contract harness):

```bash
cmake -S . -B build-asan -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DRENZU_ENABLE_METAL=OFF -DRENZU_SANITIZE=ON
cmake --build build-asan --parallel
ctest --test-dir build-asan --output-on-failure -V
```

## Bundle and verification

```
RenzuGrade.ofx.bundle/
  Contents/
    Info.plist
    MacOS/RenzuGrade.ofx
    Resources/renzu_grade.metallib
```

The CMake build compiles Metal offline once and ad-hoc signs the completed bundle.
The verifier checks plist syntax/identity, expected layout, Mach-O architectures,
OpenFX entry points per slice, system-only runtime dependencies, metallib presence,
absence of unexpected symlinks, and signature integrity. A CPU-only verification
uses `./scripts/verify_bundle.sh build-cpu/RenzuGrade.ofx.bundle --cpu-only`.

The CPU and OpenFX tests run without Resolve. The Metal test executes actual
GPU commands and returns skip code 77 when no device is available, including
restricted CI/sandbox sessions. A skipped GPU test is not a parity pass. Check
`build/Testing/Temporary/LastTest.log`. Successful tests do not establish Resolve
UI, playback/export, host routing, or project-serialization compatibility.

## Install and recovery

Quit Resolve, then run:

```bash
./scripts/install.sh build/RenzuGrade.ofx.bundle
```

The script verifies before writing and uses administrator rights only for the
system installation. It stages a copy, moves an existing version to a timestamped
recovery directory outside Plugins, then renames the new bundle into place.
Restart Resolve. The standard destination is `/Library/OFX/Plugins/`; no unverified
per-user scan path is assumed.

To uninstall:

```bash
./scripts/uninstall.sh
```

It validates the Renzu bundle identifier and moves exactly that bundle outside
the plugin scan directory, preserving a recovery copy under
`/Library/OFX/RenzuGrade-Backups/`. Missing installations are safe. Other effects
and previous backups are untouched. Restart Resolve afterward. Recovery copies
can be reinstalled by giving their full bundle path to install.sh.

## Packaging and signing

```bash
./scripts/package.sh build/RenzuGrade.ofx.bundle
```

The zip in `dist/` includes the verified bundle, GNU GPLv3 license, notices, SDK
license, README, install/uninstall/verifier scripts, install instructions and
manual checklist. Corresponding source, vendored SDK headers, build scripts and
documentation are included under `Source/renzu-grade/`. All bundle files have SHA-256 hashes checked before archival;
the zip is integrity-tested and gets an archive SHA-256 sidecar. Existing named
archives are not overwritten. Nothing is published, uploaded, or released.

Ad-hoc signing is useful for local development and requires no Apple account.
It is not notarization and does not convey a trusted developer identity for
internet distribution. A distributor can separately Developer-ID-sign the built
bundle and notarize an archive using their own credentials and Apple tooling,
then re-verify and package. No credentials or signing service is part of this repo.

## Troubleshooting

- **Missing Metal compiler:** install the Xcode component, ensure the intended
  full Xcode is selected, then retry `xcrun -sdk macosx metal --version`. A tool
  lookup refresh via `xcrun --kill-cache` may be necessary after installation.
- **Sandbox reports GPU unavailable:** run the standalone Metal tests from an
  ordinary terminal with device access; do not turn a skipped test into a pass.
- **Compiler cannot write its module cache:** use a normal development terminal
  or provide a writable compiler module cache; this is distinct from shader errors.
- **Effect absent in Resolve:** restart the host, verify architecture and bundle,
  inspect host plugin-load diagnostics and security policy. Avoid repeatedly
  replacing a loaded bundle while Resolve is open.
- **Loaded successfully in Preferences, but absent from Effects:** that status
  confirms binary loading, not successful effect registration. A Resolve log entry
  `describe failed on plugin org.renzu.grade, error code 3` followed by
  `No context is available` identified rejection of optional description metadata
  in the initial build. Rebuild the current source, quit Resolve, reinstall, then
  restart. The current plugin tolerates unavailable optional metadata and still
  registers its contexts. The installer replaces the binary, allowing the host
  to detect its changed modification time and rescan it.
- **Wrong color response:** new instances expect DWG/Intermediate before the output
  transform; use appropriate CSTs in other workflows. Older instances keep their
  saved encoding. The simplified panel has no Advanced/input-space menu.
- **Button cannot access a frame:** let the viewer update, then press that same
  action again. For WB, select Auto again. No separate capture controls are needed.

For CPU-only Windows/Linux distributions, see [Windows](build-windows.md) and
[Linux](build-linux.md). Metal defaults to OFF on those platforms. The macOS shell
build/install/package scripts remain macOS-only.
