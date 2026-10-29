// Lost Pages battle engine: runs on the game thread right after npcMain each frame, moves Mario and the
// enemies itself (their scripts are paused), and publishes a BattleUi snapshot for the overlay.

#include "lp_battle.h"
#include "lp_guest.h"
#include "lp_progress.h"
#include "lp_testarea.h"
#include "lp_badge_system.h"
#include "lp_combat_rules.h"
#include "lp_badge_pickups.h"
#include "lp_fracktail.h"
#include "lp_final_boss_visual.h"
#include "lp_native_battle_ui.h"
#include "lp_bleck_rules.h"
#include "lp_dimentio_rules.h"
#include "lp_ochunks_rules.h"
#include "lp_scene_poses.h"
#include <atomic>

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
enum class Action { Jump, Hammer, Item, Run, Defend, Charge, Appeal, Recharge, Pulse, Swap, Tattle, Thoreau, PureShield, LoversGrief };
enum Status { Sleep, Freeze, Shrink, Soft, Confuse, Burn, Poison, Circuit, StatusCount };
enum class Tactic { Defend, Run, Charge, DoubleDip, Recharge, Appeal, Pulse, Swap, Tattle, Thoreau };
struct StageEffect {Vec3 from,to;int type=0,age=0,life=30;uint32_t color=ColWhite;};
struct BattlePart {uint32_t ptr;Vec3 pos,ownerPos,rotation,scale;uint32_t color;};

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
    bool flying=false,spiky=false,fiery=false,ranged=false,boss=false;
    int blockPose=0,barrier=0;  // frames left of the block squash / the final boss's barrier flash
    std::array<int,StatusCount> status{};
    uint32_t savedFlags=0,savedColor=0,savedCamera=0,savedOffscreen=0;
    Vec3 savedRotation{},savedScale{1,1,1};
    std::vector<BattlePart> parts;
    float height = kEnemyHeight;  // where Mario lands on it
    std::string namedAnim;  // an animation set by name on its own pose (story bosses)
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
    bool fracktail=false,storyBoss=false;
    // O'Chunks (1-3): his native NPC fights; its animations are set by name on its own pose.
    bool ochunks=false;
    int finalBoss=0, phase1Turns=0, finalAttack=0, finalTurn=0, buffsAtk=0, buffsDef=0;
    int slowTurns=0,debuffTurns=0,splitDodges=0,splitPenalty=0;
    bool phase2=false,pureShield=false,introTalkDone=false,defeatTalkDone=false;
    // Story final bosses hand the barrier break to the game's own cutscene, then the battle resumes.
    bool suspendForScene=false,opening=false;
    int bleckStage=1,barrierHits=0;  // Bleck's phase (1 protected, 2 barrier up after the Pure Hearts, 3 hurtable)
    int shieldPart=-1;  // Bleck's Chaos Heart barrier (a part of his actor), shown only when it blocks a hit
    float whiteFlash=0;
    std::string pendingTalk;
    BattlePhase afterTalk=BattlePhase::Menu;
    OChunksRules oRules;
    OChunksAttack oAttack=OChunksAttack::ShoulderBash;
    std::string oAnim;
    FracktailRules bossRules;
    FracktailAttack bossAttack=FracktailAttack::Charge;
    Vec3 bossRoot{},throwFrom{},throwPos{};
    int glitchFrames=0,throwIndex=-1;
    uint32_t originalBoss=0;
    std::string musicBefore;

    float side = 1.f;  // enemies on Mario's right (+1) or left (-1)
    float groundY = 0.f;
    Vec3 marioHome, marioPos;
    Vec3 leapFrom;
    std::vector<Enemy> enemies;
    int enemiesAtStart = 0;

    int menu = 0, target = 0, listIndex = 0;
    std::vector<int> itemSlots;  // pouch use-item indices shown in the item menu

    Action action = Action::Jump;
    int stage = 0, st = 0;  // action stage, frames in stage
    int jumpHits = 0, jumpLimit = 2;
    int windup = 0;  // frames of Mario's angry wind-up before an attack
    BadgeId move=BadgeId::Count;
    std::vector<BadgeId> moves;
    std::vector<Tactic> tactics;
    int charge=0,spentCharge=0,round=1,guardPose=0,itemChain=0,itemUses=0;
    bool actionHit=false,stylish=false,actionQuality=false,reveal=false;
    std::array<int,StatusCount> status{};
    std::vector<StageEffect> effects;
    uint32_t rng=0x4C505442u,marioFlags=0;
    Vec3 marioScale{1,1,1},marioRotation{};
    std::array<float,4> marioFacing{};
    Vec3 facingTarget{};
    int electric=0,boonDefense=0;
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
struct BossCameraOverride {
    uint32_t camera=0;
    std::array<uint32_t,7> packedProjection{};
    std::array<uint32_t,16> projection{};
    std::array<uint32_t,12> view{};
} bossCamera;
void RestoreBossCamera() {
    if(!bossCamera.camera)return;
    const auto camera=bossCamera.camera;
    if(Memory::Contains(camera,0xD4)) {
        for(unsigned i=0;i<bossCamera.packedProjection.size();++i)Memory::Write32(camera+0x38+i*4,bossCamera.packedProjection[i]);
        for(unsigned i=0;i<bossCamera.projection.size();++i)Memory::Write32(camera+0x94+i*4,bossCamera.projection[i]);
        for(unsigned i=0;i<bossCamera.view.size();++i)Memory::Write32(camera+0x54+i*4,bossCamera.view[i]);
    }
    bossCamera.camera=0;
}
std::atomic<bool> fracktailRequested{false};

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

// The Pure Hearts' Special menu: in a final battle once they're back (Bleck's phase 2 on, Super Dimentio's 415 fight).
bool SpecialOpen() {return s.finalBoss && (s.phase2 || s.bleckStage>=2);}
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
int Roll() {s.rng=s.rng*1664525u+1013904223u;return static_cast<int>((s.rng>>8)%100);}
int Has(Guest& g,BadgeId id) {return BadgeCount(g,id);}
void Effect(const Vec3& a,const Vec3& b,int type,uint32_t color,int life=30) {s.effects.push_back({a,b,type,0,life,color});}
void Sound(Guest& g,const char* name) {if(const auto ptr=GameString(name))g.Call(0x80238AF4,{ptr});}  // spsndSFXOn
// Mario's voice on an attack: one of the game's four attack shouts.
void Shout(Guest& g) {static const char* shouts[]={"SFX_P_V_MARIO_ATTACK1","SFX_P_V_MARIO_ATTACK2","SFX_P_V_MARIO_ATTACK3","SFX_P_V_MARIO_ATTACK4"};Sound(g,shouts[(s.rng>>9)%4]);}

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
bool IsOChunks(uint32_t npc) {
    // The 1-3 boss: the map spawns "dodon" on a placeholder tribe and gives it the e_ddtas model.
    return GuestString(npc + Npc::name, 32) == "dodon" && GuestString(npc + 0x48, 32) == "e_ddtas";
}

bool Battleable(Guest& g, uint32_t npc) {
    if (!Memory::Contains(npc, Npc::size) || !(Memory::Read32(npc + Npc::flag8) & 1)) return false;
    // The final room's actors (Super Dimentio's first form among them) only fight as story bosses.
    if (GuestString(Memory::Read32(0x8056D198) + 0x44, 32) == DimentioRules::Map) return false;
    if (IsOChunks(npc)) return false;  // his fight starts on its own, as a boss battle
    const int tribe = static_cast<int32_t>(Memory::Read32(npc + Npc::tribeId));
    const uint32_t tp = TribePtr(g, tribe);
    if (!tp) return false;
    const int maxHp = Memory::Read8(tp + Tribe::maxHp);
    const int hp = static_cast<int32_t>(Memory::Read32(npc + Npc::hp));
    return maxHp >= 1 && maxHp <= 40 && hp > 0 && Memory::Read32(npc + Npc::parts) != 0;
}

int MarioHp(Guest& g) { return static_cast<int32_t>(Memory::Read32(g.PouchPtr() + Pouch::hp)); }

bool CanStartBattle(Guest& g) {
    if (!Enabled() || s.active || s.pendingNpc || s.cooldown > 0) return false;
    if (g.Call(Fn::seqGetSeq) != 2) return false;  // in a map, not a menu or map change
    const uint32_t mario = g.MarioPtr();
    // Not in a cutscene, and not while a defeat is still turning into the game over.
    return Memory::Contains(mario, 0x130) && Memory::Read8(mario + Mario::keyOff) == 0 && MarioHp(g) > 0;
}

// The native final-fight actors use generated names, so they are identified by tribe and by the
// defeat-callback unitwork installed when their script hands control to the battle.
bool FinalBossNpc(Guest& g, int kind, uint32_t& npc) {
    const int tribe=kind==1?BleckRules::Tribe:DimentioRules::Tribe;
    const uint32_t script=kind==1?BleckRules::NativeDefeatScript:DimentioRules::NativeDefeatScript;
    const uint32_t unitwork=0x510u+(kind==1?BleckRules::NativeDefeatUnitwork:DimentioRules::NativeDefeatUnitwork)*4u;
    const uint32_t base=g.Call(Fn::npcGetEntries),count=g.Call(Fn::npcGetMaxEntries);
    if(!base || count>1024 || !Memory::Contains(base,count*Npc::size))return false;
    for(uint32_t i=0;i<count;++i) {
        const uint32_t entry=base+i*Npc::size;
        if(!(Memory::Read32(entry+Npc::flag8)&1))continue;
        if(static_cast<int32_t>(Memory::Read32(entry+Npc::tribeId))!=tribe)continue;
        if(Memory::Contains(entry+unitwork,4) && Memory::Read32(entry+unitwork)==script) {
            npc=entry;return true;
        }
    }
    return false;
}

// Super Dimentio's first form, at the end of Bleck's defeat scene: a live actor of his own tribe (named for him by its
// Catch Card), with a hit model, before the 415 fight's defeat callback is installed.
bool FinalBossOpeningNpc(Guest& g, uint32_t& npc) {
    const uint32_t base=g.Call(Fn::npcGetEntries),count=g.Call(Fn::npcGetMaxEntries);
    if(!base || count>1024 || !Memory::Contains(base,count*Npc::size))return false;
    static int loggedTribe=-1;
    for(uint32_t i=0;i<count;++i) {
        const uint32_t entry=base+i*Npc::size;
        if(!(Memory::Read32(entry+Npc::flag8)&1) || !Memory::Read32(entry+Npc::parts))continue;
        if(Memory::Read32(entry+0x510+DimentioRules::NativeDefeatUnitwork*4)==DimentioRules::NativeDefeatScript)continue;
        const int tribe=static_cast<int32_t>(Memory::Read32(entry+Npc::tribeId));
        if(tribe!=DimentioRules::Tribe && EnemyName(g,tribe).find("Super Dimentio")==std::string::npos)continue;
        if(tribe!=loggedTribe){loggedTribe=tribe;RT_LOGF(RT_TAG_RUNTIME,"[lpfinal] Super Dimentio's first form: tribe %d npc %08X\n",tribe,entry);}
        npc=entry;return true;
    }
    return false;
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
    e.flying=IsFlyer(g,e.tribe);
    const auto model=GuestString(Memory::Read32(tp));
    e.spiky=e.name.find("Spik")!=std::string::npos || e.name.find("Spiny")!=std::string::npos || e.name.find("Cleft")!=std::string::npos || e.name.find("Bristle")!=std::string::npos || model.find("toge")!=std::string::npos;
    e.fiery=e.name.find("Lava")!=std::string::npos || e.name.find("Ember")!=std::string::npos || model.find("hidama")!=std::string::npos;
    e.ranged=e.name.find("Squig")!=std::string::npos || e.name.find("Boom")!=std::string::npos || e.name.find("Magiblot")!=std::string::npos || e.name.find("Puff")!=std::string::npos || model.find("octa")!=std::string::npos;
    e.savedFlags=Memory::Read32(npc+Npc::flag8);e.savedColor=Memory::Read32(npc+0xFC);
    e.savedCamera=Memory::Read32(npc+0x298);e.savedOffscreen=Memory::Read32(npc+0x29C);
    e.savedRotation=ReadVec3(npc+0xB4);e.savedScale=ReadVec3(npc+0xE4);
    for(uint32_t part=e.part,n=0;part && n<64 && Memory::Contains(part,0x398);++n) {  // Super Dimentio has ~40 parts
        if(Memory::Read32(part+0x388)!=npc) break;
        e.parts.push_back({part,ReadVec3(part+8),ReadVec3(part+0x14),ReadVec3(part+0xC4),ReadVec3(part+0xF4),Memory::Read32(part+0x10C)});part=Memory::Read32(part+0x390);
    }
    // SPM's per-attack immunity tables include a 127 default sentinel and are not a
    // TTYD armor statistic. Assign battle armor to the actual armored archetypes.
    e.defense=0;
    if(e.name.find("Cleft")!=std::string::npos || e.name.find("Bristle")!=std::string::npos)e.defense=2;
    if(e.name.find("Koopatrol")!=std::string::npos)e.defense=1;
    if(e.defense && e.name.find("Dark")!=std::string::npos)++e.defense;
    RT_LOGF(RT_TAG_RUNTIME,"[lpbattle] fighter %s defense %d parts %zu scale %.2f %.2f facing %.1f\n",e.name.c_str(),e.defense,e.parts.size(),e.savedScale.x,e.savedScale.y,e.savedRotation.y);
    return e;
}

void SetPose(Guest& g, const char* name) {
    const uint32_t mario = g.MarioPtr();
    const uint32_t ptr = PersistentString(g, name);
    if (!ptr || !Memory::Contains(mario, 0x30)) return;
    s.marioPose = name;
    if (Memory::Read32(mario + Mario::curPose) != ptr) g.Call(Fn::marioChgPose, {ptr});
}

bool HasAnim(const Enemy& e, int id) { return std::find(e.anims.begin(), e.anims.end(), id) != e.anims.end(); }
void OChunksAnim(Guest& g, const char* name) {
    Enemy& e = s.enemies[0];
    if (!e.present || s.oAnim == name) return;
    const uint32_t pose = Memory::Read32(e.npc + 0x44);  // NPCEntry m_Anim.m_nPoseId
    const uint32_t anim = PersistentString(g, name);
    if (pose >= 4096 || !anim) return;
    s.oAnim = name;
    g.Call(0x800432F8, {pose, anim, 1});  // animPoseSetAnim
}

void SetAnim(Guest& g, Enemy& e, int id) {
    if (s.ochunks && &e == &s.enemies[0]) {  // the generic idle / walk / hurt / wind-up ids
        OChunksAnim(g, id == 4 ? "D_1" : id == 1 ? "W_1" : id == 25 ? "S_2" : "S_1");
        return;
    }
    if (!HasAnim(e, id)) id = HasAnim(e,0)?0:e.anims.empty()?-1:e.anims.front();
    if(id<0) return;
    if (e.anim == id || !e.present) return;
    e.anim = id;
    g.Call(Fn::npcSetAnim, {e.npc, static_cast<uint32_t>(id), 1});
    e.namedAnim.clear();
}

int AliveCount() {
    return static_cast<int>(std::count_if(s.enemies.begin(), s.enemies.end(), [](const Enemy& e) { return e.alive; }));
}
int NextAlive(int from) {
    for (int i = std::max(from, 0); i < static_cast<int>(s.enemies.size()); ++i)
        if (s.enemies[i].alive) return i;
    return -1;
}
int BattleWindow(int base,int easier,int harder) {
    const int window=CombatRules::Window(base,easier,harder);
    return s.slowTurns?std::max(1,window/2):window;
}
int MarioAttack(Guest& g) {
    const uint32_t pouch = g.PouchPtr();
    const int hp=static_cast<int>(Memory::Read32(pouch+Pouch::hp));
    return std::max(0,static_cast<int>(Memory::Read32(pouch+Pouch::attack))+Has(g,BadgeId::PowerPlus)+Has(g,BadgeId::PUpDDown)-Has(g,BadgeId::PDownDUp)-Has(g,BadgeId::HpDrain)-Has(g,BadgeId::FpDrain)+
        (hp<=5?2*Has(g,BadgeId::PowerRush):0)+(hp==1?5*Has(g,BadgeId::MegaRush):0)-(s.status[Circuit]?1:0)-(s.debuffTurns?3:0));
}

void DamageEnemy(Guest& g, Enemy& e, int power,bool pierce=false) {
    if(!e.alive)return;
    // The barrier flashes up only while it turns a hit away (Tick shows Bleck's barrier part while e.barrier runs).
    if(s.finalBoss && e.boss && !s.phase2) {
        Pop(e.pos,"Chaos Heart barrier",ColBlue,1);e.barrier=BleckRules::BarrierFlashFrames;++s.barrierHits;Sound(g,"SFX_E_BARRIERMAN_CANCEL1");
        // As the game's own barrier-hit callback does: partswork 0 = 1 makes Bleck's actor play the barrier's animation.
        if(&e==&s.enemies[0] && s.shieldPart>=0 && s.shieldPart<static_cast<int>(e.parts.size()))
            Memory::Write32(e.parts[s.shieldPart].ptr+0x2A8,1);
        return;
    }
    if(s.fracktail && e.boss && !s.bossRules.Vulnerable()){Pop(e.pos,"Antenna out of reach!",ColBlue,1);Sound(g,"SFX_E_HIT_KOURA1");return;}
    const int dmg = std::max(0, power - (pierce?0:std::max(0,e.defense-(e.status[Soft]?3:0))));
    RT_LOGF(RT_TAG_RUNTIME, "[lpbattle] %s takes %d (hp %d -> %d)\n", e.name.c_str(), dmg, e.hp, std::max(0, e.hp - dmg));
    e.hp = std::max(0, e.hp - dmg);
    Sound(g,dmg==0?"SFX_E_HIT_KOURA1":s.finalBoss==1 && e.boss?"SFX_BS_ZIGEN_DAMAGE1":"SFX_MAP_NPC_HIT1");
    if(dmg==0){e.blockPose=18;Effect(e.pos,e.pos,0,ColBlue,22);}
    e.hurt = 18;
    SetAnim(g, e, 4);
    Pop({e.pos.x, e.pos.y + e.height + 8.f, e.pos.z}, std::to_string(dmg), ColYellow, 0);
    if (e.hp <= 0) {e.alive = false;if(e.boss)for(auto& minion:s.enemies)if(!minion.boss){minion.alive=false;minion.hp=0;}}
    if (e.present) Memory::Write32(e.npc + Npc::hp, static_cast<uint32_t>(std::max(e.hp, 1)));
}

