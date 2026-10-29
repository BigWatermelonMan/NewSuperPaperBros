# Digital battle stage and test area

Press **F10 at the in-game title screen** to enter the test area. During normal play, F10 still opens settings.

## Story battles (F5)

In the test area, **F5** opens the story battle menu. It is driven with Up/Down and Enter, or the D-pad and A;
Esc or B closes it, and the game gets no input while it's open. Each entry sets the story (GSW0) to just before
the fight, refills Mario's HP and loads the fight's map through the entrance the story uses. The original intro
scene plays, then the turn-based battle, then whatever the story does next, as in a normal playthrough. Leaving
the test room ends the test session, so the normal badge collection applies again.

| Battle | Map | Entrance | GSW0 | Notes |
|---|---|---|---|---|
| O'Chunks (1-3) | `he3_04` | default | 33 | The intro starts on arrival; the battle starts his theme, `BGM_BTL_DODONTAS1`. |
| Fracktail (1-4) | `he4_10` | `dokan_1` (the pipe from `he4_09`) | 51 | Walk into the arena to wake him. |

Each finished boss battle is added to `StoryBattles()` in `lp_testarea.cpp`.

## Cutscene teleporter (F6)

In the test area, **F6** opens the same menu, listing cutscenes instead of battles. A pick sets the story to the
scene's point and warps to its room. Two seconds after arriving, Mario is placed where the scene plays. A second
later the scene's own script starts, unless the room has already started it. A scene must never run twice: it
asserts in `npcNameToPtr`.

| Cutscene | Map | GSW0 | Mario x | Script |
|---|---|---|---|---|
| The Void destroys Sammer's Kingdom (6) | `wa1_02` | 279 | 0 | `0x80D7B34C` |

Add scenes to `Cutscenes()` in `lp_testarea.cpp`. For automated runs, `SPM_LP_TEST_CUTSCENE=n` picks entry n, and
`SPM_LP_TEST_KEYS=8:F6,11:RETURN` presses the keys for real.

For automated runs:

* `SPM_LP_TEST_STORY_BATTLE=n` picks entry n.
* `tools/test-lost-pages-title.py` takes `SPM_LP_TEST_KEYS=10:F5,12:DOWN,15:RETURN`, which posts real keys to the
  private window that many seconds after the test area opens.
* Verified 2026-10-27:
  * `autotest/story5`: O'Chunks from the menu, through intro, battle and victory to the cleared arena.
  * `autotest/story4`: Fracktail from the menu, through the full intro (WARNING, Fracktail rising, Dimentio) into
    the battle. The run ended mid-fight on time.

Enhanced effects stay on all the way through: the test room keeps its floor reflections and shadows, and so do
the story maps and their battles. `he4_10` now counts as outdoor, so Fracktail's desert arena gets sun rays; the
rest of `he4` (the ruins) stays indoor.

The room uses green-and-white checker and grid blanks over Lineland's existing room collision. It spawns Goomba,
Koopa Troopa, Sproing-Oing, Squiglet and Piranha Plant using the game's enemy templates. More test subjects can be
added to `kRoster` in `lp_testarea.cpp`. F10 → Graphics → Lost Pages offers **Reset test area and enemies** between
battles. This reloads the room and respawns its roster.

Battles draw a green-and-black zigzag border around the game picture, including when the picture is letterboxed.
The border traces onto the screen during the battle introduction. The map then hides, and a pop-up diorama of
the *area* stands up behind the fighters. It isn't a copy of the spot where the battle started. Each area has a
preset built from things typical of it: Lineland Road has striped dome hills, block trees, bushes and block clouds,
while Yold Desert has dunes, cacti and rocks. Each preset has five paper cards:

1. sky
2. far skyline
3. a row of mid props
4. a row of near props
5. large wings framing the sides

A ground strip slides in below the walking line. The far cards stand up first. The cards have white cut edges,
cast drop shadows onto each other, and sway a little once up.

There are 23 presets, one per sub-area, matched against screenshots of each area. They are defined in
`StagePresets` in `aurora-main/lib/gfx/scene_fx.cpp`. Each preset sets a back card (sky or wall pattern), a far
skyline, mid and near prop rows, wing props, a ground pattern, a drawing style (paper, line art, neon or pixel art)
and a 10-colour palette. `StagePresetFor` in `lp_testarea.cpp` maps map prefixes to presets. Set `SPM_LP_STAGE_PRESET=n` to force
one preset in every battle. Test-area battles use the test room as their stage.

Lighting stays on in battle:

* Fighters keep the AO, key light and shadows.
* Fighters drop paper shadows onto the diorama.
* The ground strip is glossy and mirrors the fighters and the scenery.
* Outdoor presets get softened sun rays through the diorama's sky, with the area's tint measured in the field.

Only the battle's own enemies are drawn during a fight (the `npcDisp` hook); other NPCs in the room are hidden.
Active map objects (? blocks, bricks, switches, save blocks: mobjdrv's entries) and field items (itemdrv's) are
parked 10000 units below their spot for the battle and restored when it ends.
Non-flying enemies stand on the stage floor, and flyers hover. `marioGetScreenPos` reports y on the 528-line EFB,
so the stage scales y by 480/528.

For isolated verification, `tools/test-lost-pages-title.py` creates a private runtime folder and starts the title
entry request with `SPM_LP_TESTAREA=1`. Optional `SPM_LP_AUTOPLAY=J,H` drives battles. Output folders must be new;
the helper preserves previous captures. Automated title entry exercises the request used by F10; it does not
simulate a physical F10 keypress.

Only the USA engine has been rebuilt. No test switches are enabled in the installed game.

Verified on October 23, 2026: a posted F10 key event on the private title window opened the room and spawned
all five subjects. An outdoor Lineland battle completed through victory and returned to the map; the map sweep
reported 1,799 frames during its 30-second sample and no missing-texture or dropped-draw issues. Captures are in
`autotest/finish-f10-key` and `autotest/finish-stage-verify`. The reset control is implemented but has not had a
separate UI-driven verification run.
