/**
 * @file    engine/chara_types.h
 * @brief   The guest character object graph: the party/roster list node, the
 *          Chara base it embeds, and the Player and Enemy bodies that extend
 * it.
 *
 *          Thank you Mega for the offsets!
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#pragma once

#include <cstddef>

#include <rex/types.h>

#include "core/memory_helpers.h"

namespace bd::engine {

// ---- enumerations ----

// Bits of CharaBattleParams_t::statusFlags. Player_RecalcParams clears
// kKelolon once statusResist[kKelolon] reaches kResistImmune, and the field
// party walk clears kKnockedOut as it restores a downed member to 1 HP.
enum class CharaStatus : u32 {
  kSleep = 0x001,
  kDizzy = 0x002,
  kPanic = 0x004,
  kParalyze = 0x008,
  kStun = 0x010,
  kUnknown20 = 0x020,
  kPoison = 0x040,
  kKelolon = 0x080,
  kStink = 0x100,
  kPetrify = 0x200,
  kKnockedOut = 0x400,
};

// A resistance at or above this value makes the matching status unappliable.
inline constexpr u32 kResistImmune = 100;

inline constexpr u32 StatusBit(CharaStatus bit) {
  return static_cast<u32>(bit);
}

inline bool HasStatus(u32 status_flags, CharaStatus bit) {
  return (status_flags & StatusBit(bit)) != 0;
}

inline const char *ToString(CharaStatus bit) {
  switch (bit) {
  case CharaStatus::kSleep:
    return "sleep";
  case CharaStatus::kDizzy:
    return "dizzy";
  case CharaStatus::kPanic:
    return "panic";
  case CharaStatus::kParalyze:
    return "paralyze";
  case CharaStatus::kStun:
    return "stun";
  case CharaStatus::kUnknown20:
    return "unknown20";
  case CharaStatus::kPoison:
    return "poison";
  case CharaStatus::kKelolon:
    return "kelolon";
  case CharaStatus::kStink:
    return "stink";
  case CharaStatus::kPetrify:
    return "petrify";
  case CharaStatus::kKnockedOut:
    return "knocked-out";
  }
  return "?";
}

// Every CharaStatus, in bit order, so a reader can name a whole flag word.
inline constexpr CharaStatus kCharaStatusBits[] = {
    CharaStatus::kSleep,    CharaStatus::kDizzy,      CharaStatus::kPanic,
    CharaStatus::kParalyze, CharaStatus::kStun,       CharaStatus::kUnknown20,
    CharaStatus::kPoison,   CharaStatus::kKelolon,    CharaStatus::kStink,
    CharaStatus::kPetrify,  CharaStatus::kKnockedOut,
};

// Index into PlayerChara_t::classes and bit index in unlockedClasses.
// Player_RecalcParams walks 0..8 in this order.
enum class CharaClass : u32 {
  kWhiteMagic = 0,
  kBlackMagic = 1,
  kSupportMagic = 2,
  kBarrierMagic = 3,
  kSwordMaster = 4,
  kMonk = 5,
  kGuardian = 6,
  kAssassin = 7,
  kGeneralist = 8,
};

inline constexpr u32 kCharaClassCount = 9;

inline constexpr u32 ClassBit(CharaClass c) {
  return 1u << static_cast<u32>(c);
}

inline const char *ToString(CharaClass c) {
  switch (c) {
  case CharaClass::kWhiteMagic:
    return "white-magic";
  case CharaClass::kBlackMagic:
    return "black-magic";
  case CharaClass::kSupportMagic:
    return "support-magic";
  case CharaClass::kBarrierMagic:
    return "barrier-magic";
  case CharaClass::kSwordMaster:
    return "sword-master";
  case CharaClass::kMonk:
    return "monk";
  case CharaClass::kGuardian:
    return "guardian";
  case CharaClass::kAssassin:
    return "assassin";
  case CharaClass::kGeneralist:
    return "generalist";
  }
  return "?";
}

// Index into CharaBattleParams_t::statusResist. The guest orders these by
// itself, and it is not the CharaStatus bit order.
enum class CharaResist : u32 {
  kSleep = 0,
  kKelolon = 1,
  kDizzy = 2,
  kPoison = 3,
  kParalyze = 4,
  kPanic = 5,
  kStink = 6,
  kPetrify = 7,
  kStun = 8,
  kInstantDeath = 9,
  kWeaken = 10,
};

inline constexpr u32 kCharaResistCount = 11;

// Index into CharaBattleParams_t::elementDefense. Light and dark sit apart
// from this run, in their own fields.
enum class CharaElement : u32 {
  kGeneral = 0,
  kFire = 1,
  kWater = 2,
  kEarth = 3,
  kWind = 4,
};

inline constexpr u32 kCharaElementCount = 5;

// ---- guest structs ----

// Effect types are carried as raw numbers rather than an enum: the set comes
// from the dev comments in the shipped data tables rather than from engine
// code, so it is advisory and open-ended. effect_names.h labels them.
//
// Type 11 is the one known contradiction. Player_ApplyPassiveEffect handles it
// as a structural type the recompute consumes, Player_RecalcParams adds its
// value to a non-active class's skill slot count, and the SkEf comment names a
// paralysis barrier. A barrier that widens the slot count fits all three, but
// that is unconfirmed in-engine.
//
// An element of CharaBattleParams_t::passiveEffects, rebuilt from the equipped
// skills on every Player_RecalcParams.
struct CharaPassiveEffect_t {
  /* 0x00 */ be_u32 type;
  /* 0x04 */ be_u32 value;
};
static_assert(sizeof(CharaPassiveEffect_t) == 0x08);

