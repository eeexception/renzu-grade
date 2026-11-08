// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "Engine.h"
namespace renzu {
// Opaque pointers are borrowed Objective-C objects; MetalBackend retains no host images.
bool renderMetal(void *queue, void *source, void *destination, const Dispatch &,
                 const KernelParams &, bool textures, const std::string &library,
                 std::string &error, bool wait = false);
bool downloadMetal(void *queue, void *source, const ImageLayout &, bool texture,
                   std::vector<unsigned char> &bytes, std::string &error);
} // namespace renzu
