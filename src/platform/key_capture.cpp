/**
 * @file    platform/key_capture.cpp
 * @license BSD 3-Clause, see LICENSE
 */
#include "platform/key_capture.h"

#include <string_view>

#include <rex/ui/keybinds.h>
#include <rex/ui/virtual_key.h>

#include "platform/keyboard_input.h"
#include "platform/mouse_input.h"

namespace bd::platform {
namespace {

bool s_prev_down[kBindableKeyCount] = {};
bool s_capturing = false;
// The hit waiting for its key to come back up, with the modifier prefix taken
// at the press, since the modifier may be released first.
int s_pending = -1;
std::string s_pendingPrefix;

// The three mouse names sit in the same bindable list as the keys, and
// ParseVirtualKey even resolves them, but a mouse button never reaches the
// keyboard tracker: that is fed by key events alone. Polled against the
// keyboard they read as permanently up, so a mouse button could be typed into
// a config file and never captured from the bind screen.
struct MouseName {
  const char *name;
  rex::ui::MouseEvent::Button button;
};

constexpr MouseName kMouseButtons[] = {
    {"LMB", rex::ui::MouseEvent::Button::kLeft},
    {"RMB", rex::ui::MouseEvent::Button::kRight},
    {"MMB", rex::ui::MouseEvent::Button::kMiddle},
};

bool KeyDown(size_t i) {
  const std::string_view name = kBindableKeys[i];
  for (const MouseName &mb : kMouseButtons)
    if (name == mb.name)
      return Mouse().IsButtonDown(mb.button);

  auto vk = rex::ui::ParseVirtualKey(name);
  if (vk == rex::ui::VirtualKey::kNone)
    return false;
  return Keyboard().IsDown(vk);
}

std::string ModifierPrefix() {
  const u8 mods = Keyboard().Modifiers();
  std::string prefix;
  if (mods & KeyboardInput::kModCtrl)
    prefix += "Ctrl+";
  if (mods & KeyboardInput::kModAlt)
    prefix += "Alt+";
  if (mods & KeyboardInput::kModShift)
    prefix += "Shift+";
  return prefix;
}

} // namespace

void BeginKeyCapture() {
  for (size_t i = 0; i < kBindableKeyCount; ++i)
    s_prev_down[i] = KeyDown(i);
  s_capturing = true;
  s_pending = -1;
}

std::string PollKeyCapture() {
  if (!s_capturing)
    return {};

  if (s_pending >= 0) {
    if (KeyDown(size_t(s_pending)))
      return {};
    s_capturing = false;
    const int hit = s_pending;
    s_pending = -1;
    return s_pendingPrefix + kBindableKeys[hit];
  }

  for (size_t i = 0; i < kBindableKeyCount; ++i) {
    const bool down = KeyDown(i);
    if (down && !s_prev_down[i] && s_pending < 0) {
      s_pending = int(i);
      s_pendingPrefix = ModifierPrefix();
    }
    s_prev_down[i] = down;
  }
  return {};
}

bool KeyCapturePending() { return s_capturing && s_pending >= 0; }

} // namespace bd::platform
