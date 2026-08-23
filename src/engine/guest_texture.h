/**
 * @file    engine/guest_texture.h
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#pragma once

#include <vector>

#include <rex/types.h>

namespace bd::engine {

// Straight RGBA in, out the layout bdAllocRenderBuffer (0x82285740) reads for a
// texture file that does not begin with 'DDS': a big-endian payload size, the
// 52-byte D3DTexture it memcpys into its own struct, zero padding out to 2048,
// then the tiled texels it copies into an XPhysicalAllocEx block.
//
// The result is k_8_8_8_8 whatever the file it stands in for shipped as, which
// costs four bytes a texel against DXT but needs no encoder, so a texture whose
// content depends on runtime state can be composed where it is served.
std::vector<u8> BuildGuestTexture(const std::vector<u8> &rgba, u32 width,
                                  u32 height);

} // namespace bd::engine
