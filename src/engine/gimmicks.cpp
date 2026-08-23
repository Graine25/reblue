/**
 * @file    engine/gimmicks.cpp
 * @brief   The baked table comes from tools/gen_gimmicks_table.py, the live
 *          state from the field's script variable block.
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#include "engine/gimmicks.h"

#include <algorithm>
#include <cstring>
#include <type_traits>

#include "core/logging.h"
#include "engine/field.h"
#include "embedded.h"

namespace bd::engine {

namespace {

constexpr u32 kTableMagic = 0x31474442; // 'BDG1'
constexpr u32 kTableVersion = 1;

// A search point with this flag carries none, and respawns every map load.
constexpr u16 kNoFlag = 0xFFFF;

// Little-endian on purpose: the generator writes host order, not guest order.
struct MapRec {
  u32 nameOff;
  u32 pointFirst, pointCount;
  u32 chestFirst, chestCount;
  u32 barrierFirst, barrierCount;
};
static_assert(sizeof(MapRec) == 28, "MapRec matches the generator");

struct PointRec {
  u16 flag;
  u8 kind;
  u8 pad;
  f32 x, y, z;
};
static_assert(sizeof(PointRec) == 16, "PointRec matches the generator");

// A chest or barrier on one map: which entry of the distinct flag list it is,
// and where the script that sets that flag stands.
struct PlacementRec {
  u16 slot;
  u16 pad;
  f32 x, y, z;
};
static_assert(sizeof(PlacementRec) == 16, "PlacementRec matches the generator");

struct BarrierRec {
  u16 flag;
  u8 color;
};

} // namespace

// Copied out of the embedded blob rather than pointed into it: bin2c emits a
// plain byte array, whose alignment is not enough to read records through.
// Maps are sorted by name, so a stem lookup is a binary search.
struct Gimmicks::Table {
  std::vector<MapRec> maps;
  std::vector<PointRec> points;
  std::vector<u16> chestFlags;
  std::vector<BarrierRec> barriers;
  std::vector<PlacementRec> chestsPlaced;
  std::vector<PlacementRec> barriersPlaced;
  std::vector<char> strings;
  bool ok = false;

  std::string_view Name(const MapRec &m) const {
    return strings.data() + m.nameOff;
  }

  const MapRec *Find(std::string_view stem) const {
    const auto lo = std::lower_bound(
        maps.begin(), maps.end(), stem,
        [this](const MapRec &m, std::string_view s) { return Name(m) < s; });
    return (lo != maps.end() && Name(*lo) == stem) ? &*lo : nullptr;
  }
};

std::unique_ptr<Gimmicks::Table> Gimmicks::ParseTable() {
  auto t = std::make_unique<Table>();
  constexpr auto kBlob = bd::Embedded("gimmicks_table.bin");
  constexpr u32 kHeaderWords = 9;
  constexpr u32 kHeaderBytes = kHeaderWords * sizeof(u32);
  if (kBlob.size < kHeaderBytes)
    return t;

  u32 head[kHeaderWords];
  std::memcpy(head, kBlob.data, kHeaderBytes);
  if (head[0] != kTableMagic || head[1] != kTableVersion) {
    BD_ERROR("[gimmicks] embedded table is magic {:#x} version {}, expected "
             "{:#x} version {}",
             head[0], head[1], kTableMagic, kTableVersion);
    return t;
  }

  size_t off = kHeaderBytes;
  bool truncated = false;
  const auto take = [&](auto &dst, size_t count) {
    using Elem = typename std::decay_t<decltype(dst)>::value_type;
    const size_t bytes = count * sizeof(Elem);
    if (truncated || off + bytes > kBlob.size) {
      truncated = true;
      return;
    }
    dst.resize(count);
    if (count)
      std::memcpy(dst.data(), kBlob.data + off, bytes);
    off = (off + bytes + 3u) & ~size_t{3u};
  };

  std::vector<u16> barrierFlags;
  std::vector<u8> barrierColors;
  take(t->maps, head[2]);
  take(t->points, head[3]);
  take(t->chestFlags, head[4]);
  take(barrierFlags, head[5]);
  take(barrierColors, head[5]);
  take(t->chestsPlaced, head[6]);
  take(t->barriersPlaced, head[7]);
  take(t->strings, head[8]);

  if (truncated) {
    BD_ERROR("[gimmicks] embedded table is truncated at {} of {} bytes", off,
             kBlob.size);
    return t;
  }

  t->barriers.reserve(barrierFlags.size());
  for (size_t i = 0; i < barrierFlags.size(); ++i)
    t->barriers.push_back({barrierFlags[i], barrierColors[i]});

  // Queries index blind, so a bad table is no table.
  for (const MapRec &m : t->maps) {
    if (m.nameOff >= t->strings.size() ||
        m.pointFirst + m.pointCount > t->points.size() ||
        m.chestFirst + m.chestCount > t->chestsPlaced.size() ||
        m.barrierFirst + m.barrierCount > t->barriersPlaced.size()) {
      BD_ERROR("[gimmicks] embedded table has an out-of-range map record");
      return t;
    }
  }
  for (const PlacementRec &p : t->chestsPlaced) {
    if (p.slot >= t->chestFlags.size()) {
      BD_ERROR("[gimmicks] embedded table has a bad chest index");
      return t;
    }
  }
  for (const PlacementRec &p : t->barriersPlaced) {
    if (p.slot >= t->barriers.size()) {
      BD_ERROR("[gimmicks] embedded table has a bad barrier index");
      return t;
    }
  }
  if (!t->strings.empty() && t->strings.back() != '\0') {
    BD_ERROR("[gimmicks] embedded table's string block is unterminated");
    return t;
  }

  t->ok = true;
  return t;
}

const char *ToString(GimmickKind kind) {
  switch (kind) {
  case GimmickKind::Nothing: return "nothing";
  case GimmickKind::Message: return "message";
  case GimmickKind::Item: return "item";
  case GimmickKind::Gold: return "gold";
  case GimmickKind::Heal: return "heal";
  case GimmickKind::Damage: return "damage";
  case GimmickKind::Status: return "status";
  case GimmickKind::Medal: return "medal";
  case GimmickKind::Lock: return "lock";
  case GimmickKind::Grass: return "grass";
  case GimmickKind::Param: return "param";
  case GimmickKind::Chest: return "chest";
  case GimmickKind::Barrier: return "barrier";
  }
  return "?";
}

const char *ToString(BarrierColor color) {
  switch (color) {
  case BarrierColor::Blue: return "blue";
  case BarrierColor::Red: return "red";
  case BarrierColor::Green: return "green";
  case BarrierColor::White: return "white";
  case BarrierColor::Black: return "black";
  }
  return "?";
}

Gimmicks::Gimmicks() : table_(ParseTable()) {}
Gimmicks::~Gimmicks() = default;

Gimmicks &Gimmicks::Get() {
  static Gimmicks g;
  return g;
}

bool Gimmicks::IsReady() const {
  return table_->ok && static_cast<bool>(Field().Vars());
}

bool Gimmicks::Has(std::string_view stem) const {
  return table_->ok && table_->Find(stem) != nullptr;
}

Tally Gimmicks::Points(std::string_view stem,
                       std::optional<GimmickKind> kind) const {
  Tally out;
  const ScriptVars vars = Field().Vars();
  if (!table_->ok || !vars)
    return out;

  const auto count = [&](const MapRec &m) {
    for (u32 i = 0; i < m.pointCount; ++i) {
      const PointRec &p = table_->points[m.pointFirst + i];
      if (p.flag == kNoFlag)
        continue; // respawns, so it belongs to neither side of the tally
      if (kind && p.kind != static_cast<u8>(*kind))
        continue;
      ++out.total;
      if (!vars.Flag(p.flag))
        ++out.remaining;
    }
  };
  if (stem == kEverywhere) {
    for (const MapRec &m : table_->maps)
      count(m);
  } else if (const MapRec *m = table_->Find(stem)) {
    count(*m);
  }
  return out;
}

Tally Gimmicks::Chests(std::string_view stem) const {
  Tally out;
  const ScriptVars vars = Field().Vars();
  if (!table_->ok || !vars)
    return out;

  const auto count = [&](u16 flag) {
    ++out.total;
    if (!vars.Global(flag))
      ++out.remaining;
  };
  // A chest reachable from several story state variants of one map is listed
  // under each, so the whole-game tally walks the distinct flags instead.
  if (stem == kEverywhere) {
    for (const u16 flag : table_->chestFlags)
      count(flag);
  } else if (const MapRec *m = table_->Find(stem)) {
    for (u32 i = 0; i < m->chestCount; ++i)
      count(table_->chestFlags[table_->chestsPlaced[m->chestFirst + i].slot]);
  }
  return out;
}

Tally Gimmicks::Barriers(std::string_view stem,
                         std::optional<BarrierColor> color) const {
  Tally out;
  const ScriptVars vars = Field().Vars();
  if (!table_->ok || !vars)
    return out;

  const auto count = [&](const BarrierRec &b) {
    if (color && b.color != static_cast<u8>(*color))
      return;
    ++out.total;
    if (!vars.Global(b.flag))
      ++out.remaining;
  };
  if (stem == kEverywhere) {
    for (const BarrierRec &b : table_->barriers)
      count(b);
  } else if (const MapRec *m = table_->Find(stem)) {
    for (u32 i = 0; i < m->barrierCount; ++i)
      count(table_->barriers[table_->barriersPlaced[m->barrierFirst + i].slot]);
  }
  return out;
}

std::vector<Marker> Gimmicks::Markers(std::string_view stem) const {
  std::vector<Marker> out;
  const MapRec *m = table_->ok ? table_->Find(stem) : nullptr;
  if (!m)
    return out;

  const ScriptVars vars = Field().Vars();
  out.reserve(m->pointCount + m->chestCount + m->barrierCount);
  for (u32 i = 0; i < m->pointCount; ++i) {
    const PointRec &p = table_->points[m->pointFirst + i];
    Marker mk;
    mk.kind = static_cast<GimmickKind>(p.kind);
    mk.trackable = p.flag != kNoFlag;
    mk.collected = mk.trackable && vars.Flag(p.flag);
    mk.x = p.x;
    mk.y = p.y;
    mk.z = p.z;
    out.push_back(mk);
  }

  const auto placed = [&](GimmickKind kind, const PlacementRec &p, u16 flag) {
    Marker mk;
    mk.kind = kind;
    mk.trackable = true;
    mk.collected = vars.Global(flag) != 0;
    mk.x = p.x;
    mk.y = p.y;
    mk.z = p.z;
    out.push_back(mk);
  };
  for (u32 i = 0; i < m->chestCount; ++i) {
    const PlacementRec &p = table_->chestsPlaced[m->chestFirst + i];
    placed(GimmickKind::Chest, p, table_->chestFlags[p.slot]);
  }
  for (u32 i = 0; i < m->barrierCount; ++i) {
    const PlacementRec &p = table_->barriersPlaced[m->barrierFirst + i];
    placed(GimmickKind::Barrier, p, table_->barriers[p.slot].flag);
  }
  return out;
}

} // namespace bd::engine
