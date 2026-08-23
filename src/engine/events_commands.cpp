/**
 * @file    engine/events_commands.cpp
 * @brief   Console command (category "GameState") over the engine event trace.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 */
#include <string_view>

#include <rex/cvar.h>

#include "core/logging.h"
#include "engine/events.h"

REXCVAR_DEFINE_COMMAND_ARGS(
    game_events,
    [](std::string_view) {
      const auto recent = bd::engine::EventLog::Recent();
      BD_INFO("[events] {} published, last {} shown",
              bd::engine::EventLog::TotalPublished(), recent.size());
      for (const auto &e : recent)
        BD_INFO("[events]   tick {:<8} {:<20} {:#010x} {:#010x}", e.tick,
                e.name ? e.name : "?", e.a, e.b);
    },
    "GameState", "Print the most recently published engine events");
