/**
 * @file    engine/d2anime/cmdselect.cpp
 * @license BSD 3-Clause, see LICENSE
 */
#include "engine/d2anime/cmdselect.h"


#include "engine/d2anime/anime_hittest.h"

#include <rex/types.h>

namespace bd::engine {

namespace {

// Width a scrollbar takes off the list, hard-coded in Visual__DrawGridItems and
// again in CommandSelectTask_RepositionCursor. Only a list that divides its
// window loses it, one with an item rect places rows itself.
constexpr f32 kScrollbarExtent = 24.0f;


// Where one row of a list sits, resolved from whichever of the two layouts the
// list uses. Cells are laid at origin + index*stride and are cell-sized, which
// is narrower than the stride wherever a list leaves gaps.
struct CmdSelectGrid {
  f32 originX;
  f32 originY;
  f32 cellW;
  f32 cellH;
  f32 strideX;
  f32 strideY;
  int rows;
  int cols;
};

bool CmdSelectGridOf(const CommandSelectTask_t &task, CmdSelectGrid &out) {
  const int rows = int(u16(task.gridRows));
  const int cols = int(u16(task.gridCols));
  if (rows <= 0 || cols <= 0)
    return false;

  const auto *window = mem::try_at<const CmdSelectWindow_t>(u32(task.window));
  if (!window)
    return false;

  out.rows = rows;
  out.cols = cols;

  const auto *item = mem::try_at<const CmdSelectItem_t>(u32(task.item));
  const auto *stride = mem::try_at<const CmdSelectStride_t>(u32(task.stride));
  if (item && stride) {
    // The row box Visual__DrawGridItems highlights, which is the one on screen.
    // No inset here: the inset moves the cursor sprite, not the rows.
    out.originX = f32(window->x) + f32(item->x);
    out.originY = f32(window->y) + f32(item->y);
    out.cellW = item->w;
    out.cellH = item->h;
    out.strideX = stride->x;
    out.strideY = stride->y;
    return out.cellW > 0.0f && out.cellH > 0.0f;
  }

  // Otherwise the window is simply divided into the grid, which is how the
  // yes/no popup lays its two answers out.
  const f32 inset = window->inset;
  f32 innerW = f32(window->w) - inset * 2.0f;
  f32 innerH = f32(window->h) - inset * 2.0f;
  if (u32(task.pageMode) == 0 &&
      task.entries.size() > u32(rows) * u32(cols)) {
    if (u32(task.orientation) != 0)
      innerH -= kScrollbarExtent;
    else
      innerW -= kScrollbarExtent;
  }
  if (innerW <= 0.0f || innerH <= 0.0f)
    return false;

  out.originX = f32(window->x) + inset;
  out.originY = f32(window->y) + inset;
  out.cellW = innerW / f32(cols);
  out.cellH = innerH / f32(rows);
  out.strideX = out.cellW;
  out.strideY = out.cellH;
  return true;
}

} // namespace

bool CmdSelectHasFocus(const CommandSelectTask_t &task) {
  const u32 f = task.flags;
  if ((f & u32(MenuFlag::Active)) == 0)
    return false;
  if ((f & (u32(MenuFlag::Locked) | u32(MenuFlag::Animating))) != 0)
    return false;
  if (u32(task.inputEnabled) == 0)
    return false;
  // The entry test is CommandSelectTask_CheckConfirm's, and it is what keeps a
  // list built but never filled from claiming the pointer.
  return !task.entries.empty();
}

bool CmdSelectCellAt(const CommandSelectTask_t &task, f32 x, f32 y,
                     int &index) {
  CmdSelectGrid grid{};
  if (!CmdSelectGridOf(task, grid))
    return false;

  const f32 localX = x - grid.originX;
  const f32 localY = y - grid.originY;
  if (localX < 0.0f || localY < 0.0f)
    return false;

  // An axis with one cell on it has no stride, and the battle command list is
  // exactly that: five rows in one column, so CommandSelectTask_SetItemStride
  // leaves its x at zero. Dividing by a stride is correct only where there is
  // a second cell to reach.
  const auto cellOn = [](f32 local, f32 stride, int count) {
    return count > 1 && stride > 0.0f ? int(local / stride) : 0;
  };
  const int col = cellOn(localX, grid.strideX, grid.cols);
  const int row = cellOn(localY, grid.strideY, grid.rows);
  if (row >= grid.rows || col >= grid.cols)
    return false;

  // Inside the band but past the cell itself is a gap between rows.
  if (localX - f32(col) * grid.strideX > grid.cellW)
    return false;
  if (localY - f32(row) * grid.strideY > grid.cellH)
    return false;

  // Orientation picks how Visual__DrawGridItems numbers the page, so it has to
  // pick how the page is read back too.
  const bool columnMajor = u32(task.orientation) != 0;
  const int page = columnMajor ? col * grid.rows + row : row * grid.cols + col;
  if (page < 0 || page >= int(task.visible.size()))
    return false;

  // CommandSelectTask_RebuildVisibleItems fills the page starting one whole
  // page's minor dimension per scroll step, which is the same conversion
  // CommandSelectTask_RecalcCursorGrid inverts.
  const int pageStride = columnMajor ? grid.rows : grid.cols;
  const int absolute = int(u16(task.scrollOffset)) * pageStride + page;
  if (absolute >= int(task.entries.size()))
    return false;

  index = absolute;
  return true;
}

int CmdSelectRowEdgeDirection(const CommandSelectTask_t &task) {
  f32 x = 0.0f;
  f32 y = 0.0f;
  if (!CursorInMenuSpace(x, y))
    return 0;

  const auto *window = mem::try_at<const CmdSelectWindow_t>(u32(task.window));
  if (!window)
    return 0;

  const f32 left = window->x;
  const f32 top = window->y;
  if (x < left || x > left + f32(window->w))
    return 0;
  if (y < top)
    return -1;
  if (y > top + f32(window->h))
    return 1;
  return 0;
}

int CmdSelectRowStep(const CommandSelectTask_t &task) {
  if (u32(task.orientation) != 0)
    return 1;
  const int cols = int(u16(task.gridCols));
  return cols > 0 ? cols : 0;
}


} // namespace bd::engine
