# Final boss native integration (US0)

Implemented in the engine: `lp_final_boss_visual.h/.cpp`, `lp_bleck_rules.h`,
`lp_dimentio_rules.h`, the shared turn controller in `lp_final_battle.inc`, the
natural story gates in `lp_battle.cpp`, and both F5 rows in `lp_testarea.cpp`.
The earlier standalone-only status below is kept for the recon evidence; the
final build and installation are recorded in MODLOG.md.

## Integration

`FinalBossVisual` accepts kind 1 Bleck / 2 Super Dimentio, active, root, facing
(degrees), scale, attacking, absorb, miniLuigis (0..8), voidSprites (0..3).
`miniLuigisAttacking` animates the swarm independently of the boss.
With `useHelperPositions`, supply `miniLuigiRoot` (one swarm actor) and
`voidSpritePositions[3]` (individual actors). Helpers otherwise use a default layout.

Call `UpdateFinalBossVisual(g,view)` once per tick, `FinalBossTarget(g,point)`
for the visible boss target, and `ReleaseFinalBossVisual(g)` on exit, map change,
or cancellation. Inactive/invalid-kind updates release automatically. All poses
are private to this renderer; it never writes the story NPC, HUD, or Mario pose.
Native callback address: 0x817F0300.

Super Dimentio uses native assets e_DmenL_k (torso), e_DmenL_f (face),
e_DmenL_lh/rh (hands), e_DmenL_s (shoes), e_DmenL_r (joint chains).
`e_dmnyan` is DOMNYAN, a different mechanical enemy; tribe309's model pointer is zero.
The native idle layout at 0x8033AA08, used by 0x801E7A4C, is:
face (0,195,0), torso (0,130,0), hands (50,90,-50)/(-60,90,50),
shoes (-60,0,-50)/(60,0,50). Our limbs use seven native joints per chain;
bobbing and punch motion are presentation adaptations. Mini Luigis use
`p_dot_luigi` S_1 / J_1. Void Sprites reuse Fleep (`FRY_mekri`, S_1),
with pulsing purple native material tint and orbit/absorb transforms.

Bleck uses e_jigen. Native attack 0x8042C2B0 opens the cape with A_4a
(ID33), holds suction with A_4b (ID34), then uses A_4c..g (IDs35..39).
See calls at 0x8042C37C, C3B0, C528, C54C, C5A0, C5D4, C610.
Absorb uses A_4a -> A_4b, then A_4c -> A_4g on release; transitions wait
for native animation completion. No Mario animation surveys were performed.

Native APIs match lp_fracktail: create 0x80042384; set anim 0x800432F8;
step 0x80045170; draw 0x80048EE4 (passes1/2); release 0x80049EAC.
Tint 0x80044B74 takes (pose,GXColor*), verified from its four byte loads.
Loop count accessor 0x8004454C returns f1 from pose+0x8C, so the renderer
reads that field through animPoseGetAnimPosePtr 0x8004C548.

## Story routes and gates

All routes use **ls4_11**, with null entrance. Its init is 0x80DEE498.
Opcode0x26 is CASE_LT, not equality; this distinction is essential.

| Fight | F5 entry | Live battle GSW0 | Victory reload GSW0 | Music |
|---|---:|---:|---:|---|
| Bleck | 410 (409 for full conversation) | 410 | 411 | BGM_BTL_MR_ZIGEN1 |
| Super Dimentio | 414 | 415 | 415 | BGM_BTL_BOSS_LAST1 |

Bleck's CASE_LT411 branch (0x80DEED88) creates template123. The preceding
CASE_LT410 branch starts the full intro at 0x80DEAD10. Intro completion
spawns the fighting actor at 0x80DEB830, sets its UW13 to 0x80DEACF8,
then sets GSW0=410 at 0x80DEB870. Its script ID is **0x1000007B**.

Super Dimentio's CASE_LT415 branch (0x80DEEFA4) covers GSW0 413/414;
it spawns template124 at 0x80DEF02C and invokes Pure Hearts setup
0x80DED9B0. At 0x80DEE328 that event installs UW9=0x80DED9A0;
at 0x80DEE424 it sets GSW0=415 and returns control. Script ID **0x1000007C**.
Thus loading 415 directly is a defeat route, not a playable F5 entry.

Recommended natural gates, in addition to map, alive NPC, and Mario key enabled:

- Bleck: tribe305, GSW0=410, UW13==0x80DEACF8.
- Super: tribe309, GSW0=415, UW9==0x80DED9A0.

