# Changelog

## Unreleased

- Add Windows x64 (MSVC, static CRT) and Linux x86_64 CPU-only OFX bundles,
  distribution archives, build scripts and installation instructions.
- Add Windows/Linux GitHub Actions builds and tests, archive/load verification,
  corresponding-source rebuilds and Linux ASan/UBSan coverage. Build Linux
  distributions against glibc 2.28 with a private static C++ runtime.
- Port reference library locks, atomic replacement, Unicode paths and contract
  test loading to Windows; respect XDG_DATA_HOME on Linux.
- Replace compiler-specific CPU half-float conversion with portable IEEE binary16
  conversion and exhaustive encoding/rounding tests.
- License project code under GNU GPL version 3 only; retain the BSD-3-Clause
  license and notices for the official OpenFX headers.
- Include corresponding source and build scripts in distribution archives.

## 0.3.0

- Reorder the native panel: Auto, WB, Exposure, Contrast, Match / Reference,
  HDR, Vibrance, Saturation. Remove Advanced and technical capture controls.
- Make Status and Selected reference read-only, with concise status text.
- Add full-frame Auto including WB, exposure, contrast, HDR, vibrance and saturation.
- Add As Shot / Auto / Custom WB, illuminant-based Kelvin temperature, and direct
  viewer-point WB sampling without a separate Sample button.
- Rename/order HDR zones as Highlights, Shadows, Whites, Blacks on photographic
  -100..100 scales. Contrast and Saturation also use neutral-zero scales.
- Add independent Match checkboxes for WB, exposure, contrast, and a combined
  Vibrance/Saturation fit, with all field combinations tested.
- Reuse the main action on deferred frame access rather than exposing capture
  internals. Preserve legacy parameter IDs and stored grades.

## 0.2.0

- Add named frame references, shared across effect instances and application
  sessions without exporting an image. Save includes the current Renzu grade.
- Add one-shot reference Match for WB/exposure, three match modes, persistent
  parameter-based strength and manual refinement without continuous adaptation.
- Add a 16-slot reference library with atomic files, checksums and save locks;
  selected snapshots are embedded in effect parameters for portable projects.
- Add explicit save/match captured-frame fallbacks and matching/storage/host tests.

- Fix discovery in Resolve when the host rejects optional OpenFX description
  metadata: register supported contexts and depths instead of aborting Describe.
- Handle unavailable group-open and Metal capability properties as optional while retaining real
  property errors; add host-discovery regression tests.

## 0.1.0

- Native OpenFX filter/general contexts, persistent photographic controls and undo groups.
- Bradford temperature/tint, scene-linear exposure and four tonal zones, contrast,
  pivot, luminance-preserving saturation and adaptive vibrance.
- One-shot histogram/percentile Auto Tone and confidence-gated Auto WB.
- Neutral viewer-point selection and normalized-coordinate patch sampling.
- Explicit selected-frame capture/apply fallback with optional GPU readback.
- Shared CPU/Metal math; multithreaded CPU, Metal buffers and textures.
- Standalone tests, macOS scripts, bundle verification, archives, and CI.

Initial release requires manual verification in the intended Resolve host.
