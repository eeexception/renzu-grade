# Match / Reference

## Workflow

On the reference clip, choose a frame, enter Name and press **Save Reference**.
On the target, select an entry and press **Load Reference**, then select any
combination of **White Balance**, **Exposure**, **Contrast**, **Color (Vibrance +
Saturation)** and press **Match**. Amount blends actual parameters against the
pre-match baseline. Manual offsets survive changing Amount; repeated Match does
not accumulate the previous correction. Ordinary playback never re-analyzes.

Color intentionally fits vibrance and saturation together. Their effects overlap,
so a unique separate solution is not guaranteed from an arbitrary reference image.
Both resulting sliders remain editable. Unchecked fields and all four HDR controls
remain unchanged. No selection produces a diagnostic and preserves the grade.

A save captures the input frame and applies the current Renzu settings before
storing up to 4096 analysis pixels. It excludes downstream nodes and display
transforms. Use equivalent grading points and working representations on both
clips; the plugin cannot invert arbitrary LUTs/DRTs. New instances expect
DWG/Intermediate. Earlier saved instances retain their selected encoding.

## References and persistence

There are 16 fixed numbered slots. Latest creates an empty slot; explicitly
saving to a numbered slot replaces it. Latest is resolved when loaded and pinned
in that effect. Other saves do not silently change it; Load Reference refreshes
its snapshot. The Selected label shows the loaded name and is read-only.

On macOS, references live at:
`~/Library/Application Support/Renzu Grade/References/`.
Each file contains a versioned identifier, name, time, encoding and float RGB data,
with a checksum. Thread/process locks and atomic writes protect allocation.
Corrupt entries are isolated and explicit loads report errors. A loaded snapshot
is also embedded in persistent effect parameters (about 100 KB maximum), allowing
re-matching after project reopening without the shared library, subject to host
serialization support. Rendering reads only the grading parameters, never these files.

Library saves remain after grade Undo. Selection, Match, and Amount changes use
OpenFX undo groups. Replacing a slot does not change existing pinned references
or applied grades. Uninstall does not erase the library. Tests use isolated
`RENZU_REFERENCE_DIRECTORY` locations rather than the user's library.

## Automatic frame preparation

Some host paths cannot supply pixels while handling a button click. The plugin
then requests exactly that time, captures it during the next matching render,
and asks you to press **the same button again**. The second click applies from
owned pixels in a legal user callback. A different playhead time cannot satisfy
the request. There are no visible Capture/Apply or Advanced controls. A GPU
capture performs one explicit readback; ordinary grading performs none.

## Fitting

All analysis compares decoded pixels in common linear Rec.709 primaries through
XYZ. WB uses confidence-gated relative gray-world/Shades-of-Gray estimates, so a
warm reference remains warm. Exposure fits a weighted log residual of the 10th,
50th and 90th luminance percentiles. Contrast fits the robust tonal spread;
exposure and contrast are refined together when both are selected. Color fits
25th/50th/90th chroma statistics with bounded saturation/vibrance search and a
small baseline preference to resolve ambiguous solutions.

These are global statistical fits, not subject recognition or a complete look
transfer. Different compositions, colored lighting, surfaces, clipping or weak
variation can prevent a reliable match. Status identifies unreliable WB and
approximate/bounded fits. Unselected parameters are not adjusted to compensate
for a missing selected field.

See manual-test-plan.md for Resolve-specific acceptance checks.
