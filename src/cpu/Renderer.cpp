// SPDX-License-Identifier: GPL-3.0-only
#include "renzu/Engine.h"
#include "renzu/Half.h"
#include <cstring>
#include <future>
#include <limits>
#include <stdexcept>
#include <thread>
namespace renzu {
int bytesPerChannel(Depth d) {
    switch (d) {
    case Depth::Byte:
        return 1;
    case Depth::Short:
    case Depth::Half:
        return 2;
    case Depth::Float:
        return 4;
    }
    return 0;
}
bool valid(const Image &i) {
    auto w = std::int64_t(i.bounds.x2) - i.bounds.x1, h = std::int64_t(i.bounds.y2) - i.bounds.y1;
    return i.data && w > 0 && h > 0 && w <= std::numeric_limits<int>::max() &&
           h <= std::numeric_limits<int>::max() &&
           i.rowBytes != std::numeric_limits<std::ptrdiff_t>::min() &&
           (i.components == 3 || i.components == 4) && bytesPerChannel(i.depth) > 0 &&
           std::abs(i.rowBytes) >= w * i.components * bytesPerChannel(i.depth);
}
static char *pixel(const Image &i, int x, int y) {
    if (!i.data || x < i.bounds.x1 || x >= i.bounds.x2 || y < i.bounds.y1 || y >= i.bounds.y2)
        return nullptr;
    return static_cast<char *>(i.data) + (y - i.bounds.y1) * i.rowBytes +
           std::ptrdiff_t(x - i.bounds.x1) * i.components * bytesPerChannel(i.depth);
}
static float readChannel(const char *p, Depth d) {
    if (d == Depth::Byte) {
        std::uint8_t v;
        std::memcpy(&v, p, 1);
        return v / 255.f;
    }
    if (d == Depth::Short) {
        std::uint16_t v;
        std::memcpy(&v, p, 2);
        return v / 65535.f;
    }
    if (d == Depth::Half) {
        std::uint16_t v;
        std::memcpy(&v, p, 2);
        return halfToFloat(v);
    }
    float v;
    std::memcpy(&v, p, 4);
    return v;
}
static void writeChannel(char *p, Depth d, float v) {
    if (d == Depth::Byte) {
        auto q = std::uint8_t(std::lround(clampf(safe(v), 0, 1) * 255));
        std::memcpy(p, &q, 1);
    } else if (d == Depth::Short) {
        auto q = std::uint16_t(std::lround(clampf(safe(v), 0, 1) * 65535));
        std::memcpy(p, &q, 2);
    } else if (d == Depth::Half) {
        std::uint16_t q = floatToHalf(clampf(safe(v), -65504, 65504));
        std::memcpy(p, &q, 2);
    } else
        std::memcpy(p, &v, 4);
}
std::array<float, 4> readPixel(const Image &i, int x, int y) {
    auto p = pixel(i, x, y);
    if (!p)
        return {0, 0, 0, 0};
    std::array<float, 4> v{0, 0, 0, 1};
    int n = bytesPerChannel(i.depth);
    for (int c = 0; c < i.components; ++c)
        v[c] = readChannel(p + c * n, i.depth);
    return v;
}
void writePixel(const Image &i, int x, int y, const std::array<float, 4> &v) {
    auto p = pixel(i, x, y);
    if (!p)
        return;
    int n = bytesPerChannel(i.depth);
    for (int c = 0; c < i.components; ++c)
        writeChannel(p + c * n, i.depth, v[c]);
}
void renderRows(const Image &src, const Image &dst, Rect w, const KernelParams &p,
                const std::function<bool()> &abort) {
    if (!valid(dst))
        throw std::invalid_argument("Invalid destination image");
    w = {std::max(w.x1, dst.bounds.x1), std::max(w.y1, dst.bounds.y1),
         std::min(w.x2, dst.bounds.x2), std::min(w.y2, dst.bounds.y2)};
    for (int y = w.y1; y < w.y2; ++y) {
        if (abort && abort())
            return;
        for (int x = w.x1; x < w.x2; ++x) {
            auto in = pixel(src, x, y);
            if (!in) {
                writePixel(dst, x, y, {0, 0, 0, 0});
                continue;
            }
            auto v = readPixel(src, x, y);
            auto c = process({v[0], v[1], v[2]}, v[3], p);
            writePixel(dst, x, y, {c.r, c.g, c.b, v[3]});
            if (src.components == 4 && dst.components == 4 && src.depth == dst.depth)
                std::memcpy(pixel(dst, x, y) + 3 * bytesPerChannel(dst.depth),
                            in + 3 * bytesPerChannel(src.depth), bytesPerChannel(src.depth));
        }
    }
}
void renderCPU(const Image &s, const Image &d, Rect w, const KernelParams &p, unsigned threads,
               const std::function<bool()> &abort) {
    if (!valid(d) || (!valid(s) && s.data))
        throw std::invalid_argument("Malformed image");
    w.y1 = std::max(w.y1, d.bounds.y1);
    w.y2 = std::min(w.y2, d.bounds.y2);
    if (w.y2 <= w.y1)
        return;
    if (!threads)
        threads = std::thread::hardware_concurrency();
    threads = std::clamp(threads, 1u, unsigned(std::max(1, (w.y2 - w.y1) / 16)));
    std::vector<std::future<void>> jobs;
    for (unsigned i = 1; i < threads; ++i) {
        Rect r = w;
        r.y1 = w.y1 + (w.y2 - w.y1) * i / threads;
        r.y2 = w.y1 + (w.y2 - w.y1) * (i + 1) / threads;
        jobs.push_back(std::async(std::launch::async, [&, r] { renderRows(s, d, r, p, abort); }));
    }
    Rect first = w;
    first.y2 = w.y1 + (w.y2 - w.y1) / threads;
    renderRows(s, d, first, p, abort);
    for (auto &j : jobs)
        j.get();
}
std::vector<V3> sampleImage(const Image &s, int space, bool premult, std::size_t budget) {
    std::vector<V3> v;
    if (!valid(s))
        return v;
    int step = std::max(1, int(std::ceil(std::sqrt(double(s.bounds.x2 - s.bounds.x1) *
                                                   (s.bounds.y2 - s.bounds.y1) / double(budget)))));
    for (int y = s.bounds.y1; y < s.bounds.y2; y += step)
        for (int x = s.bounds.x1; x < s.bounds.x2; x += step) {
            auto c = readPixel(s, x, y);
            if (c[3] <= .01 || !std::isfinite(c[0]) || !std::isfinite(c[1]) || !std::isfinite(c[2]))
                continue;
            V3 a{c[0], c[1], c[2]};
            if (premult)
                a = mul(a, 1 / c[3]);
            v.push_back(decode3(a, space));
        }
    return v;
}
} // namespace renzu
