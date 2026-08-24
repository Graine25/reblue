/**
 * @file    engine/d2anime/d2anime_task.h
 * @brief   Wrapper over the guest D2AnimeTask objects.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause - see LICENSE
 */
#pragma once

#include "core/task_layout.h"
#include "engine/d2anime/d2anime_menu.h"
#include "engine/d2anime/d2anime_types.h"

#include <string_view>

#include <rex/types.h>

namespace bd::engine {

class D2AnimeTask {
public:
  // How a loaded task first shows. The guest's own screen loaders start their
  // CSVs hidden and reveal them once parsed, and Load follows suit: WhenReady
  // shows the task on the tick its parse completes, Held leaves the reveal to
  // the owner.
  enum class Reveal { WhenReady, Held };

  D2AnimeTask() = default;
  explicit D2AnimeTask(u32 guestAddr);

  static D2AnimeTask Load(u32 parentTask, const char *csvPath,
                          Reveal reveal = Reveal::WhenReady);

  // Reveals every WhenReady task whose parse has finished. Once per guest tick.
  static void Tick();

  D2AnimeTask_t *operator->() { return ref_.At<D2AnimeTask_t>(); }
  const D2AnimeTask_t *operator->() const {
    return ref_.At<const D2AnimeTask_t>();
  }
  u32 guest_address() const { return ref_.Address(); }
  explicit operator bool() const { return static_cast<bool>(ref_); }

  void SetVisibleAndPlay(bool visible);
  bool IsVisible() const;

  // Timeline controls, the pair Camp::Diary::MainTask's Exit uses to run a
  // transition screen backwards: seek to its last frame, then play at a
  // negative rate. SetAnimTime recurses into child anime, so a plain write to
  // animFrame would leave every nested d2anime running forwards.
  void SetAnimTime(float frame);
  void SetAnimSpeed(float speed);
  float AnimTime() const;
  float AnimSpeed() const;  // negative plays the timeline backwards
  float AnimLength() const; // -1 for an anime the CSV gives no end

  // The timeline reached that end. Only an anime with a length ever reports
  // it, so a caller driving an endless one needs a bound of its own.
  bool IsAnimFinished() const;

  void Kill();

  bool IsReady() const;

  // Find a menu by its CSV-defined name (e.g. "SltSection", "ModList").
  D2AnimeMenu FindMenuByName(const char *name) const;

  // Guest address of the task's embedded AnimeData, which is where its
  // AnimeVarBag lives. Zero when the task does not resolve.
  u32 VarBag() const;

  // VarBag helpers.
  void SetFloat(const char *name, double value);
  // Engine tokens (window types, texture paths) only. User-facing text goes
  // through SetText, which does not pass through the Shift-JIS widener.
  void SetString(const char *name, const char *value);
  void SetText(const char *name, std::string_view utf8);

private:
  bd::TaskRef ref_;
};

} // namespace bd::engine