void HeroHeal(Guest& g,int amount) {
    const int hp=std::min(BattleMaxHp(g),MarioHp(g)+amount);g.Call(Fn::pouchSetHp,{static_cast<uint32_t>(hp)});
    if(amount>0) {Pop({s.marioPos.x,s.marioPos.y+50,s.marioPos.z},"+"+std::to_string(amount)+" HP",ColGreen,2);Effect(s.marioPos,s.marioPos,9,ColGreen);}
}
int HeroHurt(Guest& g,int damage) {if(damage<=0)return 0;if(s.pureShield)damage=std::min(damage,1);
    if(s.marioHurt<16){Sound(g,"SFX_P_MARIO_DAMAGE1");Sound(g,"SFX_P_V_MARIO_DAMEGE1");}  // once per hit, not per swarm Luigi
    s.marioHurt=24;g.Call(Fn::pouchSetHp,{static_cast<uint32_t>(std::max(0,MarioHp(g)-damage))});return damage;}
void SetStatus(Guest& g,Enemy& e,Status status,int turns) {
    if(!e.alive || (s.fracktail && e.boss && !s.bossRules.Vulnerable()) || (s.finalBoss && e.boss && !s.phase2))return;e.status[status]=std::max(e.status[status],turns);
    constexpr const char* names[]={"Sleep","Frozen","Shrunk","Soft","Confused","Burning","Poison","Circuit -ATK"};
    RT_LOGF(RT_TAG_RUNTIME,"[lpbattle] status %s: %s %d turns\n",e.name.c_str(),names[status],turns);
    Pop({e.pos.x,e.pos.y+55,e.pos.z},names[status],ColBlue,1);
    Effect(e.pos,e.pos,status==Freeze?5:status==Burn?4:12,ColBlue,40);
    Sound(g,status==Freeze?"SFX_I_STOP_WATCH1":"SFX_I_KIRAKIRA1");
}
void AttackEffect(Guest& g,Enemy& e) {
    const char* sounds[]={"SFX_I_THUNDER1","SFX_I_POW_ATTACK1","SFX_I_KIRAKIRA1","SFX_I_BIRIBIRI1","SFX_SYS_TALK_MSG5"};
    const uint32_t colors[]={ColRed,ColYellow,ColBlue,ColGreen,0xFFFF70D0u};
    for(int i=0;i<5;++i) if(Has(g,static_cast<BadgeId>(static_cast<int>(BadgeId::AttackFxR)+i))) {
        Effect(e.pos,e.pos,20+i,colors[i],25);Sound(g,sounds[i]);
    }
}
void Strike(Guest& g,Enemy& e,int bonus=0,bool pierce=false,bool jump=false,bool quality=true) {
    if(!e.alive)return;
    if(jump && ((e.spiky && !Has(g,BadgeId::SpikeShield)) || (e.fiery && !Has(g,BadgeId::IcePower)))) {
        HeroHurt(g,1);Pop({s.marioPos.x,s.marioPos.y+50,s.marioPos.z},e.spiky?"Spikes!":"Too hot!",ColRed,1);
        s.success=false;return;
    }
    if(!jump && e.flying && s.move!=BadgeId::HammerThrow && s.move!=BadgeId::TornadoJump) {Pop(e.pos,"Too high!",ColWhite,1);return;}
    int power=MarioAttack(g)+s.spentCharge+bonus+(jump?Has(g,BadgeId::Jumpman):Has(g,BadgeId::Hammerman));
    if(e.fiery)power+=Has(g,BadgeId::IcePower);
    power=CombatRules::Attack(power,0,quality,Has(g,BadgeId::AllOrNothing)!=0,true);
    const int before=e.hp;DamageEnemy(g,e,power,pierce);s.actionHit=s.actionHit||e.hp<before;
    AttackEffect(g,e);
    if(quality) {
        AddStarEnergy(g,std::max(1,5-2*Has(g,BadgeId::Simplifier)+3*Has(g,BadgeId::Unsimplifier)));
        if(s.move==BadgeId::ShrinkStomp)SetStatus(g,e,Shrink,3);
        if(s.move==BadgeId::SleepyStomp)SetStatus(g,e,Sleep,3);
        if(s.move==BadgeId::SoftStomp)SetStatus(g,e,Soft,3);
        if(s.move==BadgeId::HeadRattle)SetStatus(g,e,Confuse,3);
        if(s.move==BadgeId::IceSmash)SetStatus(g,e,Freeze,2);
    }
    if(s.move==BadgeId::FireDrive)SetStatus(g,e,Burn,3);
}

bool StartBattleTalk(Guest& g,const std::string& message,BattlePhase after) {
    const bool key=message.rfind("stg",0)==0;
    // Only O'Chunks keeps his original fight actor name. The final bosses use
    // generated actor names and their cutscene stand-ins are hidden on stage, so
    // a centered box is safer than pointing at an invisible NPC.
    const char* speaker=s.ochunks?"dodon":nullptr;
    bool started=StartDialogue(g,message.c_str(),speaker,!key);
    if(!started && key)started=StartDialogue(g,s.ochunks?"I'm gonna get chunky on yeh!<k>":"The battle continues!<k>",speaker,true);
    if(!started)return false;
    s.afterTalk=after;Go(BattlePhase::Talk);return true;
}

