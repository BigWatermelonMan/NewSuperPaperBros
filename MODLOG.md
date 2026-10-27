# Local modding notes

## 2026-10-24: native Badges pause-menu integration (USA)

The native plus menu uses `pluswinOpen` (0x801851E8), `pluswinMain` (0x80185A58), `pluswinClose` (0x80185800), and `pluswinStatsDisp` (0x801862A4). Its work pointer is at r13-32272: entry IDs start at +4, submenu state is +132, selection is +136, cursor target is +144/+148 and displayed position is +152/+156. The outer pause state 50 is the top-level plus menu.

`pausewinEntry` (0x80183F2C) allocates a real 120-byte entry. Type 1 is the game's plain paper frame; position/size are +4/+8/+12/+16, settled position +36/+40, opacity +52, drawing type +64, display callback +112. `pausewinEntryDisp` invokes callbacks with the entry address in r3. Entries use a shared pool, so an entry ID is not a menu row index. The native close routine manages only the original eleven entries; the extra Badges entry must be deleted explicitly, using 0x80184588 with its ID.

Native left-column row 3 is Important Things; rows 4-7 are the remaining conditional entries. Badges is inserted between row 3 and the first available row after it, without using unsupported indices in the original eight-node navigation tables. The normal menu's repeat accessor (0x802350F4) preserves held-direction timing. Selection flags 0x10/0x100 and the native glove renderer (0x80186238) are updated on entry/exit. Normal help uses `menu_help_%03d`; badge help owns a short game-heap string and releases it on close.

The equipment page reuses the original right panel, switching its drawing type to plain paper while selected and restoring the original stats texture on exit. Native FontDrawStart/Color/Scale/String render its text. Scripted tests under `autotest/badges-native-*` show the actual game's menu, equipment state, navigation and reopen behavior. Builds/logs and extracted inputs stay local; no game assets or decompiled output were added to these notes.

## 2026-10-25: animated teal Badges tab

The Badges row now uses a callback-only type-2 entry and native GX triangle fans (private vertex format 7: direct XYZ/RGBA8) for a layered, chamfered teal tab. Geometry and lettering share the entry's current position, opacity and selection animation. The shimmer is clipped to the inner face. Color/vertex/matrix setup goes through the existing guest GX hooks and FIFO helpers so the runtime's tracked state stays consistent. Each native font draw restores its own text setup afterward.

Native UI geometry needs the current camera's view matrix at `camGetCurPtr()+0x54`, as used by `FontDrawStringMtx`. An identity matrix clips these polygons even though separately transformed text remains visible. Using the current view fixes alignment and clipping. In-game captures under `autotest/badges-teal-camera-2` confirm the full tab and different selected-animation phases; the initial `badges-teal-button-1` capture records the transform issue before correction.

### Matching native button and label sizes

Read the user's local `win/us/pause.tpl`: image indices 3 (Important Things) and 4 (Chapters) are both 120x40 RGB5A3 textures. Their visible alpha bounds are x=2..117 and y=0..37. Badges now copies the live Important Things dimensions, includes its extrusion inside that same 116x38 visible footprint, and uses native font scale 1.0 instead of the previous 0.85. `FontGetMessageWidth` (0x801A1B20) measures Badges as 96 unscaled pixels for centering. Selection no longer grows/shrinks the text or shape; the sheen remains animated. `autotest/badges-native-size-*` records the alignment checks; the sizing log reports the live native dimensions and label scale.

### Block lettering and visible-border alignment

The dialogue BFN differs from the heavy lettering baked into the tab textures; adjusting its scale alone did not match the appearance. The tab label now uses a fixed, antialiased alpha mask generated from the free Mario-style font credited in `assets/fonts/README.md`. `tools/make-badge-label.py` bakes the label at an 18px ink height, merges equal alpha runs into rectangles, and emits `lp_badge_label_art.h`. The native GX callback draws the mask, dark stroke and shadow without a runtime font/file dependency. It does not claim to be the original shipped tab font.

The custom drawing origin needed a five-game-pixel up/left correction relative to the logical entry origin. Measured from `badges-block-label-1/shots/28.bmp`, all left outlines align at x=242; the neighboring vertical centers are y=179/299 and Badges is y=238.5. The geometric midpoint is y=239. This measures the actual rendered outlines rather than relying only on texture metadata.


