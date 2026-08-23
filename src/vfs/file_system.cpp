/**
 * @file    vfs/file_system.cpp
 * @copyright   Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *              All rights reserved.
 * @license     BSD 3-Clause - see LICENSE
 */
#include "vfs/file_system.h"

#include <algorithm>
#include <iterator>

#include "core/logging.h"

namespace bd::vfs {

const char *ToString(MountKind kind) {
  switch (kind) {
  case MountKind::Generated:
    return "generated";
  case MountKind::Loose:
    return "loose";
  case MountKind::Archive:
    return "archive";
  case MountKind::ShippedPack:
    return "shipped-pack";
  }
  return "?";
}

void FileSystem::Add(std::string name, int priority,
                     std::shared_ptr<Mount> mount) {
  if (!mount)
    return;

  BD_INFO("[vfs] mount '{}' prio {} {} ({} keys)", name, priority,
          ToString(mount->Kind()), mount->KeyCount());

  revision_.fetch_add(1, std::memory_order_release);

  std::lock_guard lock(mutex_);
  std::erase_if(mounts_, [&](const Entry &e) { return e.name == name; });
  mounts_.push_back({std::move(name), priority, next_seq_++, std::move(mount)});
  std::stable_sort(mounts_.begin(), mounts_.end(),
                   [](const Entry &a, const Entry &b) {
                     if (a.priority != b.priority)
                       return a.priority > b.priority;
                     return a.seq > b.seq;
                   });
}

void FileSystem::Remove(std::string_view name) {
  revision_.fetch_add(1, std::memory_order_release);

  std::lock_guard lock(mutex_);
  if (std::erase_if(mounts_, [&](const Entry &e) { return e.name == name; }))
    BD_INFO("[vfs] unmount '{}'", name);
}

std::vector<FileSystem::Entry> FileSystem::Snapshot(Tier tier) const {
  std::lock_guard lock(mutex_);
  std::vector<Entry> entries;
  std::copy_if(mounts_.begin(), mounts_.end(), std::back_inserter(entries),
               [tier](const Entry &e) {
                 return (e.priority >= kPriorityEngine
                             ? Tier::Overlay
                             : Tier::Fallback) == tier;
               });
  return entries;
}

std::optional<StatResult> FileSystem::Stat(const Key &key, Tier tier) const {
  for (const auto &e : Snapshot(tier)) {
    if (auto size = e.mount->Stat(key))
      return StatResult{e.name, e.mount->Kind(), *size};
  }
  return std::nullopt;
}

std::optional<ReadResult> FileSystem::Read(const Key &key, Tier tier) const {
  for (const auto &e : Snapshot(tier)) {
    auto bytes = e.mount->Read(key);
    if (!bytes)
      continue;
    // Indexed but unreadable: keep walking, and the guest's own IO still
    // backstops us, so a broken overlay degrades to the shipped file.
    if (bytes->empty()) {
      BD_ERROR("[vfs] mount '{}' holds '{}' but produced nothing", e.name,
               key.str());
      continue;
    }
    return ReadResult{e.name, e.mount->Kind(), std::move(*bytes)};
  }
  return std::nullopt;
}

void FileSystem::RebuildIndex() const {
  const u64 revision = revision_.load(std::memory_order_acquire);
  if (indexed_revision_ == revision)
    return;

  index_.clear();
  for (const auto tier : {Tier::Overlay, Tier::Fallback}) {
    for (const auto &entry : Snapshot(tier)) {
      for (const auto &key : entry.mount->Keys()) {
        const std::string_view path = key.str();
        // Every separator splits one parent from one child name, and the tail
        // past the last one is the only child that is not itself a directory.
        for (size_t at = 0;;) {
          const auto sep = path.find('\\', at);
          const bool leaf = sep == std::string_view::npos;
          auto name = path.substr(at, leaf ? std::string_view::npos : sep - at);
          if (name.empty())
            break;

          auto &children = index_[at == 0 ? Key() : key.Slice(0, at - 1)];
          auto [it, inserted] = children.emplace(std::string(name), !leaf);
          if (!inserted && !leaf)
            it->second = true;
          if (leaf)
            break;
          at = sep + 1;
        }
      }
    }
  }
  indexed_revision_ = revision;
  BD_INFO("[vfs] directory index: {} directories", index_.size());
}

std::vector<DirEntry> FileSystem::List(const Key &dir) const {
  std::lock_guard lock(index_mutex_);
  RebuildIndex();

  std::vector<DirEntry> entries;
  auto it = index_.find(dir);
  if (it == index_.end())
    return entries;

  entries.reserve(it->second.size());
  for (const auto &[name, is_dir] : it->second)
    entries.push_back({name, is_dir});
  return entries;
}

} // namespace bd::vfs
