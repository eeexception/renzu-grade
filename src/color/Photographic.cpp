// SPDX-License-Identifier: GPL-3.0-only
#include "renzu/Photographic.h"
namespace renzu {
static V3 illuminantLMS(double t) {
    double x = t <= 4000
                   ? -.2661239e9 / (t * t * t) - .234358e6 / (t * t) + .8776956e3 / t + .179910
                   : -3.0258469e9 / (t * t * t) + 2.1070379e6 / (t * t) + .2226347e3 / t + .240390;
    double y = t <= 2222 ? -1.1063814 * x * x * x - 1.34811020 * x * x + 2.18555832 * x - .20219683
               : t <= 4000 ? -.9549476 * x * x * x - 1.37418593 * x * x + 2.09137015 * x - .16748867
                           : 3.081758 * x * x * x - 5.8733867 * x * x + 3.75112997 * x - .37001483;
    double X = x / y, Z = (1 - x - y) / y;
    return {float(.8951 * X + .2664 - .1614 * Z), float(-.7502 * X + 1.7135 + .0367 * Z),
            float(.0389 * X - .0685 + 1.0296 * Z)};
}
std::array<double, 2> whiteBalanceFromKelvin(double kelvin, double tint) {
    auto s = illuminantLMS(std::clamp(std::isfinite(kelvin) ? kelvin : 6504., 2000., 25000.));
    auto n = illuminantLMS(6504);
    double u = std::log((n.r / n.g) / (s.r / s.g)), v = std::log((n.b / n.g) / (s.b / s.g));
    return {(u - v) / .005, (u + v) / .006 + (std::isfinite(tint) ? tint : 0.)};
}
WhiteBalanceControls whiteBalanceToKelvin(double temperature, double tint) {
    double lo = 2000, hi = 25000;
    for (int j = 0; j < 40; ++j) {
        double mid = (lo + hi) * .5;
        if (whiteBalanceFromKelvin(mid, 0)[0] < temperature)
            lo = mid;
        else
            hi = mid;
    }
    double k = (lo + hi) * .5;
    if (std::abs(temperature) < 1.e-8)
        k = 6504;
    return {k, tint - whiteBalanceFromKelvin(k, 0)[1]};
}
ColorStatistics analyzeColor(const std::vector<V3> &pixels, int space) {
    std::vector<double> values;
    values.reserve(pixels.size());
    for (auto rgb : pixels) {
        auto c = convertLinear(rgb, space, 2);
        float y = luma(c, 2);
        if (!std::isfinite(y) || y < .003 || y > 8)
            continue;
        double dr = c.r - y, dg = c.g - y, db = c.b - y;
        double chroma = std::sqrt(dr * dr + dg * dg + db * db) / std::sqrt(double(y) * y + .0004);
        if (std::isfinite(chroma) && chroma < 8)
            values.push_back(chroma);
    }
    ColorStatistics stats;
    stats.samples = values.size();
    if (values.empty())
        return stats;
    std::sort(values.begin(), values.end());
    auto p = [&](double q) { return values[std::size_t(q * (values.size() - 1))]; };
    stats.low = p(.25);
    stats.median = p(.5);
    stats.high = p(.9);
    return stats;
}
AutoResult autoGrade(const std::vector<V3> &raw, const Settings &baseline) {
    AutoResult result;
    result.settings = baseline;
    if (raw.size() < 32) {
        result.status = "Frame has too few usable samples";
        return result;
    }
    Settings neutral;
    neutral.space = baseline.space;
    neutral.premultiplied = baseline.premultiplied;
    auto wb = autoWB(raw, baseline.space);
    neutral.temperature = wb.reliable ? wb.temperature : baseline.temperature;
    neutral.tint = wb.reliable ? wb.tint : baseline.tint;
    result.whiteBalanceApplied = wb.reliable;
    auto balanced = gradeSamples(raw, neutral);
    auto stats = analyze(balanced, baseline.space);
    if (stats.samples < 32 || stats.median < 1.e-6) {
        result.status = "Frame is too dark for Auto";
        return result;
    }
    result.settings = autoTone(stats, neutral, 1);
    auto color = analyzeColor(gradeSamples(raw, result.settings), baseline.space);
    if (color.samples >= 32 && color.high > .02 && color.median > .015 && color.median < 1.3) {
        result.settings.saturation =
            std::clamp(std::pow(.30 / std::max(color.median, .05), .12), .9, 1.12);
        result.settings.vibrance = std::clamp(45 * (.35 - color.median) / .35, 0., 25.);
    }
    result.applied = true;
    result.status = wb.reliable ? "Auto applied" : "Auto applied; WB unchanged (low confidence)";
    return result;
}
} // namespace renzu
