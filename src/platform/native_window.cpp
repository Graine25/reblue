/**
 * @file    platform/native_window.cpp
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#include "platform/native_window.h"

#include <SDL3/SDL_video.h>
#if defined(__APPLE__)
#include <SDL3/SDL_metal.h>

#include <mach-o/dyld.h>
#include <unistd.h>

#include <cstdint>
#include <cstdlib>
#include <string>
#endif

#include <rex/ui/window.h>

#include "core/logging.h"
#include "platform/host_resources.h"

namespace bd::platform {

#if defined(_WIN32)

bool GetNativeRenderWindow(rex::ui::Window *window, plume::RenderWindow &out) {
  out = static_cast<plume::RenderWindow>(window->GetNativeWindowHandle());
  if (!out) {
    BD_ERROR("Window has no native HWND yet");
    return false;
  }
  return true;
}

#elif defined(__APPLE__)

namespace {

void SetenvIfUnset(const char *name, const char *value) {
  if (const char *cur = std::getenv(name); !cur || !cur[0])
    setenv(name, value, 1);
}

bool HasMoltenVK(const std::string &dir) {
  return access((dir + "/libMoltenVK.dylib").c_str(), R_OK) == 0;
}

// True when the ':'-separated 'list' has 'dir' as one of its entries.
bool PathListHas(const std::string &list, const std::string &dir) {
  for (size_t start = 0; start <= list.size();) {
    size_t end = list.find(':', start);
    if (end == std::string::npos)
      end = list.size();
    if (list.compare(start, end - start, dir) == 0)
      return true;
    start = end + 1;
  }
  return false;
}

// volk dlopen's libvulkan/libMoltenVK by leaf name, and dyld snapshots its
// DYLD_* search paths at launch, so setting them in-process is too late: the
// run that needs them has to be a fresh exec. Priority 101 (the lowest the
// attribute allows) puts this ahead of every other initializer in the image, so
// the exec cannot tear down state one of them already built.
//
// Only the SDK-staged runtime goes on DYLD_LIBRARY_PATH. dyld checks every
// entry of that variable against the leaf name of every library the process
// loads, system ones included, and the boot volume ignores case. With
// /opt/homebrew/lib there, Homebrew's libpng/libjpeg/libtiff/libgif/liblzma
// replaced ImageIO's private libPng/libJPEG/libTIFF/libGIF.dylib and
// /usr/lib/liblzma.5.dylib. They lack the _cg_* symbols ImageIO imports, dyld
// binds each missing one to 0xbad4007, and the first ImageIO image decode (the
// Metal performance HUD drawing an emoji glyph when Shift+F9 turns it on) died
// with SIGBUS. Shared prefixes such as Homebrew's go on
// DYLD_FALLBACK_LIBRARY_PATH, after the system dirs: dyld only searches it for
// a library it found nowhere else, so it cannot replace a system one.
__attribute__((constructor(101))) void
ConfigureMoltenVKAndReexec(int /*argc*/, char **argv) {
  RaiseFDLimit();

  SetenvIfUnset("SDL_MAC_PRESS_AND_HOLD", "0");

  SetenvIfUnset("MVK_CONFIG_SYNCHRONOUS_QUEUE_SUBMITS", "1");
  // Style 3 is the single-Metal-queue mode.
  SetenvIfUnset("MVK_CONFIG_SEMAPHORE_SUPPORT_STYLE", "3");
  SetenvIfUnset("MVK_CONFIG_PRESENT_WITH_COMMAND_BUFFER", "1");

  if (std::getenv("REBLUE_MOLTENVK_RELAUNCHED"))
    return; // already relaunched

  char exe_buf[4096];
  uint32_t exe_size = sizeof(exe_buf);
  if (_NSGetExecutablePath(exe_buf, &exe_size) != 0)
    return;
  const std::string exe(exe_buf);
  const std::string exe_dir = exe.substr(0, exe.find_last_of('/'));

  // SDK-staged runtime (rexglue_configure_target). It holds only the Vulkan
  // loader and MoltenVK, so it is safe ahead of the system libraries.
  const std::string staged = exe_dir + "/vulkan/lib";
  const char *cur_lib = std::getenv("DYLD_LIBRARY_PATH");
  const std::string old_lib = cur_lib ? cur_lib : "";
  std::string new_lib = old_lib;
  if (HasMoltenVK(staged) && !PathListHas(old_lib, staged))
    new_lib = old_lib.empty() ? staged : staged + ":" + old_lib;

  // Homebrew molten-vk and its Vulkan loader (volk asks for libvulkan.dylib
  // first, which the staged dir lacks, so a dev build loads Homebrew's loader).
  // The list starts from /usr/local/lib:/usr/lib, dyld's built-in fallback for
  // binaries built before Fall 2023 (newer ones have none), so setting the
  // variable drops no directory dyld would otherwise search.
  const char *cur_fb = std::getenv("DYLD_FALLBACK_LIBRARY_PATH");
  const bool had_fb = cur_fb && *cur_fb;
  const std::string old_fb = had_fb ? cur_fb : "/usr/local/lib:/usr/lib";
  std::string new_fb = old_fb;
  for (const char *dir : {"/opt/homebrew/lib", "/usr/local/lib"}) {
    if (HasMoltenVK(dir) && !PathListHas(new_fb, dir)) {
      new_fb += ':';
      new_fb += dir;
    }
  }

  if (new_lib == old_lib && new_fb == old_fb)
    return; // nothing to add, volk will report any failure

  if (new_lib != old_lib)
    setenv("DYLD_LIBRARY_PATH", new_lib.c_str(), 1);
  if (new_fb != old_fb)
    setenv("DYLD_FALLBACK_LIBRARY_PATH", new_fb.c_str(), 1);
  setenv("REBLUE_MOLTENVK_RELAUNCHED", "1", 1);
  execv(exe.c_str(), argv);
  // execv only returns on failure, fall through and let startup continue.
}

} // namespace

