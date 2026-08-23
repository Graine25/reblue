/**
 * @file    engine/d2anime/anime_mount.cpp
 * @copyright   Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *              All rights reserved.
 * @license     BSD 3-Clause - see LICENSE
 */
#include "engine/d2anime/anime_mount.h"

#include "engine/d2anime/anime_layout.h"
#include "vfs/vfs.h"

namespace bd::engine {

namespace {

std::vector<u8> Bytes(const std::string &s) { return {s.begin(), s.end()}; }

} // namespace

LayoutMount::LayoutMount(std::string dir)
    : dir_(std::move(dir)), mount_(std::make_shared<vfs::GeneratedMount>()) {}

LayoutMount::~LayoutMount() = default;

LayoutMount &LayoutMount::Add(const char *name, AnimeLayout *layout) {
  return Add(name, [layout] { return layout->ToCSV(); });
}

LayoutMount &LayoutMount::Add(const char *name,
                              std::function<std::string()> csv) {
  return AddRaw(dir_ + name,
                [csv = std::move(csv)] { return Bytes(csv()); });
}

LayoutMount &LayoutMount::AddRaw(std::string key,
                                 std::function<std::vector<u8>()> bytes) {
  mount_->Set(vfs::Key::FromRelative(key), std::move(bytes));
  return *this;
}

void LayoutMount::Publish(const char *mountName) {
  vfs::VFS::Get().Files().Add(mountName, vfs::kPriorityGenerated, mount_);
}

void LayoutMount::Remove(const char *mountName) {
  vfs::VFS::Get().Files().Remove(mountName);
}

} // namespace bd::engine
