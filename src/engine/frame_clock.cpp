/**
 * @file    engine/frame_clock.cpp
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#include "engine/frame_clock.h"

#include <algorithm>
#include <chrono>

#include "engine/cutscene.h"
#include "engine/settings.h"

namespace bd::engine {
namespace {

constexpr double kTick = 1.0 / 30.0;
constexpr double kMaxDelta = 1.0 / 15.0;
constexpr int kMaxTicksPerIter = 4;

using Clock = std::chrono::steady_clock;

double g_lastTime = 0.0;
double g_accum = 0.0;
float g_alpha = 0.0f;
bool g_tickDue = true;
u64 g_tickCount = 0;
double g_tps = 30.0;

double NowSeconds() {
  static const Clock::time_point kEpoch = Clock::now();
  return std::chrono::duration<double>(Clock::now() - kEpoch).count();
}

} // namespace

bool InterpolationActive() {
  const i32 fps = Settings::Get().FPSLimit();
  return (fps == 0 || fps > 30) && !SofdecMoviePlaying() &&
         !EventScenePlaying();
}

void Advance() {
  if (!InterpolationActive()) {
    g_tickDue = true;
    g_alpha = 0.0f;
    g_lastTime = 0.0;
    return;
  }

  const double now = NowSeconds();
  double dt = (g_lastTime > 0.0) ? (now - g_lastTime) : kTick;
  g_lastTime = now;
  dt = std::clamp(dt, 0.0, kMaxDelta);

  g_accum += dt;

  int ticks = 0;
  while (g_accum >= kTick && ticks < kMaxTicksPerIter) {
    g_accum -= kTick;
    ++ticks;
  }
  if (ticks == kMaxTicksPerIter) {
    g_accum = 0.0;
  }

  g_tickDue = ticks > 0;
  if (g_tickDue) {
    g_tickCount += ticks;
    g_tps = g_tps * 0.95 + (ticks / std::max(dt, 1e-6)) * 0.05;
  }
  g_alpha = static_cast<float>(std::clamp(g_accum / kTick, 0.0, 0.9999));
}

bool TickDue() { return g_tickDue; }
float Alpha() { return InterpolationActive() ? g_alpha : 0.0f; }
u64 TickCount() { return g_tickCount; }
double TicksPerSecond() { return g_tps; }

} // namespace bd::engine
