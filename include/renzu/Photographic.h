// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "Match.h"
namespace renzu {
struct WhiteBalanceControls {
    double kelvin = 6504, tint = 0;
};
// Planckian CCT approximation (2000..25000 K), normalized at D65/6504 K.
std::array<double, 2> whiteBalanceFromKelvin(double kelvin, double tint);
WhiteBalanceControls whiteBalanceToKelvin(double temperature, double tint);
struct ColorStatistics {
    double low = 0, median = 0, high = 0;
    std::size_t samples = 0;
};
ColorStatistics analyzeColor(const std::vector<V3> &linear, int space);
struct AutoResult {
    Settings settings;
    bool applied = false, whiteBalanceApplied = false;
    std::string status;
};
AutoResult autoGrade(const std::vector<V3> &raw, const Settings &baseline);
} // namespace renzu
