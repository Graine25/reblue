/**
 * @file    gpu/screenshot.h
 * @brief   One-shot capture of the presented game frame (pre-overlay) to PNG.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#pragma once

#include <rex/types.h>
#include <vector>

namespace plume {
struct RenderTexture;
struct RenderFramebuffer;
} // namespace plume

namespace bd::gpu {
struct VideoState;
} // namespace bd::gpu

namespace bd::gpu {

// Tightly packed RGBA8 image (alpha forced opaque). Empty == capture failed.
struct Capture {
  u32 width = 0;
  u32 height = 0;
  std::vector<u8> rgba; // width*height*4, row-major, no padding
};

// UI thread: latch a one-shot capture of the next presented frame.
void RequestScreenshot();

// UI thread: true once a capture (or a failure result) is available.
bool ReadyScreenshot();

// UI thread: move out the latest capture and clear the ready state.
// Returns an empty Capture if the capture failed or none is ready.
Capture TakeScreenshot();

// UI thread: abandon any pending/ready capture (call when the dialog closes).
void CancelScreenshot();

// UI thread: encode an RGBA capture to PNG bytes via miniz. Empty on failure.
std::vector<u8> EncodePng(const Capture &c);

// Render thread: called once per Present, after the gamma pass and before the
// ImGui overlay, with the swapchain back texture (COLOR_WRITE) and its bound
// framebuffer. No-op unless a request is latched or a prior copy is ready to
// map. When it records a copy it briefly unbinds the framebuffer for the copy
// and rebinds back_fb so the overlay draws normally.
void ServiceOnPresent(VideoState &s, plume::RenderTexture *back,
                      plume::RenderFramebuffer *back_fb, u32 width, u32 height);

} // namespace bd::gpu
