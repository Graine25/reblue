/**
 * @file    engine/achievements/achievement_list.h
 * @brief   Catalog snapshot for the achievement viewer.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#pragma once

#include <string>
#include <vector>

#include <rex/types.h>

namespace bd::engine {

struct AchievementRow {
  u32 id = 0;
  std::string label;
  std::string
      desc; // description when unlocked, unachieved_description when not
  u32 gamerscore = 0;
  bool unlocked = false;
  u64 unlockedAt = 0; // Windows FILETIME, 0 while locked
};

struct AchievementSummary {
  int unlocked = 0;
  int total = 0;
  int earnedScore = 0;
  int totalScore = 0;
};

// Re-reads the catalog and unlock state, most recently unlocked first. Call
// once when the viewer opens. The list is stable while it is open because
// nothing unlocks from inside a menu. Display order only, since icons are keyed
// by achievement id, so re-sorting never moves them.
const std::vector<AchievementRow> &RefreshAchievementList();

const std::vector<AchievementRow> &GetAchievementList();

AchievementSummary GetAchievementSummary();

} // namespace bd::engine
