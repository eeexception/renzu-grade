// SPDX-License-Identifier: GPL-3.0-only
#include "renzu/Photographic.h"
#include <iostream>
#include <stdexcept>
using namespace renzu;
int checks = 0;
void require(bool v, const char *message) {
    ++checks;
    if (!v)
        throw std::runtime_error(message);
}
void near(double a, double b, double e, const char *message) {
    require(std::abs(a - b) <= e, message);
}
std::vector<V3> chart() {
    std::vector<V3> out;
    for (int i = 0; i < 512; ++i) {
        float y = float(.025 * std::exp2((i % 64) * 6. / 63.));
        float c = i < 128 ? 0.f : float((i % 7 + 1) * .035);
        switch ((i / 64) % 3) {
        case 0:
            out.push_back({y * (1 + c), y * (1 - .2f * c), y * (1 - .8f * c)});
            break;
        case 1:
            out.push_back({y * (1 - .4f * c), y * (1 + .3f * c), y * (1 - .2f * c)});
            break;
        default:
            out.push_back({y * (1 - .3f * c), y * (1 - .2f * c), y * (1 + .6f * c)});
        }
    }
    return out;
}
int main() {
    try {
        auto neutral = whiteBalanceFromKelvin(6504, 0);
        near(neutral[0], 0, 0, "D65 temperature identity");
        near(neutral[1], 0, 0, "D65 tint identity");
        double previous = -1.e9;
        for (double kelvin :
             {2000, 2500, 3200, 3990, 4000, 4010, 5500, 6504, 7500, 10000, 15000, 25000}) {
            auto wb = whiteBalanceFromKelvin(kelvin, 23);
            require(wb[0] > previous, "Kelvin warmth monotonic");
            previous = wb[0];
            auto roundtrip = whiteBalanceToKelvin(wb[0], wb[1]);
            near(roundtrip.kelvin, kelvin, .2, "Kelvin roundtrip");
            near(roundtrip.tint, 23, .02, "tint roundtrip");
            Settings s;
            s.space = 2;
            s.temperature = wb[0];
            s.tint = wb[1];
            auto p = prepare(s);
            auto c = process({.18, .18, .18}, 1, p);
            require(std::isfinite(c.r) && std::isfinite(c.g) && std::isfinite(c.b),
                    "finite Kelvin WB");
            near(luma(c, 2), .18, 3.e-5, "Kelvin preserves neutral luminance");
        }
        auto pixels = chart();
        Settings base;
        base.space = 2;
        auto automatic = autoGrade(pixels, base);
        require(automatic.applied, "full Auto computes grade");
        require(automatic.settings.saturation >= .9 && automatic.settings.saturation <= 1.12,
                "Auto saturation conservative");
        require(automatic.settings.vibrance >= 0 && automatic.settings.vibrance <= 25,
                "Auto vibrance bounded");
        auto muted = pixels;
        for (auto &c : muted) {
            float y = luma(c, 2);
            c = {y + (c.r - y) * .2f, y + (c.g - y) * .2f, y + (c.b - y) * .2f};
        }
        auto colorAuto = autoGrade(muted, base);
        require(colorAuto.settings.saturation > 1 || colorAuto.settings.vibrance > 0,
                "Auto actually boosts muted color");
        auto gray = std::vector<V3>(512, {.18, .18, .18});
        auto grayAuto = autoGrade(gray, base);
        near(grayAuto.settings.saturation, 1, 0, "Auto leaves grayscale saturation");
        near(grayAuto.settings.vibrance, 0, 0, "Auto leaves grayscale vibrance");
        require(!autoGrade(std::vector<V3>(512, {0, 0, 0}), base).applied,
                "Auto rejects black frame");
        Settings look = base;
        look.exposure = .65;
        look.contrast = 1.3;
        look.saturation = 1.25;
        look.vibrance = 25;
        auto reference = makeReference(pixels, look, "Look", 0);
        for (int mask = 0; mask < 16; ++mask) {
            MatchFields fields{bool(mask & 1), bool(mask & 2), bool(mask & 4), bool(mask & 8)};
            auto fit = matchReference(pixels, base, reference, fields);
            if (!mask) {
                require(!fit.applied(), "empty match selection preserves grade");
                continue;
            }
            require(fit.applied(), "selected match fields fit");
            if (!fields.whiteBalance) {
                near(fit.settings.temperature, base.temperature, 0,
                     "unselected WB temperature unchanged");
                near(fit.settings.tint, base.tint, 0, "unselected WB tint unchanged");
            }
            if (!fields.exposure)
                near(fit.settings.exposure, base.exposure, 0, "unselected exposure unchanged");
            if (!fields.contrast)
                near(fit.settings.contrast, base.contrast, 0, "unselected contrast unchanged");
            if (!fields.color) {
                near(fit.settings.vibrance, base.vibrance, 0, "unselected vibrance unchanged");
                near(fit.settings.saturation, base.saturation, 0,
                     "unselected saturation unchanged");
            }
            near(fit.settings.shadows, base.shadows, 0, "match leaves HDR blacks");
            near(fit.settings.midtones, base.midtones, 0, "match leaves HDR shadows");
            near(fit.settings.lights, base.lights, 0, "match leaves HDR highlights");
            near(fit.settings.highlights, base.highlights, 0, "match leaves HDR whites");
        }
        Settings toneLook = base;
        toneLook.exposure = .7;
        toneLook.contrast = 1.4;
        auto toneReference = makeReference(pixels, toneLook, "Tone", 0);
        auto tone =
            matchReference(pixels, base, toneReference, MatchFields{false, true, true, false});
        near(tone.settings.exposure, .7, .04, "joint exposure fit");
        near(tone.settings.contrast, 1.4, .04, "joint contrast fit");
        Settings colorLook = base;
        colorLook.saturation = 1.35;
        colorLook.vibrance = 35;
        auto colorRef = makeReference(pixels, colorLook, "Color", 0);
        auto colorFit =
            matchReference(pixels, base, colorRef, MatchFields{false, false, false, true});
        require(colorFit.colorMatched, "Color match operates");
        auto actual = gradeSamples(pixels, colorFit.settings), expected = colorRef.pixels;
        double error = 0, before = 0;
        for (std::size_t i = 0; i < actual.size(); ++i)
            for (auto component : {&V3::r, &V3::g, &V3::b}) {
                error += std::abs(actual[i].*component - expected[i].*component);
                before += std::abs(pixels[i].*component - expected[i].*component);
            }
        require(error < before * .25, "combined vibrance/saturation fit improves actual pixels");
        std::cout << "PASS: " << checks
                  << " photographic controls, full Auto and field-combination assertions\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "FAIL after " << checks << ": " << e.what() << '\n';
        return 1;
    }
}