## 2026-10-25: full badge catalogue and animated battles

Implemented the 85-entry catalogue from the supplied TTYD badge database, with original paraphrased descriptions and explicit SPM adaptations in `docs/lost-pages/badge-catalog.md`. The existing native teal tab/label was retained; its panel now browses five entries with page navigation, ownership/equipment marks, BP/FP costs and contextual help. Normal saves start with zero badges. F10 testing uses a separate in-memory full collection with 999 BP; no test equipment is written into the normal inventory.

Added FP/BP, jump/hammer move lists, status durations and skips, multiple-target attacks, shields and guard/superguard poses, hammer/projectile/fire/ice/quake/tornado/electric/healing/charge effects, five distinct Attack FX shapes/sounds, recovery/danger/evasion/defense/counter passives, real item chains and refunds, victory drops, Star Energy/Appeal/Digital Burst, and hero changes. P variants apply to the active alternate playable hero; repeated copies of one badge are not implemented. L/W Emblem are green/gold whole-sprite tints (purple together), not new texture costumes. The emblem color pass is enabled after native `marioPreDisp`; applying event color only during updates did not survive the draw preparation. The pass preserves native alpha.

NPC tribe-animation fallback now selects only an actual animation ID. The battle moves linked NPC parts and their owner transforms together, forces the participating models into the active camera with visible alpha, sets hero/enemy facing independently, and restores transforms/flags/visibility on exit. Cleft/Bristle/Koopatrol battle armor is assigned explicitly rather than treating SPM's fallback immunity values as universal armor. Ordinary enemies correctly have zero battle armor.

Badge profiles use atomic sidecar files linked by a 10-bit token in the existing unused pouch word (0x4D format); old 0x4C level/SP migrate without granting legacy Power Bounce. Effective HP bonuses do not alter saved base max HP. Keep `UserData/LostPages/Badges` with saves. Missing/corrupt profiles become empty collections. Pure format and rule tests use the same headers as the game.

Validation:

- Release build including full translation for the native HP/material hooks; subsequent release compilation passed. `git diff --check` passed.
- `tools/test-badge-rules.cpp`: PASS for 85 unique definitions, locked defaults, ownership/equipment/FP/energy serialization, corrupt/truncated profile recovery, piercing, All or Nothing, guard/defense/Double Pain/Last Stand rounding and timing bounds.
- `autotest/badge-catalog-normal-menu-1`: native normal collection shows 0/85, 0/3 BP, locked entries. `badge-catalog-test-menu-1`: 85/85 test collection, scrolling and equip actions, original aligned teal tab retained. The footer was subsequently lifted clear of the native help box.
- `badge-catalog-jump-matrix-1`: all seven equipped jump moves cycled in a three-enemy battle; multiple-target bounce, status moves and seven Power Bounce hits observed. `badge-catalog-bounce-2` verifies normal damage and Mario facing the target.
- `badge-catalog-hammer-final`: all seven hammer moves, correct zero armor for the normal roster, multiple-target Quake/Fire damage, confusion/freeze/burn statuses and skipped frozen turns. Captures show visible Koopa (two model parts), Squiglet and Piranha Plant facing Mario. Initial hammer-matrix captures retain the immunity-sentinel bug as diagnostic evidence; that behavior is corrected in the final run.
- `badge-catalog-hero-1`: Quick Change cycled Peach, Bowser, Luigi and Mario; alternate heroes dealt 2 per basic hit with Power Plus P, Mario returned to 1. Effective HP/FP maxima reflected base/P HP Plus and FP Plus. All four heroes rendered and animated.
- `badge-catalog-items-victory`: Double Dip consumed the actual Mushroom and Fire Burst, restored FP, applied Refund, defeated the three enemies and returned to the field; another encounter then worked.
- `badge-catalog-emblem-pass`: native preparation trace and live capture confirm the green emblem material pass. Earlier palette captures document the unsuccessful update-only and material-setter attempts.

