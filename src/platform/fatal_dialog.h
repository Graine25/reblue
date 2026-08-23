/**
 * @file    platform/fatal_dialog.h
 * @brief   Blocking modal fatal-error dialog usable before presentation setup.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#pragma once

#include <string_view>

namespace bd::platform {

// Blocking. Needs no presenter or window, so it is safe before
// SetupPresentation(). Always logs. Shows a dialog via MessageBoxW on Windows
// and via SDL elsewhere, where it degrades to log-only if no dialog backend is
// reachable.
void ShowFatalError(std::string_view title, std::string_view body);

} // namespace bd::platform
