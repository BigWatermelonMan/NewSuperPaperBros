# O'Chunks, first fight (1-3)

The Yold Desert boss fight in `he3_04` is now a turn-based boss battle.

## How it starts

At GSW0 33 the map spawns the boss as an NPC named `dodon`. It uses a placeholder tribe (0, Goomba, 1 HP) with the
`e_ddtas` model. The native intro scene plays unchanged. Once it gives Mario control back (30 frames with `keyOff`
clear and O'Chunks present), the battle begins. `IsOChunks()` keeps him out of ordinary encounters, so touching
him can't start a one-HP "Goomba" battle.

## Rules

The rules live in `lp_ochunks_rules.h`, and `tools/test-badge-rules.cpp` covers them.

* 20 HP and 0 defence, as in the original. He gives 15 Star Points.
* He attacks in a fixed cycle:
  * **Shoulder Bash** (1 damage). He runs in.
  * **Ground Pound** (2 damage). He leaps over Mario and slams down.
  * **Grab 'n' Toss** (2 damage). He stomps over and grabs Mario. A guard shakes him off for 1 damage, and a
    superguard breaks free and hits him back. A clean grab throws Mario over his head, and he then spends his next
    turn on a victory dance.
* At half HP he enrages ("gettin' chunky"): +1 to every attack. This happens once.
* **Tactics → Thoreau: Toss O'Chunks.** Press A as Thoreau lifts him. A good throw deals 3 damage, ignoring defence,
  and leaves him dizzy for his next turn. Thoreau then rests for 3 rounds.
* You can't run away. Guards, superguards and the defence, dodge and counter badges work as in other battles.

His animations are set by name on his own pose (`npc+0x44`, `animPoseSetAnim`):

| Use | Animation |
|---|---|
| Idle | `S_1` |
| Wind-up | `S_2` |
| Walk | `W_1` |
| Run | `R_1` |
| Hurt | `D_1` |
| Jump (rise, fall, land) | `J_1`–`J_4` |
| Grab | `A_1a`–`A_1c` |
| Dance | `Y_1` |
| Roar | `Q_1` |

The exact sets were picked from the names in `a/e_ddtas`, not checked one by one against the original fight.

## After the fight

Victory sets GSW0 34, the value after the original fight, and reloads the cleared `he3_04`. The original
post-battle scene (the "Maria" line and his escape) is not replayed. That is the same compromise as Fracktail.

## Mario in every battle

* Mario idles with a TTYD-style battle bounce on `S_1`: a squash and stretch anchored at his feet.
* Before Jump, Hammer and Thoreau he winds up for 28 frames with SPM's angry pose and face, `S_5`.

## Testing

* `SPM_SWEEP_GSW0_TABLE` with `he3_04 33`, an input script that presses 2 every 0.5 s through the intro, and
  `SPM_LP_AUTOPLAY=J,T,J,H`. Autoplay `T` picks the Thoreau tactic.
* While autoplay runs it now ignores real input, so the script's presses can't disturb the action commands.
* The sweep now accepts up to 300 seconds, because the intro alone takes about a minute.
* Probes:
  * `SPM_LP_PROBE_NPCS=n` lists each map's active NPCs n frames after the map loads.
  * `SPM_LP_POSE_CYCLE=S_1,E_1,...` steps Mario through poses and shows each name on screen.
* Verified 2026-10-27 (`autotest/och-fight3`): natural intro → battle → both Thoreau tosses → enrage → victory
  (15 SP) → cleared arena at GSW0 34.
