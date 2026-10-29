#pragma once
#include <limits>

// Pure, shared combat policy. No Guest, renderer, controller state or clock.
namespace LostPages::FinalBossRules {
inline constexpr int ProtectedEnemyTurns = 3;
inline constexpr int PureShieldEnergyCost = 50, LoversGriefEnergyCost = 30;
inline constexpr int LoversGriefDamage = 8;
inline constexpr int RealitySplitBaseDamage = 20, RealitySplitDodgeCount = 3;
inline constexpr int VoidsplitterBaseDamage = 24;
// Applied during an enemy turn, then immediately ticked at EndRound: three
// subsequent player command rounds retain the ATK/DEF penalties.
inline constexpr int VoidsplitterDuration = 4;
inline constexpr int VoidsplitterAttackPenalty = 3, VoidsplitterDefensePenalty = 2;
inline constexpr int MaxVoidSprites = 3, SwarmHits = 6, SwarmBaseDamage = 1;

constexpr int Nonnegative(int n) { return n > 0 ? n : 0; }
constexpr int Clamp(int n, int lo, int hi) { return n < lo ? lo : n > hi ? hi : n; }
constexpr bool IsFinalBoss(int kind) { return kind == 1 || kind == 2; }
// turns counts completed whole enemy rounds, never individual helper actions.
constexpr bool PhaseTwoReady(int turns) { return turns >= ProtectedEnemyTurns; }
constexpr int CompletedProtectedTurn(int turns) { return Clamp(turns, 0, ProtectedEnemyTurns-1)+1; }
constexpr int GateDamage(bool phase2, int damage) { return phase2 ? Nonnegative(damage) : 0; }
// Apply AFTER defense/guard/badge calculation; zero/superguard stays zero.
// Shield is active for the whole enemy round; each swarm hit is capped separately.
constexpr int PureShieldDamage(int damage, bool shield) {
    return damage <= 0 ? 0 : shield ? 1 : damage;
}
constexpr int SpecialCost(int index) {
    return index == 0 ? PureShieldEnergyCost : index == 1 ? LoversGriefEnergyCost : -1;
}
constexpr bool SpecialAvailable(int kind, int index) {
    return index == 0 || (kind == 2 && index == 1);
}
constexpr bool CanAffordSpecial(int energy, int index) {
    return SpecialCost(index) >= 0 && energy >= SpecialCost(index);
}
// Failure preserves energy: callers can check affordability before committing.
constexpr int EnergyAfterSpecial(int energy, int index) {
    return CanAffordSpecial(energy,index) ? energy-SpecialCost(index) : energy;
}
// Scale pre-defense attack, including absorbed ATK bonuses. Divide before the
// multiply so even INT_MAX inputs cannot overflow; truncation matches power*n/3.
constexpr int RealitySplitDamage(int power, int dodges) {
    const int p=Nonnegative(power), remaining=RealitySplitDodgeCount-Clamp(dodges,0,RealitySplitDodgeCount);
    return (p/RealitySplitDodgeCount)*remaining + (p%RealitySplitDodgeCount)*remaining/RealitySplitDodgeCount;
}
constexpr int TickDebuff(int turns) { return turns > 0 ? turns-1 : 0; }
constexpr int VoidsplitterDebuffTurns(bool shield) { return shield ? 0 : VoidsplitterDuration; }
constexpr int AbsorptionBonus(int livingSprites) { return Clamp(livingSprites,0,MaxVoidSprites); }
constexpr int AddAbsorptionBonus(int current, int livingSprites) {
    const int n=Nonnegative(current), add=AbsorptionBonus(livingSprites);
    return n > std::numeric_limits<int>::max()-add ? std::numeric_limits<int>::max() : n+add;
}
// perHit already includes normal combat mitigation. No flat swarm-wide shield cap.
constexpr int SwarmDamage(int perHit, bool shield) {
    const int hit=PureShieldDamage(perHit,shield);
    return hit > std::numeric_limits<int>::max()/SwarmHits ? std::numeric_limits<int>::max() : hit*SwarmHits;
}
// Controller attack IDs, deliberately distinct from the descriptive attack enums:
// Bleck: 0 suction, 1 cards, 2 bullets, 3 slow, 4 summon, 5 absorb, 6 split.
// Dimentio: 0 punch, 1 blast, 2 jump, 3 blocks, 4 summon, 5 Voidsplitter.
constexpr bool AttackAllowed(int kind, bool phase2, int attack) {
    return IsFinalBoss(kind) && attack >= 0 && attack <= (phase2 ? (kind==1 ? 6 : 5) : 3);
}
constexpr int AttackForTurn(int kind, bool phase2, int turn) {
    if(!IsFinalBoss(kind))return -1;
    const int t=Nonnegative(turn);
    if(!phase2)return t%4;
    if(kind==2)return t%6;
    constexpr int order[]={0,4,1,5,3,2,6};
    return order[t%7];
}
} // namespace LostPages::FinalBossRules
