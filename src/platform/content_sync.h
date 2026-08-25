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

#include <rex/types.h>

namespace bd::platform {

// Reads the content manifest, reconciles the pack list against what the
// catalog has, fetches the difference and remounts. Runs on its own thread: a
// pack arriving mid-session takes effect without a restart, since the mount
// registry is swapped under its own lock.
//
// Nothing here is tied to a release. The app manifest says where the content
// manifest lives, and that document is published on its own schedule.
class ContentSync {
public:
  static ContentSync &Get();

  enum class Stage {
    kIdle,     // disabled, or no endpoint to ask
    kChecking, // waiting on the manifest
    kFetching, // downloading, with Current naming the pack
    kDone,     // everything the manifest names is installed
  };

  // One reconciliation for the life of the process. Returns immediately. The
  // url comes from the app manifest, so the caller is whoever read that, and
  // bd_update_check already gated that read. Call once the VFS is up: the
  // catalog it reconciles against is the VFS's.
  void Start(const std::string &url);

  Stage State() const;

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

  void Run(const std::string &url);

  mutable std::mutex mutex_;
  std::string current_;
  std::atomic<Stage> stage_{Stage::kIdle};
  std::atomic<size_t> done_{0};
  std::atomic<size_t> total_{0};
  std::atomic<bool> started_{false};
};

} // namespace bd::platform
