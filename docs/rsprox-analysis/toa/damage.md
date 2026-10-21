# Damage on the raider (rev 241 audit, 2026-10-21)

Every splat on the recorder in the 21 ToA sessions (`docs/rsprox-analysis/toa/invocations.md`
lists them), attributed by `tools/rsprox/rsprox_player_hits.py` (method: the `rsprox-content`
skill, §13). Normal-world sessions: index 4292 (rev 241, raid 160), 45 (rev 223), 4288. The
rest are Leagues V / Grid Master: their maxima are not used, only zero rates, shapes and
ratios inside one session.

## What was wrong

| | Ours before | Recorded | Now |
|---|---|---|---|
| Npc combat levels | 74 ToA npcs at the engine seed (attack/defence/strength 1, ranged/magic 0) from `_explicit/configs/server_fields.generated.npc`; Zebak defended at Defence 1 (stab roll 2240) | the cache's `stat1..6` (Zebak 250/70/140/580/120/100) | the cache's levels (stab roll 17696) |
| Accuracy | none: every styled attack landed 0..max | unprayed zeros far above 1 / (max + 1): Zebak magic 16% (116), ranged 24-36% (58, 77), the scarabs' shots 81% (442), Akkha 32-47% | `~toa_roll_vs` / `~toa_roll_carried`: the attacker's roll x raid accuracy scaling vs the raider's defence roll |
| Akkha's basic max | 40 | no unprayed hit above 44 at the cap (66 hits; 40 puts ~20 past 55); prayed maxima 1 / 2 / 4 at raid 0 / 160 / cap | 22 (his melee formula: Strength 140, +30) |
| Akkha through prayer | 15% ranged/magic, 25% melee (NR) | at the cap 1/2/3/4 at 34/37/33/33 of 242 magic, no 5 but 3: floor(roll x 9 / 100) | 9%, melee 15% (4 hits only) |
| Quiet Prayers | half the reduction (Zebak prayed: 50% through) | "10% less effective" (wiki); Zebak prayed at the cap 1-4 (79 hits), Akkha 0-9 | +10 points |
| Spitting scarab max | 15 | wiki 10; unprayed up to 13 at raid 150+ | 10 |
| Agile scarab max | 10 | wiki 5; up to 5 at raid 0 (index 45) | 5 |
| Apmeken baboons | 8 / 12 / 4 / 10 / 16 | wiki 5 / 7 / 5 / 7 / 7; 4292 (raid 160): thrower up to 11, thrall up to 9 | the wiki's |
| Raid scaling exempt | only Kephri's overlords (defence) | wiki: crocodiles and agile scarabs do not scale with raid level | both exempt from defence and accuracy scaling |

Index 4292's prayed Akkha zeros against the recorder's own gear (`acc_predict` method, skill
§13.7): magic 17 recorded, 15.8 predicted for max 22 / 9%, 11.9 for the old 40 / 15%; ranged
20, 20.5, 17.9.

## Per attack

Recorded: normal-world rows unless marked S (seasonal). "prayed" = the matching overhead up.

Ours is the room tests' tick logs (`rsprox_player_hits.py --ours`): an unarmoured raider at
Defence 99 who never prays, so most attacks land (Akkha's magic vs that raider: 94% to hit,
about 10% zeros predicted, 1 of 7 recorded). The recorded zero rates are matched against the
recorder's own gear instead (above).

| Attack | Recorded | Ours (tests, no armour, no prayer) | Verdict |
|---|---|---|---|
| Zebak magic / ranged split | prayed: 0 (4292: 27 of 27, 17 of 17); unprayed raid 0 max 15/16 | rolls accuracy; split 0-15 (17 magic, 5% zero, the unarmoured raider's magic defence is low) | match (wiki 16/16) |
| Zebak melee | not recorded on the recorder | rolls accuracy, max 38 (wiki) | unverified |
| Akkha basic (3 styles) | see above | melee swing 0-22 (14, 28% zero), throws 0-25 at the test's level | fixed |
| Ba-Ba melee | prayed 0 (12 of 12) | rolls accuracy, 24 (wiki) | match |
| Ba-Ba's baboons | not separable | 8 as before | open |
| Kephri's spitting overlord | through Protect from Missiles 0-3 | shots 0-9 (11, 27% zero) | max fixed |
| Agile scarabs | prayed 99% zero (826, index 45), unprayed 81% (72) | shots 0-9 (11, 27% zero) | fixed; no raid scaling |
| Apmeken baboons | 4292 unprayed thrower 77% zero (9), thrall 80% (26) | rolls accuracy (toa_paths_tells passes) | fixed |
| Warden phase 2 | prayed: 20% through (5 of 20 at Entry) | throws 0-28 (70 each, 5-7% zero); phantom Zebak 0-21 (37, 5% zero) | match (wiki 20/22) |
| Warden phantoms (Akkha, Zebak) | prayed 0 (4292: 12 of 12) | rolls on the phantom's stats | match |

The Wardens' phase-three release (`npc_wardens_release01`) lands 170-210 on a raider who
does nothing (the test); a recorded raider never let it go off. Typeless hits (boulders, rockfall, waves, beams, the blood barrage, Kephri's fireball, Het's
orbs, the Wardens' slams) do not roll accuracy and are not in this table: their zeros are
dodges.

## Open

- ~~3,710 other npcs at the engine seed~~: fixed the same way on 2026-10-21 (14,438 levels;
  zombies 22-30 Hitpoints, not 10). `tools/check_npc_cache_levels.py` prints 0.
- Akkha's prayed melee: 4 nonzero hits, all 4s; 15% keeps NR's ratio to ranged, not a fit.
- Ba-Ba's baboons' max (wiki "1?").
