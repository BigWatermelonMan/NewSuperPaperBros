# Fracktail: first playable battle

The Chapter 1 encounter now enters the turn-based battle after the native introduction reaches its battle phase. Fracktail uses the original `e_zun_all` upright model, partly clipped below the diorama floor, with its head above Mario and turned toward him. The original `BGM_BTL_BOSS_STG1` music continues. The dragon now renders at 3.15 native scale (3.5 times the first playable version), with Mario and minions at their existing scale. Its rest position is farther across the stage. Both upright poses sit 80 world units higher than the first giant-scale revision, exposing more of the body. The boss camera measures the room's native screen scale and normalizes it to the same combat framing in the test room and desert arena. Mario's home position stays at the left of the stage, with the floor and world-attached UI using the same 480-line viewport mapping. The dragon/Mario size ratio stays unchanged; the original view and projection matrices are restored on exit. Throws and jumps target the animated antenna orb (with the stalk pivot as a fallback), following the native movable-allocation handle correctly. Mario launches from outside the enlarged snout and boss landings use the full 3D target position.

Fracktail has 24 HP and cycles through:

- Charge: switches to the original flying head/body/tail models with a dedicated visible neck connector in a straight horizontal rig, crosses Mario's position, then returns to the upright model; 4 base damage.
- Summon Frackles: restores two 2-HP, 1-ATK minions.
- Digital Breath: a stream of digital particles; 2 base damage.
- Circuit Breaker: glitch bands and digital fragments; 1 base damage and ATK -1 for two following player turns. Guarding, evasion or Feeling Fine prevents the debuff.

The raised antenna is protected. Choose Tactics > Thoreau while a Frackle is alive, then press A/2 during the timing cue to throw it at the antenna. A successful throw consumes the minion and exposes the weak point for the next two player attack turns. A failed throw also consumes that minion. Summoning replenishes ammunition. Normal defense, guarding, superguarding, badges and status effects apply; the boss cannot be fled from. Winning grants 20 Star Points.

Enter testing with F10 on the title screen, then open F10 settings and choose Challenge Fracktail. This uses the separate test collection and returns to the testing field afterward. The boss can be challenged repeatedly there.

This is a first playable implementation. Story victory advances the native clear flag and reloads the cleared arena, allowing progress toward the Pure Heart; the original extended defeat cinematic is not integrated yet. Natural introduction-to-battle timing and all badge combinations still need broader playtesting. Live evidence and known verification limits are in `MODLOG.md`.

Developer fixtures (private copies only): `SPM_LP_TEST_FRACKTAIL=1`, `SPM_LP_AUTOPLAY=B`, `SPM_LP_TEST_NO_GUARD=1`, and `SPM_LP_TEST_MISS_THROW=1`. `SPM_LP_TEST_FRACKTAIL_STORY=1` tests the arena handoff independently of the full introduction. `SPM_LP_TEST_HERO_HP` applies only in badge testing. `SPM_LP_TEST_LEVEL_CHOICE=2` selects BP. `SPM_LP_TEST_PICKUPS=0x59,0x5a` calls the native acquisition path for the two stat badges.

Presentation follow-up: a native follower graphic remains visible during some Mario jumps alongside the custom-positioned Thoreau. Its draw path still needs cleanup.
