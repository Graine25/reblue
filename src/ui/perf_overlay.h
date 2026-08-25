/**
 * @file    ui/perf_overlay.h
 * @brief   Passive F3 performance overlay: KPI strip and ImPlot graphs.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#pragma once

#include <rex/ui/imgui_dialog.h>

struct ImGuiIO;

namespace rex::ui {
class ImGuiDrawer;
} // namespace rex::ui

namespace bd::ui {

// What F3 cycles through, and the persisted value of bd_perf_overlay. Ordered:
// each stage adds to the one before it.
enum class OverlayStage : int {
  Off = 0,
  Kpi = 1,
  Graphs = 2,
};
inline constexpr OverlayStage kLastOverlayStage = OverlayStage::Graphs;

// Wraps back to Off past the last stage.
inline constexpr OverlayStage NextOverlayStage(OverlayStage s) {
  return s >= kLastOverlayStage
             ? OverlayStage::Off
             : static_cast<OverlayStage>(static_cast<int>(s) + 1);
}

class PerfOverlay final : public rex::ui::ImGuiDialog {
public:
  explicit PerfOverlay(rex::ui::ImGuiDrawer *drawer);
  ~PerfOverlay();

  void SetStage(OverlayStage stage) { stage_ = stage; }

protected:
  void OnDraw(ImGuiIO &io) override;

private:
  void DrawKpiStrip(ImGuiIO &io);
  void DrawGraphs(ImGuiIO &io);

  OverlayStage stage_ = OverlayStage::Off;
};

} // namespace bd::ui
