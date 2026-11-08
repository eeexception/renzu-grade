// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "Engine.h"
namespace renzu {
struct Control {
    const char *id;
    const char *label;
    const char *group;
    double value, min, max;
    double Settings::*member;
    const char *hint;
};
inline constexpr Control controls[] = {
    {"temperature", "Temperature", "whiteBalance", 0, -600, 200, &Settings::temperature,
     "Normalized Bradford adaptation. Positive warms; not Kelvin."},
    {"tint", "Tint", "whiteBalance", 0, -500, 500, &Settings::tint,
     "Positive magenta; negative green."},
    {"exposure", "Exposure (EV)", "tone", 0, -5, 5, &Settings::exposure,
     "Scene-linear gain: 2^EV."},
    {"shadows", "Shadows (EV)", "tone", 0, -3, 3, &Settings::shadows,
     "Smooth dark-region exposure weight."},
    {"midtones", "Midtones (EV)", "tone", 0, -3, 3, &Settings::midtones,
     "Smooth middle-region exposure weight."},
    {"lights", "Lights (EV)", "tone", 0, -3, 3, &Settings::lights,
     "Smooth bright-midrange exposure weight."},
    {"highlights", "Highlights (EV)", "tone", 0, -3, 3, &Settings::highlights,
     "Smooth brightest-region exposure weight."},
    {"contrast", "Contrast", "color", 1, 0, 2, &Settings::contrast,
     "Log luminance contrast with soft tails around the pivot."},
    {"pivot", "Pivot (linear)", "color", .18, .001, 4, &Settings::pivot,
     "Scene-linear reference luminance; 0.18 is middle gray."},
    {"saturation", "Saturation", "color", 1, 0, 2, &Settings::saturation,
     "Luminance-preserving chroma scaling."},
    {"vibrance", "Vibrance", "color", 0, -100, 100, &Settings::vibrance,
     "Preferentially adjusts less saturated colors, protecting warm hues."}};
inline constexpr const char *toneNames[] = {"exposure", "shadows",    "midtones",
                                            "lights",   "highlights", "contrast"};
} // namespace renzu