void Begin(Guest& g,bool boss=false,uint32_t storyNpc=0,int finalKind=0) {
    const uint32_t mario = g.MarioPtr();
    const uint32_t first = s.pendingNpc;
    s = State{};
    s.active = true;s.finalBoss=finalKind;s.fracktail=boss;s.storyBoss=storyNpc!=0;s.originalBoss=storyNpc;
    s.marioFlags=Memory::Read32(mario+8);s.marioScale=ReadVec3(mario+0xBC);s.marioRotation=ReadVec3(mario+0xB0);
    constexpr int faceOffsets[]={0x174,0x178,0x180,0x184};
    for(int i=0;i<4;++i)s.marioFacing[i]=Memory::ReadFloat32(mario+faceOffsets[i]);
    s.facingTarget=ReadVec3(mario+0x155C);
    s.rng^=first;
    const Vec3 mpos = ReadVec3(mario + Mario::position);
    s.groundY = g_lastGroundY;
    s.marioHome = s.marioPos = {mpos.x, s.groundY, mpos.z};

    // The enemy that started it, then up to two more nearby.
    std::vector<uint32_t> picks;if(!boss && !finalKind)picks.push_back(first);
    const uint32_t base = g.Call(Fn::npcGetEntries);
    const uint32_t count = g.Call(Fn::npcGetMaxEntries);
    std::vector<std::pair<float, uint32_t>> nearby;
    for (uint32_t i = 0; !boss && !finalKind && i < count; ++i) {
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
    if(boss) {
        Enemy dragon;dragon.present=false;dragon.boss=true;dragon.tribe=313;dragon.name="Fracktail";
        dragon.hp=dragon.maxHp=24;dragon.attack=4;dragon.level=18;
        dragon.start={mpos.x+FracktailLayout::RestX,s.groundY+FracktailLayout::RaisedRootY+FracktailLayout::AntennaOffsetY,mpos.z};s.enemies.push_back(dragon);
        for(int i=0;i<2;++i){Enemy minion;minion.present=false;minion.tribe=450;minion.name="Frackle";minion.hp=minion.maxHp=2;minion.attack=1;minion.level=5;minion.start={mpos.x+75.f+i*40.f,s.groundY,mpos.z};s.enemies.push_back(minion);}
        s.musicBefore=GuestString(g.Call(0x8023ABDC,{0}));
        if(s.musicBefore!="BGM_BTL_BOSS_STG1")g.Call(0x8023A340,{0,GameString("BGM_BTL_BOSS_STG1")});
        RT_LOGF(RT_TAG_RUNTIME,"[lpfracktail] start story=%d original=%08X music BGM_BTL_BOSS_STG1\n",s.storyBoss,storyNpc);
    }
    if(finalKind) {
        // In the story the fight's own actor fights, with its real model, size and animations. Only the test
        // fixture (no actor in the room) falls back to the rebuilt rig in lp_final_boss_visual.cpp.
        Enemy enemy;
        if(storyNpc && Memory::Contains(storyNpc,Npc::size)){enemy=MakeEnemy(g,storyNpc);}
        else {enemy.present=false;enemy.start={mpos.x+180.f,s.groundY+(finalKind==1?35.f:0.f),mpos.z};}
        enemy.boss=true;enemy.name=finalKind==1?"Count Bleck":"Super Dimentio";
        // The original HP, scaled to Mario's attack so a low-level Mario (the F5 test menu) isn't in for 50 turns.
        const float hpScale=std::clamp(static_cast<float>(MarioAttack(g))/5.f,.25f,1.f);
        enemy.hp=enemy.maxHp=std::max(20,static_cast<int>((finalKind==1?BleckRules::MaxHp:DimentioRules::MaxHp)*hpScale));
        enemy.attack=6;enemy.level=60;enemy.defense=0;
        // Bleck's barrier is the actor part whose scripted barrier counter (partswork 3 of 4, armed by work 5)
        // the game's own fight uses; its model only shows while the barrier blocks.
        for(size_t i=0;finalKind==1 && i<enemy.parts.size();++i) {
            const uint32_t work=enemy.parts[i].ptr+0x2A8;
            if(Memory::Contains(work,0x18) && Memory::Read32(work+16)>=1 && Memory::Read32(work+20)==1)s.shieldPart=static_cast<int>(i);
        }
        RT_LOGF(RT_TAG_RUNTIME,"[lpfinal] barrier part %d of %zu\n",s.shieldPart,enemy.parts.size());
        for(size_t i=0;finalKind==1 && i<enemy.parts.size() && i<8;++i) {
            const uint32_t work=enemy.parts[i].ptr+0x2A8;
            RT_LOGF(RT_TAG_RUNTIME,"[lpfinal] part %zu %08X work %d %d %d %d %d %d alpha %u\n",i,enemy.parts[i].ptr,
                    Memory::Read32(work),Memory::Read32(work+4),Memory::Read32(work+8),Memory::Read32(work+12),Memory::Read32(work+16),Memory::Read32(work+20),Memory::Read8(enemy.parts[i].ptr+0x10F));
        }
        enemy.height=finalKind==1?55.f:190.f;enemy.flying=finalKind==1;enemy.spiky=enemy.fiery=enemy.ranged=false;
        s.enemies.push_back(enemy);
        s.musicBefore=GuestString(g.Call(0x8023ABDC,{0}));
        const char* music=finalKind==1?BleckRules::Music:DimentioRules::Music;
        g.Call(0x8023A340,{0,PersistentString(g,music)});
        AddStarEnergy(g,100);
        SetBattleStagePreset(finalKind==1?kBleckStage:kDimentioStage);  // their own dioramas, not Castle Bleck's
        RT_LOGF(RT_TAG_RUNTIME,"[lpfinal] begin kind %d story %d HP %d music %s\n",finalKind,s.storyBoss,enemy.hp,music);
    }
    if (const char* hp = std::getenv("SPM_LP_TEST_ENEMY_HP"))
        for (auto& e : s.enemies) e.hp = e.maxHp = std::clamp(std::atoi(hp), 1, 999);
    if (const char* badge = std::getenv("SPM_LP_TEST_POWER_BOUNCE")) {
        SetPowerBounceEquipped(g, std::atoi(badge) != 0);
        SaveProgress(g, LoadProgress(g)); // exercise equipment preservation through SP saves
    }
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
    if(BadgeTesting())if(const char* hp=std::getenv("SPM_LP_TEST_HERO_HP")) {
        const auto value=static_cast<uint32_t>(std::clamp(std::atoi(hp),1,999));g.Call(Fn::pouchSetMaxHp,{value});g.Call(Fn::pouchSetHp,{value});
    }
    s.enemiesAtStart = static_cast<int>(s.enemies.size());
    s.side = s.enemies[0].start.x >= mpos.x ? 1.f : -1.f;
    for (size_t i = 0; i < s.enemies.size(); ++i) {
        auto& e = s.enemies[i];
        // Everyone stands on the stage floor (the map's pipes and ledges are gone), flying enemies hover.
        const float fly = !boss && IsFlyer(g, e.tribe) ? std::clamp(e.start.y - s.groundY, 24.f, 60.f) : 0.f;
        e.slot = {s.marioHome.x + s.side * (78.f + 46.f * static_cast<float>(i)), s.groundY + fly, s.marioHome.z};
    }

    if(boss) {
        s.enemies[0].slot=s.enemies[0].start;
        s.enemies[0].pos=s.enemies[0].slot;
        s.bossRoot={mpos.x+FracktailLayout::RestX,s.groundY+FracktailLayout::RaisedRootY,mpos.z};
        for(int i=1;i<3;++i)s.enemies[i].slot=s.enemies[i].pos={mpos.x+65.f+(i-1)*40.f,s.groundY,mpos.z};
    }
    if(finalKind) {
        // Boss stage: Mario on the left, the boss across the stage (Bleck hovers).
        s.side=1.f;
        s.enemies[0].slot={s.marioHome.x+(finalKind==1?170.f:200.f),s.groundY+(finalKind==1?30.f:0.f),s.marioHome.z};
    }
    g.Call(Fn::marioKeyOff);
    g.Call(Fn::evtStopAll, {0xFFu});  // enemy AI scripts (and the rest of the world) hold still
    if(Has(g,BadgeId::LuckyStart)) {
        const int boon=Roll()%3;
        if(boon==0)HeroHeal(g,2);
        else if(boon==1){RecoverFp(g,3);Pop(s.marioHome,"Lucky FP!",ColGreen,1);}
        else {s.electric=3;s.boonDefense=1;Effect(s.marioHome,s.marioHome,8,ColYellow,60);}
    }
    Go(BattlePhase::Intro);
    Sound(g,"SFX_P_MARIO_3D2D_CHANGE1");  // the digital cover sweeping in
    RT_LOGF(RT_TAG_RUNTIME, "[lpbattle] start: %zu enemies\n", s.enemies.size());
}

// O'Chunks' fight, once the 1-3 intro scene ends: just him, as a boss.
void BeginOChunks(Guest& g,uint32_t npc) {
    s.pendingNpc=npc;
    Begin(g);
    s.pendingNpc=0;
    s.enemies.resize(1);
    s.enemiesAtStart=1;
    s.ochunks=true;
    s.oRules=OChunksRules{};
    s.oAnim.clear();
    Enemy& e=s.enemies[0];
    e.name="O'Chunks";e.hp=e.maxHp=OChunksRules::MaxHp;e.attack=2;e.level=12;e.defense=0;
    e.anims.clear();e.flying=e.spiky=e.fiery=e.ranged=false;e.height=62.f;
    if (const char* hp = std::getenv("SPM_LP_TEST_ENEMY_HP")) e.hp = e.maxHp = std::clamp(std::atoi(hp), 1, 999);
    // His own battle theme: the native fight script would start it after the intro, but the battle freezes scripts.
    s.musicBefore=GuestString(g.Call(0x8023ABDC,{0}));  // spsndGetBgmName
    if(s.musicBefore!="BGM_BTL_DODONTAS1")g.Call(0x8023A340,{0,GameString("BGM_BTL_DODONTAS1")});  // spsndBGMOn
    s.side=1.f;  // a boss stage: Mario on the left, as in TTYD
    e.slot={s.marioHome.x+s.side*120.f,s.groundY,s.marioHome.z};
    s.first=FirstStrike::None;
    RT_LOGF(RT_TAG_RUNTIME,"[lpochunks] start: O'Chunks %08X HP %d\n",npc,e.hp);
}

// The field as it was before the battle: map, camera, Mario and the fighters' models (battle state stays in s).
void RestoreField(Guest& g) {
    ReleaseDialogue(g);
    SetBattleStagePreset(-1);
    if(s.finalBoss)ReleaseFinalBossVisual(g);
    RestoreBossCamera();
    SaveBadges(g);
    if(s.fracktail)ReleaseFracktailVisual(g);
    UnparkFieldObjects();
    // Back to the real map (the test area keeps its own painted room).
    if (TestAreaActive()) {
        SetStage(g, 3, s.marioHome);
    } else {
        g.Call(Fn::mapDispOn);
        SetStage(g, 0, s.marioHome);
    }
    const auto mario=g.MarioPtr();
    WriteVec3(mario+0xBC,s.marioScale);WriteVec3(mario+0xB0,s.marioRotation);Memory::Write32(mario+8,s.marioFlags);
    constexpr int faceOffsets[]={0x174,0x178,0x180,0x184};
    for(int i=0;i<4;++i)Memory::WriteFloat32(mario+faceOffsets[i],s.marioFacing[i]);
    WriteVec3(mario+0x155C,s.facingTarget);
    // Restore model flags/transforms before returning survivors to their original field positions.
    for(auto& e:s.enemies) if(e.present) {
        Memory::Write32(e.npc+Npc::flag8,e.savedFlags);Memory::Write32(e.npc+0xFC,e.savedColor);
        Memory::Write32(e.npc+0x298,e.savedCamera);Memory::Write32(e.npc+0x29C,e.savedOffscreen);
        WriteVec3(e.npc+0xB4,e.savedRotation);WriteVec3(e.npc+0xE4,e.savedScale);
        if(e.alive) WriteVec3(e.npc+Npc::position,e.start);
        for(const auto& part:e.parts)if(Memory::Contains(part.ptr,0x398)) {
            if(e.alive){WriteVec3(part.ptr+8,part.pos);WriteVec3(part.ptr+0x14,part.ownerPos);}
            WriteVec3(part.ptr+0xC4,part.rotation);WriteVec3(part.ptr+0xF4,part.scale);Memory::Write32(part.ptr+0x10C,part.color);
        }
    }
}

// A story final boss's barrier breaks in the game's own cutscene: the battle steps aside (scripts stay frozen for
// Bleck, whose scene runs on its own; Super Dimentio's room scripts carry his scenes) and resumes afterwards.
struct Intermission {
    int kind = 0;
    uint32_t npc = 0, eventId = 0;
    int bossHp = 0, frames = 0, stage = 1;
} g_intermission;

void Suspend(Guest& g) {
    Enemy& boss=s.enemies[0];
    g_intermission={};
    g_intermission.kind=s.finalBoss;g_intermission.npc=boss.npc;g_intermission.bossHp=boss.hp;g_intermission.stage=s.bleckStage;
    RestoreField(g);
    g.Call(Fn::marioKeyOn);
    SetPose(g,"S_1");
    if(s.finalBoss==1) {
        // Bleck's AI runs this scene (the Pure Hearts break the Chaos Heart's barrier) once its barrier part has
        // blocked enough hits; run it directly, owned by his actor ("me" is evt+0x168), while his AI stays frozen.
        const uint32_t entry=g.Call(0x800D8D58,{BleckRules::PureHeartsScene,0xEF,0});  // evtEntry
        if(Memory::Contains(entry,0x1A8)) {
            Memory::Write32(entry+0x168,boss.npc);
            Memory::Write8(entry+12,0);Memory::Write8(entry+8,Memory::Read8(entry+8)&~2u);
            g_intermission.eventId=Memory::Read32(entry+0x158);
        }
        RT_LOGF(RT_TAG_RUNTIME,"[lpfinal] intermission: Pure Hearts scene event %08X id %u\n",entry,g_intermission.eventId);
    } else {
        // Super Dimentio's first form: the game moves on (GSW0 413) on the third hit his barrier takes. Count two,
        // then let one real hit through; the room's scripts play "You can't defeat me!" and the Pure Hearts' return.
        g.Call(Fn::evtStartAll,{0xFFu});
        Memory::Write32(boss.npc+0x510+4,DimentioRules::OpeningHits-1);
        g_allowGameDamage=true;
        g.Call(Fn::npcTakeDamage,{0,boss.part,7,1,0,0});
        g_allowGameDamage=false;
        RT_LOGF(RT_TAG_RUNTIME,"[lpfinal] intermission: Super Dimentio's opening hit, GSW0 %d\n",static_cast<int32_t>(Memory::Read32(Memory::Read32(0x8056D198)+0x140)));
    }
    s.active=false;
    s.phase=BattlePhase::None;
    s.cooldown=45;
}

void Finish(Guest& g) {
    RestoreField(g);
    g.Call(Fn::evtStartAll, {0xFFu});
    // O'Chunks isn't a Goomba: no death puff for him, the story picks up below.
    if (s.ochunks && !s.defeated) s.enemies[0].present = false;
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
    if(s.ochunks && !defeated) {
        // As after the original fight: Yold Desert's arena, cleared (the post-battle scene isn't replayed).
        const auto gp=Memory::Read32(0x8056D198);Memory::Write32(gp+0x140,34);
        g.Call(Fn::seqSetSeq,{3,GameString("he3_04"),0});
        RT_LOGF(RT_TAG_RUNTIME,"[lpochunks] victory -> cleared arena GSW0=34\n");
    }
    if(s.fracktail && !defeated) {
        if(s.storyBoss && !s.fled) {
            const auto gp=Memory::Read32(0x8056D198);Memory::Write32(gp+0x140,52);
            g.Call(Fn::seqSetSeq,{3,GameString("he4_10"),0});
            RT_LOGF(RT_TAG_RUNTIME,"[lpfracktail] story victory -> cleared arena GSW0=52\n");
        } else if(!s.musicBefore.empty())g.Call(0x8023A340,{0,GameString(s.musicBefore.c_str())});
    }
    if(s.finalBoss && !defeated) {
        if(s.storyBoss && !s.fled) {
            // Reloading these exact post-fight GSW0 values runs each boss's own
            // defeat scene, which continues the chapter instead of replaying HP.
            const int victory=s.finalBoss==1?BleckRules::VictoryGsw0:DimentioRules::VictoryGsw0;
            const auto gp=Memory::Read32(0x8056D198);
            Memory::Write32(gp+0x140,static_cast<uint32_t>(victory));
            g.Call(Fn::seqSetSeq,{3,GameString("ls4_11"),0});
            RT_LOGF(RT_TAG_RUNTIME,"[lpfinal] story victory kind %d -> GSW0=%d\n",s.finalBoss,victory);
        } else if(!s.musicBefore.empty()) {
            g.Call(0x8023A340,{0,GameString(s.musicBefore.c_str())});
        }
    }
    if (defeated && (TestAreaActive() || StoryTestRun())) {
        // A test fight: no game over. Mario gets his HP back, and an F5 story battle returns to the test room.
        g.Call(Fn::pouchSetHp, {g.Call(0x8014C9F4)});  // pouchGetMaxHp
        if (!TestAreaActive()) ReturnToTestArea(g);
        RT_LOGF(RT_TAG_RUNTIME, "[lpbattle] test defeat: HP restored%s\n",TestAreaActive() ? "" : ", back to the test area");
    } else if (defeated) {
        // Let the game run its own game over: one more hit at 0 HP.
        const uint32_t pos = g.Scratch(0x300);
        WriteVec3(pos, {s.marioHome.x + s.side * 20.f, s.groundY, s.marioHome.z});
        g.Call(Fn::pouchSetHp, {1});
        g.Call(Fn::marioTakeDamage, {pos, 0, 99});
    }
}

// --- Mario's actions ------------------------------------------------------------------------------

void AfterMarioAction(Guest& g) {
    if(s.action==Action::Jump || s.action==Action::Hammer) {
        if(s.actionHit) {if(Has(g,BadgeId::HpDrain))HeroHeal(g,Has(g,BadgeId::HpDrain));if(Has(g,BadgeId::FpDrain))RecoverFp(g,Has(g,BadgeId::FpDrain));}
        s.charge=0;s.spentCharge=0;
    }
    if(s.fracktail && s.enemies[0].alive)for(auto& minion:s.enemies)if(!minion.boss && !minion.alive)minion.hp=0;
    SetPose(g, "S_1");
    s.marioPos = s.marioHome;
    // Bleck's phase 2: the Pure Hearts let Mario's strike crack the barrier, and its breaking scene follows at once.
    if(s.finalBoss==1 && s.storyBoss && s.bleckStage==2 && s.barrierHits>=BleckRules::Phase2BarrierHits && MarioHp(g)>0) {
        RT_LOGF(RT_TAG_RUNTIME,"[lpfinal] barrier struck in stage 2: handing over to the story scene\n");
        Suspend(g);
        return;
    }
    // Spikes, fire and confusion can knock Mario out on his own turn: no enemy turn after that.
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
    s.enemyTurn = NextAlive(0);
    StartStage(0);
    s.lockout = false;
    s.guard = 0;
    Go(BattlePhase::EnemyAction);
}

void StartAction(Guest& g, Action action) {
    s.action = action;
    s.jumpHits = 0;
    s.jumpLimit = s.move==BadgeId::PowerBounce ? 7 : 2;
    s.actionHit=false;s.stylish=false;s.actionQuality=false;s.spentCharge=s.charge;
    RT_LOGF(RT_TAG_RUNTIME,"[lpbattle] action %d move %s FP %d/%d hero %d\n",static_cast<int>(action),s.move==BadgeId::Count?"Basic":kBadges[static_cast<int>(s.move)].name,BattleFp(g),BattleMaxFp(g),Memory::Read8(g.MarioPtr()+0x38));
    if (action == Action::Jump) RT_LOGF(RT_TAG_RUNTIME, "[lpbattle] Jump limit %d\n", s.jumpLimit);
    s.pressed = s.success = s.lockout = false;
    s.gauge = 0.f;
    s.windup = action == Action::Jump || action == Action::Hammer || action == Action::Thoreau ? 28 : 0;
    if((action==Action::Jump || action==Action::Hammer) && s.status[Confuse] && Roll()<50) {
        s.action=Action::Defend;s.defending=false;
        HeroHurt(g,1);Pop(s.marioHome,"Confused!",ColRed,1);Effect(s.marioHome,s.marioHome,12,ColBlue);
    }
    s.holding = s.released = false;
    StartStage(0);
    Go(BattlePhase::MarioAction);
}

void TickJump(Guest& g, Enemy& e, uint32_t pressed) {
    // Launch clear of the giant dragon's snout instead of running underneath its face.
    const Vec3 approach{e.pos.x - s.side * (e.boss?120.f:34.f), s.groundY, s.marioHome.z};
    // A protected-head attempt falls short, keeping Mario on screen instead of
    // launching him above the giant's raised antenna. Exposed hits use the orb.
    const float hitY=s.fracktail && e.boss && !s.bossRules.Vulnerable()?std::min(e.pos.y,s.groundY+100.f):e.pos.y;
    const Vec3 top{e.pos.x - s.side * 4.f, hitY + (s.fracktail && e.boss?0.f:e.height), e.boss?e.pos.z:s.marioHome.z};
    switch (s.stage) {
    case 0: {  // run up
        const float t = std::min(1.f, s.st / 22.f);
        s.marioPos = LerpV(s.marioHome, approach, Smooth(t));
        SetPose(g, "R_1");
        if (s.st >= 22) {s.leapFrom=approach;StartStage(1);Sound(g,"SFX_P_MARIO_JUMP1");Shout(g);}
    } break;
    case 1: {  // jump onto the enemy; press A just before landing
        constexpr int kLen = 32;
        const int kWindow=BattleWindow(8,Has(g,BadgeId::Simplifier),Has(g,BadgeId::Unsimplifier));
        const float t = std::min(1.f, s.st / static_cast<float>(kLen));
        s.marioPos = Arc(s.leapFrom, top, 46.f, t);
        SetPose(g, t < 0.5f ? "J_1B" : "J_1C");
        if ((pressed & (Btn::two | Btn::a)) && !s.pressed) {
            s.pressed = true;
            s.success = s.st >= kLen - kWindow;
        }
        if (s.st >= kLen) {
            s.actionQuality=s.success;
            Strike(g,e,s.move==BadgeId::PowerJump?2:0,false,true,s.success);
            ++s.jumpHits;
            if(s.move==BadgeId::TornadoJump && s.success) {
                Effect(e.pos,e.pos,7,ColBlue,45);
                for(auto& other:s.enemies)if(&other!=&e && other.flying && other.alive)Strike(g,other,1,false,true,true);
            }
            if(s.move==BadgeId::Multibounce) {
                if(s.success) {const int next=NextAlive(s.target+1);if(next>=0){s.target=next;s.leapFrom=top;StartStage(1);s.pressed=s.success=false;break;}}
                StartStage(3);
            } else if (s.success && e.alive) {
                Pop({top.x, top.y + 26.f, top.z}, "Nice!", ColYellow, 1);
                Sound(g,"SFX_I_AC_BUTTON_OK1");Sound(g,"SFX_P_MARIO_JUMP1");
                StartStage(2);
                s.pressed = s.success = false;
            } else {
                StartStage(3);
            }
        }
    } break;
    case 2: {  // each successful landing earns another bounce, up to the equipped limit
        constexpr int kLen = 26;
        const float t = std::min(1.f, s.st / static_cast<float>(kLen));
        s.marioPos = Arc(top, top, 40.f, t);
        SetPose(g, t < 0.5f ? "J_1B" : "J_1C");
        if ((pressed & (Btn::two | Btn::a)) && !s.pressed) {
            s.pressed = true;
            s.success = s.st >= kLen - BattleWindow(8,Has(g,BadgeId::Simplifier),Has(g,BadgeId::Unsimplifier));
        }
        if (s.st >= kLen) {
            Strike(g,e,s.move==BadgeId::PowerJump?2:0,false,true,s.actionQuality);
            ++s.jumpHits;
            if (s.success && e.alive && s.jumpHits < s.jumpLimit) {
                Pop({top.x, top.y + 26.f, top.z}, "Nice!", ColYellow, 1);
                Sound(g,"SFX_I_AC_BUTTON_OK1");Sound(g,"SFX_P_MARIO_JUMP1");
                StartStage(2);
                s.pressed = s.success = false;
            } else StartStage(3);
        }
    } break;
    default: {  // hop back home
        constexpr int kLen = 30;
        const float t = std::min(1.f, s.st / static_cast<float>(kLen));
        s.marioPos = Arc(top, s.marioHome, 36.f, t);
        SetPose(g, t < 0.5f ? "J_1B" : "J_1C");
        if(!s.stylish && s.st>=8 && s.st<=16 && (pressed&Btn::two)) {s.stylish=true;AddStarEnergy(g,10);Sound(g,"SFX_I_STAR_BOUND1");Pop({s.marioPos.x,s.marioPos.y+60,s.marioPos.z},"Stylish!",ColYellow,1);Effect(s.marioPos,s.marioPos,13,ColGreen);}
        if (s.st >= kLen) AfterMarioAction(g);
    } break;
    }
}

void TickHammer(Guest& g, Enemy& e, uint32_t held) {
    const Vec3 approach=s.move==BadgeId::HammerThrow?s.marioHome:Vec3{e.pos.x - s.side * 30.f, s.groundY, s.marioHome.z};
    const Vec3 strike=s.move==BadgeId::HammerThrow?approach:Vec3{e.pos.x - s.side * 12.f, s.groundY, s.marioHome.z};
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
            const float extra=.03f*(Has(g,BadgeId::Simplifier)-Has(g,BadgeId::Unsimplifier));
            s.success = release && s.gauge >= 1.f-extra && s.gauge <= 1.2f+extra;
            StartStage(2);
            Sound(g,"SFX_F_HAMMER_SWING1");Shout(g);
            Effect(s.marioPos,e.pos,s.move==BadgeId::HammerThrow?3:s.move==BadgeId::FireDrive?4:s.move==BadgeId::QuakeHammer?6:s.move==BadgeId::IceSmash?5:2,s.move==BadgeId::FireDrive?ColRed:ColYellow,28);
        }
    } break;
    case 2: {  // the swing: a short hop into the enemy
        constexpr int kLen = 14;
        const float t = std::min(1.f, s.st / static_cast<float>(kLen));
        s.marioPos = s.move==BadgeId::HammerThrow?approach:Arc(approach, strike, 20.f, t);
        SetPose(g, t < 0.5f ? "J_1B" : "J_1C");
        if (s.st >= kLen) {
            if (s.success) {Pop({e.pos.x, e.pos.y + e.height + 30.f, e.pos.z}, "Nice!", ColYellow, 1);Sound(g,"SFX_I_AC_BUTTON_OK1");}
            Sound(g,"SFX_F_HAMMER_HIT1");
            if(s.move==BadgeId::QuakeHammer || s.move==BadgeId::FireDrive) {
                for(auto& enemy:s.enemies)if(enemy.alive && !enemy.flying)Strike(g,enemy,(s.success?1:0)+(s.move==BadgeId::FireDrive?2:0),false,false,s.success);
            } else Strike(g,e,(s.success?1:0)+(s.move==BadgeId::PowerSmash?2:0),s.move==BadgeId::PiercingBlow,false,s.success);
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
        // SPM has no native FP field: +0x1E is score, which must remain cosmetic.
        // Adapt healing foods to replenish FP as well; attack items never grant FP.
        const int fpHeal=heal>0?std::clamp(heal/3,1,10):0;
        bool all = true;
        const int dmg = ItemDamage(itemId, all);
        if (dmg > 0) {
            for (auto& e : s.enemies) {
                if (!e.alive) continue;
                DamageEnemy(g, e, dmg);
                Effect(s.marioPos,e.pos,itemId==0x42?5:itemId==0x41?4:13,itemId==0x42?ColBlue:ColYellow,35);
                if(itemId==0x42)SetStatus(g,e,Freeze,2);
                if(itemId==0x41)SetStatus(g,e,Burn,3);
                if (!all) break;
            }
        } else if (heal > 0) {
            const int maxHp = BattleMaxHp(g);
            const int hp = std::min(maxHp, MarioHp(g) + heal);
            g.Call(Fn::pouchSetHp, {static_cast<uint32_t>(hp)});
            Pop({s.marioPos.x, s.marioPos.y + 50.f, s.marioPos.z}, "+" + std::to_string(heal) + " HP", ColGreen, 2);
        } else if(fpHeal<=0) {
            Pop({s.marioPos.x, s.marioPos.y + 50.f, s.marioPos.z}, "No effect...", ColWhite, 1);
        }
        if(fpHeal>0){RecoverFp(g,fpHeal);Pop(s.marioPos,"FP +"+std::to_string(fpHeal),ColGreen,2);}
        if(Has(g,BadgeId::Refund)) {const int coins=std::max(1,static_cast<int>(Memory::Read16(data+0x18))/4);g.Call(0x8014CA98,{static_cast<uint32_t>(coins)});Pop(s.marioPos,"Refund +"+std::to_string(coins),ColYellow,1);}
        // Use it up: the pouch keeps its items packed at the front.
        for (uint32_t i = static_cast<uint32_t>(s.itemToUse); i + 1 < Pouch::useItemCount; ++i)
            Memory::Write16(pouch + Pouch::useItems + 2 * i, Memory::Read16(pouch + Pouch::useItems + 2 * (i + 1)));
        Memory::Write16(pouch + Pouch::useItems + 2 * (Pouch::useItemCount - 1), 0);
    }
    if (s.st >= 50) {
        if(s.itemChain>0 && --s.itemChain>0) {
            s.itemSlots.clear();const auto pouch=g.PouchPtr();
            for(uint32_t i=0;i<Pouch::useItemCount;++i)if(Memory::Read16(pouch+Pouch::useItems+2*i))s.itemSlots.push_back(i);
            if(!s.itemSlots.empty()){s.listIndex=0;Go(BattlePhase::ItemMenu);return;}
        }
        AfterMarioAction(g);
    }
}

void TickUtility(Guest& g) {
    SetPose(g,"S_1");
    if(s.st==15) {
        switch(s.action) {
        case Action::PureShield:s.pureShield=true;Effect(s.marioHome,s.marioHome,30,ColWhite,70);break;
        case Action::LoversGrief:DamageEnemy(g,s.enemies[0],8,true);Effect(s.marioHome,s.enemies[0].pos,35,ColGreen,65);break;
        case Action::Charge:s.charge+=2*std::max(1,Has(g,BadgeId::Charge));Pop(s.marioHome,"Charge +"+std::to_string(s.charge),ColYellow,1);Effect(s.marioHome,s.marioHome,10,ColYellow,40);break;
        case Action::Recharge:RecoverFp(g,2);Pop(s.marioHome,"FP +2",ColGreen,2);Effect(s.marioHome,s.marioHome,9,ColGreen);break;
        case Action::Appeal:AddStarEnergy(g,25+25*Has(g,BadgeId::SuperAppeal));Effect(s.marioHome,s.marioHome,13,ColYellow,40);Pop(s.marioHome,"Star Energy!",ColYellow,1);break;
        case Action::Pulse:
            for(auto& e:s.enemies)if(e.alive){DamageEnemy(g,e,3,true);Effect(s.marioHome,e.pos,13,ColGreen,45);}
            HeroHeal(g,2);Sound(g,"SFX_I_STAR_MEDAL1");break;
        case Action::Swap: {
            const auto mario=g.MarioPtr();int next=Memory::Read8(mario+0x38);
            for(int step=1;step<=4;++step) {
                const int candidate=(Memory::Read8(mario+0x38)+step)%4;
                bool unlocked=BadgeTesting();
                for(int slot=0;slot<4 && !unlocked;++slot) {
                    const auto info=g.Call(0x8014E4A4,{static_cast<uint32_t>(slot)});
                    unlocked=Memory::Contains(info,4) && static_cast<int16_t>(Memory::Read16(info+2))==candidate;
                }
                if(unlocked){next=candidate;break;}
            }
            g.Call(0x80146A7C,{static_cast<uint32_t>(next)});Effect(s.marioHome,s.marioHome,14,ColGreen,45);
            g.Call(Fn::pouchSetHp,{static_cast<uint32_t>(std::min(MarioHp(g),BattleMaxHp(g)))});
            s.marioPose.clear();Pop(s.marioHome,"Hero changed!",ColGreen,1);break;
        }
        case Action::Tattle:s.reveal=true;Pop(s.marioHome,"Enemy data revealed!",ColBlue,1);Effect(s.marioHome,s.marioHome,12,ColBlue);break;
        default:break;
        }
    }
    if(s.st>=45) {
        if(s.action==Action::Swap && Has(g,BadgeId::QuickChange)) {Go(BattlePhase::Menu);return;}
        AfterMarioAction(g);
    }
}

