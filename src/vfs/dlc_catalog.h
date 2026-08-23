/**
 * @file    vfs/dlc_catalog.h
 * @brief   Host DLC packs: install, list, toggle, and publish to the XAM tree.
 *
 * @copyright   Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *              All rights reserved.
 * @license     BSD 3-Clause - see LICENSE
 */
#pragma once

#include <rex/types.h>

#include <cstddef>
#include <filesystem>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace bd::vfs {

// Outcome of a header-only validity check on a candidate package. No extraction
// or disk writes happen during validation.
struct DLCValidation {
  bool ok = false;
  std::string error;        // human-friendly reason, set when !ok
  std::string display_name; // English display name, set when ok
};

struct DLCPackage {
  std::string name; // store folder name, the dlc.toml key
  std::string display_name;
  std::string description;
  std::string publisher;
  std::filesystem::path root;         // <install>/dlc/<name>
  std::filesystem::path download_dir; // <root>/[data/]Download
  i32 package_id = -1;                // last integer in info.txt
  bool enabled = true;
};

// Loose-extracted packages under <install>/dlc, sibling to the game and saves
// directories. Enabled packs are published into the profile's XAM content tree
// at boot, where the SDK ContentManager enumerates and mounts them, so the
// guest's own title screen pipeline runs natively. Enable flags come from the
// profile's dlc_state.toml and apply on the next launch.
class DLCCatalog {
public:
  // Verifies a file is a genuine Blue Dragon marketplace STFS package without
  // mounting or extracting it, so the installer wizard and the config menu
  // reject non-DLC and wrong-game input identically.
  static DLCValidation Validate(const std::filesystem::path &package);

  void Init(const std::filesystem::path &dlc_root);
  void SetProfile(const std::filesystem::path &profile);
  void Reload();

  size_t Count() const;
  const DLCPackage &At(size_t i) const;

  bool IsEnabled(size_t i) const;
  // Returns whether the flag actually changed, so a caller latching a
  // restart-required flag does not do so on a no-op toggle.
  bool SetEnabled(size_t i, bool on);

  bool Install(const std::filesystem::path &package);
  bool Remove(size_t i);

  // Rebuild the profile's XAM marketplace content tree so the SDK
  // ContentManager enumerates and mounts enabled packs. Each enabled pack's
  // folder is copied as a real directory, no symlink, and its .header restored
  // from the store's sidecar: that header holds the license mask the guest
  // checks, and without it items are granted as worthless junk.
  void Publish();

  // Mount every enabled pack's IPK archives, so content the guest does not
  // route through its single 'download' IPK slot (dungeon scripts, map
  // geometry, models, enemy AI) resolves without extraction or symlinks. Call
  // after Publish, once enable state is known. Registers in name order, which
  // decides which pack wins when two ship the same record name.
  void MountArchives();

private:
  // Drops what MountArchives registered for one pack. Those mounts hold the
  // pack's archives open, so deleting it has to run through here first.
  void UnmountArchives(const DLCPackage &pack) const;

  // Recreates root_ if missing, then rescans and reloads metadata. Shared by
  // Reload() and Install(), both of which already hold mutex_.
  void ReloadLocked();
  void ScanLocked();
  void LoadMetadataLocked();
  void SaveMetadataLocked();
  std::map<std::string, bool> LoadEnabledState() const;
  bool SaveEnabledState(const std::map<std::string, bool> &state) const;
  // Content folder plus .header, for a pack deleted from the store. Publish's
  // boot cleanup only covers packs the store still lists.
  void RemovePublished(const std::string &name) const;
  // Writes dlc_state.toml. Caller already holds mutex_.
  void FlushLocked();

  mutable std::mutex mutex_;
  std::filesystem::path root_;
  std::filesystem::path profile_;
  std::vector<DLCPackage> packages_;
};

} // namespace bd::vfs
