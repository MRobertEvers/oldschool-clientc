# Scorching bow special (Scorching shackles) vs RSProx

Content: `skill_combat/scripts/player/specs/pvm_scorching_bow.rs2` (OSRS-Content).
Test: `test/combat/scorching_bow_spec.lua` (scriptrun; marks `sb:man`, `sb:demon`, `sb:second`).

## Sessions

| session | rev | world | shooter | target | specials |
|---|---|---|---|---|---|
| index 4303 (`--all-players`) | 241.1 | normal, TFA 6 | Jonoko, ShadeR3av3r, Taurine90 | Dreadnoth (`tfa_jelly_boss`) | 19 |
| index 3297 | 235.1 | Grid Master | the recorder | Catacombs hellhound (7256) | 2 |
| index 1740 | 227.1 | Leagues V | the recorder | tormented demon | 7+ (burn values are seasonal) |

Rev 227/235 send neither 2806 (player) nor 2808 (npc); rev 241 sends both. Our cache is
rev 241, so 4303 decides the graphics.

## The special's tick

| field | recording | ours (test: specials at ticks 34, 74, 84) | verdict |
|---|---|---|---|
| player seq | `human_weapon_bow_scorched_01_spec` 11133, delay 0 (19/19) | same | matched |
| player spotanim | 2806 slot 1, height 0, delay 0 (4303, 19/19) | same | matched |
| projectile | 2807, start 46, end 50 + 4d, angle 255, progress 80, height 160 -> 146 (v2) | 46 -> 66 at d 4, 62 at d 3, heights 40 -> 36 (x4) | matched |
| npc spotanim | 2808 slot 5, then 2908 slot 1, delay = endtime (4303, 19/19) | same, delay 62/66 | **fixed** (was absent) |
| npc defend seq | none on the special's tick (3297 2/2, 1740 7/7); an ordinary arrow has one (3297 [2251]) | none | **fixed** (the shared funnel sent `demon_block`) |
| synths | 9325 entangle @end, 9326 impact @end, 9327 draw back @11, 9328 let go @46, 2700 longbow @0 (3297 2/2, 1740 7/7) | same order and delays | **fixed** (was silent) |
| arrow launch spotanim | none | none | matched |
| xp | on the special's tick only (3297 [2474]) | same | matched |

## Landing and burn

| rule | recording | ours | verdict |
|---|---|---|---|
| splat tick | spec + 1 + (endtime + 15) / 30: +3 for 54..70, +4 for 82, 86 (every lone special, 3 sessions) | +3 at 62 (34 -> 37) and 66 (74 -> 77) | **fixed** (was spec + 1 + endtime / 30: a tick early at d 1 and d 7+) |
| burn splat | hitsplat 74, 1 a hit | same | **fixed** (was one red 5) |
| burn count | 6: landing, then every 4 ticks to +20 (4303 [2724] [2801] [2892] [2917], 3297 [2474]) | 6 (34: 37, 41, 45, 49, 53, 57) | **fixed** |
| two burns, one shooter | one clock; the new burn waits for it, ends 20 after its own landing, overlap = one splat of 2 (4303 [2695]->[2698]; [2696] lands [2699], first hit [2701]; [2820] -> [2824]) | 74's clock 77, 81, 85, ...; 84's lands 87, first hit 89: 89, 93, 97 = 2, then 101, 105 = 1 (five hits, as [2696]) | matched |
| two shooters | separate clocks, never merged (Taurine90 1 mod 4 beside Jonoko 2 mod 4, [2753..2797]) | one clock per shooter | matched by construction; one shooter in the test |
| burn xp | none (3297 [2477..2497]) | none | **fixed** (gave xp) |
| sound per burn | the npc's defend sound to the shooter (3297: `dog_hit` synth on each) | the engine's npc hit sound | engine-generic; the hellhound's own sound mode not checked |
| target | demons only (every recorded special) | `npc_param(demon)`, now from the wiki for 203 npcs (`tools/gen_npc_demon.py`) | matched |
| non-demon | no recording; wiki: "Scorching shackles won't work against a non-demon enemy." | that message, no energy, an ordinary shot | *unverified* (what follows the message) |
| bind | Dreadnoth steps 13 after landing (7/7, `tfa_dreadnoth_freeze.lua`) | unchanged | not re-run to the bow (that test fails earlier, see below) |

## Open

- Jonoko's [2772] burn (lands [2775]) hits [2778], [2782], [2786] and then stops; the rule
  predicts [2790] and [2794] too. 1 of 19 specials; not modelled.
- Burn immunity: the wiki's Cerberus and the red Alchemical Hydra are immune to normal
  burns, the wiki's 5-stack cap is across all shooters. Ours keeps 5 a shooter and has no
  immunity list.
- One target per shooter: a burn on a second npc moves the shooter's clock there, and the
  first npc's remaining hits stop.
- Burns pause while the npc is `npc_delay`ed (an npc queue does not drain then).
- `tfa_dreadnoth_freeze.lua` fails before it reaches the bow (the barrage immunity step,
  then its own setup has no dragon arrows), so the bind was not re-checked on Dreadnoth.
