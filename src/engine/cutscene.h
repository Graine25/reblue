/**
 * @file    engine/cutscene.h
 * @brief   The two unrelated cutscene systems, reported separately: compiled
 *          .evt scenes replayed by issEvent, and prerendered Sofdec .sfd
 * movies.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 */
#pragma once

#include <string>

#include <rex/types.h>

namespace bd::engine {

// Both can be live at once: ev001 ships ev001_evt_99.evt and ev001.sfd.
bool EventScenePlaying();
bool SofdecMoviePlaying();

// The first live issEvent, of up to eight tracked.
class Cutscene {
public:
  Cutscene() = default;

  explicit operator bool() const { return EventScenePlaying(); }

  int LiveCount() const; // a pack event and a viewEvent can overlap
  u32 TaskAddress() const;
  int Tasks(u32 *out, int max) const;
  int EventId()
      const; // issEvent+0x3AC, eventNumber*100 + sceneNumber, -1 if none
  int EventNumber() const;
  int SceneNumber() const;
  std::string Prefix() const; // "ev" or "sv", empty if none
};

class Movie {
public:
  Movie() = default;

  explicit operator bool() const { return SofdecMoviePlaying(); }

  int Status() const; // mwPly status, 2 advances a frame, -1 if never seen
};

} // namespace bd::engine
