# Contributing

Contributions are licensed under GPL-3.0-only and must be compatible with GNU GPL
version 3. Retain third-party notices. Please
open a focused issue or pull request; no contributor license agreement is required.

1. Run `./scripts/bootstrap_macos.sh` and `./scripts/test.sh`.
2. Format C++/Objective-C++/Metal using `xcrun clang-format -i <changed files>`.
3. Keep all per-pixel math in `include/renzu/Kernel.h` so both renderers agree.
4. Add deterministic tests for algorithm changes; run the real Metal suite.
5. For host changes, follow `docs/manual-test-plan.md` and report Resolve edition,
   build, macOS version, hardware, project encoding, and reproducible steps.
6. Do not claim host compatibility from a contract-test or CI pass alone.

`cmake -S . -B build-asan -G Ninja -DRENZU_SANITIZE=ON
-DRENZU_ENABLE_METAL=OFF -DCMAKE_BUILD_TYPE=Debug` followed by build and CTest
runs the portable paths with address/undefined-behavior sanitizers. Put the
configuration on one shell line or use shell line continuations.

Do not commit generated bundles, developer machine paths, sample footage without
permission, credentials, or binary dependencies. Keep PRs small enough to review.