Test runs use private portable copies, internal Wii Remote inputs, offscreen window capture and fresh/copied fixtures; user save files are untouched. A full native save-block/relaunch cycle, every badge/enemy combination, all late-game enemies and map transitions with equipped badges have not been playtested. Field weak-enemy bypass and some situational passives are implemented but were reviewed in code rather than individually exercised live. The save serializer round trip is not a substitute for a native save-block/relaunch test.

Installed the final USA engine and loader into NewSuperPaperBros/engine. Backup: engine/backup-before-badge-catalog-20261025-152759. Engine SHA-256: 950D1E3CABB7D737CC993B6A62753E66B4E0813F3915260DADDBE25CC5B62E97. Loader SHA-256: F7FFDFC92FFD47792960F29A183D82017D14A613A52315914ACF4CF1C3354844. Installed hashes match Play. The installed-engine smoke run (autotest/badge-catalog-installed-smoke) confirmed 0/85, normal 3 BP, and Not obtained on a locked equip attempt. The final placement-only follow-up moves the footer and control hint fully inside the paper panel; compilation passed.



## 2026-10-25: BP, stat-pickup badges and Fracktail

Route: extend the existing native recompiled battle hooks and draw the user's original boss/Pixl models through native animation/display callbacks. Keep the native Chapter 1 boss music and preserve story scripts outside combat. Stat pickup IDs confirmed against public spm-headers item IDs: 0x59 HP Plus, 0x5A Power Plus. Intercept native pouch acquisition, support old held consumables, and keep these pickups as unequipped badges instead of permanent stat increases. BP becomes a saved stat (3 initially; +3 as a third level-up choice). Profile v3 reads v2 and preserves the previous automatic budget at migration.

Recon: Fracktail is tribe 313; Frackle is 450 (e_kozun). The original encounter's full boss model is e_zun_all, not e_dodon (O'Chunks). Map he4_10 contains the encounter; he4_11 is the Pure Heart room. Native model animation definitions at REL 0x80CF3518 include zun_all_S_1B (idle), zun_all_S_2B, zun_all_Z_1, zun_all_I_1, zun_all_M_1, zun_all_D_1 and zun_all_H_1. Original music label is BGM_BTL_BOSS_STG1. The new fight uses the original Thoreau FRY_throw model and Frackles to lower the head, then expose the antenna.

Implemented the original 0x59/0x5A pickups as unequipped HP Plus/Power Plus badges, including native acquisition/ownership checks, badge-specific item text and legacy held-item conversion. Normal acquisitions do not alter base HP/ATK and work with a full consumable inventory. BP is now a saved profile-v3 stat; v2 migration preserves the former level-derived budget. Level-up selects HP +5, ATK +1 or BP +3 (cap 99), then refills effective HP/FP. Native pause/field overlays avoid drawing over one another.

Fracktail first playable pass: original e_zun_all upright S_1B pose, buried floor clipping, native Antena joint targeting, animated Frackles/Thoreau, 24 HP, raised-head damage immunity, timed Thoreau throws opening two player attack turns, four-attack cycle (Charge 4, summon two 2-HP minions, Digital Breath 2, Circuit Breaker 1 plus temporary ATK -1). Guards, evasion, counters, status badges and death handling participate. Circuit glitch/particle effects and the original boss theme are retained. F10 test settings expose Challenge Fracktail. Story victory grants 20 SP, sets GSW0=52 and reloads the cleared he4_10 arena. The original extended defeat cinematic is not integrated; the fixture handoff passed, while a full natural intro-to-battle playthrough remains unverified.

Validation:

