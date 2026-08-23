/**
 * @file    platform/reboot.cpp
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#include "platform/reboot.h"
#include "core/app_root.h"
#include "core/logging.h"

#include <atomic>
#include <cctype>
#include <filesystem>
#include <mutex>
#include <string>

#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/logging.h>
#include <rex/platform/env.h>

#if defined(_WIN32)
#include "core/windows_lean.h"
#else
#include <cerrno>
#include <unistd.h>
#include <vector>
#endif

namespace bd::platform {
namespace {

std::function<void()> g_handler;
std::mutex g_handler_mutex;
std::atomic<bool> g_requested{false};
std::string g_active_profile;
std::filesystem::path g_config_path;

// /proc/self/exe inside an AppImage is the inner binary in a FUSE mount that
// dies with this process, so relaunching must use the AppImage file itself.
std::filesystem::path ExecutablePath() {
#if !defined(_WIN32) && !defined(__APPLE__)
  if (auto appimage = rex::platform::env::get("APPIMAGE");
      appimage && !appimage->empty())
    return std::filesystem::path(*appimage);
#endif
  return rex::filesystem::GetExecutablePath();
}

#if defined(_WIN32)
bool SpawnFreshInstance(const std::filesystem::path &exe) {
  std::wstring cmdline = L"\"" + exe.wstring() + L"\"";
  if (!g_active_profile.empty()) {
    std::wstring wprofile(g_active_profile.begin(), g_active_profile.end());
    cmdline += L" --profile \"" + wprofile + L"\"";
  }
  std::wstring workdir = exe.parent_path().wstring();

  STARTUPINFOW si{};
  si.cb = sizeof(si);
  PROCESS_INFORMATION pi{};

  if (!::CreateProcessW(exe.c_str(), cmdline.data(), nullptr, nullptr, FALSE, 0,
                        nullptr, workdir.c_str(), &si, &pi)) {
    BD_ERROR("[reboot] CreateProcessW failed (err {})", ::GetLastError());
    return false;
  }
  ::CloseHandle(pi.hThread);
  ::CloseHandle(pi.hProcess);
  return true;
}
#else
bool SpawnFreshInstance(const std::filesystem::path &exe) {
  // Mirror the Win32 branch: relaunch with only --profile. The fresh instance
  // re-resolves the install root from the install store.
  std::vector<std::string> args;
  args.push_back(exe.string());
  if (!g_active_profile.empty()) {
    args.push_back("--profile");
    args.push_back(g_active_profile);
  }
  std::vector<char *> argv;
  argv.reserve(args.size() + 1);
  for (auto &a : args)
    argv.push_back(a.data());
  argv.push_back(nullptr);

  pid_t pid = ::fork();
  if (pid < 0) {
    BD_ERROR("[reboot] fork failed (errno {})", errno);
    return false;
  }
  if (pid == 0) {
    ::setsid(); // detach so the child outlives the exiting parent
    std::error_code ec;
    std::filesystem::current_path(exe.parent_path(), ec);
    ::execv(exe.c_str(), argv.data());
    ::_exit(127); // execv returns only on failure
  }
  return true;
}
#endif

bool DetectSteamGameMode() {
  // gamescope-session, the session Game Mode runs, stamps itself here.
  if (auto desktop = rex::platform::env::get("XDG_CURRENT_DESKTOP")) {
    std::string lowered = *desktop;
    for (char &c : lowered)
      c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (lowered.find("gamescope") != std::string::npos)
      return true;
  }
  // Exported by gamescope to everything it launches. Desktop Mode does not set
  // it unless the user asked for a gamescope wrapper, where exiting instead of
  // relaunching is a harmless degradation.
  if (auto display = rex::platform::env::get("GAMESCOPE_WAYLAND_DISPLAY");
      display && !display->empty())
    return true;
  return false;
}

} // namespace

bool IsSteamGameMode() {
  static const bool game_mode = DetectSteamGameMode();
  return game_mode;
}

std::filesystem::path ConfigFilePath() {
  if (!g_config_path.empty())
    return g_config_path;
  return AppRootFolder() / "reblue.toml";
}

void SetProfileContext(std::string profile_name,
                       std::filesystem::path config_path) {
  g_active_profile = std::move(profile_name);
  g_config_path = std::move(config_path);
}

void SetWarmRebootHandler(std::function<void()> handler) {
  std::lock_guard<std::mutex> lk(g_handler_mutex);
  g_handler = std::move(handler);
}

void RequestWarmReboot() {
  bool expected = false;
  if (!g_requested.compare_exchange_strong(expected, true))
    return;

  std::function<void()> handler;
  {
    std::lock_guard<std::mutex> lk(g_handler_mutex);
    handler = g_handler;
  }
  if (!handler) {
    BD_ERROR("[reboot] no handler registered, cannot relaunch");
    g_requested.store(false);
    return;
  }
  BD_WARN("[reboot] warm reboot requested");
  handler();
}

[[noreturn]] void PerformWarmReboot(const std::function<void()> &quiesce) {
  auto exe = ExecutablePath();

  // 1. Settings are not auto-persisted, so write them before relaunch.
  rex::cvar::SaveConfig(ConfigFilePath());

  // 2. Quiesce and release the GPU first: the replacement creates its own
  //    device and swap chain, and two processes owning the window's surface at
  //    once is exactly the overlap that wedges drivers.
  if (quiesce)
    quiesce();

  // 3. Steam owns the launched process in Game Mode, so a spawn from here would
  //    start outside the session with no focus while Steam counts the game as
  //    stopped. Exit clean and let the user press play again.
  if (IsSteamGameMode()) {
    BD_WARN("[reboot] steam game mode: exiting instead of relaunching");
    rex::FlushLogging();
    std::_Exit(0);
  }

  // 4. Spawn after the drain: if it fails there is nothing left to render with,
  //    so this is a hard failure rather than the old stay-in-session fallback.
  if (!SpawnFreshInstance(exe)) {
    BD_ERROR("[reboot] relaunch failed after teardown, exiting");
    rex::FlushLogging();
    std::_Exit(1);
  }

  // 5. Flush the writers that stay up. No subsystem teardown beyond the
  // renderer (it
  //    would deadlock on a host lock a straggler still holds).
  rex::FlushLogging();

  // 6. Exit without running destructors.
  std::_Exit(0);
}

} // namespace bd::platform
