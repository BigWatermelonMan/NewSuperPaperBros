// Lost Pages: TTYD-style turn-based battles. Touching an enemy (or being touched) no longer deals damage
// in the field; it starts a battle on the spot instead. Mario attacks with action commands (Jump, Hammer),
// uses items, defends or runs; enemies take turns attacking and can be guarded or superguarded.
#pragma once

#include "lp_guest.h"

#include <cstdint>
#include <string>
#include <vector>

namespace LostPages {

enum class BattlePhase : uint8_t {
    None,
    Intro,       // "BATTLE!" and everyone takes their place
    Menu,        // Mario picks a command
    Target,      // ...and an enemy
    ItemMenu,
    TacticsMenu,
    MarioAction, // Jump / Hammer / item / run, with action commands
    EnemyAction, // the enemies' turn, guard with timing
    Victory,     // Star Points tally
    LevelUp,     // choose HP or ATK
    Defeat,
    Fled,
};

enum class UiPrompt : uint8_t {
    None,
    PressA,      // timed press (Jump)
    HoldLeft,    // hammer: hold left, release on the last star
    Mash,        // run away: mash A
    Guard,       // enemy attacking: A guard, B superguard
};

// What the overlay draws; the game thread fills it once per frame.
struct BattleUi {
    struct Actor {
        float x = 0, y = 0;  // screen position in the game's 608x480 space (feet)
        int hp = 0, maxHp = 0;
        bool alive = false;
        std::string name;
    };
    struct Popup {
        float x = 0, y = 0;
        std::string text;
        uint32_t color = 0;  // ImGui ABGR
        int kind = 0;        // 0 damage, 1 word, 2 heal
        int age = 0;
    };

    uint32_t serial = 0;  // bumps every published frame
    bool active = false;
    BattlePhase phase = BattlePhase::None;
    int phaseFrame = 0;
    Actor mario;
    std::vector<Actor> enemies;
    int hp = 0, maxHp = 0, attack = 0, level = 1, starPoints = 0;

    int menuIndex = 0;  // Jump, Hammer, Items, Tactics
    int targetIndex = -1;
    std::vector<std::string> list;  // item or tactics names
    int listIndex = 0;

    UiPrompt prompt = UiPrompt::None;
    float gauge = 0;  // 0..1
    bool promptLit = false;

    std::vector<Popup> popups;
    std::string banner;
    int bannerAge = 0;

    int starPointsGained = 0;
    int levelUpChoice = 0;  // 0 HP, 1 ATK
};

BattleUi BattleUiSnapshot();
// An enemy tribe's display name (its Catch Card name in the game's language).
std::string EnemyName(Guest& g, int tribe);
void DrawBattleUi();  // ImGui; from settings_overlay::Draw

}  // namespace LostPages
