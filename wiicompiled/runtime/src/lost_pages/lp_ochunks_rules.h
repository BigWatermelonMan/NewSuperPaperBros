#pragma once
// O'Chunks, first fight (Yold Desert, 1-3): his moves from the original real-time battle as TTYD-style turns.
namespace LostPages {
enum class OChunksAttack { ShoulderBash, GroundPound, GrabToss };
struct OChunksRules {
    static constexpr int MaxHp = 20, ThoreauRest = 3;
    int turn = 0;
    int skip = 0;            // turns he loses: dancing after a toss, or dizzy after Thoreau throws him
    bool dizzy = false;      // why he is skipping (false: his victory dance)
    bool enraged = false;    // at half HP he gets chunkier: +1 attack
    int thoreauRest = 0;     // rounds until Thoreau can lift him again
    OChunksAttack Next() { return static_cast<OChunksAttack>(turn++ % 3); }
    // The original hits for 1; here the heavier moves hit harder, and a guard takes 1 off each.
    int Power(OChunksAttack a) const { return (a == OChunksAttack::ShoulderBash ? 1 : 2) + (enraged ? 1 : 0); }
    // True once, when his HP first drops to half.
    bool CheckEnrage(int hp) {
        if (enraged || hp * 2 > MaxHp) return false;
        enraged = true;
        return true;
    }
    void Tossed() { skip = 1; dizzy = false; }          // he celebrates instead of attacking
    void Thrown() { skip = 1; dizzy = true; thoreauRest = ThoreauRest; }
    bool TakeSkip() {
        if (skip <= 0) return false;
        --skip;
        return true;
    }
    void EndRound() {
        if (thoreauRest > 0) --thoreauRest;
    }
};
}  // namespace LostPages
