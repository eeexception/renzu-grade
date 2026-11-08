# Color science

All channels are floats in the shared pixel kernel. Pipeline order is optional
unpremultiplication, transfer decoding, Bradford WB, linear exposure, luminance
zones, luminance contrast/pivot, saturation/vibrance, transfer encoding, optional
repremultiplication. Alpha is copied separately. Default settings bypass all color
math for exact finite identity within the storage type's range and safety bounds.

## Encodings and primaries

For DaVinci Intermediate the published constants are A=0.0075, B=7,
C=0.07329248, M=10.44426855, linear cut=0.00262409,
encoded cut=0.02740668:

```
encode(L) = L*M                         when L <= linear cut
            (log2(L+A)+B)*C             otherwise

decode(V) = V/M                         when V <= encoded cut
            2^(V/C-B)-A                 otherwise
```

Negative values use the linear branch. Constants and the DWG RGB-to-XYZ matrix
come from Blackmagic Design's
[DaVinci Wide Gamut / Intermediate specification](https://documents.blackmagicdesign.com/InformationNotes/DaVinci_Resolve_17_Wide_Gamut_Intermediate.pdf).
The row-major DWG matrix is:

```
 0.70062239   0.14877482   0.10105872
 0.27411851   0.87363190  -0.14775041
-0.09896291  -0.13789533   1.32591599
```

For Rec.709 gamma 2.4, decoding is `sign(V)*abs(V)^2.4`; encoding uses
power `1/2.4`. The sign extension preserves floating-point negatives. It is a
pure gamma 2.4 encoding, not the piecewise BT.709 camera OETF or BT.1886 with a
nonzero display black. Scene Linear is the identity transfer with Rec.709
primaries. Both Rec.709 modes use this D65 RGB-to-XYZ matrix derived from the
BT.709 chromaticities:

```
0.41239080  0.35758434  0.18048079
0.21263901  0.71516868  0.07219232
0.01933082  0.11919478  0.95053215
```

See [ITU-R BT.709](https://www.itu.int/rec/R-REC-BT.709) for the chromaticities.
The Y row of the selected matrix defines luminance throughout the kernel.
DWG has a negative blue luminance coefficient; wide-gamut values are not clipped
to a narrow display gamut. Working RGB primaries remain unchanged by the engine;
only WB temporarily transforms into XYZ/LMS. Internal operations never treat
Intermediate code values as linear exposure or display luminance.

## Temperature and tint

The Bradford XYZ-to-LMS matrix is:

```
 0.8951   0.2664  -0.1614
-0.7502   1.7135   0.0367
 0.0389  -0.0685   1.0296
```

With internal normalized temperature T and tint I, LMS gains are
`exp(.0025*T+.001*I)`, `exp(-.002*I)`, `exp(-.0025*T+.001*I)`.
Positive T warms and positive I shifts toward magenta. This is a defined
chromatic-adaptation control space, not a camera profile. These are hidden engine
coordinates; the visible Kelvin control introduced in 0.3.0 maps an illuminant
locus into them, as described below.

For matrix A=Bradford*RGBtoXYZ the transform is
`inverse(A)*diag(gains)*A`, divided by the resulting D65 neutral's luminance.
This preserves reference neutral exposure; it cannot preserve the luminance of
every arbitrary color under adaptation. The Bradford matrix and von Kries model
are documented, for example, in the [ICC chromatic-adaptation discussion](https://www.color.org/chadtag.xalter).

For a sampled neutral, compute LMS chromaticity ratios relative to a D65 neutral.
Let u=ln((Ln/Mn)/(Ls/Ms)) and v=ln((Sn/Mn)/(Ss/Ms)); solve
`T=(u-v)/.005` and `I=(u+v)/.006`. Luminance normalization is a common scalar and
does not affect the chromaticity solution. Tests check forward/inverse neutrality
in all working spaces. Corrections outside the control range are bounded.

## Exposure and tonal regions

Exposure multiplies decoded RGB by `2^EV`. The four zone controls apply a second
common scalar `2^(sum(weight_i*EV_i))`, preserving RGB ratios. For
`x=log2(abs(Y)/.18)`, unnormalized weights are Gaussians with sigma 1.8 stops and
centers [-5,-1,2,5.5] stops. Normalize them to sum to one. Smooth overlap avoids
hard boundaries or spatial halos. At underflow extremes the weighted EV is zero.
Weights are evaluated after exposure, so exposure predictably shifts zone placement.
The zones are EV-equivalent local luminance responses, not proprietary Adobe controls.

## Contrast and color

For nonzero luminance, define `x=log2(abs(Y)/pivot)` and
`z=x*contrast/(1+.08*abs(contrast-1)*abs(x))`.
The new magnitude is `pivot*2^z`, with original luminance sign and RGB ratios
preserved. This supplies compressing tails for contrast away from one. The pivot
is invariant, contrast 1 bypasses the transform, and exact black stays black.
Contrast zero maps nonzero luminance magnitudes to the pivot. This curve does not
clip to display black/white and is not a substitute for the project's display transform.

Saturation scales `RGB-Y` about the selected luminance. Vibrance uses
`chroma=length(RGB-Y)/sqrt(Y^2+.0004)` and weight `1/(1+2*chroma^2)`.
The vibrance multiplier is `1+(vibrance/100)*weight*(1-.35*warmHueWeight)`.
The smooth warm-hue weight uses red/blue differences around a broad warm region
(see the shared kernel). It is an artistic heuristic without face detection.
Saturation and vibrance preserve working-space Y to floating-point tolerance.

## Numerical limits

Negative/super-white floats survive the normal pipeline. Integer writes are
rounded and constrained to their storage range. Half writes constrain RGB to
±65504; float RGB safety bounds are ±1e12. Non-finite RGB becomes zero. Exponents
are bounded to ±40; zone-weight inputs are bounded to avoid underflow/overflow.
WB, exposure, tone, and color controls are bounded independently. Finite alpha
is copied exactly for matching formats; raw alpha bytes are preserved for buffer
paths, including non-finite alpha. Unpremultiplication is skipped when alpha is
near zero. Texture read/write alpha follows the texture format's precision.

CPU and Metal compile the same scalar kernel, with fast-math disabled for Metal.
Different transcendental instructions and fused arithmetic can differ slightly.
The extended-range randomized buffer tests allow absolute + relative errors:
byte 1.01/255 + 2e-4*abs(CPU), short 8.01/65535 + 2e-4*abs(CPU),
half .015 + 1.1e-3*abs(CPU), float .003 + 2e-4*abs(CPU). The half relative term
allows one quantization step at large values. The short stress tolerance covers
gamma encoding of tiny residuals after channel cancellation in extreme colors;
nominal texture-format checks use the tighter 2.01/65535 absolute term. A separate float texture comparison
uses 1e-4 + 2e-4*abs(CPU). Exact identity, alpha and nominal color behavior also
have independent CPU assertions. These tolerances describe the tests, not a
claim that all possible inputs are bit-identical.

## Photographic controls in 0.3.0

The visible controls are projections of the persistent processing parameters.
Contrast and Saturation use `1 + amount/100`. HDR Highlights/Shadows/Whites/Blacks
use ±100 = ±3 EV-equivalent and map, respectively, to the existing bright-mid,
shadow-mid, brightest and darkest Gaussian zones. The old numeric IDs are hidden,
so existing project values and rendered grades remain valid. Exposure remains EV.

Temperature now uses correlated illuminant temperature in 2000..25000 K. The
Planckian-locus xy approximation attributed to Kang et al. is documented in the
[Colour temperature reference implementation](https://www.colour-science.org/api/0.3.3/html/_modules/colour/temperature/cct.html).
The mathematical coefficients are implemented independently in Photographic.cpp.
White XYZ is transformed to Bradford LMS; log gain ratios relative to the 6504 K
reference yield the existing two adaptation coordinates. Additional Tint offsets
that second coordinate. The 6504 K/Tint 0 default is exactly neutral. A monotonic
inverse locates Kelvin when Auto, Match or the picker updates adaptation values.
The internal adaptation bounds are expanded to cover the full Kelvin UI range.
This is an additional D65-relative illuminant correction to already decoded RGB,
not recovered camera RAW metadata or Adobe's camera/profile calibration.
