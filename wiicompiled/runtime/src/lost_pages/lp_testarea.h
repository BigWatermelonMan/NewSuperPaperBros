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
// True from an F5 pick until Mario is back in the test room: a defeat there returns to the room, no game over.
bool StoryTestRun();
void ReturnToTestArea(Guest& g);  // game thread

// Cutscenes the test area teleports to (F6, the same menu as F5). Each warps to the scene's room at the story point
// just before it, puts Mario where it starts, and starts its script if the room doesn't on its own.
struct Cutscene {
    const char* name;
    const char* where;
    const char* map;
    const char* entrance;  // nullptr: the map's default
    int gsw0;
    float marioX;     // where Mario is put two seconds after arriving
    uint32_t script;  // the scene's event script, started if no cutscene is running a second later
    const char* about;
};
const std::vector<Cutscene>& Cutscenes();
void ToggleCutsceneMenu();
bool CutsceneMenu();  // the open menu lists cutscenes (F6) rather than story battles (F5)
// From npcMain outside battle: places Mario and starts the scene after an F6 warp.
void CutsceneArrivalTick(Guest& g);

// The level picker (F7 in the test area): Left/Right +-1, Up/Down +-10, Enter applies. Mario's HP, ATK and BP are
// rebuilt as if he had levelled up to it, the level-ups shared out HP, ATK, BP in turn. Any thread except the apply.
void ToggleLevelPicker(int currentLevel);
bool LevelPickerOpen();
int LevelPickerLevel();
void LevelPickerMove(int delta);
void LevelPickerConfirm();

// A battle's own diorama instead of its area's (the final bosses: kBleckStage, kDimentioStage); -1 clears it.
constexpr int kBleckStage = 24, kDimentioStage = 25;
void SetBattleStagePreset(int preset);

// Battle stage / test room parameters for the renderer around a world point on the floor.
void SetStage(Guest& g, int mode, const Vec3& floorPoint, float efbHeight=528.f);

}  // namespace LostPages
