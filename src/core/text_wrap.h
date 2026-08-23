/**
 * @file    core/text_wrap.h
 * @brief   Greedy word wrap for fixed-width d2anime message lines.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#pragma once

#include <array>
#include <string>

namespace bd {

// Wraps at maxChars, breaking on spaces. An embedded newline forces a break.
std::string WordWrap(const std::string &text, int maxChars);

// The first two wrapped lines, for the two-message description bands the menus
// draw. Anything past them is dropped, since there is no third line to put it
// on.
std::array<std::string, 2> WrapTwoLines(const std::string &text, int maxChars);

} // namespace bd
