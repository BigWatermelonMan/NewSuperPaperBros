#include "lp_final_boss_visual.h"
#include "runtime_log.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <map>
#include <string>

namespace LostPages {
namespace {
constexpr uint32_t kDraw = 0x817F0300;
constexpr int kMini = 7, kVoid = 15, kSlots = 18;
struct Pose {
    int id = -1;
    std::string animation;
};
std::array<Pose, kSlots> poses;
FinalBossVisual visual;
int frame = 0, stateFrame = 0, retry = 0;
int miniFrame = 0;
bool wasMiniAttacking = false;
bool wasAttacking = false, wasAbsorbing = false;
bool readyForDraw = false;
enum class BleckPose { Idle, CastStart, CastHold, CastEnd, Open, Suction, Swallow, Close };
BleckPose bleckPose = BleckPose::Idle;
bool Finite(const Vec3& p) { return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z); }
bool Ended(Guest& g, const Pose& pose) {
    if (pose.id < 0) return false;
    // animPoseGetLoopTimes (0x8004454C) returns this float in f1, rather
    // than r3. Read the same verified field through the native pose accessor.
    const uint32_t p = g.Call(0x8004C548,{static_cast<uint32_t>(pose.id)});
    return Memory::Contains(p,0x90) && Memory::ReadFloat32(p+0x8C)>=1.f;
}
const char* BleckAnimation(Guest& g, const FinalBossVisual& view) {
    if (view.absorb && !wasAbsorbing) bleckPose = BleckPose::Open;
    else if (view.attacking && !wasAttacking && !view.absorb) bleckPose = BleckPose::CastStart;
    else if (!view.absorb && wasAbsorbing) bleckPose = BleckPose::Swallow;
    else if (!view.attacking && wasAttacking && !view.absorb && bleckPose!=BleckPose::Swallow)
        bleckPose = BleckPose::CastEnd;
    else if (Ended(g,poses[0])) {
        switch (bleckPose) {
        case BleckPose::Open: bleckPose=BleckPose::Suction; break;
        case BleckPose::Swallow: bleckPose=BleckPose::Close; break;
        case BleckPose::Close: case BleckPose::CastEnd: bleckPose=BleckPose::Idle; break;
        case BleckPose::CastStart: bleckPose=BleckPose::CastHold; break;
        default: break;
        }
    }
    switch (bleckPose) {
    case BleckPose::Open: return "A_4a";
    case BleckPose::Suction: return "A_4b";
    case BleckPose::Swallow: return "A_4c";
    case BleckPose::Close: return "A_4g";
    case BleckPose::CastStart: return "A_1a";
    case BleckPose::CastHold: return "A_1b";
    case BleckPose::CastEnd: return "A_1c";
    default: return "S_1";
    }
}

uint32_t String(Guest& g, const char* value) {
    static std::map<std::string, uint32_t> cache;
    auto it = cache.find(value);
    if (it != cache.end()) return it->second;
    uint32_t p = GameString(value);
    if (!p) {
        const size_t size = std::char_traits<char>::length(value) + 1;
        p = g.Call(0x801A5634, {0, static_cast<uint32_t>(size)});
        if (!p || !Memory::Contains(p, size)) return 0;
        for (size_t i = 0; i < size; ++i) Memory::Write8(p + static_cast<uint32_t>(i), value[i]);
    }
    cache.emplace(value, p);
    return p;
}
bool Animate(Guest& g, Pose& pose, const char* animation) {
    if (pose.id < 0) return false;
    if (pose.animation != animation) {
        const uint32_t name = String(g, animation);
        if (!name) return false;
        g.Call(0x800432F8, {static_cast<uint32_t>(pose.id), name, 1});
        pose.animation = animation;
    }
    return true;
}
bool Ensure(Guest& g, int slot, const char* model, const char* animation) {
    auto& pose = poses[slot];
    if (pose.id < 0) {
        const uint32_t name = String(g, model);
        if (!name) return false;
        pose.id = static_cast<int32_t>(g.Call(0x80042384, {name, 0}));
        if (pose.id < 0) {
            RT_LOGF(RT_TAG_RUNTIME, "[lpfinal] cannot load native model %s\n", model);
            return false;
        }
        RT_LOGF(RT_TAG_RUNTIME, "[lpfinal] native model %s pose %d\n", model, pose.id);
    }
    return Animate(g, pose, animation);
}
void Step(Guest& g, int slot) {
    if (poses[slot].id >= 0) g.Call(0x80045170, {static_cast<uint32_t>(poses[slot].id)});
}
void Draw(Guest& g, int slot, Vec3 p, float facing, float scale) {
    if (poses[slot].id < 0) return;
    for (uint32_t pass = 1; pass <= 2; ++pass)
        g.Call(0x80048EE4, {static_cast<uint32_t>(poses[slot].id), pass}, {p.x, p.y, p.z, facing, scale});
}
float ModelFacing(int slot) {
    // Optional isolated renderer probe: no NPC or Mario pose is touched.
    // Four stable views let screenshots distinguish a missing part from a
    // profile/back view before changing production layout.
    static const bool probe = std::getenv("SPM_LP_FINAL_RIG_PROBE") != nullptr;
    static const char* yaw = std::getenv("SPM_LP_FINAL_RIG_YAW");
    float offset = 0.f;
    if (probe) offset = static_cast<float>((frame/240)%4)*90.f;
    else if (yaw) { const float v=std::strtof(yaw,nullptr); if (std::isfinite(v)) offset=v; }
    return visual.facing+offset+((slot==3 || slot==4)?180.f:0.f);
}
Vec3 At(float x, float y, float z = 0.f) {
    const float angle = visual.facing * (3.14159265358979323846f / 180.f);
    const float c = std::cos(angle), s = std::sin(angle);
    return {visual.root.x + (x*c + z*s)*visual.scale,
            visual.root.y + y*visual.scale,
            visual.root.z + (z*c - x*s)*visual.scale};
}
// Native idle layout, npc_dimeen_l at 0x801E7A4C, vectors 0x8033AA08..44.
// Slots: torso, face, left/right hand, left/right shoe. The native face is
// the vulnerable part (6). Its NPC origin is 195 units above the floor root.
std::array<Vec3, 6> Rig() {
    const float bob = std::sin(frame*.055f)*2.f;
    const float punch = visual.attacking ? std::sin(std::min(stateFrame, 40)*3.14159265f/40.f)*35.f : 0.f;
    return {{At(0,130+bob), At(0,195+bob), At(50-punch,90+bob,-50),
             At(-60-punch,90+bob,50), At(-60,0,-50), At(60,0,50)}};
}
void Link(Guest& g, Vec3 a, Vec3 b) {
    // e_DmenL_r is the original round, black/white joint. Native parts 13..40
    // form four chains of seven links. Share one evaluated pose across them.
    for (int i=1; i<=7; ++i) {
        const float t = i/8.f;
        Draw(g,6,{a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t,a.z+(b.z-a.z)*t},visual.facing,visual.scale);
    }
}
}

