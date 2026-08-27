/**
 * @file    platform/content_sync.h
 * @brief   Fetches the content packs the content manifest names, and mounts
 *          them.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#pragma once

#include <atomic>
#include <mutex>
#include <string>
#include <vector>

#include <rex/types.h>

#include "platform/manifest.h"

namespace bd::platform {

// Reads the content manifest, reconciles the pack list against what the
// catalog has, and holds the difference until something answers for it. The
// fetch and the remount follow. Runs on its own thread: a pack arriving
// mid-session takes effect without a restart, since the mount registry is
// swapped under its own lock.
//
// Nothing here is tied to a release. The app manifest says where the content
// manifest lives, and that document is published on its own schedule.
class ContentSync {
public:
  static ContentSync &Get();

  enum class Stage {
    kIdle,     // disabled, or no endpoint to ask
    kChecking, // waiting on the manifest
    kPending,  // it names packs this install lacks, waiting on an answer
    kFetching, // downloading, with Current naming the pack
    kDone,     // everything is installed, or the offer was declined
  };

  // One reconciliation for the life of the process. Returns immediately, and
  // stops at kPending rather than fetching: the title's update prompt offers
  // the download. Call once the VFS is up, since the catalog it reconciles
  // against is the VFS's.
  void Start(const std::string &url);

  Stage State() const;

  // What the check found and nothing has answered for yet.
  size_t PendingCount() const;
  u64 PendingBytes() const;

  void BeginFetch();

  // The next launch offers it again.
  void Decline();

  // The pack being fetched, and how far through the fetch list this is. Done
  // counts the packs already attempted, so it never reaches Total while a pack
  // is still on screen.
  std::string Current() const;
  size_t Done() const;
  size_t Total() const;

private:
  ContentSync() = default;
  ContentSync(const ContentSync &) = delete;
  ContentSync &operator=(const ContentSync &) = delete;

  void Check(const std::string &url);
  void Fetch();

  mutable std::mutex mutex_;
  std::string current_;
  std::vector<ContentEntry> pending_;
  std::atomic<Stage> stage_{Stage::kIdle};
  std::atomic<size_t> done_{0};
  std::atomic<size_t> total_{0};
  std::atomic<u64> pending_bytes_{0};
  std::atomic<bool> started_{false};
  std::atomic<bool> fetching_{false};
};

} // namespace bd::platform
