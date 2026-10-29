// Per-scene pose swaps for Mario (see lp_scene_poses.cpp).
#pragma once

#include "lp_guest.h"

namespace LostPages {

// From npcMain every frame: moves a running swap on to its next animation.
void ScenePoseTick(Guest& g);

}  // namespace LostPages
