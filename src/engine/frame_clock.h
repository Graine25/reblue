/**
 * @file    engine/frame_clock.h
 * @brief   Fixed 30Hz logic tick clock for the fps unlock.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#pragma once

#include <rex/types.h>

namespace bd::engine {

// Advance the tick clock. Called once per bdMainGameStep iteration, before the
// guest's logic block.
void Advance();

// True when the render cap (bd_fps_limit) exceeds 30Hz or is unlimited (0), and
// neither cutscene system forces native coupled mode. Sofdec movies advance
// their clock from the per-frame delta, and event render lists live in the
// flip-recycled Visual::Tag pool and need the vf02 update walk before every
// rendered frame, so both must run coupled at 30Hz
bool InterpolationActive();

// True when this iteration should execute BD's 30Hz logic block.
// Always true when interpolation is inactive.
bool TickDue();

// Interpolation phase in [0,1): fraction of the current tick elapsed.
float Alpha();

// Monotonic count of logic ticks issued.
u64 TickCount();

// Smoothed measured logic tick rate, for the F3 overlay.
double TicksPerSecond();

} // namespace bd::engine
