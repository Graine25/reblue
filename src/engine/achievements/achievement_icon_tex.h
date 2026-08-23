/**
 * @file    engine/achievements/achievement_icon_tex.h
 * @brief   Achievement icons as one texture the d2anime tex element can load.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include <rex/types.h>

namespace bd::engine {

// One atlas rather than a texture per achievement: a tex element's file column
// has to be a CSV literal. A path written into a string var at runtime is
// re-anchored to the directory of the CSV that declared the var and then
// prefixed again by the loader, which is not a path any mount can serve. UVs
// are expressions in the same columns the camp volume bars drive (L_cfg01.csv),
// so the row picks its cell without the file ever changing.
struct AchievementIconUv {
  float u0 = 0.0f, v0 = 0.0f, u1 = 0.0f, v1 = 0.0f;
};

// Every icon in catalog order, cells left transparent where an achievement has
// none. Ours come from the baked metadata assets, the title's from its XDBF
// image table. Emitted in the native form bdAllocRenderBuffer (0x82285740)
// reads for a file that does not start with 'DDS', a tiled k_8_8_8_8 payload
// behind a Xenos fetch constant, because the 'DDS' branch hands the buffer to
// the recompiled D3DXCreateTextureFromFileInMemoryEx instead. Built on first
// read and cached until the catalog outgrows it.
const std::vector<u8> *GetAchievementIconAtlas();

// Rebuilds the atlas once the catalog has grown past the one it was built
// from, and rewrites the loaded instances in place: the custom rows register
// on engine events, which can fire after the engine first loads the texture.
// Called from the viewers' refresh, every frame one is up.
void SyncAchievementIconAtlas();

// Cell holding one achievement's icon. Keyed by id rather than by list
// position because the viewer sorts its rows by unlock time, and the atlas is
// built once and cached. An achievement with no icon, or one past the last cell
// the atlas has room for, takes the reserved empty cell and draws nothing.
AchievementIconUv AchievementIconUVFor(u32 achievementId);

// What a tex element names to reach the atlas, relative to its own CSV and
// extension-implied, as every tex path is.
inline constexpr const char *kAchievementIconAtlasRef = "res\\ach_icons";

// Directories the atlas is served under: the camp Encyclopedia screen's CSVs
// and the config menu's.
extern const std::string_view kAchievementIconDirs[2];

std::string AchievementIconAtlasVFSKey(std::string_view csvDir);

} // namespace bd::engine
