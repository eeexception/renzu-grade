// SPDX-License-Identifier: GPL-3.0-only
#include "renzu/Metal.h"
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <cstring>
#include <map>
#include <mutex>
namespace renzu {
struct Pipelines {
    id<MTLComputePipelineState> buffers;
    id<MTLComputePipelineState> textures;
};
static std::mutex cacheMutex;
static std::map<std::pair<std::uint64_t, std::string>, Pipelines> cache;
static bool pipelines(id<MTLDevice> device, const std::string &path, Pipelines &out,
                      std::string &error) {
    std::lock_guard<std::mutex> lock(cacheMutex);
    auto key = std::make_pair(device.registryID, path);
    auto it = cache.find(key);
    if (it != cache.end()) {
        out = it->second;
        return true;
    }
    NSError *e = nil;
    id<MTLLibrary> lib = [device
        newLibraryWithURL:[NSURL fileURLWithPath:[NSString stringWithUTF8String:path.c_str()]]
                    error:&e];
    if (!lib) {
        error = e.localizedDescription.UTF8String ?: "Unable to load metallib";
        return false;
    }
    out.buffers =
        [device newComputePipelineStateWithFunction:[lib newFunctionWithName:@"gradeBuffers"]
                                              error:&e];
    out.textures =
        [device newComputePipelineStateWithFunction:[lib newFunctionWithName:@"gradeTextures"]
                                              error:&e];
    if (!out.buffers || !out.textures) {
        error = e.localizedDescription.UTF8String ?: "Unable to create Metal pipelines";
        return false;
    }
    cache.emplace(key, out);
    return true;
}
static bool bufferFits(id<MTLBuffer> b, const ImageLayout &i) {
    auto row = (std::int64_t(i.x2) - i.x1) * i.components * bytesPerChannel(Depth(i.depth));
    auto height = std::int64_t(i.y2) - i.y1;
    return b && (i.components == 3 || i.components == 4) && row > 0 && height > 0 &&
           std::abs(std::int64_t(i.rowBytes)) >= row &&
           std::uint64_t((height - 1) * std::abs(std::int64_t(i.rowBytes)) + row) <= b.length;
}
static bool textureFits(id<MTLTexture> t, const ImageLayout &i, id<MTLDevice> device) {
    const MTLPixelFormat formats[] = {MTLPixelFormatRGBA8Unorm, MTLPixelFormatRGBA16Unorm,
                                      MTLPixelFormatRGBA16Float, MTLPixelFormatRGBA32Float};
    return t && i.depth >= 0 && i.depth < 4 && i.components == 4 && t.device == device &&
           t.textureType == MTLTextureType2D && t.pixelFormat == formats[i.depth] && i.x2 > i.x1 &&
           i.y2 > i.y1 && t.width == NSUInteger(std::int64_t(i.x2) - i.x1) &&
           t.height == NSUInteger(std::int64_t(i.y2) - i.y1);
}
bool renderMetal(void *q, void *s, void *d, const Dispatch &layout, const KernelParams &p,
                 bool textures, const std::string &path, std::string &error, bool wait) {
    @autoreleasepool {
        id<MTLCommandQueue> queue = (__bridge id<MTLCommandQueue>)q;
        if (!queue || !s || !d) {
            error = "Missing Metal queue or image";
            return false;
        }
        if (layout.x2 <= layout.x1 || layout.y2 <= layout.y1)
            return true;
        if (textures) {
            id<MTLTexture> a = (__bridge id<MTLTexture>)s, b = (__bridge id<MTLTexture>)d;
            if (!textureFits(a, layout.src, queue.device) ||
                !textureFits(b, layout.dst, queue.device) ||
                (a.usage != MTLTextureUsageUnknown && !(a.usage & MTLTextureUsageShaderRead)) ||
                (b.usage != MTLTextureUsageUnknown && !(b.usage & MTLTextureUsageShaderWrite))) {
                error = "Invalid Metal texture layout";
                return false;
            }
        } else if (!bufferFits((__bridge id<MTLBuffer>)s, layout.src) ||
                   !bufferFits((__bridge id<MTLBuffer>)d, layout.dst) ||
                   ((__bridge id<MTLBuffer>)s).device != queue.device ||
                   ((__bridge id<MTLBuffer>)d).device != queue.device) {
            error = "Metal buffer is smaller than image layout";
            return false;
        }
        Pipelines ps;
        if (!pipelines(queue.device, path, ps, error))
            return false;
        auto pipeline = textures ? ps.textures : ps.buffers;
        id<MTLCommandBuffer> cmd = [queue commandBuffer];
        id<MTLComputeCommandEncoder> enc = [cmd computeCommandEncoder];
        if (!cmd || !enc) {
            error = "Unable to allocate Metal command encoder";
            return false;
        }
        [enc setComputePipelineState:pipeline];
        if (textures) {
            [enc setTexture:(__bridge id<MTLTexture>)s atIndex:0];
            [enc setTexture:(__bridge id<MTLTexture>)d atIndex:1];
        } else {
            [enc setBuffer:(__bridge id<MTLBuffer>)s offset:0 atIndex:0];
            [enc setBuffer:(__bridge id<MTLBuffer>)d offset:0 atIndex:1];
        }
        [enc setBytes:&layout length:sizeof(layout) atIndex:2];
        [enc setBytes:&p length:sizeof(p) atIndex:3];
        NSUInteger tw = pipeline.threadExecutionWidth,
                   th = std::min(NSUInteger(8), pipeline.maxTotalThreadsPerThreadgroup / tw);
        [enc dispatchThreads:MTLSizeMake(layout.x2 - layout.x1, layout.y2 - layout.y1, 1)
            threadsPerThreadgroup:MTLSizeMake(tw, th, 1)];
        [enc endEncoding];
        [cmd commit];
        if (wait) {
            [cmd waitUntilCompleted];
            if (cmd.status == MTLCommandBufferStatusError) {
                error = cmd.error.localizedDescription.UTF8String;
                return false;
            }
        }
        return true;
    }
}
bool downloadMetal(void *q, void *s, const ImageLayout &i, bool texture,
                   std::vector<unsigned char> &bytes, std::string &error) {
    @autoreleasepool {
        id<MTLCommandQueue> queue = (__bridge id<MTLCommandQueue>)q;
        if (!queue || !s) {
            error = "No Metal queue for analysis capture";
            return false;
        }
        if (texture && !textureFits((__bridge id<MTLTexture>)s, i, queue.device)) {
            error = "Invalid capture texture";
            return false;
        }
        auto w = i.x2 - i.x1, h = i.y2 - i.y1;
        if (w <= 0 || h <= 0) {
            error = "Empty capture";
            return false;
        }
        std::size_t row = texture ? std::size_t(w) * i.components * bytesPerChannel(Depth(i.depth))
                                  : std::abs(std::int64_t(i.rowBytes));
        std::size_t aligned = texture ? (row + 255) / 256 * 256 : row;
        id<MTLBuffer> staging = [queue.device newBufferWithLength:aligned * h
                                                          options:MTLResourceStorageModeShared];
        if (!staging) {
            error = "Capture allocation failed";
            return false;
        }
        if (!texture && !bufferFits((__bridge id<MTLBuffer>)s, i)) {
            error = "Invalid capture buffer";
            return false;
        }
        id<MTLCommandBuffer> cmd = [queue commandBuffer];
        id<MTLBlitCommandEncoder> blit = [cmd blitCommandEncoder];
        if (texture)
            [blit copyFromTexture:(__bridge id<MTLTexture>)s
                             sourceSlice:0
                             sourceLevel:0
                            sourceOrigin:MTLOriginMake(0, 0, 0)
                              sourceSize:MTLSizeMake(w, h, 1)
                                toBuffer:staging
                       destinationOffset:0
                  destinationBytesPerRow:aligned
                destinationBytesPerImage:aligned * h];
        else
            [blit copyFromBuffer:(__bridge id<MTLBuffer>)s
                     sourceOffset:0
                         toBuffer:staging
                destinationOffset:0
                             size:row * (h - 1) +
                                  w * i.components * bytesPerChannel(Depth(i.depth))];
        [blit endEncoding];
        [cmd commit];
        [cmd waitUntilCompleted];
        if (cmd.status == MTLCommandBufferStatusError) {
            error = cmd.error.localizedDescription.UTF8String;
            return false;
        }
        bytes.resize(row * h);
        for (int y = 0; y < h; ++y)
            std::memcpy(bytes.data() + y * row,
                        static_cast<const char *>(staging.contents) + y * aligned, row);
        return true;
    }
}
} // namespace renzu
