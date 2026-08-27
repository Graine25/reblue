/**
 * @file    ui/watermark.h
 * @brief   Build watermark in the bottom-right corner, up with the F3 overlay.
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

class WatermarkOverlay final : public rex::ui::ImGuiDialog {
public:
  explicit WatermarkOverlay(rex::ui::ImGuiDrawer *drawer);
  ~WatermarkOverlay();

protected:
  void OnDraw(ImGuiIO &io) override;
};

} // namespace bd::ui
