# Resolve host acceptance checklist (0.3.0)

Record Resolve edition/build, operating system/version, GPU, plugin architecture,
CPU/Metal rendering backend and project color
management. Use a disposable project. Standalone tests do not certify the host.

## Installation and layout

- [ ] Quit Resolve, build/test, install, restart. Find Renzu Grade in Effects / Color Grading.
- [ ] macOS: verify the bundle is arm64-only and test on Apple Silicon with macOS 13+.
- [ ] Windows x64: install the whole bundle under `C:\Program Files\Common Files\OFX\Plugins\`;
      confirm loading without a separately installed Visual C++ runtime.
- [ ] Linux x86_64: install under `/usr/OFX/Plugins/`; check `ldd` for missing libraries
      and record the distribution/glibc version.
- [ ] At defaults, node bypass and effect output agree.
- [ ] Order: Auto -> WB -> Exposure -> Contrast -> Match / Reference -> HDR -> Vibrance -> Saturation.
- [ ] WB dropdown contains only As Shot / Auto / Custom.
- [ ] Status and Selected reference cannot be edited.
- [ ] HDR order/names: Highlights, Shadows, Whites, Blacks. Values range -100..100.
- [ ] No Advanced, coordinate/radius, separate Auto Tone/Auto WB, or Capture/Apply controls appear.
- [ ] Numeric input, slider dragging and per-control reset update interactively.

## Color and Auto

- [ ] For a new instance use DWG/Intermediate before the output transform; use surrounding CSTs when necessary.
- [ ] An older project retains its saved encoding and grade after reopening.
- [ ] Temp defaults to 6504 K, Tint to 0; warmer/cooler values and tint directions behave smoothly.
- [ ] Editing either WB slider selects Custom. As Shot resets only WB to defaults.
- [ ] WB Auto analyzes only this frame; low confidence preserves WB with a diagnostic.
- [ ] Auto calculates WB, exposure, contrast, four HDR zones, vibrance and saturation in one operation.
- [ ] Move the playhead: parameters remain fixed. Repeated Auto on the same input does not compound.
- [ ] Try under/over-exposed, low/high-key, low/high contrast, muted color, grayscale and saturated stage light.
- [ ] Exposure ±1 EV gives expected scene-linear gain. Test smooth tonal ramps with each HDR slider separately.
- [ ] Contrast and saturation zero are neutral; -100 saturation produces gray. Vibrance acts preferentially on weak chroma.
- [ ] Inspect shadows/highlights, negatives and super-whites for clipping, banding or hue jumps.

## WB picker

- [ ] Enable the host OpenFX overlay; press Pick WB, then click a neutral card.
- [ ] WB applies immediately from a small neighborhood; no Sample button or coordinates are needed.
- [ ] A noisy patch averages sensibly; dark/clipped regions show a diagnostic and preserve WB.
- [ ] Move the playhead while armed: an old frame is not sampled at a new time.
- [ ] Repeat at viewer zoom/proxy scales, frame edges and non-square PAR if used.

## Match / Reference

- [ ] Save a named graded frame on A, load it on B, then Match.
- [ ] Exercise WB, Exposure, Contrast and Color checkboxes individually and in combinations.
- [ ] Color changes the Vibrance/Saturation pair; unchecked fields and HDR retain their values.
- [ ] No fields selected: a useful diagnostic, no grade change.
- [ ] Check Amount 0/50/100, manual offsets, repeatability and a new target frame.
- [ ] Save multiple entries with Latest selected; a numbered slot is replaced only explicitly.
- [ ] Load/refresh names on another instance. Pinned references stay fixed until loaded again.
- [ ] Save/reopen and move a project without its library: names, values, snapshot and Amount persist.
- [ ] Undo/Redo Match, Amount, WB reset and Auto as coherent operations. Shared library saves remain available.
- [ ] Try missing/corrupt/full slots, black frames, flat images and colored light; failure preserves the appropriate fields.

## Frame access, rendering and recovery

- [ ] If a button reports Preparing frame, wait for the viewer and press that same button again.
- [ ] For WB Auto's deferred path, select Auto again after the viewer updates.
- [ ] A different frame does not satisfy a pending operation; capture alone never changes sliders.
- [ ] Compare CPU-only and Metal builds, quitting Resolve between replacements.
- [ ] Windows/Linux: confirm CPU rendering, Auto, WB picker and Match in the real host.
      CPU-only here refers to the plugin; Resolve still needs its own supported GPU.
- [ ] Verify alpha, signed/nonzero bounds and representative RGB/RGBA media where the host supports them.
- [ ] Test 1080p, UHD/4K and 6K playback, scrubbing, cache/proxy modes, export and cancellation.
- [ ] Test several instances and a long export; inspect memory growth, crashes and GPU errors.
- [ ] Install over a previous bundle; confirm backup. Uninstall/reinstall without changing unrelated effects.