- Full release translation/compilation passed (build-logs/fracktail-release-build.log); pure badge/BP/profile/pickup/Fracktail rules tests and git diff --check passed.
- autotest/stat-badge-pickups-1: two owned badges in normal collection, base HP10/ATK1/BP3, profile v3 written.
- autotest/bp-level-up-1: BP choice writes budget6 with base HP/ATK unchanged. Native save-block/relaunch remains untested.
- autotest/stat-badges-full-bag: ten Mushroom acquisitions followed by both stat pickups; native pause shows 2/85 owned and 0/3 BP, with base HP10/ATK1 unchanged.
- autotest/fracktail-attack-cycle-2 and fracktail-final-visual: all four attacks, repeated successful throws, exposure/closure, real 4/2/1 damage, Circuit ATK debuff, visible original models and guard poses. Latest antenna evaluation resolves group169. fracktail-vsync-off captures native antenna-targeted jumps and visible Frackles; shots44/52/60 show charge, lowered head and jump respectively.
- autotest/fracktail-story-handoff-1: fixture victory grants20SP and returns to the cleared arena (GSW0=52), opening progression. This fixture bypasses the natural introduction and does not validate that entire sequence.
- Late offscreen tests encountered roughly 4FPS at startup with BOTH a known-good prior engine and the new engine. Disabling V-Sync solely in private test configuration restored normal speed. Controller-disable comparisons did not help. Installed configuration and user saves were not changed. Root cause of that environmental/display-timing interaction is not established.

Installed USA engine and loader; backup engine/backup-before-fracktail-20261025-210935. Engine SHA-256 BF42F5B65EC96CFD1555D21B0FC780F42ADF497E0977DA5F8E703C62C840C8E5; loader SHA-256 40AEFFF6CF3BAA17479E631AC060309919C6FFF03E92997B007DCD3CC2DCD1CB. Both installed hashes match Play. Broader boss/badge balancing, native save-block roundtrip and the full original outro remain future verification/integration work.

Installed-engine smoke (autotest/fracktail-installed-smoke) passed: native antenna group169, visible upright/lowered boss, both Frackles, antenna jump placement, successful timed Thoreau throws, all four attacks and Circuit debuff. Captures26/34/42 reviewed. A native follower graphic still accompanies Mario during some jumps alongside the separately positioned Thoreau actor; follower presentation cleanup remains pending. Disabling the native fairy update did not fully remove that graphic. This does not block the tested fight loop.

## 2026-10-26: Fracktail giant scale

