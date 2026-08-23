/**
 * @file    engine/achievements/achievements.h
 * @brief   reblue-authored achievements, unlocked from engine events.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#pragma once

namespace bd::engine {

class Achievements {
public:
  // Subscribes every condition to the events that can satisfy it. Called once
  // at startup.
  static void Init();

  // Repeatable: the SDK clears the store when it loads the title's own XDBF
  // catalog at kernel boot, so readers assert the catalog rather than trust an
  // earlier registration.
  static void Register();
};

} // namespace bd::engine
