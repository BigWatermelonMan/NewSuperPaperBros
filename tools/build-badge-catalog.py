"""Maintain the 85-entry TTYD catalogue and its explicit SPM adaptations (no copied wiki descriptions)."""
from pathlib import Path
rows = '''PowerJump|Power Jump|1|2|Jump|A stronger two-hit jump.
Multibounce|Multibounce|1|2|Jump|Bounce across each foe; missed timing ends the chain.
ShrinkStomp|Shrink Stomp|1|2|Jump|Timed stomps shrink foes and halve their attack for three turns.
SleepyStomp|Sleepy Stomp|1|2|Jump|Timed stomps put a foe to sleep for three turns.
SoftStomp|Soft Stomp|1|2|Jump|Timed stomps lower defense for three turns.
TornadoJump|Tornado Jump|2|3|Jump|A timed vortex strikes the target and every airborne foe.
PowerBounce|Power Bounce|3|3|Jump|Timed landings extend this jump to seven hits.
PowerSmash|Power Smash|1|2|Hammer|A charged Cudge smash deals extra damage.
PiercingBlow|Piercing Blow|1|2|Hammer|Cudge ignores the target's defense.
HammerThrow|Hammer Throw|1|2|Hammer|Throw a digital hammer at any target, including fliers.
HeadRattle|Head Rattle|1|2|Hammer|A timed smash confuses a foe for three turns.
IceSmash|Ice Smash|1|3|Hammer|A timed smash freezes a foe for two turns.
QuakeHammer|Quake Hammer|2|3|Hammer|A shock wave strikes every grounded foe.
FireDrive|Fire Drive|3|5|Hammer|A pixel flame wave burns every grounded foe.
Charge|Charge|1|1|Tactic|Bank two attack power for the next attack.
ChargeP|Charge P|1|1|Tactic|Charge for Peach, Bowser or Luigi; P badges affect alternate heroes.
DoubleDip|Double Dip|3|4|Tactic|Use two real pouch items during one turn.
DoubleDipP|Double Dip P|3|4|Tactic|Alternate heroes can use two items in one turn.
HpPlus|HP Plus|3|0|Passive|Add five maximum HP while equipped.
HpPlusP|HP Plus P|6|0|Passive|Add five maximum HP for alternate heroes.
FpPlus|FP Plus|3|0|Passive|Add five maximum FP for badge moves.
PowerPlus|Power Plus|6|0|Passive|Add one attack to jumps and hammer moves.
PowerPlusP|Power Plus P|6|0|Passive|Add one attack for alternate heroes.
AllOrNothing|All or Nothing|4|0|Passive|Successful commands add one attack; failed commands deal zero damage.
Jumpman|Jumpman|2|0|Passive|Add one jump attack; hammer commands become unavailable.
Hammerman|Hammerman|2|0|Passive|Add one hammer attack; jump commands become unavailable.
PUpDDown|P-Up, D-Down|2|0|Passive|Trade one defense for one attack.
PUpDDownP|P-Up, D-Down P|2|0|Passive|Alternate heroes trade one defense for one attack.
PDownDUp|P-Down, D-Up|2|0|Passive|Trade one attack for one defense.
PDownDUpP|P-Down, D-Up P|2|0|Passive|Alternate heroes trade one attack for one defense.
DamageDodge|Damage Dodge|2|0|Passive|Successful guards prevent one additional damage.
DamageDodgeP|Damage Dodge P|2|0|Passive|Alternate heroes gain stronger guards.
DefendPlus|Defend Plus|5|0|Passive|Add one defense against incoming attacks.
DefendPlusP|Defend Plus P|5|0|Passive|Alternate heroes gain one defense.
DoublePain|Double Pain|0|0|Passive|Double incoming damage after defense and guarding.
PowerRush|Power Rush|1|0|Passive|Add two attack at five HP or less.
PowerRushP|Power Rush P|1|0|Passive|Alternate heroes gain two attack in Danger.
LastStand|Last Stand|1|0|Passive|Halve damage, rounded up, at five HP or less.
LastStandP|Last Stand P|1|0|Passive|Alternate heroes take half damage in Danger.
MegaRush|Mega Rush|1|0|Passive|Add five attack at one HP.
MegaRushP|Mega Rush P|1|0|Passive|Alternate heroes gain five attack in Peril.
CloseCall|Close Call|1|0|Passive|In Danger, attacks have a ten percent chance to miss.
CloseCallP|Close Call P|1|0|Passive|Alternate heroes gain Danger evasion.
PrettyLucky|Pretty Lucky|2|0|Passive|Incoming attacks have a ten percent chance to miss.
PrettyLuckyP|Pretty Lucky P|2|0|Passive|Alternate heroes gain ten percent evasion.
LuckyDay|Lucky Day|7|0|Passive|Incoming attacks have a twenty-five percent chance to miss.
LuckyStart|Lucky Start|4|0|Passive|Begin battle with a random recovery, defense or electric boon.
HappyHeart|Happy Heart|2|0|Passive|Recover one HP every second completed round.
HappyHeartP|Happy Heart P|2|0|Passive|Alternate heroes recover HP during battle.
HappyFlower|Happy Flower|2|0|Passive|Recover one FP every second completed round.
FlowerSaver|Flower Saver|4|0|Passive|Reduce move costs by one FP, to a minimum of one.
FlowerSaverP|Flower Saver P|4|0|Passive|Reduce alternate heroes' move costs.
PityFlower|Pity Flower|3|0|Passive|Taking damage sometimes restores one FP.
HpDrain|HP Drain|1|0|Passive|Lose one attack; recover one HP once per attack action.
HpDrainP|HP Drain P|1|0|Passive|Alternate heroes trade one attack for HP drain.
FpDrain|FP Drain|1|0|Passive|Lose one attack; recover one FP once per attack action.
HeartFinder|Heart Finder|3|0|Passive|Victories restore extra HP as digital heart pickups.
FlowerFinder|Flower Finder|3|0|Passive|Victories restore extra FP as digital flower pickups.
ItemHog|Item Hog|3|0|Passive|Victories can add a real Mushroom to an empty pouch slot.
IcePower|Ice Power|1|0|Passive|Jump safely on fiery foes; resist fire and hit them harder.
SpikeShield|Spike Shield|3|0|Passive|Jump safely on spiky foes.
ZapTap|Zap Tap|3|0|Passive|Contact attackers take one electric counter damage.
ReturnPostage|Return Postage|7|0|Passive|Contact attackers receive half their inflicted damage.
FeelingFine|Feeling Fine|4|0|Passive|Prevent poison, confusion and other harmful battle statuses.
FeelingFineP|Feeling Fine P|4|0|Passive|Alternate heroes resist harmful battle statuses.
Refund|Refund|1|0|Passive|Using a pouch item refunds some coins.
MoneyMoney|Money Money|5|0|Passive|Victories award additional real coins.
SuperAppeal|Super Appeal|1|0|Passive|Appeal earns more Star Energy for Digital Burst.
SuperAppealP|Super Appeal P|1|0|Passive|Alternate heroes earn more Star Energy from Appeal.
Peekaboo|Peekaboo|2|0|Passive|Reveal enemy HP, defense and active statuses without Tattle.
QuickChange|Quick Change|7|0|Tactic|Switch playable heroes without spending the turn.
TimingTutor|Timing Tutor|1|0|Passive|Show command windows and the Stylish timing cue.
Simplifier|Simplifier|1|0|Passive|Widen command windows; reduce earned Star Energy.
Unsimplifier|Unsimplifier|1|0|Passive|Tighten command windows; increase earned Star Energy.
ChillOut|Chill Out|1|0|Field|Prevent enemy first strikes.
FirstAttack|First Attack|1|0|Field|First strikes defeat sufficiently weak foes immediately.
BumpAttack|Bump Attack|5|0|Field|Touching sufficiently weak foes defeats them immediately.
AttackFxR|Attack FX R|0|0|Fx|Red digital sparks and a distinct attack sound.
AttackFxY|Attack FX Y|0|0|Fx|Yellow pixel stars and a distinct attack sound.
AttackFxB|Attack FX B|0|0|Fx|Blue digital ripples and a distinct attack sound.
AttackFxG|Attack FX G|0|0|Fx|Green zigzag sparks and a distinct attack sound.
AttackFxP|Attack FX P|0|0|Fx|Purple digital notes and a distinct attack sound.
LEmblem|L Emblem|0|0|Emblem|Dress Mario in a green costume tint.
WEmblem|W Emblem|0|0|Emblem|Dress Mario in a gold costume tint; combines with L Emblem.
SlowGo|Slow Go|0|0|Field|Limit field movement to walking speed.''' .splitlines()
root=Path(__file__).resolve().parents[1]
names=[r.split('|')[0] for r in rows]
out=['// Generated by tools/build-badge-catalog.py. Mechanics reference: docs/lost-pages/badge-catalog.md.', '#pragma once','#include <array>','#include <cstdint>', 'namespace LostPages {',
     'enum class BadgeKind : uint8_t { Jump, Hammer, Tactic, Passive, Field, Fx, Emblem };',
     'enum class BadgeId : uint8_t { '+', '.join(names)+', Count };',
     'constexpr int kBadgeCount = static_cast<int>(BadgeId::Count);',
     'struct BadgeDef { BadgeId id; const char* name; const char* description; int bp, fp; BadgeKind kind; BadgeId family; bool alternate; };',
     'inline constexpr std::array<BadgeDef,kBadgeCount> kBadges{{']
