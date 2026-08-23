/**
 * @file    platform/host_devices.h
 * @license BSD 3-Clause, see LICENSE
 */
#pragma once

#include <string>

namespace bd::platform {

// Which console's art a connected pad wears. Unknown covers both an empty
// port and a pad the host cannot place.
enum class PadBrand {
  Unknown,
  Xbox360,
  XboxSeries,
  PlayStation,
  Switch,
  SteamDeck,
};

// Brand of the first pad the host has connected, in the connection order the
// input system numbers its devices by.
PadBrand ConnectedPad();

// Displays the host is driving, 0 before its video subsystem is up.
int DisplayCount();

// Host name of a display ("Generic PnP Monitor"), numbered from 1 the way the
// monitor cvar numbers them. Empty when the index names no display.
std::string DisplayName(int index);

} // namespace bd::platform
