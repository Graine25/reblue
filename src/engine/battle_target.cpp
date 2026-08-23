/**
 * @file    engine/battle_target.cpp
 * @brief   Point at the enemy you mean during the battle's target step.
 *
 * @copyright   Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *              All rights reserved.
 * @license     BSD 3-Clause - see LICENSE
 */
#include <cmath>
#include <cstddef>

#include <rex/hook.h>
#include <rex/ppc.h>
#include <rex/ppc/stack.h>
#include <rex/types.h>

#include "core/memory_helpers.h"
#include "engine/d2anime/anime_hittest.h"
#include "engine/d2anime/anime_input.h"
#include "engine/d2anime/anime_mouse.h"
#include "reblue_init.h"

REX_IMPORT(__imp__bdBattleTargetIsSelectable, TargetIsSelectable,
           u32(u32, u32, u32));
REX_IMPORT(__imp__bdBattleTargetRefreshHighlights, TargetRefreshHighlights,
           u32(u32, u32));
REX_IMPORT(__imp__bdWorldToScreenPos3, WorldToScreenPos3,
           void(u32, u32, u32, u32, u32));
REX_EXTERN(__imp__bdBattleTargetSelectInput);

namespace bd::engine {

namespace {

// The battle scene, which bdBattleTargetSelectInput takes in r3 and the rest of
// the battle code reaches through g_battleSubsystemActive.
constexpr u32 kScene_CommandsBegin = 0x11C;
constexpr u32 kScene_CommandsEnd = 0x120;
constexpr u32 kScene_Selection = 0x1C0;
constexpr u32 kScene_TargetGroup = 0x230;
constexpr u32 kScene_TargetShape = 0x244;

// One command entry per side, holding that side's target grid.
constexpr u32 kCommandStride = 124;
constexpr u32 kEntry_GroupsBegin = 0x04;
constexpr u32 kEntry_GroupsEnd = 0x08;

// Groups run across the field and each holds a front and a back slot, which is
// the split bdBattleTargetStepGroup walks when left or right steps the group.
constexpr u32 kGroupStride = 16;
constexpr u32 kGroup_MembersBegin = 0x04;
constexpr u32 kGroup_MembersEnd = 0x08;
constexpr u32 kMemberStride = 32;
constexpr u32 kMember_Actor = 0x18;
constexpr u32 kMembersPerGroup = 2;

// What the pending action covers. Zero reaches nothing and eight reaches
// everything, and bdBattleTargetSelectInput skips its own movement code for
// both, so neither leaves a target to point at.
constexpr u32 kShape_Enabled = 0x28;
constexpr u32 kShape_Mode = 0x20;
constexpr u32 kShapeEverything = 8;

constexpr u32 kActor_WorldPos = 0x1B8;

constexpr u32 kVisualRenderVA = 0x82DC9848;

// Design canvas units. The world position sits at the actor's feet, so the pick
// point rises to roughly where the body reads, and the radius is about a
// character's width at battle distance: wide enough to be easy to hit, short
// enough that a pointer parked over the command window claims nobody.
constexpr f32 kBodyRise = 60.0f;
constexpr f32 kPickRadius = 140.0f;

// bdVisualObjectGetHipsScreenDepth negates the third component to get a depth,
// and the status icon stack at 0x82242C48 draws only while it is under one, so
// this is the engine's own answer to whether a point is in front of the camera.
constexpr f32 kFrontOfCamera = 1.0f;

struct GuestVec3_t {
  be_f32 x;
  be_f32 y;
  be_f32 z;
};
static_assert(sizeof(GuestVec3_t) == 0x0C);

// What bdBattleTargetStepGroup copies over the scene's selection once a
// candidate passes bdBattleTargetIsSelectable, and what
// bdBattleTargetBuildSelection fills in at 0x822C5FA8.
struct TargetSel_t {
  /* 0x00 */ be_u32 actor;
  /* 0x04 */ be_u32 side;
  /* 0x08 */ be_u32 commandSlot;
  /* 0x0C */ be_u32 group;
  /* 0x10 */ be_u32 member;
  // The part of a multi-part enemy that takes the hit, which
  // bdBattleTargetIsSelectable resolves and writes back. Minus one asks it to
  // start over rather than keep the previous target's part.
  /* 0x14 */ be_i32 part;
  /* 0x18 */ be_u32 flags;
};
static_assert(sizeof(TargetSel_t) == 0x1C);

// Guest scratch, pushed once per frame. The first 0x20 is left to the callee
// stack parameter area, which a call writes at r1+8 and would otherwise put
// straight over whatever was pushed at r1.
constexpr u32 kScratchBytes = 0x60;
constexpr u32 kScratch_Sel = 0x20;
constexpr u32 kScratch_Screen = 0x40;
constexpr u32 kScratch_World = 0x50;

struct Candidate {
  u32 actor = 0;
  u32 group = 0;
  u32 member = 0;
  f32 x = 0.0f;
  f32 y = 0.0f;
  f32 z = 0.0f;
  f32 distance = 0.0f;
};

// Guest vector of fixed-stride elements, as the battle code spells them out.
u32 VectorCount(u32 objectVA, u32 beginOffset, u32 endOffset, u32 stride) {
  const u32 begin = mem::try_field<u32>(objectVA, beginOffset);
  const u32 end = mem::try_field<u32>(objectVA, endOffset);
  if (!begin || end <= begin || !stride)
    return 0;
  return (end - begin) / stride;
}

// True while the pad, rather than the pointer, is moving the target: the same
// four buttons bdBattleTargetSelectInput takes first out of its own poll.
bool PadMovedTarget() {
  return CheckButton(Button::Up) || CheckButton(Button::Down) ||
         CheckButton(Button::Left) || CheckButton(Button::Right);
}

// Runs ahead of the engine's own target input, so a click in the same frame
// confirms whoever the pointer had just moved to.
void UpdateBattleTargetHover(PPCContext &ctx, u8 *base, u32 sceneVA) {
  // Reaching this function at all means the player is being asked who to hit,
  // and a cancel from here has somewhere to go, so escape and right-click have
  // to read as one and the arrow keys have to reach the pad. Ahead of every
  // gate below, which are about the pointer rather than about the keyboard, and
  // ahead of the region test, since an action that covers the whole field still
  // takes a confirm and a cancel.
  MenuMouse::Get().MarkInputOwned();

  const u32 shape = mem::try_field<u32>(sceneVA, kScene_TargetShape);
  if (!shape || mem::try_field<u32>(shape, kShape_Enabled) == 0)
    return;
  const u32 mode = mem::try_field<u32>(shape, kShape_Mode);
  if (mode == 0 || mode == kShapeEverything)
    return;

  if (PadMovedTarget()) {
    MenuMouse::Get().SetMouseHasCursor(false);
    return;
  }
  if (!MenuMouse::Get().PointerActive())
    return;

  f32 pointerX = 0.0f;
  f32 pointerY = 0.0f;
  if (!CursorInMenuSpace(pointerX, pointerY))
    return;

  const auto *selection =
      mem::try_at<const TargetSel_t>(sceneVA + kScene_Selection);
  if (!selection)
    return;
  const u32 slot = selection->commandSlot;
  if (slot >= VectorCount(sceneVA, kScene_CommandsBegin, kScene_CommandsEnd,
                          kCommandStride))
    return;
  const u32 entry =
      mem::try_field<u32>(sceneVA, kScene_CommandsBegin) + slot * kCommandStride;
  const u32 groupCount =
      VectorCount(entry, kEntry_GroupsBegin, kEntry_GroupsEnd, kGroupStride);
  if (!groupCount)
    return;

  const u32 targetGroup = mem::try_field<u32>(sceneVA, kScene_TargetGroup);
  const u32 visualRender = mem::load<u32>(kVisualRenderVA);
  if (!targetGroup || !visualRender)
    return;

  // Every guest call below runs on a frame of its own, so the register state
  // the original is about to read is left exactly as it arrived.
  rex::CallFrame frame(ctx);
  rex::ppc::stack_guard guard(frame.ctx);
  alignas(8) u8 zeroed[kScratchBytes]{};
  const u32 scratch =
      rex::ppc::stack_push(frame.ctx, base, zeroed, kScratchBytes);

  auto *scratchSel = mem::try_at<TargetSel_t>(scratch + kScratch_Sel);
  auto *world = mem::try_at<GuestVec3_t>(scratch + kScratch_World);
  const auto *screen = mem::try_at<const GuestVec3_t>(scratch + kScratch_Screen);
  if (!scratchSel || !world || !screen)
    return;

  Candidate best{};
  bool found = false;

  for (u32 g = 0; g < groupCount; ++g) {
    const u32 group =
        mem::try_field<u32>(entry, kEntry_GroupsBegin) + g * kGroupStride;
    const u32 memberCount = VectorCount(group, kGroup_MembersBegin,
                                        kGroup_MembersEnd, kMemberStride);
    const u32 members =
        memberCount < kMembersPerGroup ? memberCount : kMembersPerGroup;
    for (u32 m = 0; m < members; ++m) {
      const u32 member =
          mem::try_field<u32>(group, kGroup_MembersBegin) + m * kMemberStride;
      const u32 actor = mem::try_field<u32>(member, kMember_Actor);
      if (!actor)
        continue;

      // Ask the engine, exactly as bdBattleTargetStepGroup does: a fresh copy of
      // the live selection with this cell written into it, which
      // bdBattleTargetIsSelectable then either accepts or refuses.
      *scratchSel = *selection;
      scratchSel->actor = actor;
      scratchSel->group = g;
      scratchSel->member = m;
      scratchSel->part = -1;
      if (!TargetIsSelectable(frame, base, targetGroup, actor,
                              scratch + kScratch_Sel))
        continue;

      const auto *pos = mem::try_at<const GuestVec3_t>(actor + kActor_WorldPos);
      if (!pos)
        continue;
      *world = *pos;
      WorldToScreenPos3(frame, base, visualRender, 0, scratch + kScratch_Screen,
                        scratch + kScratch_World, 0);

      Candidate c{};
      c.actor = actor;
      c.group = g;
      c.member = m;
      c.x = screen->x;
      c.y = screen->y;
      c.z = screen->z;
      const f32 dx = c.x - pointerX;
      const f32 dy = (c.y - kBodyRise) - pointerY;
      c.distance = std::sqrt(dx * dx + dy * dy);

      if (c.z >= kFrontOfCamera || c.distance > kPickRadius)
        continue;
      if (!found || c.distance < best.distance) {
        best = c;
        found = true;
      }
    }
  }

  if (!found || best.actor == u32(selection->actor))
    return;

  *scratchSel = *selection;
  scratchSel->actor = best.actor;
  scratchSel->group = best.group;
  scratchSel->member = best.member;
  scratchSel->part = -1;
  if (!TargetIsSelectable(frame, base, targetGroup, best.actor,
                          scratch + kScratch_Sel))
    return;

  auto *live = mem::try_at<TargetSel_t>(sceneVA + kScene_Selection);
  if (!live)
    return;
  *live = *scratchSel;

  // The highlight list is rebuilt only where the engine's own movement
  // succeeds, so a pointer move has to ask for it or the marks stay on the
  // previous target. Zero clears the list first, the value the pad passes.
  TargetRefreshHighlights(frame, base, sceneVA, 0);
}

} // namespace

} // namespace bd::engine

// The battle's target step, called once per frame from
// bdBattleSceneActionDispatch for as long as the player is choosing who to hit.
// It polls buttons 0 through 23 itself, so running ahead of it leaves the
// selection already moved when it reads the confirm button. r3 is the scene.
REX_HOOK_RAW(bdBattleTargetSelectInput) {
  bd::engine::UpdateBattleTargetHover(ctx, base, ctx.r3.u32);
  __imp__bdBattleTargetSelectInput(ctx, base);
}
