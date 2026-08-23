/**
 * @file    ui/theme.h
 * @brief   re:Blue chrome palette for the host ImGui overlays.
 *
 * @copyright   Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *              All rights reserved.
 * @license     BSD 3-Clause - see LICENSE
 */
#pragma once

#include <imgui.h>
#include <rex/ui/style.h>

namespace bd::ui {

// The launcher accent fills every surface and control face, white carries every
// foreground mark, dimmed by alpha where a row reads as secondary.
struct Theme {
  // #151033 and the shades derived from it.
  static constexpr ImVec4 kAccent{0.082f, 0.063f, 0.200f, 1.00f};
  static constexpr ImVec4 kAccentDeep{0.049f, 0.039f, 0.122f, 1.00f};
  static constexpr ImVec4 kAccentHovered{0.141f, 0.114f, 0.341f, 1.00f};
  static constexpr ImVec4 kAccentActive{0.208f, 0.169f, 0.494f, 1.00f};
  static constexpr ImVec4 kAccentSelected{0.267f, 0.212f, 0.651f, 1.00f};
  // Panel fill over the installer background image.
  static constexpr ImVec4 kPanel{0.049f, 0.039f, 0.122f, 0.86f};

  static constexpr ImVec4 White(float alpha) {
    return {1.00f, 1.00f, 1.00f, alpha};
  }

  static void Apply(ImGuiStyle &style, rex::ui::Style &overlays);
};

} // namespace bd::ui
