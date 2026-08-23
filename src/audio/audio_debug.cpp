/**
 * @file    audio/audio_debug.cpp
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause - see LICENSE
 */
#include "audio/audio_debug.h"

#include "audio/settings.h"
#include "core/logging.h"
#include "core/memory_helpers.h"

#include <mutex>
#include <unordered_map>

#include <rex/hook.h>
#include <rex/logging/api.h>
#include <rex/ppc.h>
#include <rex/types.h>

namespace bd::audio {
namespace {

constexpr u32 kSoundDebugGlobalsAddr = 0x82DC9820;

// Engine sound debug globals (retail-present, drawn by the Mindows debug text
// path).
struct SoundDebugGlobals_t {
  be_u32 cue_monitor_mode; // 0/1/2
  be_u32 peak_meter;       // output peak-meter overlay enable
  be_u32 peak_meter_db;    // peak-meter dB readout enable
};
static_assert(offsetof(SoundDebugGlobals_t, cue_monitor_mode) == 0x00);
static_assert(offsetof(SoundDebugGlobals_t, peak_meter) == 0x04);
static_assert(offsetof(SoundDebugGlobals_t, peak_meter_db) == 0x08);

} // namespace

void ApplyAudioDebugPokes() {
  auto *g = mem::at<SoundDebugGlobals_t>(kSoundDebugGlobalsAddr);
  if (!g)
    return;
  const i32 peak = Settings::Get().PeakMeter();
  g->cue_monitor_mode = static_cast<u32>(Settings::Get().CueMonitor());
  g->peak_meter = peak >= 1 ? 1u : 0u;
  g->peak_meter_db = peak >= 2 ? 1u : 0u;
}

} // namespace bd::audio

// bdSoundBankPlayCue(bank, cueIndex, mode, param, cb) -> cue handle (0 = fail,
// otherwise the cue slot's serial). Cue names appear in the on-screen cue
// monitor, and this log provides the timeline.
//
// Raw, on the inherited context: a typed REX_IMPORT re-roots the guest stack
// at ThreadState's r1 and overwrites the frames live underneath it.
REX_EXTERN(__imp__bdSoundBankPlayCue);
REX_HOOK_RAW(bdSoundBankPlayCue) {
  const u32 bank = ctx.r3.u32;
  const u32 cue_index = ctx.r4.u32 & 0xFFFF;
  const u32 mode = ctx.r5.u32;
  __imp__bdSoundBankPlayCue(ctx, base);
  if (bd::audio::Settings::Get().Log())
    BD_INFO("PlayCue bank={:08X} idx={} mode={} handle={:08X}", bank, cue_index,
            mode, ctx.r3.u32);
}

// XAUDIO::CSourceStream::SetFrequencyScale(this, float). Pitch ratio actually
// applied to a voice. Classifies whine as authored/RPC pitch vs decode
// artifact.
REX_EXTERN(__imp__CSourceStream_SetFrequencyScale);
REX_HOOK_RAW(CSourceStream_SetFrequencyScale) {
  const u32 self = ctx.r3.u32;
  const float scale = static_cast<float>(ctx.f1.f64);
  __imp__CSourceStream_SetFrequencyScale(ctx, base);
  if (!bd::audio::Settings::Get().Log())
    return;

  // Voices hold a steady ratio for long stretches, so log only the transitions.
  static std::mutex m;
  static std::unordered_map<u32, float> last;
  std::lock_guard lk(m);
  auto [it, inserted] = last.try_emplace(self, scale);
  if (!inserted && it->second == scale)
    return;
  it->second = scale;
  if (scale != 1.0f)
    BD_INFO("FreqScale voice={:08X} scale={:.5f}", self, scale);
}
