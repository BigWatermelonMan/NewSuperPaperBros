#include "../wiicompiled/runtime/src/lost_pages/lp_bleck_rules.h"
#include "../wiicompiled/runtime/src/lost_pages/lp_dimentio_rules.h"
#include "../wiicompiled/runtime/src/lost_pages/lp_combat_rules.h"
#include <cassert>
#include <cstdio>
#include <limits>
using namespace LostPages;
namespace F = LostPages::FinalBossRules;
template<class Rules> constexpr bool BarrierContract() {
    Rules rules;
    // Arbitrarily large attacks cannot reduce original vulnerable-phase HP
    // during the protected phase, and cannot advance its script.
    if (rules.CanDamage() || rules.DamageTaken(99999) != 0) return false;
    if (rules.CanDamage()) return false;
    rules.PureHeartsReturned();
    return rules.CanDamage() && rules.DamageTaken(7)==7 &&
           rules.DamageTaken(-7)==0 && rules.DamageTaken(0)==0;
}
static_assert(BleckRules::Tribe==305 && BleckRules::MaxHp==150);
static_assert(DimentioRules::Tribe==309 && DimentioRules::MaxHp==200);
static_assert(BleckRules::ProtectedEnemyTurns==3 && DimentioRules::ProtectedEnemyTurns==3);
static_assert(BarrierContract<BleckRules>() && BarrierContract<DimentioRules>());

constexpr bool ProtectedTurnContract() {
    int turns=0;
    for(int i=0;i<3;++i) {
        if(F::PhaseTwoReady(turns))return false;
        turns=F::CompletedProtectedTurn(turns);
    }
    return F::PhaseTwoReady(turns) && F::CompletedProtectedTurn(turns)==3 &&
           !F::PhaseTwoReady(-1) && F::PhaseTwoReady(999);
}
constexpr bool AttackCycleContract() {
    for(int kind=1;kind<=2;++kind)for(bool phase2:{false,true}) {
        bool seen[7]={};
        const int cycle=phase2?(kind==1?7:6):4;
        for(int turn=0;turn<cycle;++turn) {
            int attack=F::AttackForTurn(kind,phase2,turn);
            if(!F::AttackAllowed(kind,phase2,attack) || seen[attack])return false;
            seen[attack]=true;
            if(F::AttackForTurn(kind,phase2,turn+cycle)!=attack)return false;
        }
    }
    return !F::AttackAllowed(0,true,0) && !F::AttackAllowed(3,true,0) &&
           !F::AttackAllowed(1,false,6) && !F::AttackAllowed(2,false,5) &&
           !F::AttackAllowed(2,true,6) && !F::AttackAllowed(1,true,-1) &&
           F::AttackForTurn(0,true,0)==-1;
}
constexpr bool DodgeContract() {
    for(int power=0;power<=200;++power) {
        int previous=power;
        for(int dodges=0;dodges<=3;++dodges) {
            int damage=F::RealitySplitDamage(power,dodges);
            if(damage<0 || damage>previous || damage!=power*(3-dodges)/3)return false;
            previous=damage;
        }
        if(previous!=0)return false;
    }
    return F::RealitySplitDamage(-20,0)==0 && F::RealitySplitDamage(20,-1)==20 &&
           F::RealitySplitDamage(20,4)==0;
}
constexpr bool DebuffContract() {
    int duration=F::VoidsplitterDebuffTurns(false);
    duration=F::TickDebuff(duration); // immediate end of the casting enemy round
    for(int playerRound=0;playerRound<3;++playerRound) {
        if(duration<=0)return false;
        duration=F::TickDebuff(duration);
    }
    return duration==0 && F::TickDebuff(0)==0 && F::TickDebuff(-2)==0 &&
           F::VoidsplitterDebuffTurns(true)==0;
}
static_assert(ProtectedTurnContract());
static_assert(AttackCycleContract());
static_assert(DodgeContract());
static_assert(DebuffContract());
static_assert(F::RealitySplitDamage(20,0)==20 && F::RealitySplitDamage(20,1)==13 &&
              F::RealitySplitDamage(20,2)==6 && F::RealitySplitDamage(20,3)==0);
static_assert(F::RealitySplitDamage(std::numeric_limits<int>::max(),0)==std::numeric_limits<int>::max());
static_assert(F::PureShieldDamage(0,true)==0 && F::PureShieldDamage(-1,true)==0 &&
              F::PureShieldDamage(999,true)==1 && F::PureShieldDamage(999,false)==999);
static_assert(!F::CanAffordSpecial(49,0) && F::CanAffordSpecial(50,0));
static_assert(!F::CanAffordSpecial(29,1) && F::CanAffordSpecial(30,1));
static_assert(!F::CanAffordSpecial(100,-1) && !F::CanAffordSpecial(100,2));
static_assert(F::EnergyAfterSpecial(49,0)==49 && F::EnergyAfterSpecial(50,0)==0 &&
              F::EnergyAfterSpecial(100,1)==70 && F::EnergyAfterSpecial(100,2)==100);
static_assert(F::SpecialAvailable(0,0) && F::SpecialAvailable(1,0) && F::SpecialAvailable(2,0));
static_assert(!F::SpecialAvailable(1,1) && F::SpecialAvailable(2,1) && !F::SpecialAvailable(2,2));
static_assert(F::AbsorptionBonus(-1)==0 && F::AbsorptionBonus(0)==0 && F::AbsorptionBonus(3)==3 && F::AbsorptionBonus(99)==3);
static_assert(F::AddAbsorptionBonus(2,3)==5 && F::AddAbsorptionBonus(2,0)==2);
static_assert(F::AddAbsorptionBonus(std::numeric_limits<int>::max(),3)==std::numeric_limits<int>::max());
static_assert(F::SwarmDamage(1,false)==6 && F::SwarmDamage(9,true)==6 && F::SwarmDamage(0,true)==0);
static_assert(F::SwarmDamage(-1,false)==0 && F::SwarmDamage(std::numeric_limits<int>::max(),false)==std::numeric_limits<int>::max());

int main() {
    // Cross-policy checks catch ordering bugs: shield must follow Double Pain,
    // defense and guard reductions; successful zero-damage guards stay zero.
    for(int power=0;power<=30;++power)for(int defense=0;defense<=5;++defense)
    for(int guard=0;guard<=2;++guard)for(bool doublePain:{false,true}) {
        int incoming=guard==2?0:CombatRules::Incoming(power,defense,guard,doublePain,0);
        int shielded=F::PureShieldDamage(incoming,true);
        assert(shielded==(incoming>0?1:0));
        assert(F::SwarmDamage(incoming,true)==shielded*F::SwarmHits);
        assert(F::SwarmDamage(incoming,false)==incoming*F::SwarmHits);
    }
    // Absorb only living helpers, then clear them in the controller. Reusing
    // an empty helper set must never manufacture a second bonus.
    bool living[]={true,false,true};
    int count=0;
    for(bool& alive:living)if(alive){++count;alive=false;}
    int attack=F::AddAbsorptionBonus(0,count),defense=F::AddAbsorptionBonus(0,count);
    assert(attack==2 && defense==2);
    count=0;for(bool alive:living)if(alive)++count;
    assert(F::AddAbsorptionBonus(attack,count)==attack);
    assert(F::AddAbsorptionBonus(defense,count)==defense);
    std::puts("Final boss rules: phase, energy, mitigation, dodge, debuff, absorption and swarm contracts passed.");
}
