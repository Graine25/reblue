/**
 * @file    engine/d2anime/sysmes.h
 * @brief   Wrapper over the engine's yes/no confirmation popup.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause - see LICENSE
 */
#pragma once

#include "core/task_layout.h"

#include <rex/types.h>

namespace bd::engine {

// Wraps the engine's SelMesWinTask, the yes/no confirmation popup. The engine
// handles input.
class SysMesConfirm {
public:
  // Spawn a yes/no popup as a child of parentTask. Up to 3 UTF-8 question
  // lines. a1/a2 are answer labels, null for the catalog's own yes/no.
  bool Create(u32 parentTask, const char *q1, const char *q2 = "",
              const char *q3 = "", const char *a1 = nullptr,
              const char *a2 = nullptr, int defaultSel = 1);

  // True once the user has made a choice (confirm or cancel).
  bool Poll() const;

  bool Confirmed() const;
  int SelectedAnswer() const;

  void Kill();

  // Forget the handle without touching guest memory: when the popup's parent
  // task dies, the engine frees the child too, and a later Kill() would write
  // DEAD flags into freed (possibly reused) guest heap.
  void Drop() { task_.Reset(); }

private:
  bd::TaskRef task_;
};

} // namespace bd::engine
