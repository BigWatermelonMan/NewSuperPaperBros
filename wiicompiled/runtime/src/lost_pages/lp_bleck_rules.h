#pragma once
#include "lp_final_boss_rules.h"
namespace LostPages {
enum class BleckAttack { VoidSuction, Slow, VoidBullets, Teleport, Haste,
                         VoidSprites, Absorb, RealitySplit };
struct BleckRules {
    static constexpr int Tribe = 305, MaxHp = 150;
    static constexpr const char* Map = "ls4_11";
    static constexpr const char* Music = "BGM_BTL_MR_ZIGEN1";
    static constexpr int IntroGsw0 = 409, EntryGsw0 = 410, VictoryGsw0 = 411;
    static constexpr unsigned NpcScriptId = 0x1000007B, NativeDefeatScript = 0x80DEACF8;
    static constexpr unsigned NativeDefeatUnitwork = 13;
    // His AI's Pure Hearts scene (the barrier breaks), run once the barrier part has blocked enough hits.
    // His AI's barrier scene. Its first run (part work 2 == 0) brings the Pure Hearts and leaves the barrier up
    // for one more hit (work 4 = 1); its second run breaks it. Three phases: protected, barrier, hurtable.
    static constexpr unsigned PureHeartsScene = 0x8042F1F0;
    static constexpr int Phase2BarrierHits = 1;
    // How long the barrier shows (its own animation) after it turns a hit away.
    static constexpr int BarrierFlashFrames = 50;
    static constexpr int ProtectedEnemyTurns = FinalBossRules::ProtectedEnemyTurns;
    // No original hit/turn threshold: the Pure Hearts event removes the shield.
    bool phase2 = false;
    constexpr void PureHeartsReturned() { phase2 = true; }
    constexpr bool CanDamage() const { return phase2; }
    constexpr int DamageTaken(int damage) const { return FinalBossRules::GateDamage(phase2,damage); }
};
}