bool GetNativeRenderWindow(rex::ui::Window *window, plume::RenderWindow &out) {
  // The SDK owns the single SDL window, fetch it the same way the X11 path and
  // reblue_app.cpp's ApplyWindowSizeConstraints do.
  (void)window;
  int count = 0;
  SDL_Window **windows = SDL_GetWindows(&count);
  SDL_Window *sdl_window = (windows && count > 0) ? windows[0] : nullptr;
  SDL_free(windows);
  if (!sdl_window) {
    BD_ERROR("No SDL window available for the Vulkan surface");
    return false;
  }

  SDL_PropertiesID props = SDL_GetWindowProperties(sdl_window);
  out.window = SDL_GetPointerProperty(
      props, SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, nullptr);

  // The view is a subview of the SDL window and dies with it, so it is cached
  // against the window it was made for rather than unconditionally: a
  // recreated window gets a fresh view instead of a dangling one.
  static SDL_Window *view_owner = nullptr;
  static SDL_MetalView metal_view = nullptr;
  if (!metal_view || view_owner != sdl_window) {
    metal_view = SDL_Metal_CreateView(sdl_window);
    view_owner = sdl_window;
  }
  if (!metal_view) {
    BD_ERROR("SDL_Metal_CreateView failed: {}", SDL_GetError());
    return false;
  }
  out.view = SDL_Metal_GetLayer(metal_view);

  if (!out.window || !out.view) {
    BD_ERROR(
        "SDL window exposed no NSWindow/CAMetalLayer for the Metal surface");
    return false;
  }
  return true;
}

#else

bool GetNativeRenderWindow(rex::ui::Window *window, plume::RenderWindow &out) {
  // The SDK exposes no SDL_Window accessor and GetNativeWindowHandle is null
  // off Windows. The app owns a single window, fetched the same way
  // ApplyWindowSizeConstraints does. plume takes it whole so the surface
  // follows whichever video driver SDL picked.
  (void)window;
  int count = 0;
  SDL_Window **windows = SDL_GetWindows(&count);
  out = (windows && count > 0) ? windows[0] : nullptr;
  SDL_free(windows);
  if (!out) {
    BD_ERROR("No SDL window available for the Vulkan surface");
    return false;
  }
  return true;
}

#endif

} // namespace bd::platform
