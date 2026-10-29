#include "lp_testarea.h"
#include "lp_battle.h"
#include "lp_badge_system.h"
#include "lp_progress.h"

#include <aurora/gfx.h>

#include "runtime_log.h"

#include <array>
#include <algorithm>
#include <atomic>
#include <cstdlib>

namespace LostPages {
namespace {

constexpr uint32_t kGpPtr = 0x8056D198;  // SpmarioGlobals* gp
constexpr uint32_t kGpMapName = 0x44, kGpGsw0 = 0x140;
constexpr uint32_t kNpcTribes = 0x803FC290, kNpcTribeCount = 535, kTribeSize = 0x68;
constexpr uint32_t kNpcTemplates = 0x80409BE8, kNpcTemplateCount = 435, kTemplateSize = 0x68;
constexpr uint32_t kSeqTitle = 1, kSeqMapChange = 3;

// A flat room with no enemies of its own: Lineland's door room, its geometry hidden and painted over.
constexpr const char* kTestMap = "he1_02";
// Story progress for the visit: past the game, so no cutscene claims the room.
constexpr int32_t kTestGsw0 = 425;

// Test enemies (tribe model names), lined up to the right of the entrance. More get added here over time.
constexpr std::array<const char*, 5> kRoster{"e_kuribo", "e_nokoteki", "e_tekti", "e_octa2", "e_pakflwr"};

std::atomic<bool> g_requested{false};
bool g_session = false;  // entered through the title screen, still in the test map
bool g_inMap = false;
int g_mapFrames = 0;
bool g_spawned = false;
bool g_storyTest = false;  // an F5 story battle is under way
std::vector<std::string> g_roster;

// The F5 story battle menu: the overlay opens it and moves the cursor; the game thread carries out a pick.
std::atomic<bool> g_storyMenu{false};
std::atomic<int> g_storySelection{0};
std::atomic<int> g_storyPick{-1};
std::atomic<bool> g_cutsceneList{false};  // the open menu shows Cutscenes() (F6)
std::atomic<int> g_cutscenePick{-1};
// The F7 level picker; the game thread applies a confirmed level.
std::atomic<bool> g_levelPicker{false};
std::atomic<int> g_levelPick{1};
std::atomic<int> g_levelApply{-1};
// After an F6 warp: the scene to set up once its room is loaded.
struct Arrival {
    int index = -1;
    int waited = 0;  // frames waiting for the room
    int frames = 0;  // frames in the room
} g_arrival;

uint32_t Gp() {
    uint32_t gp = 0;
    return Memory::TryRead32(kGpPtr, gp) && Memory::Contains(gp, 0x200) ? gp : 0;
}
std::string CurrentMap() {
    const uint32_t gp = Gp();
    return gp ? GuestString(gp + kGpMapName, 32) : std::string{};
}

int FindTribe(const char* animPose) {
    for (uint32_t i = 0; i < kNpcTribeCount; ++i) {
        const uint32_t tribe = kNpcTribes + i * kTribeSize;
        if (GuestString(Memory::Read32(tribe), 32) == animPose) return static_cast<int>(i);
    }
    return -1;
}
uint32_t FindTemplate(int tribe) {
    for (uint32_t i = 0; i < kNpcTemplateCount; ++i) {
        const uint32_t t = kNpcTemplates + i * kTemplateSize;
        if (static_cast<int32_t>(Memory::Read32(t + 0x14)) == tribe) return t;
    }
    return 0;
}

void SpawnRoster(Guest& g) {
    const uint32_t mario = g.MarioPtr();
    const Vec3 at = ReadVec3(mario + Mario::position);
    g_roster.clear();
    int slot = 0;
    for (const char* pose : kRoster) {
        const int tribe = FindTribe(pose);
        const uint32_t tmpl = tribe >= 0 ? FindTemplate(tribe) : 0;
        if (!tmpl) {
            RT_LOGF(RT_TAG_RUNTIME, "[lptest] no template for %s\n", pose);
            continue;
        }
        // A private copy of the game's own template (its AI scripts included) at our spot.
        const uint32_t copy = g.Scratch(0x100);
        for (uint32_t i = 0; i < kTemplateSize; i += 4) Memory::Write32(copy + i, Memory::Read32(tmpl + i));
        Memory::Write32(copy + 0x04, g.Call(0x801BEB88));  // npcGetNextTemplateNpcId
        Memory::Write32(copy + 0x0C, 0);                    // no spawn condition
        Memory::Write32(copy + 0x18, 0);                    // generated instance name
        WriteVec3(copy + 0x20, {at.x + 150.f + 120.f * static_cast<float>(slot), at.y, at.z});
        for (uint32_t i = 0x5C; i < kTemplateSize; i += 4) Memory::Write32(copy + i, 0);
        const uint32_t npc = g.Call(0x801BD118, {copy});  // npcEntryFromTemplate
        RT_LOGF(RT_TAG_RUNTIME, "[lptest] spawned %s (tribe %d) -> %08X\n", pose, tribe, npc);
        if (npc) g_roster.push_back(EnemyName(g, tribe));
        ++slot;
    }
}

}  // namespace

void RequestTestArea() { g_requested = true; }
bool TestAreaActive() { return g_session && g_inMap; }
bool StoryTestRun() { return g_storyTest; }

void ReturnToTestArea(Guest& g) {
    const uint32_t gp = Gp(), name = GameString(kTestMap);
    if (!gp || !name) return;
    Memory::Write32(gp + kGpGsw0, static_cast<uint32_t>(kTestGsw0));
    g_session = true;
    g_inMap = false;
    g_mapFrames = 0;
    g_spawned = false;
    g_storyTest = false;
    g.Call(Fn::seqSetSeq, {kSeqMapChange, name, 0});
    RT_LOGF(RT_TAG_RUNTIME, "[lptest] back to the test area\n");
}
std::vector<std::string> TestAreaRoster() { return g_roster; }

const std::vector<StoryBattle>& StoryBattles() {
    static const std::vector<StoryBattle> battles{
        {"O'Chunks", "1-3  Yold Desert", "he3_04", nullptr, 33,
         "Shoulder Bash, Ground Pound, Grab 'n' Toss. Thoreau can toss him."},
        {"Fracktail", "1-4  Yold Ruins", "he4_10", "dokan_1", 51,
         "Throw Frackles with Thoreau to lower the head, then hit the antenna."},
        {"Count Bleck", "8-4  Castle Bleck", "ls4_11", nullptr, 410,
         "Three protected turns, the Pure Hearts arrive, strike the barrier to break it, then fight."},
        {"Super Dimentio", "8-4  The Final Battle", "ls4_11", nullptr, 411,
         "His entrance, three barrier turns, \"You can't defeat me!\", then the real fight."},
    };
    return battles;
}

const std::vector<Cutscene>& Cutscenes() {
    static const std::vector<Cutscene> scenes{
        {"The Void destroys Sammer's Kingdom", "6  Sammer's Kingdom", "wa1_02", nullptr, 279, 0.f, 0x80D7B34Cu,
         "\"We're too late?!\" Ends with Mario's E_3 / E_4 (in place of T_7)."},
    };
    return scenes;
}

void ToggleStoryMenu() {
    if (!g_storyMenu && !TestAreaActive()) return;
    if (!g_storyMenu) g_storySelection = 0;
    g_cutsceneList = false;
    g_storyMenu = !g_storyMenu;
}
void ToggleCutsceneMenu() {
    if (!g_storyMenu && !TestAreaActive()) return;
    if (!g_storyMenu) g_storySelection = 0;
    g_cutsceneList = true;
    g_storyMenu = !g_storyMenu;
}
bool CutsceneMenu() { return g_cutsceneList; }
bool StoryMenuOpen() { return g_storyMenu && TestAreaActive(); }
int StoryMenuSelection() { return g_storySelection; }
void StoryMenuMove(int delta) {
    const int n = static_cast<int>(g_cutsceneList ? Cutscenes().size() : StoryBattles().size());
    g_storySelection = ((g_storySelection + delta) % n + n) % n;
}
void StoryMenuConfirm() {
    if (!StoryMenuOpen()) return;
    (g_cutsceneList ? g_cutscenePick : g_storyPick) = g_storySelection.load();
    g_storyMenu = false;
}

void ToggleLevelPicker(int currentLevel) {
    if (!g_levelPicker && !TestAreaActive()) return;
    if (!g_levelPicker) g_levelPick = std::clamp(currentLevel, 1, kMaxLevel);
    g_storyMenu = false;
    g_levelPicker = !g_levelPicker;
}
bool LevelPickerOpen() { return g_levelPicker && TestAreaActive(); }
int LevelPickerLevel() { return g_levelPick; }
void LevelPickerMove(int delta) { g_levelPick = std::clamp(g_levelPick.load() + delta, 1, kMaxLevel); }
void LevelPickerConfirm() {
    if (!LevelPickerOpen()) return;
    g_levelApply = g_levelPick.load();
    g_levelPicker = false;
}

namespace {
// Mario as if he had levelled up to `level` from a new game (10 HP, 1 ATK, 3 BP), choosing HP, ATK, BP in turn.
void ApplyTestLevel(Guest& g, int level) {
    const uint32_t pouch = g.PouchPtr();
    if (!Memory::Contains(pouch, 0x20)) return;
    const int ups = level - 1;
    const int hp = 10 + 5 * ((ups + 2) / 3), attack = 1 + (ups + 1) / 3, bp = std::min(99, 3 + 3 * (ups / 3));
    SaveProgress(g, {level, 0});
    g.Call(Fn::pouchSetMaxHp, {static_cast<uint32_t>(hp)});
    g.Call(Fn::pouchSetAttack, {static_cast<uint32_t>(attack)});
    Badges(g, true).bp = bp;
    g.Call(Fn::pouchSetHp, {static_cast<uint32_t>(BattleMaxHp(g))});
    RecoverFp(g, BattleMaxFp(g));
    SaveBadges(g);
    RT_LOGF(RT_TAG_RUNTIME, "[lptest] level %d: max HP %d, ATK %d, BP %d\n", level, hp, attack, bp);
}
}  // namespace

// Which area's diorama a battle in `map` gets (the presets in aurora's scene_fx.cpp).
int StagePresetFor(const std::string& map) {
    if(map=="he4_10")return 3; // Fracktail's outdoor desert arena, beyond the interior ruins
    static constexpr std::pair<const char*, int> kAreas[] = {
        {"he1", 1}, {"he2", 2}, {"he3", 3}, {"he4", 4},    // Lineland Road, Mount Lineland, Yold Desert, Yold Ruins
        {"mi1", 5}, {"mi4", 7}, {"mi", 6},                // Gloam Valley, the mansion's basement, Merlee's Mansion
        {"ta1", 8}, {"ta2", 9}, {"ta3", 10}, {"ta4", 11}, // the Bitlands, Tile Pool, Dotwood Tree, Fort Francis
        {"sp2", 13}, {"sp4", 14}, {"sp", 12},             // Planet Blobule, the Whoa Zone, Outer Space
        {"gn3", 16}, {"gn4", 16}, {"gn", 15},             // Floro Caverns, Land of the Cragnons
        {"wa1", 17}, {"wa", 18},                          // Sammer's Kingdom, then in the void
        {"an1", 19}, {"an2", 19}, {"an", 20},             // the Underwhere, the Overthere
        {"ls", 21},                                       // Castle Bleck
        {"dan", 23},                                      // the Pits of 100 Trials
    };
    for (const auto& [prefix, preset] : kAreas)
        if (map.rfind(prefix, 0) == 0) return preset;
    return 22;  // Flipside, Flopside and the rest
}

std::atomic<int> g_battlePreset{-1};
void SetBattleStagePreset(int preset) { g_battlePreset = preset; }

void SetStage(Guest& g, int mode, const Vec3& floorPoint, float efbHeight) {
    float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    g.ScreenPos({0.f, floorPoint.y, floorPoint.z}, x0, y0);
    g.ScreenPos({100.f, floorPoint.y + 100.f, floorPoint.z}, x1, y1);
    float fx = 0, fy = 0;
    g.ScreenPos(floorPoint, fx, fy);
    // Use the battle camera's EFB coordinates, then convert to the 480-line overlay.
    const float kEfbToPicture = 480.f / efbHeight;
    y0 *= kEfbToPicture;
    y1 *= kEfbToPicture;
    fy *= kEfbToPicture;
    const float unitX = (x1 - x0) / 100.f, unitY = (y0 - y1) / 100.f;
    // SPM_LP_STAGE_PRESET=n shows that area's diorama in every battle (for testing the presets).
    static const char* forced = std::getenv("SPM_LP_STAGE_PRESET");
    const int own = g_battlePreset;
    const int preset = forced ? std::atoi(forced) : own >= 0 && mode == 2 ? own : TestAreaActive() ? 0 : StagePresetFor(CurrentMap());
    static int probes = 0;
    if (std::getenv("SPM_LP_STAGE_LOG") && mode == 2 && probes++ < 120)
        RT_LOGF(RT_TAG_RUNTIME, "[lpstage] %d mode %d efb %.0f floor %.1f v %.4f units %.3f %.3f preset %d\n",
                probes, mode, efbHeight, fy, fy / 480.f, unitX, unitY, preset);
    aurora_set_scene_fx_stage(mode, fy / 480.f, x0, unitX != 0.f ? unitX : 1.f, unitY != 0.f ? unitY : 1.f, preset);
}

void TestAreaTitleTick(CpuContext* ctx) {
    static const bool autoStart = std::getenv("SPM_LP_TESTAREA") != nullptr;
    uint32_t seq = 0;
    if (!Memory::TryRead32(0x8056D0D8u, seq)) return;
    const bool resetVisit = seq == 2 && TestAreaActive() && !BattleUiSnapshot().active;
    if (seq == kSeqTitle) g_storyTest = false;  // back at the title: a fresh game
    if (seq != kSeqTitle && !resetVisit) return;
    static int titleFrames = 0;
    if (autoStart && seq == kSeqTitle && ++titleFrames == 20) g_requested = true;
    if (!g_requested.exchange(false)) return;
    Guest g(ctx);
    const uint32_t name = GameString(kTestMap);
    const uint32_t gp = Gp();
    if (!name || !gp) {
        RT_LOGF(RT_TAG_RUNTIME, "[lptest] can't open the test area (map name %08X, gp %08X)\n", name, gp);
        return;
    }
    Memory::Write32(gp + kGpGsw0, static_cast<uint32_t>(kTestGsw0));
    g_session = true;
    g_inMap = false;
    g_mapFrames = 0;
    g_spawned = false;
    g.Call(Fn::seqSetSeq, {kSeqMapChange, name, 0});
    RT_LOGF(RT_TAG_RUNTIME, "[lptest] opening the test area (%s)\n", kTestMap);
}

void TestAreaFieldTick(Guest& g) {
    const bool inMap = g_session && CurrentMap() == kTestMap;
    if (inMap != g_inMap) {
        g_inMap = inMap;
        g_mapFrames = 0;
        if (!inMap) {
            g_session = false;  // walked out: the next map is a normal one
            g_roster.clear();
            aurora_set_scene_fx_stage(0, 0.8f, 0.f, 1.f, 1.f, 0);
        }
    }
    if (!g_inMap) return;
    ++g_mapFrames;
    // SPM_LP_TEST_STORY_BATTLE=n picks story battle n (from 1) once the room is up (automated runs).
    static const char* autoPick = std::getenv("SPM_LP_TEST_STORY_BATTLE");
    static bool autoPicked = false;
    if (autoPick && !autoPicked && g_mapFrames == 90) {
        autoPicked = true;
        g_storyPick = std::clamp(std::atoi(autoPick) - 1, 0, static_cast<int>(StoryBattles().size()) - 1);
    }
    // SPM_LP_TEST_CUTSCENE=n picks cutscene n (from 1) the same way.
    static const char* autoScene = std::getenv("SPM_LP_TEST_CUTSCENE");
    static bool autoScenePicked = false;
    if (autoScene && !autoScenePicked && g_mapFrames == 90) {
        autoScenePicked = true;
        g_cutscenePick = std::clamp(std::atoi(autoScene) - 1, 0, static_cast<int>(Cutscenes().size()) - 1);
    }
    // SPM_LP_TEST_LEVEL=n applies level n once the room is up (automated runs).
    static const char* autoLevel = std::getenv("SPM_LP_TEST_LEVEL");
    if (autoLevel && g_mapFrames == 60) g_levelApply = std::clamp(std::atoi(autoLevel), 1, kMaxLevel);
    if (const int level = g_levelApply.exchange(-1); level > 0) ApplyTestLevel(g, level);
    if (const int pick = g_cutscenePick.exchange(-1); pick >= 0) {
        const Cutscene& scene = Cutscenes()[pick];
        const uint32_t gp = Gp(), name = GameString(scene.map);
        if (gp && name) {
            g_session = g_inMap = false;
            g_storyTest = true;  // a defeat on the way back still returns here
            g_roster.clear();
            g_storyMenu = false;
            aurora_set_scene_fx_stage(0, 0.8f, 0.f, 1.f, 1.f, 0);
            Memory::Write32(gp + kGpGsw0, static_cast<uint32_t>(scene.gsw0));
            g_arrival = {pick, 0, 0};
            g.Call(Fn::seqSetSeq, {kSeqMapChange, name, scene.entrance ? GameString(scene.entrance) : 0u});
            RT_LOGF(RT_TAG_RUNTIME, "[lptest] cutscene %s: %s at GSW0 %d\n", scene.name, scene.map, scene.gsw0);
            return;
        }
    }
    if (const int pick = g_storyPick.exchange(-1); pick >= 0) {
        // Leave the test room for the story: the fight's map at its intro, with Mario at full HP.
        const StoryBattle& battle = StoryBattles()[pick];
        const uint32_t gp = Gp(), name = GameString(battle.map);
        if (gp && name) {
            g_session = g_inMap = false;
            g_storyTest = true;
            g_roster.clear();
            g_storyMenu = false;
            aurora_set_scene_fx_stage(0, 0.8f, 0.f, 1.f, 1.f, 0);
            Memory::Write32(gp + kGpGsw0, static_cast<uint32_t>(battle.gsw0));
            g.Call(Fn::pouchSetHp, {g.Call(0x8014C9F4)});  // pouchGetMaxHp
            g.Call(Fn::seqSetSeq, {kSeqMapChange, name, battle.entrance ? GameString(battle.entrance) : 0u});
            RT_LOGF(RT_TAG_RUNTIME, "[lptest] story battle %s: %s at GSW0 %d\n", battle.boss, battle.map, battle.gsw0);
            return;
        }
        RT_LOGF(RT_TAG_RUNTIME, "[lptest] can't open story battle %s (map name %08X)\n", battle.boss, name);
    }
    g.Call(Fn::mapDispOff);  // cheap, and the map may turn itself back on during loading
    const uint32_t mario = g.MarioPtr();
    if (!Memory::Contains(mario, 0x130)) return;
    if (!g_spawned && g_mapFrames == 45) {
        g_spawned = true;
        SpawnRoster(g);
    }
    const Vec3 pos = ReadVec3(mario + Mario::position);
    SetStage(g, 3, {pos.x, g_lastGroundY, pos.z});
}

void CutsceneArrivalTick(Guest& g) {
    if (g_arrival.index < 0) return;
    const Cutscene& scene = Cutscenes()[g_arrival.index];
    if (g.Call(Fn::seqGetSeq) != 2 || CurrentMap() != scene.map) {
        if (++g_arrival.waited > 60 * 60) g_arrival.index = -1;  // never arrived
        return;
    }
    const int f = ++g_arrival.frames;
    const uint32_t mario = g.MarioPtr();
    if (!Memory::Contains(mario, 0x70)) return;
    if (f == 120) Memory::WriteFloat32(mario + Mario::position, scene.marioX);
    if (f == 180) {
        // The room may have started the scene on its own when Mario landed on its trigger; never run it twice.
        if (Memory::Read8(mario + Mario::keyOff) == 0) {
            const uint32_t entry = g.Call(0x800D8D58, {scene.script, 0xEF, 0});  // evtEntry
            RT_LOGF(RT_TAG_RUNTIME, "[lptest] cutscene %s started (%08X)\n", scene.name, entry);
        } else {
            RT_LOGF(RT_TAG_RUNTIME, "[lptest] cutscene %s started by the room\n", scene.name);
        }
        g_arrival.index = -1;
    }
}

}  // namespace LostPages
