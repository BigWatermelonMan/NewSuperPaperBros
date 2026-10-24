// Lost Pages battle engine: runs on the game thread right after npcMain each frame, moves Mario and the
// enemies itself (their scripts are paused), and publishes a BattleUi snapshot for the overlay.

#include "lp_battle.h"
#include "lp_guest.h"
#include "lp_progress.h"
#include "lp_testarea.h"

#include "runtime_config.h"
#include "runtime_log.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <mutex>

extern "C" void func_801CA9EC(CpuContext* ctx);  // npcDamageMario
extern "C" void func_801C5568(CpuContext* ctx);  // npcTakeDamage
extern "C" void func_801A8AD8(CpuContext* ctx);  // npcMain

namespace LostPages {

float g_lastGroundY = 0.f;

// Lets the battle's own npcTakeDamage calls (finishing off defeated enemies) through the hook.
bool g_allowGameDamage = false;

namespace {

bool Enabled() {
    static const bool forcedOff = std::getenv("SPM_LP_CLASSIC") != nullptr;
    return !forcedOff && RuntimeConfigFile::TurnBasedBattles(true);
}
bool Recon() {
    static const bool on = std::getenv("SPM_LP_RECON") != nullptr;
    return on;
}

constexpr uint32_t ColWhite = 0xFFFFFFFFu, ColYellow = 0xFF30E6FFu, ColGreen = 0xFF60E070u,
                   ColRed = 0xFF4040FFu, ColBlue = 0xFFFFB050u;
constexpr int kMaxEnemies = 3;
constexpr float kEnemyHeight = 30.f;  // where Mario lands on a typical SPM enemy

enum class FirstStrike { None, Mario, Enemy };
enum class Action { Jump, Hammer, Item, Run, Defend };

struct Enemy {
    uint32_t npc = 0;
    uint32_t part = 0;
    int tribe = -1;
    std::string name;
    int hp = 0, maxHp = 0, attack = 1, defense = 0, level = 1;
    bool alive = true;
    bool present = true;  // still a live NPC
    Vec3 start, slot, pos;
    int anim = -1;
    int hurt = 0;
    std::vector<int> anims;  // anim ids its tribe defines
};

struct Popup {
    Vec3 world;
    std::string text;
    uint32_t color = ColWhite;
    int kind = 0;
    int age = 0;
};

struct State {
    bool active = false;
    BattlePhase phase = BattlePhase::None;
    int t = 0;  // frames in phase
    // start request from a collision hook, handled after npcMain
    uint32_t pendingNpc = 0;
    FirstStrike pendingFirst = FirstStrike::None;
    FirstStrike first = FirstStrike::None;
    int cooldown = 0;

    float side = 1.f;  // enemies on Mario's right (+1) or left (-1)
    float groundY = 0.f;
    Vec3 marioHome, marioPos;
    std::vector<Enemy> enemies;
    int enemiesAtStart = 0;

    int menu = 0, target = 0, listIndex = 0;
    std::vector<int> itemSlots;  // pouch use-item indices shown in the item menu

    Action action = Action::Jump;
    int stage = 0, st = 0;  // action stage, frames in stage
    bool pressed = false, success = false, lockout = false;
    int guard = 0;  // 0 none, 1 guard, 2 superguard
    float gauge = 0.f;
    bool holding = false, released = false;
    int enemyTurn = -1;
    bool defending = false;
    int itemToUse = -1;
    int marioHurt = 0;
    std::string marioPose;

    int stageFrame = 0;  // frames since the battle stage went up

    int starPointsGained = 0;
    int levelUps = 0;
    int levelChoice = 0;
    bool defeated = false, fled = false;

