# Contributing

Contributions are licensed under GPL-3.0-only and must be compatible with GNU GPL
version 3. Retain third-party notices. Please
open a focused issue or pull request; no contributor license agreement is required.

1. Run the build/tests for your platform: `./scripts/test.sh` on macOS (after
   `./scripts/bootstrap_macos.sh`), `./scripts/build-windows.ps1 -RunTests` on
   Windows, or `./scripts/build-linux.sh --test` on Linux.
2. Format C++/Objective-C++/Metal using `clang-format -i <changed files>` (available via Xcode on macOS).
3. Keep all per-pixel math in `include/renzu/Kernel.h` so both renderers agree.
4. Add deterministic tests for algorithm changes; run the real Metal suite.
5. For host changes, follow `docs/manual-test-plan.md` and report Resolve edition,
   build, operating system/version, hardware, rendering backend, project encoding, and reproducible steps.
6. Do not claim host compatibility from a contract-test or CI pass alone.

`cmake -S . -B build-asan -G Ninja -DRENZU_SANITIZE=ON
-DRENZU_ENABLE_METAL=OFF -DCMAKE_BUILD_TYPE=Debug` followed by build and CTest
runs the portable paths with address/undefined-behavior sanitizers. Put the
configuration on one shell line or use shell line continuations.
MSVC does not support this combined sanitizer option; CI runs it on Linux.
See [Windows](docs/build-windows.md) and [Linux](docs/build-linux.md) for toolchains,
packaging and the CI checks. Windows/Linux distribution packages must be Release
builds without sanitizers. No Resolve installation is needed for standalone tests.

CI is split into [platform-specific and All test/build/release workflows](docs/github-actions.md). Run
release-tool checks with `python3 -B -m unittest discover -s tests -p 'test_release.py'`.
Python 3.9+ is used only by release automation, not plugin compilation or runtime.

Do not commit generated bundles, developer machine paths, sample footage without
permission, credentials, or binary dependencies. Keep PRs small enough to review.