// An element of CharaBattleParams_t::timedEffects. Chara_SetTimedEffect erases
// any entry of the same type before pushing a replacement, so type is a key.
// The third word is written from that function's third argument and its
// meaning is not yet confirmed, so it is not named a turn count.
struct CharaTimedEffect_t {
  /* 0x00 */ be_u32 type;
  /* 0x04 */ be_u32 value;
  /* 0x08 */ be_u32 extra;
};
static_assert(sizeof(CharaTimedEffect_t) == 0x0C);

// Chara + 0x16FC. What battle math reads and equipment feeds: the resistances,
// the derived stats, live HP/MP and the status word. Player_RecalcParams
// (0x823B2270) takes its address as its second argument, and bdEventFindEntry
// searches the effect vector at its tail.
struct CharaBattleParams_t {
  /* 0x00 */ u8 _pad00[0x20];
  /* 0x20 */ u8 statusResist[kCharaResistCount]; // by CharaResist
  /* 0x2B */ u8 _pad2B[0x01];
  /* 0x2C */ be_u16 elementDefense[kCharaElementCount]; // by CharaElement
  /* 0x36 */ u8 _pad36[0x02];
  /* 0x38 */ be_u32 attack;
  /* 0x3C */ be_u32 defense;
  /* 0x40 */ be_u32 hitRate;
  /* 0x44 */ u8 _pad44[0x04];
  /* 0x48 */ be_u32 magicAttack;
  /* 0x4C */ be_u32 magicDefense;
  /* 0x50 */ u8 _pad50[0x04];
  /* 0x54 */ be_u32 agility;
  /* 0x58 */ u8 _pad58[0x07];
  /* 0x5F */ u8 slowResist;
  /* 0x60 */ u8 stopResist;
  /* 0x61 */ u8 _pad61[0x01];
  /* 0x62 */ be_u16 lightDefense;
  /* 0x64 */ be_u16 darkDefense;
  /* 0x66 */ u8 _pad66[0x06];
  /* 0x6C */ be_u32 curHP;
  /* 0x70 */ be_u32 curMP;
  /* 0x74 */ u8 _pad74[0x10];
  /* 0x84 */ be_u32 paralyzeTurns;
  /* 0x88 */ be_u32 stunTurns;
  /* 0x8C */ u8 _pad8C[0x0C];
  /* 0x98 */ be_u32 statusFlags; // CharaStatus bits
  /* 0x9C */ u8 _pad9C[0x14];
  /* 0xB0 */ be_u32 maxHP;
  /* 0xB4 */ be_u32 maxMP;
  /* 0xB8 */ u8 _padB8[0x18];
  // Both containers carry a leading word before the pointer triple, so the
  // vectors sit past the base addresses the engine passes around. Engine code
  // hands a container helper params+0xD0 or params+0xE0 and the helper reads
  // begin at +4.
  /* 0xD0 */ be_u32 _timedHeader;
  /* 0xD4 */ mem::GuestVec<CharaTimedEffect_t> timedEffects;
  /* 0xE0 */ be_u32 _passiveHeader;
  /* 0xE4 */ mem::GuestVec<CharaPassiveEffect_t> passiveEffects;
};
static_assert(sizeof(CharaBattleParams_t) == 0xF0);
static_assert(offsetof(CharaBattleParams_t, statusResist) == 0x20);
static_assert(offsetof(CharaBattleParams_t, elementDefense) == 0x2C);
static_assert(offsetof(CharaBattleParams_t, attack) == 0x38);
static_assert(offsetof(CharaBattleParams_t, agility) == 0x54);
static_assert(offsetof(CharaBattleParams_t, curHP) == 0x6C);
static_assert(offsetof(CharaBattleParams_t, curMP) == 0x70);
static_assert(offsetof(CharaBattleParams_t, statusFlags) == 0x98);
static_assert(offsetof(CharaBattleParams_t, maxHP) == 0xB0);
static_assert(offsetof(CharaBattleParams_t, maxMP) == 0xB4);
static_assert(offsetof(CharaBattleParams_t, paralyzeTurns) == 0x84);
static_assert(offsetof(CharaBattleParams_t, stunTurns) == 0x88);
static_assert(offsetof(CharaBattleParams_t, timedEffects) == 0xD4);
static_assert(offsetof(CharaBattleParams_t, passiveEffects) == 0xE4);