    std::vector<Popup> popups;
    std::string banner;
    int bannerT = 0;
};

State s;

// Blocks, switches and other map objects (mobjs) and items lying in the field aren't part of the diorama: they are
// parked far below the stage while the battle runs, then put back exactly where they were.
constexpr uint32_t kMobjWorkSda = 0x7FF0;  // mobjdrv's work pointer at r13 - 0x7FF0 (MobjWork: count, entries)
constexpr uint32_t kMobjSize = 0x2A8, kMobjPos = 0x18;
constexpr uint32_t kItemSize = 0x88, kItemPos = 0x14;  // itemGetWork(): ItemWork { num, entries }
constexpr uint32_t kItemGetWork = 0x80077548;
constexpr float kParkDrop = -10000.f;
struct Parked {
    uint32_t pos;
    Vec3 home;
};
std::vector<Parked> g_parked;

void ParkFieldObjects(Guest& g) {
    if (g_parked.empty()) {
        auto collect = [](uint32_t work, uint32_t size, uint32_t posOffset) {
            if (!Memory::Contains(work, 8)) return;
            const uint32_t count = std::min<uint32_t>(Memory::Read32(work), 0x400);
            const uint32_t entries = Memory::Read32(work + 4);
            if (!Memory::Contains(entries, count * size)) return;
            for (uint32_t i = 0; i < count; ++i) {
                const uint32_t e = entries + i * size;
                if (Memory::Read32(e) & 1) g_parked.push_back({e + posOffset, ReadVec3(e + posOffset)});  // active
            }
        };
        uint32_t mobjWork = 0;
        if (Memory::TryRead32(g.Sda() - kMobjWorkSda, mobjWork)) collect(mobjWork, kMobjSize, kMobjPos);
        collect(g.Call(kItemGetWork), kItemSize, kItemPos);
        RT_LOGF(RT_TAG_RUNTIME, "[lpbattle] parked %zu field objects\n", g_parked.size());
        if (g_parked.empty()) g_parked.push_back({0, {}});  // nothing to park: don't look again this battle
    }
    for (const auto& p : g_parked)
        if (p.pos) WriteVec3(p.pos, {p.home.x, p.home.y + kParkDrop, p.home.z});
}

void UnparkFieldObjects() {
    for (const auto& p : g_parked)
        if (p.pos) WriteVec3(p.pos, p.home);
    g_parked.clear();
}
std::mutex g_uiMutex;
BattleUi g_ui;

float Lerp(float a, float b, float t) { return a + (b - a) * t; }
float Smooth(float t) { return t * t * (3.f - 2.f * t); }
Vec3 LerpV(const Vec3& a, const Vec3& b, float t) { return {Lerp(a.x, b.x, t), Lerp(a.y, b.y, t), Lerp(a.z, b.z, t)}; }
Vec3 Arc(const Vec3& a, const Vec3& b, float height, float t) {
    Vec3 p = LerpV(a, b, t);
    p.y += 4.f * height * t * (1.f - t);
    return p;
}

void Banner(const std::string& text) {
    s.banner = text;
    s.bannerT = 0;
}
void Pop(const Vec3& at, const std::string& text, uint32_t color, int kind) {
    s.popups.push_back({at, text, color, kind, 0});
}
void Go(BattlePhase phase) {
    s.phase = phase;
    s.t = 0;
}
void StartStage(int stage) {
    s.stage = stage;
    s.st = 0;
}

bool IsNpcEntry(Guest& g, uint32_t addr) {
    const uint32_t base = g.Call(Fn::npcGetEntries);
    const uint32_t count = g.Call(Fn::npcGetMaxEntries);
    return addr >= base && addr < base + count * Npc::size && (addr - base) % Npc::size == 0;
}

uint32_t TribePtr(Guest& g, int tribe) {
    if (tribe < 0 || tribe > 600) return 0;
    const uint32_t p = g.Call(Fn::npcGetTribe, {static_cast<uint32_t>(tribe)});
    return Memory::Contains(p, 0x68) ? p : 0;
}

// Ordinary enemies only: bosses (high HP) keep their own scripted fights.
bool Battleable(Guest& g, uint32_t npc) {
    if (!Memory::Contains(npc, Npc::size) || !(Memory::Read32(npc + Npc::flag8) & 1)) return false;
    const int tribe = static_cast<int32_t>(Memory::Read32(npc + Npc::tribeId));
    const uint32_t tp = TribePtr(g, tribe);
    if (!tp) return false;
    const int maxHp = Memory::Read8(tp + Tribe::maxHp);
    const int hp = static_cast<int32_t>(Memory::Read32(npc + Npc::hp));
    return maxHp >= 1 && maxHp <= 40 && hp > 0 && Memory::Read32(npc + Npc::parts) != 0;
}

bool CanStartBattle(Guest& g) {
    if (!Enabled() || s.active || s.pendingNpc || s.cooldown > 0) return false;
    if (g.Call(Fn::seqGetSeq) != 2) return false;  // in a map, not a menu or map change
    const uint32_t mario = g.MarioPtr();
    return Memory::Contains(mario, 0x130) && Memory::Read8(mario + Mario::keyOff) == 0;  // not in a cutscene
}

std::vector<int> TribeAnims(uint32_t tp) {
    std::vector<int> ids;
    uint32_t defs = Memory::Read32(tp + 0x4);
    for (int i = 0; i < 48 && Memory::Contains(defs, 8); ++i, defs += 8) {
        const int32_t id = static_cast<int32_t>(Memory::Read32(defs));
        if (id < 0) break;
        ids.push_back(id);
    }
    return ids;
}

}  // namespace

std::string EnemyName(Guest& g, int tribe) {
    const uint32_t tp = TribePtr(g, tribe);
    if (!tp) return "Enemy";
    std::string name;
    // The enemy's own name, from its Catch Card ("Goomba Card" -> "Goomba", in the game's language).
    const int card = static_cast<int32_t>(Memory::Read32(tp + Tribe::catchCard));
    if (card > 0 && card < 0x220) {
        const uint32_t nameMsg = Memory::Read32(kItemDataTable + kItemDataSize * static_cast<uint32_t>(card) + 0x10);
        if (nameMsg) name = GuestString(g.Call(Fn::msgSearch, {nameMsg}));
        for (const char* suffix : {" Card", " card"}) {
            const size_t n = std::strlen(suffix);
            if (name.size() > n && name.compare(name.size() - n, n, suffix) == 0) name.resize(name.size() - n);
        }
    }
    if (name.empty()) {
        // Fall back to the tribe's model name: "e_kuribo" -> "Kuribo".
        std::string pose = GuestString(Memory::Read32(tp + Tribe::animPose));
        if (pose.rfind("e_", 0) == 0) pose = pose.substr(2);
        if (!pose.empty()) pose[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(pose[0])));
        name = pose.empty() ? "Enemy" : pose;
    }
    return name;
}

