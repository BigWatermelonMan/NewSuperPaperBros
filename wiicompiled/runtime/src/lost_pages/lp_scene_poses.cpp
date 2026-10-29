// Per-scene pose swaps for Mario. Only the scene named here is touched: the chapter 6 cutscene where The Void
// destroys Sammer's Kingdom (wa1_02, "We're too late?!"... "No... Not like this..."). Its script ends by setting
// Mario's pose to T_7 (0x80D7B7A8) and GSW0 to 280; there Mario plays E_3 and then E_4 instead. Battles, idle and
// every other cutscene keep their animations.

#include "lp_scene_poses.h"
#include "lp_battle.h"

#include "runtime_log.h"

#include <cstdlib>
#include <map>
#include <string>

extern "C" void func_800432F8(CpuContext* ctx);  // animPoseSetAnim(poseId, animName, reset)

namespace LostPages {
namespace {

constexpr uint32_t kGp = 0x8056D198, kGpMap = 0x44, kGpGsw0 = 0x140;
constexpr const char* kSceneMap = "wa1_02";
// 279 while the scene runs; its SET GSW0 280 comes right after the T_7, before Mario's model takes it.
bool InScene() {
    const uint32_t gp = Memory::Read32(kGp);
    if (!Memory::Contains(gp, 0x200) || GuestString(gp + kGpMap, 32) != kSceneMap) return false;
    const int gsw0 = static_cast<int32_t>(Memory::Read32(gp + kGpGsw0));
    return (gsw0 == 279 || gsw0 == 280) && !BattleUiSnapshot().active;
}

int g_pose = -1;      // Mario's model pose while the swap runs
bool g_third = false;  // E_3 has played through: E_4 holds

}  // namespace

void ScenePoseTick(Guest& g) {
    if (g_pose < 0) return;
    if (!InScene()) { g_pose = -1; g_third = false; return; }
    if (g_third) return;
    // animPoseGetAnimPosePtr: +0x8C counts finished loops.
    const uint32_t p = g.Call(0x8004C548, {static_cast<uint32_t>(g_pose)});
    if (Memory::Contains(p, 0x90) && Memory::ReadFloat32(p + 0x8C) >= 1.f) {
        g_third = true;
        if (const uint32_t s3 = PersistentString(g, "mario_E_4")) g.Call(0x800432F8, {static_cast<uint32_t>(g_pose), s3, 1});
        RT_LOGF(RT_TAG_RUNTIME, "[lppose] Sammer's Kingdom: E_3 done -> E_4\n");
    }
}

}  // namespace LostPages

using namespace LostPages;

extern "C" void animPoseSetAnim_LostPagesScene(CpuContext* ctx) {
    static const bool log = std::getenv("SPM_LP_LOG_POSE") != nullptr;
    const std::string name = GuestString(ctx->gpr[4], 16);
    if (name.rfind("mario_", 0) == 0 && InScene()) {
        if (name == "mario_T_7") {
            Guest g(ctx);
            if (const uint32_t swap = PersistentString(g, g_third ? "mario_E_4" : "mario_E_3")) {
                if (g_pose < 0) RT_LOGF(RT_TAG_RUNTIME, "[lppose] Sammer's Kingdom: T_7 -> E_3, then E_4\n");
                g_pose = static_cast<int32_t>(ctx->gpr[3]);
                ctx->gpr[4] = swap;
            }
        }
        if (log) {
            static std::map<int32_t, std::string> last;  // only changes, per pose
            auto& seen = last[static_cast<int32_t>(ctx->gpr[3])];
            const std::string now = GuestString(ctx->gpr[4], 16);
            if (seen != now) {
                seen = now;
                RT_LOGF(RT_TAG_RUNTIME, "[lppose] anim pose %d %s\n", static_cast<int32_t>(ctx->gpr[3]), now.c_str());
            }
        }
    }
    func_800432F8(ctx);
}
REGISTER_NATIVE_FUNCTION_AS(0x800432F8, animPoseSetAnim_LostPagesScene, "animPoseSetAnim_LostPagesScene");
