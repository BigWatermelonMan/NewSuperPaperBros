# Digital battle stage and test area

Press **F10 at the in-game title screen** to enter the test area. During normal play, F10 still opens settings.

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

There are 16 presets, defined in `StagePresets` in `aurora-main/lib/gfx/scene_fx.cpp`: shapes plus an 8-colour
palette. `StagePresetFor` in `lp_testarea.cpp` maps map prefixes to presets. Set `SPM_LP_STAGE_PRESET=n` to force
one preset in every battle. Test-area battles use the test room as their stage.

Lighting stays on in battle:

* Fighters keep the AO, key light and shadows.
* Fighters drop paper shadows onto the diorama.
* The ground strip is glossy and mirrors the fighters and the scenery.
* Outdoor presets get softened sun rays through the diorama's sky, with the area's tint measured in the field.

Only the battle's own enemies are drawn during a fight (the `npcDisp` hook); other NPCs in the room are hidden.
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