namespace {

// Winged and floating enemies (by their model names: Paragoomba "e_patakuri", Paratroopa "e_patapata", Boo
// "e_teresa", Swoopula "e_basabasa", Lakitu "e_jugem", Ruff Puff "e_kumo", ...).
bool IsFlyer(Guest& g, int tribe) {
    const uint32_t tp = TribePtr(g, tribe);
    if (!tp) return false;
    const std::string pose = GuestString(Memory::Read32(tp + Tribe::animPose));
    for (const char* flyer : {"pata", "teresa", "basa", "jugem", "kumo", "hane", "fly"})
        if (pose.find(flyer) != std::string::npos) return true;
    return false;
}

Enemy MakeEnemy(Guest& g, uint32_t npc) {
    Enemy e;
    e.npc = npc;
    e.part = Memory::Read32(npc + Npc::parts);
    e.tribe = static_cast<int32_t>(Memory::Read32(npc + Npc::tribeId));
    const uint32_t tp = TribePtr(g, e.tribe);
    e.maxHp = std::max(1, static_cast<int>(Memory::Read8(tp + Tribe::maxHp)));
    e.hp = std::clamp(static_cast<int>(static_cast<int32_t>(Memory::Read32(npc + Npc::hp))), 1, e.maxHp);
    e.attack = std::max(1, static_cast<int>(Memory::Read8(tp + Tribe::attack)));
    const int killXp = static_cast<int16_t>(Memory::Read16(tp + Tribe::killXp));
    e.level = EnemyLevel(killXp, e.attack, e.maxHp);
    e.anims = TribeAnims(tp);
    e.name = EnemyName(g, e.tribe);
    e.start = e.pos = ReadVec3(npc + Npc::position);
    return e;
}

void SetPose(Guest& g, const char* name) {
    const uint32_t mario = g.MarioPtr();
    const uint32_t ptr = GameString(name);
    if (!ptr || !Memory::Contains(mario, 0x30)) return;
    s.marioPose = name;
    if (Memory::Read32(mario + Mario::curPose) != ptr) g.Call(Fn::marioChgPose, {ptr});
}

bool HasAnim(const Enemy& e, int id) { return std::find(e.anims.begin(), e.anims.end(), id) != e.anims.end(); }
void SetAnim(Guest& g, Enemy& e, int id) {
    if (!HasAnim(e, id)) id = 0;
    if (e.anim == id || !e.present) return;
    e.anim = id;
    g.Call(Fn::npcSetAnim, {e.npc, static_cast<uint32_t>(id), 1});
}

int AliveCount() {
    return static_cast<int>(std::count_if(s.enemies.begin(), s.enemies.end(), [](const Enemy& e) { return e.alive; }));
}
int NextAlive(int from) {
    for (int i = std::max(from, 0); i < static_cast<int>(s.enemies.size()); ++i)
        if (s.enemies[i].alive) return i;
    return -1;
}
int MarioAttack(Guest& g) {
    const uint32_t pouch = g.PouchPtr();
    return std::max(1, static_cast<int>(static_cast<int32_t>(Memory::Read32(pouch + Pouch::attack))));
}
int MarioHp(Guest& g) { return static_cast<int32_t>(Memory::Read32(g.PouchPtr() + Pouch::hp)); }

void DamageEnemy(Guest& g, Enemy& e, int power) {
    const int dmg = std::max(0, power - e.defense);
    RT_LOGF(RT_TAG_RUNTIME, "[lpbattle] %s takes %d (hp %d -> %d)\n", e.name.c_str(), dmg, e.hp, std::max(0, e.hp - dmg));
    e.hp = std::max(0, e.hp - dmg);
    e.hurt = 18;
    SetAnim(g, e, 4);
    Pop({e.pos.x, e.pos.y + kEnemyHeight + 8.f, e.pos.z}, std::to_string(dmg), ColYellow, 0);
    if (e.hp <= 0) e.alive = false;
    if (e.present) Memory::Write32(e.npc + Npc::hp, static_cast<uint32_t>(std::max(e.hp, 1)));
}

void Begin(Guest& g) {
    const uint32_t mario = g.MarioPtr();
    const uint32_t first = s.pendingNpc;
    s = State{};
    s.active = true;
    const Vec3 mpos = ReadVec3(mario + Mario::position);
    s.groundY = g_lastGroundY;
    s.marioHome = s.marioPos = {mpos.x, s.groundY, mpos.z};

    // The enemy that started it, then up to two more nearby.
    std::vector<uint32_t> picks{first};
    const uint32_t base = g.Call(Fn::npcGetEntries);
    const uint32_t count = g.Call(Fn::npcGetMaxEntries);
    std::vector<std::pair<float, uint32_t>> nearby;
    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t npc = base + i * Npc::size;
        if (npc == first || !Battleable(g, npc)) continue;
        const Vec3 p = ReadVec3(npc + Npc::position);
        const float dx = std::fabs(p.x - mpos.x), dy = std::fabs(p.y - mpos.y);
        if (dx < 200.f && dy < 90.f) nearby.push_back({dx, npc});
    }
    std::sort(nearby.begin(), nearby.end());
    for (const auto& n : nearby) {
        if (picks.size() >= kMaxEnemies) break;
        picks.push_back(n.second);
    }
    for (uint32_t npc : picks) s.enemies.push_back(MakeEnemy(g, npc));
    if (const char* boost = std::getenv("SPM_LP_ENEMY_ATK_BONUS"))  // testing defeat
        for (auto& e : s.enemies) e.attack += std::atoi(boost);
    if (const char* give = std::getenv("SPM_LP_GIVE_ITEMS")) {  // testing items: "0x50,0x41"
        const uint32_t pouch = g.PouchPtr();
        uint32_t slot = 0;
        for (const char* c = give; *c && slot < Pouch::useItemCount;) {
            char* end = nullptr;
            const long id = std::strtol(c, &end, 0);
            if (end == c) break;
            Memory::Write16(pouch + Pouch::useItems + 2 * slot++, static_cast<uint16_t>(id));
            c = *end == ',' ? end + 1 : end;
        }
    }
    s.enemiesAtStart = static_cast<int>(s.enemies.size());
    s.side = s.enemies[0].start.x >= mpos.x ? 1.f : -1.f;
    for (size_t i = 0; i < s.enemies.size(); ++i) {
        auto& e = s.enemies[i];
        // Everyone stands on the stage floor (the map's pipes and ledges are gone), flying enemies hover.
        const float fly = IsFlyer(g, e.tribe) ? std::clamp(e.start.y - s.groundY, 24.f, 60.f) : 0.f;
        e.slot = {s.marioHome.x + s.side * (78.f + 46.f * static_cast<float>(i)), s.groundY + fly, s.marioHome.z};
    }

    g.Call(Fn::marioKeyOff);
    g.Call(Fn::evtStopAll, {0xFFu});  // enemy AI scripts (and the rest of the world) hold still
    Banner("BATTLE!");
    Go(BattlePhase::Intro);
    RT_LOGF(RT_TAG_RUNTIME, "[lpbattle] start: %zu enemies\n", s.enemies.size());
}

void Finish(Guest& g) {
    UnparkFieldObjects();
    g.Call(Fn::evtStartAll, {0xFFu});
    // Back to the real map (the test area keeps its own painted room).
    if (TestAreaActive()) {
        SetStage(g, 3, s.marioHome);
    } else {
        g.Call(Fn::mapDispOn);
        SetStage(g, 0, s.marioHome);
    }
    // Defeated enemies go down the game's own way (death puff, coins, cosmetic score).
    for (auto& e : s.enemies) {
        if (e.alive || !e.present || !(Memory::Read32(e.npc + Npc::flag8) & 1)) continue;
        Memory::Write32(e.npc + Npc::hp, 1);
        g_allowGameDamage = true;
        g.Call(Fn::npcTakeDamage, {0, e.part, 7, 99, 0, 0});
        g_allowGameDamage = false;
    }
    g.Call(Fn::marioKeyOn);
    SetPose(g, "S_1");
    g.Call(Fn::marioSetInvincibility, {0}, {s.fled ? 120.0 : 60.0});
    const bool defeated = s.defeated;
    s.active = false;
    s.phase = BattlePhase::None;
    s.cooldown = 45;
    RT_LOGF(RT_TAG_RUNTIME, "[lpbattle] end (%s)\n", defeated ? "defeat" : (s.fled ? "fled" : "victory"));
    if (defeated) {
        // Let the game run its own game over: one more hit at 0 HP.
        const uint32_t pos = g.Scratch(0x300);
        WriteVec3(pos, {s.marioHome.x + s.side * 20.f, s.groundY, s.marioHome.z});
        g.Call(Fn::pouchSetHp, {1});
        g.Call(Fn::marioTakeDamage, {pos, 0, 99});
    }
}

