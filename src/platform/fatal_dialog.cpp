/**
 * @file    platform/fatal_dialog.cpp
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#include "platform/fatal_dialog.h"

#include "core/encoding.h"
#include "core/logging.h"

#if defined(_WIN32)
#include "core/windows_lean.h"

namespace bd::platform {
namespace {

// A borderless-fullscreen game window covers the screen and holds foreground,
// so a plain MessageBox renders behind it and never grabs focus. Force this
// process's visible top-level windows down first. SW_FORCEMINIMIZE minimizes
// even a wedged window thread, and ShowWindowAsync never blocks on it.
BOOL CALLBACK MinimizeOwnTopLevelWindow(HWND hwnd, LPARAM) {
  DWORD pid = 0;
  GetWindowThreadProcessId(hwnd, &pid);
  if (pid == GetCurrentProcessId() && IsWindowVisible(hwnd) &&
      GetWindow(hwnd, GW_OWNER) == nullptr) {
    ShowWindowAsync(hwnd, SW_FORCEMINIMIZE);
  }
  return TRUE;
}

} // namespace

void ShowFatalError(std::string_view title, std::string_view body) {
  BD_ERROR("Fatal: {} - {}", title, body);
  // Drop the (possibly fullscreen) game window, then show the dialog topmost
  // and in the foreground so it is always visible and grabs focus.
  EnumWindows(MinimizeOwnTopLevelWindow, 0);
  MessageBoxW(nullptr, bd::Utf8ToWide(body).c_str(),
              bd::Utf8ToWide(title).c_str(),
              MB_OK | MB_ICONERROR | MB_TOPMOST | MB_SETFOREGROUND);
}

} // namespace bd::platform

#elif defined(__APPLE__)

#include <SDL3/SDL.h>

#include <string>

namespace bd::platform {

void ShowFatalError(std::string_view title, std::string_view body) {
  BD_ERROR("Fatal: {} - {}", title, body);
  if (!SDL_IsMainThread()) {
    BD_WARN("fatal dialog suppressed (not on the main thread, AppKit alerts "
            "are main-thread only) - see the logged error above");
    return;
  }
  const std::string t(title);
  const std::string b(body);
  SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, t.c_str(), b.c_str(), nullptr);
}

} // namespace bd::platform

#else

#include <SDL3/SDL.h>

#include <string>

namespace bd::platform {

void ShowFatalError(std::string_view title, std::string_view body) {
  BD_ERROR("Fatal: {} - {}", title, body);

  // SDL walks its bootstrap list for a message box, and the X11 entry stores
  // through controls.window ahead of the null check meant to guard it
  // (SDL_x11messagebox.c), so a display it cannot open SIGSEGVs and eats the
  // error being reported. Pinning the driver keeps the walk off the others.
  const bool owned = !SDL_WasInit(SDL_INIT_VIDEO);
  if (owned && !SDL_InitSubSystem(SDL_INIT_VIDEO)) {
    BD_WARN("no usable dialog backend ({}), the error above is log-only",
            SDL_GetError());
    return;
  }
  if (const char *driver = SDL_GetCurrentVideoDriver())
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, driver);

  const std::string t(title);
  const std::string b(body);
  SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, t.c_str(), b.c_str(), nullptr);

  if (owned)
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
}
} // namespace bd::platform

#endif
