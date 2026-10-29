#pragma once
#include "lp_final_boss_rules.h"
namespace LostPages {
enum class DimentioAttack { Punch, HomingBlast, SuperJump, MiniLuigis, Voidsplitter };
struct DimentioRules {
    static constexpr int Tribe = 309, MaxHp = 200;
    static constexpr const char* Map = "ls4_11";
    static constexpr const char* Music = "BGM_BTL_BOSS_LAST1";
    // Entry runs the Pure Hearts setup. Fight/Victory share 415: a live fight
    // has UW9 installed; loading 415 starts its defeat scene instead.
    static constexpr int EntryGsw0 = 414, FightGsw0 = 415, VictoryGsw0 = 415;
    static constexpr unsigned NpcScriptId = 0x1000007C, NativeDefeatScript = 0x80DED9A0;
    static constexpr unsigned NativeDefeatUnitwork = 9;
    // His first form (GSW0 411/412, inside Bleck's defeat scene) is barrier-protected; its hit script moves the
    // story to 413 on the third hit (unitwork 1 counts them), which plays "You can't defeat me!" and the Pure Hearts.
    static constexpr int OpeningGsw0First = 411, OpeningGsw0Last = 412, OpeningHits = 3;
    static constexpr int ProtectedEnemyTurns = FinalBossRules::ProtectedEnemyTurns;
    // The original Pure Hearts cutscene unlocks damage; HP belongs to that fight.
    // Turn pacing for the adaptation must be explicitly chosen by the controller.
    bool phase2 = false;
    constexpr void PureHeartsReturned() { phase2 = true; }
    constexpr bool CanDamage() const { return phase2; }
    constexpr int DamageTaken(int damage) const { return FinalBossRules::GateDamage(phase2,damage); }
};
}