// --- Mario's actions ------------------------------------------------------------------------------

void AfterMarioAction(Guest& g) {
    SetPose(g, "S_1");
    s.marioPos = s.marioHome;
    if (AliveCount() == 0) {
        Banner("VICTORY!");
        Go(BattlePhase::Victory);
        return;
    }
    s.enemyTurn = NextAlive(0);
    StartStage(0);
    s.lockout = false;
    s.guard = 0;
    Go(BattlePhase::EnemyAction);
}

void StartAction(Action action) {
    s.action = action;
    s.pressed = s.success = s.lockout = false;
    s.gauge = 0.f;
    s.holding = s.released = false;
    StartStage(0);
    Go(BattlePhase::MarioAction);
}

void TickJump(Guest& g, Enemy& e, uint32_t pressed) {
    const Vec3 approach{e.pos.x - s.side * 34.f, s.groundY, s.marioHome.z};
    const Vec3 top{e.pos.x - s.side * 4.f, e.pos.y + kEnemyHeight, s.marioHome.z};
    switch (s.stage) {
    case 0: {  // run up
        const float t = std::min(1.f, s.st / 22.f);
        s.marioPos = LerpV(s.marioHome, approach, Smooth(t));
        SetPose(g, "R_1");
        if (s.st >= 22) StartStage(1);
    } break;
    case 1: {  // jump onto the enemy; press A just before landing
        constexpr int kLen = 32, kWindow = 8;
        const float t = std::min(1.f, s.st / static_cast<float>(kLen));
        s.marioPos = Arc(approach, top, 46.f, t);
        SetPose(g, t < 0.5f ? "J_1B" : "J_1C");
        if ((pressed & (Btn::two | Btn::a)) && !s.pressed) {
            s.pressed = true;
            s.success = s.st >= kLen - kWindow;
        }
        if (s.st >= kLen) {
            DamageEnemy(g, e, MarioAttack(g));
            if (s.success && e.alive) {
                Pop({top.x, top.y + 26.f, top.z}, "Nice!", ColYellow, 1);
                StartStage(2);
            } else {
                StartStage(3);
            }
        }
    } break;
    case 2: {  // the second bounce
        constexpr int kLen = 26;
        const float t = std::min(1.f, s.st / static_cast<float>(kLen));
        s.marioPos = Arc(top, top, 40.f, t);
        SetPose(g, t < 0.5f ? "J_1B" : "J_1C");
        if (s.st >= kLen) {
            DamageEnemy(g, e, MarioAttack(g));
            StartStage(3);
        }
    } break;
    default: {  // hop back home
        constexpr int kLen = 30;
        const float t = std::min(1.f, s.st / static_cast<float>(kLen));
        s.marioPos = Arc(top, s.marioHome, 36.f, t);
        SetPose(g, t < 0.5f ? "J_1B" : "J_1C");
        if (s.st >= kLen) AfterMarioAction(g);
    } break;
    }
}

void TickHammer(Guest& g, Enemy& e, uint32_t held) {
    const Vec3 approach{e.pos.x - s.side * 30.f, s.groundY, s.marioHome.z};
    const Vec3 strike{e.pos.x - s.side * 12.f, s.groundY, s.marioHome.z};
    switch (s.stage) {
    case 0: {
        const float t = std::min(1.f, s.st / 22.f);
        s.marioPos = LerpV(s.marioHome, approach, Smooth(t));
        SetPose(g, "R_1");
        if (s.st >= 22) StartStage(1);
    } break;
    case 1: {  // hold left, release when the fourth star lights
        s.marioPos = approach;
        SetPose(g, "S_1");
        const bool holdingNow = (held & Btn::screenLeft) != 0;
        if (holdingNow) {
            s.holding = true;
            s.gauge += 1.f / 60.f;
        }
        const bool release = s.holding && !holdingNow;
        if (release || s.gauge > 1.2f || (!s.holding && s.st > 120)) {
            s.success = release && s.gauge >= 1.f && s.gauge <= 1.2f;
            StartStage(2);
        }
    } break;
    case 2: {  // the swing: a short hop into the enemy
        constexpr int kLen = 14;
        const float t = std::min(1.f, s.st / static_cast<float>(kLen));
        s.marioPos = Arc(approach, strike, 20.f, t);
        SetPose(g, t < 0.5f ? "J_1B" : "J_1C");
        if (s.st >= kLen) {
            if (s.success) Pop({e.pos.x, e.pos.y + kEnemyHeight + 30.f, e.pos.z}, "Nice!", ColYellow, 1);
            DamageEnemy(g, e, MarioAttack(g) + (s.success ? 1 : 0));
            StartStage(3);
        }
    } break;
    default: {
        const float t = std::min(1.f, s.st / 22.f);
        s.marioPos = LerpV(strike, s.marioHome, Smooth(t));
        SetPose(g, "R_1");
        if (s.st >= 22) AfterMarioAction(g);
    } break;
    }
}

// TTYD-ish damage for SPM's attack items; anything with an HP value heals.
int ItemDamage(int itemId, bool& allTargets) {
    allTargets = true;
    switch (itemId) {
    case 0x41: return 3;  // Fire Burst
    case 0x42: return 3;  // Ice Storm
    case 0x43: return 5;  // Thunder Rage
    case 0x44: return 6;  // Shooting Star
    case 0x45: return 2;  // POW Block
    case 0x46: allTargets = false; return 6;  // Shell Shock
    default: return 0;
    }
}

