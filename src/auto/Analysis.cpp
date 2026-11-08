// SPDX-License-Identifier: GPL-3.0-only
#include "renzu/Engine.h"
#include <numeric>
namespace renzu {
Statistics analyze(const std::vector<V3> &pixels, int space) {
    Statistics s;
    std::vector<double> y;
    y.reserve(pixels.size());
    for (auto c : pixels) {
        double v = luma(c, space);
        if (!std::isfinite(v))
            continue;
        y.push_back(std::max(v, 0.));
        double stops = std::log2(std::max(v, 1.e-8) / .18);
        int bin = std::clamp(int((stops + 20.) * 512. / 40.), 0, 511);
        ++s.histogram[bin];
        s.shadows += v < .04;
        s.midtones += v >= .04 && v < .5;
        s.highlights += v >= .5;
        s.blackClipping += v <= 1.e-6;
        s.whiteClipping += v >= .995 && v <= 1.005;
    }
    s.samples = y.size();
    if (y.empty())
        return s;
    std::sort(y.begin(), y.end());
    auto percentile = [&](double p) {
        double index = (y.size() - 1) * p;
        auto i = std::size_t(index);
        return y[i] + (y[std::min(i + 1, y.size() - 1)] - y[i]) * (index - i);
    };
    s.low = percentile(.005);
    s.high = percentile(.995);
    s.median = percentile(.5);
    s.p10 = percentile(.1);
    s.p90 = percentile(.9);
    s.rangeEV = std::log2(std::max(s.high, 1.e-8) / std::max(s.low, 1.e-8));
    for (double *p : {&s.shadows, &s.midtones, &s.highlights, &s.blackClipping, &s.whiteClipping})
        *p /= s.samples;
    return s;
}
Settings autoTone(const Statistics &s, const Settings &baseline, double strength) {
    Settings target = baseline; // WB and color parameters are deliberately preserved.
    if (s.samples < 16 || s.high < 1.e-6 || s.high - s.low < 1.e-7)
        return target;
    // Absolute corrections from raw scene-linear statistics, never compounded.
    double bias = std::clamp(std::log2(.18 / std::max(s.median, .002)) * .32, -1.25, 1.25);
    if (s.shadows > .70 && s.median < .025)
        bias = std::min(bias, .40); // preserve low key
    if (s.median > .5 && s.shadows < .05)
        bias = std::max(bias, -.45); // preserve high key
    if (s.high * std::exp2(bias) > 4.)
        bias = std::min(bias, std::log2(4. / s.high));
    target.exposure = std::clamp(bias, -2., 1.25);
    double gain = std::exp2(target.exposure);
    target.shadows =
        s.p10 > 1.e-5 ? std::clamp(std::log2(.025 / (s.p10 * gain)) * .18, -.45, .5) : 0;
    if (s.shadows > .7)
        target.shadows = std::min(target.shadows, .20);
    target.midtones = std::clamp(std::log2(.18 / std::max(.002, s.median * gain)) * .12, -.35, .35);
    target.lights = std::clamp(std::log2(.65 / std::max(.01, s.p90 * gain)) * .12, -.3, .3);
    target.highlights = std::clamp(std::log2(1.5 / std::max(.02, s.high * gain)) * .18, -.8, .2);
    target.contrast = std::clamp(1. + (.95 - std::clamp(s.rangeEV / 6., 0., 2.)) * .14, .90, 1.14);
    double a = std::clamp(std::isfinite(strength) ? strength : 0., 0., 1.);
    auto blend = [&](double b, double t) { return b + (t - b) * a; };
    target.exposure = blend(baseline.exposure, target.exposure);
    target.shadows = blend(baseline.shadows, target.shadows);
    target.midtones = blend(baseline.midtones, target.midtones);
    target.lights = blend(baseline.lights, target.lights);
    target.highlights = blend(baseline.highlights, target.highlights);
    target.contrast = blend(baseline.contrast, target.contrast);
    return target;
}
WBResult autoWB(const std::vector<V3> &pixels, int space) {
    WBResult out;
    std::vector<V3> candidates;
    V3 mean{};
    double sum = 0, grayR = 0, grayG = 0, grayB = 0;
    for (auto c : pixels) {
        double y = luma(c, space);
        float mx = std::max({c.r, c.g, c.b}), mn = std::min({c.r, c.g, c.b});
        if (y < .015 || y > .85 || mn <= .003 || mx >= .98)
            continue;
        float chroma = (mx - mn) / std::max(mx, .001f);
        if (chroma > .42f)
            continue;
        double w = std::pow(1. - chroma / .42, 2);
        mean.r += float(c.r * w);
        mean.g += float(c.g * w);
        mean.b += float(c.b * w);
        sum += w;
        candidates.push_back(c);
        grayR += std::pow(c.r, 4);
        grayG += std::pow(c.g, 4);
        grayB += std::pow(c.b, 4);
    }
    out.candidates = candidates.size();
    if (candidates.size() < 32 || sum < 8) {
        out.status = "Auto WB: insufficient reliable neutrals; existing WB preserved";
        return out;
    }
    mean = mul(mean, float(1 / sum));
    V3 shades{float(std::pow(grayR / candidates.size(), .25)),
              float(std::pow(grayG / candidates.size(), .25)),
              float(std::pow(grayB / candidates.size(), .25))};
    auto gray = neutralWB(mean, space), bright = neutralWB(shades, space);
    double agreement = std::exp(
        -(std::abs(gray.temperature - bright.temperature) + std::abs(gray.tint - bright.tint)) /
        35.);
    double coverage = double(candidates.size()) / std::max(std::size_t(1), pixels.size());
    out.confidence = std::clamp(coverage * 3., 0., 1.) * agreement;
    if (out.confidence < .25) {
        out.status = "Auto WB: low confidence / mixed light; existing WB preserved";
        return out;
    }
    out.temperature = std::clamp(.75 * gray.temperature + .25 * bright.temperature, -70., 70.);
    out.tint = std::clamp(.75 * gray.tint + .25 * bright.tint, -70., 70.);
    out.reliable = true;
    out.status = "Auto WB applied; confidence " + std::to_string(int(out.confidence * 100)) + "%";
    return out;
}
WBResult sampleNeutral(const Image &s, int space, bool premult, double nx, double ny,
                       double radius) {
    WBResult result;
    if (!valid(s)) {
        result.status = "No CPU sample image";
        return result;
    }
    int w = s.bounds.x2 - s.bounds.x1, h = s.bounds.y2 - s.bounds.y1;
    int cx = s.bounds.x1 + int(std::clamp(nx, 0., 1.) * (w - 1)),
        cy = s.bounds.y1 + int(std::clamp(ny, 0., 1.) * (h - 1));
    int r = std::max(1, int(std::clamp(radius, .0001, .25) * std::min(w, h)));
    std::vector<float> reds, greens, blues;
    for (int y = std::max(s.bounds.y1, cy - r); y < std::min(s.bounds.y2, cy + r + 1); ++y)
        for (int x = std::max(s.bounds.x1, cx - r); x < std::min(s.bounds.x2, cx + r + 1); ++x) {
            auto p = readPixel(s, x, y);
            if (p[3] < .01)
                continue;
            V3 c{p[0], p[1], p[2]};
            if (premult)
                c = mul(c, 1 / p[3]);
            c = decode3(c, space);
            float lum = luma(c, space);
            if (lum < .01 || lum > .9 || std::min({c.r, c.g, c.b}) <= .001 ||
                std::max({c.r, c.g, c.b}) >= .98)
                continue;
            reds.push_back(c.r);
            greens.push_back(c.g);
            blues.push_back(c.b);
        }
    if (reds.size() < 4) {
        result.status = "Neutral patch: too dark, clipped, or too small";
        return result;
    }
    auto median = [](std::vector<float> &v) {
        std::sort(v.begin(), v.end());
        return v[v.size() / 2];
    };
    result = neutralWB({median(reds), median(greens), median(blues)}, space);
    result.candidates = reds.size();
    return result;
}
} // namespace renzu
