/**
 * @file    engine/menus/map_markers.h
 * @brief   The minimap floor database and the marker swatches drawn from it,
 *          shared by the area map screen and the field compass.
 *
 * @copyright   Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *              All rights reserved.
 * @license     BSD 3-Clause - see LICENSE
 */
#pragma once

#include <cstddef>

#include <rex/types.h>

#include "core/memory_helpers.h"
#include "engine/d2anime/anime_layout.h"
#include "engine/gimmicks.h"
#include "engine/guest_prim.h"
#include "engine/guest_texlist.h"

namespace bd::engine {

// One floor of the area minimap: the database\minimap\db_dgXX_YY.mmp record
// loaded beside a minimap\MM_dgXX_YY texture. Every field below is named by
// MiniMapDB_RegisterMindowsNodes, and MiniMapTask__DrawWidget reads them in
// this combination to place the compass crop.
struct MiniMapDB_t {
  /* 0x000 */ be_u32 kind;
  /* 0x004 */ be_f32 texW; // TexSize
  /* 0x008 */ be_f32 texH;
  /* 0x00C */ be_f32 scaleX; // MapScale, the world extent the texture covers
  /* 0x010 */ be_f32 scaleZ;
  /* 0x014 */ be_f32 dispW; // DispSize, the compass crop half-extent
  /* 0x018 */ be_f32 dispH;
  /* 0x01C */ be_f32 offsetX; // OffSet, the world origin's place on the texture
  /* 0x020 */ be_f32 offsetZ;
  /* 0x024 */ be_f32 offsetRot;
  /* 0x028 */ be_f32 plyRot;
  /* 0x02C */ u8 _pad02C[0x764 - 0x02C];
  /* 0x764 */ be_f32 texRot;
  /* 0x768 */ u8 _pad768[0x794 - 0x768];
  /* 0x794 */ be_u32 texEntries; // null until the .mmp and its texture resolve
};
static_assert(sizeof(MiniMapDB_t) == 0x798);
static_assert(offsetof(MiniMapDB_t, offsetRot) == 0x024);
static_assert(offsetof(MiniMapDB_t, texRot) == 0x764);
static_assert(offsetof(MiniMapDB_t, texEntries) == 0x794);

// Visual__SelectRenderTarget takes the holder eight bytes ahead of the entry
// table it reads.
constexpr u32 kFloorTexHolder = offsetof(MiniMapDB_t, texEntries) - 8;

// MiniMapTask [FieldSceneCtl_t::miniMapTask], which owns the compass and every
// floor of the area map.
struct MiniMapTask_t {
  /* 0x000 */ u8 _pad000[0x06C];
  /* 0x06C */ be_u32 chromeTex; // ring, player arrow, target marker
  /* 0x070 */ u8 _pad070[0x08C - 0x070];
  /* 0x08C */ MiniMapDB_t baseFloor;
  /* 0x824 */ u8 _pad824[0x83C - 0x824];
  /* 0x83C */ mem::GuestVec<u32> floors; // the MM_dgXX_YY_NN sub-floors
  /* 0x848 */ be_u32 floor; // null until an area map loads
};
static_assert(offsetof(MiniMapTask_t, chromeTex) == 0x06C);
static_assert(offsetof(MiniMapTask_t, baseFloor) == 0x08C);
static_assert(offsetof(MiniMapTask_t, floors) == 0x83C);
static_assert(offsetof(MiniMapTask_t, floor) == 0x848);

// Slots of the chrome texture holder as MiniMapTask__DrawWidget binds them: 1
// ring, 2 player arrow, 3 destination marker.
constexpr u32 kChromeArrow = 2;
constexpr u32 kChromeMarker = 3;

constexpr float kMarkerZ = 9.9f;
constexpr float kPlayerZ = 9.8f; // below the marker band, so it draws over it
constexpr float kMarkerSize = 20.0f; // as the compass draws the same arrow

constexpr u32 kTexturedQuad2D = 0x200001; // bdPrimDrawRect2D's own prim flags
// mm_entrance has a 7x7 block of opaque white here, with filtering margin, so
// a quad pinned to it takes its color entirely from the vertices.
constexpr float kSolidU = 0.609375f;
constexpr float kSolidV = 0.390625f;

// bdPrimEnd inserts into a z-sorted list by walking it, so a run of equal-z
// prims costs a pass over the run each. Stepping z per quad keeps that linear,
// within a band that stops short of the player.
constexpr float kMarkerZStep = 0.0001f;
constexpr float kMarkerZFloor = kPlayerZ + 0.01f;
constexpr float kSqrtHalf = 0.70710678f; // cos and sin of the turned quad

// D3DCOLOR is ARGB.
constexpr u32 kDotItem = 0xFFFFD24Au;
constexpr u32 kDotGold = 0xFFFFA02Au;
constexpr u32 kDotMedal = 0xFF6FE0FFu;
constexpr u32 kDotParam = 0xFF7CE07Cu;
constexpr u32 kDotHeal = 0xFFFF8CC0u;
constexpr u32 kDotGrass = 0xFF9CD86Cu;
constexpr u32 kDotPlain = 0xFFB0B0B0u;
constexpr u32 kChestLid = 0xFFD8B44Au;
constexpr u32 kDotBarrier = 0xFFC08CFFu;

constexpr u32 kOpaqueWhite = 0xFFFFFFFFu;

constexpr float kDegToRad = 0.017453292f;
constexpr float kHalfPi = 1.5707964f;

// The game's native world map icon sheets.
struct IconSheet {
  Texlist list;
  u32 texW;
  u32 texH;
};

extern IconSheet g_targetSheet;
extern IconSheet g_target03Sheet;
extern IconSheet *const kIconSheets[2];

// Matching the stock L_wrmap.csv marker dimensions.
constexpr float kMarkerHalf = 8.0f;        // 16x16 on the map
constexpr float kLegendMarkerHalf = 10.5f; // ~21x21 in the legend

u32 DotColor(GimmickKind kind);

// A run of quads at descending z, drawn through the guest's own 2D prim path.
// Caller must have bound the sheet with PrimSelectTexture.
struct QuadWriter {
  u32 texture = 0;
  float z = kMarkerZ;
  float zFloor = kMarkerZFloor;
  // Scales every color's own alpha, for the compass rim fade.
  u32 alpha = 255;

  // A solid quad, pinned to the sheet's opaque-white cell so its color comes
  // entirely from the vertices.
  void Draw(float cx, float cy, float halfW, float halfH, bool turned,
            u32 color);

  // One rect of an icon sheet, which brings a texture of its own rather than
  // the one the markers are pinned to, tinted by vertex color and alpha.
  void DrawIcon(u32 tex, float cx, float cy, float halfW, float halfH,
                const UVRect &uv, u32 color = kOpaqueWhite);
};

// A fast flash with a long rest: swell to 1.0 by 0.18, snap back by 0.28, then
// hold there so the base icon reads as steady.
float AsymmetricPulse(float phase);

// Where one marker stands in that flash this frame. Both maps draw the same
// markers, so both take their phase from here.
float MarkerPulse(const Marker &mk);

void DrawMarkerShape(QuadWriter &out, GimmickKind kind, float x, float y,
                     float pulse = 0.0f, float halfSize = kMarkerHalf);

// A marker is on the map only while it is still there to find.
bool MarkerVisible(const Marker &mk);

// Whether a floor's record and the texture it names have both resolved.
bool FloorReady(u32 db);

} // namespace bd::engine
