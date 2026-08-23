/**
 * @file    engine/virtual_buttons.h
 * @license BSD 3-Clause, see LICENSE
 */
#pragma once

#include "engine/d2anime/anime_input.h"

namespace bd::engine {

// True while a d2anime menu has input focus. Published by the AnimeMenu_Update
// hook and read by the button layer, so a key can mean one thing in the field
// and another inside a menu.
bool MenuOwnsInput();
void SetMenuOwnsInput(bool owns);

// True while one of reblue's own ImGui surfaces is up. Those need the system
// pointer, and the SDK's input-active callback only knows about the overlays
// reblue replaced, so reblue has to speak for its own. Everything the pointer
// layer does stands down for it: mouse look lets the cursor go, the drawn
// cursor is not drawn, the arrow is not hidden, and hover stops following the
// mouse onto whatever guest list happens to be behind the window.
bool HostOverlayOwnsPointer();

// Records that the guest pad reported a press this frame, and reads that back
// draining. True for a key the MnK driver turned into pad state too, so a
// caller telling the devices apart has to test the keyboard first.
void PadInputSeen();
bool TakePadInputSeen();

// Samples the physical mouse button levels this guest frame and latches any
// down transition for SynthesizedButton to read. Must be called exactly once
// per guest frame, before any bdInputCheckButton polling that frame.
void SampleButtonEdges();

// The press this frame carries for a button the pad did not report, so a
// caller can add it to whatever the guest input manager said. Reads only
// latched edges, so any number of callers can poll it in the same frame and
// each sees the press.
bool SynthesizedButton(Button btn);

// Queues a one-frame synthetic press of a pad button, for a host bind that
// stands in for one. Activated at the next SampleButtonEdges, so every
// handler polling that frame sees the same press, then cleared. The guest's
// own gating applies exactly as it would to the physical button.
void PressButton(int padButton);

// The same question asked of the key's level rather than its edge, for the
// readers that sweep a value while a direction is held.
bool SynthesizedButtonHeld(Button btn);

// Hands the right stick to the mouse while the look button is held and no menu
// is up. Called once per guest frame, after the menu layer has published who
// owns input.
void UpdateMouseLook();

} // namespace bd::engine