void TickItem(Guest& g) {
    SetPose(g, "S_1");
    if (s.st == 20 && s.itemToUse >= 0) {
        const uint32_t pouch = g.PouchPtr();
        const uint32_t slot = pouch + Pouch::useItems + 2u * static_cast<uint32_t>(s.itemToUse);
        const int itemId = Memory::Read16(slot);
        const uint32_t data = kItemDataTable + kItemDataSize * static_cast<uint32_t>(itemId);
        const int heal = static_cast<int16_t>(Memory::Read16(data + 0x1C));
        bool all = true;
        const int dmg = ItemDamage(itemId, all);
        if (dmg > 0) {
            for (auto& e : s.enemies) {
                if (!e.alive) continue;
                DamageEnemy(g, e, dmg);
                if (!all) break;
            }
        } else if (heal > 0) {
            const int maxHp = static_cast<int32_t>(Memory::Read32(pouch + Pouch::maxHp));
            const int hp = std::min(maxHp, MarioHp(g) + heal);
            g.Call(Fn::pouchSetHp, {static_cast<uint32_t>(hp)});
            Pop({s.marioPos.x, s.marioPos.y + 50.f, s.marioPos.z}, "+" + std::to_string(heal) + " HP", ColGreen, 2);
        } else {
            Pop({s.marioPos.x, s.marioPos.y + 50.f, s.marioPos.z}, "No effect...", ColWhite, 1);
        }
        // Use it up: the pouch keeps its items packed at the front.
        for (uint32_t i = static_cast<uint32_t>(s.itemToUse); i + 1 < Pouch::useItemCount; ++i)
            Memory::Write16(pouch + Pouch::useItems + 2 * i, Memory::Read16(pouch + Pouch::useItems + 2 * (i + 1)));
        Memory::Write16(pouch + Pouch::useItems + 2 * (Pouch::useItemCount - 1), 0);
    }
    if (s.st >= 50) AfterMarioAction(g);
}

void TickRun(Guest& g, uint32_t pressed) {
    SetPose(g, "R_1");
    if (pressed & (Btn::two | Btn::a)) s.gauge += 0.085f;
    s.gauge = std::max(0.f, s.gauge - 0.004f);
    if (s.gauge >= 1.f) {
        s.fled = true;
        Banner("Got away!");
        Go(BattlePhase::Fled);
        return;
    }
    if (s.st >= 150) {
        Pop({s.marioPos.x, s.marioPos.y + 50.f, s.marioPos.z}, "Couldn't escape!", ColWhite, 1);
        AfterMarioAction(g);
    }
}

// --- the enemies' turn ----------------------------------------------------------------------------

void TickEnemy(Guest& g, uint32_t pressed) {
    if (s.enemyTurn < 0 || s.enemyTurn >= static_cast<int>(s.enemies.size())) {
        s.defending = false;
        Go(BattlePhase::Menu);
        return;
    }
    Enemy& e = s.enemies[s.enemyTurn];
    const Vec3 strikeFrom{s.marioHome.x + s.side * 34.f, s.groundY + (e.slot.y - s.groundY) * 0.5f, s.marioHome.z};
    const Vec3 contact{s.marioHome.x + s.side * 12.f, s.groundY + 6.f, s.marioHome.z};
    constexpr int kLunge = 12, kGuard = 8, kSuper = 3;
    switch (s.stage) {
    case 0: {
        const float t = std::min(1.f, s.st / 26.f);
        e.pos = LerpV(e.slot, strikeFrom, Smooth(t));
        SetAnim(g, e, 1);
        if (s.st >= 26) StartStage(1);
    } break;
    case 1: {  // wind-up
        e.pos = strikeFrom;
        SetAnim(g, e, HasAnim(e, 25) ? 25 : 0);
        if ((pressed & (Btn::two | Btn::a | Btn::one | Btn::b)) && !s.guard) s.lockout = true;
        if (s.st >= 16) StartStage(2);
    } break;
    case 2: {  // lunge: A within the last 8 frames guards, B within the last 3 superguards
        const float t = std::min(1.f, s.st / static_cast<float>(kLunge));
        e.pos = Arc(strikeFrom, contact, 14.f, t);
        if (!s.lockout && !s.guard) {
            const int left = kLunge - s.st;
            if ((pressed & (Btn::one | Btn::b)) && left <= kSuper) s.guard = 2;
            else if ((pressed & (Btn::two | Btn::a)) && left <= kGuard) s.guard = 1;
            else if (pressed & (Btn::two | Btn::a | Btn::one | Btn::b)) s.lockout = true;
        }
        if (s.st >= kLunge) {
            const Vec3 above{s.marioHome.x, s.groundY + 52.f, s.marioHome.z};
            const Vec3 word{above.x, above.y + 40.f, above.z};  // clear of the damage starburst
            if (s.guard == 2) {
                Pop(word, "Superguard!", ColBlue, 1);
                DamageEnemy(g, e, 1);
            } else {
                int dmg = e.attack - (s.defending ? 1 : 0) - (s.guard == 1 ? 1 : 0);
                dmg = std::max(0, dmg);
                if (s.guard == 1) Pop(word, "Guard!", ColBlue, 1);
                Pop(above, std::to_string(dmg), ColRed, 0);
                RT_LOGF(RT_TAG_RUNTIME, "[lpbattle] Mario takes %d (guard %d)\n", dmg, s.guard);
                if (dmg > 0) {
                    s.marioHurt = 24;
                    g.Call(Fn::pouchSetHp, {static_cast<uint32_t>(std::max(0, MarioHp(g) - dmg))});
                }
            }
            StartStage(3);
        }
    } break;
    default: {
        const float t = std::min(1.f, s.st / 24.f);
        e.pos = Arc(contact, e.slot, 18.f, t);
        if (s.st >= 24) {
            SetAnim(g, e, 0);
            if (MarioHp(g) <= 0) {
                s.defeated = true;
                Banner("Mario was defeated...");
                Go(BattlePhase::Defeat);
                return;
            }
            if (AliveCount() == 0) {
                Banner("VICTORY!");
                Go(BattlePhase::Victory);
                return;
            }
            s.enemyTurn = (s.first == FirstStrike::Enemy) ? -1 : NextAlive(s.enemyTurn + 1);
            s.first = FirstStrike::None;
            s.lockout = false;
            s.guard = 0;
            StartStage(0);
        }
    } break;
    }
}

void OpenMenu() {
    s.defending = false;
    s.target = std::max(0, NextAlive(0));
    Go(BattlePhase::Menu);
}

// --- per frame ------------------------------------------------------------------------------------

