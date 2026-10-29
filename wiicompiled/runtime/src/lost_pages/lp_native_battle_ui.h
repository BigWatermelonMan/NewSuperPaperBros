// US0 native event messages for battle dialogue. All calls belong on the guest/game thread.
#pragma once

#include "lp_guest.h"

namespace LostPages {

// Starts one blocking evt_msg_print script; returns false on invalid input, allocation failure,
// missing message key, or while our previous dialogue is still alive. Does not stop/resume world events.
// Call AFTER evtStopAll. speaker is an actual NPC entry name ("dodon"), or nullptr for a centered box.
// Unknown NPC names fall back to centered. literal accepts the game's message markup, not a message key.
bool StartDialogue(Guest& g, const char* msgName, const char* speaker, bool literal = false);
// Poll once per game frame. With SPM_LP_AUTOPLAY set, closes our box after 90 polls (test fixture only).
bool DialogueActive(Guest& g);
// Cancels only our event and closes only its window. Safe after natural completion or a map change.
void ReleaseDialogue(Guest& g);
// An active NPC entry with this name.
bool NpcActive(Guest& g, const char* name);
// Any of the game's message windows (dialogue boxes) is up.
bool MessageWindowOpen(Guest& g);
// A live event is executing a command whose arguments lie in [lo, hi] (a known spot in a story script).
bool EventRunningBetween(Guest& g, uint32_t lo, uint32_t hi);

} // namespace LostPages
