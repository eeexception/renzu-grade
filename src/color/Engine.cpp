// SPDX-License-Identifier: GPL-3.0-only
#include "renzu/Engine.h"
namespace renzu {
using M = std::array<double, 9>;
static M multiply(const M &a, const M &b) {
    M r{};
    for (int y = 0; y < 3; ++y)
        for (int x = 0; x < 3; ++x)
            for (int k = 0; k < 3; ++k)
                r[y * 3 + x] += a[y * 3 + k] * b[k * 3 + x];
    return r;
}
static M inverse(const M &m) {
    double d = m[0] * (m[4] * m[8] - m[5] * m[7]) - m[1] * (m[3] * m[8] - m[5] * m[6]) +
               m[2] * (m[3] * m[7] - m[4] * m[6]);
    return {(m[4] * m[8] - m[5] * m[7]) / d, (m[2] * m[7] - m[1] * m[8]) / d,
            (m[1] * m[5] - m[2] * m[4]) / d, (m[5] * m[6] - m[3] * m[8]) / d,
            (m[0] * m[8] - m[2] * m[6]) / d, (m[2] * m[3] - m[0] * m[5]) / d,
            (m[3] * m[7] - m[4] * m[6]) / d, (m[1] * m[6] - m[0] * m[7]) / d,
            (m[0] * m[4] - m[1] * m[3]) / d};
}
static const M bradford = {.8951, .2664, -.1614, -.7502, 1.7135, .0367, .0389, -.0685, 1.0296};
M rgbToXYZ(int s) {
    if (s == 0)
        return {.70062239,  .14877482,  .10105872,  .27411851, .87363190,
                -.14775041, -.09896291, -.13789533, 1.32591599};
    return {.41239080, .35758434, .18048079, .21263901, .71516868,
            .07219232, .01933082, .11919478, .95053215};
}
V3 convertLinear(V3 rgb, int fromSpace, int toSpace) {
    auto m = multiply(inverse(rgbToXYZ(toSpace)), rgbToXYZ(fromSpace));
    return {float(m[0] * rgb.r + m[1] * rgb.g + m[2] * rgb.b),
            float(m[3] * rgb.r + m[4] * rgb.g + m[5] * rgb.b),
            float(m[6] * rgb.r + m[7] * rgb.g + m[8] * rgb.b)};
}
V3 toLMS(V3 c, int s) {
    M m = multiply(bradford, rgbToXYZ(s));
    return {float(m[0] * c.r + m[1] * c.g + m[2] * c.b),
            float(m[3] * c.r + m[4] * c.g + m[5] * c.b),
            float(m[6] * c.r + m[7] * c.g + m[8] * c.b)};
}
KernelParams prepare(const Settings &s) {
    KernelParams p{};
    p.space = std::clamp(s.space, 0, 2);
    p.premultiplied = s.premultiplied;
    auto bounded = [](double x, double d, double a, double b) {
        return float(std::isfinite(x) ? std::clamp(x, a, b) : d);
    };
    p.exposure = bounded(s.exposure, 0, -5, 5);
    p.shadows = bounded(s.shadows, 0, -3, 3);
    p.midtones = bounded(s.midtones, 0, -3, 3);
    p.lights = bounded(s.lights, 0, -3, 3);
    p.highlights = bounded(s.highlights, 0, -3, 3);
    p.contrast = bounded(s.contrast, 1, 0, 2);
    p.pivot = bounded(s.pivot, .18, .001, 4);
    p.saturation = bounded(s.saturation, 1, 0, 2);
    p.vibrance = bounded(s.vibrance, 0, -100, 100);
    double t = bounded(s.temperature, 0, -600, 200), v = bounded(s.tint, 0, -500, 500);
    M m = {1, 0, 0, 0, 1, 0, 0, 0, 1};
    if (t != 0 || v != 0) {
        M rgb = rgbToXYZ(p.space), a = multiply(bradford, rgb);
        M gains = {std::exp(.0025 * t + .001 * v), 0, 0, 0, std::exp(-.002 * v), 0, 0, 0,
                   std::exp(-.0025 * t + .001 * v)};
        m = multiply(inverse(a), multiply(gains, a));
        // Normalize the luminance of a D65 neutral to preserve reference exposure.
        double yr = rgb[3] * (m[0] + m[1] + m[2]) + rgb[4] * (m[3] + m[4] + m[5]) +
                    rgb[5] * (m[6] + m[7] + m[8]);
        for (double &z : m)
            z /= yr;
    }
    for (int i = 0; i < 9; ++i)
        p.wb[i] = float(m[i]);
    p.identity = t == 0 && v == 0 && p.exposure == 0 && p.shadows == 0 && p.midtones == 0 &&
                 p.lights == 0 && p.highlights == 0 && p.contrast == 1 && p.saturation == 1 &&
                 p.vibrance == 0;
    return p;
}
WBResult neutralWB(V3 c, int s) {
    WBResult r;
    V3 a = toLMS(c, s), n = toLMS({1, 1, 1}, s);
    if (a.r <= 0 || a.g <= 0 || a.b <= 0) {
        r.status = "Invalid neutral chromaticity";
        return r;
    }
    double u = std::log((n.r / n.g) / (a.r / a.g)), v = std::log((n.b / n.g) / (a.b / a.g));
    r.temperature = std::clamp((u - v) / .005, -100., 100.);
    r.tint = std::clamp((u + v) / .006, -100., 100.);
    r.reliable = true;
    r.confidence = 1;
    r.status = "Neutral patch sampled";
    return r;
}
} // namespace renzu
