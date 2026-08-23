/**
 * @file    engine/stat_breakdown.cpp
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 * @license   BSD 3-Clause License
 */
#include "engine/stat_breakdown.h"

#include <algorithm>

#include "core/memory_helpers.h"
#include "engine/character.h"
#include "engine/game_tables.h"

namespace bd::engine {

namespace addr {
inline constexpr u32 kCharaClassTable = 0x82DC9B40; // g_pCharaClassTable
// g_bdConfig + 0x164, the word bdGetGlobalConfig()[89] reads. Selects both the
// alternate formula in Player_CalcBattleParams and the _new variants of every
// shipped data table. bdGetGlobalConfig returns the address of a static object
// rather than a stored pointer, so this is a direct read with no chase.
inline constexpr u32 kConfigNewTables = 0x82DEC3D4;
} // namespace addr

namespace {

// The five u16 slots of a CharaStatBlock_t, in the order the final assignments
// at the tail of Player_CalcBattleParams read them.
enum class BlockStat : u32 {
  kAttack = 0,
  kAgility = 1,
  kMagicAttack = 2,
  kMagicDefense = 3,
  kDefense = 4,
};

struct StatWiring {
  const char *name;
  BlockStat block;
  PermanentBonus permanent;
  size_t paramOffset;
};

constexpr StatWiring kWiring[] = {
    {"attack", BlockStat::kAttack, PermanentBonus::kAttack,
     offsetof(CharaBattleParams_t, attack)},
    {"defense", BlockStat::kDefense, PermanentBonus::kDefense,
     offsetof(CharaBattleParams_t, defense)},
    {"magicAttack", BlockStat::kMagicAttack, PermanentBonus::kMagicAttack,
     offsetof(CharaBattleParams_t, magicAttack)},
    {"magicDefense", BlockStat::kMagicDefense, PermanentBonus::kMagicDefense,
     offsetof(CharaBattleParams_t, magicDefense)},
    {"agility", BlockStat::kAgility, PermanentBonus::kAgility,
     offsetof(CharaBattleParams_t, agility)},
};

constexpr u32 CharaField(size_t off) {
  return kNodeChara + static_cast<u32>(off);
}

u32 BlockStatAt(u32 blockVA, BlockStat s) {
  return mem::try_load<u16>(blockVA + offsetof(CharaStatBlock_t, stats) +
                            static_cast<u32>(s) * sizeof(be_u16));
}

// Mirrors Chara_FindClassSkill.
u32 FindSkillDef(u32 classId, u32 skillId) {
  const u32 table = mem::try_load<u32>(addr::kCharaClassTable);
  if (!table)
    return 0;
  for (u32 i = 0; i < kCharaClassCount; ++i) {
    const u32 entry = table + i * sizeof(CharaClassTableEntry_t);
    const u32 id =
        mem::try_load<u32>(entry + offsetof(CharaClassTableEntry_t, classId));
    if (id != classId)
      continue;
    for (u32 j = 0; j < kClassSkillCount; ++j) {
      const u32 def = entry + offsetof(CharaClassTableEntry_t, skills) +
                      j * sizeof(CharaSkillDef_t);
      if (mem::try_load<u32>(def + offsetof(CharaSkillDef_t, skillId)) ==
          skillId)
        return def;
    }
    return 0;
  }
  return 0;
}

std::optional<CharaClass> ToClass(u32 raw) {
  if (raw >= kCharaClassCount)
    return std::nullopt;
  return static_cast<CharaClass>(raw);
}

bool NewTablesActive() {
  return mem::try_load<u32>(addr::kConfigNewTables) != 0;
}

} // namespace

StatBreakdown StatBreakdown::For(const PlayableCharacter &c) {
  StatBreakdown out;
  const u32 ea = c.Address();
  if (!ea)
    return out;

  const u32 baseBlock = ea + CharaField(offsetof(PlayerChara_t, _pad1AF0));
  const u32 classes = ea + CharaField(offsetof(PlayerChara_t, classes));
  const u32 bonuses = ea + CharaField(offsetof(PlayerChara_t, permanentBonus));
  const u32 unlocked = c.UnlockedClasses();
  const auto active = c.ActiveClass();

  for (const auto &w : kWiring) {
    StatContribution sc{};
    sc.name = w.name;
    sc.final = mem::try_load<u32>(
        ea + CharaField(offsetof(Chara_t, params) + w.paramOffset));
    sc.base = BlockStatAt(baseBlock, w.block);
    sc.permanent = mem::try_load<u32>(
        bonuses + static_cast<u32>(w.permanent) * sizeof(be_u32));
    if (active)
      sc.classActive = BlockStatAt(
          classes + static_cast<u32>(*active) * sizeof(CharaStatBlock_t),
          w.block);
    for (u32 i = 0; i < kCharaClassCount; ++i) {
      if (!(unlocked & (1u << i)))
        continue;
      const u32 v =
          BlockStatAt(classes + i * sizeof(CharaStatBlock_t), w.block);
      if (v > sc.classBest) {
        sc.classBest = v;
        sc.winner = static_cast<CharaClass>(i);
      }
    }
    out.stats_.push_back(sc);
  }

  if (active) {
    const u32 block =
        classes + static_cast<u32>(*active) * sizeof(CharaStatBlock_t);
    out.slotCount_ = std::min<u32>(
        mem::try_load<u32>(block + offsetof(CharaStatBlock_t, slotCount)),
        kCharaSkillSlotCount);
    for (u32 i = 0; i < out.slotCount_; ++i) {
      const u32 slot = block + offsetof(CharaStatBlock_t, slots) +
                       i * sizeof(CharaSkillSlot_t);
      EquippedSkill es{};
      es.slotIndex = i;
      const u32 rawClass =
          mem::try_load<u32>(slot + offsetof(CharaSkillSlot_t, classIndex));
      es.sourceClass = ToClass(rawClass);
      es.skillId =
          mem::try_load<u32>(slot + offsetof(CharaSkillSlot_t, skillId));
      if (!es.skillId)
        continue;
      const u32 def = FindSkillDef(rawClass, es.skillId);
      if (def) {
        es.effectType =
            mem::try_load<u32>(def + offsetof(CharaSkillDef_t, effectType));
        es.value = mem::try_load<u32>(def + offsetof(CharaSkillDef_t, value));
        es.name = GameTables::Name(
            mem::try_load<u32>(def + offsetof(CharaSkillDef_t, namePtr)));
      }
      out.skills_.push_back(es);
    }
  }

  const u32 worn = std::min<u32>(
      mem::try_load<u32>(baseBlock + offsetof(CharaStatBlock_t, equipCount)),
      kCharaEquipCount);
  for (u32 i = 0; i < worn; ++i) {
    EquippedItem it{};
    it.slotIndex = i;
    it.itemId = mem::try_load<u32>(
        baseBlock + offsetof(CharaStatBlock_t, equipment) + i * sizeof(be_u32));
    if (!it.itemId)
      continue;
    it.name = GameTables::Get().PhenomeName(it.itemId);
    out.items_.push_back(it);
  }

  out.classGate_ =
      mem::try_load<u32>(ea + CharaField(offsetof(PlayerChara_t, _pad1B3C))) !=
      0;
  out.newTables_ = NewTablesActive();
  out.valid_ = true;
  return out;
}

} // namespace bd::engine
