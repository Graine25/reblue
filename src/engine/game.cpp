/**
 * @file    engine/game.cpp
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 * @license   BSD 3-Clause License
 */
#include "engine/game.h"

#include <cstddef>

#include "core/memory_helpers.h"
#include "core/task_layout.h"
#include "engine/state_layout.h"

namespace bd::engine {

namespace {

constexpr u32 kLoaderTaskVA = 0x82DC97F4; // asset-slot manager
constexpr u32 kMindowsActivePanelVA =
    0x827A7D68; // Mindows panel, NOT the camp menu

// GameTask [addr::kGameTask]
constexpr u32 kGameTask_ShutdownFlag = 0x8C;
constexpr u32 kShutdownFieldActive = 0;

constexpr u32 kFieldStateTransition = 5;

// Loader [kLoaderTaskVA]
constexpr u32 kLoader_NowLoading = 0x88; // non-null => loading screen up
constexpr u32 kLoader_SlotArray = 0x90;  // INLINE array base
constexpr u32 kLoader_SlotStride = 124;
constexpr int kLoader_SlotScan = 8;

// SequenceControl [addr::kSequenceControl]
constexpr u32 kSeq_CurrentModule = 0x70;

constexpr u32 kNoFieldState = ~0u;

// Any asset slot mid-load, mirroring the per-slot test in
// bdAssetSlotCheckLoaded: (state-1) <= 2. Bounded scan.
bool AnySlotLoading(u32 loaderEA) {
  if (!loaderEA)
    return false;
  const u32 base = loaderEA + kLoader_SlotArray;
  for (int i = 0; i < kLoader_SlotScan; ++i) {
    const u32 state =
        bd::mem::try_load<u32>(base + static_cast<u32>(i) * kLoader_SlotStride);
    if (state - 1u <= 2u)
      return true;
  }
  return false;
}

} // namespace

Game &Game::Get() {
  static Game instance;
  return instance;
}

const char *ToString(EngineMode mode) {
  switch (mode) {
  case EngineMode::TitleOrMenu:
    return "TitleOrMenu";
  case EngineMode::FieldActive:
    return "FieldActive";
  case EngineMode::FieldTransition:
    return "FieldTransition";
  case EngineMode::Battle:
    return "Battle";
  case EngineMode::Loading:
    return "Loading";
  case EngineMode::Unknown:
  default:
    return "Unknown";
  }
}

bool Game::IsReady() const { return bd::mem::ready(); }

bool Game::FieldSessionLive() const {
  return bd::mem::try_load<u32>(addr::kGameTask) != 0;
}

bool Game::FieldControllerLive() const {
  return bd::mem::try_load<u32>(addr::kFieldSceneCtl) != 0;
}

bool Game::FieldGameplayActive() const {
  const u32 gt = bd::mem::try_load<u32>(addr::kGameTask);
  return gt && mem::try_field<u32>(gt, kGameTask_ShutdownFlag) ==
                   kShutdownFieldActive;
}

u32 Game::FieldState() const {
  const u32 fsc = bd::mem::try_load<u32>(addr::kFieldSceneCtl);
  return fsc ? mem::try_field<u32>(fsc, offsetof(FieldSceneCtl_t, fieldState))
             : kNoFieldState;
}

bool Game::IsLoading() const {
  return AnySlotLoading(bd::mem::try_load<u32>(kLoaderTaskVA));
}

bool Game::LoadingScreenUp() const {
  const u32 loader = bd::mem::try_load<u32>(kLoaderTaskVA);
  return loader && mem::try_field<u32>(loader, kLoader_NowLoading) != 0;
}

bool Game::MindowsPanelActive() const {
  return bd::mem::try_load<u32>(kMindowsActivePanelVA) != 0;
}

u32 Game::CurrentModuleAddress() const {
  const u32 seq = bd::mem::try_load<u32>(addr::kSequenceControl);
  return seq ? mem::try_field<u32>(seq, kSeq_CurrentModule) : 0;
}

engine::Stage Game::Stage() const { return Field().Stage(); }

EngineMode Game::Mode() const {
  if (!IsReady())
    return EngineMode::Unknown;
  if (Battle().IsActive())
    return EngineMode::Battle;
  if (IsLoading() || LoadingScreenUp())
    return EngineMode::Loading;
  if (FieldGameplayActive())
    return FieldState() == kFieldStateTransition ? EngineMode::FieldTransition
                                                 : EngineMode::FieldActive;
  return EngineMode::TitleOrMenu;
}

} // namespace bd::engine
