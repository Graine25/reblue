/**
 * @file    engine/party.cpp
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 * @license   BSD 3-Clause License
 */
#include "engine/party.h"

#include <algorithm>
#include <vector>

#include <rex/hook.h>

#include "core/memory_helpers.h"
#include "engine/events.h"
#include "engine/state_layout.h"

namespace bd::engine {

namespace {

u32 ListHead(u32 head_offset) {
  const u32 fpe = bd::mem::try_load<u32>(addr::kFieldPlayerEntity);
  return fpe ? mem::try_field<u32>(fpe, head_offset) : 0;
}

// Hands each node to 'fn' until it returns false or the chain ends.
//
// Two cursors, one stepping twice for every step of the other: they can only
// meet inside a cycle, so a corrupt or self-referential list ends the walk
// instead of running forever. That is what a fixed hop cap used to buy, except
// the cap also truncated any list that legitimately grew past it.
template <typename Fn> void Walk(u32 head, u32 next_offset, Fn &&fn) {
  u32 slow = head;
  u32 fast = head;
  for (size_t i = 0; slow; ++i) {
    if (!fn(slow, i))
      return;
    slow = mem::try_field<u32>(slow, next_offset);
    fast = mem::try_field<u32>(fast, next_offset);
    if (fast)
      fast = mem::try_field<u32>(fast, next_offset);
    if (fast && fast == slow)
      return;
  }
}

size_t ListLength(u32 head, u32 next_offset) {
  size_t count = 0;
  Walk(head, next_offset, [&](u32, size_t) {
    ++count;
    return true;
  });
  return count;
}

u32 NodeAt(u32 head, u32 next_offset, size_t i) {
  u32 found = 0;
  Walk(head, next_offset, [&](u32 node, size_t index) {
    if (index != i)
      return true;
    found = node;
    return false;
  });
  return found;
}

u32 ActiveHead() { return ListHead(offsetof(FieldPlayerEntity_t, activeHead)); }

u32 RosterHead() { return ListHead(offsetof(FieldPlayerEntity_t, rosterHead)); }

constexpr u32 kNextParty = offsetof(PlyTask_t, nextParty);
constexpr u32 kNextRoster = offsetof(PlyTask_t, nextRoster);

// The publishers below take the entity from the guest rather than from
// addr::kFieldPlayerEntity, so a call made on some other entity reports that
// one or nothing at all.
u32 LeaderNode(u32 entity) {
  return mem::try_field<u32>(entity, offsetof(FieldPlayerEntity_t, activeHead));
}

std::vector<u32> ActiveNodes(u32 entity) {
  std::vector<u32> nodes;
  Walk(LeaderNode(entity), kNextParty, [&](u32 node, size_t) {
    nodes.push_back(node);
    return true;
  });
  return nodes;
}

// The node the marching party gained, or 0. Every publishing site adds at
// most one.
u32 JoinedSince(u32 entity, const std::vector<u32> &before) {
  for (const u32 node : ActiveNodes(entity))
    if (std::find(before.begin(), before.end(), node) == before.end())
      return node;
  return 0;
}

} // namespace

Party::operator bool() const {
  return bd::mem::try_load<u32>(addr::kFieldPlayerEntity) != 0;
}

size_t Party::Size() const { return ListLength(ActiveHead(), kNextParty); }

PlayableCharacter Party::At(size_t i) const {
  return PlayableCharacter(NodeAt(ActiveHead(), kNextParty, i));
}

PlayableCharacter Party::Leader() const { return At(0); }

size_t Party::ActiveCount() const {
  size_t count = 0;
  Walk(ActiveHead(), kNextParty, [&](u32 node, size_t) {
    const PlayableCharacter c{node};
    if (c.IsAlive() && !c.HasStatus(CharaStatus::kPetrify) &&
        !c.HasStatus(CharaStatus::kKnockedOut))
      ++count;
    return true;
  });
  return count;
}

Roster::operator bool() const {
  return bd::mem::try_load<u32>(addr::kFieldPlayerEntity) != 0;
}

size_t Roster::Size() const { return ListLength(RosterHead(), kNextRoster); }

PlayableCharacter Roster::At(size_t i) const {
  return PlayableCharacter(NodeAt(RosterHead(), kNextRoster, i));
}

bool Roster::Contains(u32 nodeEA) const {
  if (!nodeEA)
    return false;
  bool found = false;
  Walk(RosterHead(), kNextRoster, [&](u32 node, size_t) {
    found = node == nodeEA;
    return !found;
  });
  return found;
}

u32 Roster::UnlockedSlots() const {
  const u32 gt = bd::mem::try_load<u32>(addr::kGameTask);
  return gt ? mem::try_field<u32>(gt, offsetof(GameTask_t, unlockedSlots)) : 0;
}

bool Roster::IsSlotUnlocked(u32 slot_id) const {
  if (slot_id < kSlotIdStride)
    return false;
  const u32 bit = slot_id / kSlotIdStride - 1;
  return bit < 32 && (UnlockedSlots() & (1u << bit)) != 0;
}

} // namespace bd::engine

