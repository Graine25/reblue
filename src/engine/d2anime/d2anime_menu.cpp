/**
 * @file    engine/d2anime/d2anime_menu.cpp
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause - see LICENSE
 */
#include "engine/d2anime/d2anime_menu.h"
#include "core/memory_helpers.h"
#include "engine/d2anime/anime_hittest.h"
#include "engine/d2anime/anime_vars.h"
#include "engine/d2anime/d2anime_task.h"

#include <rex/hook.h>
#include <rex/types.h>

REX_IMPORT(__imp__AnimeMenu_attachToParent, MenuAttachToParent, void(u32));
REX_IMPORT(__imp__AnimeMenu_SetVisibleAndPlay, MenuSetVisibleAndPlay,
           void(u32, u32));
REX_IMPORT(__imp__AnimeMenu_AddEntryData, MenuAddEntryData, u32(u32, u32, u32));
REX_IMPORT(__imp__AnimeMenu_AnimateCursor, MenuAnimateCursor, void(u32, u32));

namespace bd::engine {

D2AnimeMenu::D2AnimeMenu(u32 guestAddr) {
  ptr_ =
      rex::MappedPtr<AnimeMenu_t>(mem::at<AnimeMenu_t>(guestAddr), guestAddr);
}

void D2AnimeMenu::SetActive(bool active) {
  if (!ptr_)
    return;
  ptr_->activeFlag = active ? 1 : 0;
  ptr_->deselectAll = active ? 0 : 1;
  ptr_->cursorShown = active ? 1 : 0;
  ptr_->needsRebuild = 1;
}

void D2AnimeMenu::SetCursorShown(bool shown) {
  if (!ptr_)
    return;
  ptr_->cursorShown = shown ? 1 : 0;
}

void D2AnimeMenu::SetVisible(bool visible) {
  if (!ptr_)
    return;
  MenuSetVisibleAndPlay(ptr_.guest_address(), visible ? 1 : 0);
}

void D2AnimeMenu::AttachCursor() {
  if (!ptr_)
    return;
  MenuAttachToParent(ptr_.guest_address());
}

int D2AnimeMenu::CursorIndex() const {
  if (!ptr_)
    return 0;
  return static_cast<int>(static_cast<u32>(ptr_->cursorIndex));
}

void D2AnimeMenu::SetCursorIndex(int index) {
  if (!ptr_)
    return;
  ptr_->cursorIndex = static_cast<u32>(index);
  ptr_->needsRebuild = 1;
  MenuAnimateCursor(ptr_.guest_address(), 0);
}

bool D2AnimeMenu::PointerRowX(int &index, f32 &x) const {
  if (!ptr_)
    return false;
  const auto *menu = mem::try_at<const AnimeMenu_t>(ptr_.guest_address());
  return menu && MenuCellPointerX(*menu, index, x);
}

bool D2AnimeMenu::RowPointerX(int index, f32 &x) const {
  if (!ptr_)
    return false;
  const auto *menu = mem::try_at<const AnimeMenu_t>(ptr_.guest_address());
  return menu && MenuRowPointerX(*menu, index, x);
}

u32 D2AnimeMenu::EnableColor() const {
  if (!ptr_)
    return 0xFFFFFFFF;
  return static_cast<u32>(ptr_->enableColor);
}

u32 D2AnimeMenu::DisableColor() const {
  if (!ptr_)
    return 0x7F7F7FFF;
  return static_cast<u32>(ptr_->disableColor);
}

void D2AnimeMenu::ForEachTemplate(std::function<void(int, u32)> cb) const {
  if (!ptr_)
    return;
  const u32 count = ptr_->templates.size();
  for (u32 i = 0; i < count; ++i) {
    D2AnimeTask tpl(ptr_->templates[i]);
    if (!tpl)
      continue;
    cb(static_cast<int>(i), tpl.VarBag());
  }
}

int D2AnimeMenu::ItemDataIndex(int slot) const {
  if (!ptr_)
    return slot;
  auto *item =
      mem::at<AnimeItemData_t>(ptr_->itemData[static_cast<u32>(slot)]);
  return item ? static_cast<int>(static_cast<u32>(item->index)) : slot;
}

void D2AnimeMenu::ForEachSlot(const RowFn &fn) const {
  ForEachTemplate(
      [&](int slot, u32 vb) { fn(slot, ItemDataIndex(slot), vb); });
}

void D2AnimeMenu::ForEachRow(size_t count, const RowFn &fn) const {
  ForEachSlot([&](int slot, int index, u32 vb) {
    if (index >= 0 && index < static_cast<int>(count))
      fn(slot, index, vb);
  });
}

void D2AnimeMenu::SetItemEnabled(int index, bool enabled) {
  if (!ptr_)
    return;
  auto *item =
      mem::at<AnimeItemData_t>(ptr_->itemData[static_cast<u32>(index)]);
  if (!item)
    return;
  item->enabled = enabled ? 1 : 0;
}

void D2AnimeMenu::SetToggleRow(int slot, u32 varBag, bool enabled, u32 color) {
  SetItemEnabled(slot, enabled);
  VarBagSetColor(varBag, "Color", color);
  VarBagSetFloat(varBag, "ChkOn", enabled ? 1.0 : -1.0);
  VarBagSetFloat(varBag, "ChkOff", enabled ? -1.0 : 1.0);
}

void D2AnimeMenu::AddEntryData(int index, bool enabled) {
  if (!ptr_)
    return;
  MenuAddEntryData(ptr_.guest_address(), static_cast<u32>(index),
                   enabled ? 1 : 0);
}

int D2AnimeMenu::EntryCount() const {
  return ptr_ ? static_cast<int>(ptr_->entryData.size()) : 0;
}

int D2AnimeMenu::RowCount() const {
  return ptr_ ? static_cast<int>(static_cast<u32>(ptr_->gridDimX)) : 0;
}

void D2AnimeMenu::GrowGridByOneRow() {
  if (!ptr_)
    return;
  const int rows = RowCount();
  const float itemH = ptr_->itemH;
  const float extentH = ptr_->extentH;
  const float gap =
      rows > 1 ? (extentH - itemH * rows) / static_cast<float>(rows - 1) : 0.0f;

  const int newRows = rows + 1;
  ptr_->gridDimX = static_cast<u32>(newRows);
  ptr_->extentH = itemH * newRows + gap * (newRows - 1);
}

void D2AnimeMenu::SeedEntries(size_t count, bool enabled) {
  for (size_t i = 0; i < count; ++i)
    AddEntryData(static_cast<int>(i), enabled);
}

void D2AnimeCursor::Poll(const D2AnimeMenu &menu, size_t count,
                         const std::function<void(int index)> &fn) {
  const int cursor = menu.CursorIndex();
  if (cursor == last_)
    return;
  if (count != kUnbounded && (cursor < 0 || cursor >= static_cast<int>(count)))
    return;
  last_ = cursor;
  fn(cursor);
}

} // namespace bd::engine
