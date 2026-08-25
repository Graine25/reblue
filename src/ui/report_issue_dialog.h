/**
 * @file    ui/report_issue_dialog.h
 * @brief   F12 "Report an Issue" dialog: captures a screenshot + debug info to
 *          a timestamped folder under <cache>/reports.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#pragma once

#include <filesystem>
#include <functional>
#include <rex/types.h>

#include <rex/ui/imgui_dialog.h>

#include "engine/engine.h"

struct ImGuiIO;

namespace rex::ui {
class ImGuiDrawer;
} // namespace rex::ui

namespace bd::ui {

// Paths + window dims resolved by the caller (ReblueApp) at open time.
struct ReportContext {
  std::filesystem::path reports_root; // <cache>/reports
  std::filesystem::path logs_dir;     // <exe>/logs
  u32 window_width = 0;
  u32 window_height = 0;
};

class ReportIssueDialog final : public rex::ui::ImGuiDialog {
public:
  ReportIssueDialog(rex::ui::ImGuiDrawer *drawer, ReportContext ctx,
                    std::function<void()> on_closed);
  ~ReportIssueDialog();

  // Close requested externally (e.g. F12 pressed again while open).
  void RequestClose();

protected:
  void OnDraw(ImGuiIO &io) override;
  void OnClose() override;

private:
  engine::HostPointerClaim pointer_;
  ReportContext ctx_;
  std::function<void()> on_closed_;
  char description_[4096] = {0};
  bool submitted_ = false;           // switched to the saved-confirmation view
  std::filesystem::path saved_path_; // the written .zip (empty == save failed)
};

} // namespace bd::ui
