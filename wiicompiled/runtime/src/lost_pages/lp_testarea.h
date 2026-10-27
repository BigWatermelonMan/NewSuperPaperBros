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

// Story battles the test area replays (F5). Each sets the story to just before the fight and loads its map, so the
// original scene, the battle and the story after it all play as they do in a normal game. Add each boss battle
// here once it's done.
struct StoryBattle {
    const char* boss;
    const char* where;  // chapter and area
    const char* map;
    const char* entrance;  // where Mario comes in (the intro may depend on it); nullptr: the map's default
    int gsw0;              // story progress (GSW0) that plays the fight's intro
    const char* about;  // a line on how the fight works
};
const std::vector<StoryBattle>& StoryBattles();
// The F5 menu; any thread (the overlay drives it, the game thread acts on a pick).
void ToggleStoryMenu();
bool StoryMenuOpen();
int StoryMenuSelection();
void StoryMenuMove(int delta);
void StoryMenuConfirm();

// Battle stage / test room parameters for the renderer around a world point on the floor.
void SetStage(Guest& g, int mode, const Vec3& floorPoint, float efbHeight=528.f);

}  // namespace LostPages
