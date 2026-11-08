// SPDX-License-Identifier: GPL-3.0-only
#include "renzu/Controls.h"
#include "renzu/Engine.h"
#include "renzu/Half.h"
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace renzu;
int checks = 0;
void require(bool v, const char *msg) {
    ++checks;
    if (!v)
        throw std::runtime_error(msg);
}
void near(double a, double b, double eps, const char *msg) {
    require(std::abs(a - b) <= eps, msg);
}
V3 run(V3 c, Settings s = {}) {
    auto p = prepare(s);
    return process(c, .37f, p);
}
std::vector<V3> ramp(double min, double max) {
    std::vector<V3> r;
    for (int i = 0; i < 4096; ++i) {
        float y = float(min + (max - min) * i / 4095);
        r.push_back({y, y, y});
    }
    return r;
}
int main() {
    try {
        // Exhaust every binary16 encoding. Finite values and signed zero must
        // round-trip exactly; infinity/NaN must retain their classification.
        for (unsigned bits = 0; bits < 65536; ++bits) {
            auto h = std::uint16_t(bits);
            float value = halfToFloat(h);
            if ((h & 0x7fffu) > 0x7c00u)
                require(std::isnan(value) && (floatToHalf(value) & 0x7fffu) > 0x7c00u,
                        "half NaN classification");
            else
                require(floatToHalf(value) == h, "exact half encoding roundtrip");
            if ((h & 0x7c00u) != 0x7c00u) {
                int exponent = (h >> 10) & 31, fraction = h & 1023;
                double expected = exponent == 0
                                      ? std::ldexp(double(fraction), -24)
                                      : std::ldexp(double(1024 + fraction), exponent - 25);
                near(value, (h & 0x8000u) ? -expected : expected, 0,
                     "half numeric value matches IEEE definition");
            }
        }
        near(halfToFloat(0x0001), std::ldexp(1., -24), 0, "minimum half subnormal");
        near(halfToFloat(0x0400), std::ldexp(1., -14), 0, "minimum half normal");
        near(halfToFloat(0x7bff), 65504, 0, "maximum finite half");
        require(floatToHalf(std::ldexp(1.f, -25)) == 0, "half underflow tie to even zero");
        require(floatToHalf(std::nextafter(std::ldexp(1.f, -25), 1.f)) == 1,
                "half rounds above underflow midpoint");
        for (unsigned bits = 0; bits < 0x7bff; ++bits) {
            float low = halfToFloat(std::uint16_t(bits));
            float high = halfToFloat(std::uint16_t(bits + 1));
            float midpoint = (low + high) * .5f;
            auto even = std::uint16_t(bits + (bits & 1u));
            require(floatToHalf(midpoint) == even, "half midpoint rounds to even");
            require(floatToHalf(-midpoint) == (even | 0x8000u),
                    "negative half midpoint rounds to even");
            require(floatToHalf(std::nextafter(midpoint, low)) == bits &&
                        floatToHalf(std::nextafter(midpoint, high)) == bits + 1,
                    "half rounding on both sides of midpoint");
        }
        for (int space = 0; space < 3; ++space) {
            Settings s;
            s.space = space;
            for (float v : {-2.f, -.01f, 0.f, .001f, .18f, 1.f, 3.f}) {
                near(encode(decode(v, space), space), v, 2.e-5, "transfer roundtrip");
                auto c = run({v, v * .7f, v * 1.2f}, s);
                near(c.r, v, 0, "exact neutral identity");
            }
        }
        Settings s;
        s.space = 2;
        s.exposure = 1;
        near(run({.18, .18, .18}, s).r, .36, 1.e-6, "+EV");
        s.exposure = -1;
        near(run({.18, .18, .18}, s).r, .09, 1.e-6, "-EV");
        s = Settings{};
        s.space = 2;
        s.contrast = 1.5;
        near(run({.18, .18, .18}, s).r, .18, 1.e-6, "contrast pivot");
        require(run({.04, .04, .04}, s).r < .04, "contrast shadows");
        require(run({.6, .6, .6}, s).r > .6, "contrast lights");
        s.contrast = 0;
        near(run({.05, .05, .05}, s).r, .18, 1.e-6, "zero contrast");
        for (int j = 0; j < 4; ++j) {
            s = Settings{};
            s.space = 2;
            double Settings::*m[] = {&Settings::shadows, &Settings::midtones, &Settings::lights,
                                     &Settings::highlights};
            s.*m[j] = 1;
            float center = float(.18 * std::exp2(std::array<double, 4>{-5, -1, 2, 5.5}[j]));
            require(run({center, center, center}, s).r > center * 1.4, "tonal isolation center");
            float previous = 0;
            for (int i = 1; i <= 10000; ++i) {
                float x = i / 1000.f;
                float v = run({x, x, x}, s).r;
                require(std::isfinite(v), "finite tonal ramp");
                require(std::abs(v - previous) < .05, "smooth tonal ramp");
                previous = v;
            }
        }
        s = Settings{};
        s.space = 2;
        s.saturation = 0;
        auto gray = run({.6, .2, .1}, s);
        near(gray.r, gray.g, 1.e-6, "desaturate gray");
        near(gray.b, gray.g, 1.e-6, "desaturate blue");
        near(gray.r, luma({.6, .2, .1}, 2), 1.e-6, "luminance saturation");
        s.saturation = 1;
        s.vibrance = 100;
        auto v = run({.3, .2, .15}, s);
        require(v.r - v.b > .15, "vibrance effect");
        near(luma(v, 2), luma({.3, .2, .15}, 2), 1.e-6, "vibrance luminance");
        auto n = run({.18, .18, .18}, s);
        near(n.r, n.g, 1.e-6, "gray vibrance");
        s = Settings{};
        s.space = 2;
        s.temperature = 60;
        auto warm = run({.18, .18, .18}, s);
        require(warm.r > warm.b, "temperature warms");
        near(luma(warm, 2), .18, 3.e-5, "WB luminance");
        s.temperature = -60;
        auto cool = run({.18, .18, .18}, s);
        require(cool.b > cool.r, "temperature cools");
        s.temperature = 0;
        s.tint = 60;
        auto tint = run({.18, .18, .18}, s);
        require((tint.r + tint.b) / 2 > tint.g, "tint magenta");
        for (int space = 0; space < 3; ++space) {
            s = Settings{};
            s.space = space;
            s.temperature = 35;
            s.tint = -18;
            auto p = prepare(s);
            auto cast = matrix({.25, .25, .25}, p.wb);
            auto corr = neutralWB(cast, space);
            s.temperature = corr.temperature;
            s.tint = corr.tint;
            auto q = prepare(s);
            auto restored = matrix(cast, q.wb);
            near(restored.r, restored.g, 2.e-5, "neutral inverse r/g");
            near(restored.b, restored.g, 2.e-5, "neutral inverse b/g");
        }
        s = Settings{};
        s.space = 2;
        s.exposure = .5;
        v = run({-.2, 2, 4}, s);
        require(v.r < 0 && v.g > 1, "extended range");
        auto bad = run({NAN, INFINITY, -INFINITY}, s);
        require(std::isfinite(bad.r) && std::isfinite(bad.g) && std::isfinite(bad.b),
                "NaN safeguard");
        s.exposure = NAN;
        require(prepare(s).exposure == 0, "invalid settings");
        for (auto depth : {Depth::Byte, Depth::Short, Depth::Half, Depth::Float})
            for (int channels : {3, 4}) {
                constexpr int w = 17, h = 37;
                int row = w * channels * bytesPerChannel(depth) + 16;
                std::vector<unsigned char> a(row * h, 0), b(row * h, 0xcd), c(row * h, 0xcd);
                Image src{a.data(), {3, 7, 3 + w, 7 + h}, row, depth, channels},
                    dst{b.data(), src.bounds, row, depth, channels},
                    negative{c.data() + (h - 1) * row, src.bounds, -row, depth, channels};
                for (int y = 7; y < 7 + h; ++y)
                    for (int x = 3; x < 3 + w; ++x)
                        writePixel(src, x, y, {.1f, .2f, .3f, .37f});
                s = Settings{};
                s.space = 2;
                s.exposure = 1;
                auto p = prepare(s);
                renderCPU(src, dst, {5, 9, 16, 35}, p, 4);
                renderCPU(src, negative, {5, 9, 16, 35}, p, 3);
                auto x = readPixel(dst, 8, 20), z = readPixel(negative, 8, 20);
                near(x[0], .2, .009, "CPU exposure depth");
                near(x[3], channels == 4 ? readPixel(src, 8, 20)[3] : 1, 0, "alpha preserved");
                near(x[1], z[1], 0, "negative stride");
                require(b[0] == 0xcd, "window guard");
            }
        {
            float in[] = {.1, .2, .3, .25}, out[4];
            Image a{in, {0, 0, 1, 1}, 16, Depth::Float, 4}, b{out, a.bounds, 16, Depth::Float, 4};
            s = Settings{};
            s.space = 2;
            s.premultiplied = true;
            s.exposure = 1;
            renderCPU(a, b, a.bounds, prepare(s), 1);
            near(out[0], .2, 1.e-6, "premult exposure");
            near(out[3], .25, 0, "premult alpha");
        }
        auto under = analyze(ramp(.005, .09), 2), over = analyze(ramp(.5, 1.5), 2),
             normal = analyze(ramp(.03, .6), 2), low = analyze(ramp(.15, .21), 2),
             high = analyze(ramp(.001, 2), 2), dark = analyze(ramp(.0001, .01), 2),
             bright = analyze(ramp(.6, .9), 2);
        s = Settings{};
        s.space = 2;
        auto t = autoTone(under, s, 1);
        require(t.exposure > 0, "auto under");
        require(autoTone(over, s, 1).exposure < 0, "auto over");
        require(std::abs(autoTone(normal, s, 1).exposure) < .5, "normal bounded");
        require(autoTone(low, s, 1).contrast > 1, "low contrast");
        require(autoTone(high, s, 1).contrast <= 1.05, "high contrast");
        require(autoTone(dark, s, 1).exposure <= .4, "low key preserved");
        require(autoTone(bright, s, 1).exposure >= -.45, "high key preserved");
        auto night = ramp(.001, .008);
        night[10] = {15, 15, 15};
        auto ns = analyze(night, 2);
        require(ns.high < .009, "isolated lights percentile");
        require(autoTone(ns, s, 1).exposure <= .4, "night preserved");
        auto backlit = ramp(.008, .03);
        for (std::size_t i = 0; i < backlit.size() / 3; ++i)
            backlit[i] = {2, 2, 2};
        auto backlitTone = autoTone(analyze(backlit, 2), s, 1);
        require(backlitTone.exposure <= 1.25 && backlitTone.shadows <= .5 &&
                    backlitTone.highlights <= .2,
                "backlit foreground remains bounded");
        auto clipped = ramp(.2, 1);
        for (std::size_t i = 0; i < clipped.size() / 4; ++i)
            clipped[i] = {1, 1, 1};
        auto clippingStats = analyze(clipped, 2);
        require(clippingStats.whiteClipping >= .25, "white plateau clipping estimate");
        for (auto pixels : {ramp(0, 0), ramp(1, 1), ramp(.02, 3), ramp(.001, .002)}) {
            auto a = autoTone(analyze(pixels, 2), s, 1);
            require(std::isfinite(a.exposure) && a.exposure >= -2 && a.exposure <= 1.25,
                    "edge tone bounds");
        }
        s.temperature = 21;
        s.tint = -7;
        s.saturation = 1.3;
        s.vibrance = 20;
        s.exposure = .2;
        auto a = autoTone(under, s, 0);
        near(a.exposure, s.exposure, 0, "strength zero");
        a = autoTone(under, s, 1);
        near(a.temperature, 21, 0, "tone preserves temperature");
        near(a.saturation, 1.3, 0, "tone preserves saturation");
        auto half = autoTone(under, s, .5);
        near(half.exposure, (a.exposure + s.exposure) / 2, 1.e-9, "strength parameter blend");
        for (auto test : std::array<std::array<float, 3>, 5>{{{.2, .2, .2},
                                                              {.24, .2, .17},
                                                              {.17, .2, .24},
                                                              {.19, .24, .19},
                                                              {.24, .19, .24}}}) {
            std::vector<V3> pixels(4096, {test[0], test[1], test[2]});
            auto wb = autoWB(pixels, 2);
            require(wb.reliable, "WB cast reliable");
            if (test[0] > test[2])
                require(wb.temperature < 0, "warm cast correction");
            if (test[2] > test[0])
                require(wb.temperature > 0, "cool cast correction");
            if (test[1] > std::max(test[0], test[2]))
                require(wb.tint > 0, "green cast correction");
            if (test[1] < std::min(test[0], test[2]))
                require(wb.tint < 0, "magenta correction");
        }
        for (V3 c : std::vector<V3>{
                 {1, .01, .02}, {.01, .02, 1}, {1, 0, 1}, {0, 0, 0}, {1, 1, 1}, {.001, .001, .001}})
            require(!autoWB(std::vector<V3>(4096, c), 2).reliable, "unreliable WB rejected");
        auto mixed = ramp(.04, .7);
        for (std::size_t i = 0; i < mixed.size() / 2; ++i)
            mixed[i] = {.4, .03, .01};
        require(std::isfinite(autoWB(mixed, 2).confidence), "mixed lighting confidence");
        auto opposing = std::vector<V3>(4096, {.24, .2, .17});
        for (std::size_t i = 0; i < opposing.size() / 2; ++i)
            opposing[i] = {.17, .2, .24};
        auto mixedWB = autoWB(opposing, 2);
        require(std::abs(mixedWB.temperature) < 5, "opposing casts do not overcorrect temperature");
        std::uint64_t total = 0;
        for (auto bin : under.histogram)
            total += bin;
        require(total == under.samples, "histogram population");
        require(under.low < under.median && under.median < under.high, "percentile order");
        for (auto &c : controls)
            require(c.value >= c.min && c.value <= c.max, "control range");
        std::cout << "PASS: " << checks << " color, analysis, and image assertions\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "FAIL after " << checks << " checks: " << e.what() << "\n";
        return 1;
    }
}
