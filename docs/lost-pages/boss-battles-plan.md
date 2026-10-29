# Plan: boss battles, dialogue and battle transitions

Status (2026-10-28): implemented and reviewed. Transition, declutter, Left/Right menus, dialogue, Special/Pure Shield, Count Bleck and Super Dimentio are in the engine. The battle HUD is a native-style overlay: the battle diorama is a post-process pass over the guest EFB, so guest-drawn HUD entries are hidden behind it. See MODLOG.md for validation and limits.

## What was asked

1. **Battle transition.** When any battle starts (normal and boss), the green digital effect covers the whole screen briefly. Then it
   clears and the pop-up diorama appears.
2. **Music on impact.** Battle music starts the moment the transition hits.
3. **Boss dialogue.** Bosses talk in battle using the game's normal dialogue boxes, including O'Chunks' existing
   lines.
4. **Declutter.** Remove the enemy attack-name banners and other on-screen clutter.
5. **Native HUD.** HP hearts and Star Points should be part of the game's own HUD, not overlays.
6. **Icons.** Better battle command icons.
7. **Menu controls.** Battle menus are navigated with Left/Right instead of Up/Down.
8. **Mario's pose.** Mario gets the scared, open-mouthed, arm-waving pose. It does **not** replace his battle idle
   (S_1 with the TTYD bounce). This is deferred until the user finds the animation; don't run pose surveys.
9. **Count Bleck**, turn-based, with the same number of hits as the original fight, counted in turns:
   * **Phase 1:** his original attacks, a bit more varied.
   * **Phase 2:** starts once he can be damaged. It keeps all his normal attacks, adapted to turns (time slow,
     for example), and adds:
     * **Void Sprites:** small, distorted Void Pixls that help him and fire small void blasts. Bleck can absorb
       them with his open-cape animation for +ATK and +DEF.
     * **Reality Split:** three white void flashes with a very hard dodge timing, then void energy envelops Mario
       for massive damage. Pure Shield mitigates it.
10. **Pure Shield.** A new Special menu holds one skill, Pure Shield, the combined power of all the Pure Hearts.
    Add no other Pure Heart skills yet.
11. **Super Dimentio:**
    * **Phase 1:** handled like Bleck's.
    * **Phase 2:** a swarm of mini Luigis is summoned. The swarm counts as a second enemy with high HP. They all
      jump on Mario at once, each for low damage.
    * His other attacks are adapted to turns.
    * **Voidsplitter:** a void tinted Luigi green and Dimentio purple. It sharply lowers Mario's stats and deals
      immense damage. Pure Shield mitigates it.
    * **Lover's Grief:** a special skill for this battle only, dealing moderate to high damage.
12. Add both new boss battles to the F5 story battle list.
13. Build, test and install. Commit or push only when asked.

## Findings so far (verified in the game files and map)

### Event bytecode

* The `USER_FUNC` opcode is `0x5C`. Its header word is `(argument count << 16) | 0x5C`. For example, `0x5005C` is
  followed by `evt_msg_print` and its 4 arguments.
* `evt_msg_print` is at `0x800FB668`. Its arguments are `(flags, msgName, 0, speaker)`.
  * In `main.dol`, the koburon lines are called as `(0, "koburon_20", 0, <"me">)`.
  * Flag bit 1 (`EVT_MSG_FLAG_DIRECT` in the decomp) takes the text literally. This is untested.
* Related functions:
  * `evt_msg_print_add` at `0x800FB878` (3 arguments)
  * `evt_msg_select` at `0x800FBB60`
  * `evt_msg_continue` at `0x800FBA88`
  * `evt_msg_close` at `0x800FBB30`
* Under the hood, `_evt_msg_print` (`0x800FAE94`) calls `msgWindow_Entry` at `0x80032B24` with
  `(text, r31, (s16)evt[+0x188])`, then attaches the speaker. Calling `msgWindow_Entry` directly would need more
  reverse engineering.

### HUD functions

| Function | Address |
|---|---|
| `hudMain` | `0x801986B0` |
| `hudHide` | `0x80198CA0` |
| `hudUnhide` | `0x80198D78` |
| `hudUpdateStats` | `0x80198F68` |
| `hudLoadStats` | `0x80198660` |
| `evt_sub_hud_configure` | `0x800D4F58` |

The native HUD row shows HP (heart), SCORE and COINS. The battle currently hides it and draws `StatusPanel`
instead. Outside battle, `FieldChip` (SP, Lv, BP) is drawn under the HUD.

## Implementation steps

### 1. Transition and music (`lp_battle.cpp` `Begin`/Intro, `lp_battle_ui.cpp`)

* Lengthen `BattlePhase::Intro`:
  * frames 0–18: a green digital cover sweeps in (pixel blocks and scanlines from the edges)
  * frames 18–34: the screen is fully covered
  * frames 34–56: the cover dissolves
* Delay `SetStage(g,2,…)` and `mapDispOff` until about frame 28, while the screen is covered, so the diorama's
  stand-up animation plays as the cover lifts.
* Move the zigzag draw-on after the cover. Delay the enemies' slide-in and the menu by the same amount.
* Normal battles get the same transition as boss battles.
* Start boss BGM in `Begin`/`BeginOChunks` on frame 0 (it already starts there). Normal battles keep the field
  music; no new track was requested.
