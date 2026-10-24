// Lost Pages test area: a blank green-and-white room for trying out enemies and new features. F10 on the
// title screen opens it (SPM_LP_TESTAREA=1 does too, for automated runs).
#pragma once

#include "lp_guest.h"

#include <string>
#include <vector>

namespace LostPages {

void RequestTestArea();  // any thread
bool TestAreaActive();
// From the VI tick: on the title screen, a pending request warps into the test map.
void TestAreaTitleTick(CpuContext* ctx);
// From npcMain while no battle runs: keeps the map hidden, the room painted and the test enemies spawned.
void TestAreaFieldTick(Guest& g);
// The test enemies currently on offer (for the overlay's label).
std::vector<std::string> TestAreaRoster();

// Battle stage / test room parameters for the renderer around a world point on the floor.
void SetStage(Guest& g, int mode, const Vec3& floorPoint);

}  // namespace LostPages