// One equipped skill slot: which class supplied the skill, and its id within
// that class.
struct CharaSkillSlot_t {
  /* 0x00 */ be_u32 classIndex;
  /* 0x04 */ be_u32 skillId;
};
static_assert(sizeof(CharaSkillSlot_t) == 0x08);

inline constexpr u32 kCharaSkillSlotCount = 30;

// Accessory slots. Player_RecalcParams makes four or five live, and a load
// clamps to the full seven.
inline constexpr u32 kCharaEquipCount = 7;

// The block at Chara+0x1AF0 and each of the nine records in
// PlayerChara_t::classes are the same type: Chara_CopyStatBlock copies one
// onto the other field for field. The player body spends on level and exp the
// two words a class record spends on rank and sp.
struct CharaStatBlock_t {
  /* 0x000 */ be_u16 stats[5];
  /* 0x00A */ u8 _pad00A[0x06];
  /* 0x010 */ be_u32 rank; // level, on the player body
  /* 0x014 */ be_u32 sp;   // exp, on the player body
  /* 0x018 */ be_u32 extraBonus;
  /* 0x01C */ be_u16 stats2[5];
  /* 0x026 */ u8 _pad026[0x02];
  /* 0x028 */ be_u32 equipment[kCharaEquipCount];
  /* 0x044 */ be_u32 equipCount;
  /* 0x048 */ u8 _pad048[0x1C];
  /* 0x064 */ CharaSkillSlot_t slots[kCharaSkillSlotCount];
  /* 0x154 */ be_u32 slotCount;
  /* 0x158 */ u8 resistBonus[kCharaResistCount];
  /* 0x163 */ u8 _pad163[0x05];
};
static_assert(sizeof(CharaStatBlock_t) == 0x168);
static_assert(offsetof(CharaStatBlock_t, rank) == 0x010);
static_assert(offsetof(CharaStatBlock_t, sp) == 0x014);
static_assert(offsetof(CharaStatBlock_t, equipment) == 0x028);
static_assert(offsetof(CharaStatBlock_t, equipCount) == 0x044);
static_assert(offsetof(CharaStatBlock_t, slots) == 0x064);
static_assert(offsetof(CharaStatBlock_t, slotCount) == 0x154);
static_assert(offsetof(CharaStatBlock_t, resistBonus) == 0x158);

// One of the nine job records in PlayerChara_t::classes.
using CharaClassRecord_t = CharaStatBlock_t;
static_assert(sizeof(CharaClassRecord_t) == 0x168);
static_assert(offsetof(CharaClassRecord_t, rank) == 0x10);
static_assert(offsetof(CharaClassRecord_t, sp) == 0x14);

