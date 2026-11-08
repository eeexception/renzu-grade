# Architecture and lifecycle

`src/ofx/Plugin.cpp` exports `OfxGetNumberOfPlugins` and `OfxGetPlugin` for the
stable ID `org.renzu.grade` (OpenFX plugin version 0.3). It implements the standard
C suites directly; small RAII wrappers release fetched images in the same action
and balance parameter edits even on failure. No proprietary host SDK is linked.

| Module | Responsibility |
| --- | --- |
| `include/renzu/Controls.h` | Visible manual parameter metadata and Settings mapping |
| `include/renzu/Engine.h` | Host-independent settings, images, statistics, analysis API |
| `include/renzu/Kernel.h` | Identical scalar per-pixel math and dispatch ABI compiled for CPU and Metal |
| `src/color/Engine.cpp` | Working-primary matrices, Bradford adaptation, bounded settings |
| `src/auto/Analysis.cpp` | Histograms, robust percentiles, Auto Tone, Auto WB, patch estimator |
| `src/match/Match.cpp` | Graded reference samples, common-primary WB/exposure fitting |
| `src/match/ReferenceStore.cpp` | Stable named library slots, checksummed atomic storage and project serialization |
| `src/cpu/Renderer.cpp` | Pixel conversion, signed strides, render windows, standalone threading |
| `src/ofx/Plugin.cpp` | Native registration, host parameters, one-shot actions, overlay and fallback capture |
| `src/metal/MetalBackend.mm` | Resource validation, compiled-library cache, host-queue submission and explicit capture |
| `shaders/renzu_grade.metal` | Buffer-format access and texture kernels around shared pixel math |

## Render contract

Supported contexts are filter and general. Source/Output negotiate RGBA or RGB
and byte, short, half or float. Multiple clip depths are not advertised. Textures
are RGBA only, and validate the exact corresponding Metal pixel format. No
OpenCL, CUDA or OpenGL rendering capability is advertised. The input encoding is
an explicit user choice; no reliance on unfinished host color-management suites.

The effect is fully render-thread safe. Settings are read per render; no grading
state accumulates across frames. Parameter handles are resolved at creation and
remain immutable. CPU work uses the host multithread suite, checking host abort
between rows. The standalone core uses bounded asynchronous row workers. Render
windows are clipped to destination bounds. Unavailable source pixels become
transparent black. CPU data pointers address the lower-left bounds pixel with
signed row stride. Metal buffer objects address the allocation start; a negative
stride includes a height-dependent physical offset in the shader. Texture
coordinates follow the OpenFX bottom-left convention, without an extra flip.
Pixel aspect ratio only matters for the viewer: canonical pen positions are
normalized using the source canonical region of definition, so PAR and render
scale cancel. Patch radius is measured in source pixel coordinates.

Tiles are not advertised, so explicit capture can examine the full fetched
source rather than assembling independent tiles. Arbitrary render windows remain
supported. Multi-resolution support concerns image bounds/scales, not different
input/output encodings. No field-neighbor or temporal requests are made.

## One-shot actions and persistence

All grading numeric parameters are persistent and non-animating. Buttons use
`kOfxParamTypePushButton`; user `kOfxActionInstanceChanged` gets its frame time
from `kOfxPropTime`. Source `clipGetImage` is fetched in that action and always
released there. Results are written with `paramSetValue`, not time-keyframed
setters, inside a `paramEditBegin`/`paramEditEnd` undo group. Re-entrant callbacks
caused by those writes are guarded with a recursive UI lock and editing flag.

Auto Tone/Strength write the six tonal controls. Auto WB and patch sampling
write only WB. Match writes only Temperature, Tint and Exposure; independent
mode selection can restrict it further. Reset changes the manual grade and strength state, leaving input
encoding and patch coordinates in place. Hidden baseline/target/applied-strength
parameters preserve parameter blending through project serialization. Ordinary
slider callbacks write no private data, allowing the host to restore their state
for undo/redo. Captured source pixels are per-instance temporary data, never
project data. Closing the effect/project discards them; visible results persist.

Saved references are different from temporary capture: the reference library is
shared across instances on disk, and a loaded snapshot is embedded in persistent
effect parameters. Reference and match state is read/written only during UI
actions; rendering reads neither the library nor the embedded reference. Stable
numbered slots allow option labels to refresh without reordering choice indices.
Atomic writes plus a filesystem save lock protect shared library allocation.
See matching.md for storage format, color management and lifecycle details.

If callback frame access fails, the visible action stores a time-specific
request and invalidates evaluation using a hidden revision parameter. Exactly
one matching render copies analysis pixels/statistics into an instance-owned
snapshot; render never changes host parameter values. Snapshot publication is
mutex protected and generation-checked against newer capture requests. Repeating the same button in a later user callback consumes this snapshot and updates sliders.
There is no pending automatic application when the playhead moves.

GPU capture uses a blit to a shared staging buffer, on the host queue, and waits
only for this explicitly requested capture. Ordinary grading does not download
pixels or wait. ARC manages plugin-owned resources; host queue/images are
borrowed and never released manually. Command buffers retain bound resources;
immutable pipeline states are cached by Metal device registry ID and library path.

## Failure paths and boundaries

Missing suites return MissingHostFeature. Malformed/unsupported images return an
image-format error, allocation failure returns memory error, and Metal setup
failure returns GPURenderFailed so the host may retry CPU. An asynchronous GPU
submission can only report submission-time failures; device loss/error recovery
belongs to the host. Explicit capture and standalone tests wait and inspect
command-buffer errors. Non-finite RGB is sanitized, while alpha remains untouched.
The plugin does not own or change upstream media or global Resolve state.

See the official [image clip contract](https://openfx.readthedocs.io/en/latest/Reference/ofxImageClip.html),
[parameter API](https://openfx.readthedocs.io/en/latest/Reference/ofxParameter.html),
[render contract](https://openfx.readthedocs.io/en/latest/Reference/ofxRendering.html),
and pinned `third_party/ofx/include/ofxGPURender.h`. The developer samples installed
with Resolve were inspected to confirm host-buffer and host-queue usage, but no
sample source was copied.

## Native photographic panel (0.3.0)

The root declaration and page order are Auto, WB, Exposure, Contrast, Match /
Reference, HDR, Vibrance, Saturation. Status/Selected use label-mode strings with
Enabled=0 so hosts cannot treat them as editable text. There is no Advanced group.
Legacy parameter IDs remain persistent/secret for grade compatibility; UI aliases
map photographic percentages and Kelvin into those engine values. Source-change
callbacks synchronize aliases for reopened instances without writing in render.
New instances retain the existing default DWG/Intermediate interpretation; old
instances retain their saved hidden encoding and premultiplication settings.

The public Auto action combines one-shot WB, tone and conservative chroma targets.
Match fields are independent booleans; baseline/target strength state now covers
WB, EV, Contrast, Vibrance and Saturation. Unselected parameter targets equal their
baselines. The previous mode API is retained internally for compatibility/tests.

If callback fetching fails, the same visible action arms a time-specific capture;
a later repeat of that action consumes it in InstanceChanged. No internal capture
buttons appear. Pick WB requests a bounded owned image grid (up to 1024 pixels per
dimension), then samples it during viewer PenDown. Interact performs no host image
fetch. The owned grid is released after selection. Parameter writes remain legal
UI/interact edits and ordinary rendering never applies pending results on its own.