for row in rows:
    id,name,bp,fp,kind,desc=row.split('|')
    alt=id.endswith('P') and id[:-1] in names
    family=id[:-1] if alt else id
    out.append(f'    {{BadgeId::{id}, "{name}", "{desc}", {bp}, {fp}, BadgeKind::{kind}, BadgeId::{family}, {str(alt).lower()}}},')
out+=['}};', '} // namespace LostPages','']
(root/'wiicompiled/runtime/src/lost_pages/lp_badge_catalog.h').write_text('\n'.join(out))
doc=['# Badge catalogue', '',
     'Generated by `tools/build-badge-catalog.py`. Names and base BP/FP costs reference the [TTYD Badge Database](https://papermario.fandom.com/wiki/Paper_Mario:_The_Thousand-Year_Door_Badge_Database). Descriptions below describe this SPM adaptation, not exact TTYD behavior.', '',
     'All 85 entries start unobtained in normal games. The F10 test session owns the full collection, initially unequipped, with 999 BP. Each entry has one equip slot; repeated copies of the same badge are not implemented. P variants apply to the active Peach, Bowser, or Luigi and can stack with the base badge. See [equipment, saves and verification](badges.md).', '',
     '| Test ID | Badge | BP | Base FP | Category | SPM behavior |','|---|---|---|---|---|---|']
for number,row in enumerate(rows,1):
    id,name,bp,fp,kind,desc=row.split('|')
    doc.append(f'| {number} | {name} | {bp} | {fp} | {kind} | {desc} |')
(root/'docs/lost-pages/badge-catalog.md').write_text('\n'.join(doc)+'\n',encoding='utf-8')
print('Generated',len(rows),'badges')