The fighting actors have generated NPC names (format npc_%08x); do not depend
on their literal names. Resolve by tribe plus the callback/script ID.
`jigen` is the separate named cutscene Bleck, `nasta` is Nastasia,
`dimen` is ordinary Dimentio, and `head` is a temporary e_DmenL_f cutscene NPC.

NPC unitwork base is **npc+0x510**, not the partswork base.
UW13 is npc+0x544; UW9 is npc+0x534. Verified from 0x801034C8.
Partswork is part+0x2A8, verified from 0x801035B4.

## Defeat handoff without replaying an HP fight

After restoring map display, camera, Mario, and events, reload ls4_11 with
Bleck GSW0=411 or Super GSW0=415. These are statically verified native
post-fight routes. Keep natural gates above to avoid catching their stand-ins.

- Bleck CASE_LT412 at 0x80DEEE58 invokes 0x80DEACF8 -> 0x80DE75E4.
  That scene resolves 0x1000007B and creates a stand-in at 0x80DE7614
  if absent. Its later progression sets411 then412 at 0x80DEAB04/AB10.
- Super CASE_LT416 at 0x80DEF09C invokes 0x80DED9A0 -> 0x80DEB9B0.
  That scene resolves 0x1000007C and creates a stand-in if absent
  (0x80DEB9EC), then plays defeat/escape. It sets416 at 0x80DED938.
- Live native death scripts read Bleck UW13 at 0x8042AD48 and Super UW9
  at 0x8041D2E0; invoking those wrappers while keeping the original actor
  is an alternative requiring runtime validation.

The original Super opening is a separate encounter at GSW0=411 that advances
to413 (0x8041D228). 413 is NOT final victory. Final victory is416, reached
through the415 reload scene. Do not warp to416 if the defeat scene is wanted.

## Rules and original attack evidence

Original vulnerable HP is150/200 (tribe table 0x803FC290, stride0x68,
tribes305/309). Three protected enemy turns for both bosses is the user's
chosen adaptation; the rules headers expose it explicitly.

Bleck's original gate is scripted shield interaction, not vulnerable HP:
part2 work3=0, work4=3, work5=1 at 0x8042ABC0..ABF0; AI tests work3>=work4
and calls the Pure Hearts scene at 0x8042BD30..BD6C. Shield-hit callbacks
0x801F2BCC and 0x801F3464 increment work3 while denying ordinary damage.
This provides evidence for three initial barrier interactions, not original
turns or an HP-based phase threshold. Native Super likewise advances a script
counter and GSW0, rather than consuming its later200HP to remove the barrier.

Bleck native attack scripts/effects prove void suction (0x8042C2B0),
slow (0x8042C0B0, A_3a/b/c), void bullets (0x8042C688, A_1a/b/c),
haste (0x8042CB68), teleport/reposition (0x8042BDE8), plus native
jigen_darkness, jigen_darkness_mini, jigen_time, jigen_paralysis effects.
Relevant SFX include ZIGEN_MANTLE, SLOW_EFFECT, ANKOKU_SHOOT,
ANKOKU_ATTACK, MINIANKOKU_THROW, KASOKU_EFFECT, DASH, ANKOKU_SWALLOW.

Super native module (npc_dimeen_l.c, 0x8041C7F4 onward) loads p_dot_luigi
at 0x8041C954. Its assets/SFX prove normal/big hand attacks, jump/locked-on
super jump, beams, and falling/jumping dot Luigis: DMNL_BIG_HAND,
NORMAL_HAND, S_JUMP_ROCKON, S_JUMP_LANDING, BEAM, ALLBEAM_EFFECT,
DOTRUIGI_FALL/JUMP/DAMAGE. New Void Sprites, stat absorption, Reality Split,
Voidsplitter, Pure Shield, and Lover's Grief are adaptation features.

## Validation and limits

Standalone clang syntax check of renderer and compile-time rules assertions
passed (C++20, Wall/Wextra/Werror, disabling only pre-existing unused-parameter
warnings in PPC headers). No shared build was run.
Read-only binary/asset fixture checks: `python tools/lp-final-boss-research.py --verify`.

Isolated play-script probes at410/415 both stopped before gameplay with
`Stale generated indirect dispatch winner at 0x8019933c`. Logs are under
autotest/final-boss-native-bleck-410 and final-boss-native-dimentio-415.
The latter was also the wrong F5 entry and has been corrected to414.
No runtime visual/layout or story-flow success is claimed. Main integration
must render-check the multipart rig, cape sequence, tinted Pixls and swarm,
then test F5 entries410/414 and victory reloads411/415 with a current engine.
