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

// An address whose flags field no longer resolves counts as dead: a zero
// fallback would otherwise read back as a live task carrying no sentinel.
inline bool IsDeadTask(u32 taskAddr) {
  if (!taskAddr)
    return true;
  const auto *flags =
      mem::try_at<const be_u32>(taskAddr + offsetof(TaskBase_t, flags));
  return !flags ||
         (static_cast<u32>(*flags) & kTaskDeadSentinel) == kTaskDeadSentinel;
}

// Necessary before dereferencing any task, never sufficient on its own for one
// remembered across frames. See TaskRef.
inline bool LiveTask(u32 taskAddr) { return !IsDeadTask(taskAddr); }

// What the engine's own notifyParentUID compares against. Zero for an address
// that does not resolve.
inline u64 TaskUID(u32 taskAddr) {
  return mem::try_field<u64>(taskAddr, offsetof(TaskBase_t, taskUID));
}

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

// A task address held across frames.
//
// The address alone is not an identity: the heap hands a freed task's address
// straight to the next allocation, and the task that lands there is mapped,
// live and correctly flagged, so no liveness test tells it apart from the one
// the address was taken from. Only taskUID does. Nothing remembers a task
// between frames as a bare u32.
class TaskRef {
public:
  TaskRef() = default;
  explicit TaskRef(u32 taskAddr)
      : addr_(taskAddr), uid_(TaskUID(taskAddr)) {}

  explicit operator bool() const {
    return LiveTask(addr_) && TaskUID(addr_) == uid_;
  }

  // Zero once stale, so a dead ref reads like an absent one wherever the bare
  // address is passed on.
  u32 Address() const { return *this ? addr_ : 0; }

  template <typename T> T *At() const { return mem::at<T>(Address()); }

  // False for a recycled address, which is what a bare == misses.
  bool Is(u32 taskAddr) const { return addr_ == taskAddr && *this; }

  // Rebinds and reports whether the task changed, a recycled address included.
  bool Rebind(u32 taskAddr) {
    if (Is(taskAddr))
      return false;
    *this = TaskRef(taskAddr);
    return true;
  }

  void Reset() { *this = TaskRef(); }

private:
  u32 addr_ = 0;
  u64 uid_ = 0;
};

} // namespace bd
