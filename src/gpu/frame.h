/**
 * @file    gpu/frame.h
 * @brief   Shared by the TUs that record one frame: the command list ring,
 *          the draw path, the EDRAM resolve emulation, and present.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#pragma once

#include <rex/types.h>

#include <plume_render_interface.h>

#include "gpu/device.h"

namespace bd::gpu {

// Frame ring.

// Guarded by command_list_open. Opened lazily by whichever of
// RequestClear / OpenCommandList wins, closed at Present.
void BeginCommandList(VideoState &s);

// Tear down everything frame slot 'slot' retired. Must run WITHOUT s.mutex:
// DestroyResourceNow -> NotifyTextureDestroyed and DrainEvictedNativeTextures
// both re-acquire it.
void DrainSlot(VideoState &s, u32 slot);

// Advance the frame cursor and make the newly current slot safe to record into.
// Caller holds s.mutex, and the matching DrainSlot runs after it drops.
void AdvanceAndWaitReused(VideoState &s);

// Fence only, no wait/signal semaphores. No-op if no list is open. Caller holds
// s.mutex.
void SubmitOpenListLocked(VideoState &s);

// Draw path.

// A surface with no host texture counts as unbound. With neither bound, BD is
// drawing 2D/UI with RT[0] implicit, so substitute back_buffer_surface
// color-only. A depth-only pass is left alone. Shared by BindDrawFramebuffer
// and FlushRenderState, which have to reach the same answer.
void ResolveEffectiveTargets(VideoState &s, GuestTexture *&rt,
                             GuestTexture *&ds);

// Cached per (rt, ds) pair.
plume::RenderFramebuffer *GetFramebuffer(VideoState &s, GuestTexture *rt,
                                         GuestTexture *ds);

// Resolve path.
bool CopySurfaceToTextureLocked(VideoState &s, GuestTexture *src,
                                GuestTexture *dst, const char *reason);
bool MaterializeOutboundLocked(VideoState &s, GuestTexture *source,
                               bool aliasable_only = false);
void MaterializeInboundLocked(VideoState &s, GuestTexture *dst);
void DetachSourceSurfaceLocked(VideoState &s, GuestTexture *texture);
bool FullscreenChainClassLocked(const VideoState &s, const GuestTexture *t);

// Registered by the app (ReblueApp::OnPreLaunchModule), invoked from
// PresentOverlayFrame.
extern Video::OverlayDrawHook g_overlay_draw_hook;

} // namespace bd::gpu