// Two sites publish PartyMemberAdded, and both diff the marching list rather
// than reading their arguments. bdPartyAddMember builds the PlyTask nodes, one
// per character, and every caller is a construction path, so on its own the
// event would fire at load and never again. Joining an existing party is the
// script opcode, which finds the node on the roster and links it into the
// marching list itself instead of calling back here. The diff also settles the
// two questions the arguments cannot: bdPartyAddMember registers a node on the
// roster whether or not it marches, and the opcode both adds and removes.

// PlayerSpawned rides the same call and is not a second spelling of
// PartyMemberAdded. bdPartyAddMember builds the PlyTask and links it to the
// roster whatever r5 says, and bdGameTaskUpdate has two alternative session
// init blocks: one passes r5 = 0 for all five members and threads the marching
// list by hand afterwards, so the marching diff sees nothing at all, and the
// other passes the slot's unlock bit, so an unlocked slot there does raise both
// events for the one node. The gap is on the first block rather than on every
// call, and each event still means what it says where the two coincide.
REX_EXTERN(__imp__bdPartyAddMember);
REX_HOOK_RAW(bdPartyAddMember) {
  const u32 entity = ctx.r3.u32;
  const std::vector<u32> before = bd::engine::ActiveNodes(entity);
  __imp__bdPartyAddMember(ctx, base);
  const u32 node = ctx.r3.u32;
  // The marching diff is taken before either publish, because a subscriber
  // running guest code can relink the list it reads.
  const u32 joined = bd::engine::JoinedSince(entity, before);
  if (node)
    bd::engine::Events::Publish(bd::engine::PlayerSpawned{node});
  if (joined)
    bd::engine::Events::Publish(bd::engine::PartyMemberAdded{joined});
}

REX_EXTERN(__imp__bdScriptOpPartyChange);
REX_HOOK_RAW(bdScriptOpPartyChange) {
  const u32 entity =
      bd::mem::try_load<u32>(bd::engine::addr::kFieldPlayerEntity);
  const std::vector<u32> before = bd::engine::ActiveNodes(entity);
  __imp__bdScriptOpPartyChange(ctx, base);
  const u32 joined = bd::engine::JoinedSince(entity, before);
  if (joined)
    bd::engine::Events::Publish(bd::engine::PartyMemberAdded{joined});
}

// The leader is the head of the marching list, and the original rotates the
// list until the member it was asked for reaches it. Callers ask for a leader
// that already leads, which the head comparison absorbs, and the rotation can
// also fail to find one, which leaves the head where it was.
REX_EXTERN(__imp__bdPartySetLeader);
REX_HOOK_RAW(bdPartySetLeader) {
  const u32 entity = ctx.r3.u32;
  const u32 before = bd::engine::LeaderNode(entity);
  __imp__bdPartySetLeader(ctx, base);
  const u32 after = bd::engine::LeaderNode(entity);
  if (after && after != before)
    bd::engine::Events::Publish(bd::engine::PartyLeaderChanged{after});
}