void ReleaseFinalBossVisual(Guest& g) {
    visual = {};
    for (auto& pose : poses) {
        if (pose.id >= 0) g.Call(0x80049EAC, {static_cast<uint32_t>(pose.id)});
        pose = {};
    }
    frame = stateFrame = retry = 0;
    miniFrame = 0;
    wasMiniAttacking = false;
    wasAttacking = wasAbsorbing = false;
    readyForDraw = false;
    bleckPose = BleckPose::Idle;
}

void UpdateFinalBossVisual(Guest& g, const FinalBossVisual& view) {
    if (!view.active || (view.kind != 1 && view.kind != 2) ||
        !Finite(view.root)) {
        ReleaseFinalBossVisual(g);
        return;
    }
    if (view.kind != visual.kind) ReleaseFinalBossVisual(g);
    visual = view;
    visual.scale = std::isfinite(view.scale) ? std::clamp(view.scale,.05f,5.f) : 1.f;
    visual.facing = std::isfinite(view.facing) ? view.facing : 0.f;
    visual.miniLuigis = view.kind == 2 ? std::clamp(view.miniLuigis,0,8) : 0;
    visual.voidSprites = view.kind == 1 ? std::clamp(view.voidSprites,0,3) : 0;
    ++frame;
    if (view.miniLuigisAttacking != wasMiniAttacking) miniFrame = 0;
    else miniFrame = std::min(miniFrame+1,3600);
    wasMiniAttacking = view.miniLuigisAttacking;
    if (wasAttacking != view.attacking || wasAbsorbing != view.absorb) stateFrame = 0;
    else stateFrame = std::min(stateFrame+1,3600);
    if (retry > 0) { --retry; return; }

    bool ready = !view.drawBoss;
    if (!view.drawBoss) {
    } else if (view.kind == 1) {
        // Native suction script 0x8042C37C..648: ids 33..39 = A_4a..g.
        // A_4a opens the cape; A_4b holds it while the void sucks in its target.
        const char* anim = BleckAnimation(g,view);
        ready = Ensure(g,0,"e_jigen",anim);
    } else {
        // Actual tribe-309 part definitions 0x8041BC80..C7F0. Never e_dmnyan.
        ready = Ensure(g,0,"e_DmenL_k",view.attacking ? "A_2b" : "S_1");
        ready = Ensure(g,1,"e_DmenL_f",view.attacking ? "A_1" : "S_1") && ready;
        ready = Ensure(g,2,"e_DmenL_lh",view.attacking ? "A_1b" : "S_1") && ready;
        ready = Ensure(g,3,"e_DmenL_rh",view.attacking ? "A_1b" : "S_1") && ready;
        ready = Ensure(g,4,"e_DmenL_s","S_1") && ready;
        ready = Ensure(g,5,"e_DmenL_s","S_1") && ready;
        ready = Ensure(g,6,"e_DmenL_r","S_1") && ready;
    }
    readyForDraw = ready;
    if (!ready) { retry = 120; return; }
    wasAttacking = view.attacking;
    wasAbsorbing = view.absorb;
    if (view.drawBoss) for (int i=0;i<(view.kind==1 ? 1 : kMini);++i) Step(g,i);
    for (int i=0;i<visual.miniLuigis;++i)
        if (Ensure(g,kMini+i,"p_dot_luigi",view.miniLuigisAttacking ? "J_1" : "S_1")) Step(g,kMini+i);
    // New Void Sprites reuse Fleep's native Pixl asset, with tumbling/orbiting
    // transforms. This is adaptation art direction, not an original enemy model.
    for (int i=0;i<visual.voidSprites;++i) {
        if (!Ensure(g,kVoid+i,"FRY_mekri","S_1")) continue;
        // Native material event tint, verified 0x80044B74 takes (pose,GXColor*).
        // Each pose owns its tint; no shared Pixl / HUD material is changed.
        const uint32_t color = g.Scratch(0x200);
        const auto pulse = static_cast<uint32_t>(20.f+12.f*std::sin(frame*.13f+i));
        Memory::Write32(color,((55u+pulse)<<24)|((22u+pulse/2)<<16)|((95u+pulse)<<8)|235u);
        g.Call(0x80044B74,{static_cast<uint32_t>(poses[kVoid+i].id),color});
        // npcDispSub enables this after applying its material event color
        // (0x801B261C..2628); setting the bytes alone leaves it unused.
        g.Call(0x8004461C,{static_cast<uint32_t>(poses[kVoid+i].id),0x2000});
        Step(g,kVoid+i);
    }
    const uint32_t mario = g.MarioPtr();
    if (!Memory::Contains(mario,0x124)) return;
    const uint32_t camera = Memory::Read32(mario+0x120);
    g.Call(0x8005ADEC,{camera,1,kDraw,0},{100.f});
}

