/**
 * @file    gpu/gpu_timing.h
 * @brief   GPU timestamp attribution: per-frame draw vs inter-draw GPU time
 *          via plume query pools, aggregated into the [perf] log line.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#pragma once

#include <rex/types.h>

namespace plume {
struct RenderCommandList;
struct RenderDevice;
} // namespace plume

namespace bd::gpu {

// All entry points require the caller to hold the video state mutex (single
// recording thread). No-ops when the backend lacks queryPools.

// Reset + frame start timestamp. Call once per command list, after begin().
void FrameBegin(plume::RenderDevice *device, plume::RenderCommandList *cmd,
                u32 slot);

// Category transitions: a timestamp is written only when the category changes,
// closing the previous segment. MarkDraw is safe to call per draw.
// MarkResolve covers the EDRAM resolve/copy paths (their barriers, copies and
// fullscreen blit draws), and MarkInter is every other pass-breaking event.
void MarkDraw(plume::RenderCommandList *cmd);
void MarkInter(plume::RenderCommandList *cmd);
void MarkResolve(plume::RenderCommandList *cmd);

// Close the last segment. Call right before cmd->end().
void FrameEnd(plume::RenderCommandList *cmd);

// Read back + aggregate the slot's results. Call after its fence signalled.
void CollectGPUTimings(u32 slot);

} // namespace bd::gpu