void TickThoreau(Guest& g,uint32_t pressed) {
    SetPose(g,"S_1");
    const auto antenna=s.enemies[0].pos;
    if(s.st==10 && s.throwIndex>0){s.enemies[s.throwIndex].alive=false;s.enemies[s.throwIndex].hp=0;Sound(g,"SFX_I_KIRAKIRA1");}
    if(!s.pressed && (pressed&(Btn::two|Btn::a))){s.pressed=true;s.success=s.st>=24 && s.st<=38;}
    if(s.st<24)s.throwPos=LerpV(s.throwFrom,{s.marioHome.x,s.groundY+50,s.marioHome.z},std::clamp((s.st-10)/14.f,0.f,1.f));
    else s.throwPos=Arc({s.marioHome.x,s.groundY+50,s.marioHome.z},antenna,45,std::clamp((s.st-24)/20.f,0.f,1.f));
    if(s.st==44) {
        if(s.success){s.bossRules.ThrowHit();Pop(antenna,"Antenna exposed!",ColGreen,1);Effect(antenna,antenna,13,ColYellow,40);AddStarEnergy(g,8);}
        else {Pop(antenna,"Miss!",ColWhite,1);}
        RT_LOGF(RT_TAG_RUNTIME,"[lpfracktail] Thoreau throw %s exposed=%d\n",s.success?"hit":"miss",s.bossRules.exposed);
    }
    if(s.st>=65)AfterMarioAction(g);
}

// Thoreau grabs O'Chunks: press A as he lifts; a good throw slams him down, dizzy for a turn.
void TickThoreauOChunks(Guest& g,uint32_t pressed) {
    SetPose(g,"S_1");
    Enemy& e=s.enemies[0];
    if(!s.pressed && (pressed&(Btn::two|Btn::a))){s.pressed=true;s.success=s.st>=26 && s.st<=34;}
    if(s.st==1)Pop({s.marioHome.x,s.groundY+70,s.marioHome.z},"Thoreau!",ColGreen,1);
    if(s.st>=20 && s.st<36) OChunksAnim(g,"D_1");
    if(s.st==36 && !s.success){Pop(e.pos,"Too heavy!",ColWhite,1);}
    if(s.success && s.st>=36 && s.st<=64) {  // up, over and down
        const float t=(s.st-36)/28.f;
        e.pos={e.slot.x,e.slot.y+std::sin(t*3.14159265f)*90.f,e.slot.z};
    }
    if(s.st==64) {
        if(s.success) {
            e.pos=e.slot;s.oRules.Thrown();Strike(g,e,2,true,false,true);
            Pop({e.pos.x,e.pos.y+e.height+40,e.pos.z},"Dizzy!",ColYellow,1);Effect(e.pos,e.pos,6,ColYellow,30);
            Sound(g,"SFX_I_POW_ATTACK1");
        }
        RT_LOGF(RT_TAG_RUNTIME,"[lpochunks] Thoreau toss %s, HP %d\n",s.success?"hit":"miss",e.hp);
    }
    if(s.st>=80){OChunksAnim(g,"S_1");AfterMarioAction(g);}
}

// One hit on Mario from O'Chunks: guards, lucky dodges and defence badges, as for any enemy. Returns the damage.
int OChunksHit(Guest& g,Enemy& e,int power) {
    const Vec3 above{s.marioHome.x,s.groundY+52.f,s.marioHome.z},word{above.x,above.y+40.f,above.z};
    if(s.guard==2) {
        Pop(word,"Superguard!",ColBlue,1);DamageEnemy(g,e,1,true);
        Effect(s.marioHome,e.pos,1,ColBlue,28);Sound(g,"SFX_I_AC_BUTTON_OK1");Sound(g,"SFX_P_PEACH_GUARD_HANEKAESU1");return 0;
    }
    const int hp=MarioHp(g);
    const int dodge=10*Has(g,BadgeId::PrettyLucky)+25*Has(g,BadgeId::LuckyDay)+(hp<=5?10*Has(g,BadgeId::CloseCall):0);
    if(Roll()<std::min(90,dodge)){Pop(word,"Lucky!",ColGreen,1);return 0;}
    const int defense=(s.defending?1:0)+Has(g,BadgeId::DefendPlus)+Has(g,BadgeId::PDownDUp)-Has(g,BadgeId::PUpDDown)+s.boonDefense-(s.debuffTurns?2:0);
    const int guard=s.guard==1?1+Has(g,BadgeId::DamageDodge):0;
    const int dmg=CombatRules::Incoming(power,defense,guard,Has(g,BadgeId::DoublePain)!=0,hp<=5?Has(g,BadgeId::LastStand):0);
    if(s.guard==1){Pop(word,"Guard!",ColBlue,1);Effect(s.marioHome,s.marioHome,0,ColBlue,25);Sound(g,"SFX_I_AC_BUTTON_OK1");}
    Pop(above,std::to_string(dmg),ColRed,0);
    HeroHurt(g,dmg);
    if(dmg>0 && (Has(g,BadgeId::ZapTap) || s.electric)){DamageEnemy(g,e,1,true);Effect(e.pos,e.pos,8,ColYellow,30);}
    if(dmg>0 && Has(g,BadgeId::ReturnPostage))DamageEnemy(g,e,(dmg+1)/2,true);
    RT_LOGF(RT_TAG_RUNTIME,"[lpochunks] hit power %d damage %d guard %d\n",power,dmg,s.guard);
    return dmg;
}

// A: guard in the last 8 frames before the hit, B: superguard in the last 3, anything earlier locks out.
void OChunksGuardInput(Guest& g,uint32_t pressed,int framesLeft) {
    if(s.lockout || s.guard)return;
    if((pressed&(Btn::one|Btn::b)) && framesLeft<=BattleWindow(3,0,Has(g,BadgeId::Unsimplifier)))s.guard=2;
    else if((pressed&(Btn::two|Btn::a)) && framesLeft<=BattleWindow(8,Has(g,BadgeId::Simplifier),Has(g,BadgeId::Unsimplifier)))s.guard=1;
    else if(pressed&(Btn::two|Btn::a|Btn::one|Btn::b))s.lockout=true;
    if(s.guard)s.guardPose=20;
}

void EndOChunksTurn() {s.enemyTurn=NextAlive(1);s.guard=0;s.lockout=false;StartStage(0);}

// His turn: Shoulder Bash -> Ground Pound -> Grab 'n' Toss, around and around.
void TickOChunks(Guest& g,uint32_t pressed) {
    Enemy& e=s.enemies[0];
    const Vec3 contact{s.marioHome.x+s.side*26.f,s.groundY,s.marioHome.z};
    if(s.stage==0 && s.st==1) {
        if(!e.alive){EndOChunksTurn();return;}
        if(s.oRules.TakeSkip() || e.status[Sleep] || e.status[Freeze]) {
            const bool danced=!s.oRules.dizzy && !e.status[Sleep] && !e.status[Freeze];
            Pop({e.pos.x,e.pos.y+e.height+30,e.pos.z},danced?"Victory dance!":e.status[Freeze]?"Frozen!":e.status[Sleep]?"Zzz...":"Dizzy...",ColBlue,1);
            OChunksAnim(g,danced?"Y_1":"D_1");s.stage=9;return;  // spends the turn
        }
        if(s.oRules.CheckEnrage(e.hp)){OChunksAnim(g,"Q_1");s.stage=8;StartBattleTalk(g,"stg1_3_013",BattlePhase::EnemyAction);return;}
        s.oAttack=s.oRules.Next();

        RT_LOGF(RT_TAG_RUNTIME,"[lpochunks] attack %d HP %d enraged %d\n",static_cast<int>(s.oAttack),e.hp,s.oRules.enraged);
    }
    const int power=s.oRules.Power(s.oAttack)-(e.status[Shrink]?1:0);
    switch(s.stage) {
    case 8: if(s.st>=50){OChunksAnim(g,"S_1");StartStage(0);} return;  // enrage roar, then attack this turn
    case 9: if(s.st>=50){OChunksAnim(g,"S_1");EndOChunksTurn();} return;
    case 0:  // wind-up
        OChunksAnim(g,s.oAttack==OChunksAttack::GroundPound?"J_1":"S_2");
        if(s.st==29)Sound(g,s.oAttack==OChunksAttack::GroundPound?"SFX_BS_DDN_JUMP1":"SFX_E_DODON_ATTACK1");
        if(s.st>=30)StartStage(1);
        return;
    default: break;
    }
    if(s.oAttack==OChunksAttack::ShoulderBash) {
        constexpr int kRun=20;
        if(s.stage==1) {
            OChunksAnim(g,"R_1");
            e.pos=LerpV(e.slot,contact,std::min(1.f,s.st/static_cast<float>(kRun)));
            OChunksGuardInput(g,pressed,kRun-s.st);
            if(s.st>=kRun){OChunksHit(g,e,power);StartStage(2);}
        } else {
            OChunksAnim(g,"W_1");
            e.pos=LerpV(contact,e.slot,std::min(1.f,s.st/30.f));
            if(s.st>=30){e.pos=e.slot;OChunksAnim(g,"S_1");EndOChunksTurn();}
        }
        return;
    }
    if(s.oAttack==OChunksAttack::GroundPound) {
        const Vec3 overhead{s.marioHome.x,s.groundY+120.f,s.marioHome.z};
        if(s.stage==1) {  // leap above Mario
            OChunksAnim(g,"J_2");
            e.pos=Arc(e.slot,overhead,60.f,std::min(1.f,s.st/30.f));
            if(s.st>=36)StartStage(2);
        } else if(s.stage==2) {  // slam
            constexpr int kSlam=10;
            OChunksAnim(g,"J_3");
            e.pos=LerpV(overhead,{s.marioHome.x+s.side*8.f,s.groundY+28.f,s.marioHome.z},std::min(1.f,s.st/static_cast<float>(kSlam)));
            OChunksGuardInput(g,pressed,kSlam-s.st);
            if(s.st>=kSlam){OChunksHit(g,e,power);Effect(s.marioHome,s.marioHome,6,ColYellow,30);Sound(g,"SFX_I_POW_ATTACK1");StartStage(3);}
        } else if(s.stage==3) {
            OChunksAnim(g,"J_4");
            if(s.st>=16)StartStage(4);
        } else {  // hop back
            OChunksAnim(g,"J_2");
            e.pos=Arc({s.marioHome.x+s.side*8.f,s.groundY+28.f,s.marioHome.z},e.slot,40.f,std::min(1.f,s.st/26.f));
            if(s.st>=26){e.pos=e.slot;OChunksAnim(g,"S_1");EndOChunksTurn();}
        }
        return;
    }
    // Grab 'n' Toss: he stomps over and grabs; a guard shakes him off, a superguard breaks free and hits back.
    if(s.stage==1) {
        OChunksAnim(g,"W_1");
        e.pos=LerpV(e.slot,contact,std::min(1.f,s.st/34.f));
        if(s.st>=34)StartStage(2);
    } else if(s.stage==2) {
        constexpr int kGrab=12;
        OChunksAnim(g,"A_1a");
        OChunksGuardInput(g,pressed,kGrab-s.st);
        if(s.st>=kGrab) {
            if(s.guard) {
                Pop({s.marioHome.x,s.groundY+92,s.marioHome.z},s.guard==2?"Broke free!":"Shook him off!",ColBlue,1);
                OChunksHit(g,e,s.guard==2?power:1);  // a superguard counters; a guard takes only the shove
                StartStage(5);
            } else StartStage(3);
        }
    } else if(s.stage==3) {  // grabbed: up and over his head...
        OChunksAnim(g,"A_1b");
        s.marioPos=Arc(s.marioHome,{e.pos.x,s.groundY+70.f,s.marioHome.z},50.f,std::min(1.f,s.st/24.f));
        SetPose(g,"D_2");
        if(s.st>=24)StartStage(4);
    } else if(s.stage==4) {  // ...and slammed down
        OChunksAnim(g,"A_1c");
        s.marioPos=Arc({e.pos.x,s.groundY+70.f,s.marioHome.z},s.marioHome,20.f,std::min(1.f,s.st/12.f));
        SetPose(g,"D_2");
        if(s.st>=12) {
            s.marioPos=s.marioHome;OChunksHit(g,e,power);Effect(s.marioHome,s.marioHome,6,ColRed,30);
            s.oRules.Tossed();StartStage(5);
        }
    } else {
        OChunksAnim(g,"W_1");
        e.pos=LerpV(contact,e.slot,std::min(1.f,s.st/30.f));
        if(s.st>=30){e.pos=e.slot;OChunksAnim(g,"S_1");EndOChunksTurn();}
    }
}

void SpawnFinalHelpers(Guest& g);

void EndRound(Guest& g) {
    ++s.round;
    s.pureShield=false;
    if(s.slowTurns>0)--s.slowTurns;
    if(s.debuffTurns>0)--s.debuffTurns;
    // Story Bleck has three phases: three protected turns, then the Pure Hearts arrive (a scene) but his barrier
    // holds until Mario strikes it once more, then it breaks (a second scene) and he can be hurt. Super Dimentio's
    // first form hands over after its three protected turns.
    const bool storyScene=s.finalBoss && s.storyBoss && !s.phase2 &&
        (s.bleckStage==2 ? s.barrierHits>=BleckRules::Phase2BarrierHits : ++s.phase1Turns>=3);
    if(storyScene) {
        s.suspendForScene=true;
        RT_LOGF(RT_TAG_RUNTIME,"[lpfinal] barrier stage %d done: handing over to the story scene\n",s.bleckStage);
    } else if(s.finalBoss && !s.storyBoss && !s.phase2 && s.phase1Turns>=3) {
        s.phase2=true;s.finalTurn=0;s.pendingTalk="The Pure Hearts have broken the Chaos Heart's barrier!<k>";
        if(s.finalBoss==2)SpawnFinalHelpers(g);  // phase 2 opens with the mini-Luigi swarm
        Effect(s.marioHome,s.enemies[0].pos,30,ColWhite,70);
        RT_LOGF(RT_TAG_RUNTIME,"[lpfinal] phase2 after %d enemy turns\n",s.phase1Turns);
    }
    if(s.fracktail)s.bossRules.EndRound();
    if(s.ochunks)s.oRules.EndRound();
    for(auto& e:s.enemies) if(e.alive) {
        if(e.status[Burn] || e.status[Poison])DamageEnemy(g,e,1,true);
        for(auto& duration:e.status)if(duration>0)--duration;
    }
    if(s.status[Burn] || s.status[Poison]) {HeroHurt(g,1);Pop(s.marioHome,"Status -1",ColRed,1);}
    for(auto& duration:s.status)if(duration>0)--duration;
    if(s.electric>0)--s.electric;
    if(s.round%2==0) {if(Has(g,BadgeId::HappyHeart))HeroHeal(g,Has(g,BadgeId::HappyHeart));if(Has(g,BadgeId::HappyFlower)){RecoverFp(g,Has(g,BadgeId::HappyFlower));Pop(s.marioHome,"Happy FP!",ColGreen,1);}}
}

const char* TacticName(Tactic t) {
    switch(t){case Tactic::Defend:return "Defend";case Tactic::Run:return "Run Away";case Tactic::Charge:return "Charge";
    case Tactic::DoubleDip:return "Double Dip";case Tactic::Recharge:return "Recharge FP";case Tactic::Appeal:return "Appeal";
    case Tactic::Pulse:return "Digital Burst";case Tactic::Swap:return "Change Hero";case Tactic::Tattle:return "Tattle";case Tactic::Thoreau:return s.ochunks?"Thoreau: Toss O'Chunks":"Thoreau: Throw Frackle";}return "Tactics";
}
void BuildTactics(Guest& g) {
    s.tactics={Tactic::Defend,Tactic::Run,Tactic::Recharge,Tactic::Appeal,Tactic::Pulse,Tactic::Tattle};
    if(Has(g,BadgeId::Charge))s.tactics.push_back(Tactic::Charge);
    if(Has(g,BadgeId::DoubleDip))s.tactics.push_back(Tactic::DoubleDip);
    s.tactics.push_back(Tactic::Swap);
    if(s.fracktail || s.ochunks)s.tactics.insert(s.tactics.begin(),Tactic::Thoreau);
}

void TickRun(Guest& g, uint32_t pressed) {
    SetPose(g, "R_1");
    if (pressed & (Btn::two | Btn::a)) s.gauge += 0.085f;
    s.gauge = std::max(0.f, s.gauge - 0.004f);
    if (s.gauge >= 1.f) {
        s.fled = true;
        Banner("Got away!");
        Sound(g,"SFX_SYS_MENU_CLOSE1");
        Go(BattlePhase::Fled);
        return;
    }
    if (s.st >= 150) {
        Pop({s.marioPos.x, s.marioPos.y + 50.f, s.marioPos.z}, "Couldn't escape!", ColWhite, 1);
        AfterMarioAction(g);
    }
}

// --- the enemies' turn ----------------------------------------------------------------------------

