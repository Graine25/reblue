/**
 * @file    engine/frame_interp.cpp
 * @brief   Render interpolation between the 30Hz logic ticks of the fps unlock.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#include "engine/frame_interp.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <rex/hook.h>
#include <rex/ppc.h>
#include <rex/runtime.h>
#include <rex/system/function_dispatcher.h>
#include <rex/types.h>

#include "core/memory_helpers.h"
#include "engine/d2anime/anime_mouse.h"
#include "engine/d2anime/d2anime_task.h"
#include "engine/d2anime/d2anime_types.h"
#include "engine/frame_clock.h"
#include "engine/guest_prim.h"
#include "engine/glyph_set.h"
#include "engine/menus/camp_settings.h"
#include "engine/menus/local_map.h"
#include "engine/mouse_cursor.h"
#include "engine/state_layout.h"
#include "engine/virtual_buttons.h"
#include "gpu/gpu.h"

namespace {

// Rotation similarity floor: trace(prevR^T currR)/3 = (1+2cos(theta))/3.
// 0.90 ~= a 32-degree single-tick turn, beyond any authored pan, so cutscene
// shot cuts snap while pans still interpolate.
constexpr float kViewCutRotDot = 0.90f;

struct ViewEntry {
  float prevView[16];
  float currView[16];
  float avgStep = 0.0f;
  double lastChange = 0.0;
  u64 lastSeen = 0;
  bool valid = false;
  bool cut = false;
};

std::unordered_map<u32, ViewEntry> g_views;
u64 g_camFrame = 0;
u32 g_viewScratch = 0; // guest scratch holding the interpolated view matrix

void ReadFloats(be_f32 *p, float *out, int n) {
  for (int i = 0; i < n; ++i)
    out[i] = p[i];
}
void WriteFloats(be_f32 *p, const float *in, int n) {
  for (int i = 0; i < n; ++i)
    p[i] = in[i];
}
constexpr double kTickSeconds = 1.0 / 30.0;
constexpr double kFastChangeSeconds = kTickSeconds * 0.5;

float EntityAlpha(double lastChange) {
  const double held = bd::engine::FrameTime() - lastChange;
  return float(std::clamp(held / kTickSeconds, 0.0, 1.0));
}

float EyeDistSq(const float a[3], const float b[3]) {
  const float dx = a[0] - b[0], dy = a[1] - b[1], dz = a[2] - b[2];
  return dx * dx + dy * dy + dz * dz;
}
void LerpMatrix(const float a[16], const float b[16], float t, float out[16]) {
  for (int i = 0; i < 16; ++i)
    out[i] = a[i] + (b[i] - a[i]) * t;
}

void PruneViews() {
  for (auto it = g_views.begin(); it != g_views.end();) {
    if (g_camFrame - it->second.lastSeen > 4)
      it = g_views.erase(it);
    else
      ++it;
  }
}

void EyeFromView(const float v[16], float eye[3]) {
  const float tx = v[12], ty = v[13], tz = v[14];
  eye[0] = -(tx * v[0] + ty * v[1] + tz * v[2]);
  eye[1] = -(tx * v[4] + ty * v[5] + tz * v[6]);
  eye[2] = -(tx * v[8] + ty * v[9] + tz * v[10]);
}

constexpr int kWorldFloats = 16;
constexpr u32 kMaxPaletteMatrices = 96;
constexpr int kMaxPaletteFloats = int(kMaxPaletteMatrices) * 16;
constexpr u64 kSnapshotStaleFrames = 4;
u32 g_paletteScratch = 0; // guest scratch for the interpolated palette
u32 g_worldScratch = 0;   // guest scratch for the interpolated world

struct FloatSnapshot {
  std::vector<float> prev;
  std::vector<float> curr;
  float avgStep = 0.0f;
  float avgSpacing = 0.0f;
  double lastChange = 0.0;
  u64 lastSeen = 0;
  bool valid = false;
  bool cut = false;
  u16 streak = 0;
};

std::mutex g_interpMutex;

std::unordered_map<u64, FloatSnapshot> g_objSnapshots;
u64 g_objFrame = 0;
u32 g_drawObject = 0;
u32 g_paletteSlot = 0;
u64 g_worldKey = 0;
u64 g_nodeScope = 0;
u32 g_listObject = 0;
u32 g_listSeq = 0;
std::unordered_map<u32, u64> g_recordKeys;
u32 g_recordSeq = 0;

u64 NodeIdentity(u32 nodeIdx) {
  const u32 vo = bd::mem::try_load<u32>(bd::engine::addr::kCameraRenderVO);
  return vo ? ((u64(nodeIdx) + 1) << 32) | vo : 0;
}

constexpr float kCutDistance = 64.0f;
constexpr float kCutRatio = 10.0f;
constexpr float kCutFloor = 20.0f;
constexpr float kStepBlend = 0.25f;
constexpr float kObjCutRotDot = 0.25f;
constexpr u16 kTrustedStreak = 8;
constexpr float kSubTickSpacing = float(kTickSeconds * 0.85);

bool SubTickWriter(const FloatSnapshot &e) {
  return e.avgSpacing != 0.0f && e.avgSpacing < kSubTickSpacing;
}

bool StepDiscontinuous(float step, float avgStep) {
  return avgStep > 0.0f && step > kCutFloor && step > avgStep * kCutRatio;
}

float BlendStep(float avgStep, float step) {
  return avgStep > 0.0f ? avgStep + (step - avgStep) * kStepBlend : step;
}

float MinRowDot(const float *cur, const float *prv) {
  float worst = 1.0f;
  for (int r = 0; r < 12; r += 4) {
    float dot = 0.0f, mc = 0.0f, mp = 0.0f;
    for (int i = r; i < r + 3; ++i) {
      const float c = cur[i], p = prv[i];
      dot += c * p;
      mc += c * c;
      mp += p * p;
    }
    const float denom = std::sqrt(mc * mp);
    if (denom > 1e-6f && dot / denom < worst)
      worst = dot / denom;
  }
  return worst;
}

bool RotationDiscontinuous(const float *cur, const float *prv, float floor) {
  return MinRowDot(cur, prv) < floor;
}

float PaletteMaxDelta(const float *cur, const float *prv, int floats) {
  float m = 0.0f;
  for (int i = 0; i < floats; ++i) {
    const float d = std::fabs(cur[i] - prv[i]);
    if (d > m)
      m = d;
  }
  return m;
}

bool InShadowDepthPass() {
  auto *view = bd::mem::try_at<be_u32>(bd::engine::addr::kRenderView);
  auto *sun = bd::mem::try_at<be_u32>(bd::engine::addr::kShadowLightView);
  auto *cube = bd::mem::try_at<be_u32>(bd::engine::addr::kCubeShadowLightView);
  if (!view)
    return false;
  bool isSun = sun != nullptr;
  bool isCube = cube != nullptr;
  for (int i = 0; i < 16 && (isSun || isCube); ++i) {
    const u32 v = view[i];
    if (isSun && u32(sun[i]) != v)
      isSun = false;
    if (isCube && u32(cube[i]) != v)
      isCube = false;
  }
  return isSun || isCube;
}

void PruneSnapshots() {
  for (auto it = g_objSnapshots.begin(); it != g_objSnapshots.end();) {
    if (g_objFrame - it->second.lastSeen > kSnapshotStaleFrames)
      it = g_objSnapshots.erase(it);
    else
      ++it;
  }
}

struct AnimeClock {
  float prev = 0.0f;
  float curr = 0.0f;
  double lastChange = 0.0;
  u64 lastSeen = 0;
  bool valid = false;
};

std::unordered_map<u32, AnimeClock> g_animeClocks;

bool AnimeClockDiscontinuous(float delta, float speed) {
  if (speed == 0.0f)
    return true;
  return delta * speed < 0.0f || std::fabs(delta) > std::fabs(speed) * 1.5f;
}

void PruneAnimeClocks() {
  for (auto it = g_animeClocks.begin(); it != g_animeClocks.end();) {
    if (g_objFrame - it->second.lastSeen > kSnapshotStaleFrames)
      it = g_animeClocks.erase(it);
    else
      ++it;
  }
}

enum class Snapshot { Missing, Ready, Rolled, First, Shared };

Snapshot AdvanceSnapshot(u64 key, u32 srcVa, int floats, FloatSnapshot *&out) {
  out = nullptr;
  auto *src = bd::mem::try_at<be_f32>(srcVa);
  if (!src)
    return Snapshot::Missing;
  FloatSnapshot &e = g_objSnapshots[key];
  e.lastSeen = g_objFrame;
  out = &e;
  if (e.curr.size() != size_t(floats)) {
    e.curr.assign(size_t(floats), 0.0f);
    e.prev.assign(size_t(floats), 0.0f);
    e.valid = false;
  }
  const double now = bd::engine::FrameTime();
  if (!e.valid) {
    for (int i = 0; i < floats; ++i)
      e.curr[size_t(i)] = src[i];
    e.prev = e.curr;
    e.valid = true;
    e.lastChange = now;
    return Snapshot::First;
  }
  bool changed = false;
  for (int i = 0; i < floats; ++i) {
    if (e.curr[size_t(i)] != float(src[i])) {
      changed = true;
      break;
    }
  }
  if (!changed)
    return Snapshot::Ready;
  const double spacing = now - e.lastChange;
  const bool fast = spacing < kFastChangeSeconds;
  const float sample = float(std::min(spacing, kTickSeconds * 4.0));
  e.avgSpacing = e.avgSpacing == 0.0f
                     ? sample
                     : e.avgSpacing + (sample - e.avgSpacing) * 0.25f;
  e.prev.swap(e.curr);
  for (int i = 0; i < floats; ++i)
    e.curr[size_t(i)] = src[i];
  e.lastChange = now;
  if (fast) {
    e.prev = e.curr;
    return Snapshot::Shared;
  }
  return Snapshot::Rolled;
}

u32 LerpToScratch(const FloatSnapshot &e, float a, u32 &scratch,
                  int scratchFloats) {
  if (scratch == 0) {
    scratch = bd::gpu::HostHeap::Get().AllocGuest(u32(scratchFloats) * 4, 16);
    if (scratch == 0)
      return 0;
  }
  auto *dst = bd::mem::at<be_f32>(scratch);
  if (!dst)
    return 0;
  for (size_t i = 0; i < e.curr.size(); ++i)
    dst[i] = e.prev[i] + (e.curr[i] - e.prev[i]) * a;
  return scratch;
}

} // namespace

// Skip the 30Hz logic block on non-tick frames. r28=0xDEAD0000 is the sentinel
// the skipped lis would load (the render block DEAD root checks read it
// unreloaded).
bool bdLogicTickGateHook(PPCRegister &r28) {
  if (bd::engine::TickDue())
    return false;
  r28.u64 = 0xDEAD0000;
  return true;
}

// The master animation clock ticks once per present, so freeze it on
// interpolated frames. No-op at <=30Hz, where TickDue() is always true.
bool bdFrameClockGateHook() { return !bd::engine::TickDue(); }

// Render-side accumulators step once per rendered frame with no delta time.
// Only the accumulation store is skipped, the uploads and draws after it
// re-read the unchanged values.
bool bdShaderAnimGateHook() { return !bd::engine::TickDue(); }

bool bdPlayerAmbientRampGateHook(PPCRegister &r31) {
  if (!bd::engine::InterpolationActive())
    return false;
  auto *amb = bd::mem::try_at<be_f32>(r31.u32 + 0xBC4);
  if (!amb)
    return true;
  const float green = amb[1];
  if (green >= 1.0f)
    return true;
  const float next =
      green + 0.1f * float(bd::engine::FrameDelta() / kTickSeconds);
  if (next >= 1.0f) {
    amb[0] = 1.0f;
    amb[1] = 1.0f;
    amb[2] = 1.0f;
  } else {
    amb[1] = next;
    amb[2] = next;
  }
  return true;
}

// Event camera cuts retire the outgoing shot draw-once-then-hide: the cut tick
// arms a one-shot flag and the next Draw consumes it. The consume runs at tick
// start, ahead of that tick's logic, because a cut window re-arms the hide
// every tick and a later consume kills the fresh re-arm.
constexpr u32 kIssObjectVtableEA = 0x8208AFA4;
constexpr u32 kIssActorVtableEA = 0x8208B334;
constexpr u32 kIssActorHideFnEA = 0x82410A18; // clears issActor +0x14C

namespace {
struct IssObject_t {
  u8 _pad000[0x9E0];
  be_u32 hideConsume; // Draw writes 0 to cancel the armed hide
  be_u32 hideArm;     // cut tick arms the one-shot hide
};
static_assert(offsetof(IssObject_t, hideConsume) == 0x9E0);
static_assert(offsetof(IssObject_t, hideArm) == 0x9E4);
static_assert(sizeof(IssObject_t) == 0x9E8);

// issActor__ApplySpecialModelFlags (kIssActorHideFnEA) is the consume path.
// It clears +0x14C in the guest, which is not modeled here.
struct IssActor_t {
  u8 _pad000[0x150];
  be_u32 hideArm;
};
static_assert(offsetof(IssActor_t, hideArm) == 0x150);
static_assert(sizeof(IssActor_t) == 0x154);
} // namespace

namespace {
std::unordered_set<u32> g_evtHidePending;
} // namespace

bool bdEvtShotHideDeferHook(PPCRegister &r31) {
  if (!bd::engine::InterpolationActive()) {
    g_evtHidePending.clear();
    return false;
  }
  if (g_evtHidePending.size() > 256)
    g_evtHidePending.clear();
  g_evtHidePending.insert(r31.u32);
  return true;
}

namespace {
void FlushEvtHidePending() {
  if (g_evtHidePending.empty())
    return;
  auto *dispatcher = REX_KERNEL_STATE()->function_dispatcher();
  for (const u32 obj : g_evtHidePending) {
    const u32 vtable = bd::mem::load<u32>(obj);
    if (vtable == kIssObjectVtableEA) {
      if (auto *o = bd::mem::at<IssObject_t>(obj)) {
        if (o->hideArm != 0)
          o->hideConsume = 0;
      }
    } else if (vtable == kIssActorVtableEA) {
      if (auto *a = bd::mem::at<IssActor_t>(obj)) {
        if (a->hideArm != 0) {
          if (auto *fn = dispatcher->GetFunction(kIssActorHideFnEA))
            rex::ppc::GuestToHostFunction<void>(fn, obj);
        }
      }
    }
  }
  g_evtHidePending.clear();
}
} // namespace

// The blink arm flag is set once per logic tick and consumed by the armed
// draw, so interpolated frames find it already consumed. Track liveness here
// and force the armed path while a blink is active.
bool bdCompassBlinkHoldHook(PPCRegister &r11) {
  static bool blinkActive = false;
  if (r11.u32 != 0) {
    blinkActive = true;
    return false;
  }
  if (bd::engine::TickDue()) {
    blinkActive = false;
    return false;
  }
  return blinkActive;
}

// The prim pool is flip-recycled every rendered frame, so a quad pushed from
// the 30Hz logic side exists only on tick frames. Capture the args each tick
// and re-issue from an ungated per-frame hook in the same 2D submission
// window. A capture goes stale the moment a tick passes without vf02 re-issuing
// it, so replay stops with it.

namespace {
struct FrostPrimCapture {
  u64 tick = ~0ull;
  double x = 0.0, y = 0.0, w = 0.0, h = 0.0;
  u32 color = 0;
  u32 texObjEA = 0;
};
FrostPrimCapture g_frostPrim;
} // namespace

void bdFaceFrostCaptureHook(PPCRegister &f1, PPCRegister &f2, PPCRegister &f4,
                            PPCRegister &f5, PPCRegister &r8,
                            PPCRegister &r30) {
  g_frostPrim.tick = bd::engine::TickCount();
  g_frostPrim.x = f1.f64;
  g_frostPrim.y = f2.f64;
  g_frostPrim.w = f4.f64;
  g_frostPrim.h = f5.f64;
  g_frostPrim.color = r8.u32;
  g_frostPrim.texObjEA = r30.u32;
}

// Two text prims into the same flip-recycled pool, so the same
// capture-and-replay as the frost quad above.
//
// bdPushTextPrim takes five doubles and nine integers: r3-r10 then one slot the
// SDK marshaller places at r1+0x54, read back as the text style word. Only r8
// (string), r9 (color) and r10 carry meaning here.
//
// The replayed label sits at the tick's projected position, so it steps at 30Hz
// while the camera interpolates. Re-projecting would need the world position
// and the text width centering the guest applies after it.
REX_IMPORT(__imp__Visual__method_7E60, ItemDropPushText,
           void(f64, f64, f64, f64, f64, u32, u32, u32, u32, u32, u32, u32, u32,
                u32));

namespace {

constexpr u32 kItemDropTextChars = 96;
constexpr u32 kItemDropTextPrims = 2;

struct ItemDropTextPrim {
  f64 x = 0.0, y = 0.0, z = 0.0, w = 0.0, h = 0.0;
  u32 color = 0;
  u32 mode = 0;
};

struct ItemDropTextCapture {
  u64 tick = ~0ull;
  u32 count = 0;
  u32 textEA = 0;
  ItemDropTextPrim prims[kItemDropTextPrims];
};

ItemDropTextCapture g_itemDropText;

// The guest string lives in the vf13 stack frame, which is gone by replay time.
bool CopyGuestWideString(u32 srcVa, u32 dstVa) {
  auto *src = bd::mem::try_at<const be_u16>(srcVa);
  auto *dst = bd::mem::at<be_u16>(dstVa);
  if (!src || !dst)
    return false;
  for (u32 i = 0; i + 1 < kItemDropTextChars; ++i) {
    const u16 c = src[i];
    dst[i] = c;
    if (c == 0)
      return i > 0;
  }
  dst[kItemDropTextChars - 1] = 0;
  return true;
}

} // namespace

void bdItemDropTextCaptureHook(PPCRegister &f1, PPCRegister &f2,
                               PPCRegister &f3, PPCRegister &f4,
                               PPCRegister &f5, PPCRegister &r8,
                               PPCRegister &r9, PPCRegister &r10) {
  if (!bd::engine::InterpolationActive())
    return;
  auto &cap = g_itemDropText;
  const u64 tick = bd::engine::TickCount();
  if (cap.tick != tick) {
    cap.tick = tick;
    cap.count = 0;
  }
  if (cap.count >= kItemDropTextPrims)
    return;
  if (cap.textEA == 0) {
    cap.textEA = bd::gpu::HostHeap::Get().AllocGuest(
        static_cast<u32>(kItemDropTextChars * sizeof(be_u16)), 4);
    if (cap.textEA == 0)
      return;
  }
  if (cap.count == 0 && !CopyGuestWideString(r8.u32, cap.textEA)) {
    cap.tick = ~0ull;
    return;
  }
  auto &prim = cap.prims[cap.count++];
  prim.x = f1.f64;
  prim.y = f2.f64;
  prim.z = f3.f64;
  prim.w = f4.f64;
  prim.h = f5.f64;
  prim.color = r9.u32;
  prim.mode = r10.u32;
}

// Before bdPrimFlush, so the re-issued prims join this frame's 2D pass ahead
// of the slot flip. The frost quad's own replay site exists only while a
// dialogue portrait window does.
void bdItemDropTextReplayHook() {
  if (!bd::engine::InterpolationActive() || bd::engine::TickDue())
    return;
  if (g_itemDropText.tick != bd::engine::TickCount() ||
      !g_itemDropText.textEA || g_itemDropText.count == 0)
    return;
  for (u32 i = 0; i < g_itemDropText.count; ++i) {
    const auto &prim = g_itemDropText.prims[i];
    PrimSelectTexture(0, 0);
    ItemDropPushText(prim.x, prim.y, prim.z, prim.w, prim.h, 0, 0, 0, 0, 0,
                     g_itemDropText.textEA, prim.color, prim.mode, 0);
  }
}

REX_EXTERN(__imp__FreeDfsTask__vf03);
REX_HOOK_RAW(FreeDfsTask__vf03) {
  __imp__FreeDfsTask__vf03(ctx, base);
  if (!bd::engine::InterpolationActive() || bd::engine::TickDue())
    return;
  if (g_frostPrim.tick != bd::engine::TickCount() || !g_frostPrim.texObjEA)
    return;
  PrimSelectTexture(0, g_frostPrim.texObjEA);
  PrimDrawRect2D(g_frostPrim.x, g_frostPrim.y, 1.0, g_frostPrim.w,
                      g_frostPrim.h, 0, 0, 0, 0, 0, g_frostPrim.color);
}

// A changed light already in an object's active set is what forces the
// re-score that drops a light since disabled or moved out of range, and the
// changed list is empty on interpolated frames. Hold it across a tick and
// clear it here at tick start, before the guest repopulates it.
namespace {

constexpr u32 kLightEntriesEA = 0x82E18694;      // light manager + 8
constexpr u32 kLightChangedListEA = 0x82E1DFA8;  // entries + 0x5914
constexpr u32 kLightChangedCountEA = 0x82E1E458; // entries + 0x5DC4
constexpr u32 kLightChangedFlag = 0x40;          // entry flags bit 6
constexpr u32 kLightMaxEntries = 300;
constexpr u32 kLightEntryStride = 0x4C;

void SetChangedFlags(u32 count, bool set) {
  auto *list = bd::mem::at<be_u32>(kLightChangedListEA);
  if (!list)
    return;
  for (u32 i = 0; i < count; ++i) {
    const u32 entry = static_cast<u32>(list[i]);
    if (entry < kLightEntriesEA ||
        entry >= kLightEntriesEA + kLightMaxEntries * kLightEntryStride) {
      continue;
    }
    if (auto *flags = bd::mem::at<be_u32>(entry)) {
      const u32 v = *flags;
      *flags = set ? (v | kLightChangedFlag) : (v & ~kLightChangedFlag);
    }
  }
}

void ClearLightChangedList() {
  u32 count = bd::mem::load<u32>(kLightChangedCountEA);
  if (count > kLightMaxEntries)
    count = kLightMaxEntries;
  SetChangedFlags(count, false);
  bd::mem::store<u32>(kLightChangedCountEA, 0u);
}

} // namespace

REX_EXTERN(__imp__bdLightListUpdateSnapshot);
REX_HOOK_RAW(bdLightListUpdateSnapshot) {
  u32 held = bd::engine::InterpolationActive()
                 ? bd::mem::load<u32>(kLightChangedCountEA)
                 : 0;
  if (held > kLightMaxEntries)
    held = 0;

  __imp__bdLightListUpdateSnapshot(ctx, base);

  if (held == 0)
    return;
  SetChangedFlags(held, true);
  bd::mem::store<u32>(kLightChangedCountEA, held);
}

namespace bd::engine {

void OnGuestGameStep() {
  Advance();
  {
    std::lock_guard<std::mutex> lock(g_interpMutex);
    ++g_objFrame;
    ++g_camFrame;
    PruneSnapshots();
    PruneViews();
    PruneAnimeClocks();
    g_recordKeys.clear();
  }
  g_drawObject = 0;
  if (InterpolationActive() && TickDue()) {
    FlushEvtHidePending();
    ClearLightChangedList();
  }
}

} // namespace bd::engine

REX_EXTERN(__imp__bdSceneNodeProcessRenderCmds);
REX_HOOK_RAW(bdSceneNodeProcessRenderCmds) {
  if (bd::engine::InterpolationActive()) {
    g_worldKey = g_nodeScope = NodeIdentity(ctx.r4.u32);
    g_recordSeq = 0;
  }
  __imp__bdSceneNodeProcessRenderCmds(ctx, base);
  g_worldKey = 0;
  g_nodeScope = 0;
}

REX_EXTERN(__imp__bdSceneNodeDrawSingle);
REX_HOOK_RAW(bdSceneNodeDrawSingle) {
  if (bd::engine::InterpolationActive()) {
    g_worldKey = g_nodeScope = NodeIdentity(ctx.r4.u32);
    g_recordSeq = 0;
  }
  __imp__bdSceneNodeDrawSingle(ctx, base);
  g_worldKey = 0;
  g_nodeScope = 0;
}

REX_EXTERN(__imp__bdBuildViewMatrix);
REX_HOOK_RAW(bdBuildViewMatrix) {
  if (bd::engine::InterpolationActive() && ctx.r3.u32 && !ctx.r4.u32 &&
      !ctx.r5.u32) {
    std::lock_guard<std::mutex> lock(g_interpMutex);
    u64 key;
    if (g_worldKey) {
      key = (1ull << 63) | g_worldKey;
    } else if (auto it = g_recordKeys.find(ctx.r3.u32 - 16);
               it != g_recordKeys.end()) {
      key = (1ull << 63) | it->second;
    } else if (g_listObject) {
      key = (1ull << 62) | (u64(g_listSeq++) << 32) | g_listObject;
    } else {
      key = u64(ctx.r3.u32);
    }
    g_drawObject = u32(key ^ (key >> 32));
    g_worldKey = 0;
    g_paletteSlot = 0;
    const bool depthPass = InShadowDepthPass();
    FloatSnapshot *e = nullptr;
    switch (AdvanceSnapshot(key, ctx.r3.u32, kWorldFloats, e)) {
    case Snapshot::Rolled: {
      const float step = std::sqrt(EyeDistSq(&e->curr[12], &e->prev[12]));
      e->cut = step > kCutDistance || StepDiscontinuous(step, e->avgStep) ||
               MinRowDot(e->curr.data(), e->prev.data()) < kObjCutRotDot ||
               SubTickWriter(*e);
      if (e->cut) {
        e->streak = 0;
      } else {
        e->avgStep = BlendStep(e->avgStep, step);
        if (e->streak < 0xFFFF)
          ++e->streak;
      }
    }
      [[fallthrough]];
    case Snapshot::Ready:
      if (!e->cut && (!depthPass || e->streak >= kTrustedStreak)) {
        const u32 scratch = LerpToScratch(*e, EntityAlpha(e->lastChange),
                                          g_worldScratch, kWorldFloats);
        if (scratch)
          ctx.r3.u32 = scratch;
      }
      break;
    case Snapshot::First:
      break;
    case Snapshot::Shared:
      e->streak = 0;
      g_drawObject = 0;
      break;
    case Snapshot::Missing:
      break;
    }
  }

  const u32 viewVa = bd::engine::InterpolationActive() ? ctx.r4.u32 : 0;
  auto *live = viewVa ? bd::mem::try_at<be_f32>(viewVa) : nullptr;
  if (live) {
    if (viewVa == bd::engine::addr::kShadowLightView ||
        viewVa == bd::engine::addr::kCubeShadowLightView) {
      __imp__bdBuildViewMatrix(ctx, base);
      return;
    }
    float liveView[16];
    ReadFloats(live, liveView, 16);

    float view[16];
    bool shared = false;
    {
      std::lock_guard<std::mutex> lock(g_interpMutex);
      ViewEntry &e = g_views[viewVa];
      e.lastSeen = g_camFrame;
      const double now = bd::engine::FrameTime();
      bool changed = false;
      for (int i = 0; i < 16; ++i) {
        if (e.currView[i] != liveView[i]) {
          changed = true;
          break;
        }
      }
      if (!e.valid) {
        for (int i = 0; i < 16; ++i)
          e.prevView[i] = e.currView[i] = liveView[i];
        e.valid = true;
        e.lastChange = now;
      } else if (changed) {
        const bool fast = now - e.lastChange < kFastChangeSeconds;
        for (int i = 0; i < 16; ++i) {
          e.prevView[i] = fast ? liveView[i] : e.currView[i];
          e.currView[i] = liveView[i];
        }
        e.lastChange = now;
        if (fast) {
          shared = true;
        } else {
          float pe[3], ce[3];
          EyeFromView(e.prevView, pe);
          EyeFromView(e.currView, ce);
          const float step = std::sqrt(EyeDistSq(pe, ce));
          e.cut = StepDiscontinuous(step, e.avgStep) ||
                  RotationDiscontinuous(e.currView, e.prevView, kViewCutRotDot);
          if (!e.cut)
            e.avgStep = BlendStep(e.avgStep, step);
        }
      }
      if (!shared) {
        if (e.cut) {
          for (int i = 0; i < 16; ++i)
            view[i] = e.currView[i];
        } else {
          LerpMatrix(e.prevView, e.currView, EntityAlpha(e.lastChange), view);
        }
      }
    }
    if (shared) {
      __imp__bdBuildViewMatrix(ctx, base);
      return;
    }

    if (g_viewScratch == 0)
      g_viewScratch = bd::gpu::HostHeap::Get().AllocGuest(64, 16);
    if (g_viewScratch != 0) {
      WriteFloats(bd::mem::at<be_f32>(g_viewScratch), view, 16);
      ctx.r4.u32 = g_viewScratch;
    }
  }
  __imp__bdBuildViewMatrix(ctx, base);
}

REX_EXTERN(__imp__D2AnimeTask_Draw);
REX_HOOK_RAW(D2AnimeTask_Draw) {
  auto *task = bd::engine::InterpolationActive()
                   ? bd::mem::try_at<bd::engine::D2AnimeTask_t>(ctx.r3.u32)
                   : nullptr;
  if (!task) {
    __imp__D2AnimeTask_Draw(ctx, base);
    return;
  }

  const float live = static_cast<float>(task->animFrame);
  float prev = 0.0f;
  float curr = 0.0f;
  float alpha = 0.0f;
  {
    std::lock_guard<std::mutex> lock(g_interpMutex);
    AnimeClock &c = g_animeClocks[ctx.r3.u32];
    c.lastSeen = g_objFrame;
    const double now = bd::engine::FrameTime();
    if (!c.valid) {
      c.prev = c.curr = live;
      c.valid = true;
      c.lastChange = now;
    } else if (c.curr != live) {
      const bool cut = now - c.lastChange < kFastChangeSeconds ||
                       AnimeClockDiscontinuous(
                           live - c.curr, static_cast<float>(task->animSpeed));
      c.prev = cut ? live : c.curr;
      c.curr = live;
      c.lastChange = now;
    }
    alpha = EntityAlpha(c.lastChange);
    if (c.prev == c.curr || alpha >= 1.0f)
      c.prev = c.curr;
    prev = c.prev;
    curr = c.curr;
  }

  if (prev == curr) {
    __imp__D2AnimeTask_Draw(ctx, base);
    return;
  }
  task->animFrame = prev + (curr - prev) * alpha;
  __imp__D2AnimeTask_Draw(ctx, base);
  task->animFrame = live;
}

// Poll input at 30Hz so edge-detect and auto-repeat stay in lockstep with the
// logic.
REX_EXTERN(__imp__bdInputSystemUpdate);
REX_HOOK_RAW(bdInputSystemUpdate) {
  if (!bd::engine::TickDue())
    return;
  // Ahead of the original, so the game's own screens see the cursor write
  // already applied when they poll input this tick. A second REX_HOOK_RAW on
  // the same symbol would collide at link time.
  bd::engine::SampleButtonEdges();
  bd::engine::MenuMouse::Get().BeginFrame();
  // After BeginFrame, which publishes whether a menu owns input this frame, so
  // a look starts and stops on the same tick the menu opens.
  bd::engine::UpdateMouseLook();
  bd::engine::MouseCursorTick();
  bd::engine::Glyphs::Get().Tick();
  bd::engine::D2AnimeTask::Tick();
  bd::engine::CampSettings::Get().Tick();
  // After BeginFrame too: the bind stands down while a menu owns input.
  bd::engine::AreaMapTick();
  __imp__bdInputSystemUpdate(ctx, base);
}

// PadVibrationCore::vf03 drains the accumulated amplitude with a store, not a
// max, so it has to run at the same 30Hz that fills it.
REX_EXTERN(__imp__PadVibrationCore__vf03);
REX_HOOK_RAW(PadVibrationCore__vf03) {
  if (!bd::engine::TickDue())
    return;
  __imp__PadVibrationCore__vf03(ctx, base);
}

void bdAlphaPrimCaptureHook(PPCRegister &r3) {
  if (g_nodeScope == 0 || r3.u32 == 0)
    return;
  std::lock_guard<std::mutex> lock(g_interpMutex);
  g_recordKeys[r3.u32] = g_nodeScope | (u64(++g_recordSeq) << 48);
}

void bdListObjectBeginHook(PPCRegister &r3) {
  g_listObject = r3.u32;
  g_listSeq = 0;
}

void bdListObjectEndHook() { g_listObject = 0; }

void bdObjectPaletteInterpHook(PPCRegister &r4, PPCRegister &r5) {
  if (!bd::engine::InterpolationActive())
    return;
  const u32 matrices = r5.u32;
  if (!r4.u32 || matrices == 0 || matrices > kMaxPaletteMatrices)
    return;
  if (g_drawObject == 0)
    return;
  const int floats = int(matrices) * 16;
  const u64 key = (u64(g_drawObject) << 32) | u64(g_paletteSlot + 1);
  ++g_paletteSlot;

  std::lock_guard<std::mutex> lock(g_interpMutex);
  FloatSnapshot *e = nullptr;
  switch (AdvanceSnapshot(key, r4.u32, floats, e)) {
  case Snapshot::Rolled:
    e->cut = PaletteMaxDelta(e->curr.data(), e->prev.data(), floats) >
                 kCutDistance ||
             SubTickWriter(*e);
    break;
  case Snapshot::Ready:
    break;
  case Snapshot::First:
  case Snapshot::Shared:
  case Snapshot::Missing:
    return;
  }
  const u32 scratch =
      e->cut ? 0
             : LerpToScratch(*e, EntityAlpha(e->lastChange), g_paletteScratch,
                             kMaxPaletteFloats);
  if (scratch)
    r4.u32 = scratch;
}

u32 rex_QueryPerformanceCounter_hook(u32 lpPerformanceCount) {
  if (lpPerformanceCount) {
    auto *out = bd::mem::at<be_i64>(lpPerformanceCount);
    if (out)
      *out = std::chrono::steady_clock::now().time_since_epoch().count();
  }
  return 1;
}
REX_HOOK(rex_QueryPerformanceCounter, rex_QueryPerformanceCounter_hook);

u32 rex_QueryPerformanceFrequency_hook(u32 lpFrequency) {
  if (lpFrequency) {
    constexpr i64 kFreq = std::chrono::steady_clock::period::den /
                          std::chrono::steady_clock::period::num;
    auto *out = bd::mem::at<be_i64>(lpFrequency);
    if (out)
      *out = kFreq;
  }
  return 1;
}
REX_HOOK(rex_QueryPerformanceFrequency, rex_QueryPerformanceFrequency_hook);
