// SPDX-License-Identifier: GPL-3.0-only
#include "renzu/Photographic.h"
#include <stdexcept>
namespace renzu {
static std::vector<V3> compact(const std::vector<V3> &in) {
    std::vector<V3> out;
    std::size_t n = std::min(in.size(), maxReferenceSamples);
    out.reserve(n);
    for (std::size_t j = 0; j < n; ++j) {
        auto c = in[(j * in.size() + in.size() / 2) / n];
        if (std::isfinite(c.r) && std::isfinite(c.g) && std::isfinite(c.b))
            out.push_back(c);
    }
    return out;
}
std::vector<V3> gradeSamples(const std::vector<V3> &in, const Settings &settings) {
    auto p = prepare(settings);
    p.premultiplied = 0; // sampleImage already unpremultiplied.
    auto out = compact(in);
    if (p.identity)
        return out;
    for (auto &c : out) {
        V3 encoded{encode(c.r, p.space), encode(c.g, p.space), encode(c.b, p.space)};
        c = decode3(process(encoded, 1, p), p.space);
    }
    return out;
}
ReferenceFrame makeReference(const std::vector<V3> &raw, const Settings &settings,
                             const std::string &name, double time) {
    ReferenceFrame r;
    r.name = name;
    r.time = time;
    r.space = settings.space;
    r.pixels = gradeSamples(raw, settings);
    if (r.pixels.size() < 32)
        throw std::runtime_error("Reference needs at least 32 valid opaque samples");
    return r;
}
static std::vector<V3> common(const std::vector<V3> &in, int space) {
    auto out = compact(in);
    for (auto &c : out)
        c = convertLinear(c, space, 2);
    return out;
}
static WBResult estimate(const std::vector<V3> &rgb) {
    auto stats = analyze(rgb, 2);
    if (stats.median <= 1.e-5)
        return {};
    std::vector<V3> normalized;
    normalized.reserve(rgb.size());
    float gain = float(.18 / stats.median);
    for (auto c : rgb)
        normalized.push_back(mul(c, gain));
    return autoWB(normalized, 2);
}
static double exposureError(const Statistics &target, const Statistics &reference) {
    // A few isolated bright/dark samples cannot dominate the fitted placement.
    auto ratio = [](double a, double b) {
        return std::log2(std::max(a, 1.e-6) / std::max(b, 1.e-6));
    };
    return .25 * ratio(target.p10, reference.p10) + .5 * ratio(target.median, reference.median) +
           .25 * ratio(target.p90, reference.p90);
}
MatchResult matchReference(const std::vector<V3> &raw, const Settings &baseline,
                           const ReferenceFrame &reference, MatchFields fields) {
    MatchResult result;
    result.settings = baseline;
    if (!std::isfinite(baseline.temperature) || !std::isfinite(baseline.tint) ||
        !std::isfinite(baseline.exposure)) {
        result.status = "Match: reset invalid white balance / exposure values before matching";
        return result;
    }
    if (!fields.whiteBalance && !fields.exposure && !fields.contrast && !fields.color) {
        result.status = "Select at least one Match field";
        return result;
    }
    if (baseline.space < 0 || baseline.space > 2 || reference.space < 0 || reference.space > 2) {
        result.status = "Match: unsupported mode or encoding";
        return result;
    }
    auto samples = compact(raw);
    auto ref = common(reference.pixels, reference.space);
    auto refStats = analyze(ref, 2);
    auto current = common(gradeSamples(samples, baseline), baseline.space);
    auto targetStats = analyze(current, 2);
    if (refStats.samples < 32 || targetStats.samples < 32 || refStats.median < 1.e-5 ||
        targetStats.median < 1.e-5) {
        result.status = "Match: reference or target has insufficient usable light; grade preserved";
        return result;
    }
    bool wantWB = fields.whiteBalance, wantExposure = fields.exposure;
    if (wantWB) {
        auto desired = estimate(ref), initial = estimate(current);
        result.confidence = std::min(desired.confidence, initial.confidence);
        if (desired.reliable && initial.reliable) {
            // Bradford gains are logarithmic. Relative neutral estimates preserve
            // the reference's cast instead of independently neutralizing both shots.
            for (int iteration = 0; iteration < 6; ++iteration) {
                auto measured = estimate(current);
                if (!measured.reliable)
                    break;
                double dt = measured.temperature - desired.temperature;
                double di = measured.tint - desired.tint;
                if (std::abs(dt) + std::abs(di) < .01)
                    break;
                result.settings.temperature = std::clamp(
                    result.settings.temperature + std::clamp(dt, -35., 35.), -100., 100.);
                result.settings.tint =
                    std::clamp(result.settings.tint + std::clamp(di, -35., 35.), -100., 100.);
                current = common(gradeSamples(samples, result.settings), baseline.space);
            }
            result.whiteBalanceMatched = true;
            result.limited = std::abs(result.settings.temperature) >= 99.999 ||
                             std::abs(result.settings.tint) >= 99.999;
        }
    }
    for (int round = 0; round < (fields.contrast && fields.exposure ? 4 : 1); ++round) {
        if (fields.contrast) {
            auto spread = [](const Statistics &s) {
                return std::log2(std::max(s.p90, 1.e-6) / std::max(s.p10, 1.e-6));
            };
            auto before =
                analyze(common(gradeSamples(samples, result.settings), baseline.space), 2);
            if (spread(before) > .01) {
                double desired = spread(refStats), lo = 0, hi = 2, best = result.settings.contrast;
                auto error = [&](double value) {
                    auto s = result.settings;
                    s.contrast = value;
                    return spread(analyze(common(gradeSamples(samples, s), baseline.space), 2)) -
                           desired;
                };
                double bestError = std::abs(error(best));
                for (int j = 0; j < 20; ++j) {
                    double mid = (lo + hi) * .5, e = error(mid);
                    if (std::abs(e) < bestError) {
                        best = mid;
                        bestError = std::abs(e);
                    }
                    if (e > 0)
                        hi = mid;
                    else
                        lo = mid;
                }
                result.settings.contrast = best;
                result.contrastMatched = true;
                result.limited |= bestError > .1;
            }
        }
        if (wantExposure) {
            Settings candidate = result.settings;
            auto error = [&](double ev) {
                candidate.exposure = ev;
                return exposureError(
                    analyze(common(gradeSamples(samples, candidate), baseline.space), 2), refStats);
            };
            double best = baseline.exposure, bestError = std::abs(error(best));
            double lo = -5, hi = 5;
            // Evaluate endpoints too, so an unreachable target chooses a bounded result.
            for (double ev : {lo, hi}) {
                double e = std::abs(error(ev));
                if (e < bestError) {
                    best = ev;
                    bestError = e;
                }
            }
            for (int j = 0; j < 20; ++j) {
                double mid = (lo + hi) * .5, e = error(mid);
                if (std::abs(e) < bestError) {
                    best = mid;
                    bestError = std::abs(e);
                }
                if (e > 0)
                    hi = mid;
                else
                    lo = mid;
            }
            result.settings.exposure = best;
            result.exposureMatched = true;
            result.limited |= bestError > .1;
        }
    }
    if (fields.color) {
        auto signature = [](std::vector<V3> colors) {
            auto stats = analyze(colors, 2);
            if (stats.median > 1.e-6)
                for (auto &c : colors)
                    c = mul(c, float(.18 / stats.median));
            return analyzeColor(colors, 2);
        };
        auto desired = signature(ref);
        if (desired.samples >= 32) {
            auto loss = [&](double saturation, double vibrance) {
                auto s = result.settings;
                s.saturation = saturation;
                s.vibrance = vibrance;
                auto measured = signature(common(gradeSamples(samples, s), baseline.space));
                auto term = [](double a, double b) {
                    return std::pow(std::log((a + .01) / (b + .01)), 2);
                };
                return term(measured.low, desired.low) + term(measured.median, desired.median) +
                       term(measured.high, desired.high) +
                       .00001 * (std::pow(saturation - baseline.saturation, 2) +
                                 std::pow((vibrance - baseline.vibrance) / 100., 2));
            };
            double sat = baseline.saturation, vib = baseline.vibrance, best = loss(sat, vib);
            for (int a = 0; a <= 8; ++a)
                for (int b = 0; b <= 8; ++b) {
                    double s = a * .25, v = -100 + b * 25., e = loss(s, v);
                    if (e < best) {
                        sat = s;
                        vib = v;
                        best = e;
                    }
                }
            for (double step : {.125, .0625, .03125, .015625, .0078125}) {
                for (int repeat = 0; repeat < 3; ++repeat) {
                    bool improved = false;
                    double centerS = sat, centerV = vib;
                    for (int a = -1; a <= 1; ++a)
                        for (int b = -1; b <= 1; ++b) {
                            double s = std::clamp(centerS + a * step, 0., 2.),
                                   v = std::clamp(centerV + b * step * 100, -100., 100.),
                                   e = loss(s, v);
                            if (e + 1.e-12 < best) {
                                sat = s;
                                vib = v;
                                best = e;
                                improved = true;
                            }
                        }
                    if (!improved)
                        break;
                }
            }
            result.settings.saturation = sat;
            result.settings.vibrance = vib;
            result.colorMatched = true;
            result.limited |= best > .03;
        }
    }
    if (result.applied()) {
        result.status = "Matched " + reference.name;
        if (result.whiteBalanceMatched)
            result.status +=
                "; WB confidence " + std::to_string(int(result.confidence * 100)) + "%";
        if (wantWB && !result.whiteBalanceMatched)
            result.status += "; WB uncertain, existing WB preserved";
        if (result.limited)
            result.status += "; approximate fit, refine manually";
    } else
        result.status = fields.whiteBalance ? "WB unchanged: no reliable neutral"
                                            : "Not enough variation to match selected fields";
    return result;
}
MatchResult matchReference(const std::vector<V3> &raw, const Settings &baseline,
                           const ReferenceFrame &reference, MatchMode mode) {
    if (int(mode) < 0 || int(mode) > 2) {
        MatchResult r;
        r.settings = baseline;
        r.status = "Invalid match mode";
        return r;
    }
    return matchReference(
        raw, baseline, reference,
        MatchFields{mode != MatchMode::Exposure, mode != MatchMode::WhiteBalance, false, false});
}
} // namespace renzu
