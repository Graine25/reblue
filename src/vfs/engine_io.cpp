/**
 * @file    vfs/engine_io.cpp
 * @brief   Engine file IO interception: bdFileExistsCheck / bdFileReadStart are
 *          answered from the mount overlay when it holds the path, and fall
 *          through to the engine's own disc and .ipk IO when it does not.
 *
 * @copyright   Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *              All rights reserved.
 * @license     BSD 3-Clause - see LICENSE
 */
#include <algorithm>
#include <atomic>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

#include <rex/hook.h>
#include <rex/memory/utils.h>
#include <rex/ppc/function.h>
#include <rex/runtime.h>
#include <rex/system/function_dispatcher.h>
#include <rex/system/kernel_state.h>
#include <rex/system/thread_state.h>
#include <rex/types.h>

#include "core/logging.h"
#include "core/memory_helpers.h"
#include "vfs/access_log.h"
#include "vfs/file_system.h"
#include "vfs/vfs.h"

namespace {

// Guest ReadRequest object: bdFileReadStart's second argument, filled in by
// ServeReadRequest for overlay-served reads.
constexpr u32 kReadRequestComplete = 2;
constexpr u32 kNoIoChannel = 0;

struct BdFileReadRequest_t {
  be_u32 status;
  u8 _pad004[0x06 - 0x04];
  be_u16 allocType; // bdFilePhysicalMallocRead key
  be_u32 buffer;    // dest VA, null = server allocates
  be_u32 fileSize;
  be_u32 ioChannel; // least-loaded IO channel bdFileReadStart enqueues onto
};
static_assert(offsetof(BdFileReadRequest_t, status) == 0x00);
static_assert(offsetof(BdFileReadRequest_t, allocType) == 0x06);
static_assert(offsetof(BdFileReadRequest_t, buffer) == 0x08);
static_assert(offsetof(BdFileReadRequest_t, fileSize) == 0x0C);
static_assert(offsetof(BdFileReadRequest_t, ioChannel) == 0x10);

} // namespace

// The I/O worker allocates the buffer at op+8 with this, keyed on the
// request's alloc type at op+6, and the resource destructor frees it through
// the matching allocator. Overlay serving stands in for the worker, so it has
// to allocate the same way. The callable must NOT be named
// bdFilePhysicalMallocRead: on ELF the unmangled variable symbol collides with
// the generated weak function alias and hijacks its function table entry.
REX_IMPORT(__imp__bdFilePhysicalMallocRead, guest_bdFilePhysicalMallocRead,
           u32(u32, u32));

namespace {

bd::vfs::FileSource SourceOf(bd::vfs::MountKind kind) {
  switch (kind) {
  case bd::vfs::MountKind::Generated:
    return bd::vfs::FileSource::Virtual;
  case bd::vfs::MountKind::Loose:
    return bd::vfs::FileSource::Mod;
  case bd::vfs::MountKind::Archive:
    return bd::vfs::FileSource::DLC;
  case bd::vfs::MountKind::ShippedPack:
    return bd::vfs::FileSource::Pack;
  }
  return bd::vfs::FileSource::Missing;
}

// Complete a guest ReadRequest with 'content'. A caller that pre-allocated its
// own buffer at op+8 expects the data written IN PLACE. When op+8 is null,
// allocate as the native I/O worker does, through bdFilePhysicalMallocRead(
// allocType, size) keyed on op+6, so the resource destructor frees through the
// matching allocator. Any other heap (or rewriting op+6) makes that free a
// no-op and leaks the buffer on every load. Returns false only when an
// allocation was needed and failed.
bool ServeReadRequest(u32 req_addr, const std::vector<u8> &content) {
  const u32 size = static_cast<u32>(content.size());
  auto *req = bd::mem::at<BdFileReadRequest_t>(req_addr);
  if (!req)
    return false;
  if (!req->buffer) {
    const u16 alloc_type = req->allocType;
    const u32 guest_buf = guest_bdFilePhysicalMallocRead(alloc_type, size);
    if (!guest_buf)
      return false;
    req->buffer = guest_buf;
  }
  std::memcpy(bd::mem::at<u8>(req->buffer), content.data(), size);
  req->status = kReadRequestComplete;
  req->fileSize = size;
  // An overlay-served read never enters a channel queue.
  req->ioChannel = kNoIoChannel;
  return true;
}

// The native I/O worker completes a load by filling the ReadRequest fields and
// invoking bdFileReadStart's callback. That callback runs the resource's derived
// vf04, the only code that parses the bytes and clears the async wrapper busy
// bit, so an overlay that skips it leaves every bdAsyncRequestPoll waiter
// spinning.
//
// Deferring to poll time is what makes the replay safe: LoadBinary__ctor re-sets
// the busy bit after bdFileReadStart returns, and the derived vtable is not
// installed until the base ctor does, so a synchronous fire would hit the base
// nullsub and then be re-marked busy.
struct PendingCompletion {
  u32 cb;
  u32 cbctx;
  u32 request;
  u32 tid;
};

std::mutex g_pendingMutex;
std::vector<PendingCompletion> g_pending;
std::atomic<u32> g_pendingCount{0};
thread_local bool g_draining = false;

void EnqueueDeferredCompletion(u32 request, u32 cb, u32 cbctx) {
  if (!cb)
    return; // simple readers pass cb==0, nothing to complete
  std::lock_guard lock(g_pendingMutex);
  g_pending.push_back(
      {cb, cbctx, request, rex::runtime::ThreadState::GetThreadID()});
  g_pendingCount.store(static_cast<u32>(g_pending.size()),
                       std::memory_order_release);
}

void DrainDeferredCompletions() {
  if (g_draining || g_pendingCount.load(std::memory_order_acquire) == 0)
    return;
  g_draining = true;
  const u32 tid = rex::runtime::ThreadState::GetThreadID();
  auto *dispatcher = REX_KERNEL_STATE()->function_dispatcher();
  for (;;) {
    PendingCompletion pc;
    {
      std::lock_guard lock(g_pendingMutex);
      auto it = std::find_if(
          g_pending.begin(), g_pending.end(),
          [tid](const PendingCompletion &p) { return p.tid == tid; });
      if (it == g_pending.end())
        break;
      pc = *it;
      g_pending.erase(it);
      g_pendingCount.store(static_cast<u32>(g_pending.size()),
                           std::memory_order_release);
    }
    // cb(r3=request, r4=cbctx) on a fresh nested PPCContext.
    if (auto *fn = dispatcher->GetFunction(pc.cb))
      rex::ppc::GuestToHostFunction<void>(fn, pc.request, pc.cbctx);
  }
  g_draining = false;
}

} // namespace