void TickFracktail(Guest& g,uint32_t pressed) {
    auto& boss=s.enemies[0];
    const Vec3 rest{s.marioHome.x+FracktailLayout::RestX,s.bossRoot.y,s.marioHome.z};
    if(s.stage==0 && s.st==1) {
        if(!boss.alive || boss.status[Sleep] || boss.status[Freeze]) {
            Pop(boss.pos,!boss.alive?"":boss.status[Freeze]?"Frozen!":"Zzz...",ColBlue,1);
            s.enemyTurn=NextAlive(1);s.guard=0;s.lockout=false;StartStage(0);return;
        }
        if(boss.status[Confuse] && Roll()<50) {
            Pop(boss.pos,"Confused!",ColGreen,1);DamageEnemy(g,boss,1,true);
            s.enemyTurn=NextAlive(1);s.guard=0;s.lockout=false;StartStage(0);return;
        }
        s.bossAttack=s.bossRules.Next();

        RT_LOGF(RT_TAG_RUNTIME,"[lpfracktail] attack %d round %d exposed=%d\n",static_cast<int>(s.bossAttack),s.round,s.bossRules.exposed);
    }
    if(s.bossAttack==FracktailAttack::Summon) {
        if(s.st==36)for(int i=1;i<3;++i){auto& e=s.enemies[i];e.hp=e.maxHp;e.alive=true;e.status.fill(0);e.pos=e.slot;Effect(e.pos,e.pos,14,ColGreen,35);}
        if(s.st>=65){s.enemyTurn=NextAlive(1);s.guard=0;s.lockout=false;StartStage(0);}return;
    }
    if(s.stage==0) {if(s.st>=36)StartStage(1);return;}
    if(s.stage==1) {
        if(s.st==1) {
            Sound(g,s.bossAttack==FracktailAttack::Charge?"SFX_BS_ZNB_FRONT_ATTACK1":s.bossAttack==FracktailAttack::Breath?"SFX_E_TESHITA_BREATH1":"SFX_E_PHOTON_ATTACK1");
            if(s.bossAttack==FracktailAttack::CircuitBreaker){s.glitchFrames=30;Effect(boss.pos,s.marioHome,26,ColGreen,35);}
            else Effect(boss.pos,boss.pos,10,ColYellow,20);
        }
        if(pressed&(Btn::two|Btn::one|Btn::a|Btn::b))s.lockout=true;
        if(s.st>=18)StartStage(2);return;
    }
    if(s.stage==2) {
        const float t=std::min(1.f,s.st/12.f);
        if(s.bossAttack==FracktailAttack::Charge)s.bossRoot.x=rest.x+(s.marioHome.x-90-rest.x)*t;
        if(s.st==1 && s.bossAttack==FracktailAttack::Breath)Effect(boss.pos,{s.marioHome.x,s.groundY+25,s.marioHome.z},25,ColBlue,38);
        const int left=12-s.st;
        if(!s.guard && !s.lockout) {
            if((pressed&(Btn::one|Btn::b)) && left<=BattleWindow(3,0,Has(g,BadgeId::Unsimplifier)))s.guard=2;
            else if((pressed&(Btn::two|Btn::a)) && left<=BattleWindow(8,Has(g,BadgeId::Simplifier),Has(g,BadgeId::Unsimplifier)))s.guard=1;
            else if(pressed&(Btn::two|Btn::one|Btn::a|Btn::b))s.lockout=true;
            if(s.guard)s.guardPose=22;
        }
        if(s.st>=12) {
            int power=s.bossAttack==FracktailAttack::Charge?4:s.bossAttack==FracktailAttack::Breath?2:1;
            if(boss.status[Shrink])power=std::max(1,power/2);
            const int defense=(s.defending?1:0)+Has(g,BadgeId::DefendPlus)+Has(g,BadgeId::PDownDUp)-Has(g,BadgeId::PUpDDown)+s.boonDefense-(s.debuffTurns?2:0);
            const int dodge=10*Has(g,BadgeId::PrettyLucky)+25*Has(g,BadgeId::LuckyDay)+(MarioHp(g)<=5?10*Has(g,BadgeId::CloseCall):0);
            const bool missed=Roll()<std::min(90,dodge);
            int damage=s.guard==2 || missed?0:CombatRules::Incoming(power,defense,s.guard?1+Has(g,BadgeId::DamageDodge):0,Has(g,BadgeId::DoublePain),MarioHp(g)<=5?Has(g,BadgeId::LastStand):0);
            HeroHurt(g,damage);Pop({s.marioHome.x,s.groundY+55,s.marioHome.z},std::to_string(damage),ColRed,0);
            if(s.guard){Sound(g,"SFX_I_AC_BUTTON_OK1");if(s.guard==2)Sound(g,"SFX_P_PEACH_GUARD_HANEKAESU1");Effect(s.marioHome,s.marioHome,s.guard==2?1:0,ColBlue,28);Pop({s.marioHome.x,s.groundY+95,s.marioHome.z},s.guard==2?"Superguard!":"Guard!",ColBlue,1);if(s.guard==2 && s.bossRules.Vulnerable())DamageEnemy(g,boss,1,true);}
            if(missed)Pop(s.marioHome,"Lucky!",ColGreen,1);
            if(damage && Has(g,BadgeId::PityFlower) && Roll()<30)RecoverFp(g,1);
            if(s.bossAttack==FracktailAttack::Charge && !missed && s.guard!=2) {
                if(Has(g,BadgeId::ZapTap) || s.electric)DamageEnemy(g,boss,1,true);
                if(Has(g,BadgeId::ReturnPostage) && damage)DamageEnemy(g,boss,(damage+1)/2,true);
            }
            if(s.bossAttack==FracktailAttack::CircuitBreaker) {
                s.glitchFrames=45;
                if(!s.guard && !missed && !Has(g,BadgeId::FeelingFine)){s.status[Circuit]=3;Pop(s.marioHome,"Circuit: ATK -1",ColBlue,1);}
            }
            RT_LOGF(RT_TAG_RUNTIME,"[lpfracktail] hit power %d damage %d guard %d circuit %d\n",power,damage,s.guard,s.status[Circuit]);
            StartStage(3);
        }
        return;
    }
    s.bossRoot.x=LerpV({s.marioHome.x-90,s.bossRoot.y,s.marioHome.z},rest,std::min(1.f,s.st/30.f)).x;
    if(s.bossAttack!=FracktailAttack::Charge)s.bossRoot.x=rest.x;
    if(s.st>=30){s.bossRoot.x=rest.x;s.enemyTurn=NextAlive(1);s.guard=0;s.lockout=false;StartStage(0);}
}

#include "lp_final_battle.inc"

// A final boss whose barrier the story's cutscene already broke: phase 2 from the first turn.
void StartFinalPhase2(Guest& g) {
    s.phase2=true;s.finalTurn=0;s.introTalkDone=true;
    if(s.finalBoss==2)SpawnFinalHelpers(g);
    RT_LOGF(RT_TAG_RUNTIME,"[lpfinal] kind %d starts in phase 2\n",s.finalBoss);
}

