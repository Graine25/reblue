/**
 * @file    engine/d2anime/anime_vars.h
 * @brief       Setters for the variables a d2anime CSV layout binds against.
 *
 * @copyright   Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *              All rights reserved.
 * @license     BSD 3-Clause - see LICENSE
 */
#pragma once

#include <string_view>

#include <rex/types.h>

namespace bd::engine {

void VarBagSetString(u32 varBag, const char *varName, const char *value);

// VarBagSetString runs its value through the engine's Shift-JIS widener, which
// mangles every byte >= 0x80. This writes the wstring directly instead.
void VarBagSetTextU16(u32 varBag, const char *varName,
                      std::u16string_view text);

// UTF-8 convenience wrapper over VarBagSetTextU16.
void VarBagSetText(u32 varBag, const char *varName, std::string_view utf8);
void VarBagSetColor(u32 varBag, const char *varName, u32 rgba);
void VarBagSetFloat(u32 varBag, const char *varName, double value);

// False when the bag has no such variable or it is not a float. Reads another
// anime's var the way AnimeVarBag_GetFloatVar does, for a value the
// engine owns and we only follow.
bool VarBagGetFloat(u32 varBag, const char *varName, float *out);

} // namespace bd::engine