// bdFileExistsCheck(path), returning the file size.
//
// Raw, on the inherited context: a typed REX_IMPORT re-roots the guest stack
// at ThreadState's r1 and overwrites the frames live underneath it.
REX_EXTERN(__imp__bdFileExistsCheck);
REX_HOOK_RAW(bdFileExistsCheck) {
  auto &vfs = bd::vfs::VFS::Get();

  const u32 pathVA = ctx.r3.u32;
  const auto key =
      bd::vfs::Key::FromGuestPath(bd::mem::str(pathVA)).Localized();
  auto &log = vfs.Log();
  auto answer = [&](const bd::vfs::StatResult &hit) {
    log.Record(key.string(), SourceOf(hit.kind));
    ctx.r3.u64 = static_cast<u32>(hit.size);
  };

  if (auto hit = vfs.Files().Stat(key))
    return answer(*hit);

  ctx.r3.u32 = pathVA;
  __imp__bdFileExistsCheck(ctx, base);
  if (ctx.r3.u32) {
    log.RecordEngineHit(key.string());
    return;
  }

  // A path the engine cannot find may still be shipped in a pack it has not
  // registered, and the existence check has to agree with what the read would get.
  if (auto hit = vfs.Files().Stat(key, bd::vfs::Tier::Fallback))
    return answer(*hit);

  log.Record(key.string(), bd::vfs::FileSource::Missing);
  ctx.r3.u64 = 0;
}

// bdFileReadStart(path, request, cb, cbctx).
//
// Raw, on the inherited context, for the same reason as bdFileExistsCheck.
REX_EXTERN(__imp__bdFileReadStart);
REX_HOOK_RAW(bdFileReadStart) {
  auto &vfs = bd::vfs::VFS::Get();

  const u32 pathVA = ctx.r3.u32;
  const u32 request = ctx.r4.u32;
  const u32 cb = ctx.r5.u32;
  const u32 cbctx = ctx.r6.u32;

  const auto key =
      bd::vfs::Key::FromGuestPath(bd::mem::str(pathVA)).Localized();
  auto &log = vfs.Log();
  auto hit = vfs.Files().Read(key);

  // A serve failure falls through to the engine rather than failing the read,
  // so a broken overlay degrades to the shipped file instead of a load hang.
  if (hit && !ServeReadRequest(request, hit->bytes)) {
    BD_ERROR("[vfs] no guest buffer for '{}' ({} bytes) from mount '{}'",
             key.str(), hit->bytes.size(), hit->mount);
    hit.reset();
  }

  if (!hit) {
    ctx.r3.u32 = pathVA;
    ctx.r4.u32 = request;
    ctx.r5.u32 = cb;
    ctx.r6.u32 = cbctx;
    __imp__bdFileReadStart(ctx, base);
    if (ctx.r3.u32)
      log.RecordEngineHit(key.string());
    else
      log.Record(key.string(), bd::vfs::FileSource::Missing);
    return;
  }

  log.Record(key.string(), SourceOf(hit->kind));
  EnqueueDeferredCompletion(request, cb, cbctx);
  ctx.r3.u64 = 1;
}

// bdFileReadStart's async completion is normally driven by the native I/O
// worker. Overlay-served reads defer the completion callback and replay it
// here, the universal poll chokepoint every resource loader waits on.
REX_EXTERN(__imp__bdAsyncRequestPoll);

REX_HOOK_RAW(bdAsyncRequestPoll) {
  DrainDeferredCompletions();
  __imp__bdAsyncRequestPoll(ctx, base);
}
