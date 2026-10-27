#pragma once
namespace LostPages {
enum class FracktailAttack { Charge, Summon, Breath, CircuitBreaker };
struct FracktailRules {
    int turn=0,exposed=0;
    FracktailAttack Next(){return static_cast<FracktailAttack>(turn++%4);}
    void ThrowHit(){exposed=3;} // throw turn, then two complete player attack turns
    void EndRound(){if(exposed>0)--exposed;}
    bool Vulnerable()const{return exposed>0;}
};
}
