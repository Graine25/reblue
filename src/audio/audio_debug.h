/**
 * @file    audio/audio_debug.h
 * @brief   Audio debug: engine debug display pokes.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause - see LICENSE
 */
#pragma once

namespace bd::audio {

// Reapplied every frame, since engine init can rewrite the globals.
void ApplyAudioDebugPokes();

} // namespace bd::audio
