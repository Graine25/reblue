/**
 * @file    engine/menus/camp_settings.h
 * @brief   reblue's settings screen hosted on the stock camp Config task.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#pragma once

#include "engine/menus/config_menu.h"

#include <rex/ppc/func.h>
#include <rex/types.h>

namespace bd::engine {

// The stock Camp::Config task keeps running: it owns the header, the exit path
// and the four Type A-D controller diagrams, which are already built and
// already indexed by control type. reblue takes over only the two row pages.
class CampSettings {
public:
  static CampSettings &Get();

  // True when reblue drew this frame and the stock update must be skipped.
  bool Update(PPCContext &ctx, u8 *base, u32 taskAddr);

  // Once per guest tick. Loads the menu when a camp opens, so the CSV and its
  // textures load while the user is still on the camp menus and the settings
  // screen swaps in the moment the stock pages reach a row state.
  void Tick();

private:
  CampSettings() = default;
  CampSettings(const CampSettings &) = delete;
  CampSettings &operator=(const CampSettings &) = delete;

  void Open(u32 taskAddr);
  void Park(u32 taskAddr);
  void Close();
  void HidePrompts();
  void RestorePrompts();

  ConfigMenu menu_;
  bool open_ = false;
  // One load per camp visit. The latch is the camp task's identity, so a new
  // camp, even at a reused address, creates fresh over the handles the old
  // one took down with its task tree.
  u32 camp_task_ = 0;
  u64 camp_uid_ = 0;
  // Open runs every frame the stock screen is up, so the load failure is
  // reported once per camp task rather than once per frame.
  u32 warned_task_ = 0;
};

} // namespace bd::engine