Research: original encounter description and screenshots (https://www.mariowiki.com/Fracktail) show Mario boarding the dragon's back and using Frackles against its antenna; Hooktail encounter/battle screenshots (https://www.mariowiki.com/Hooktail and https://www.mariowiki.com/File:PMTTYD_Hooktail_Battle.png) supply the requested large-dragon framing reference. This is a visual adaptation, not a claim of an exact shared physical unit between the two games.

Route: retain the native e_zun_all model, antenna-joint targeting and normal Mario/camera scale. Increase dragon draw scale from0.9 to3.15 (3.5x); separate raised/lowered buried root placement and move the rest position away from Mario. Keep native animation, minions, music and battle rules. Validate raised head, charge, Thoreau throw and jump framing before copying the module into the installed game.

Visual iteration: the initial giant-scale root heights buried the head when exposed (fracktail-giant-scale-1), so raised/lowered offsets were corrected to -400/-475 and the antenna fallback estimate scaled with the model. fracktail-giant-framing-final captures22/32/36 confirm the giant raised pose, exposed head and intact minion/boss turn flow. At this size the old 34-unit jump run-up placed Mario beneath the snout; boss-only takeoff clearance is now120 units, with ordinary enemies unchanged. Actual antenna-joint targeting remains authoritative.

Further visual validation found that antenna acquisition had been falling back silently: native pose+372 is a smartAlloc allocation handle, not a raw Vec3 array. Native routines80048B30/8004CFB0 explicitly dereference that handle before indexing group positions. FracktailAntenna now follows the same indirection. Boss jumps also use the actual antenna Z rather than forcing the normal ground-plane Z. Bounded coordinate diagnostics are enabled only in the explicit Fracktail test fixture. This fixes targeting for the enlarged draw transform rather than compensating with another hand-tuned screen offset.

The orb/stalk groups share a pivot; the visible orb center is offset within the mesh. Antenna targeting now enables native world-matrix evaluation and transforms a calibrated model-space orb-center offset (-16,+20,0) by that evaluated group matrix. This preserves the center under scale and animation instead of treating a group pivot as a mesh center. Native matrix arrays use a smartAlloc handle at pose+380, count+384. The smaller first-pass version's estimated targeting had hidden this discrepancy.

Final orb calibration uses local center(-13,+24,0), validated against the lit mesh rather than the shared group origin. Protected-head jump attempts now fall short at a visible height; an exposed weak point still receives the real orb landing. This avoids launching Mario above the screen when the raised giant antenna is protected. Release compilation passed (build-logs/fracktail-giant-release.log).

Final verification: autotest/fracktail-giant-release captures28 (Mario visible above the orb during a successful bounce),34 (approach/jump arc),37 (exposed head),40 (raised giant),41 (throw lowering the head), plus logs confirming repeated throws, jump damage and Charge/Summon/Breath/Circuit. Raised and exposed heads occupy the right side at full Mario scale; native antenna/orb matrix lookup produces real coordinates. Release build, existing pure rules suite and git diff --check passed. Private test V-Sync disabled to avoid the previously documented offscreen test slowdown; installed config and user saves untouched.

Installed the tested USA engine and loader, hashes matched Play. Backup: engine/backup-before-fracktail-giant-20261026-031518. Engine SHA256653CBFA4E0B09EA83093903355D6DABBFBC4C87B8EBE917EEC093080E40942E8. Loader SHA256742FF740B9C9C5AFA7F98B5F73EA02D568A53DE7C1D43A3248FC07A88E47CBEA. This presentation pass does not change the prior first-pass story-outro and follower-graphic limitations.

## 2026-10-26: straight flight charge

Use the native full-model flight animation zun_all_S_1 during Charge's latter wind-up, guard tell, pass-through and return, then restore the normal upright idle. The REL flight animation table following the grounded table at80CF3518 names S_1/S_2/S_3 separately from S_1B/S_2B. Charge visual root is lifted to stage flight height instead of retaining the upright pose's buried-root offset. Giant scale, native music and damage/timing rules remain. Build passed (build-logs/fracktail-flight-build.log); verify the actual pose and height live before installing.

Charge rig correction: all three full-model S_1/S_2/S_3 variants remained curved/upright in live comparisons (fracktail-straight-flight-1, fracktail-flight-S2/S3). The original encounter's flight rig instead has separate e_zun_hed/e_zun_neck/e_zun_nomal/e_zun_tail assets. Use those native pieces for a straight horizontal charge with native head/tail animations and subtle segment motion; retain e_zun_all for grounded upright poses. Part-animation names exist in the user's asset files but not the linked REL string ranges, so the native pose-entry helper now supports persistent heap-0 copies of those names. No asset files are copied into source.

Final flight implementation uses the native head (zun_hed_S_1A), neck, eight repeated body sections and animated tail (zun_tail_S_1), kept horizontal through the charge and return. Flight height65; body spacing13 in model units and a head-overlapping first segment close the native mesh origin gaps. Full upright e_zun_all is restored for minion turns/next commands. All flight poses are released with the boss; persistent animation/model name strings use heap0. Native charge damage/guard timing remains unchanged. Release build and git diff --check passed.

Installed engine/loader match Play; backup engine/backup-before-fracktail-straight-flight-20261026-033203. Engine SHA25663A68ED7AF351B71BFE2497F4CBD19964E7AFBE1EF64AA766534B7EF58FCB8DA. Loader SHA256C19E5937F6A8CA7A80D997F318A3775A7F4019BAE1E3E3231AD87C77A3D7C289. Private installed-engine smoke uses the same offscreen harness and temporary V-Sync override as previous validations; user config/saves untouched.

Installed smoke passed: autotest/fracktail-flight-installed-smoke/shots/24.bmp and24.4.bmp show the connected horizontal flying head/body during the pass-through and return;25.bmp shows the restored upright pose. Charge impact logs power4 with guard1 and damage3, preserving guard behavior. No new fatal/panic observed. Earlier full-model flight experiments remain diagnostic only and were not installed.

## 2026-10-26: missing charge connector and idle ground depth

Replace the invisible default neck part in the straight-flight rig with a dedicated visible native body connector, positioned one body spacing ahead of the first repeated segment. Raise both upright root heights by80 world units (-320 raised/-395 exposed). Preserve the 3.15 model scale and dragon/Mario ratio by widening the entire boss camera projection 1.4x and lifting its view40 units; restore camera matrices/position/target before each native update and at battle exit to avoid accumulation or field camera changes. Native camera layout verified against https://raw.githubusercontent.com/SeekyCt/spm-headers/master/include/spm/camdrv.h (private autotest reference). Rebuild includes full translation for the camMain wrapper. Verify idle/charge/exposed frames and camera restoration on victory before installing.

Validation before install: full translated release build passed (build-logs/fracktail-neck-idle-build.log), git diff --check passed. autotest/fracktail-neck-idle-1/shots/22.bmp shows the raised idle body with the antenna inside frame;23.4/23.6 show the continuous charge connector;36 shows the exposed/breathing pose. autotest/fracktail-camera-restore-1 reached victory (log end(victory));42.bmp shows normal-size Mario and original field framing after exit. The boss camera does not keep widening over time. Tests use private saves and temporary V-Sync overrides only.

Installed USA engine and loader hashes match Play. Backup engine/backup-before-fracktail-neck-idle-20261026-071055. Engine SHA256560D50F7F0BF7A5E1B462CB8C7562C427BC32CF5B9843C4D59CF854C615E4ACE. Loader SHA256691AC28F771DF62E19FB501704E5476680F45CB1553F4D919D1C7027A30E59B1.

Installed desert-arena smoke passed: autotest/fracktail-neck-idle-arena-installed has native he4_10 story fixture;22.bmp shows the straight connected charge over the desert diorama,26.bmp the raised visible body during orb attacks,38.bmp the cleared native arena after victory with normal field camera restored. Log records start(story=1), charge and end(victory). This fixture checks the arena camera/rendering and clear handoff, not the full natural introduction. Both requested presentation fixes verified; earlier outro/follower limitations remain as documented.

## 2026-10-26: consistent desert battle scale

The fixed1.4x boss zoom-out compounded he4_10's already distant native camera. Replace it with projection normalization measured from a100-world-unit vertical span at Mario's home position. Target1.45 native EFB pixels per world unit and anchor home at28% of the native608-pixel width, Y480 of528 lines. Recenter both packed GX and native projection matrices, handling perspective vs orthographic center terms. This preserves model/Mario ratios and gives rooms a common apparent combat scale, independent of their field camera distance. Only projection state is overridden/restored; field view position/target/FOV and unrelated cameras remain native. Added a game-sequence guard. Release compile and git diff --check passed; compare rooms and verify field restoration live before install.

Viewport iteration: the first normalization matched apparent scales but put feet too low because528 lines had been assumed for the USA battle picture. The active view uses the480-line picture. Use measured native viewport dimensions for view-space panning, target1.3 pixels/world unit and homeY420 in480 lines, and keep projection center terms native. Boss diorama and world-anchored UI now use the same480-line mapping; other battles retain their existing mapping. Restore both view and projection matrices before native camera updates/exit. The initial center-offset experiment is diagnostic only (fracktail-normalized-camera-test/desert) and is not installed.

Final room-scale validation: native viewport is608x480 in BOTH rooms (logged), confirming the old528-line assumption was inappropriate for this boss picture. Final logs measure test-room1.640 and desert1.083 native pixels/world unit, normalized to1.300 in both; home projects to(170.2,420.0) exactly. autotest/fracktail-framing-viewport-test and fracktail-framing-viewport-desert captures reviewed: combatant size now matches, feet and stage floor align, charge and orb attacks remain visible. Desert victory returns to the native cleared arena camera (44.bmp). Release compilation (build-logs/fracktail-camera-viewport-build.log) and git diff --check passed. Private copies only; field camera/saves/config are unchanged by test fixtures.

Installed tested USA engine and loader; hashes match Play. Backup engine/backup-before-fracktail-room-scale-20261026-073051. Engine SHA25669B051FED98BB13EB5B3AC9AA7983F18FA85D3A585533B8ECC9AFDEF3EC9D186. Loader SHA256904BBF734F135311F1D2678D1F488AF16A5D932174084A509A30C8E627222FF7.

## 2026-10-27: review fixes, Mario battle stance, O'Chunks boss battle

Review of the badge/Fracktail work, fixed:

- The battle shader still drew four debug lines across every battle stage (red at the floor, green at mid-screen, yellow top, cyan bottom), and logged a `[scenefx-stage]` line every 30 frames. Both removed.
- Mario could reach 0 HP on his own turn (spikes, fire, confusion) and still sit through the enemies' turn. Defeat is now checked right after his action.
- After a defeat, a story boss trigger could start a new battle at 0 HP before the game over. No battle starts while Mario has 0 HP.
- The badge profile was rewritten to disk (write-through) on every turn and every FP spend. It now saves when the battle ends and on badge changes, grants and level-ups.

Not changed, noted: badge profiles are sidecar files linked by a token in the pouch, so loading an older save keeps the newer badges/FP. A new game that is never saved allocates a fresh profile file each session.

Added:

- Mario's TTYD-style battle stance (`S_1` with a feet-anchored bounce) in all battles.
- SPM's angry pose `S_5` as a 28-frame wind-up before Jump, Hammer and Thoreau.
- O'Chunks' first fight as a boss battle. See `docs/lost-pages/ochunks.md`.
- Test probes `SPM_LP_PROBE_NPCS` and `SPM_LP_POSE_CYCLE`.
- The sweep now allows up to 300 s.
- Autoplay ignores real input.
- `PersistentString()` for pose names the game never names itself.

Validation: release build; `tools/test-badge-rules.cpp` PASS, including the new O'Chunks rules. `autotest/och-fight3`: natural he3_04 intro, then the turn-based fight through victory and the cleared arena at GSW0 34. `autotest/idle1`: Lineland battle with the new stance and wind-up, no debug lines. Installed; backup `engine/backup-before-ochunks-20261027-083200`.

## 2026-10-27: F5 story battle menu

F5 in the test area opens a list of story boss battles (`StoryBattles()` in `lp_testarea.cpp`).

- A pick sets GSW0 to just before the fight, refills HP, and loads the map through the story's entrance.
- The native intro, the turn-based battle and the following story then play as normal.
- Entries: O'Chunks (he3_04, GSW0 33) and Fracktail (he4_10 via dokan_1, GSW0 51; walk in to wake him). Each new boss battle gets added.
- Keyboard and pad drive the menu, and it blocks game input while open.

`he4_10` is now outdoor for sun rays. `tools/test-lost-pages-title.py` gained `SPM_LP_TEST_KEYS` to post real keys.

Verified:

- `autotest/story5`: O'Chunks from the menu through victory and the cleared arena.
- `autotest/story4`: Fracktail's full native intro into the battle (time ran out mid-fight).

Installed.

## 2026-10-27: Fracktail stage floor

The Fracktail battle had no sand floor. There were two causes:

- The boss camera put Mario's feet at line 420 of 480, leaving almost nothing below them that the zigzag border didn't cover.
- The stage floor was passed in 480-line units, but the renderer places it in the 528-line EFB, so the floor was drawn about 10% too low.

Fixes:

- Feet now go at line 372.
- Both `SetStage` calls use the EFB floor, as in other battles.

Verified in `autotest/fr-floor3` (test room) and `autotest/fr-floor4` (he4_10 story fixture): the sand floor and its reflections sit right under the fighters, and the raised head and antenna stay on screen. Installed.

## 2026-10-27: Frackle scale

The Frackles drew at 0.85x their model (0.8x while thrown), about half Mario's height. Standing and thrown Frackles now draw at 1.5x (`kFrackleScale` in `lp_fracktail.cpp`), about two-thirds of his height. Verified in `autotest/frackle2`. Installed.

## 2026-10-27: O'Chunks boss music

The O'Chunks battle was silent (`spsndGetBgmName` returned garbage, nothing playing). The native fight script starts his theme after the intro, but the battle freezes scripts first. `BeginOChunks` now starts `BGM_BTL_DODONTAS1` itself; the cleared map then sets its own music after victory.

Fracktail was checked too: `BGM_BTL_BOSS_STG1` plays throughout its battle. `SPM_LP_LOG_BGM=1` logs the playing track every 2 s during battles.

Verified in `autotest/bgm-och2` (theme from start to victory, audio output non-zero) and `autotest/bgm-fr`. Installed.
