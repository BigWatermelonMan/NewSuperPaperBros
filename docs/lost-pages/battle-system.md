# Lost Pages battle system (v1)

Super Paper Mario's field combat (stomp an enemy, take contact damage) is replaced with turn-based battles
modelled on *Paper Mario: The Thousand-Year Door*. Code: `wiicompiled/runtime/src/lost_pages/`.

## TTYD reference (what we copy)

| TTYD rule | Source |
|---|---|
| Hitting an enemy in the field (jump/hammer) gives Mario a **First Strike**; enemies can strike first too. | [Super Mario Wiki: First Strike](https://www.mariowiki.com/First%20Strike) |
| Timed **action commands** boost attacks; **Guard** = A within 8 frames of the hit, −1 damage; **Superguard** = B, tighter timing, no damage and counters the attacker. | [Super Mario Wiki: Guard](https://www.mariowiki.com/Guard), [Action Command](https://www.mariowiki.com/Action_command) |
| **Star Points**: per enemy, `floor((enemy level − Mario level) × multiplier)`, multiplier from the enemy count at the start (0.5 for 1–2 … 0.75 for 5); at least 1, at most 100 per battle; nothing from enemies at or below Mario's level. | [Super Mario Wiki: Star Point amounts (TTYD)](https://www.mariowiki.com/List_of_Star_Point_amounts_in_Paper_Mario:_The_Thousand-Year_Door), [Star Point](https://www.mariowiki.com/Star_Point) |
| **100 Star Points = level up**, choose a stat (TTYD: HP +5 / FP +5 / BP +3), level cap 99. | [StrategyWiki: TTYD battle information](https://StrategyWiki.org/wiki/Paper_Mario:_The_Thousand-Year_Door/Battle_Information) |
| Battle menu: Jump, Hammer, Items, Special, Tactics (Defend, Run Away…). | same |

## What v1 does

- **Encounters.** `npcDamageMario` (enemy touches Mario) and `npcTakeDamage` (Mario stomps/hits an enemy) are
  hooked. For ordinary enemies (tribe max HP ≤ 40, so scripted bosses keep their own fights) no field damage
  happens; a battle starts on the spot with the enemy's or Mario's first strike. Up to two more enemies within
  200 units join. Cutscenes (Mario key-off) and non-map sequences never start battles.
- **Stage.** Mario holds his ground, enemies line up opposite him (pipe/flying enemies keep up to 60 units of
  height), the world's scripts are paused (`evtStopAll`) and the overlay draws TTYD's red curtain frame.
- **Mario's turn** (command arc beside Mario, description box at the bottom):
  - **Jump**: run up, jump on the target; press **A just before landing** for a second stomp. ATK per hit.
  - **Hammer**: hold **Left**, four stars light up; **release on the last star** for ATK+1, otherwise ATK.
  - **Items**: the real pouch items with their in-game names. HP items heal by the game's own `hpGain`; attack
    items: Fire Burst 3, Ice Storm 3, Thunder Rage 5, Shooting Star 6, POW Block 2 (all enemies), Shell Shock 6.
  - **Tactics**: Defend (+1 DEF through the enemies' turn) or Run Away (**mash A** to fill the gauge in 2.5 s).
- **Enemies' turn**: each enemy walks up, winds up and lunges. **A** in the last 8 frames = Guard (−1),
  **B** in the last 3 frames = Superguard (no damage, 1 damage back). Pressing early locks you out for that attack.
  Damage = tribe `attackStrength` − DEF, minimum 0.
- **End.** Victory tallies Star Points (and pending level-ups: **HP +5** or **ATK +1**, then a full heal).
  Defeated enemies are finished off by the game itself after the battle (death puff, coin drops, score).
  Defeat hands Mario to the game's own Game Over. Running away gives 2 s of invincibility.
- **Progression.** Star Points and the battle level live in the pouch's unused first word, so they are saved in
  the normal save file (`lp_progress.cpp`). Score is now cosmetic: `pouchGetNextLevelXp` returns a value score
  can't reach, so it no longer levels Mario up (it will buy cosmetics in a Flipside shop later). The pause
  menu's level shows the battle level. Outside battle a small "SP / Lv" chip sits under the HP display.
- **Enemy level** (SPM has none): `6 + 2.6·log2(killXp/50) + 0.5·ATK + 0.15·maxHP`, so Lineland's first foes
  are level ~9 and give ~4 SP at level 1.

## Tuning

| What | Where |
|---|---|
| Action command windows, animation lengths | `TickJump`, `TickHammer`, `TickRun`, `TickEnemy` in `lp_battle.cpp` (frame counts at 60 fps) |
| Enemy line-up spacing, battle radius, max enemies | `Begin` (`78 + 46·i`, 200 units, `kMaxEnemies`) |
| Star Point formula, enemy level, level-up gains | `lp_progress.cpp` |
| Item damage | `ItemDamage` |
| UI layout (menu arc, panels, prompts, popups) | `lp_battle_ui.cpp` (positions in the game's 608×480 space × scale) |
| On/off | F10 → Graphics → Lost Pages → "Turn-based battles" (`[game] turn_based_battles` in Config.toml) |

## Controls (Xbox layout; the game sees a sideways Wii Remote)

A (= 2) confirm / jump timing / guard / mash · B or X (= 1) back / superguard · D-pad or stick: move the cursor,
hold Left for the hammer.

## Test switches (environment variables)

`SPM_LP_AUTOPLAY=J,H,I,D,R` plays battles itself with perfect timing (cycling commands, alternating guard and
superguard) · `SPM_LP_SP_BONUS=150` extra Star Points per win · `SPM_LP_GIVE_ITEMS=0x50,0x41` fills the item
bag at battle start · `SPM_LP_ENEMY_ATK_BONUS=15` forces a defeat · `SPM_LP_CLASSIC=1` original field combat ·
`SPM_LP_RECON=1` logs collision hooks. `tools/sweep-spm-maps.py` with `SPM_SWEEP_INPUT` /
`SPM_SWEEP_BOOTSHOTS` drives and screenshots them.

## Not in v1 yet

FP and Special moves (Star Power, audience), badges/BP, partners (Pixls as partners), Stylish moves, spiky/flying
rules (can't jump on spikes, can't hammer fliers), a real hammer swing animation (Mario hops into the target for
now), enemy-specific attacks and defense values, status effects, battle sounds and music, Mario facing the
enemies when they come from the left.
