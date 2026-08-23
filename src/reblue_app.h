/**
 * @file    reblue_app.h
 * @brief   ReblueApp: the application class wiring paths, installer, and
 * renderer.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#pragma once

#include <atomic>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <thread>

#include <rex/rex_app.h>

#include "installer/installer.h"
#include "ui/ui.h"

struct ImFontAtlas;

class ReblueApp : public rex::ReXApp {
public:
  static std::unique_ptr<rex::ui::WindowedApp>
  Create(rex::ui::WindowedAppContext &ctx);
  ~ReblueApp() override;

protected:
  void OnPostInitLogging() override;
  void OnPreSetup(rex::RuntimeConfig &config) override;
  void OnConfigureFonts(ImFontAtlas *atlas) override;
  void OnConfigureStyle(ImGuiStyle &imgui_style,
                        rex::ui::Style &ui_style) override;
  void OnCreateDialogs(rex::ui::ImGuiDrawer *drawer) override;
  std::unique_ptr<rex::ui::ImmediateDrawer> OnCreateImmediateDrawer() override;
  std::optional<rex::PathConfig>
  OnFinalizePaths(const rex::PathConfig &defaults,
                  std::function<void(rex::PathConfig)> resume) override;
  void OnConfigurePaths(rex::PathConfig &paths) override;
  void OnPreLaunchModule() override;
  void OnWindowPixelSizeChanged(u32 pixel_width, u32 pixel_height) override;
  bool OnWindowCloseRequested() override;
  void OnShutdown() override;

private:
  explicit ReblueApp(rex::ui::WindowedAppContext &ctx);

#ifdef REBLUE_BUILD_INSTALLER
  void FinishInstaller(rex::PathConfig defaults,
                       std::function<void(rex::PathConfig)> resume,
                       bool completed, const bd::installer::InstallConfig &cfg,
                       const bd::installer::WizardChoices &choices);
#endif

  // Pre-guest UI plumbing for the installer wizard: bring up the native device
  // + overlay, then a tick thread that presents frames until the wizard
  // resolves. No Runtime/guest exists yet.
  bool BeginPreGuestUI();
  void StartPreGuestPump();
  void PumpPreGuestFrame();
  void StopPreGuestPump();
  void InstallOverlayDrawHook();

  void SetPerfOverlayStage(bd::ui::OverlayStage stage,
                           rex::ui::ImGuiDrawer *drawer);

  // Owned via the derived type: ImGuiDialog's destructor is non-virtual.
#ifdef REBLUE_BUILD_INSTALLER
  std::unique_ptr<bd::installer::InstallerWizard> installer_wizard_;
#endif
  std::unique_ptr<bd::ui::PerfOverlay> perf_overlay_;
  std::unique_ptr<bd::ui::WatermarkOverlay> watermark_;

  // Raw observer: ImGuiDialog self-deletes on Close(), and the on_closed lambda
  // nulls this back to nullptr.
  bd::ui::ReportIssueDialog *report_issue_ = nullptr;

  std::thread pre_guest_tick_thread_;
  std::atomic<bool> pre_guest_pump_stop_{false};
  std::atomic<bool> pre_guest_tick_pending_{false};

  // Active profile, resolved once in OnConfigurePaths from the 'profile' cvar.
  // profile_root_ = install_root_/"profiles"/active_profile_ = user_data_root.
  std::string active_profile_ = "default";
  std::filesystem::path install_root_;
  std::filesystem::path profile_root_;
};