void TickEnemy(Guest& g, uint32_t pressed) {
    if (s.enemyTurn < 0 || s.enemyTurn >= static_cast<int>(s.enemies.size())) {
        EndRound(g);
        if(MarioHp(g)<=0){s.defeated=true;Banner("Mario was defeated...");Go(BattlePhase::Defeat);return;}
        if(s.suspendForScene){Suspend(g);return;}
        if(AliveCount()==0){Banner("VICTORY!");Go(BattlePhase::Victory);return;}
        s.defending = false;
        Go(BattlePhase::Menu);
        return;
    }
    if(s.finalBoss && (s.enemyTurn==0 || (s.finalBoss==2 && s.enemyTurn==1))){TickFinalBoss(g,pressed);return;}
    if(s.fracktail && s.enemyTurn==0){TickFracktail(g,pressed);return;}
    if(s.ochunks && s.enemyTurn==0){TickOChunks(g,pressed);return;}
    Enemy& e = s.enemies[s.enemyTurn];
    const Vec3 strikeFrom=e.ranged?e.slot:Vec3{s.marioHome.x + s.side * 34.f, s.groundY + (e.slot.y - s.groundY) * 0.5f, s.marioHome.z};
    const Vec3 contact{s.marioHome.x + s.side * 12.f, s.groundY + 6.f, s.marioHome.z};
    constexpr int kLunge = 12;
    const int kGuard=BattleWindow(8,Has(g,BadgeId::Simplifier),Has(g,BadgeId::Unsimplifier));
    const int kSuper=BattleWindow(3,0,Has(g,BadgeId::Unsimplifier));
    if(s.stage==0 && s.st==1 && (!e.alive || e.status[Sleep] || e.status[Freeze])) {
        Pop({e.pos.x,e.pos.y+45,e.pos.z},!e.alive?"":e.status[Freeze]?"Frozen!":"Zzz...",ColBlue,1);StartStage(4);
    }
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
        e.pos = e.ranged?e.slot:Arc(strikeFrom, contact, 14.f, t);
        if(s.st==1)Sound(g,e.ranged?"SFX_E_MAHOU_ATTACK1":e.flying?"SFX_E_TELESA_ATTACK1":"SFX_E_KURIBOO_JUMP1");
        if(e.ranged && s.st==1) {
            const bool voidBlast=s.finalBoss==1;
            Effect(e.slot,contact,voidBlast?31:(e.fiery?4:3),voidBlast?ColBlue:(e.fiery?ColRed:ColYellow),kLunge+6);
        }
        if (!s.lockout && !s.guard) {
            const int left = kLunge - s.st;
            if ((pressed & (Btn::one | Btn::b)) && left <= kSuper) s.guard = 2;
            else if ((pressed & (Btn::two | Btn::a)) && left <= kGuard) s.guard = 1;
            else if (pressed & (Btn::two | Btn::a | Btn::one | Btn::b)) s.lockout = true;
            if(s.guard) s.guardPose=20;
        }
        if (s.st >= kLunge) {
            const Vec3 above{s.marioHome.x, s.groundY + 52.f, s.marioHome.z};
            const Vec3 word{above.x, above.y + 40.f, above.z};  // clear of the damage starburst
            if (s.guard == 2) {
                Pop(word, "Superguard!", ColBlue, 1);
                DamageEnemy(g, e, 1,true);
                Effect(s.marioHome,e.pos,1,ColBlue,28);Sound(g,"SFX_I_AC_BUTTON_OK1");Sound(g,"SFX_P_PEACH_GUARD_HANEKAESU1");
            } else {
                const int hp=MarioHp(g);
                const int dodge=10*Has(g,BadgeId::PrettyLucky)+25*Has(g,BadgeId::LuckyDay)+(hp<=5?10*Has(g,BadgeId::CloseCall):0);
                const bool miss=Roll()<std::min(90,dodge);
                const bool confused=e.status[Confuse] && Roll()<50;
                int attack=e.status[Shrink]?std::max(1,e.attack/2):e.attack;
                const int defense=(s.defending?1:0)+Has(g,BadgeId::DefendPlus)+Has(g,BadgeId::PDownDUp)-Has(g,BadgeId::PUpDDown)+s.boonDefense-(s.debuffTurns?2:0)+(e.fiery?Has(g,BadgeId::IcePower):0);
                const int guard=s.guard==1?1+Has(g,BadgeId::DamageDodge):0;
                int dmg=CombatRules::Incoming(attack,defense,guard,Has(g,BadgeId::DoublePain)!=0,hp<=5?Has(g,BadgeId::LastStand):0);
                if(miss || confused){dmg=0;Pop(word,confused?"Confused!":"Lucky!",ColGreen,1);Effect(s.marioHome,s.marioHome,12,ColGreen);if(confused)DamageEnemy(g,e,1,true);}
                if (s.guard == 1) {Pop(word, "Guard!", ColBlue, 1);Effect(s.marioHome,s.marioHome,0,ColBlue,25);Sound(g,"SFX_I_AC_BUTTON_OK1");}
                Pop(above, std::to_string(dmg), ColRed, 0);
                RT_LOGF(RT_TAG_RUNTIME, "[lpbattle] Mario takes %d (guard %d)\n", dmg, s.guard);
                if (dmg > 0) {
                    HeroHurt(g,dmg);
                    if(Has(g,BadgeId::PityFlower) && Roll()<30){RecoverFp(g,1);Pop(s.marioHome,"Pity FP +1",ColGreen,1);}
                    if(!Has(g,BadgeId::FeelingFine) && s.guard==0) {
                        if(e.fiery)s.status[Burn]=3;
                        if(e.name.find("Poison")!=std::string::npos || e.name.find("Putrid")!=std::string::npos)s.status[Poison]=3;
                        if(e.name.find("Cursya")!=std::string::npos)s.status[Confuse]=3;
                    }
                }
                if(!miss && !confused && !e.ranged) {
                    if(Has(g,BadgeId::ZapTap) || s.electric){DamageEnemy(g,e,1,true);Effect(e.pos,e.pos,8,ColYellow,30);}
                    if(Has(g,BadgeId::ReturnPostage) && dmg>0)DamageEnemy(g,e,(dmg+1)/2,true);
                }
            }
            StartStage(3);
        }
    } break;
    default: {
        const float t = std::min(1.f, s.st / 24.f);
        e.pos = e.ranged || s.stage==4?e.slot:Arc(contact, e.slot, 18.f, t);
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
void BuildMoves(Guest& g,bool jump) {
    s.moves={BadgeId::Count};
    for(const auto& badge:kBadges)if(badge.kind==(jump?BadgeKind::Jump:BadgeKind::Hammer) && Has(g,badge.id))s.moves.push_back(badge.id);
    s.listIndex=0;
}
void OpenItems(Guest& g) {
    s.itemSlots.clear();const auto pouch=g.PouchPtr();
    for(uint32_t i=0;i<Pouch::useItemCount;++i)if(Memory::Read16(pouch+Pouch::useItems+2*i))s.itemSlots.push_back(i);
    s.listIndex=0;Go(BattlePhase::ItemMenu);
}

// --- per frame ------------------------------------------------------------------------------------

// Test probes. SPM_LP_PROBE_NPCS=1 lists the map's active NPCs (name, tribe, model, HP, position and the tribe's
// animations) once a few seconds in. SPM_LP_POSE_CYCLE=S_1,E_1,... holds Mario still and steps through those poses,
// 50 frames each, logging each switch, to find animations by eye.
void ProbeTick(Guest& g) {
    if (g.Call(Fn::seqGetSeq) != 2) return;
    static int frames = 0;
    ++frames;
    static const char* listNpcs = std::getenv("SPM_LP_PROBE_NPCS");  // =n: n frames into each map
    static std::string probedMap;
    static int mapFrames = 0;
    const std::string map = GuestString(Memory::Read32(0x8056D198) + 0x44, 32);
    if (map != probedMap) {
        probedMap = map;
        mapFrames = 0;
    }
    ++mapFrames;
    if (listNpcs && mapFrames == std::max(30, std::atoi(listNpcs))) {
        RT_LOGF(RT_TAG_RUNTIME, "[lpprobe] map %s gsw0 %d\n", map.c_str(),
                static_cast<int32_t>(Memory::Read32(Memory::Read32(0x8056D198) + 0x140)));
        const uint32_t base = g.Call(Fn::npcGetEntries), count = g.Call(Fn::npcGetMaxEntries);
        for (uint32_t i = 0; i < count; ++i) {
            const uint32_t npc = base + i * Npc::size;
            if (!(Memory::Read32(npc + Npc::flag8) & 1)) continue;
            const int tribe = static_cast<int32_t>(Memory::Read32(npc + Npc::tribeId));
            const uint32_t tp = TribePtr(g, tribe);
            const Vec3 p = ReadVec3(npc + Npc::position);
            std::string anims;
            if (tp)
                for (uint32_t defs = Memory::Read32(tp + 4), k = 0; k < 48 && Memory::Contains(defs, 8); ++k, defs += 8) {
                    const int32_t id = static_cast<int32_t>(Memory::Read32(defs));
                    if (id < 0) break;
                    anims += " " + std::to_string(id) + "=" + GuestString(Memory::Read32(defs + 4), 24);
                }
            RT_LOGF(RT_TAG_RUNTIME, "[lpprobe] npc %08X '%s' tribe %d model %s hp %d pos %.1f %.1f %.1f anims%s\n", npc,
                    GuestString(npc + Npc::name, 32).c_str(), tribe, tp ? GuestString(Memory::Read32(tp), 32).c_str() : "-",
                    static_cast<int32_t>(Memory::Read32(npc + Npc::hp)), p.x, p.y, p.z, anims.c_str());
        }
    }
    // SPM_LP_RUN_EVT=0x80D7B34C starts that event script two seconds into the map (jump straight into a cutscene).
    // SPM_LP_RUN_EVT_X=n puts Mario at that x at the same moment (on a cutscene's trigger, or where its camera looks).
    static const char* runEvt = std::getenv("SPM_LP_RUN_EVT");
    static const char* runX = std::getenv("SPM_LP_RUN_EVT_X");
    if (runX && mapFrames == 120) {
        const uint32_t mario = g.MarioPtr();
        if (Memory::Contains(mario, 0x70)) Memory::WriteFloat32(mario + Mario::position, std::strtof(runX, nullptr));
    }
    if (runEvt && mapFrames == 120) {
        const uint32_t script = static_cast<uint32_t>(std::strtoul(runEvt, nullptr, 0));
        if (Memory::Contains(script, 8)) {
            const uint32_t entry = g.Call(0x800D8D58, {script, 0xEF, 0});  // evtEntry
            RT_LOGF(RT_TAG_RUNTIME, "[lpprobe] started evt %08X in %s -> %08X\n", script, map.c_str(), entry);
        }
    }
    // SPM_LP_LOG_POSE: Mario's current pose (whatever set it: scripts, handlers, motion), each time it changes.
    static const bool poseLog = std::getenv("SPM_LP_LOG_POSE") != nullptr;
    if (poseLog) {
        static std::string lastPose;
        const uint32_t mario = g.MarioPtr();
        const std::string now = Memory::Contains(mario, 0x20) ? GuestString(Memory::Read32(mario + Mario::curPose), 16) : "";
        if (now != lastPose) {
            lastPose = now;
            RT_LOGF(RT_TAG_RUNTIME, "[lppose] current %s map %s gsw0 %d x %.0f\n", now.c_str(), map.c_str(),
                    static_cast<int32_t>(Memory::Read32(Memory::Read32(0x8056D198) + 0x140)),
                    Memory::Contains(mario, 0x70) ? Memory::ReadFloat32(mario + Mario::position) : 0.f);
        }
    }
    static const char* cycle = std::getenv("SPM_LP_POSE_CYCLE");
    if (cycle && frames > 120) {
        static std::vector<std::string> poses = [] {
            std::vector<std::string> list;
            std::string rest = cycle;
            for (size_t end;; rest.erase(0, end + 1)) {
                end = rest.find(',');
                list.push_back(rest.substr(0, end));
                if (end == std::string::npos) break;
            }
            return list;
        }();
        const size_t index = static_cast<size_t>((frames - 120) / 50) % poses.size();
        g.Call(Fn::marioKeyOff);
        if ((frames - 120) % 50 == 0) {
            RT_LOGF(RT_TAG_RUNTIME, "[lpprobe] frame %d pose %s\n", frames, poses[index].c_str());
            ShowFieldNotice("Pose " + poses[index], 50);
        }
        SetPose(g, poses[index].c_str());
    }
}

// Test autopilot (SPM_LP_AUTOPLAY=J,H,I,D,R: one command per Mario turn, cycling): picks commands and nails
// every action command, guarding and superguarding alternately. Adds synthetic pressed/held buttons.
void Autoplay(Guest& g,uint32_t& pressed, uint32_t& held) {
    static int moveSelections=0;
    static const std::string plan = [] {
        const char* v = std::getenv("SPM_LP_AUTOPLAY");
        std::string p;
        for (const char* c = v ? v : ""; *c; ++c)
            if (*c != ',') p += *c;
        return p;
    }();
    if (plan.empty()) return;
    pressed = held = 0;  // the autopilot alone plays the battle (a test's input script may still be pressing)
    static int turn = 0, enemyTurns = 0, lastEnemyStage = -1;
    const char cmd = plan[turn % plan.size()];
    switch (s.phase) {
    case BattlePhase::Menu:
        if (s.t == 20) {
            if(s.fracktail && cmd=='B') {s.menu=s.bossRules.Vulnerable()?0:3;pressed|=Btn::two;++turn;break;}
            if(cmd=='S') {s.menu=SpecialOpen() && Badges(g).energy>=50?4:3;pressed|=Btn::two;++turn;break;}
            if(cmd=='T') {s.menu=s.ochunks && !s.oRules.thoreauRest?3:0;pressed|=Btn::two;++turn;break;}
            s.menu = cmd == 'J' ? 0 : cmd == 'H' ? 1 : cmd == 'I' ? 2 : 3;
            pressed |= Btn::two;
            ++turn;
        }
        break;
    case BattlePhase::Target:
    case BattlePhase::ItemMenu:
        if (s.t == 15) pressed |= Btn::two;
        break;
    case BattlePhase::MoveMenu:
        if(s.t==10) {
            s.listIndex=0;
            std::string moveKey;
            if(const char* sequence=std::getenv("SPM_LP_TEST_MOVES")) {
                std::vector<std::string> keys;std::string rest=sequence;
                for(size_t end=rest.find(',');;end=rest.find(',')) {
                    keys.push_back(rest.substr(0,end));if(end==std::string::npos)break;rest.erase(0,end+1);
                }
                if(!keys.empty())moveKey=keys[moveSelections++%keys.size()];
            } else if(const char* move=std::getenv("SPM_LP_TEST_MOVE"))moveKey=move;
            for(size_t i=1;i<s.moves.size();++i) {
                const int id=static_cast<int>(s.moves[i]);
                if(moveKey==std::to_string(id+1) || moveKey==kBadges[id].name)s.listIndex=static_cast<int>(i);
            }
        }
        if(s.t==15)pressed|=Btn::two;
        break;
    case BattlePhase::SpecialMenu:
        if(s.t==10) {s.listIndex=0;if(s.finalBoss==2 && s.phase2 && s.finalAttack!=5)s.listIndex=1;}
        if(s.t==15) {
            pressed|=Btn::two;
        }
        break;
    case BattlePhase::TacticsMenu:
        if(s.t==10 && plan[(turn+plan.size()-1)%plan.size()]=='S')for(size_t i=0;i<s.tactics.size();++i)if(s.tactics[i]==Tactic::Appeal)s.listIndex=static_cast<int>(i);
        if (s.t == 10 && plan[(turn+plan.size()-1)%plan.size()]!='S') s.listIndex = plan[(turn + plan.size() - 1) % plan.size()] == 'R' ? 1 : 0;
        if(s.t==10) if(const char* tactic=std::getenv("SPM_LP_TEST_TACTIC"))for(size_t i=0;i<s.tactics.size();++i)if(std::string(tactic)==TacticName(s.tactics[i]))s.listIndex=static_cast<int>(i);
        if(s.t==10 && plan[(turn+plan.size()-1)%plan.size()]=='T')
            for(size_t i=0;i<s.tactics.size();++i)if(s.tactics[i]==Tactic::Thoreau)s.listIndex=static_cast<int>(i);
        if(s.t==10 && s.fracktail && plan[(turn+plan.size()-1)%plan.size()]=='B') {
            const bool minions=std::any_of(s.enemies.begin()+1,s.enemies.end(),[](const auto& e){return e.alive;});
            for(size_t i=0;i<s.tactics.size();++i)if(s.tactics[i]==(minions?Tactic::Thoreau:Tactic::Defend))s.listIndex=i;
        }
        if (s.t == 15) pressed |= Btn::two;
        break;
    case BattlePhase::MarioAction:
        {
            if(s.action==Action::Thoreau && s.st==(s.ochunks?30:32) && !std::getenv("SPM_LP_TEST_MISS_THROW"))pressed|=Btn::two;
            const char* miss = std::getenv("SPM_LP_TEST_MISS_BOUNCE");
            const bool hitCommand = !miss || std::atoi(miss) != s.jumpHits + 1;
            if (hitCommand && s.action == Action::Jump && s.stage == 1 && s.st == 28) pressed |= Btn::two;
            if (hitCommand && s.action == Action::Jump && s.stage == 2 && s.jumpLimit > 2 && s.st == 22) pressed |= Btn::two;
            if(s.action==Action::Jump && s.stage==3 && s.st==10)pressed|=Btn::two;
        }
        if (s.action == Action::Hammer && s.stage == 1 && s.gauge < 1.05f) held |= Btn::screenLeft;
        if (s.action == Action::Run && s.st % 3 == 0) pressed |= Btn::two;
        break;
    case BattlePhase::EnemyAction:
        if(std::getenv("SPM_LP_TEST_NO_GUARD"))break;
        if (s.stage == 2 && lastEnemyStage != 2) ++enemyTurns;
        lastEnemyStage = s.stage;
        if (s.stage == 2) {
            const bool super = enemyTurns % 2 == 0;
            if (s.st == (super ? 10 : 7)) pressed |= super ? Btn::one : Btn::two;
        }
        break;
    case BattlePhase::LevelUp:
        if(s.t==20)if(const auto choice=std::getenv("SPM_LP_TEST_LEVEL_CHOICE"))s.levelChoice=std::clamp(std::atoi(choice),0,2);
        if (s.t == 30) pressed |= Btn::two;
        break;
    default: break;
    }
}

bool MenuPhase(BattlePhase p) {
    return p==BattlePhase::Menu || p==BattlePhase::Target || p==BattlePhase::ItemMenu || p==BattlePhase::TacticsMenu ||
           p==BattlePhase::MoveMenu || p==BattlePhase::SpecialMenu || p==BattlePhase::LevelUp;
}
// The game's own menu sounds for the battle menus: cursor, confirm, back, and a buzz when a choice is refused.
void MenuSounds(Guest& g,BattlePhase before,int cursorBefore,bool confirm,bool cancel) {
    if(!MenuPhase(before))return;
    const int cursor=s.menu*4096+s.listIndex*64+s.target*4+s.levelChoice;
    if(s.phase==before) {
        if(s.t==0)Sound(g,"SFX_SYS_MENU_DESIDE1");  // re-entered (the next level-up choice)
        else if(cursor!=cursorBefore)Sound(g,"SFX_SYS_MENU_CURSOR_MOVE1");
        else if(confirm && s.t>5)Sound(g,"SFX_SYS_SELECT_NG1");
        return;
    }
    if(cancel && (s.phase==BattlePhase::Menu || s.phase==BattlePhase::MoveMenu || s.phase==BattlePhase::TacticsMenu))Sound(g,"SFX_SYS_MENU_CANCEL1");
    else if(MenuPhase(s.phase))Sound(g,"SFX_SYS_MENU_OPEN1");
    else Sound(g,"SFX_SYS_MENU_DESIDE1");
}

void Tick(Guest& g) {
    uint32_t pressed = g.Call(Fn::wpadGetButtonsPressed, {0});
    uint32_t held = g.Call(Fn::wpadGetButtonsHeld, {0});
    Autoplay(g,pressed,held);
    // The final bosses' own AI keeps spawning attack scripts; freeze anything new (our dialogue event is exempt).
    if(s.finalBoss)g.Call(Fn::evtStopAll,{0xFFu});
    const bool confirm = pressed & (Btn::two | Btn::a);
    const bool cancel = pressed & (Btn::one | Btn::b);
    ++s.t;
    ++s.st;
    // Menu sounds come from what this frame's input did: compared after the phase logic below.
    const BattlePhase phaseBefore=s.phase;
    const int cursorBefore=s.menu*4096+s.listIndex*64+s.target*4+s.levelChoice;
    static const bool bgmLog = std::getenv("SPM_LP_LOG_BGM") != nullptr;  // the music playing, every 2 s
    if (bgmLog && s.stageFrame % 120 == 1)
        RT_LOGF(RT_TAG_RUNTIME, "[lpbattle] bgm '%s' playing %u\n", GuestString(g.Call(0x8023ABDC, {0})).c_str(),
                g.Call(0x8023ABBC, {0}));
    ++s.bannerT;
    s.whiteFlash=0;
    if(s.pureShield && s.t%12==0)Effect(s.marioHome,s.marioHome,30,ColWhite,16);

    for (auto& e : s.enemies) {
        if(e.npc) {
            e.present = e.present && (Memory::Read32(e.npc + Npc::flag8) & 1);
            if (!e.present) e.alive = false;
        }
        if (e.hurt > 0 && --e.hurt == 0 && e.alive) SetAnim(g, e, 0);
        if(e.blockPose>0)--e.blockPose;
        if(e.barrier>0)--e.barrier;
    }

    switch (s.phase) {
    case BattlePhase::Intro: {
        const float t = std::min(1.f, std::max(0, s.t-42) / 24.f);
        for (auto& e : s.enemies) e.pos = LerpV(e.start, e.slot, Smooth(t));
        SetPose(g, "S_1");
        if (s.t >= 76) {
            if(!s.introTalkDone && (s.ochunks || s.fracktail || s.finalBoss)) {
                s.introTalkDone=true;
                const std::string line=s.ochunks?"stg1_3_006":s.finalBoss==1?"stg8_4_011":s.finalBoss==2?"The destruction begins with you!<k>":"ERROR. Target Mario. Commencing battle.<k>";
                if(StartBattleTalk(g,line,BattlePhase::Menu))break;
            }
            if (s.first == FirstStrike::Mario && !s.enemies.empty()) {
                Pop({s.enemies[0].pos.x, s.enemies[0].pos.y + s.enemies[0].height + 30.f, s.enemies[0].pos.z}, "First Strike!",
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
        const int commands=SpecialOpen()?5:4;  // Special: once the Pure Hearts are back
        if(s.menu>=commands)s.menu=0;
        if (pressed & Btn::screenLeft) s.menu = (s.menu + commands - 1) % commands;
        if (pressed & Btn::screenRight) s.menu = (s.menu + 1) % commands;
        if (confirm && s.t > 4) {
            if (s.menu <= 1) {
                if((s.menu==0 && Has(g,BadgeId::Hammerman)) || (s.menu==1 && Has(g,BadgeId::Jumpman))) {Pop(s.marioPos,"Blocked by equipped badge",ColWhite,1);break;}
                s.target = std::max(0, NextAlive(0));
                BuildMoves(g,s.menu==0);Go(BattlePhase::MoveMenu);
            } else if (s.menu == 2) {
                s.itemSlots.clear();
                const uint32_t pouch = g.PouchPtr();
                for (uint32_t i = 0; i < Pouch::useItemCount; ++i)
                    if (Memory::Read16(pouch + Pouch::useItems + 2 * i) != 0) s.itemSlots.push_back(static_cast<int>(i));
                s.listIndex = 0;
                if (s.itemSlots.empty()) Pop({s.marioPos.x, s.marioPos.y + 50.f, s.marioPos.z}, "No items!", ColWhite, 1);
                else Go(BattlePhase::ItemMenu);
            } else if(s.menu==4) {
                s.listIndex=0;Go(BattlePhase::SpecialMenu);
            } else {
                s.listIndex = 0;
                BuildTactics(g);
                Go(BattlePhase::TacticsMenu);
            }
        }
    } break;
    case BattlePhase::Talk:
        SetPose(g,"S_1");
        if(!DialogueActive(g)) {
            const auto next=s.afterTalk;
            if(next==BattlePhase::Menu)OpenMenu();else Go(next);
        }
        break;
    case BattlePhase::SpecialMenu: {
        const int n=s.finalBoss==2 && s.phase2?2:1;
        if(pressed&Btn::screenLeft)s.listIndex=(s.listIndex+n-1)%n;
        if(pressed&Btn::screenRight)s.listIndex=(s.listIndex+1)%n;
        if(cancel)Go(BattlePhase::Menu);
        else if(confirm && s.t>4) {
            const int cost=s.listIndex==0?50:30;
            if(Badges(g).energy<cost){Pop(s.marioHome,"Not enough Star Energy",ColYellow,1);break;}
            AddStarEnergy(g,-cost);StartAction(g,s.listIndex==0?Action::PureShield:Action::LoversGrief);
        }
    } break;
    case BattlePhase::MoveMenu: {
        const int n=static_cast<int>(s.moves.size());
        if(pressed&Btn::screenLeft)s.listIndex=(s.listIndex+n-1)%n;
        if(pressed&Btn::screenRight)s.listIndex=(s.listIndex+1)%n;
        if(cancel)Go(BattlePhase::Menu);
        else if(confirm && s.t>4){s.move=s.moves[s.listIndex];Go(BattlePhase::Target);}
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
        if (cancel) Go(BattlePhase::MoveMenu);
        else if (confirm && s.t > 4) {
            if(s.menu==1 && s.enemies[s.target].flying && s.move!=BadgeId::HammerThrow) {Pop(s.marioPos,"Use Hammer Throw for fliers",ColWhite,1);break;}
            const int cost=s.move==BadgeId::Count?0:MoveFpCost(g,s.move);
            if(cost && !SpendFp(g,cost)){Pop(s.marioPos,"Not enough FP",ColWhite,1);break;}

            StartAction(g, s.menu == 0 ? Action::Jump : Action::Hammer);
        }
    } break;
    case BattlePhase::ItemMenu: {
        const int n = static_cast<int>(s.itemSlots.size());
        if (pressed & Btn::screenLeft) s.listIndex = (s.listIndex + n - 1) % n;
        if (pressed & Btn::screenRight) s.listIndex = (s.listIndex + 1) % n;
        if (cancel) {if(s.itemChain>0){s.itemChain=0;AfterMarioAction(g);}else Go(BattlePhase::Menu);}
        else if (confirm && s.t > 4) {
            s.itemToUse = s.itemSlots[s.listIndex];
            const int itemId = Memory::Read16(g.PouchPtr() + Pouch::useItems + 2u * static_cast<uint32_t>(s.itemToUse));
            const uint32_t nameMsg = Memory::Read32(kItemDataTable + kItemDataSize * static_cast<uint32_t>(itemId) + 0x10);

            StartAction(g, Action::Item);
        }
    } break;
    case BattlePhase::TacticsMenu: {
        const int n=static_cast<int>(s.tactics.size());
        if(pressed&Btn::screenLeft)s.listIndex=(s.listIndex+n-1)%n;
        if(pressed&Btn::screenRight)s.listIndex=(s.listIndex+1)%n;
        if (cancel) Go(BattlePhase::Menu);
        else if (confirm && s.t > 4) {
            const auto tactic=s.tactics[s.listIndex];
            if(tactic==Tactic::Thoreau && s.ochunks) {
                if(s.oRules.thoreauRest>0){Pop(s.marioPos,"Thoreau needs "+std::to_string(s.oRules.thoreauRest)+" more turn(s)",ColBlue,1);break;}
                s.throwFrom=s.enemies[0].pos;s.throwPos=s.throwFrom;
                StartAction(g,Action::Thoreau);
            } else if(tactic==Tactic::Thoreau) {
                s.throwIndex=-1;for(int i=1;i<static_cast<int>(s.enemies.size());++i)if(s.enemies[i].alive){s.throwIndex=i;break;}
                if(s.throwIndex<0){Pop(s.marioPos,"Wait for Fracktail to summon Frackles",ColBlue,1);break;}
                s.throwFrom=s.enemies[s.throwIndex].pos;s.throwPos=s.throwFrom;
                StartAction(g,Action::Thoreau);
            } else if (tactic==Tactic::Defend) {
                // +1 defense through the enemies' turn (OpenMenu clears it).
                s.defending = true;
                s.action=Action::Defend;
                Pop({s.marioPos.x, s.marioPos.y + 50.f, s.marioPos.z}, "DEF +1", ColBlue, 1);
                AfterMarioAction(g);
            } else if(tactic==Tactic::Run) {
                if(s.fracktail || s.ochunks || s.finalBoss){Pop(s.marioPos,"Cannot flee a boss battle",ColWhite,1);break;}
                StartAction(g, Action::Run);
            } else if(tactic==Tactic::Charge || tactic==Tactic::DoubleDip) {
                const auto id=tactic==Tactic::Charge?BadgeId::Charge:BadgeId::DoubleDip;
                if(id==BadgeId::DoubleDip){OpenItems(g);if(s.itemSlots.empty()){Pop(s.marioPos,"No items!",ColWhite,1);Go(BattlePhase::TacticsMenu);break;}}
                if(!SpendFp(g,MoveFpCost(g,id))){Pop(s.marioPos,"Not enough FP",ColWhite,1);Go(BattlePhase::TacticsMenu);break;}
                if(id==BadgeId::Charge)StartAction(g,Action::Charge);
                else {s.itemChain=2;s.itemUses=0;}
            } else if(tactic==Tactic::Pulse) {
                if(Badges(g).energy<100){Pop(s.marioPos,"Need 100 Star Energy",ColYellow,1);break;}
                AddStarEnergy(g,-100);StartAction(g,Action::Pulse);
            } else {
                StartAction(g,tactic==Tactic::Recharge?Action::Recharge:tactic==Tactic::Appeal?Action::Appeal:tactic==Tactic::Swap?Action::Swap:Action::Tattle);
            }
        }
    } break;
    case BattlePhase::MarioAction: {
        Enemy* e = (s.target >= 0 && s.target < static_cast<int>(s.enemies.size())) ? &s.enemies[s.target] : nullptr;
        // Before he attacks, Mario squares up with SPM's angry pose and face (S_5).
        if (s.windup > 0) {
            SetPose(g, "S_5");
            s.marioPos = s.marioHome;
            if (--s.windup == 0) StartStage(0);
            break;
        }
        switch (s.action) {
        case Action::Jump: if (e) TickJump(g, *e, pressed); else AfterMarioAction(g); break;
        case Action::Hammer: if (e) TickHammer(g, *e, held); else AfterMarioAction(g); break;
        case Action::Item: TickItem(g); break;
        case Action::Thoreau:if(s.ochunks)TickThoreauOChunks(g,pressed);else TickThoreau(g,pressed);break;
        case Action::Run: TickRun(g, pressed); break;
        case Action::PureShield:case Action::LoversGrief:case Action::Charge:case Action::Appeal:case Action::Recharge:case Action::Pulse:case Action::Swap:case Action::Tattle:TickUtility(g);break;
        default: AfterMarioAction(g); break;
        }
    } break;
    case BattlePhase::EnemyAction: {
        if (s.marioHurt <= 0) SetPose(g, "S_1");
        TickEnemy(g, pressed);
    } break;
    case BattlePhase::Victory: {
        if(!s.defeatTalkDone && (s.ochunks || s.finalBoss)) {
            s.defeatTalkDone=true;
            const std::string line=s.ochunks?"stg1_3_014":s.finalBoss==1?"stg8_4_054":"Impossible... the power of love...<k>";
            if(StartBattleTalk(g,line,BattlePhase::Victory))break;
        }
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
            sp = (s.finalBoss?40:s.fracktail?20:s.ochunks?15:std::clamp(sp, 1, 100)) + bonus;
            s.starPointsGained = sp;
            p.starPoints += sp;
            while (p.starPoints >= kStarPointsPerLevel && p.level < kMaxLevel) {
                p.starPoints -= kStarPointsPerLevel;
                ++p.level;
                ++s.levelUps;
            }
            if (p.level >= kMaxLevel) p.starPoints = 0;
            SaveProgress(g, p);
            const int count=s.enemiesAtStart;
            if(Has(g,BadgeId::HeartFinder))HeroHeal(g,2*count*Has(g,BadgeId::HeartFinder));
            if(Has(g,BadgeId::FlowerFinder)){RecoverFp(g,2*count*Has(g,BadgeId::FlowerFinder));Effect(s.marioHome,s.marioHome,9,ColGreen);}
            if(Has(g,BadgeId::MoneyMoney)){g.Call(0x8014CA98,{static_cast<uint32_t>(count*5)});Pop(s.marioHome,"Bonus coins!",ColYellow,1);}
            if(Has(g,BadgeId::ItemHog) && Roll()<50){const auto pouch=g.PouchPtr();for(uint32_t i=0;i<Pouch::useItemCount;++i)if(!Memory::Read16(pouch+Pouch::useItems+2*i)){Memory::Write16(pouch+Pouch::useItems+2*i,0x50);Pop(s.marioHome,"Found a Mushroom!",ColGreen,1);break;}}
            SaveBadges(g);
            Pop({s.marioPos.x, s.marioPos.y + 60.f, s.marioPos.z}, "+" + std::to_string(sp) + " Star Points", ColYellow, 1);
            Sound(g,"SFX_SYS_COUNT_COIN1");
        }
        if (s.t >= 150 || (s.t > 60 && confirm)) {
            if (s.levelUps > 0) {
                Banner("LEVEL UP!");
                Sound(g,"SFX_SYS_LEVELUP_FIREWORKS1_2");
                s.levelChoice = 0;
                Go(BattlePhase::LevelUp);
            } else {
                Finish(g);
            }
        }
    } break;
    case BattlePhase::LevelUp: {
        if(pressed&(Btn::screenUp|Btn::screenLeft))s.levelChoice=(s.levelChoice+2)%3;
        if(pressed&(Btn::screenDown|Btn::screenRight))s.levelChoice=(s.levelChoice+1)%3;
        if (confirm && s.t > 20) {
            ApplyLevelUp(g,static_cast<LevelUpChoice>(s.levelChoice));
            Pop({s.marioPos.x, s.marioPos.y + 60.f, s.marioPos.z},s.levelChoice==0?"HP +5":s.levelChoice==1?"ATK +1":"BP +3", ColGreen, 1);
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
    MenuSounds(g,phaseBefore,cursorBefore,confirm,cancel);
    if(s.phase==BattlePhase::Menu && !s.pendingTalk.empty()) {
        const auto message=s.pendingTalk;s.pendingTalk.clear();StartBattleTalk(g,message,BattlePhase::Menu);
    }

    if (s.marioHurt > 0) {
        --s.marioHurt;
        SetPose(g, "D_2");
    }
    if(s.guardPose>0){--s.guardPose;SetPose(g,"S_1");}
    // The stage: the map hides and the area's pop-up diorama stands up behind the fighters.
    if(s.phase!=BattlePhase::Intro || s.t>=28) {
        if(++s.stageFrame==1 && !TestAreaActive())g.Call(Fn::mapDispOff);
        SetStage(g,2,s.marioHome);
        ParkFieldObjects(g);
    }
    // Hold everyone where the battle wants them.
    const uint32_t mario = g.MarioPtr();
    WriteVec3(mario + Mario::position, s.marioPos);
    WriteVec3(mario+0x155C,{s.marioPos.x+s.side*100.f,s.groundY,s.marioPos.z});
    Memory::Write32(mario+8,Memory::Read32(mario+8)|0x24u); // front paper pose + locked facing target
    Memory::WriteFloat32(mario+0x174,s.side>0?90.f:270.f);Memory::WriteFloat32(mario+0x178,s.side>0?90.f:270.f);
    Memory::WriteFloat32(mario+0x180,s.side>0?180.f:0.f);Memory::WriteFloat32(mario+0x184,s.side>0?180.f:0.f);
    Vec3 scale=s.marioScale,rotation=s.marioRotation;
    if(s.guardPose>0 || s.defending){scale.y*=s.guardPose>0?.76f:.9f;scale.x*=s.guardPose>0?1.12f:1.03f;rotation.z+=s.side*(s.guard==2?18.f:7.f);}
    else if(s.marioHurt>0){scale.x*=.9f;rotation.z+=s.side*std::sin(s.marioHurt*.8f)*9.f;}
    else if(s.marioPose=="S_1" || s.marioPose=="S_5") {
        // TTYD's battle stance: a steady knee-bend bounce, anchored at the feet (quicker and deeper while he winds up).
        const bool windup=s.marioPose=="S_5";
        const float bob=std::sin(static_cast<float>(s.stageFrame)*(windup?.42f:.16f));
        scale.y*=1.f+(windup?.05f:.035f)*bob;scale.x*=1.f-(windup?.03f:.02f)*bob;
        if(windup)rotation.z-=s.side*4.f;  // leaning in
    }
    else if(s.phase==BattlePhase::MarioAction && s.action==Action::Hammer && s.stage==2){rotation.z+=s.side*std::sin(s.st/14.f*3.14159265f)*-18.f;}
    WriteVec3(mario+0xBC,scale);WriteVec3(mario+0xB0,rotation);
    BadgeFieldTick(g);
    for (auto& e : s.enemies) if(e.present) {
        Vec3 place=e.pos;
        if(e.hurt>0)place.x+=std::sin(e.hurt*1.7f)*2.f;
        const auto old=ReadVec3(e.npc+Npc::position);
        const Vec3 delta{place.x-old.x,place.y-old.y,place.z-old.z};
        WriteVec3(e.npc+Npc::position,place);
        for(size_t index=0;index<e.parts.size();++index) {
            const auto& part=e.parts[index];
            if(!Memory::Contains(part.ptr,0x398))continue;
            const auto pos=ReadVec3(part.ptr+8);WriteVec3(part.ptr+8,{pos.x+delta.x,pos.y+delta.y,pos.z+delta.z});
            const auto owner=ReadVec3(part.ptr+0x14);WriteVec3(part.ptr+0x14,{owner.x+delta.x,owner.y+delta.y,owner.z+delta.z});
            int alpha=e.alive?255:std::clamp(e.hurt*14,0,255);
            // Bleck's barrier: invisible, flashing up for the moment it turns a hit away (gone in phase 2).
            if(&e==&s.enemies[0] && static_cast<int>(index)==s.shieldPart) {
                // While a block's flash runs the actor animates its barrier itself (only if that hasn't started a few
                // frames in does the battle flash the part up). Its frozen AI never lowers it again, so when the flash
                // ends the battle clears the hit flag and hides it; once broken (phase 3) it stays hidden.
                const int native=Memory::Read8(part.ptr+0x10F);
                if(s.phase2 || e.barrier<=0) {
                    alpha=0;
                    if(Memory::Read32(part.ptr+0x2A8)==1)Memory::Write32(part.ptr+0x2A8,0);
                }
                else if(native>0 || e.barrier>BleckRules::BarrierFlashFrames-4)alpha=native;
                else alpha=std::clamp(e.barrier*16,0,255);
                static const bool log=std::getenv("SPM_LP_LOG_BARRIER")!=nullptr;
                if(log && e.barrier>0)RT_LOGF(RT_TAG_RUNTIME,"[lpfinal] barrier frame %d native alpha %d work0 %d\n",e.barrier,native,Memory::Read32(part.ptr+0x2A8));
            }
            Memory::Write8(part.ptr+0x10F,static_cast<uint8_t>(alpha));
            Vec3 rotation=part.rotation,scale=part.scale;
            rotation.y=s.side>0?0.f:180.f;
            if(e.status[Shrink]){scale.x*=.6f;scale.y*=.6f;}
            if(e.blockPose>0){scale.y*=.8f;scale.x*=1.12f;}
            if(!e.alive)scale.y*=std::max(.05f,e.hurt/18.f);
            WriteVec3(part.ptr+0xC4,rotation);WriteVec3(part.ptr+0xF4,scale);
        }
        Memory::Write32(e.npc+Npc::flag8,(Memory::Read32(e.npc+Npc::flag8)|1u)&~0x80000000u);
        Memory::Write32(e.npc+0x298,Memory::Read32(mario+0x120));Memory::Write32(e.npc+0x29C,0xFFFFFFFFu);
        Memory::Write8(e.npc+0xFF,static_cast<uint8_t>(e.alive?255:std::clamp(e.hurt*14,0,255)));
        Vec3 enemyScale=e.savedScale,enemyRotation=e.savedRotation;
        enemyRotation.y=s.side>0?0.f:180.f;
        if(e.status[Shrink]){enemyScale.x*=.6f;enemyScale.y*=.6f;}
        if(e.blockPose>0){enemyScale.y*=.8f;enemyScale.x*=1.12f;}
        if(!e.alive){enemyScale.y*=std::max(.05f,e.hurt/18.f);enemyRotation.z+=s.side*(18-e.hurt)*5.f;}
        WriteVec3(e.npc+0xB4,enemyRotation);WriteVec3(e.npc+0xE4,enemyScale);
    }
    if(s.fracktail && (s.phase!=BattlePhase::Intro || s.t>=28)) {
        const float y=s.groundY+(s.bossRules.Vulnerable()?FracktailLayout::LoweredRootY:FracktailLayout::RaisedRootY);
        s.bossRoot.y+=(y-s.bossRoot.y)*.12f;
        s.enemies[0].slot={s.marioHome.x+FracktailLayout::RestX,y+FracktailLayout::AntennaOffsetY,s.marioHome.z};
        s.enemies[0].pos={s.bossRoot.x,s.bossRoot.y+FracktailLayout::AntennaOffsetY,s.bossRoot.z};
        Vec3 antenna;if(FracktailAntenna(g,antenna))s.enemies[0].pos=antenna;
        FracktailVisual v;v.active=true;v.lowered=s.bossRules.Vulnerable();v.root=s.bossRoot;v.facing=0;
        v.floor=s.marioHome;
        v.partner={s.marioHome.x-25,s.groundY+32,s.marioHome.z};
        for(int i=0;i<2;++i){v.minions[i]=s.enemies[i+1].pos;v.minionAlive[i]=s.enemies[i+1].alive;v.minionHurt[i]=s.enemies[i+1].hurt>0;}
        v.breathing=s.phase==BattlePhase::EnemyAction && s.enemyTurn==0 && s.bossAttack==FracktailAttack::Breath && s.stage==2;
        v.charging=s.phase==BattlePhase::EnemyAction && s.enemyTurn==0 && s.bossAttack==FracktailAttack::Charge && (s.stage>0 || s.st>=18);
        if(v.charging)v.root.y=s.groundY+65.f;
        v.throwing=s.phase==BattlePhase::MarioAction && s.action==Action::Thoreau && s.st>=10 && s.st<=48;v.thrown=s.throwPos;
        UpdateFracktailVisual(g,v);
        if(s.glitchFrames>0)--s.glitchFrames;
    }
    if(s.finalBoss && (s.phase!=BattlePhase::Intro || s.t>=28)) {
        FinalBossVisual view;view.active=true;view.kind=s.finalBoss;view.drawBoss=!s.enemies[0].present;
        view.root={s.marioHome.x+(s.finalBoss==1?180.f:230.f),s.groundY+(s.finalBoss==1?35.f:0.f),s.marioHome.z};
        if(s.enemies[0].present)view.root=s.enemies[0].pos;  // helpers gather around the real actor
        view.scale=s.finalBoss==1?1.7f:1.15f;
        view.attacking=s.phase==BattlePhase::EnemyAction && s.enemyTurn==0 && s.stage==1;
        view.absorb=s.finalBoss==1 && s.phase2 && s.finalAttack==5 && s.phase==BattlePhase::EnemyAction && s.enemyTurn==0;
        view.useHelperPositions=true;
        if(s.finalBoss==1)for(size_t i=1;i<s.enemies.size() && i<=3;++i)if(s.enemies[i].alive)view.voidSpritePositions[view.voidSprites++]=s.enemies[i].pos;
        if(s.finalBoss==2 && s.enemies.size()>1 && s.enemies[1].alive){
            view.miniLuigis=6;view.miniLuigiRoot=s.enemies[1].pos;
            view.miniLuigisAttacking=s.phase==BattlePhase::EnemyAction && s.enemyTurn==1;
        }
        UpdateFinalBossVisual(g,view);
        Vec3 target;if(FinalBossTarget(g,target)){s.enemies[0].pos=target;s.enemies[0].height=0;}
    }
    for(auto& fx:s.effects)++fx.age;
    s.effects.erase(std::remove_if(s.effects.begin(),s.effects.end(),[](const auto& fx){return fx.age>=fx.life;}),s.effects.end());
    for (auto& p : s.popups) {
        ++p.age;
        p.world.y += p.kind == 0 ? 0.6f : 0.3f;
    }
    s.popups.erase(std::remove_if(s.popups.begin(), s.popups.end(), [](const Popup& p) { return p.age > 70; }),
                   s.popups.end());
}

// The game's HUD (hudMain's work at r13-0x7E00): hudHide sets flags 0xA000 (0x2000 while hidden), showing it again
// 0x5000; +0x24 is how far it has slid away (0 shown, 150 gone). The Star Points counter shows only while that HUD is up and Mario is in control.
void FieldHudState(Guest& g,BattleUi& ui) {
    uint32_t work=0;
    if(!Memory::TryRead32(g.Sda()-0x7E00,work) || !Memory::Contains(work,0x28))return;
    const uint32_t flags=Memory::Read32(work);
    const float slideX=Memory::ReadFloat32(work+0x20),slideY=Memory::ReadFloat32(work+0x24);
    const uint32_t mario=g.MarioPtr();
    const bool cutscene=Memory::Contains(mario,0x130) && Memory::Read8(mario+Mario::keyOff)!=0;
    const bool talking=MessageWindowOpen(g);
    const bool hidden=(flags&0x2000)!=0;
    ui.fieldHud=!cutscene && !talking && !hidden;
    ui.hudSlide=std::clamp(slideY,0.f,150.f);
    static const bool log=std::getenv("SPM_LP_LOG_HUD")!=nullptr;
    static uint32_t lastFlags=~0u;static float lastX=-1,lastY=-1;static bool lastShown=false;
    if(log && (flags!=lastFlags || slideX!=lastX || slideY!=lastY || ui.fieldHud!=lastShown)) {
        lastFlags=flags;lastX=slideX;lastY=slideY;lastShown=ui.fieldHud;
        RT_LOGF(RT_TAG_RUNTIME,"[lphud] flags %08X slide %.1f %.1f cutscene %d talking %d -> shown %d\n",flags,slideX,slideY,cutscene,talking,ui.fieldHud);
    }
}

void Publish(Guest& g) {
    static uint32_t serial = 0;
    BattleUi ui;
    ui.serial = ++serial;
    ui.active = s.active;
    ui.phase = s.phase;
    ui.phaseFrame = s.t;
    ui.powerBounce = s.active?s.action==Action::Jump && s.move==BadgeId::PowerBounce:PowerBounceEquipped(g);
    ui.jumpHits = s.jumpHits;
    ui.fp=BattleFp(g);ui.maxFp=BattleMaxFp(g);ui.energy=Badges(g).energy;ui.round=s.round;
    ui.bp=BadgeBpMax(g);ui.bpUsed=BadgeBpUsed(g);ui.pickupNotice=BadgePickupNotice();
    ui.revealHp=s.reveal || Has(g,BadgeId::Peekaboo);ui.timingTutor=Has(g,BadgeId::TimingTutor)!=0;
    ui.moveMenuType=s.menu;ui.hasSpecial=SpecialOpen();ui.finalBoss=s.finalBoss;ui.pureShield=s.pureShield;ui.whiteFlash=s.whiteFlash;
    ui.fracktail=s.fracktail;ui.weakPointOpen=s.bossRules.Vulnerable();ui.glitchFrames=s.glitchFrames;
    const uint32_t pouch = g.PouchPtr();
    // Only in a map (not the title, file select or map changes).
    if (Memory::Contains(pouch, 0x80) && g.Call(Fn::seqGetSeq) == 2) {
        ui.hp = static_cast<int32_t>(Memory::Read32(pouch + Pouch::hp));
        ui.maxHp = BattleMaxHp(g);
        ui.attack = s.active?MarioAttack(g):static_cast<int32_t>(Memory::Read32(pouch + Pouch::attack));
        const Progress p = LoadProgress(g);
        ui.level = p.level;
        ui.starPoints = p.starPoints;
        if(!s.active)FieldHudState(g,ui);
    }
    ui.levelPicker=LevelPickerOpen();ui.levelPick=LevelPickerLevel();
    if (s.active) {
        const float screenYScale=s.fracktail?1.f:480.f/528.f;
        g.ScreenPos(s.marioPos, ui.mario.x, ui.mario.y);
        ui.mario.y*=screenYScale;
        ui.mario.alive = true;
        if(s.slowTurns)ui.mario.statuses.push_back("Time slow "+std::to_string(s.slowTurns));
        if(s.debuffTurns)ui.mario.statuses.push_back("Void weakness "+std::to_string(s.debuffTurns));
        constexpr const char* heroStatusNames[]={"Sleep","Frozen","Small","Soft","Dizzy","Burn","Poison","Circuit -ATK"};
        for(int i=0;i<StatusCount;++i)if(s.status[i])ui.mario.statuses.push_back(std::string(heroStatusNames[i])+" "+std::to_string(s.status[i]));
        for (const auto& e : s.enemies) {
            BattleUi::Actor a;
            g.ScreenPos(e.pos, a.x, a.y);
            a.y*=screenYScale;
            a.hp = e.hp;
            a.maxHp = e.maxHp;
            a.alive = e.alive;
            a.name = e.name;
            a.flying=e.flying;a.spiky=e.spiky;a.fiery=e.fiery;a.defense=std::max(0,e.defense-(e.status[Soft]?3:0));
            constexpr const char* statusNames[]={"Sleep","Frozen","Small","Soft","Dizzy","Burn","Poison","Circuit -ATK"};
            for(int i=0;i<StatusCount;++i)if(e.status[i])a.statuses.push_back(std::string(statusNames[i])+" "+std::to_string(e.status[i]));
            ui.enemies.push_back(a);
        }
        ui.menuIndex = s.menu;
        ui.targetIndex = s.phase == BattlePhase::Target ? s.target : -1;
        if(s.phase==BattlePhase::SpecialMenu) {
            ui.list={"Pure Shield  [50 Energy]"};
            if(s.finalBoss==2 && s.phase2)ui.list.push_back("Lover's Grief  [30 Energy]");
            ui.listIndex=s.listIndex;
        } else if(s.phase==BattlePhase::MoveMenu) {
            for(const auto id:s.moves)ui.list.push_back(id==BadgeId::Count?(s.menu==0?"Jump  [0 FP]":"Hammer  [0 FP]"):
                std::string(kBadges[static_cast<int>(id)].name)+"  ["+std::to_string(MoveFpCost(g,id))+" FP]");
            ui.listIndex=s.listIndex;
        } else if (s.phase == BattlePhase::ItemMenu) {
            for (int slot : s.itemSlots) {
                const int itemId = Memory::Read16(pouch + Pouch::useItems + 2u * static_cast<uint32_t>(slot));
                const uint32_t nameMsg = Memory::Read32(kItemDataTable + kItemDataSize * static_cast<uint32_t>(itemId) + 0x10);
                ui.list.push_back(nameMsg ? GuestString(g.Call(Fn::msgSearch, {nameMsg})) : "???");
            }
            ui.listIndex = s.listIndex;
        } else if (s.phase == BattlePhase::TacticsMenu) {
            for(const auto tactic:s.tactics) {
                std::string text=TacticName(tactic);
                if(tactic==Tactic::Charge || tactic==Tactic::DoubleDip)text+="  ["+std::to_string(MoveFpCost(g,tactic==Tactic::Charge?BadgeId::Charge:BadgeId::DoubleDip))+" FP]";
                if(tactic==Tactic::Pulse)text+=" [100 Energy]";
                if(tactic==Tactic::Swap && Has(g,BadgeId::QuickChange))text+=" [Free]";
                ui.list.push_back(text);
            }
            ui.listIndex = s.listIndex;
        }
        if (s.phase == BattlePhase::MarioAction) {
            if(s.action==Action::Thoreau){ui.prompt=UiPrompt::PressA;ui.promptLit=s.st>=24 && s.st<=38;}
            if (s.action == Action::Jump && (s.stage == 1 || (s.stage == 2 && s.jumpLimit > 2 && s.jumpHits + 1 < s.jumpLimit))) {
                ui.prompt = UiPrompt::PressA;
                ui.promptLit = s.st >= (s.stage == 1 ? 32 : 26)-BattleWindow(8,Has(g,BadgeId::Simplifier),Has(g,BadgeId::Unsimplifier));
            } else if(s.action==Action::Jump && s.stage==3 && ui.timingTutor && !s.stylish) {
                ui.prompt=UiPrompt::Stylish;ui.promptLit=s.st>=8 && s.st<=16;
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
            up.y*=screenYScale;
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
        for(const auto& fx:s.effects) {
            BattleUi::Effect effect;g.ScreenPos(fx.from,effect.x,effect.y);g.ScreenPos(fx.to,effect.tx,effect.ty);
            effect.y*=screenYScale;effect.ty*=screenYScale;effect.type=fx.type;effect.age=fx.age;effect.life=fx.life;effect.color=fx.color;
            ui.effects.push_back(effect);
        }
    }
    std::lock_guard lock{g_uiMutex};
    g_ui = std::move(ui);
}

}  // namespace

void RequestFracktailBattle(){fracktailRequested=true;}

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
        if(Has(g,BadgeId::BumpAttack) && MakeEnemy(g,npc).level<LoadProgress(g).level) {
            const auto part=Memory::Read32(npc+Npc::parts);
            Memory::Write32(npc+Npc::hp,1);
            g_allowGameDamage=true;g.Call(Fn::npcTakeDamage,{0,part,7,99,0,0});g_allowGameDamage=false;
            ctx->gpr[3]=0;return;
        }
        s.pendingNpc = npc;
        s.pendingFirst = Has(g,BadgeId::ChillOut)?FirstStrike::None:FirstStrike::Enemy;
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
        if(Has(g,BadgeId::FirstAttack) && MakeEnemy(g,victim).level<LoadProgress(g).level) {
            Memory::Write32(victim+Npc::hp,1);
            ctx->gpr[6]=99;func_801C5568(ctx);return;
        }
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
    if (!s.active) {TestAreaFieldTick(g);BadgeFieldTick(g);}
    if (s.pendingNpc && !s.active) {
        const FirstStrike first = s.pendingFirst;
        Begin(g);
        s.first = first;
        RT_LOGF(RT_TAG_RUNTIME, "[lpbattle] first strike: %s\n", first == FirstStrike::Mario ? "Mario" : "enemy");
        s.pendingNpc = 0;
    }
    if(!s.active && CanStartBattle(g)) {
        static int ready=0;
        const auto gp=Memory::Read32(0x8056D198);
        uint32_t boss=0;
        if(GuestString(gp+0x44,32)=="he3_04" && static_cast<int32_t>(Memory::Read32(gp+0x140))==33) {
            const auto base=g.Call(Fn::npcGetEntries),count=g.Call(Fn::npcGetMaxEntries);
            for(uint32_t i=0;i<count && !boss;++i){const auto npc=base+i*Npc::size;if((Memory::Read32(npc+Npc::flag8)&1) && IsOChunks(npc))boss=npc;}
        }
        ready=boss?ready+1:0;
        if(ready==30){ready=0;BeginOChunks(g,boss);}
    }
    // Count Bleck and Super Dimentio: the map's own intro hands over to a live
    // tribe actor with the native defeat callback installed. Once that gate is
    // stable, freeze the story scripts and run the turn-based adaptation.
    static int finalGate=0,finalAbsent=0;
    static bool finalGateDone=false;
    if(!s.active) {
        const auto gp=Memory::Read32(0x8056D198);
        const std::string map=gp?GuestString(gp+0x44,32):std::string{};
        if(map!="ls4_11") {
            finalGate=0;finalGateDone=false;
        } else if(CanStartBattle(g)) {
            const int gsw=static_cast<int32_t>(Memory::Read32(gp+0x140));
            int kind=0;uint32_t npc=0;bool opening=false;
            if(gsw==BleckRules::EntryGsw0 && FinalBossNpc(g,1,npc))kind=1;
            else if(gsw==DimentioRules::FightGsw0 && FinalBossNpc(g,2,npc))kind=2;
            else if(gsw>=DimentioRules::OpeningGsw0First && gsw<=DimentioRules::OpeningGsw0Last && FinalBossOpeningNpc(g,npc)){kind=2;opening=true;}
            if(g_intermission.kind==1)kind=0;  // Bleck's scene is still on
            finalGate=kind?finalGate+1:0;
            finalAbsent=kind?0:finalAbsent+1;
            if(finalAbsent>=60)finalGateDone=false;
            if(kind && !finalGateDone && finalGate>=30) {
                finalGate=0;finalGateDone=true;Begin(g,false,npc,kind);
                // Super Dimentio's first form is the barrier phase; the 415 fight comes after the Pure Hearts return.
                if(kind==2 && !opening){g_intermission={};StartFinalPhase2(g);}
                s.opening=opening;
            }
        }
    }
    // Bleck's Pure Hearts scene: once it ends, the battle picks up where it left off, his barrier gone.
    if(!s.active && g_intermission.kind==1) {
        const bool running=g_intermission.eventId && g.Call(0x800D9D58,{g_intermission.eventId});  // evtCheckID
        const uint32_t npc=g_intermission.npc;
        if(running)g_intermission.frames=0;
        else if(!Memory::Contains(npc,Npc::size) || !(Memory::Read32(npc+Npc::flag8)&1))g_intermission={};
        else if(++g_intermission.frames>=20 && g.Call(Fn::seqGetSeq)==2) {
            const int hp=g_intermission.bossHp,stage=g_intermission.stage+1;
            g_intermission={};
            Begin(g,false,npc,1);
            s.enemies[0].hp=std::clamp(hp,1,s.enemies[0].maxHp);
            s.introTalkDone=true;
            s.bleckStage=stage;
            if(stage>=3)StartFinalPhase2(g);  // the barrier is broken
            RT_LOGF(RT_TAG_RUNTIME,"[lpfinal] battle resumes after Bleck's scene in stage %d, HP %d\n",stage,s.enemies[0].hp);
        }
    }
    static bool bossFixture=false;
    if(!s.active && TestAreaActive() && CanStartBattle(g)) {
        if(!bossFixture && std::getenv("SPM_LP_TEST_FRACKTAIL") && !TestAreaRoster().empty()){bossFixture=true;fracktailRequested=true;}
        if(fracktailRequested.exchange(false))Begin(g,true);
    }
    static bool storyFixture=false;
    if(!s.active && !storyFixture && std::getenv("SPM_LP_TEST_FRACKTAIL_STORY") && g.Call(Fn::seqGetSeq)==2) {
        const auto gp=Memory::Read32(0x8056D198);
        if(GuestString(gp+0x44)=="he4_10" && Memory::Read32(gp+0x140)<52) {
            const auto base=g.Call(Fn::npcGetEntries),count=g.Call(Fn::npcGetMaxEntries);
            for(uint32_t i=0;i<count;++i){const auto npc=base+i*Npc::size;if((Memory::Read32(npc+Npc::flag8)&1) && GuestString(npc+Npc::name)=="zun"){storyFixture=true;g.Call(Fn::marioKeyOn);Begin(g,true,npc);break;}}
        }
    }
    // Fracktail's intro: the battle takes over as soon as his last line (stg1_4_012) has closed, while the scene's camera
    // pans before he digs under the sand; the old hand-over (Mario free to move, boss music on) stays as the fallback.
    bool zunCue=false;
    if(!s.active && Enabled() && !s.cooldown && g.Call(Fn::seqGetSeq)==2) {
        const auto gp=Memory::Read32(0x8056D198);
        if(GuestString(gp+0x44)=="he4_10" && Memory::Read32(gp+0x140)<52)
            zunCue=EventRunningBetween(g,FracktailIntro::AfterLastLine,FracktailIntro::BeforeDig);
        if(zunCue)RT_LOGF(RT_TAG_RUNTIME,"[lpfracktail] intro: last line closed, taking over before the dig\n");
    }
    if(!s.active && (zunCue || (CanStartBattle(g) && GuestString(g.Call(0x8023ABDC,{0}))=="BGM_BTL_BOSS_STG1"))) {
        const auto gp=Memory::Read32(0x8056D198);
        if(GuestString(gp+0x44)=="he4_10" && Memory::Read32(gp+0x140)<52) {
            uint32_t bossNpc=0;const auto base=g.Call(Fn::npcGetEntries),count=g.Call(Fn::npcGetMaxEntries);
            for(uint32_t i=0;i<count;++i){const auto npc=base+i*Npc::size;if(Memory::Read32(npc+Npc::flag8)&1)if(Memory::Read32(npc+Npc::tribeId)==313 || GuestString(npc+Npc::name)=="zun"){bossNpc=npc;break;}}
            if(bossNpc)Begin(g,true,bossNpc);
        }
    }
    if (!s.active) {ProbeTick(g);ScenePoseTick(g);CutsceneArrivalTick(g);}
    static bool finalFixture=false;
    if(!s.active && !finalFixture && CanStartBattle(g))if(const char* kind=std::getenv("SPM_LP_TEST_FINAL_BOSS")) {
        finalFixture=true;Begin(g,false,0,std::clamp(std::atoi(kind),1,2));
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

// The boss owns a positioned, animated Thoreau actor; keep field followers out of that stage.
extern "C" void func_8011D410(CpuContext* ctx);
extern "C" void fairyDisp_LostPagesFracktail(CpuContext* ctx) {
    if(s.active && s.fracktail)return;
    func_8011D410(ctx);
}
REGISTER_NATIVE_FUNCTION_AS(0x8011D410,fairyDisp_LostPagesFracktail,"fairyDisp_LostPagesFracktail");

// The story's Fracktail is drawn by its own boss code (two display callbacks registered by its controller), not
// as an NPC: hide it once the battle's cover is up, so only the battle's dragon is on the stage.
extern "C" void func_801DC434(CpuContext* ctx);
extern "C" void func_801DD8C8(CpuContext* ctx);
bool HideNativeFracktail() {return s.active && s.fracktail && (s.phase!=BattlePhase::Intro || s.t>=18);}
extern "C" void zunDisp_LostPages(CpuContext* ctx) {if(!HideNativeFracktail())func_801DC434(ctx);}
REGISTER_NATIVE_FUNCTION_AS(0x801DC434,zunDisp_LostPages,"zunDisp_LostPages");
extern "C" void zunDisp2_LostPages(CpuContext* ctx) {if(!HideNativeFracktail())func_801DD8C8(ctx);}
REGISTER_NATIVE_FUNCTION_AS(0x801DD8C8,zunDisp2_LostPages,"zunDisp2_LostPages");

// Normalize apparent scale and stage placement across native room cameras.
// Restore before each update so the projection adjustment cannot accumulate.
extern "C" void func_800550F0(CpuContext* ctx);
extern "C" void camMain_LostPagesBoss(CpuContext* ctx) {
    RestoreBossCamera();
    func_800550F0(ctx);
    if(!s.active || (!s.fracktail && !s.finalBoss) || (s.phase==BattlePhase::Intro && s.t<28))return;
    Guest g(ctx);
    if(g.Call(Fn::seqGetSeq)!=2)return;
    const auto camera=g.Call(0x80055A0C,{Memory::Read32(g.MarioPtr()+0x120)});
    if(!Memory::Contains(camera,0x198))return;
    float x=0,y=0,aboveX=0,aboveY=0;
    if(!g.ScreenPos(s.marioHome,x,y) || !g.ScreenPos({s.marioHome.x,s.marioHome.y+100.f,s.marioHome.z},aboveX,aboveY))return;
    const float units=std::abs(y-aboveY)/100.f;
    if(!std::isfinite(units) || units<.001f)return;
    bossCamera.camera=camera;
    for(unsigned i=0;i<bossCamera.packedProjection.size();++i)bossCamera.packedProjection[i]=Memory::Read32(camera+0x38+i*4);
    for(unsigned i=0;i<bossCamera.projection.size();++i)bossCamera.projection[i]=Memory::Read32(camera+0x94+i*4);
    for(unsigned i=0;i<bossCamera.view.size();++i)bossCamera.view[i]=Memory::Read32(camera+0x54+i*4);
    // Normalize the USA battle picture (608 x 480), not the room's camera distance.
    // Super Dimentio towers over the stage: pull the camera back so his whole body (and the face) fits.
    const float scale=std::clamp((s.finalBoss==2?.92f:1.3f)/units,.1f,10.f);
    for(unsigned offset:{0x94u,0xA8u,0x3Cu,0x44u})Memory::WriteFloat32(camera+offset,Memory::ReadFloat32(camera+offset)*scale);
    g.ScreenPos(s.marioHome,x,y);
    const float width=std::max(1.f,Memory::ReadFloat32(camera+0x28));
    const float height=std::max(1.f,Memory::ReadFloat32(camera+0x2C));
    const bool ortho=Memory::Read32(camera+0x158)==1;
    const float viewZ=Memory::ReadFloat32(camera+0x74)*s.marioHome.x+Memory::ReadFloat32(camera+0x78)*s.marioHome.y+Memory::ReadFloat32(camera+0x7C)*s.marioHome.z+Memory::ReadFloat32(camera+0x80);
    const float w=ortho?1.f:-viewZ;
    // Pan the view, preserving the native projection-center conventions. Mario's feet go at line 372 of 480,
    // leaving a fifth of the picture for the stage's sand floor below the zigzag border's reach.
    constexpr float kHomeY=372.f;
    const float panX=2.f*(608.f*.28f-x)/width*w/Memory::ReadFloat32(camera+0x94);
    const float panY=-2.f*(kHomeY-y)/height*w/Memory::ReadFloat32(camera+0xA8);
    Memory::WriteFloat32(camera+0x60,Memory::ReadFloat32(camera+0x60)+panX);
    Memory::WriteFloat32(camera+0x70,Memory::ReadFloat32(camera+0x70)+panY);
    SetStage(g,2,s.marioHome);
    static int probes=0;
    if((std::getenv("SPM_LP_TEST_FRACKTAIL") || std::getenv("SPM_LP_TEST_FRACKTAIL_STORY")) && probes++<3) {
        g.ScreenPos(s.marioHome,x,y);
        RT_LOGF(RT_TAG_RUNTIME,"[lpfracktail] camera units %.3f scale %.3f home %.1f %.1f viewport %.1f %.1f ortho %d\n",units,scale,x,y,width,height,ortho);
    }
}
REGISTER_NATIVE_FUNCTION_AS(0x800550F0,camMain_LostPagesBoss,"camMain_LostPagesBoss");
