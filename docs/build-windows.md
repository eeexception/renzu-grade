# Windows x64: CPU build, tests and installation

Use an x64 Windows system with CMake 3.24+ and Visual Studio 2022 or Build Tools
2022. Install **Desktop development with C++**, including MSVC v143 and a Windows
SDK. CMake/CTest/CPack must be on PATH. PowerShell 5.1+ is sufficient. No Python,
CUDA toolkit, OpenCL SDK, Metal or Resolve installation is needed to build/test.

From the repository root:

```powershell
./scripts/build-windows.ps1 -RunTests -Package
```

The script selects the Visual Studio 2022 x64 generator and disables Metal.
`-RunTests` runs the tests; `-Package` creates a Release archive. They can be
combined as above. CI uses `-WithoutTests -Package` for the separate Build workflow.
For Debug tests, use a separate directory:

```powershell
./scripts/build-windows.ps1 -Configuration Debug -BuildDirectory build-windows-debug -RunTests
```

If a local PowerShell execution policy blocks scripts, use the equivalent CMake
commands below or your organization's permitted script policy; no machine-wide
policy change is needed by this project.

```powershell
cmake -S . -B build-windows -G 'Visual Studio 17 2022' -A x64 -DRENZU_ENABLE_METAL=OFF -DRENZU_BUILD_TESTS=ON
cmake --build build-windows --config Release --parallel
ctest --test-dir build-windows -C Release --output-on-failure --no-tests=error
cpack --config build-windows/CPackConfig.cmake -C Release
```

Run packaging only after a successful build/test. The build uses the static MSVC
runtime (`/MT` in Release), so the plugin has no separate Visual C++ redistributable
dependency. Windows ARM and 32-bit Windows targets are not supported.

## Output and installation

```text
build-windows/RenzuGrade.ofx.bundle/
  Contents/Win64/RenzuGrade.ofx
build-windows/dist/
  renzu-grade-<version>-windows-x86_64-cpu-only.zip
  renzu-grade-<version>-windows-x86_64-cpu-only.zip.sha256
```

The `.ofx` file is a native x64 DLL with the two OpenFX entry points exported.
The bundle layout follows the [OpenFX packaging specification](https://openfx.readthedocs.io/en/latest/Reference/ofxPackaging.html).
The archive includes GPLv3 source and build scripts under `Source/renzu-grade/`.

1. Quit DaVinci Resolve.
2. Extract the distribution. Move an existing RenzuGrade bundle outside the OFX
   Plugins directory as a backup.
3. Copy the entire `RenzuGrade.ofx.bundle` folder into
   `C:\Program Files\Common Files\OFX\Plugins\` (administrator permission may be required).
4. Restart Resolve. Open Effects and search for **Renzu Grade**, in **Color Grading**.

The final binary path must be:
`C:\Program Files\Common Files\OFX\Plugins\RenzuGrade.ofx.bundle\Contents\Win64\RenzuGrade.ofx`.
For uninstall, close Resolve and move only that bundle outside Plugins. References
in `%LOCALAPPDATA%\Renzu Grade\References\` are preserved.

## Verification and limits

The Windows job in **Run Windows Tests** / **Run All Tests** runs all four CPU suites: color/image processing,
reference matching/library storage, photographic controls and the OpenFX host
contract, including loading the compiled plugin. The separate **Run Windows Build** / **Run All Builds** workflow
compiles only the plugin, verifies the ZIP checksum/layout and builds the
accompanying source with test targets disabled. Logs belong to Tests; distributions
belong to Build. [Run Windows Release / Run All Releases](github-actions.md) requires both workflows to
succeed for its source commit. Downloading an Actions artifact adds
an outer ZIP; extract it first, then the actual distribution ZIP inside it.

Tests cover Unicode reference names/paths, replacing existing slots, and concurrent
saves from separate Windows processes. Contract tests use `LoadLibraryW` and do
not simulate the Resolve UI. Complete the [host checklist](manual-test-plan.md)
on a real Windows Resolve installation before claiming host acceptance.

This version processes on CPU only; all grading, Auto, WB picker and Match controls
use the same algorithms as macOS. CPU rendering can be slower. Resolve's own
GPU/system requirements still apply. This is not a code-signed Windows installer.
If discovery fails, check bundle nesting, Resolve's Video Plugins preferences and
plugin-load logs. Avoid replacing a loaded DLL while Resolve is running.
