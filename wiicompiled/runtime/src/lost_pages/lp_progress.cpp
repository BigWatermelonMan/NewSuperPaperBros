#include "lp_progress.h"

#include <algorithm>
#include <cmath>

namespace LostPages {

// Layout of MarioPouchWork+0x0, unused by the game and saved with the pouch: magic, level, star points, version.
constexpr uint8_t kMagic = 0x4C;  // 'L'
constexpr uint8_t kVersion = 1;

Progress LoadProgress(Guest& g) {
    const uint32_t pouch = g.PouchPtr();
    Progress p;
    if (!Memory::Contains(pouch, 0x20)) return p;
    const uint32_t word = Memory::Read32(pouch + Pouch::lostPages);
    if ((word >> 24) == kMagic) {
        p.level = std::clamp<int>((word >> 16) & 0xFF, 1, kMaxLevel);
        p.starPoints = std::clamp<int>((word >> 8) & 0xFF, 0, kStarPointsPerLevel - 1);
    } else {
        // First time on this save: keep the level the game had reached.
        p.level = std::clamp<int>(static_cast<int32_t>(Memory::Read32(pouch + Pouch::level)), 1, kMaxLevel);
        SaveProgress(g, p);
    }
    return p;
}

void SaveProgress(Guest& g, const Progress& p) {
    const uint32_t pouch = g.PouchPtr();
    if (!Memory::Contains(pouch, 0x20)) return;
    const uint32_t word = (uint32_t{kMagic} << 24) | (static_cast<uint32_t>(p.level & 0xFF) << 16) |
                          (static_cast<uint32_t>(p.starPoints & 0xFF) << 8) | kVersion;
    Memory::Write32(pouch + Pouch::lostPages, word);
    Memory::Write32(pouch + Pouch::level, static_cast<uint32_t>(p.level));  // the pause menu shows it
}

int EnemyLevel(int killXp, int attack, int maxHp) {
    // SPM has no enemy levels; its kill score (100 for Lineland's first foes, thousands late in the game)
    // tracks toughness well enough, nudged by attack and HP.
    const double xp = std::max(killXp, 50);
    const double level = 6.0 + 2.6 * std::log2(xp / 50.0) + 0.5 * attack + 0.15 * maxHp;
    return std::clamp(static_cast<int>(std::lround(level)), 1, 120);
}

int StarPointsFor(int enemyLevel, int marioLevel, int enemiesAtStart) {
    if (marioLevel >= kMaxLevel || enemyLevel <= marioLevel) return 0;
    static constexpr double kMultiplier[] = {0.5, 0.5, 0.5, 0.55, 0.6, 0.75};
    const double mult = kMultiplier[std::clamp(enemiesAtStart, 0, 5)];
    return static_cast<int>(std::floor((enemyLevel - marioLevel) * mult));
}

void ApplyLevelUp(Guest& g, LevelUpChoice choice) {
    const uint32_t pouch = g.PouchPtr();
    if (!Memory::Contains(pouch, 0x20)) return;
    if (choice == LevelUpChoice::Hp) {
        const int maxHp = static_cast<int32_t>(Memory::Read32(pouch + Pouch::maxHp));
        g.Call(Fn::pouchSetMaxHp, {static_cast<uint32_t>(maxHp + 5)});
    } else {
        const int attack = static_cast<int32_t>(Memory::Read32(pouch + Pouch::attack));
        g.Call(Fn::pouchSetAttack, {static_cast<uint32_t>(attack + 1)});
    }
    g.Call(Fn::pouchSetHp, {Memory::Read32(pouch + Pouch::maxHp)});
}

}  // namespace LostPages

// Score no longer levels Mario up: the game compares it against the next level's threshold, never reached now.
// (Score is capped at 99999999.)
extern "C" void pouchGetNextLevelXp_LostPages(CpuContext* ctx) { ctx->gpr[3] = 100000000u; }
REGISTER_NATIVE_FUNCTION(0x8014C70C, pouchGetNextLevelXp_LostPages);
