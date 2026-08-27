/**
 * @file    vfs/content_catalog.h
 * @brief   Content packs re:Blue distributes, and one LooseMount per pack.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#pragma once

#include <cstddef>
#include <filesystem>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include <rex/types.h>

namespace bd::vfs {

class FileSystem;

struct ContentPack {
  std::string id;
  i64 version = 0;
  std::filesystem::path dir;
};

// A content pack is re:Blue's to install and update, where a mod is the
// user's. They live in the cache, one directory per pack, and mount below mods
// so a user mod still overrides one. A wiped cache costs a re-download and
// nothing else. This owns the whole layout under its root.
//
// One thread at a time: VFS::Init scans and mounts, and from there the content
// sync's thread is the only caller. FileSystem carries its own lock, so the
// mounts are safe to swap under a running game, and the pack list carries one
// of its own.
class ContentCatalog {
public:
  void Init(const std::filesystem::path &root, FileSystem &files);

  // Re-scans and re-mounts.
  void Reload();

  // 0 when no pack with that id is installed.
  i64 InstalledVersion(std::string_view id) const;

  // The first installed pack holding a file of this name, in id order.
  std::filesystem::path Find(std::string_view name) const;

  // Where a fetch unpacks a pack before it is fit to mount.
  std::filesystem::path StagingDirFor(std::string_view id) const;

  // Where a fetch parks the zip it is verifying.
  std::filesystem::path DownloadPathFor(std::string_view id, i64 version) const;

  // Unmounts, moves each named pack out of staging into its final home, then
  // re-scans and re-mounts. Unmounting first is what lets a pack that is
  // already live be replaced: its files are no longer open.
  void Commit(const std::vector<std::string> &ids);

private:
  std::filesystem::path DirFor(std::string_view id) const;
  size_t Mount();
  void Unmount();
  void Discover();

  std::vector<ContentPack> Snapshot() const;

  std::filesystem::path root_;
  FileSystem *files_ = nullptr;
  mutable std::mutex mutex_;
  std::vector<ContentPack> packs_;
  std::vector<std::string> mount_names_;
};

} // namespace bd::vfs
