/**
 * @file    core/task_layout.h
 * @brief   Base layout every engine task shares, plus the kill sequence.
 * @license BSD 3-Clause, see LICENSE
 */
#pragma once

#include <cstddef>

#include <rex/types.h>

#include "core/memory_helpers.h"

namespace bd {

// Shared by the config menu, the system message popup and KillTask below.
// D2AnimeTask_t/AnimeMenu_t independently pin the same +0x58/+0x60 offsets for
// their own views of this object.
struct TaskBase_t {
  /* 0x000 */ be_u32 vtable;
  /* 0x004 */ u8 _pad004[0x0C];
  /* 0x010 */ be_u64 taskUID;
  /* 0x018 */ u8 _pad018[0x20];
  // The child list TaskBase__ctor threads a new task onto its parent through.
  /* 0x038 */ be_u32 firstChild;
  /* 0x03C */ be_u32 nextSibling;
  /* 0x040 */ u8 _pad040[0x08];
  /* 0x048 */ be_u32 notifyParent; // notification-parent Task*
  /* 0x04C */ u8 _pad04C[0x04];
  /* 0x050 */ be_u64 notifyParentUID;
  /* 0x058 */ be_u32 flags; // 0xDEAD0000 ORed in marks the task dead
  /* 0x05C */ u8 _pad05C[0x04];
  /* 0x060 */ be_u32 destroyFlag; // 1 requests destruction
  /* 0x064 */ u8 _pad064[0x3C];
  /* 0x0A0 */ be_u32
      notifyChild; // notification-child Task* held by the parent.
                   // Must be cleared before killing the child.
                   // Distinct from AnimeMenu_t+0xA0 cursorTask.
};
static_assert(offsetof(TaskBase_t, vtable) == 0x000);
static_assert(offsetof(TaskBase_t, taskUID) == 0x010);
static_assert(offsetof(TaskBase_t, firstChild) == 0x038);
static_assert(offsetof(TaskBase_t, nextSibling) == 0x03C);
static_assert(offsetof(TaskBase_t, notifyParent) == 0x048);
static_assert(offsetof(TaskBase_t, notifyParentUID) == 0x050);
static_assert(offsetof(TaskBase_t, flags) == 0x058);
static_assert(offsetof(TaskBase_t, destroyFlag) == 0x060);
static_assert(offsetof(TaskBase_t, notifyChild) == 0x0A0);

inline constexpr u32 kTaskDeadSentinel = 0xDEAD0000;

// Mark a task dead (flags |= kTaskDeadSentinel, destroyFlag = 1).
inline void KillTask(u32 taskAddr) {
  auto *task = mem::at<TaskBase_t>(taskAddr);
  if (!task)
    return;
  task->destroyFlag = 1u;
  task->flags = static_cast<u32>(task->flags) | kTaskDeadSentinel;
}

// The read side of KillTask. Checked, because a task pointer read off a chain
// may already be freed and reused, which is the case this exists to catch.
inline bool IsDeadTask(u32 taskAddr) {
  const u32 flags = mem::try_field<u32>(taskAddr, offsetof(TaskBase_t, flags));
  return (flags & kTaskDeadSentinel) == kTaskDeadSentinel;
}

// Live = non-null and not DEAD-flagged. Use before dereferencing any task.
inline bool LiveTask(u32 taskAddr) { return taskAddr && !IsDeadTask(taskAddr); }

// The child list, for a walk over one task's children. A child pointer read
// off the chain may already be freed, so these are the checked accessors.
inline u32 FirstChild(u32 taskAddr) {
  return mem::try_field<u32>(taskAddr, offsetof(TaskBase_t, firstChild));
}

inline u32 NextSibling(u32 taskAddr) {
  return mem::try_field<u32>(taskAddr, offsetof(TaskBase_t, nextSibling));
}

// What kind of task it is, which is all a walk has to tell them apart by.
inline u32 TaskVtable(u32 taskAddr) {
  return mem::try_field<u32>(taskAddr, offsetof(TaskBase_t, vtable));
}

} // namespace bd
