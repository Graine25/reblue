/**
 * @file    installer/install_registry.cpp
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#include "installer/install_registry.h"

#include <string_view>

#include <rex/types.h>

#include "core/build_info.h"
#include "core/encoding.h"
#include "core/logging.h"

namespace bd::installer {
namespace {

// Every field added since schema 1 has a usable default, so an older record
// comes forward as it stands. A newer one cannot: this build has no idea what
// it left out, and leaving schema_version as written is what tells the caller.
void Migrate(InstallConfig &cfg) {
  const int stored = cfg.schema_version;
  if (stored > kInstallSchemaVersion) {
    BD_WARN("Install record schema {} was written by a newer build", stored);
    return;
  }
  cfg.schema_version = kInstallSchemaVersion;
  if (stored != kInstallSchemaVersion)
    BD_INFO("Install record migrated from schema {} to {}", stored,
            kInstallSchemaVersion);
}

} // namespace
} // namespace bd::installer

#if defined(_WIN32)
#include "core/windows_lean.h"

namespace bd::installer {
namespace {

constexpr wchar_t kInstallKey[] = L"Software\\Zolaware\\reblue\\Install";

// How Renderer is spelled in the store. Anything else is a record from before
// the field existed, which means D3D12.
constexpr const char *kRendererD3D12 = "dx12";
constexpr const char *kRendererVulkan = "vulkan";

const char *Serialize(Renderer renderer) {
  return renderer == Renderer::Vulkan ? kRendererVulkan : kRendererD3D12;
}

Renderer Deserialize(std::string_view text) {
  return text == kRendererVulkan ? Renderer::Vulkan : Renderer::D3D12;
}

std::optional<std::wstring> ReadString(HKEY key, const wchar_t *name) {
  DWORD type = 0;
  DWORD size = 0;
  if (RegGetValueW(key, nullptr, name, RRF_RT_REG_SZ, &type, nullptr, &size) !=
      ERROR_SUCCESS) {
    return std::nullopt;
  }
  std::wstring out(size / sizeof(wchar_t), L'\0');
  if (RegGetValueW(key, nullptr, name, RRF_RT_REG_SZ, &type, out.data(),
                   &size) != ERROR_SUCCESS) {
    return std::nullopt;
  }
  // RegGetValueW counts the terminating null in 'size'.
  while (!out.empty() && out.back() == L'\0')
    out.pop_back();
  return out;
}

bool WriteString(HKEY key, const wchar_t *name, const std::wstring &value) {
  auto bytes = static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t));
  return RegSetValueExW(key, name, 0, REG_SZ,
                        reinterpret_cast<const BYTE *>(value.c_str()),
                        bytes) == ERROR_SUCCESS;
}

} // namespace

std::optional<InstallConfig> ReadInstallRegistry() {
  HKEY key = nullptr;
  if (RegOpenKeyExW(HKEY_CURRENT_USER, kInstallKey, 0, KEY_READ, &key) !=
      ERROR_SUCCESS) {
    return std::nullopt;
  }
  struct KeyGuard {
    HKEY k;
    ~KeyGuard() {
      if (k)
        RegCloseKey(k);
    }
  } guard{key};

  auto root_w = ReadString(key, L"InstallRoot");
  if (!root_w || root_w->empty())
    return std::nullopt;

  InstallConfig cfg;
  cfg.install_root = *root_w;

  auto read_fp = [&](const wchar_t *name) -> std::string {
    auto w = ReadString(key, name);
    return w ? bd::WideToUtf8(*w) : std::string{};
  };
  for (int i = 0; i < kDiscCount; ++i)
    cfg.iso_fingerprints[i] =
        read_fp((L"Disc" + std::to_wstring(i + 1) + L"Fingerprint").c_str());

  if (auto sv = ReadString(key, L"SchemaVersion")) {
    try {
      cfg.schema_version = std::stoi(*sv);
    } catch (...) {
      cfg.schema_version = 0;
    }
  }

  if (auto r = ReadString(key, L"Renderer"))
    cfg.renderer = Deserialize(bd::WideToUtf8(*r));

  if (auto v = ReadString(key, L"AppVersion"))
    cfg.app_version = bd::WideToUtf8(*v);
  Migrate(cfg);

  const auto default_xex = cfg.game_data_path() / "default.xex";
  if (!std::filesystem::exists(default_xex)) {
    BD_WARN("Install registry present but {} missing - treating as uninstalled",
            default_xex.string());
    return std::nullopt;
  }

  return cfg;
}

bool WriteInstallRegistry(const InstallConfig &config) {
  HKEY key = nullptr;
  LONG create_status = RegCreateKeyExW(HKEY_CURRENT_USER, kInstallKey, 0,
                                       nullptr, REG_OPTION_NON_VOLATILE,
                                       KEY_WRITE, nullptr, &key, nullptr);
  if (create_status != ERROR_SUCCESS) {
    BD_ERROR("RegCreateKeyExW failed: {}", create_status);
    return false;
  }
  {
    struct KeyGuard {
      HKEY k;
      ~KeyGuard() {
        if (k)
          RegCloseKey(k);
      }
    } guard{key};

    bool ok = true;
    ok &= WriteString(key, L"InstallRoot", config.install_root.wstring());
    for (int i = 0; i < kDiscCount; ++i)
      ok &= WriteString(
          key, (L"Disc" + std::to_wstring(i + 1) + L"Fingerprint").c_str(),
          bd::Utf8ToWide(config.iso_fingerprints[i]));
    ok &= WriteString(key, L"Renderer",
                      bd::Utf8ToWide(Serialize(config.renderer)));
    ok &= WriteString(key, L"SchemaVersion",
                      std::to_wstring(kInstallSchemaVersion));
    ok &= WriteString(key, L"AppVersion",
                      bd::Utf8ToWide(REBLUE_VERSION_STRING));
    if (ok)
      return true;
    BD_ERROR("Failed to write one or more values to install registry");
  }
  // Partial write: clear so ReadInstallRegistry sees nullopt and re-runs the
  // installer.
  ClearInstallRegistry();
  return false;
}

bool ClearInstallRegistry() {
  LONG status = RegDeleteTreeW(HKEY_CURRENT_USER, kInstallKey);
  if (status == ERROR_SUCCESS || status == ERROR_FILE_NOT_FOUND)
    return true;
  BD_ERROR("RegDeleteTreeW failed: {}", status);
  return false;
}

} // namespace bd::installer

#else // non-Windows

#include <filesystem>
#include <fstream>

#include <toml++/toml.h>

#include "core/app_root.h"

namespace bd::installer {
namespace {

// HKCU equivalent: a per-user TOML file that survives moving or rebuilding the
// executable and is shared across checkouts.
std::filesystem::path InstallStorePath() {
  const auto base = bd::UserConfigFolder();
  return base.empty() ? std::filesystem::path{} : base / "install.toml";
}

} // namespace

std::optional<InstallConfig> ReadInstallRegistry() {
  const auto path = InstallStorePath();
  if (path.empty())
    return std::nullopt;
  std::error_code ec;
  if (!std::filesystem::exists(path, ec))
    return std::nullopt;

  InstallConfig cfg;
  try {
    toml::table t = toml::parse_file(path.string());
    cfg.install_root = t["install_root"].value_or(std::string{});
    for (int i = 0; i < kDiscCount; ++i)
      cfg.iso_fingerprints[i] =
          t["disc" + std::to_string(i + 1) + "_fingerprint"].value_or(
              std::string{});
    cfg.schema_version = t["schema_version"].value_or(0);
    cfg.app_version = t["app_version"].value_or(std::string{});
  } catch (const toml::parse_error &e) {
    BD_WARN("Install store {} unreadable: {}", path.string(), e.what());
    return std::nullopt;
  }
  if (cfg.install_root.empty())
    return std::nullopt;
  Migrate(cfg);

  const auto default_xex = cfg.game_data_path() / "default.xex";
  if (!std::filesystem::exists(default_xex, ec)) {
    BD_WARN("Install store present but {} missing - treating as uninstalled",
            default_xex.string());
    return std::nullopt;
  }
  return cfg;
}

bool WriteInstallRegistry(const InstallConfig &config) {
  const auto path = InstallStorePath();
  if (path.empty()) {
    BD_ERROR("Install store: neither XDG_CONFIG_HOME nor HOME is set");
    return false;
  }
  std::error_code ec;
  std::filesystem::create_directories(path.parent_path(), ec);
  if (ec) {
    BD_ERROR("Install store: cannot create {}: {}", path.parent_path().string(),
             ec.message());
    return false;
  }

  toml::table t;
  t.insert("install_root", config.install_root.string());
  for (int i = 0; i < kDiscCount; ++i)
    t.insert("disc" + std::to_string(i + 1) + "_fingerprint",
             config.iso_fingerprints[i]);
  t.insert("schema_version", static_cast<i64>(kInstallSchemaVersion));
  t.insert("app_version", std::string(REBLUE_VERSION_STRING));

  // Atomic replace: write a sibling temp file, then rename over the store.
  const auto tmp = path.parent_path() / "install.toml.tmp";
  {
    std::ofstream out(tmp, std::ios::trunc);
    if (!out) {
      BD_ERROR("Install store: cannot write {}", tmp.string());
      return false;
    }
    out << t << '\n';
    out.flush();
    if (!out.good()) {
      BD_ERROR("Install store: write to {} failed", tmp.string());
      std::filesystem::remove(tmp, ec);
      return false;
    }
  }
  std::filesystem::rename(tmp, path, ec);
  if (ec) {
    BD_ERROR("Install store: rename to {} failed: {}", path.string(),
             ec.message());
    std::filesystem::remove(tmp, ec);
    return false;
  }
  return true;
}

bool ClearInstallRegistry() {
  const auto path = InstallStorePath();
  if (path.empty())
    return true;
  std::error_code ec;
  std::filesystem::remove(path, ec);
  return !std::filesystem::exists(path, ec);
}

} // namespace bd::installer

#endif
