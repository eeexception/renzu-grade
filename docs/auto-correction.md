# One-shot automatic correction

Auto operations analyze source pixels of the button callback's `kOfxPropTime`,
not screenshots, output pixels, adjacent frames, or the latest arbitrary render.
All analysis decodes the selected encoding first and uses its working-primary Y.
A deterministic grid samples up to approximately 262,144 pixels, excluding
transparent and non-finite input. Input WB and tone sliders do not feed back into
the statistics, avoiding correction compounding.

## Histogram and tonal statistics

A 512-bin histogram covers [-20,+20] stops relative to .18, with endpoint
accumulation. Percentiles are computed by sorting actual nonnegative sampled Y
and linearly interpolating ranks. Statistics include .5%, 10%, median, 90%, and
99.5%, useful range in stops, shadows below .04, midtones .04–.5, highlights
above .5, nominal black (Y<=1e-6) and nominal white plateau (.995<=Y<=1.005).
Those clipping estimates describe linear reference white/black only; sensor or
source-transfer clipping cannot be inferred reliably from arbitrary managed RGB.
Negative Y contributes to black statistics; negative RGB is not clipped in rendering.

Fewer than 16 valid samples, black frames, and constant-luminance frames preserve
the baseline. Auto Tone computes a bounded absolute parameter target:

| Parameter | Formula before bounded strength blending |
| --- | --- |
| Exposure | .32*log2(.18/max(median,.002)), limited to [-1.25,1.25] initially |
| Shadows | .18*log2(.025/(p10*exposureGain)), limited to [-.45,.5]; zero if p10 too small |
| Midtones | .12*log2(.18/max(.002,median*gain)), limited to [-.35,.35] |
| Lights | .12*log2(.65/max(.01,p90*gain)), limited to [-.3,.3] |
| Highlights | .18*log2(1.5/max(.02,p99.5*gain)), limited to [-.8,.2] |
| Contrast | 1+.14*(.95-clamp(rangeEV/6,0,2)), limited to [.9,1.14] |

If over 70% of samples are shadows and the median is below .025, positive
exposure is limited to .4 EV and shadow lift to .2. If the median is above .5
and shadows below 5%, negative exposure is limited to -.45 EV. The robust high
percentile limits exposure when the resulting high value would exceed 4;
exposure's final range is [-2,1.25]. Isolated lights below the .5% population
cannot control this high percentile. This is a conservative starting grade,
not a promise to recover clipped detail or determine artistic intent.

Pivot, saturation, vibrance, temperature and tint are untouched. Strength blends
each target parameter with the saved pre-auto baseline, not output RGB. Baseline,
target, and previously applied strength are persistent hidden parameters. Manual
slider offsets are carried into both endpoints on a later strength change,
constrained to visible control ranges. Repeated unchanged Auto Tone keeps the
original baseline and recomputes an absolute target from input. A manually changed
grade starts a new baseline on the next invocation.

## Auto WB

Reject candidates with Y outside [.015,.85], minimum channel <=.003,
maximum channel >=.98, or `(max-min)/max > .42`. Weight accepted pixels by
`(1-chroma/.42)^2`. Combine 75% of the weighted gray-world correction with 25%
of the fourth-power Shades-of-Gray correction; both use the Bradford neutral
solution described in color-science.md.

Require at least 32 candidates and total candidate weight >=8. Confidence is
`min(1,3*candidateCoverage)*exp(-(abs(deltaTemp)+abs(deltaTint))/35)` comparing
the two estimators. Below .25 confidence, preserve existing WB and show a diagnostic.
Accepted automatic corrections are bounded to ±70 on both normalized controls;
manual/explicit patch corrections can reach ±100. Tonal and color sliders do not
change. Saturated stage lighting is usually rejected; a uniformly colored scene
that resembles a plausible cast cannot always be distinguished from illumination.

## Neutral patch

X/Y are normalized source coordinates with bottom-left origin. Radius is a
fraction of the shorter source pixel dimension, rounded to at least one pixel.
Samples need alpha >=.01, Y in [.01,.9], minimum channel >.001 and maximum
channel <.98. Four or more valid pixels are required. Component medians reject
small specular/noise outliers before the Bradford inverse is solved. These fixed
linear thresholds are reference-white-relative; choose another patch or temporarily
adjust upstream exposure if an HDR neutral reference lies outside them.

The current Pick WB action prepares an owned source grid; a viewer click samples
its neighborhood and writes WB immediately in the interact callback. No host
image is fetched during interaction, and the owned grid is released afterward.
Coordinates and radius are hidden compatibility values rather than UI controls.

## 0.3.0 panel behavior

The separate Auto Tone/Auto WB/Strength controls described above are retained as
internal mechanisms and compatibility parameters, not visible buttons. The panel
now has one **Auto** for all adjustments and WB **As Shot / Auto / Custom**.
As Shot means reset the additional plugin WB to defaults, not read RAW metadata.
Editing either visible WB slider selects Custom. The full Auto first estimates
WB, computes tonal targets from balanced linear input, then chooses conservative
saturation (.9..1.12) and vibrance (0..25) targets from robust chroma statistics.
Gray frames retain neutral color controls; very dark frames are rejected.

There is no Advanced or explicit capture/apply workflow in the current panel.
When immediate frame access fails, the same action requests a selected-time frame,
then asks to be repeated after the viewer updates. WB Auto switches to Custom
while preparing so choosing Auto again triggers a fresh user callback. Rendering
only saves owned pixels; applying the result remains a UI callback.
