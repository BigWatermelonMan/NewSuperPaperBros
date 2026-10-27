#pragma once
#include <algorithm>
namespace LostPages::CombatRules {
inline int Attack(int power,int defense,bool success,bool allOrNothing,bool pierce) {
    if(allOrNothing && !success) return 0;
    return std::max(0,power+(allOrNothing?1:0)-(pierce?0:std::max(0,defense)));
}
inline int Incoming(int attack,int defense,int guardReduction,bool doublePain,int lastStand) {
    int damage=std::max(0,attack-defense-guardReduction);
    if(doublePain) damage*=2;
    for(int i=0;i<lastStand;++i) damage=(damage+1)/2;
    return damage;
}
inline int Window(int base,int simplifiers,int unsimplifiers) {
    return std::clamp(base+2*simplifiers-2*unsimplifiers,1,16);
}
} // namespace LostPages::CombatRules
