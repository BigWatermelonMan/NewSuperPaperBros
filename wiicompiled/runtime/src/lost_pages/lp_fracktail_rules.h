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
// he4_10's intro script (REL): Fracktail's last line stg1_4_012 is the USER_FUNC at 0x80CF4A3C; the camera pan and
// 300-frame wait that follow it run with argument pointers in [AfterLastLine, BeforeDig], then he digs in.
struct FracktailIntro {
    static constexpr unsigned AfterLastLine = 0x80CF4A58, BeforeDig = 0x80CF4A90;
};
}
