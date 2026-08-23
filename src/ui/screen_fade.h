/**
 * @file    ui/screen_fade.h
 * @brief   Full-screen fade veil for host-driven screen transitions.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#pragma once

#include <atomic>

#include <rex/types.h>
#include <rex/ui/imgui_dialog.h>

struct ImGuiIO;

namespace rex::ui {
class ImGuiDrawer;
} // namespace rex::ui

namespace bd::ui {

// A black veil over the whole output, swept between clear and opaque on wall
// time. The overlay advances it on the present thread, so it moves smoothly
// whatever the guest tick is doing, and the guest-side driver polls IsOpaque
// and IsClear to sequence what happens under it.
class ScreenFade {
public:
  static ScreenFade &Get();

  // Sweeps toward 'target' (0 clear, 1 opaque) over 'seconds' of full travel.
  void FadeTo(f32 target, f32 seconds);
  bool IsOpaque() const { return level_.load() >= 1.0f; }
  bool IsClear() const { return level_.load() <= 0.0f; }

  // Advances toward the target and returns the level. FadeOverlay's step.
  f32 Step(f32 dt);

private:
  std::atomic<f32> level_{0.0f};
  std::atomic<f32> target_{0.0f};
  std::atomic<f32> rate_{0.0f};
};

// Own via the concrete type: ImGuiDialog's destructor is not virtual.
class FadeOverlay final : public rex::ui::ImGuiDialog {
public:
  explicit FadeOverlay(rex::ui::ImGuiDrawer *drawer);
  ~FadeOverlay();

protected:
  void OnDraw(ImGuiIO &io) override;
};

} // namespace bd::ui
