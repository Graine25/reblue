/**
 * @file    ui/screen_fade.cpp
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#include "ui/screen_fade.h"

#include <algorithm>

#include <imgui.h>

namespace bd::ui {

ScreenFade &ScreenFade::Get() {
  static ScreenFade instance;
  return instance;
}

void ScreenFade::FadeTo(f32 target, f32 seconds) {
  target_.store(std::clamp(target, 0.0f, 1.0f));
  rate_.store(seconds > 0.0f ? 1.0f / seconds : 0.0f);
  if (seconds <= 0.0f)
    level_.store(target_.load());
}

f32 ScreenFade::Step(f32 dt) {
  const f32 target = target_.load();
  f32 level = level_.load();
  if (level == target)
    return level;
  const f32 sweep = rate_.load() * dt;
  level = level < target ? std::min(level + sweep, target)
                         : std::max(level - sweep, target);
  level_.store(level);
  return level;
}

FadeOverlay::FadeOverlay(rex::ui::ImGuiDrawer *drawer)
    : rex::ui::ImGuiDialog(drawer) {}

FadeOverlay::~FadeOverlay() = default;

void FadeOverlay::OnDraw(ImGuiIO &io) {
  const f32 level = ScreenFade::Get().Step(io.DeltaTime);
  if (level <= 0.0f)
    return;
  const u32 alpha =
      static_cast<u32>(std::clamp(level, 0.0f, 1.0f) * 255.0f + 0.5f);
  ImGui::GetForegroundDrawList()->AddRectFilled(
      ImVec2(0.0f, 0.0f), io.DisplaySize, IM_COL32(0, 0, 0, alpha));
}

} // namespace bd::ui
