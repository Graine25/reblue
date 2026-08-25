/**
 * @file    installer/install_registry.h
 * @brief   Persists the install location and disc fingerprints in the registry.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#pragma once

#include <array>
#include <filesystem>
#include <optional>
#include <string>

namespace bd::installer {

// Bump when an older record can no longer be brought forward in memory.
// ReadInstallRegistry migrates what it can and stamps this on success, so a
// caller that sees anything else is holding a record it cannot use.
constexpr int kInstallSchemaVersion = 3;

// Blue Dragon ships on three DVDs, so every disc-indexed array here is this
// long.
inline constexpr int kDiscCount = 3;

// InstallConfig::renderer values. Absent in the registry reads as
// kRendererDX12.
inline constexpr const char *kRendererDX12 = "dx12";
inline constexpr const char *kRendererVulkan = "vulkan";

struct InstallConfig {
  // Game files under {install_root}/game, user/DLC under {install_root}/user.
  std::filesystem::path install_root;
  std::array<std::string, kDiscCount> iso_fingerprints;
  int schema_version =
      0; // registry SchemaVersion, 0 if absent (pre-schema install)
  std::string renderer = kRendererDX12;
  // REBLUE_VERSION_STRING of the build that wrote this record, stamped by
  // WriteInstallRegistry. Empty on a record written before it was recorded.
  std::string app_version;

  std::filesystem::path game_data_path() const { return install_root / "game"; }
  std::filesystem::path user_data_path() const { return install_root / "user"; }
};

// Reads the per-user install store, HKCU\Software\Zolaware\reblue\Install on
// Windows and $XDG_CONFIG_HOME/reblue/install.toml on Linux. Returns nullopt if
// absent or the recorded install_root/game/default.xex is missing. An older
// record comes back migrated, so schema_version reads as current whenever this
// build can use what it holds.
std::optional<InstallConfig> ReadInstallRegistry();

bool WriteInstallRegistry(const InstallConfig &config);

// Deletes the Install subkey. True on success or if absent.
bool ClearInstallRegistry();

} // namespace bd::installer
