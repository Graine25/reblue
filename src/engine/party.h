/**
 * @file    engine/party.h
 * @brief   The marching party and the recruited roster, as walks over the
 *          FieldPlayerEntity lists.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 */
#pragma once

#include <rex/types.h>

#include "engine/character.h"

namespace bd::engine {

// Who is marching, in formation order. Index 0 is the leader.
class Party {
public:
  Party() = default;

  explicit operator bool() const;

  size_t Size() const;
  PlayableCharacter At(size_t i) const;
  PlayableCharacter Leader() const;

  // Members with HP above zero that are neither petrified nor knocked out.
  // This is the guest's own participation count, taken from the field party
  // walk at 0x823AECE0.
  size_t ActiveCount() const;
};

// Everyone recruited, reserves included.
class Roster {
public:
  Roster() = default;

  explicit operator bool() const;

  size_t Size() const;
  PlayableCharacter At(size_t i) const;

  // Whether a PlyTask_t EA is one of ours. This is the only positive test for
  // a party member: Chara_t::kind carries 0 for an NPC too.
  bool Contains(u32 nodeEA) const;

  u32 UnlockedSlots() const;              // GameTask_t::unlockedSlots
  bool IsSlotUnlocked(u32 slot_id) const; // slot ids step by kSlotIdStride
};

} // namespace bd::engine
