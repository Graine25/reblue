/**
 * @file    gpu/surface_pool.h
 * @brief   Free list cache of RT/DS GuestTextures, keyed by dims + format.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#pragma once

#include <rex/types.h>

namespace bd::gpu {

struct GuestTexture;

// The engine creates and releases scratch surfaces every frame, each otherwise
// a fresh committed D3D12 alloc. The dims it asks for repeat, so reuse beats
// reallocating.
//
// GPU safety: Return() runs only from DrainSlot (post-fence,
// post-NotifyTextureDestroyed), so a pooled surface is detached. It is idle
// only against the slot DrainSlot awaited. The other in-flight slot's list can
// still name it, so eviction retires a victim through the texture graveyard.
class SurfacePool {
public:
  // A matching pooled surface, or nullptr on miss. It keeps its plume
  // RenderTexture + view, and the caller re-inits the X360 header and re-binds
  // the SRV.
  static GuestTexture *Acquire(u32 width, u32 height, u32 plume_format,
                               u32 sample_count, bool is_depth);

  // Offer a released RT/DS surface back. true => parked (caller must NOT free
  // it), false => rejected, caller frees it. Call only after
  // NotifyTextureDestroyed + fence.
  static bool Return(GuestTexture *surface);

  // Free every pooled surface (device teardown). Destroys inline with no fence,
  // so the GPU must already be idle.
  static void Clear();

  struct MissInfo {
    bool ever_parked = false; // this key was pooled before: evicted, not new
    u32 free_count = 0;
    u64 evicted_lru = 0;     // freed under byte-budget pressure
    u64 rejected_percap = 0; // Return refused, bucket already at per-key cap
    u64 parked_bytes = 0;
    // This key's own history: misses WITH lru evictions mean it is thrown out
    // then immediately wanted again, and misses without mean the fence delay.
    u64 key_hits = 0;
    u64 key_misses = 0;
    u64 key_evicted_lru = 0;
    u64 key_bytes = 0; // per surface
  };
  // Why the Acquire above missed. Diagnostic only, and recomputes the key.
  static MissInfo DescribeMiss(u32 width, u32 height, u32 plume_format,
                               u32 sample_count, bool is_depth);

  struct Stats {
    u64 hits = 0;   // Acquire matches (cumulative)
    u64 misses = 0; // Acquire misses -> fresh alloc (cumulative)
    u64 evicted_lru = 0;
    u64 rejected_percap = 0;
    u64 rejected_oversize = 0; // single surface exceeds the byte budget
    u32 free_count = 0;        // surfaces currently parked
    u64 parked_bytes = 0;
    u64 peak_parked_bytes = 0;
  };
  static Stats GetStats();

  // Totals plus a per-key breakdown, at shutdown and every 30s on a miss.
  static void LogSummary();
};

} // namespace bd::gpu
