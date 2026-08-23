/**
 * @file    gpu/screenshot.cpp
 * @brief   Copies the post-gamma swapchain image to a readback buffer inside
 *          Present, repacks BGRA->RGBA, and encodes PNG via miniz.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#include "gpu/screenshot.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <string>

#define MINIZ_HEADER_FILE_ONLY
#include <miniz.h>

#include <rex/math.h>

#include "core/logging.h"
#include "gpu/device.h"

namespace bd::gpu {

namespace {

enum class State { Idle, Pending };

std::atomic<bool> g_requested{false}; // UI -> render: start a capture
std::atomic<bool> g_cancel{false};    // UI -> render: drop pending/ready
std::atomic<bool> g_ready{false};     // render -> UI: capture available

std::mutex g_mutex; // guards g_capture
Capture g_capture;  // published result

// Render thread only state.
State g_state = State::Idle;
std::unique_ptr<plume::RenderBuffer> g_readback;
u32 g_rb_w = 0, g_rb_h = 0;
u32 g_cap_w = 0, g_cap_h = 0, g_cap_pitch = 0; // pitch in bytes
u64 g_present_index = 0;
u64 g_ready_at = 0;
// A cancel can idle the state machine while a recorded copy is still in
// flight, and a resized re-request would then destroy g_readback under the GPU.
// Retired buffers wait out their fence deadline here instead.
struct RetiredReadback {
  std::unique_ptr<plume::RenderBuffer> buffer;
  u64 safe_at;
};
std::vector<RetiredReadback> g_retired;

void PublishEmpty() {
  std::lock_guard<std::mutex> lk(g_mutex);
  g_capture = Capture{};
  g_ready.store(true, std::memory_order_release);
}

} // namespace

void RequestScreenshot() {
  g_cancel.store(true, std::memory_order_relaxed); // drop any stale capture
  g_requested.store(true, std::memory_order_relaxed);
}

bool ReadyScreenshot() { return g_ready.load(std::memory_order_acquire); }

Capture TakeScreenshot() {
  std::lock_guard<std::mutex> lk(g_mutex);
  Capture out = std::move(g_capture);
  g_capture = Capture{};
  g_ready.store(false, std::memory_order_release);
  return out;
}

void CancelScreenshot() {
  g_cancel.store(true, std::memory_order_relaxed);
  std::lock_guard<std::mutex> lk(g_mutex);
  g_capture = Capture{};
  g_ready.store(false, std::memory_order_release);
}

std::vector<u8> EncodePng(const Capture &c) {
  if (c.rgba.empty() || c.width == 0 || c.height == 0)
    return {};
  size_t len = 0;
  // bpl = bytes per line for the tightly packed RGBA buffer.
  void *png = tdefl_write_image_to_png_file_in_memory(
      c.rgba.data(), static_cast<int>(c.width), static_cast<int>(c.height), 4,
      static_cast<int>(c.width * 4), &len);
  if (!png) {
    BD_WARN("screenshot: PNG encode failed");
    return {};
  }
  std::vector<u8> out(static_cast<const u8 *>(png),
                      static_cast<const u8 *>(png) + len);
  mz_free(png);
  return out;
}

void ServiceOnPresent(VideoState &s, plume::RenderTexture *back,
                      plume::RenderFramebuffer *back_fb, u32 w, u32 h) {
  ++g_present_index;

  // Free retired readback buffers whose recorded copies have fenced out.
  std::erase_if(g_retired, [](const RetiredReadback &r) {
    return g_present_index >= r.safe_at;
  });

  // Cancellation drops any pending copy and clears a stale published result.
  if (g_cancel.exchange(false, std::memory_order_relaxed)) {
    g_state = State::Idle;
    std::lock_guard<std::mutex> lk(g_mutex);
    g_capture = Capture{};
    g_ready.store(false, std::memory_order_release);
  }

  // Map a previously recorded copy once its frame fence has been waited.
  if (g_state == State::Pending && g_present_index >= g_ready_at) {
    const u8 *mapped =
        g_readback ? static_cast<const u8 *>(g_readback->map()) : nullptr;
    if (mapped) {
      Capture cap;
      cap.width = g_cap_w;
      cap.height = g_cap_h;
      cap.rgba.resize(static_cast<size_t>(g_cap_w) * g_cap_h * 4);
      for (u32 y = 0; y < g_cap_h; ++y) {
        const u8 *src = mapped + static_cast<size_t>(y) * g_cap_pitch;
        u8 *dst = cap.rgba.data() + static_cast<size_t>(y) * g_cap_w * 4;
        for (u32 x = 0; x < g_cap_w; ++x) {
          dst[x * 4 + 0] = src[x * 4 + 2]; // R <- BGRA byte 2
          dst[x * 4 + 1] = src[x * 4 + 1]; // G
          dst[x * 4 + 2] = src[x * 4 + 0]; // B
          dst[x * 4 + 3] = 255;            // force opaque
        }
      }
      g_readback->unmap();
      {
        std::lock_guard<std::mutex> lk(g_mutex);
        g_capture = std::move(cap);
      }
      g_ready.store(true, std::memory_order_release);
      BD_INFO("screenshot: captured {}x{}", g_cap_w, g_cap_h);
    } else {
      BD_WARN("screenshot: readback map failed");
      PublishEmpty();
    }
    g_state = State::Idle;
  }

  // Record a new copy if a request is latched.
  if (g_state == State::Idle &&
      g_requested.exchange(false, std::memory_order_relaxed)) {
    if (!back || w == 0 || h == 0 || !s.device || !s.command_list) {
      PublishEmpty(); // cannot capture, let Submit proceed without a screenshot
      return;
    }
    const u32 pitch = rex::align(w * 4u, 256u); // D3D12 readback row pitch
    if (!g_readback || g_rb_w != w || g_rb_h != h) {
      if (g_readback && g_present_index < g_ready_at) {
        g_retired.push_back({std::move(g_readback), g_ready_at});
      }
      g_readback = bd::gpu::CreateHostBuffer(
          s.device.get(),
          plume::RenderBufferDesc::ReadbackBuffer(static_cast<u64>(pitch) * h),
          "screenshot-readback");
      g_rb_w = w;
      g_rb_h = h;
    }
    if (!g_readback) {
      PublishEmpty();
      return;
    }
    g_cap_w = w;
    g_cap_h = h;
    g_cap_pitch = pitch;

    // Copy outside the render pass: unbind, transition back to COPY_SOURCE,
    // copy, restore COLOR_WRITE, rebind so the overlay can draw and the
    // existing ->PRESENT barrier still applies.
    s.command_list->setFramebuffer(nullptr);
    s.command_list->barriers(
        plume::RenderBarrierStage::COPY,
        plume::RenderTextureBarrier(back,
                                    plume::RenderTextureLayout::COPY_SOURCE));
    // plume's copyTextureRegion unconditionally calls setSamplePositions(
    // dst.texture), and a placed footprint dst leaves .texture null -> null
    // deref in release. Point .texture at a real texture (back has no sample
    // locations, so it is a no-op). toD3D12 ignores .texture for
    // PLACED_FOOTPRINT, so the actual copy is unaffected.
    auto dst = plume::RenderTextureCopyLocation::PlacedFootprint(
        g_readback.get(), plume::RenderFormat::B8G8R8A8_UNORM, w, h, 1,
        pitch / 4u, 0);
    dst.texture = back;
    s.command_list->copyTextureRegion(
        dst, plume::RenderTextureCopyLocation::Subresource(back, 0, 0));
    s.command_list->barriers(
        plume::RenderBarrierStage::GRAPHICS,
        plume::RenderTextureBarrier(back,
                                    plume::RenderTextureLayout::COLOR_WRITE));
    s.command_list->setFramebuffer(back_fb);

    g_ready_at = g_present_index + kNumFrames; // map after the fence is waited
    g_state = State::Pending;
  }
}

} // namespace bd::gpu
