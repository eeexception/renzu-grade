// SPDX-License-Identifier: GPL-3.0-only
#include "renzu/Metal.h"
#include "renzu/Photographic.h"
#import <Metal/Metal.h>
#include <cstring>
#include <iostream>
#include <random>
using namespace renzu;
int main(int argc, char **argv) {
    @autoreleasepool {
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        if (!device) {
            std::cout << "SKIP: Metal GPU unavailable\n";
            return 77;
        }
        if (argc != 2)
            return 1;
        id<MTLCommandQueue> queue = [device newCommandQueue];
        std::mt19937 rng(42);
        std::uniform_real_distribution<float> random(-.03, 1.5);
        int assertions = 0;
        float worst = 0, worstNormalized = 0;
        for (int space = 0; space < 3; ++space)
            for (int mode = 0; mode < 4; ++mode)
                for (auto depth : {Depth::Byte, Depth::Short, Depth::Half, Depth::Float})
                    for (int channels : {3, 4}) {
                        int width = 67, height = 43,
                            row = width * channels * bytesPerChannel(depth) + 32;
                        std::vector<unsigned char> input(row * height, 0), cpu(row * height, 0),
                            gpu(row * height, 0);
                        Image a{input.data(), {-4, 7, width - 4, height + 7}, row, depth, channels},
                            b{cpu.data(), a.bounds, row, depth, channels};
                        for (int y = 7; y < height + 7; ++y)
                            for (int x = -4; x < width - 4; ++x)
                                writePixel(a, x, y, {random(rng), random(rng), random(rng), .37f});
                        Settings s;
                        s.space = space;
                        if (mode) {
                            s.exposure = .73;
                            s.shadows = -.33;
                            s.midtones = .18;
                            s.lights = .2;
                            s.highlights = -.4;
                            s.contrast = 1.18;
                            s.pivot = .21;
                            s.temperature = 23;
                            s.tint = -17;
                            s.saturation = 1.2;
                            s.vibrance = 31;
                            s.premultiplied = mode == 3;
                        }
                        if (mode == 2) {
                            a.data = input.data() + (height - 1) * row;
                            a.rowBytes = -row;
                            b.data = cpu.data() + (height - 1) * row;
                            b.rowBytes = -row;
                        }
                        auto p = prepare(s);
                        renzu::Rect window{-2, 9, width - 6, height + 5};
                        renderCPU(a, b, window, p, 2);
                        id<MTLBuffer> src =
                            [device newBufferWithBytes:input.data()
                                                length:input.size()
                                               options:MTLResourceStorageModeShared];
                        id<MTLBuffer> dst =
                            [device newBufferWithBytes:gpu.data()
                                                length:gpu.size()
                                               options:MTLResourceStorageModeShared];
                        if (mode == 3) {
                            // A host is allowed to provide GPU-only storage. Upload through
                            // the same queue, then run the unmodified production backend.
                            id<MTLBuffer> privateSrc =
                                [device newBufferWithLength:input.size()
                                                    options:MTLResourceStorageModePrivate];
                            id<MTLBuffer> privateDst =
                                [device newBufferWithLength:gpu.size()
                                                    options:MTLResourceStorageModePrivate];
                            id<MTLCommandBuffer> upload = [queue commandBuffer];
                            id<MTLBlitCommandEncoder> blit = [upload blitCommandEncoder];
                            [blit copyFromBuffer:src
                                     sourceOffset:0
                                         toBuffer:privateSrc
                                destinationOffset:0
                                             size:input.size()];
                            [blit copyFromBuffer:dst
                                     sourceOffset:0
                                         toBuffer:privateDst
                                destinationOffset:0
                                             size:gpu.size()];
                            [blit endEncoding];
                            [upload commit];
                            src = privateSrc;
                            dst = privateDst;
                        }
                        Dispatch d{
                            {-4, 7, width - 4, height + 7, int(a.rowBytes), int(depth), channels},
                            {-4, 7, width - 4, height + 7, int(b.rowBytes), int(depth), channels},
                            window.x1,
                            window.y1,
                            window.x2,
                            window.y2};
                        std::string error;
                        if (!renderMetal((__bridge void *)queue, (__bridge void *)src,
                                         (__bridge void *)dst, d, p, false, argv[1], error, true)) {
                            std::cerr << error << '\n';
                            return 1;
                        }
                        if (mode == 3) {
                            if (!downloadMetal((__bridge void *)queue, (__bridge void *)dst, d.dst,
                                               false, gpu, error)) {
                                std::cerr << error << '\n';
                                return 1;
                            }
                        } else
                            std::memcpy(gpu.data(), dst.contents, gpu.size());
                        Image g{gpu.data(), a.bounds, row, depth, channels};
                        if (mode == 2) {
                            g.data = gpu.data() + (height - 1) * row;
                            g.rowBytes = -row;
                        }
                        float tol = depth == Depth::Byte    ? 1.01f / 255
                                    : depth == Depth::Short ? 8.01f / 65535
                                    : depth == Depth::Half  ? .015f
                                                            : .003f;
                        for (int y = window.y1; y < window.y2; ++y)
                            for (int x = window.x1; x < window.x2; ++x) {
                                auto c = readPixel(b, x, y), v = readPixel(g, x, y);
                                for (int k = 0; k < 3; ++k) {
                                    float delta = std::abs(c[k] - v[k]);
                                    worst = std::max(worst, delta);
                                    worstNormalized = std::max(
                                        worstNormalized,
                                        delta /
                                            (tol + std::abs(c[k]) *
                                                       (depth == Depth::Half ? 1.1e-3f : 2.e-4f)));
                                    if (delta >
                                        tol + std::abs(c[k]) *
                                                  (depth == Depth::Half ? 1.1e-3f : 2.e-4f)) {
                                        std::cerr << "Parity failure space=" << space
                                                  << " mode=" << mode << " depth=" << int(depth)
                                                  << " CPU=" << c[k] << " GPU=" << v[k] << '\n';
                                        return 1;
                                    }
                                    ++assertions;
                                }
                                if (c[3] != v[3])
                                    return 1;
                            }
                    }
        // Kelvin-driven adaptation reaches beyond the old normalized UI limits.
        for (int space = 0; space < 3; ++space)
            for (double kelvin : {2000., 2850., 6504., 25000.}) {
                constexpr int w = 16, h = 16;
                std::vector<float> in(w * h * 4), expected(in.size()), actual(in.size());
                for (int j = 0; j < w * h; ++j) {
                    in[j * 4] = encode(.04f + .25f * (j % w) / w, space);
                    in[j * 4 + 1] = encode(.08f + .15f * (j / w) / h, space);
                    in[j * 4 + 2] = encode(.12f, space);
                    in[j * 4 + 3] = .75f;
                }
                Settings settings;
                settings.space = space;
                auto wb = whiteBalanceFromKelvin(kelvin, 20);
                settings.temperature = wb[0];
                settings.tint = wb[1];
                settings.exposure = .4;
                settings.contrast = 1.1;
                settings.saturation = 1.1;
                settings.vibrance = 15;
                auto params = prepare(settings);
                Image source{in.data(), {0, 0, w, h}, w * 16, Depth::Float, 4},
                    target{expected.data(), source.bounds, w * 16, Depth::Float, 4};
                renderCPU(source, target, source.bounds, params, 2);
                id<MTLBuffer> src = [device newBufferWithBytes:in.data()
                                                        length:in.size() * 4
                                                       options:MTLResourceStorageModeShared];
                id<MTLBuffer> dst = [device newBufferWithLength:actual.size() * 4
                                                        options:MTLResourceStorageModeShared];
                Dispatch dispatch{
                    {0, 0, w, h, w * 16, 3, 4}, {0, 0, w, h, w * 16, 3, 4}, 0, 0, w, h};
                std::string error;
                if (!renderMetal((__bridge void *)queue, (__bridge void *)src, (__bridge void *)dst,
                                 dispatch, params, false, argv[1], error, true)) {
                    std::cerr << error;
                    return 1;
                }
                std::memcpy(actual.data(), dst.contents, actual.size() * 4);
                for (std::size_t k = 0; k < actual.size(); ++k) {
                    if (std::abs(actual[k] - expected[k]) >
                        2.e-4f + std::abs(expected[k]) * 2.e-4f) {
                        std::cerr << "Kelvin GPU parity failure\n";
                        return 1;
                    }
                    ++assertions;
                }
            }
        // Execute the actual texture kernel too.
        int width = 31, height = 19;
        std::vector<float> src(width * height * 4), out(src.size()), expected(src.size());
        for (auto &v : src)
            v = random(rng);
        for (std::size_t i = 3; i < src.size(); i += 4)
            src[i] = .73f;
        MTLTextureDescriptor *desc =
            [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA32Float
                                                               width:width
                                                              height:height
                                                           mipmapped:NO];
        desc.storageMode = MTLStorageModeShared;
        desc.usage = MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite;
        id<MTLTexture> a = [device newTextureWithDescriptor:desc],
                       b = [device newTextureWithDescriptor:desc];
        [a replaceRegion:MTLRegionMake2D(0, 0, width, height)
             mipmapLevel:0
               withBytes:src.data()
             bytesPerRow:width * 16];
        Settings s;
        s.space = 2;
        s.exposure = .7;
        s.temperature = -30;
        s.vibrance = 33;
        auto p = prepare(s);
        Dispatch d{
            {0, 0, width, height, 0, 3, 4}, {0, 0, width, height, 0, 3, 4}, 0, 0, width, height};
        std::string error;
        if (!renderMetal((__bridge void *)queue, (__bridge void *)a, (__bridge void *)b, d, p, true,
                         argv[1], error, true)) {
            std::cerr << error;
            return 1;
        }
        [b getBytes:out.data()
            bytesPerRow:width * 16
             fromRegion:MTLRegionMake2D(0, 0, width, height)
            mipmapLevel:0];
        Image i{src.data(), {0, 0, width, height}, width * 16, Depth::Float, 4},
            o{expected.data(), i.bounds, width * 16, Depth::Float, 4};
        renderCPU(i, o, i.bounds, p, 2);
        for (std::size_t k = 0; k < out.size(); ++k) {
            if (std::abs(out[k] - expected[k]) > 1.e-4f + std::abs(expected[k]) * 2.e-4f)
                return 1;
            ++assertions;
        }
        std::vector<unsigned char> download;
        if (!downloadMetal((__bridge void *)queue, (__bridge void *)a, d.src, true, download,
                           error) ||
            std::memcmp(download.data(), src.data(), download.size()))
            return 1;
        // Validate all four negotiated texture storage formats and all encodings.
        const MTLPixelFormat formats[] = {MTLPixelFormatRGBA8Unorm, MTLPixelFormatRGBA16Unorm,
                                          MTLPixelFormatRGBA16Float, MTLPixelFormatRGBA32Float};
        for (int space = 0; space < 3; ++space)
            for (int depth = 0; depth < 4; ++depth) {
                int row = width * 4 * bytesPerChannel(Depth(depth));
                std::vector<unsigned char> input(row * height), reference(row * height),
                    result(row * height);
                Image source{input.data(), {0, 0, width, height}, row, Depth(depth), 4};
                Image target{reference.data(), source.bounds, row, Depth(depth), 4};
                for (int y = 0; y < height; ++y)
                    for (int x = 0; x < width; ++x)
                        writePixel(source, x, y,
                                   {.05f + .4f * x / width, .12f + .2f * y / height, .23f, .73f});
                Settings settings;
                settings.space = space;
                settings.exposure = .4;
                settings.shadows = .2;
                settings.highlights = -.3;
                settings.temperature = 19;
                settings.tint = -8;
                settings.vibrance = 33;
                auto params = prepare(settings);
                renderCPU(source, target, source.bounds, params, 2);
                desc.pixelFormat = formats[depth];
                id<MTLTexture> inputTexture = [device newTextureWithDescriptor:desc];
                id<MTLTexture> outputTexture = [device newTextureWithDescriptor:desc];
                [inputTexture replaceRegion:MTLRegionMake2D(0, 0, width, height)
                                mipmapLevel:0
                                  withBytes:input.data()
                                bytesPerRow:row];
                Dispatch dispatch{{0, 0, width, height, 0, depth, 4},
                                  {0, 0, width, height, 0, depth, 4},
                                  0,
                                  0,
                                  width,
                                  height};
                if (!renderMetal((__bridge void *)queue, (__bridge void *)inputTexture,
                                 (__bridge void *)outputTexture, dispatch, params, true, argv[1],
                                 error, true)) {
                    std::cerr << error;
                    return 1;
                }
                [outputTexture getBytes:result.data()
                            bytesPerRow:row
                             fromRegion:MTLRegionMake2D(0, 0, width, height)
                            mipmapLevel:0];
                Image actual{result.data(), source.bounds, row, Depth(depth), 4};
                float tolerance = depth == 0   ? 1.01f / 255
                                  : depth == 1 ? 8.01f / 65535
                                  : depth == 2 ? .002f
                                               : 1.e-4f;
                for (int y = 0; y < height; ++y)
                    for (int x = 0; x < width; ++x) {
                        auto expectedPixel = readPixel(target, x, y),
                             actualPixel = readPixel(actual, x, y);
                        for (int c = 0; c < 4; ++c) {
                            if (std::abs(expectedPixel[c] - actualPixel[c]) >
                                tolerance + std::abs(expectedPixel[c]) * 1.e-4f) {
                                std::cerr << "Texture storage parity failure depth=" << depth
                                          << " space=" << space << '\n';
                                return 1;
                            }
                            ++assertions;
                        }
                    }
                auto invalid = dispatch;
                invalid.src.depth = (depth + 1) % 4;
                if (renderMetal((__bridge void *)queue, (__bridge void *)inputTexture,
                                (__bridge void *)outputTexture, invalid, params, true, argv[1],
                                error, true))
                    return 1;
                ++assertions;
            }
        std::cout
            << "PASS: " << assertions
            << " real Metal buffer/texture parity assertions; worst absolute buffer difference="
            << worst << "; maximum fraction of allowed error=" << worstNormalized << " on "
            << device.name.UTF8String << '\n';
        return 0;
    }
}
