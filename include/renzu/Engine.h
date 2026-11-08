// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "Kernel.h"
#include <array>
#include <functional>
#include <string>
#include <vector>
namespace renzu {
struct Settings {
    double temperature = 0, tint = 0, exposure = 0, shadows = 0, midtones = 0, lights = 0,
           highlights = 0;
    double contrast = 1, pivot = .18, saturation = 1, vibrance = 0;
    int space = 0;
    bool premultiplied = false;
};
KernelParams prepare(const Settings &s);
std::array<double, 9> rgbToXYZ(int space);
V3 toLMS(V3 rgb, int space);
struct WBResult {
    double temperature = 0, tint = 0, confidence = 0;
    std::size_t candidates = 0;
    bool reliable = false;
    std::string status;
};
WBResult neutralWB(V3 linear, int space);
enum class Depth : int { Byte = 0, Short = 1, Half = 2, Float = 3 };
struct Rect {
    int x1, y1, x2, y2;
};
struct Image {
    void *data = nullptr;
    Rect bounds{};
    std::ptrdiff_t rowBytes = 0;
    Depth depth = Depth::Float;
    int components = 4;
};
int bytesPerChannel(Depth depth);
bool valid(const Image &image);
std::array<float, 4> readPixel(const Image &, int x, int y);
void writePixel(const Image &, int x, int y, const std::array<float, 4> &);
void renderRows(const Image &src, const Image &dst, Rect window, const KernelParams &p,
                const std::function<bool()> &abort = {});
void renderCPU(const Image &src, const Image &dst, Rect window, const KernelParams &p,
               unsigned threads = 0, const std::function<bool()> &abort = {});
std::vector<V3> sampleImage(const Image &src, int space, bool premultiplied,
                            std::size_t budget = 262144);
struct Statistics {
    std::array<std::uint64_t, 512> histogram{};
    double low = 0, high = 0, median = 0, p10 = 0, p90 = 0;
    double shadows = 0, midtones = 0, highlights = 0, blackClipping = 0, whiteClipping = 0,
           rangeEV = 0;
    std::size_t samples = 0;
};
Statistics analyze(const std::vector<V3> &linear, int space);
Settings autoTone(const Statistics &, const Settings &baseline, double strength);
WBResult autoWB(const std::vector<V3> &linear, int space);
WBResult sampleNeutral(const Image &src, int space, bool premultiplied, double x, double y,
                       double radius);
} // namespace renzu
