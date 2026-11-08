# Implementation validation record

Historical entries include Intel/universal build experiments. The current macOS
target is Apple Silicon arm64 only; those older results do not describe current
platform support.

These checks were executed locally for the initial 0.1.0 implementation. This
record is evidence of standalone checks, not a Resolve compatibility statement.

Environment: Apple Silicon (Apple M4 Pro), macOS 27.0 build 26A428, Xcode 27.1
build 27A9275, Apple Clang 21, Metal compiler 32023.921, CMake 4.4.5, Ninja 1.13.2.
The downloaded Xcode Metal component was installed before GPU compilation.
Native plugin and shaders target macOS 13.0; the binary deployment command was inspected.

| Check | Actual result |
| --- | --- |
| `./scripts/bootstrap_macos.sh` | Passed prerequisite checks |
| `./scripts/test.sh --release` with real GPU access | 3/3 suites passed, no skips |
| Core color/image/analysis suite | 80,154 assertions passed |
| OpenFX C-suite contract harness | 228 assertions passed |
| Real Metal CPU parity harness on Apple M4 Pro | 738,256 assertions passed |
| ASan + UBSan Debug CPU/OpenFX build | 2/2 suites passed, no sanitizer diagnostics |
| Universal arm64 + x86_64 build | Compiled and linked |
| Native and universal bundle verification | Passed layout/plist, exports, architectures, dependencies and ad-hoc signature checks |
| Shell syntax and native Bash command-dispatch check | Passed |
| Source `clang-format --dry-run --Werror` | Passed |
| Release packaging | Native archive generation, per-file SHA-256 and zip integrity checks executed successfully |

The Metal harness executes RGB/RGBA buffers with byte/short/half/float storage,
nonzero origins, signed/padded strides, render windows, all three encodings,
identity and combined grading, premultiplication, private GPU memory, explicit
buffer/texture capture, RGBA textures in all negotiated formats, and rejection of
mismatched texture metadata. Error tolerances are documented in color-science.md.
Extended-range stress samples include Intermediate code values well above nominal
white, so absolute errors can be large at enormous magnitudes; the maximum
fraction of the specified allowed error in the buffer run was 0.821659.

An ordinary sandboxed process could not enumerate a Metal device, and correctly
reported the GPU test as skipped. The final complete suite was separately run
with real device access and passed. A skip is not treated as a GPU pass.

The OpenFX test loads the real compiled plugin through its exported C entry
points and supplies test suites. It checks metadata, defaults/ranges, source-to-
output processing, callback time, slider writes, independent auto operations,
strength/manual offsets, noncompounding, edit grouping, capture/apply behavior,
no parameter writes during render, and reconstruction from saved host parameter
values. It does not run DaVinci Resolve or serialize a Resolve project.

Not executed here: system plugin installation/uninstallation, Resolve UI/overlay
routing, direct callback access in Resolve, project save/reopen, host undo/redo,
playback/export, 1080p/4K/6K performance or Intel GPU runtime. The provided
manual-test-plan.md covers these. The GitHub Actions workflow is configured but
has not been run remotely; no GitHub repository/release was created or published.

## Resolve discovery regression fix

The user's Resolve 21 session loaded the initial bundle but omitted the effect
from the Color Page library. Its real `ResolveDebug.txt` recorded
`describe failed on plugin org.renzu.grade, error code 3` followed by
`No context is available for org.renzu.grade`. The host's plugin cache contained
the effect label/group but empty contexts and depths, locating the failure at
the optional description property written before those mandatory registrations.
The Resolve-supplied support library also treats that metadata as optional.

A new host-contract regression test reproduced the failure before the fix.
The corrected plugin tolerates ErrUnknown/ErrUnsupported for optional description,
group-open state and Metal capability properties; genuine property errors still
propagate. Describe and DescribeInContext now succeed in tests with those
properties rejected. The updated native Release suite passed 3/3 tests with real
Metal access: 80,154 core, 237 OpenFX contract and 738,256 Metal parity assertions.
Native and universal binaries were rebuilt. The user chose to reinstall later,
so these changes have not yet been verified by restarting their Resolve session.

## 0.2.0 named references and matching

The reference/match feature adds a dedicated host-independent suite. Native
Release tests passed all four suites, including actual Metal GPU execution:
80,154 color/image assertions, 89 matching/library assertions, 404 OpenFX
contract assertions and 738,256 Metal parity assertions on Apple M4 Pro.
The three CPU/OpenFX/matching suites also ran under ASan + UBSan without diagnostics.
The macOS universal arm64/x86_64 bundle compiled and passed structure/signature
verification. The feature uses the existing shared CPU/Metal grading kernel;
there are no extra ordinary-render readbacks or library accesses.

The matching tests cover relative WB including warm/cool/green/magenta casts,
known exposure differences, WB/EV-only modes, primary conversions, preservation
of unrelated controls, outliers, unusable frames, bounded fits, UTF-8 metadata,
lossless/checksummed reference serialization, disk persistence, explicit replacement,
full/corrupt libraries and concurrent allocation from threads and separate processes.
The actual plugin C-suite tests save a graded reference on one effect instance,
match another instance, compare rendered pixels and alpha, exercise strength,
manual offsets and repeatability, reconstruct a project snapshot without library
files, and test selected-time capture/save/match fallbacks and missing references.
All filesystem tests use isolated temporary libraries, never the user's library.

Resolve UI refresh, actual project serialization of embedded reference strings,
Undo/Redo, and Metal capture in the running host still require the added manual
checklist. The user's existing installation was not replaced automatically.

## 0.3.0 photographic panel and selectable matching

Before these changes, the complete 0.2.0 implementation and registration fix were
committed as `bdd42eb`, as requested. Native Release 0.3.0 passed 5/5 suites with
real GPU access: 80,154 core, 89 reference/library, 191 photographic-control,
706 OpenFX contract and 750,544 Metal parity assertions. The four non-GPU suites
also passed ASan + UBSan; arm64 and universal arm64/x86_64 bundles built and passed
structure/dependency/export/signature checks.

New tests cover panel declaration order, absence of Advanced, secret technical
controls, disabled status/reference labels, exactly As Shot / Auto / Custom WB,
manual-slider transitions and As Shot reset, Kelvin roundtrips and monotonicity,
full Auto and conservative color correction, all 16 Match field combinations,
independent preservation of unchecked fields, actual contrast and combined color
fits, primary-button frame preparation for Auto/Save/Match/WB, and one-click viewer
WB without fetching a host image from the interact. Metal tests additionally run
Kelvin-driven adaptation at 2000, 2850, 6504 and 25000 K in all three encodings.

No live Resolve UI operation or installation was performed for this update.
The native layout, disabled-field behavior, viewer click routing, project undo/
serialization and the DWG/Intermediate working pipeline still need the current
manual acceptance checklist in the actual host.
