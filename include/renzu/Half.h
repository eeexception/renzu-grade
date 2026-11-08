// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <cstdint>
#include <cstring>
#include <limits>

namespace renzu {
static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559,
              "Half conversion requires IEEE binary32");

// IEEE binary16 storage conversions, including subnormals and round-to-nearest,
// ties-to-even. MSVC has no _Float16 type; use the same code on every CPU.
inline float halfToFloat(std::uint16_t h) {
    std::uint32_t sign = std::uint32_t(h & 0x8000u) << 16;
    std::uint32_t exponent = (h >> 10) & 31u, fraction = h & 1023u;
    std::uint32_t bits;
    if (exponent == 0) {
        if (fraction == 0)
            bits = sign;
        else {
            int e = 113;
            while (!(fraction & 1024u)) {
                fraction <<= 1;
                --e;
            }
            bits = sign | (std::uint32_t(e) << 23) | ((fraction & 1023u) << 13);
        }
    } else if (exponent == 31)
        bits = sign | 0x7f800000u | (fraction << 13);
    else
        bits = sign | ((exponent + 112u) << 23) | (fraction << 13);
    float result;
    std::memcpy(&result, &bits, sizeof(result));
    return result;
}

inline std::uint16_t floatToHalf(float value) {
    std::uint32_t bits;
    std::memcpy(&bits, &value, sizeof(bits));
    auto sign = std::uint16_t((bits >> 16) & 0x8000u);
    std::uint32_t exponent = (bits >> 23) & 255u, fraction = bits & 0x7fffffu;
    if (exponent == 255)
        return std::uint16_t(sign | 0x7c00u | (fraction ? (fraction >> 13) | 0x0200u : 0));
    int e = int(exponent) - 127;
    if (e > 15)
        return std::uint16_t(sign | 0x7c00u);
    if (e < -25)
        return sign;
    unsigned shift = e < -14 ? unsigned(-e - 1) : 13u;
    std::uint32_t mantissa = e < -14 ? fraction | 0x800000u : fraction;
    std::uint32_t result = e < -14 ? 0u : std::uint32_t(e + 15) << 10;
    result |= mantissa >> shift;
    std::uint32_t remainder = mantissa & ((1u << shift) - 1u), midpoint = 1u << (shift - 1);
    if (remainder > midpoint || (remainder == midpoint && (result & 1u)))
        ++result;
    return std::uint16_t(sign | result);
}
} // namespace renzu
