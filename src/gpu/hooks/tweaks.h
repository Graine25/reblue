/**
 * @file    gpu/hooks/tweaks.h
 * @brief   Shared policy behind the cvar-gated rendering tweaks in tweaks.cpp.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 */
#pragma once

#include <cstddef>

#include <rex/types.h>

namespace bd::gpu {

// A water object's planar reflection record, sized and rebuilt by
// bdPlaneReflectUpdateTexture. Only the fields that hook reads are named.
struct PlaneReflectInfo {
  be_u32 pad_00[3];
  be_u32 texture; // +0x0C  resolve destination
  be_u32 pad_10[37];
  be_f32 scale;  // +0xA4  quality multiplier over the 320-wide base
  be_u32 width;  // +0xA8  clamped to [128, 1280]
  be_u32 height; // +0xAC  9/16 of width
  be_u32 pad_B0[25];
  be_f32 lastScale; // +0x114 scale the current texture was built for
};
static_assert(offsetof(PlaneReflectInfo, texture) == 0x0C);
static_assert(offsetof(PlaneReflectInfo, scale) == 0xA4);
static_assert(offsetof(PlaneReflectInfo, width) == 0xA8);
static_assert(offsetof(PlaneReflectInfo, height) == 0xAC);
static_assert(offsetof(PlaneReflectInfo, lastScale) == 0x114);

// Sun shadow coverage multiplier in force right now. The light frustum hook and
// the PCF kernel compensation must read the same value or the kernel shrink
// stops matching the coverage box.
f64 ShadowCoverageScale();

} // namespace bd::gpu
