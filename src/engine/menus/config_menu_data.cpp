/**
 * @file    engine/menus/config_menu_data.cpp
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#include "engine/menus/config_menu_data.h"
#include "core/logging.h"
#include "core/settings_model.h"
#include "core/zip_unpack.h"
#include "engine/achievements/achievement_list.h"
#include "engine/menus/achievements_layout.h"
#include "engine/menus/achievements_menu.h"
#include "engine/menus/config_layout.h"
#include "engine/sfx.h"
#include "platform/platform.h"
#include "vfs/vfs.h"

#include <algorithm>
#include <fstream>
#include <memory>

#define MINIZ_HEADER_FILE_ONLY
#include <miniz.h>

#include <rex/types.h>

namespace bd::engine {

namespace {

ConfigLayout s_config_layout;
SectionTemplate s_section_tpl;
ListItemTemplate s_mod_item_tpl{"Mod", 420, 370};
NodataTemplate s_nodata_tpl;
DetailTemplate s_detail_tpl;
ListItemTemplate s_dlc_item_tpl{"DLC", 470, 430};
DlcDetailTemplate s_dlc_detail_tpl;
SettingItemTemplate s_setting_item_tpl;
KeybindItemTemplate s_keybind_item_tpl;
PadLayoutTemplate s_pad_tpl;
bool s_dlc_changed = false;

constexpr const char *kMenuMount = "ui:config-menu";

} // namespace

void RegisterVFS(ConfigMenu::Surface surface) {
  const bool inGame = surface == ConfigMenu::Surface::InGame;

  // Restart-bound rows stay listed in-game, grayed rather than hidden, so
  // both surfaces show the same pages.
  SettingsDisableRestartRows(inGame);
  s_config_layout.SetSectionCount(inGame ? kSettingsSectionCount
                                         : ConfigLayout::kSectionCount);
  s_config_layout.SetStandalone(!inGame);

  auto &mods = vfs::VFS::Get().Mods();

  size_t modCount = mods.Count();
  size_t dlcCount = vfs::VFS::Get().DLC().Count();
  s_config_layout.SetModCount(modCount);
  s_config_layout.SetDLCCount(dlcCount);
  s_config_layout.SetAchievementCount(RefreshAchievementList().size());
  // SettingsPage 0..kSettingsSectionCount-1 are the sidebar pages, in order.
  // Slots, not rows: a page's section titles are drawn as list entries of
  // their own.
  size_t pageSlots[kSettingsSectionCount];
  for (int p = 0; p < kSettingsSectionCount; ++p)
    pageSlots[p] = SettingsSlotCount(static_cast<SettingsPage>(p));
  s_config_layout.SetSettingsCounts(pageSlots,
                                    SettingsCount(SettingsPage::Keybinds));

  // Providers run per read, so each CSV reflects the layout state at that
  // moment. Nothing here needs re-registering when the data behind it changes.
  LayoutMount mount("d2anime\\modmgr\\");
  mount.Add("l_modmgr.csv", &s_config_layout)
      .Add("l_modmgr_info.csv", &s_mod_item_tpl)
      .Add("l_modmgr_detail.csv", &s_detail_tpl)
      .Add("l_modmgr_section.csv", &s_section_tpl)
      .Add("l_modmgr_nodata.csv", &s_nodata_tpl)
      .Add("l_modmgr_dlcinfo.csv", &s_dlc_item_tpl)
      .Add("l_modmgr_dlcdetail.csv", &s_dlc_detail_tpl)
      .Add("l_modmgr_setting.csv", &s_setting_item_tpl)
      .Add("l_modmgr_keybind.csv", &s_keybind_item_tpl)
      .Add("l_modmgr_pad.csv", &s_pad_tpl)
      // Full-width rows. The camp Encyclopedia screen serves its own narrower
      // instance beside its CSV, since a template path resolves relative to the
      // file naming it.
      .Add("l_modmgr_achv.csv", &AchievementRowTemplate::Config());
  // The row icons live on the achievement viewer's own mount, which the camp
  // screen would otherwise be the only thing to bring up.
  RegisterAchievementsVFS();

  mount.Publish(kMenuMount);
  mods.MountPreviews();
}

void UnregisterVFS() {
  LayoutMount::Remove(kMenuMount);
  vfs::VFS::Get().Mods().UnmountPreviews();
}

void SaveAndReload() {
  auto &mods = vfs::VFS::Get().Mods();
  mods.Flush();
  mods.Reload();
  BD_DEBUG("[config] mod config saved and reloaded");
}

void FlipMod(int index) {
  auto &mods = vfs::VFS::Get().Mods();
  auto i = static_cast<size_t>(index);
  mods.SetEnabled(i, !mods.IsEnabled(i));
  sfx::Play(sfx::kToggle);
}

void ReorderMod(int a, int b) {
  vfs::VFS::Get().Mods().Swap(static_cast<size_t>(a), static_cast<size_t>(b));
}

namespace {

bool InstallModFromFolder(const std::filesystem::path &src,
                          const std::filesystem::path &mods_dir,
                          vfs::ModCatalog &mods) {
  if (!std::filesystem::exists(src / "mod.toml")) {
    BD_WARN("[config] selected folder has no mod.toml: {}", src.string());
    return false;
  }

  auto folder_name = src.filename().string();
  auto dest = mods_dir / folder_name;

  if (std::filesystem::exists(dest)) {
    BD_WARN("[config] mod folder already exists: {}", dest.string());
    return false;
  }

  std::error_code ec;
  std::filesystem::copy(src, dest, std::filesystem::copy_options::recursive,
                        ec);
  if (ec) {
    BD_ERROR("[config] failed to copy mod: {}", ec.message());
    return false;
  }

  BD_DEBUG("[config] installed mod '{}' from {}", folder_name, src.string());

  // Enable in the active profile's loadout (order file), then reload.
  mods.Enable(folder_name);
  return true;
}

bool InstallModFromZip(const std::filesystem::path &zip_path,
                       const std::filesystem::path &mods_dir,
                       vfs::ModCatalog &mods) {
  mz_zip_archive zip{};
  if (!mz_zip_reader_init_file(&zip, zip_path.string().c_str(), 0)) {
    BD_ERROR("[config] failed to open zip: {}", zip_path.string());
    return false;
  }

  int num_files = static_cast<int>(mz_zip_reader_get_num_files(&zip));

  std::string prefix;
  bool found_mod_toml = false;

  if (mz_zip_reader_locate_file(&zip, "mod.toml", nullptr, 0) >= 0) {
    found_mod_toml = true;
  } else {
    // Fall back to a single top-level folder that contains mod.toml.
    std::string single_dir;
    bool multiple_dirs = false;
    for (int i = 0; i < num_files; i++) {
      char fname[512];
      mz_zip_reader_get_filename(&zip, i, fname, sizeof(fname));
      std::string name(fname);
      auto slash = name.find('/');
      if (slash == std::string::npos)
        slash = name.find('\\');
      if (slash != std::string::npos) {
        std::string dir = name.substr(0, slash);
        if (single_dir.empty())
          single_dir = dir;
        else if (dir != single_dir) {
          multiple_dirs = true;
          break;
        }
      }
    }
    if (!multiple_dirs && !single_dir.empty()) {
      std::string toml_path = single_dir + "/mod.toml";
      if (mz_zip_reader_locate_file(&zip, toml_path.c_str(), nullptr, 0) >= 0) {
        found_mod_toml = true;
        prefix = single_dir + "/";
      }
    }
  }

  if (!found_mod_toml) {
    BD_WARN("[config] zip has no mod.toml: {}", zip_path.string());
    mz_zip_reader_end(&zip);
    return false;
  }

  std::string folder_name;
  if (prefix.empty()) {
    folder_name = zip_path.stem().string();
  } else {
    folder_name = prefix.substr(0, prefix.size() - 1);
  }

  auto dest = mods_dir / folder_name;
  if (std::filesystem::exists(dest)) {
    BD_WARN("[config] mod folder already exists: {}", dest.string());
    mz_zip_reader_end(&zip);
    return false;
  }

  std::filesystem::create_directories(dest);
  bool extract_ok = true;
  for (int i = 0; i < num_files; i++) {
    if (mz_zip_reader_is_file_a_directory(&zip, i))
      continue;

    char fname[512];
    mz_zip_reader_get_filename(&zip, i, fname, sizeof(fname));
    std::string name(fname);

    std::string relative = name;
    if (!prefix.empty() && name.starts_with(prefix))
      relative = name.substr(prefix.size());

    if (IsUnsafeArchivePath(relative)) {
      BD_ERROR("[config] unsafe entry '{}' in zip {}", name,
               zip_path.string());
      extract_ok = false;
      break;
    }

    auto out_path = dest / relative;
    std::filesystem::create_directories(out_path.parent_path());

    if (!mz_zip_reader_extract_to_file(&zip, i, out_path.string().c_str(), 0)) {
      BD_ERROR("[config] failed to extract: {}", name);
      extract_ok = false;
      break;
    }
  }

  mz_zip_reader_end(&zip);

  if (!extract_ok) {
    std::error_code ec;
    std::filesystem::remove_all(dest, ec);
    return false;
  }

  BD_DEBUG("[config] installed mod '{}' from zip {}", folder_name,
           zip_path.string());

  // Enable in the active profile's loadout (order file), then reload.
  mods.Enable(folder_name);
  return true;
}

} // namespace

bool InstallMod() {
  static constexpr bd::platform::FileFilter kModFilters[] = {
      {L"Mod files (*.zip, mod.toml)", L"*.zip;mod.toml"},
  };
  auto selected = bd::platform::ShowOpenFileDialog(
      L"Select Mod (zip or mod.toml)", kModFilters);
  if (!selected) {
    BD_DEBUG("mod install canceled");
    return false;
  }

  auto &path = *selected;
  auto ext = path.extension().string();
  std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

  auto &mods = vfs::VFS::Get().Mods();
  auto mods_dir = vfs::VFS::Get().Paths().Mods();
  std::filesystem::create_directories(mods_dir);

  if (ext == ".zip")
    return InstallModFromZip(path, mods_dir, mods);
  else
    return InstallModFromFolder(path.parent_path(), mods_dir, mods);
}

bool InstallDLC() {
  auto selected = bd::platform::ShowOpenFileDialog(L"Select DLC Package");
  if (!selected) {
    BD_INFO("dlc install canceled");
    return false;
  }

  if (!vfs::VFS::Get().DLC().Install(*selected)) {
    BD_ERROR("dlc installation failed");
    return false;
  }

  s_dlc_changed = true;
  return true;
}

bool RemoveMod(int index) {
  auto &mods = vfs::VFS::Get().Mods();
  mods.UnmountPreviews();
  return mods.Remove(static_cast<size_t>(index));
}

bool DeleteDLC(int index) {
  bool ok = vfs::VFS::Get().DLC().Remove(static_cast<size_t>(index));
  if (ok)
    s_dlc_changed = true;
  return ok;
}

size_t ModCount() { return vfs::VFS::Get().Mods().Count(); }

size_t DlcCount() { return vfs::VFS::Get().DLC().Count(); }

const vfs::ModPackage &ModAt(int index) {
  return vfs::VFS::Get().Mods().At(static_cast<size_t>(index));
}

bool ModIsEnabled(int index) {
  return vfs::VFS::Get().Mods().IsEnabled(static_cast<size_t>(index));
}

vfs::DLCCatalog &DLC() { return vfs::VFS::Get().DLC(); }

void RefreshDLC() { vfs::VFS::Get().DLC().Reload(); }

bool DlcChanged() { return s_dlc_changed; }

void ToggleDLC(int index) {
  auto &dlc = vfs::VFS::Get().DLC();
  if (index < 0 || index >= static_cast<int>(dlc.Count()))
    return;
  if (dlc.SetEnabled(static_cast<size_t>(index),
                     !dlc.IsEnabled(static_cast<size_t>(index))))
    s_dlc_changed = true;
    sfx::Play(sfx::kToggle);
}

bool IsDLCEnabled(int index) {
  auto &dlc = vfs::VFS::Get().DLC();
  return index >= 0 && index < static_cast<int>(dlc.Count()) &&
         dlc.IsEnabled(static_cast<size_t>(index));
}

ConfigLayout &GetLayout() { return s_config_layout; }

} // namespace bd::engine