// Test autopilot (SPM_LP_AUTOPLAY=J,H,I,D,R: one command per Mario turn, cycling): picks commands and nails
// every action command, guarding and superguarding alternately. Adds synthetic pressed/held buttons.
void Autoplay(uint32_t& pressed, uint32_t& held) {
    static const std::string plan = [] {
        const char* v = std::getenv("SPM_LP_AUTOPLAY");
        std::string p;
        for (const char* c = v ? v : ""; *c; ++c)
            if (*c != ',') p += *c;
        return p;
    }();
    if (plan.empty()) return;
    static int turn = 0, enemyTurns = 0, lastEnemyStage = -1;
    const char cmd = plan[turn % plan.size()];
    switch (s.phase) {
    case BattlePhase::Menu:
        if (s.t == 20) {
            s.menu = cmd == 'J' ? 0 : cmd == 'H' ? 1 : cmd == 'I' ? 2 : 3;
            pressed |= Btn::two;
            ++turn;
        }
        break;
    case BattlePhase::Target:
    case BattlePhase::ItemMenu:
        if (s.t == 15) pressed |= Btn::two;
        break;
    case BattlePhase::TacticsMenu:
        if (s.t == 10) s.listIndex = plan[(turn + plan.size() - 1) % plan.size()] == 'R' ? 1 : 0;
        if (s.t == 15) pressed |= Btn::two;
        break;
    case BattlePhase::MarioAction:
        if (s.action == Action::Jump && s.stage == 1 && s.st == 28) pressed |= Btn::two;
        if (s.action == Action::Hammer && s.stage == 1 && s.gauge < 1.05f) held |= Btn::screenLeft;
        if (s.action == Action::Run && s.st % 3 == 0) pressed |= Btn::two;
        break;
    case BattlePhase::EnemyAction:
        if (s.stage == 2 && lastEnemyStage != 2) ++enemyTurns;
        lastEnemyStage = s.stage;
        if (s.stage == 2) {
            const bool super = enemyTurns % 2 == 0;
            if (s.st == (super ? 10 : 7)) pressed |= super ? Btn::one : Btn::two;
        }
        break;
    case BattlePhase::LevelUp:
        if (s.t == 30) pressed |= Btn::two;
        break;
    default: break;
    }
}

void Tick(Guest& g) {
    uint32_t pressed = g.Call(Fn::wpadGetButtonsPressed, {0});
    uint32_t held = g.Call(Fn::wpadGetButtonsHeld, {0});
    Autoplay(pressed, held);
    const bool confirm = pressed & (Btn::two | Btn::a);
    const bool cancel = pressed & (Btn::one | Btn::b);
    ++s.t;
    ++s.st;
    ++s.bannerT;

    for (auto& e : s.enemies) {
        e.present = e.present && (Memory::Read32(e.npc + Npc::flag8) & 1);
        if (!e.present) e.alive = false;
        if (e.hurt > 0 && --e.hurt == 0 && e.alive) SetAnim(g, e, 0);
    }

    switch (s.phase) {
    case BattlePhase::Intro: {
        const float t = std::min(1.f, s.t / 30.f);
        for (auto& e : s.enemies) e.pos = LerpV(e.start, e.slot, Smooth(t));
        SetPose(g, "S_1");
        if (s.t >= 40) {
            if (s.first == FirstStrike::Mario && !s.enemies.empty()) {
                Pop({s.enemies[0].pos.x, s.enemies[0].pos.y + kEnemyHeight + 30.f, s.enemies[0].pos.z}, "First Strike!",
                    ColYellow, 1);
                DamageEnemy(g, s.enemies[0], MarioAttack(g));
                if (AliveCount() == 0) {
                    Banner("VICTORY!");
                    Go(BattlePhase::Victory);
                } else {
                    OpenMenu();
                }
            } else if (s.first == FirstStrike::Enemy) {
                s.enemyTurn = 0;
                StartStage(0);
                Go(BattlePhase::EnemyAction);
            } else {
                OpenMenu();
            }
        }
    } break;
    case BattlePhase::Menu: {
        SetPose(g, "S_1");
        if (pressed & Btn::screenUp) s.menu = (s.menu + 3) % 4;
        if (pressed & Btn::screenDown) s.menu = (s.menu + 1) % 4;
        if (confirm && s.t > 4) {
            if (s.menu <= 1) {
                s.target = std::max(0, NextAlive(0));
                Go(BattlePhase::Target);
            } else if (s.menu == 2) {
                s.itemSlots.clear();
                const uint32_t pouch = g.PouchPtr();
                for (uint32_t i = 0; i < Pouch::useItemCount; ++i)
                    if (Memory::Read16(pouch + Pouch::useItems + 2 * i) != 0) s.itemSlots.push_back(static_cast<int>(i));
                s.listIndex = 0;
                if (s.itemSlots.empty()) Pop({s.marioPos.x, s.marioPos.y + 50.f, s.marioPos.z}, "No items!", ColWhite, 1);
                else Go(BattlePhase::ItemMenu);
            } else {
                s.listIndex = 0;
                Go(BattlePhase::TacticsMenu);
            }
        }
    } break;
    case BattlePhase::Target: {
        if (pressed & (Btn::screenRight | Btn::screenLeft)) {
            const int dir = ((pressed & Btn::screenRight) ? 1 : -1) * static_cast<int>(s.side);
            const int n = static_cast<int>(s.enemies.size());
            for (int step = 1; step <= n; ++step) {
                const int i = ((s.target + dir * step) % n + n) % n;
                if (s.enemies[i].alive) {
                    s.target = i;
                    break;
                }
            }
        }
        if (cancel) Go(BattlePhase::Menu);
        else if (confirm && s.t > 4) StartAction(s.menu == 0 ? Action::Jump : Action::Hammer);
    } break;
    case BattlePhase::ItemMenu: {
        const int n = static_cast<int>(s.itemSlots.size());
        if (pressed & Btn::screenUp) s.listIndex = (s.listIndex + n - 1) % n;
        if (pressed & Btn::screenDown) s.listIndex = (s.listIndex + 1) % n;
        if (cancel) Go(BattlePhase::Menu);
        else if (confirm && s.t > 4) {
            s.itemToUse = s.itemSlots[s.listIndex];
            const int itemId = Memory::Read16(g.PouchPtr() + Pouch::useItems + 2u * static_cast<uint32_t>(s.itemToUse));
            const uint32_t nameMsg = Memory::Read32(kItemDataTable + kItemDataSize * static_cast<uint32_t>(itemId) + 0x10);
            Banner(nameMsg ? GuestString(g.Call(Fn::msgSearch, {nameMsg})) : "Item");
            StartAction(Action::Item);
        }
    } break;
    case BattlePhase::TacticsMenu: {
        if (pressed & (Btn::screenUp | Btn::screenDown)) s.listIndex ^= 1;
        if (cancel) Go(BattlePhase::Menu);
        else if (confirm && s.t > 4) {
            if (s.listIndex == 0) {
                // +1 defense through the enemies' turn (OpenMenu clears it).
                s.defending = true;
                Pop({s.marioPos.x, s.marioPos.y + 50.f, s.marioPos.z}, "DEF +1", ColBlue, 1);
                AfterMarioAction(g);
            } else {
                StartAction(Action::Run);
            }
        }
    } break;
    case BattlePhase::MarioAction: {
        Enemy* e = (s.target >= 0 && s.target < static_cast<int>(s.enemies.size())) ? &s.enemies[s.target] : nullptr;
        switch (s.action) {
        case Action::Jump: if (e) TickJump(g, *e, pressed); else AfterMarioAction(g); break;
        case Action::Hammer: if (e) TickHammer(g, *e, held); else AfterMarioAction(g); break;
        case Action::Item: TickItem(g); break;
        case Action::Run: TickRun(g, pressed); break;
        default: AfterMarioAction(g); break;
        }
    } break;
    case BattlePhase::EnemyAction: {
        if (s.marioHurt <= 0) SetPose(g, "S_1");
        TickEnemy(g, pressed);
    } break;
    case BattlePhase::Victory: {
        SetPose(g, "S_1");
        if (s.t == 45) {
            Progress p = LoadProgress(g);
            int sp = 0;
            for (const auto& e : s.enemies)
                if (!e.alive) sp += StarPointsFor(e.level, p.level, s.enemiesAtStart);
            static const int bonus = [] {
                const char* v = std::getenv("SPM_LP_SP_BONUS");  // testing level-ups
                return v ? std::atoi(v) : 0;
            }();
            sp = std::clamp(sp, 1, 100) + bonus;
            s.starPointsGained = sp;
            p.starPoints += sp;
            while (p.starPoints >= kStarPointsPerLevel && p.level < kMaxLevel) {
                p.starPoints -= kStarPointsPerLevel;
                ++p.level;
                ++s.levelUps;
            }
            if (p.level >= kMaxLevel) p.starPoints = 0;
            SaveProgress(g, p);
            Pop({s.marioPos.x, s.marioPos.y + 60.f, s.marioPos.z}, "+" + std::to_string(sp) + " Star Points", ColYellow, 1);
        }
        if (s.t >= 150 || (s.t > 60 && confirm)) {
            if (s.levelUps > 0) {
                Banner("LEVEL UP!");
                s.levelChoice = 0;
                Go(BattlePhase::LevelUp);
            } else {
                Finish(g);
            }
        }
    } break;
    case BattlePhase::LevelUp: {
        if (pressed & (Btn::screenUp | Btn::screenDown | Btn::screenLeft | Btn::screenRight)) s.levelChoice ^= 1;
        if (confirm && s.t > 20) {
            ApplyLevelUp(g, s.levelChoice == 0 ? LevelUpChoice::Hp : LevelUpChoice::Attack);
            Pop({s.marioPos.x, s.marioPos.y + 60.f, s.marioPos.z}, s.levelChoice == 0 ? "HP +5" : "ATK +1", ColGreen, 1);
            if (--s.levelUps > 0) Go(BattlePhase::LevelUp);
            else Finish(g);
        }
    } break;
    case BattlePhase::Fled: {
        const float t = std::min(1.f, s.t / 36.f);
        s.marioPos = {s.marioHome.x - s.side * 90.f * t, s.groundY, s.marioHome.z};
        SetPose(g, "R_1");
        if (s.t >= 36) Finish(g);
    } break;
    case BattlePhase::Defeat: {
        SetPose(g, "D_2");
        if (s.t >= 100) Finish(g);
    } break;
    default: break;
    }
    if (!s.active) return;

    if (s.marioHurt > 0) {
        --s.marioHurt;
        SetPose(g, "D_2");
    }
    // The stage: the map hides and the area's pop-up diorama stands up behind the fighters.
    if (++s.stageFrame == 1 && !TestAreaActive()) g.Call(Fn::mapDispOff);
    SetStage(g, 2, s.marioHome);
    ParkFieldObjects(g);
    // Hold everyone where the battle wants them.
    const uint32_t mario = g.MarioPtr();
    WriteVec3(mario + Mario::position, s.marioPos);
    for (auto& e : s.enemies)
        if (e.present) WriteVec3(e.npc + Npc::position, e.pos);
    for (auto& p : s.popups) {
        ++p.age;
        p.world.y += p.kind == 0 ? 0.6f : 0.3f;
    }
    s.popups.erase(std::remove_if(s.popups.begin(), s.popups.end(), [](const Popup& p) { return p.age > 70; }),
                   s.popups.end());
}

