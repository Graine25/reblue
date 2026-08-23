/**
 * @file    engine/d2anime/anime_vars.cpp
 * @copyright   Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *              All rights reserved.
 * @license     BSD 3-Clause - see LICENSE
 */
#include "engine/d2anime/anime_vars.h"
#include "core/encoding.h"
#include "core/memory_helpers.h"
#include "engine/d2anime/d2anime_types.h"
#include "reblue_init.h"

#include <cstddef>
#include <vector>

#include <rex/ppc/stack.h>
#include <rex/types.h>

REX_IMPORT(__imp__AnimeVarBag_SetStringVar, VarBag_SetString,
           void(u32, u32, u32));
REX_IMPORT(__imp__AnimeVarBag_SetColorRGBA, VarBag_SetColor,
           void(u32, u32, u32));
REX_IMPORT(__imp__AnimeVarBag_SetFloatVar, VarBag_SetFloat,
           void(u32, u32, f64));
REX_IMPORT(__imp__AnimeVarBag_FindVar, VarBag_FindVar, u32(u32, u32));
REX_IMPORT(__imp__AnimeVar_PropagateStringToElements, Var_PropagateString,
           void(u32));
// std::wstring::assign(const wchar_t*, size_t), the tail of
// AnimeVar_SetWideString once its Shift-JIS conversion is done.
REX_IMPORT(__imp__WString__Assign, WString_Assign, u32(u32, u32, u32));

namespace bd::engine {

void VarBagSetString(u32 varBag, const char *varName, const char *value) {
  rex::ppc::stack_guard guard;
  u32 name = rex::ppc::stack_push_string(varName);
  u32 val = rex::ppc::stack_push_string(value);
  VarBag_SetString(varBag, name, val);
}

void VarBagSetTextU16(u32 varBag, const char *varName,
                      std::u16string_view text) {
  rex::ppc::stack_guard guard;
  u32 name = rex::ppc::stack_push_string(varName);
  u32 entry = VarBag_FindVar(varBag, name);
  auto *var = bd::mem::at<AnimeVar_t>(entry);
  if (!var || !var->Is(AnimeVarType::String))
    return;

  std::vector<u16> be(text.size() + 1, 0);
  for (size_t i = 0; i < text.size(); ++i)
    be[i] = __builtin_bswap16(static_cast<u16>(text[i]));

  u32 buf = rex::ppc::stack_push(be.data(), static_cast<u32>(be.size() * 2));
  WString_Assign(entry + offsetof(AnimeVar_t, value), buf,
                 static_cast<u32>(text.size()));
  Var_PropagateString(entry);
}

void VarBagSetText(u32 varBag, const char *varName, std::string_view utf8) {
  VarBagSetTextU16(varBag, varName, bd::Utf8ToU16(utf8));
}

void VarBagSetColor(u32 varBag, const char *varName, u32 rgba) {
  rex::ppc::stack_guard guard;
  u32 name = rex::ppc::stack_push_string(varName);
  VarBag_SetColor(varBag, name, rgba);
}

void VarBagSetFloat(u32 varBag, const char *varName, double value) {
  rex::ppc::stack_guard guard;
  u32 name = rex::ppc::stack_push_string(varName);
  VarBag_SetFloat(varBag, name, value);
}

bool VarBagGetFloat(u32 varBag, const char *varName, float *out) {
  rex::ppc::stack_guard guard;
  u32 name = rex::ppc::stack_push_string(varName);
  const auto *var = bd::mem::at<AnimeVar_t>(VarBag_FindVar(varBag, name));
  if (!var || !var->Is(AnimeVarType::Float))
    return false;
  *out = var->value;
  return true;
}

} // namespace bd::engine
