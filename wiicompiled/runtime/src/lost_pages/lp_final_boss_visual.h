#pragma once
#include "lp_guest.h"
#include <array>

namespace LostPages {
// Matches the combat controller's finalBoss: 0 none, 1 Bleck, 2 Super Dimentio.
struct FinalBossVisual {
    int kind = 0;
    bool active = false, attacking = false, absorb = false;
    bool drawBoss = true;  // false: the story's own actor is the boss; only the helpers are drawn here
    bool miniLuigisAttacking = false;
    int miniLuigis = 0, voidSprites = 0; // clamped to 8 and 3
    Vec3 root{};
    float facing = 0.f, scale = 1.f;
    // Optional world positions supplied by the combat actor layout. A swarm
    // position anchors all six/eight tiny Luigis; it remains one combat actor.
    bool useHelperPositions = false;
    Vec3 miniLuigiRoot{};
    std::array<Vec3,3> voidSpritePositions{};
};
// Call once per game tick. Owns independent native animation poses, never NPCs.
void UpdateFinalBossVisual(Guest& g, const FinalBossVisual& view);
void ReleaseFinalBossVisual(Guest& g);
// False until a valid boss is active. World position for attacks / target cursor.
bool FinalBossTarget(Guest& g, Vec3& point);
}
