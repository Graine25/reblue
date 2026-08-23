/**
 * @file    engine/game_text.h
 * @brief   Resolves "@game:" catalog values against Blue Dragon's own message
 *          tables, so our menus can borrow the terms it already ships.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 */
#pragma once

namespace bd::engine {

// Hands bd::i18n a resolver over the installed packs. Safe to call before the
// game data root is known: lookups fail until it is.
void InstallGameTermResolver();

} // namespace bd::engine
