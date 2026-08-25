/**
 * @file    installer/upgrade_prompt.h
 * @brief   Offers to copy this build over the install it was started against.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#pragma once

#include <filesystem>
#include <functional>
#include <string>

#include <rex/ui/imgui_dialog.h>

struct ImGuiIO;

namespace rex::ui {
class ImGuiDrawer;
class WindowedAppContext;
} // namespace rex::ui

namespace bd::installer {

// Raised before the renderer hands off to the guest, so it draws through the
// same pre-guest pump the wizard uses. A unique_ptr owns it as it owns the
// wizard, so this never calls Close(), and it answers through a deferred
// callback: the owner drops it there, and OnDraw must have returned first.
class UpgradePrompt final : public rex::ui::ImGuiDialog {
public:
  using DoneCallback = std::function<void(bool accepted)>;

  UpgradePrompt(rex::ui::ImGuiDrawer *drawer,
                rex::ui::WindowedAppContext &app_context,
                std::filesystem::path install_root, std::string installed,
                DoneCallback on_done);

protected:
  void OnDraw(ImGuiIO &io) override;

private:
  void Answer(bool accepted);

  rex::ui::WindowedAppContext &app_context_;
  std::filesystem::path install_root_;
  std::string installed_; // the version the install records today
  DoneCallback on_done_;
  bool answered_ = false;
};

} // namespace bd::installer
