/**
 * @file    platform/crash_handler.h
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#pragma once

namespace bd::platform {

// Install the last-chance host crash reporters. Call once, after the Runtime
// and logging are up. Covers access violations and illegal instructions
// (chained *after* the SDK's guest MMIO handler, so it only fires on faults the
// runtime did not resolve), plus abort/bus/arithmetic signals and uncaught C++
// exceptions. Each logs via BD_CRITICAL, flushes, then lets the OS default
// action terminate the process so a core dump is written.
void InstallCrashHandler();

// The subset needing only logging: uncaught exceptions and abort/bus/arithmetic
// signals. Call early, or a throw during startup dies silently.
void InstallTerminateHandler();

// Give the calling thread a signal alt stack so a stack overflow fault can
// still be reported. sigaltstack is per-thread, and InstallCrashHandler only
// covers the thread that calls it. Other threads opt in with this. No-op on
// Windows and on a thread that already has one.
void InstallCrashStackForThread();

} // namespace bd::platform
