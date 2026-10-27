// Lost Pages progression: TTYD-style Star Points and levels. Score stays in the game as a cosmetic counter
// (it will buy cosmetics in a Flipside shop later) and no longer levels Mario up.
#pragma once

#include "lp_guest.h"

namespace LostPages {

constexpr int kMaxLevel = 99;
constexpr int kStarPointsPerLevel = 100;

struct Progress {
    int level = 1;
    int starPoints = 0;
};

// Read/write the Star Points and level kept in the save file (the pouch's unused first word).
Progress LoadProgress(Guest& g);
void SaveProgress(Guest& g, const Progress& p);
bool PowerBounceEquipped(Guest& g);
void SetPowerBounceEquipped(Guest& g, bool equipped);
void DrawBadgesMenu();
bool NativePauseOpen();

// TTYD: each enemy gives floor((enemyLevel - marioLevel) * multiplier) Star Points, the multiplier set by how
// many enemies started the battle; a battle always gives at least 1 and at most 100.
int EnemyLevel(int killXp, int attack, int maxHp);
int StarPointsFor(int enemyLevel, int marioLevel, int enemiesAtStart);

enum class LevelUpChoice { Hp, Attack, BadgePoints };
// +5 max HP, +1 attack or +3 BP, followed by HP/FP recovery.
void ApplyLevelUp(Guest& g, LevelUpChoice choice);

}  // namespace LostPages