// Task + 0x70. The vtable'd character object Chara__ctor (0x822B7BD0) builds,
// shared by party members and enemies: CharaVO carries the model and world
// transform, params the combat state. Everything past it belongs to whichever
// body extends this base.
struct Chara_t {
  /* 0x0000 */ be_u32 vtable;
  /* 0x0004 */ u8 _pad0004[0x16B8]; // CharaVO +0x08, ChrPresetAnim +0x15A4
  // Position in the marching order, and the camp formation grid's only storage:
  // the Rank screen derives row from partyOrder / activeCount and column from
  // 4 - partyOrder % activeCount. Never renumber it flat.
  /* 0x16BC */ be_u32 partyOrder;
  /* 0x16C0 */ u8 _pad16C0[0x3C];
  /* 0x16FC */ CharaBattleParams_t params;
  /* 0x17EC */ u8 _pad17EC[0x34];
  /* 0x1820 */ be_u32 kind; // Chara__ctor's first argument
  /* 0x1824 */ be_u32
      slotId; // 10/20/30/40/50, the engine spells pc%02d from slotId/10
  /* 0x1828 */ u8 _pad1828[0x2C8];
};
static_assert(sizeof(Chara_t) == 0x1AF0);
static_assert(offsetof(Chara_t, partyOrder) == 0x16BC);
static_assert(offsetof(Chara_t, params) == 0x16FC);
static_assert(offsetof(Chara_t, kind) == 0x1820);
static_assert(offsetof(Chara_t, slotId) == 0x1824);

inline constexpr u32 kCharaKindEnemy = 2;

// Slot ids run in steps of ten, so the engine's own pc number is slotId/10 and
// its zero-based table index (slotId/10)-1.
inline constexpr u32 kSlotIdStride = 10;

// Player_CalcBattleParams adds one of these to each derived stat. Order is
// fixed by the loads at Chara+0x2800 upward.
enum class PermanentBonus : u32 {
  kMaxHP = 0,
  kMaxMP = 1,
  kAttack = 2,
  kMagicAttack = 3,
  kDefense = 4,
  kMagicDefense = 5,
  kAgility = 6,
};

inline constexpr u32 kPermanentBonusCount = 7;

// Chara extended for a party member.
struct PlayerChara_t {
  /* 0x0000 */ Chara_t base;
  /* 0x1AF0 */ u8 _pad1AF0[0x10];
  /* 0x1B00 */ be_u32 level;
  /* 0x1B04 */ be_u32 exp;
  /* 0x1B08 */ u8 _pad1B08[0x10];
  /* 0x1B18 */ be_u32
      equipment[kCharaEquipCount]; // item ids, equipCount of them live
  /* 0x1B34 */ be_u32
      equipCount; // Player_RecalcParams sets 4 or 5, load clamps to 7
  /* 0x1B38 */ be_u32 unlockedClasses;    // ClassBit(CharaClass)
  /* 0x1B3C */ u8 _pad1B3C[0x04];         // gates the class recompute
  /* 0x1B40 */ be_u32 activeClass;        // CharaClass
  /* 0x1B44 */ be_u32 activeClassLatched; // copy Player_RecalcParams stamps
  /* 0x1B48 */ u8 _pad1B48[0x10];
  /* 0x1B58 */ CharaClassRecord_t classes[kCharaClassCount];
  /* 0x2800 */ be_u32 permanentBonus[kPermanentBonusCount];
  /* 0x281C */ u8 _pad281C[0x0C];
};
static_assert(sizeof(PlayerChara_t) == 0x2828);
static_assert(offsetof(PlayerChara_t, level) == 0x1B00);
static_assert(offsetof(PlayerChara_t, exp) == 0x1B04);
static_assert(offsetof(PlayerChara_t, equipment) == 0x1B18);
static_assert(offsetof(PlayerChara_t, equipCount) == 0x1B34);
static_assert(offsetof(PlayerChara_t, unlockedClasses) == 0x1B38);
static_assert(offsetof(PlayerChara_t, activeClass) == 0x1B40);
static_assert(offsetof(PlayerChara_t, classes) == 0x1B58);
static_assert(offsetof(PlayerChara_t, permanentBonus) == 0x2800);

// Chara extended for a battle enemy. typeId takes the word a party member
// spends on equipCount and nextInGroup on the one that gates its class
// recompute, making these two bodies alternatives over the same bytes rather
// than one struct.
//
// An enemy carries two chain links, and they thread two different lists off the
// same EnePtyTask. The one in _pad1B38 belongs to the spawn list at group+0xF8,
// which the field build and teardown own. nextInGroup belongs to the list at
// group+0xFC, which is the one the guest's own battle walks read, so anything
// reachable from the battle manager takes nextInGroup.
struct EnemyChara_t {
  /* 0x0000 */ Chara_t base;
  /* 0x1AF0 */ u8 _pad1AF0[0x44];
  /* 0x1B34 */ be_u32 typeId; // em number, or 10000 + bs number for a boss
  /* 0x1B38 */ u8 _pad1B38[0x04];
  /* 0x1B3C */ be_u32 nextInGroup;
};
static_assert(offsetof(EnemyChara_t, typeId) == 0x1B34);
static_assert(offsetof(EnemyChara_t, nextInGroup) == 0x1B3C);
static_assert(offsetof(EnemyChara_t, typeId) ==
              offsetof(PlayerChara_t, equipCount));

