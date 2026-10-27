#include "../wiicompiled/runtime/src/lost_pages/lp_badge_profile.h"
#include "../wiicompiled/runtime/src/lost_pages/lp_combat_rules.h"
#include "../wiicompiled/runtime/src/lost_pages/lp_fracktail_rules.h"
#include "../wiicompiled/runtime/src/lost_pages/lp_ochunks_rules.h"
#include "../wiicompiled/runtime/src/lost_pages/lp_badge_pickups.h"
#include <cassert>
#include <iostream>
#include <set>
#include <sstream>
using namespace LostPages;
int main() {
    static_assert(kBadgeCount==85);
    std::set<std::string> names;
    for(int i=0;i<kBadgeCount;++i) {
        const auto& b=kBadges[i];assert(static_cast<int>(b.id)==i);
        assert(names.insert(b.name).second);assert(b.bp>=0 && b.fp>=0);
        assert(static_cast<int>(b.family)<kBadgeCount);
        if(b.alternate)assert(!kBadges[static_cast<int>(b.family)].alternate);
    }
    BadgeProfile normal;
    for(int i=0;i<kBadgeCount;++i)assert(!normal.owned[i] && !normal.equipped[i]);
    BadgeProfile source;source.fp=23;source.energy=85;source.bp=18;assert(normal.bp==3);
    for(int i=0;i<kBadgeCount;++i){source.owned[i]=i%3!=0;source.equipped[i]=source.owned[i] && i%2;}
    std::stringstream saved;WriteBadgeProfile(saved,source);const auto restored=ReadBadgeProfile(saved);
    assert(restored.bp==18);assert(restored.fp==23 && restored.energy==85 && restored.owned==source.owned && restored.equipped==source.equipped);
    for(const auto& malformed:{"","LPBADGES 9\n10 0\n","LPBADGES 2\n30 0\n11\n","LPBADGES 2\n0 0\nxx\n"}) {
        std::istringstream in(malformed);const auto p=ReadBadgeProfile(in);assert(p.owned==normal.owned && p.equipped==normal.equipped);
    }
    std::stringstream tampered;tampered<<"LPBADGES 2\n999 -9\n";for(int i=0;i<85;++i)tampered<<"01\n";
    const auto clean=ReadBadgeProfile(tampered);assert(clean.fp==200 && clean.energy==0 && clean.equipped==normal.equipped);
    assert(BadgeForPickup(0x59)==BadgeId::HpPlus && BadgeForPickup(0x5A)==BadgeId::PowerPlus && BadgeForPickup(0x50)==BadgeId::Count);
    std::stringstream v2;v2<<"LPBADGES 2\n9 15\n";for(int i=0;i<85;++i)v2<<"00\n";
    const auto legacy=ReadBadgeProfile(v2);assert(legacy.bp==-1 && legacy.fp==9 && legacy.energy==15);
    FracktailRules boss;assert(!boss.Vulnerable());
    for(int i=0;i<12;++i)assert(static_cast<int>(boss.Next())==i%4);
    boss.ThrowHit();boss.EndRound();assert(boss.Vulnerable());boss.EndRound();assert(boss.Vulnerable());boss.EndRound();assert(!boss.Vulnerable());
    using namespace CombatRules;
    assert(Attack(5,2,true,false,false)==3);assert(Attack(5,8,true,false,true)==5);
    assert(Attack(5,0,false,true,false)==0);assert(Attack(5,2,true,true,false)==4);
    assert(Incoming(5,1,2,false,false)==2);assert(Incoming(5,1,2,true,true)==2);
    assert(Incoming(1,2,1,true,true)==0);assert(Incoming(5,0,0,false,true)==3);
    assert(Incoming(7,0,0,false,2)==2);
    assert(Window(8,1,0)==10 && Window(8,0,1)==6 && Window(3,0,9)==1);
    {   // O'Chunks: Bash, Pound, Toss in turn; enraged once at half HP; a toss or Thoreau throw costs him a turn
        OChunksRules o;
        assert(o.Next()==OChunksAttack::ShoulderBash && o.Next()==OChunksAttack::GroundPound && o.Next()==OChunksAttack::GrabToss);
        assert(o.Next()==OChunksAttack::ShoulderBash);
        assert(o.Power(OChunksAttack::ShoulderBash)==1 && o.Power(OChunksAttack::GroundPound)==2);
        assert(!o.CheckEnrage(11) && o.CheckEnrage(10) && !o.CheckEnrage(5) && o.Power(OChunksAttack::GrabToss)==3);
        assert(!o.TakeSkip());o.Tossed();assert(!o.dizzy && o.TakeSkip() && !o.TakeSkip());
        o.Thrown();assert(o.dizzy && o.thoreauRest==3);o.EndRound();o.EndRound();assert(o.thoreauRest==1);
    }
    std::cout<<"PASS: 85 badge definitions; locked defaults; BP/profile migration; stat pickup mapping; Fracktail cycle/exposure; O'Chunks cycle/enrage/skips; damage, piercing, guard, Danger and timing rules\n";
}
