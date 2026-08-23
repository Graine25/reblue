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

// Bump to force every existing install to reinstall once. Stamped in the
// registry at install, and boot re-runs the wizard on mismatch.
constexpr int kInstallSchemaVersion = 1;

// Blue Dragon ships on three DVDs, so every disc-indexed array here is this
// long.
inline constexpr int kDiscCount = 3;

struct InstallConfig {
  // Game files under {install_root}/game, user/DLC under {install_root}/user.
  std::filesystem::path install_root;
  std::array<std::string, kDiscCount> iso_fingerprints;
  int schema_version =
      0; // registry SchemaVersion, 0 if absent (pre-schema install)

  std::filesystem::path game_data_path() const { return install_root / "game"; }
  std::filesystem::path user_data_path() const { return install_root / "user"; }
};

// Reads the per-user install store, HKCU\Software\Zolaware\reblue\Install on
// Windows and $XDG_CONFIG_HOME/reblue/install.toml on Linux. Returns nullopt if
// absent or the recorded install_root/game/default.xex is missing.
std::optional<InstallConfig> ReadInstallRegistry();

bool WriteInstallRegistry(const InstallConfig &config);

// Deletes the Install subkey. True on success or if absent.
bool ClearInstallRegistry();

} // namespace bd::installer
