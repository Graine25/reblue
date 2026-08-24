/**
 * @file    engine/d2anime/d2anime_menu.h
 * @brief   Wrapper over the guest AnimeMenu objects.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause - see LICENSE
 */
#pragma once

#include "core/task_layout.h"
#include "engine/d2anime/d2anime_types.h"

#include <cstddef>
#include <functional>

#include <rex/types.h>

namespace bd::engine {

class D2AnimeMenu {
public:
  D2AnimeMenu() = default;
  explicit D2AnimeMenu(u32 guestAddr);
  // A menu has no identity of its own, so it borrows the task that parsed it.
  D2AnimeMenu(u32 guestAddr, const bd::TaskRef &owner);

  AnimeMenu_t *operator->() { return ptr_.host_address(); }
  const AnimeMenu_t *operator->() const { return ptr_.host_address(); }
  u32 guest_address() const { return ptr_.guest_address(); }
  explicit operator bool() const { return ptr_ && (!owned_ || owner_); }

  void SetActive(bool active);

  // The cursor arrow alone, for an active menu whose rows sit somewhere other
  // than where its grid says they do. SetActive(true) shows it again.
  void SetCursorShown(bool shown);

  // Adds/removes templates from the scene tree, and hides overlapping menus
  // that otherwise crash.
  void SetVisible(bool visible);

  void AttachCursor();

  int CursorIndex() const;

  void SetCursorIndex(int index);

  // The row under the pointer and where the pointer sits along it, in the row
  // template's own coordinates. False when the pointer is over no row, which
  // is also what a pad press reports.
  bool PointerRowX(int &index, f32 &x) const;

  // The same reading for a row already chosen, whatever the pointer's height.
  // A drag latches onto its row this way rather than losing it the moment the
  // pointer slips off a 34px band.
  bool RowPointerX(int index, f32 &x) const;

  u32 EnableColor() const;
  u32 DisableColor() const;

  // Callback receives (index, varBagGuestAddr).
  void ForEachTemplate(std::function<void(int, u32)> cb) const;

  // Absolute entry index shown in a visible slot. Template slots are a scroll
  // window over the entry list, so slot i displays entry ItemDataIndex(i).
  // Returns 'slot' when the item data vector is not populated.
  int ItemDataIndex(int slot) const;

  // ForEachTemplate with the entry index behind each slot resolved. The index
  // can be past the end of the list, as a screen writing blanks into the
  // trailing slots needs to see.
  using RowFn = std::function<void(int slot, int index, u32 varBag)>;
  void ForEachSlot(const RowFn &fn) const;

  // ForEachSlot restricted to the slots showing a real entry, so a scrolled
  // long list tints and checks the rows it is actually drawing.
  void ForEachRow(size_t count, const RowFn &fn) const;

  void SetItemEnabled(int index, bool enabled);

  // A row whose enabled flag drives both its tint and which of the two
  // checkbox cells draws. Every list that marks rows on or off shares it.
  void SetToggleRow(int slot, u32 varBag, bool enabled, u32 color);

  void AddEntryData(int index, bool enabled);

  // How many entries the widget has. The cursor's upper bound and the drawn
  // cell count both come off this, so it is what makes a row reachable.
  int EntryCount() const;

  // Rows the grid is laid out for, which the CSV fixes and a screen adding a
  // row of its own has to raise.
  int RowCount() const;

  // Appends a row to the grid. The engine derives the row stride from the
  // extent and the row count, so the extent grows by one stride to leave the
  // rows already there where they are.
  void GrowGridByOneRow();

  // AddEntryData over a whole list. A CSV widget declares defaultItem=0, which
  // leaves it with no entries at all, so a screen has to seed them itself or
  // the engine renders into a null target. Seeding is also what lets a list
  // longer than its window scroll rather than lose its tail.
  void SeedEntries(size_t count, bool enabled = true);

private:
  rex::MappedPtr<AnimeMenu_t> ptr_;
  bd::TaskRef owner_;
  bool owned_ = false;
};

// One menu's cursor, remembered across frames. A list screen rebuilds its
// detail panel and row description only when the cursor moves somewhere new,
// while the row visuals go in every frame regardless.
class D2AnimeCursor {
public:
  // A list whose entry vector the engine may not have built reports a cursor
  // no count can be checked against, so it passes kUnbounded and takes it.
  static constexpr size_t kUnbounded = 0;

  void Poll(const D2AnimeMenu &menu, size_t count,
            const std::function<void(int index)> &fn);
  void Poll(const D2AnimeMenu &menu, const std::function<void(int index)> &fn) {
    Poll(menu, kUnbounded, fn);
  }

  // Makes the next Poll report wherever the cursor is, for a screen changing
  // state: the panels below it belong to the old list.
  void Reset() { last_ = -1; }

private:
  int last_ = -1;
};

} // namespace bd::engine