void Publish(Guest& g) {
    static uint32_t serial = 0;
    BattleUi ui;
    ui.serial = ++serial;
    ui.active = s.active;
    ui.phase = s.phase;
    ui.phaseFrame = s.t;
    const uint32_t pouch = g.PouchPtr();
    // Only in a map (not the title, file select or map changes).
    if (Memory::Contains(pouch, 0x80) && g.Call(Fn::seqGetSeq) == 2) {
        ui.hp = static_cast<int32_t>(Memory::Read32(pouch + Pouch::hp));
        ui.maxHp = static_cast<int32_t>(Memory::Read32(pouch + Pouch::maxHp));
        ui.attack = static_cast<int32_t>(Memory::Read32(pouch + Pouch::attack));
        const Progress p = LoadProgress(g);
        ui.level = p.level;
        ui.starPoints = p.starPoints;
    }
    if (s.active) {
        g.ScreenPos(s.marioPos, ui.mario.x, ui.mario.y);
        ui.mario.alive = true;
        for (const auto& e : s.enemies) {
            BattleUi::Actor a;
            g.ScreenPos(e.pos, a.x, a.y);
            a.hp = e.hp;
            a.maxHp = e.maxHp;
            a.alive = e.alive;
            a.name = e.name;
            ui.enemies.push_back(a);
        }
        ui.menuIndex = s.menu;
        ui.targetIndex = s.phase == BattlePhase::Target ? s.target : -1;
        if (s.phase == BattlePhase::ItemMenu) {
            for (int slot : s.itemSlots) {
                const int itemId = Memory::Read16(pouch + Pouch::useItems + 2u * static_cast<uint32_t>(slot));
                const uint32_t nameMsg = Memory::Read32(kItemDataTable + kItemDataSize * static_cast<uint32_t>(itemId) + 0x10);
                ui.list.push_back(nameMsg ? GuestString(g.Call(Fn::msgSearch, {nameMsg})) : "???");
            }
            ui.listIndex = s.listIndex;
        } else if (s.phase == BattlePhase::TacticsMenu) {
            ui.list = {"Defend", "Run Away"};
            ui.listIndex = s.listIndex;
        }
        if (s.phase == BattlePhase::MarioAction) {
            if (s.action == Action::Jump && (s.stage == 1)) {
                ui.prompt = UiPrompt::PressA;
                ui.promptLit = s.st >= 24;
            } else if (s.action == Action::Hammer && s.stage == 1) {
                ui.prompt = UiPrompt::HoldLeft;
                ui.gauge = std::min(s.gauge, 1.2f);
                ui.promptLit = s.gauge >= 1.f;
            } else if (s.action == Action::Run) {
                ui.prompt = UiPrompt::Mash;
                ui.gauge = std::min(s.gauge, 1.f);
            }
        } else if (s.phase == BattlePhase::EnemyAction && s.stage <= 2) {
            ui.prompt = UiPrompt::Guard;
            ui.promptLit = s.stage == 2;
        }
        for (const auto& p : s.popups) {
            BattleUi::Popup up;
            g.ScreenPos(p.world, up.x, up.y);
            up.text = p.text;
            up.color = p.color;
            up.kind = p.kind;
            up.age = p.age;
            ui.popups.push_back(up);
        }
        ui.banner = s.banner;
        ui.bannerAge = s.bannerT;
        ui.starPointsGained = s.starPointsGained;
        ui.levelUpChoice = s.levelChoice;
    }
    std::lock_guard lock{g_uiMutex};
    g_ui = std::move(ui);
}

}  // namespace