// The party/roster list node. Both heads on FieldPlayerEntity point at one of
// these, and both chains run through it.
struct PlyTask_t {
  /* 0x0000 */ be_u32 vtable;
  /* 0x0004 */ u8 _pad0004[0x54];
  /* 0x0058 */ be_u32 flags; // bit0 visible, bit2 hidden
  /* 0x005C */ u8 _pad005C[0x14];
  /* 0x0070 */ PlayerChara_t chara;
  /* 0x2898 */ be_u32 nextRoster;
  /* 0x289C */ be_u32 nextParty;
  /* 0x28A0 */ u8 _pad28A0[0x04];
  /* 0x28A4 */ be_u32 uniqueId;
};
static_assert(offsetof(PlyTask_t, flags) == 0x0058);
static_assert(offsetof(PlyTask_t, chara) == 0x0070);
static_assert(offsetof(PlyTask_t, nextRoster) == 0x2898);
static_assert(offsetof(PlyTask_t, nextParty) == 0x289C);
static_assert(offsetof(PlyTask_t, uniqueId) == 0x28A4);

// The battle actor node, chained off an enemy group.
struct EneTask_t {
  /* 0x0000 */ be_u32 vtable;
  /* 0x0004 */ u8 _pad0004[0x6C];
  /* 0x0070 */ EnemyChara_t chara;
};
static_assert(offsetof(EneTask_t, chara) == offsetof(PlyTask_t, chara));

// Both node kinds put Chara at the same offset, so a reader that only touches
// the base can hold either without knowing which it has.
inline constexpr u32 kNodeChara = offsetof(PlyTask_t, chara);

// FieldPlayerEntity [addr::kFieldPlayerEntity].
struct FieldPlayerEntity_t {
  /* 0x00 */ u8 _pad00[0x78];
  /* 0x78 */ be_u32 rosterHead; // chain PlyTask_t::nextRoster
  /* 0x7C */ be_u32 activeHead; // chain PlyTask_t::nextParty, index 0 leads
};
static_assert(offsetof(FieldPlayerEntity_t, rosterHead) == 0x78);
static_assert(offsetof(FieldPlayerEntity_t, activeHead) == 0x7C);

// GameTask [addr::kGameTask].
struct GameTask_t {
  /* 0x00 */ u8 _pad00[0x6C];
  /* 0x6C */ be_u32 unlockedSlots; // bit i unlocks slot (i + 1) * kSlotIdStride
};
static_assert(offsetof(GameTask_t, unlockedSlots) == 0x6C);

// One skill a class grants. Chara_FindClassSkill finds a class entry by
// classId then scans its skills for a skillId.
struct CharaSkillDef_t {
  /* 0x00 */ be_u32 skillId;
  /* 0x04 */ be_u32 _unk04;
  /* 0x08 */ be_u32 effectType;
  /* 0x0C */ be_u32 value;
  // Camp_Shadow_BindSkillClassWindow feeds this to the d2anime wide-string
  // setter, so a skill names itself rather than being looked up in a table.
  /* 0x10 */ be_u32 namePtr;
  /* 0x14 */ u8 _pad14[0x04];
};
static_assert(sizeof(CharaSkillDef_t) == 0x18);
static_assert(offsetof(CharaSkillDef_t, namePtr) == 0x10);

inline constexpr u32 kClassSkillCount = 15;
inline constexpr u32 kClassInnateCount = 3;

// One entry of the table g_pCharaClassTable points at.
struct CharaClassTableEntry_t {
  /* 0x000 */ u8 _pad000[0x10];
  /* 0x010 */ be_u32 classId;
  /* 0x014 */ u8 _pad014[0x3C];
  /* 0x050 */ CharaSkillDef_t skills[kClassSkillCount];
  /* 0x1B8 */ CharaSkillDef_t innate[kClassInnateCount];
  /* 0x200 */ u8 _pad200[0x30];
};
static_assert(sizeof(CharaClassTableEntry_t) == 0x230);
static_assert(offsetof(CharaClassTableEntry_t, classId) == 0x010);
static_assert(offsetof(CharaClassTableEntry_t, skills) == 0x050);
static_assert(offsetof(CharaClassTableEntry_t, innate) == 0x1B8);

} // namespace bd::engine
