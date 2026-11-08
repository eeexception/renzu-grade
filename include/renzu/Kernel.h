// SPDX-License-Identifier: GPL-3.0-only
// This exact scalar implementation is compiled by Clang and the Metal compiler.
#pragma once
#ifdef __METAL_VERSION__
#include <metal_stdlib>
using namespace metal;
#define RG_INLINE inline
#define RG_CONST constant
#else
#include <algorithm>
#include <cmath>
#include <cstdint>
using std::exp;
using std::exp2;
using std::fabs;
using std::isfinite;
using std::log2;
using std::pow;
using std::sqrt;
#define RG_INLINE inline
#define RG_CONST const
#endif
namespace renzu {
struct V3 {
    float r, g, b;
};
struct KernelParams {
    float wb[9];
    float exposure, shadows, midtones, lights, highlights;
    float contrast, pivot, saturation, vibrance;
    int space; // 0 DWG/Intermediate; 1 Rec.709 gamma 2.4; 2 linear Rec.709
    int premultiplied;
    int identity;
};
RG_INLINE float clampf(float x, float a, float b) {
    return x < a ? a : (x > b ? b : x);
}
RG_INLINE float safe(float x) {
    return isfinite(x) ? clampf(x, -1.e12f, 1.e12f) : 0.f;
}
RG_INLINE float signedpow(float x, float p) {
    return (x < 0 ? -1.f : 1.f) * pow(fabs(x), p);
}
RG_INLINE float decode(float x, int space) {
    x = safe(x);
    if (space == 0)
        return x <= 0.02740668f ? x / 10.44426855f
                                : exp2(clampf(x / 0.07329248f - 7.f, -40.f, 40.f)) - 0.0075f;
    if (space == 1)
        return signedpow(x, 2.4f);
    return x;
}
RG_INLINE float encode(float x, int space) {
    x = safe(x);
    if (space == 0)
        return x <= 0.00262409f ? x * 10.44426855f : (log2(x + 0.0075f) + 7.f) * 0.07329248f;
    if (space == 1)
        return signedpow(x, 1.f / 2.4f);
    return x;
}
RG_INLINE V3 decode3(V3 c, int s) {
    return {decode(c.r, s), decode(c.g, s), decode(c.b, s)};
}
RG_INLINE float luma(V3 c, int space) {
    return space == 0 ? 0.27411851f * c.r + 0.87363190f * c.g - 0.14775041f * c.b
                      : 0.21263901f * c.r + 0.71516868f * c.g + 0.07219232f * c.b;
}
RG_INLINE V3 mul(V3 c, float f) {
    return {c.r * f, c.g * f, c.b * f};
}
RG_INLINE V3 matrix(V3 c, RG_CONST float *m) {
    return {m[0] * c.r + m[1] * c.g + m[2] * c.b, m[3] * c.r + m[4] * c.g + m[5] * c.b,
            m[6] * c.r + m[7] * c.g + m[8] * c.b};
}
RG_INLINE float tonalEV(float y, RG_CONST KernelParams &p) {
    float x = log2(clampf(fabs(y) / 0.18f, 1.e-8f, 1.e8f));
    float a = exp(-0.5f * pow((x + 5.f) / 1.8f, 2.f));
    float b = exp(-0.5f * pow((x + 1.f) / 1.8f, 2.f));
    float c = exp(-0.5f * pow((x - 2.f) / 1.8f, 2.f));
    float d = exp(-0.5f * pow((x - 5.5f) / 1.8f, 2.f));
    float sum = a + b + c + d;
    return sum > 1.e-12f ? (a * p.shadows + b * p.midtones + c * p.lights + d * p.highlights) / sum
                         : 0.f;
}
RG_INLINE V3 process(V3 c, float alpha, RG_CONST KernelParams &p) {
    c = {safe(c.r), safe(c.g), safe(c.b)};
    if (p.identity)
        return c;
    bool unpremult = p.premultiplied && isfinite(alpha) && fabs(alpha) > 1.e-8f;
    if (unpremult)
        c = mul(c, 1.f / alpha);
    c = decode3(c, p.space);
    c = matrix(c, p.wb);
    c = mul(c, exp2(p.exposure));
    float y = luma(c, p.space);
    if (p.shadows != 0 || p.midtones != 0 || p.lights != 0 || p.highlights != 0) {
        c = mul(c, exp2(tonalEV(y, p)));
        y = luma(c, p.space);
    }
    if (p.contrast != 1.f && fabs(y) > 1.e-12f) {
        float x = log2(fabs(y) / p.pivot);
        float z = x * p.contrast / (1.f + 0.08f * fabs(p.contrast - 1.f) * fabs(x));
        float target = p.pivot * exp2(clampf(z, -40.f, 40.f));
        c = mul(c, target / fabs(y));
        y = luma(c, p.space);
    }
    if (p.saturation != 1.f || p.vibrance != 0.f) {
        float dr = c.r - y, dg = c.g - y, db = c.b - y;
        float chroma = sqrt(dr * dr + dg * dg + db * db) / sqrt(y * y + 0.0004f);
        float weight = 1.f / (1.f + chroma * chroma * 2.f);
        // Broad smooth warm-hue protection; no face or skin detection.
        float skin = exp(-pow((dr - 0.16f) / 0.30f, 2.f) - pow((db + 0.12f) / 0.30f, 2.f));
        float sat = p.saturation * (1.f + p.vibrance * 0.01f * weight * (1.f - 0.35f * skin));
        c = {y + dr * sat, y + dg * sat, y + db * sat};
    }
    c = {encode(c.r, p.space), encode(c.g, p.space), encode(c.b, p.space)};
    if (unpremult)
        c = mul(c, alpha);
    return {safe(c.r), safe(c.g), safe(c.b)};
}
// ABI uses only scalar 32-bit fields; no SIMD padding differences.
struct ImageLayout {
    int x1, y1, x2, y2, rowBytes, depth, components;
};
struct Dispatch {
    ImageLayout src, dst;
    int x1, y1, x2, y2;
};
} // namespace renzu
