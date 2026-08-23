/**
 * @file    platform/host_devices.cpp
 * @license BSD 3-Clause, see LICENSE
 */
#include "platform/host_devices.h"

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_gamepad.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_video.h>

#include "core/logging.h"

namespace bd::platform {

namespace {

// Valve's built-in Steam Deck controls. SDL has no gamepad type for them, so
// the pair is the only thing that tells a Deck from a generic pad.
constexpr Uint16 kValveVendor = 0x28DE;
constexpr Uint16 kSteamDeckProduct = 0x1205;

// SDL links statically into both the runtime DLL and this executable, so the
// copy these queries reach is not the one the runtime started: its display
// list stays empty until the subsystem is initialized here too. Cocoa is the
// exception, since bringing video up there touches AppKit from whichever
// thread opened the settings, and SDL_IsMainThread cannot say which that is
// while this copy has no main thread of its own.
bool VideoReady() {
#if defined(__APPLE__)
  return SDL_WasInit(SDL_INIT_VIDEO) != 0;
#else
  static const bool ready = [] {
    if (SDL_WasInit(SDL_INIT_VIDEO))
      return true;
    if (SDL_InitSubSystem(SDL_INIT_VIDEO))
      return true;
    BD_WARN("display enumeration unavailable: {}", SDL_GetError());
    return false;
  }();
  return ready;
#endif
}

PadBrand BrandOf(SDL_JoystickID id) {
  if (SDL_GetGamepadVendorForID(id) == kValveVendor &&
      SDL_GetGamepadProductForID(id) == kSteamDeckProduct)
    return PadBrand::SteamDeck;

  switch (SDL_GetGamepadTypeForID(id)) {
  case SDL_GAMEPAD_TYPE_XBOX360:
    return PadBrand::Xbox360;
  case SDL_GAMEPAD_TYPE_XBOXONE:
    return PadBrand::XboxSeries;
  case SDL_GAMEPAD_TYPE_PS3:
  case SDL_GAMEPAD_TYPE_PS4:
  case SDL_GAMEPAD_TYPE_PS5:
    return PadBrand::PlayStation;
  case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO:
  case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_LEFT:
  case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT:
  case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_PAIR:
    return PadBrand::Switch;
  default:
    return PadBrand::Unknown;
  }
}

} // namespace

PadBrand ConnectedPad() {
  int count = 0;
  SDL_JoystickID *ids = SDL_GetGamepads(&count);
  if (!ids)
    return PadBrand::Unknown;
  const PadBrand brand = count > 0 ? BrandOf(ids[0]) : PadBrand::Unknown;
  SDL_free(ids);
  return brand;
}

int DisplayCount() {
  if (!VideoReady())
    return 0;
  int count = 0;
  SDL_DisplayID *ids = SDL_GetDisplays(&count);
  if (!ids)
    return 0;
  SDL_free(ids);
  return count;
}

std::string DisplayName(int index) {
  if (!VideoReady())
    return std::string();
  int count = 0;
  SDL_DisplayID *ids = SDL_GetDisplays(&count);
  if (!ids)
    return std::string();
  std::string name;
  if (index >= 1 && index <= count) {
    if (const char *n = SDL_GetDisplayName(ids[index - 1]))
      name = n;
  }
  SDL_free(ids);
  return name;
}

} // namespace bd::platform
