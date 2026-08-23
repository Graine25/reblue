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
  std::lock_guard lock(mutex_);
  target_ = std::clamp(target, 0.0f, 1.0f);
  rate_ = seconds > 0.0f ? 1.0f / seconds : 0.0f;
  if (seconds <= 0.0f)
    level_ = target_;
}

bool ScreenFade::IsOpaque() const {
  std::lock_guard lock(mutex_);
  return level_ >= 1.0f;
}

bool ScreenFade::IsClear() const {
  std::lock_guard lock(mutex_);
  return level_ <= 0.0f;
}

f32 ScreenFade::Step(f32 dt) {
  std::lock_guard lock(mutex_);
  if (level_ == target_)
    return level_;
  const f32 sweep = rate_ * dt;
  level_ = level_ < target_ ? std::min(level_ + sweep, target_)
                            : std::max(level_ - sweep, target_);
  return level_;
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