BattleUi BattleUiSnapshot() {
    std::lock_guard lock{g_uiMutex};
    return g_ui;
}

}  // namespace LostPages

using namespace LostPages;

// An enemy touching Mario starts a battle (enemy first strike) instead of hurting him.
extern "C" void npcDamageMario_LostPages(CpuContext* ctx) {
    Guest g(ctx);
    const uint32_t npc = ctx->gpr[3];
    if (Recon())
        RT_LOGF(RT_TAG_RUNTIME, "[lprecon] npcDamageMario npc %08X damage %d\n", npc, static_cast<int32_t>(ctx->gpr[7]));
    if (Enabled() && (s.active || s.pendingNpc || s.cooldown > 0)) {
        ctx->gpr[3] = 0;
        return;
    }
    if (CanStartBattle(g) && Battleable(g, npc)) {
        s.pendingNpc = npc;
        s.pendingFirst = FirstStrike::Enemy;
        ctx->gpr[3] = 0;
        return;
    }
    func_801CA9EC(ctx);
}
REGISTER_NATIVE_FUNCTION_AS(0x801CA9EC, npcDamageMario_LostPages, "npcDamageMario_LostPages");

// Mario stomping or hitting an enemy starts a battle with his first strike.
extern "C" void npcTakeDamage_LostPages(CpuContext* ctx) {
    if (!Enabled() || g_allowGameDamage) {
        func_801C5568(ctx);
        return;
    }
    Guest g(ctx);
    const uint32_t attacker = ctx->gpr[3];
    const uint32_t part = ctx->gpr[4];
    const uint32_t victim = Memory::Contains(part + 0x388, 4) ? Memory::Read32(part + 0x388) : 0;
    const bool byMario = !IsNpcEntry(g, attacker);
    if (Recon())
        RT_LOGF(RT_TAG_RUNTIME, "[lprecon] npcTakeDamage attacker %08X victim %08X defType %d power %d mario %d\n", attacker,
                victim, static_cast<int32_t>(ctx->gpr[5]), static_cast<int32_t>(ctx->gpr[6]), byMario ? 1 : 0);
    if (s.active || s.pendingNpc || s.cooldown > 0) {
        if (byMario || s.active) {
            ctx->gpr[3] = 0;
            return;
        }
    } else if (byMario && CanStartBattle(g) && Battleable(g, victim)) {
        s.pendingNpc = victim;
        s.pendingFirst = FirstStrike::Mario;
        ctx->gpr[3] = 0;
        return;
    }
    func_801C5568(ctx);
}
REGISTER_NATIVE_FUNCTION_AS(0x801C5568, npcTakeDamage_LostPages, "npcTakeDamage_LostPages");

// The battle runs right after the NPCs update, so what it sets is what gets drawn this frame.
extern "C" void npcMain_LostPages(CpuContext* ctx) {
    func_801A8AD8(ctx);
    if (!Enabled()) return;
    Guest g(ctx);
    const uint32_t mario = g.MarioPtr();
    if (Memory::Contains(mario, 0x130) && !s.active) {
        const uint32_t motion = Memory::Read16(mario + Mario::motionId);
        if (motion <= 2) g_lastGroundY = Memory::ReadFloat32(mario + Mario::position + 4);  // stay, walk, dash
    }
    if (s.cooldown > 0 && !s.active) --s.cooldown;
    if (!s.active) TestAreaFieldTick(g);
    if (s.pendingNpc && !s.active) {
        const FirstStrike first = s.pendingFirst;
        Begin(g);
        s.first = first;
        RT_LOGF(RT_TAG_RUNTIME, "[lpbattle] first strike: %s\n", first == FirstStrike::Mario ? "Mario" : "enemy");
        s.pendingNpc = 0;
    }
    if (s.active) Tick(g);
    Publish(g);
}
REGISTER_NATIVE_FUNCTION_AS(0x801A8AD8, npcMain_LostPages, "npcMain_LostPages");

// On the battle stage only the fighters are drawn: the area's other NPCs aren't part of the diorama.
// npcDisp(cameraId, NPCEntry*) draws one NPC.
extern "C" void func_801B4058(CpuContext* ctx);  // npcDisp
extern "C" void npcDisp_LostPages(CpuContext* ctx) {
    if (s.active) {
        const uint32_t npc = ctx->gpr[4];
        bool fighter = false;
        for (const auto& e : s.enemies) fighter = fighter || (e.present && e.npc == npc);
        if (!fighter) return;
    }
    func_801B4058(ctx);
}
REGISTER_NATIVE_FUNCTION_AS(0x801B4058, npcDisp_LostPages, "npcDisp_LostPages");
