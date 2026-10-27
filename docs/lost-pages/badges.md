# Badges and battle expansion

The native pause menu now browses all 85 entries from the supplied TTYD database. [The complete catalogue](badge-catalog.md) lists each badge, its cost, and its adapted effect. New games start with none, including Power Bounce. The original HP Plus and Power Plus field pickups now grant their corresponding unequipped badges. Old held versions convert automatically; their names and descriptions identify them as badges. Other acquisition locations and a badge shop remain future work. Enter testing from the title with F10 to own every badge for that session; equip the ones to try from Pause > Badges.

The original teal tab, block lettering, dimensions, alignment, animation and native glove remain. Press 2/A to enter the badge list, Up/Down to browse, Left/Right to move five entries, and 2/A to equip or unequip. 1/B returns from browsing; a second Back returns to Important Things. Plus resumes play. Locked entries cannot be equipped. BP starts at 3 and caps at 99; testing has 999. Each level-up offers HP +5, ATK +1, or BP +3. Only the chosen stat increases. Jumpman and Hammerman cannot be equipped together.

## Battle adaptations

Equipped jump and hammer badges add moves to their respective command lists. Basic attacks remain free. Power Bounce is its own 3-FP move with a seven-hit ceiling. Other moves cover multiple targets, armor piercing, airborne targeting, shrinking, sleep, softening, confusion, freeze and burning. Battle armor is assigned to Clefts/Bristles (2) and Koopatrols (1), with an extra point for dark armored variants; native SPM immunity-table sentinels are excluded. Status durations and revealed HP/defense appear beside enemies. Sleep and freeze skip enemy actions; shrinking reduces size and attack, softening reduces defense, confusion can cause self-hits, and burn/poison cause damage between rounds.

SPM has playable heroes rather than TTYD's partner turn. P badges therefore affect the active Peach, Bowser or Luigi. Change Hero cycles unlocked heroes (all four in testing); Quick Change makes it free. Base badges continue working for alternate heroes and can stack with their P variant. There is one instance of each badge; duplicate-copy stacking and TTYD's audience/stage hazard simulation are not part of this adaptation.

FP starts at 10, gains one every three levels, and is increased by FP Plus. Testing starts with 30. Healing foods also recover FP equal to one third of their HP value, bounded to 1-10. The native item score field stays cosmetic. Recharge FP restores 2 FP for a turn. Flower Saver reduces nonzero move costs to at least 1. Double Dip consumes two actual pouch items and refunds coins when Refund is equipped.

Successful commands, Stylish jump returns and Appeal earn Star Energy. Digital Burst spends 100 Energy to damage all enemies and heal the hero. Simplifier/Unsimplifier trade timing difficulty for energy yield. Timing Tutor shows the extra Stylish cue. Super Appeal raises Appeal's energy reward.

Passive badges modify attack, defense, Danger/Peril bonuses, guarding, evasion, contact counters, fire/spike protection, status resistance, recovery and victory rewards. Item Hog adds an actual Mushroom when a slot is available; Money Money grants actual coins. Heart/Flower Finder use immediate victory recovery rather than new overworld pickup objects. First Attack and Bump Attack bypass battles against lower-level enemies; they grant native drops/cosmetic score, not Star Points. Chill Out suppresses enemy first strikes. Slow Go restricts field movement. L/W Emblem use full-character green/gold tints; their combination is purple rather than a new costume texture set.

Battle presentation includes native walking, jumping and hurt poses; squash/lean guard, attack and defeat motion; digital shield/superguard flashes, Cudge-shaped hammer effects and projectiles, flame waves, ice blocks, shock waves, tornadoes, electricity, healing hearts, charge rings and five distinct Attack FX visuals/sounds. NPC animation selection falls back only to a valid tribe animation. Fighters' part positions, owner transforms, camera, alpha and visibility are synchronized on stage, then restored on exit. Hero and enemy facing are set independently.

## Save format

The existing unused pouch word migrates from magic 0x4C to 0x4D and packs a 7-bit level, 7-bit Star Points and 10-bit badge-profile token. Legacy level/SP survive; the old always-available Power Bounce flag does not grant ownership. Each token points to `UserData/LostPages/Badges/profile-N.txt` in portable installations (otherwise the runtime's application data directory). Version 3 files contain ownership, equipment, FP, energy and BP. Version 2 files migrate once, preserving the previous automatic BP budget for the saved level. Keep that directory together with saves when backing up or moving the game. Writes use a temporary file and atomic replacement. Missing, truncated or invalid files safely produce an empty collection. Test equipment uses a separate in-memory profile and never writes the normal collection.

HP Plus modifies effective maximum HP without permanently adding its bonus to the native saved base maximum. Unequipping clamps current HP; level-ups offer HP, ATK or BP and refill effective HP/FP. The native HP getter/setter hooks participate in full translation.

## Developer verification

`tools/test-badge-rules.cpp` exercises all 85 definitions, unique IDs/names, locked defaults, profile round trips/corruption, armor piercing, All or Nothing, damage modifiers, guarding, Last Stand rounding and timing bounds. Compile with the portable clang++ toolchain using C++17 and `-static`.

`tools/play-script.py --test-area` drives the same title testing path in a private game copy. `SPM_LP_TEST_EQUIP` accepts comma-separated catalogue IDs; `SPM_LP_TEST_MOVE` chooses one move, `SPM_LP_TEST_MOVES` cycles a list, and `SPM_LP_TEST_TACTIC` selects a tactic. `SPM_LP_TEST_BADGES=1` enables the test collection in an ordinary warped map. Existing HP/item/autoplay fixture variables remain available. These variables are absent in the installed game.

Live checks and remaining verification limits are recorded in `MODLOG.md`. Do not treat the collection count as evidence that every enemy-specific badge combination has been playtested.