bool FinalBossTarget(Guest&, Vec3& point) {
    if (!visual.active || !readyForDraw || !visual.drawBoss) return false;
    if (visual.kind == 2) {
        if (poses[1].id < 0) return false;
        point = Rig()[1];
        point.y += 30.f*visual.scale;
    } else point = At(0,35);
    return true;
}

void DrawFinalBoss(CpuContext* ctx) {
    if (!visual.active || !readyForDraw) return;
    Guest g(ctx);
    if (!visual.drawBoss) {
    } else if (visual.kind == 1) Draw(g,0,visual.root,visual.facing,visual.scale);
    else {
        const auto p = Rig();
        // Far limbs first, face last. Round joints need no invented textures.
        Link(g,At(28,145,-25),p[2]);
        Link(g,At(-28,145,25),p[3]);
        Link(g,At(-28,100,-25),p[4]);
        Link(g,At(28,100,25),p[5]);
        Draw(g,2,p[2],ModelFacing(2),visual.scale);
        Draw(g,3,p[3],ModelFacing(3),visual.scale);
        Draw(g,4,p[4],ModelFacing(4),visual.scale);
        Draw(g,5,p[5],ModelFacing(5),visual.scale);
        Draw(g,0,p[0],ModelFacing(0),visual.scale);
        Draw(g,1,p[1],ModelFacing(1),visual.scale);
    }
    for (int i=0;i<visual.miniLuigis;++i) {
        const float jump = visual.miniLuigisAttacking ? std::max(0.f,std::sin(miniFrame*.12f))*38.f : 0.f;
        Vec3 p = At(-85+(i%4)*23.f,jump+(i/4)*10.f,35+(i/4)*12.f);
        if (visual.useHelperPositions && Finite(visual.miniLuigiRoot))
            p = {visual.miniLuigiRoot.x + ((i%3)-1)*24.f*visual.scale,
                 visual.miniLuigiRoot.y + jump*visual.scale,
                 visual.miniLuigiRoot.z + (i/3)*14.f*visual.scale};
        Draw(g,kMini+i,p,visual.facing,2.4f*visual.scale);
    }
    for (int i=0;i<visual.voidSprites;++i) {
        const float phase = frame*.055f+i*2.0943951f;
        const float pull = visual.absorb ? std::max(0.f,1.f-stateFrame/48.f) : 1.f;
        Vec3 p = At((-62+i*40.f)*pull,35+(30+std::sin(phase)*16.f)*pull,25);
        if (visual.useHelperPositions && Finite(visual.voidSpritePositions[i])) {
            const Vec3 cape = At(0,35);
            const Vec3 start = visual.voidSpritePositions[i];
            p = {cape.x+(start.x-cape.x)*pull,cape.y+(start.y-cape.y+std::sin(phase)*5.f)*pull,
                 cape.z+(start.z-cape.z)*pull};
        }
        Draw(g,kVoid+i,p,
             visual.facing+std::sin(phase)*45.f,1.2f*visual.scale*(.35f+.65f*pull));
    }
}
}
extern "C" void lostPagesFinalBossDraw(CpuContext* ctx) { LostPages::DrawFinalBoss(ctx); }
REGISTER_NATIVE_FUNCTION(0x817F0300,lostPagesFinalBossDraw);