* Draw the cover in `DrawBattleUi` from `ui.phase == Intro && phaseFrame`. Drop the "BATTLE!" banner.

### 2. Declutter

* Delete the attack-name `Banner(...)` calls:
  * O'Chunks: around line 1011
  * Fracktail: around line 1155
  * Mario's move and item names: around lines 1578 and 1591
* Keep only VICTORY, defeat, "Got away!" and LEVEL UP.
* Remove these overlay elements:
  * the Fracktail info panel text ("Partner: Thoreau", "Head raised…")
  * the "Lv/ATK/BP" and "FP/Energy/Turn" text panels (move FP/Energy into the HUD, step 4)
  * the "DEF n" labels
* Show the guard prompt text only when the Timing Tutor badge is equipped.

### 3. Left/Right menus

In `Tick`, switch the Menu, MoveMenu, ItemMenu and TacticsMenu phases from `Btn::screenUp/Down` to
`screenLeft/Right`.

* Watch out: the hammer uses held `screenLeft`, but only in MarioAction, so the two don't conflict.
* Redraw the command menu as a horizontal row of 4 (or 5, with Special) icons above Mario.
* Redraw `ListMenu` as a horizontal carousel: the selected card in the centre, neighbours faded at the sides.

### 4. Native HUD and icons

* In battle, call `hudUnhide` (`0x80198D78`) each frame instead of drawing `StatusPanel`, so the game's own HP
  heart shows.
* Add Star Points and FP in the HUD's own style (italic outlined counters with a rainbow zigzag underline) to the
  right of COINS. Remove `FieldChip`.
* Better still: find where `hudMain` draws the SCORE counter, and hook it to add an "SP" counter using the HUD's
  own number textures.
* Replace the hand-drawn boot, hammer, mushroom and flag icons with cleaner ones in `CommandIcon`. Options:
  * load the game's own item icons through the icon drawing functions
  * use higher-detail vector shapes with outlines and shading
* Add a Special icon (a heart with a rainbow edge).

### 5. Boss dialogue

* Write a tiny evt script into guest heap (via `PersistentString`/heap alloc):
  `USER_FUNC(evt_msg_print, flags, msg, 0, speaker); RETURN`. The `RETURN`/`END_SCRIPT` opcodes come from
  existing scripts in `main.dol`.
* Start it with `evtEntry` after `evtStopAll`; check that new entries aren't stopped.
* Add a `BattlePhase::Talk` that waits until the script ends before continuing.
* Speaker: the boss NPC's name (`"dodon"` for O'Chunks), so the bubble points at him.
* O'Chunks: find his `stg1_3`/`ddn` msg names in `spm_linked.rel` and reuse them at battle start, enrage and
  defeat.

### 6. Count Bleck (new `lp_bleck.cpp` and `lp_bleck_rules.h`, like the O'Chunks files)

* Find his map (chapter 8 final room), GSW0 value, NPC name, model and anims with
  `SPM_LP_PROBE_NPCS`/`play-script.py`. His music is likely `BGM_BTL_BOSS_LAST1`.
* Phase 1:
  * Invulnerable, as in the original fight.
  * Attacks: void beams, the falling-block card attack and teleports, chosen with more variety.
  * Phase 2 begins after the same number of hits the original needs.
* Phase 2:
  * Damage is enabled.
  * Time slow: Mario's timing windows are halved for one turn.
  * Void Sprites: extra enemy slots with low HP and small blasts. The Absorb action plays the cape-open animation
    and gives +1 ATK and +1 DEF per sprite.
  * Reality Split:
    * three white full-screen flashes (overlay effect), each with a superguard window of about 2 frames
    * a full miss deals heavy damage
    * an active Pure Shield cuts the damage to about 1
* Pure Shield: a new `Action::PureShield` in a new Special menu entry. It costs Star Energy, lasts until the next
  enemy turn and shows a rainbow heart shield effect.

### 7. Super Dimentio (`lp_dimentio.cpp` and rules)

* Find his map (chapter 8), GSW0 and model the same way. His music is likely `BGM_BTL_DIMEN1`/`BGM_BTL_BOSS_LAST1`.
* Phase 1 works like Bleck's.
* Phase 2:
  * A "Mini Luigis" enemy in slot 2 with high HP.
  * Their turn: N small Luigi sprites (overlay or reused NPC models) all jump at once, each for 1 damage.
  * Guarding needs one guard timing for the whole swarm.
* Voidsplitter: a green and purple tinted void effect. It lowers ATK and DEF for 3 turns (the status system) and
  deals immense damage. Pure Shield mitigates it.
* Lover's Grief: a Special menu entry for this battle only, dealing 6–8 damage with a pink and green effect.

### 8. Story list, tests and installation

* Add Bleck and Super Dimentio to `StoryBattles()` in `lp_testarea.cpp`.
* Extend the autoplay test (`SPM_LP_AUTOPLAY`) with an `S` key for Special.
* Add rules asserts in `tools/test-badge-rules.cpp`.
* Build with `tools/build-spm.ps1 -SkipTools -SkipTranslate` (without `-SkipTranslate` if hooks change). Test with
  sweep or story runs, then copy the exe and `engine_usa.dll` into `NewSuperPaperBros\engine`.

## Deferred

* Mario's scared pose: the user will point the animation out later. It is not his battle idle. Where it
  plays (for example when Reality Split or Voidsplitter hits) is decided then.
