// SPDX-License-Identifier: GPL-3.0-only
#include "renzu/Kernel.h"
using namespace renzu;
float readValue(const device uchar *p, int depth) {
    if (depth == 0)
        return float(*p) / 255.f;
    if (depth == 1)
        return float(*reinterpret_cast<const device ushort *>(p)) / 65535.f;
    if (depth == 2)
        return float(*reinterpret_cast<const device half *>(p));
    return *reinterpret_cast<const device float *>(p);
}
void writeValue(device uchar *p, int depth, float v) {
    if (depth == 0)
        *p = uchar(round(clamp(safe(v), 0.f, 1.f) * 255.f));
    else if (depth == 1)
        *reinterpret_cast<device ushort *>(p) = ushort(round(clamp(safe(v), 0.f, 1.f) * 65535.f));
    else if (depth == 2)
        *reinterpret_cast<device half *>(p) = half(clamp(safe(v), -65504.f, 65504.f));
    else
        *reinterpret_cast<device float *>(p) = v;
}
int channelBytes(int d) {
    return d == 0 ? 1 : (d == 3 ? 4 : 2);
}
kernel void gradeBuffers(const device uchar *src [[buffer(0)]], device uchar *dst [[buffer(1)]],
                         constant Dispatch &d [[buffer(2)]], constant KernelParams &p [[buffer(3)]],
                         uint2 gid [[thread_position_in_grid]]) {
    int x = d.x1 + int(gid.x), y = d.y1 + int(gid.y);
    if (x >= d.x2 || y >= d.y2)
        return;
    if (x < d.dst.x1 || x >= d.dst.x2 || y < d.dst.y1 || y >= d.dst.y2)
        return;
    int db = channelBytes(d.dst.depth), sb = channelBytes(d.src.depth);
    long doff = long(y - d.dst.y1) * d.dst.rowBytes + long(x - d.dst.x1) * d.dst.components * db;
    // Negative strides: Metal buffer offsets begin at physical allocation start.
    if (d.dst.rowBytes < 0)
        doff += long(d.dst.y2 - d.dst.y1 - 1) * (-d.dst.rowBytes);
    device uchar *out = dst + doff;
    if (x < d.src.x1 || x >= d.src.x2 || y < d.src.y1 || y >= d.src.y2) {
        for (int c = 0; c < d.dst.components; ++c)
            writeValue(out + c * db, d.dst.depth, 0);
        return;
    }
    long soff = long(y - d.src.y1) * d.src.rowBytes + long(x - d.src.x1) * d.src.components * sb;
    if (d.src.rowBytes < 0)
        soff += long(d.src.y2 - d.src.y1 - 1) * (-d.src.rowBytes);
    const device uchar *in = src + soff;
    float alpha = d.src.components == 4 ? readValue(in + 3 * sb, d.src.depth) : 1;
    V3 c = process({readValue(in, d.src.depth), readValue(in + sb, d.src.depth),
                    readValue(in + 2 * sb, d.src.depth)},
                   alpha, p);
    writeValue(out, d.dst.depth, c.r);
    writeValue(out + db, d.dst.depth, c.g);
    writeValue(out + 2 * db, d.dst.depth, c.b);
    if (d.dst.components == 4) {
        if (d.src.components == 4 && sb == db && d.src.depth == d.dst.depth)
            for (int k = 0; k < db; ++k)
                out[3 * db + k] = in[3 * sb + k];
        else
            writeValue(out + 3 * db, d.dst.depth, alpha);
    }
}
kernel void gradeTextures(texture2d<float, access::read> src [[texture(0)]],
                          texture2d<float, access::write> dst [[texture(1)]],
                          constant Dispatch &d [[buffer(2)]],
                          constant KernelParams &p [[buffer(3)]],
                          uint2 gid [[thread_position_in_grid]]) {
    int x = d.x1 + int(gid.x), y = d.y1 + int(gid.y);
    if (x >= d.x2 || y >= d.y2 || x < d.dst.x1 || y < d.dst.y1 || x >= d.dst.x2 || y >= d.dst.y2)
        return;
    float4 v = 0;
    if (x >= d.src.x1 && y >= d.src.y1 && x < d.src.x2 && y < d.src.y2) {
        v = src.read(uint2(x - d.src.x1, y - d.src.y1));
        V3 c = process({v.r, v.g, v.b}, v.a, p);
        v.rgb = float3(c.r, c.g, c.b);
        if (d.dst.depth == 2)
            v.rgb = clamp(v.rgb, float3(-65504.f), float3(65504.f));
    }
    dst.write(v, uint2(x - d.dst.x1, y - d.dst.y1));
}
