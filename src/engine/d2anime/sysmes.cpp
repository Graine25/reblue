/**
 * @file    engine/d2anime/sysmes.cpp
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause - see LICENSE
 */
#include "engine/d2anime/sysmes.h"
#include "core/i18n.h"
#include "core/logging.h"
#include "core/memory_helpers.h"
#include "core/task_layout.h"
#include "engine/d2anime/anime_vars.h"
#include "engine/d2anime/d2anime_task.h"

#include <rex/hook.h>
#include <rex/ppc/stack.h>
#include <rex/system/thread_state.h>
#include <rex/types.h>

REX_IMPORT(__imp__SelMesWinTask_Create, CreateSelMes, u32(u32, u32));
REX_IMPORT(__imp__SelMesWinConfig_Init, InitSelMesConfig, void(u32));
REX_IMPORT(__imp__SelMesWinConfig_LoadStrings, LoadSelMesStrings,
           void(u32, u32, u32));

namespace {

// SelMesWinTask fields the host polls once the popup has run.
struct SelMesWinTask_t {
  u8 _pad000[0xDC8];
  be_u32 confirmed; // non-zero once the user confirmed
  be_u32 canceled;  // non-zero once the user canceled
  // CommandSelectTask* managing answer selection within SelMesWinTask.
  be_u32 commandSelect;
};
static_assert(offsetof(SelMesWinTask_t, confirmed) == 0xDC8);
static_assert(offsetof(SelMesWinTask_t, canceled) == 0xDCC);
static_assert(offsetof(SelMesWinTask_t, commandSelect) == 0xDD0);

// SelMesWin config blob passed to SelMesWinTask_Create. The body is opaque,
// only the trailing defaultSel field is written by the host.
struct SelMesWinConfig_t {
  u8 _pad000[0x2B0];
  be_u32 defaultSel;
};
static_assert(offsetof(SelMesWinConfig_t, defaultSel) == 0x2B0);
static_assert(sizeof(SelMesWinConfig_t) == 0x2B4);

struct CommandSelectTask_t {
  u8 _pad000[0xB0];
  be_u32 cursorIndex; // selected answer index (0 = first answer / Yes)
};
static_assert(offsetof(CommandSelectTask_t, cursorIndex) == 0xB0);

} // namespace

namespace bd::engine {

bool SysMesConfirm::Create(u32 parentTask, const char *q1, const char *q2,
                           const char *q3, const char *a1, const char *a2,
                           int defaultSel) {
  if (task_) {
    BD_WARN("[sysmes] already active, killing previous");
    Kill();
  }

  // Raw base: stack_push and the call bridges address the PPC stack, not the
  // heap.
  auto *memory = REX_KERNEL_MEMORY();
  if (!memory)
    return false;
  auto *base = memory->virtual_membase();

  // Engine stores these as wchar_t, and LoadStrings reads them back by RBDEL_
  // name.
  u32 vb = D2AnimeTask(parentTask).VarBag();
  VarBagSetText(vb, "RBDEL_SQ1", q1);
  VarBagSetText(vb, "RBDEL_SQ2", q2 ? q2 : "");
  VarBagSetText(vb, "RBDEL_SQ3", q3 ? q3 : "");
  VarBagSetText(vb, "RBDEL_SA1",
                a1 ? std::string_view(a1)
                   : std::string_view(i18n::Text("common.yes")));
  VarBagSetText(vb, "RBDEL_SA2",
                a2 ? std::string_view(a2)
                   : std::string_view(i18n::Text("common.no")));

  rex::CallFrame frame(*rex::runtime::ThreadState::Get()->context());
  rex::ppc::stack_guard guard(frame.ctx);

  alignas(8) u8 zeroBuf[sizeof(SelMesWinConfig_t)]{};
  u32 configAddr =
      rex::ppc::stack_push(frame.ctx, base, zeroBuf, sizeof(SelMesWinConfig_t));

  InitSelMesConfig(frame, base, configAddr);

  u32 prefixAddr = rex::ppc::stack_push_string(frame.ctx, base, "RBDEL");
  LoadSelMesStrings(frame, base, configAddr, parentTask, prefixAddr);

  reinterpret_cast<SelMesWinConfig_t *>(base + configAddr)->defaultSel =
      static_cast<u32>(defaultSel);

  task_ = CreateSelMes(frame, base, parentTask, configAddr);

  if (!task_) {
    BD_ERROR("[sysmes] SelMesWinTask_Create returned null");
    return false;
  }

  auto *taskBase = bd::mem::at<bd::TaskBase_t>(task_);
  auto *parentBase = bd::mem::at<bd::TaskBase_t>(parentTask);
  taskBase->notifyParent = parentTask;
  taskBase->notifyParentUID = parentBase->taskUID;

  BD_INFO("[sysmes] created SelMesWinTask at 0x{:08X} (parent=0x{:08X})", task_,
          parentTask);
  return true;
}

bool SysMesConfirm::Poll() const {
  if (!task_)
    return true;
  auto *t = bd::mem::at<SelMesWinTask_t>(task_);
  return t->confirmed != 0 || t->canceled != 0;
}

bool SysMesConfirm::Confirmed() const {
  if (!task_)
    return false;
  if (bd::mem::at<SelMesWinTask_t>(task_)->confirmed == 0)
    return false;
  return SelectedAnswer() == 0;
}

int SysMesConfirm::SelectedAnswer() const {
  if (!task_)
    return -1;
  u32 cmdSel = bd::mem::at<SelMesWinTask_t>(task_)->commandSelect;
  if (!cmdSel)
    return -1;
  return static_cast<int>(
      bd::mem::at<CommandSelectTask_t>(cmdSel)->cursorIndex);
}

void SysMesConfirm::Kill() {
  if (!task_)
    return;
  bd::KillTask(task_);
  BD_INFO("[sysmes] killed SelMesWinTask at 0x{:08X}", task_);
  task_ = 0;
}

} // namespace bd::engine
