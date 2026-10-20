# Wintertodt in RSProx

What Jagex's server sends during the Wintertodt, read out of four RSProx
recordings. The content in
`OSRS-Content/osrs239-content/server/scripts/minigames/minigame_wintertodt` is built
from these rows. Each constant in `wintertodt.constant` names the row it comes
from.

| Session | Rev | World | Recorded | What it holds |
|---|---|---|---|---|
| 3254 | 233 | 311 (Wintertodt) | 2025-09-17 | three rounds, all four lanes lit, the recorder on the south-east lane |
| 3262 | 233 | 396 (2000 total) | 2025-09-18 | one long round, only the south-east lane alive: a clean sample for per-lane rates |
| 732 | 233 | 624 | 2025-09-19 | a death at 0 Warmth, rounds with the pyromancers down |
| 3263 | 233 | 367 | 2025-09-18 | an entry and an exit mid-round (the leave prompt) |

These are the current game: they come after the 9 October 2024 rework (Warmth,
Reward Cart), the 16 October 2024 update (75 points a heal, darker cold
hitsplats for other players) and the 23 April 2025 update (no Hitpoints bar on
cold hits).

Tool: `tools/rsprox_wintertodt.py` (sections `fires`, `energy`, `pyros`,
`attacks`; `--dump T0 T1` for a window). Ticks below are the transcript's. T is
the defining event's tick: the storm's throw, the fire's add, the click.

## Lanes and the HUD

| Row | Recorded |
|---|---|
| W-H1 | `wint_update` (clientscript 1421) takes points, energy (0..3500), four pyromancer flags (1 = healthy) and four brazier codes. **Lane order is south-west, north-west, north-east, south-east** (brazier loc changes matched to HUD columns: 3254 SE→3 ×53, SW→0 ×25, NE→2 ×21, NW→1 ×22). |
| W-H2 | **Brazier code 0 = broken, 1 = unlit, 2 = lit.** 3262: the south-east column reads 2 while its fires flow, 1 the moment they stop (389), 0 when broken (670), 1 when fixed (674), 2 when lit (678). |
| W-H3 | A pyromancer struck to 0 hitpoints reads 0 before she falls (3262 626: hit to 0, healed on 627, HUD 0 on 626). |
| W-H4 | The HUD is sent every second tick and again on every event. The countdown varbit `wint_transmit_respawndelay` (7980) is set to 100 at the round's end and counts down on the same cadence. |
| W-H5 | Entering: `wint_status` opens with `wint_inside_event` (1433). Outside in the camp: it reopens with `wint_outside_event` (1432) after the exit fade, and keeps the 2-tick cadence (3254 1636..1640). |
| W-H6 | Varbit 5354 `wint_area_attack` follows the recorder's tile a tick after each step (175 of 176 changes): 1 on a tile an area attack could centre on, 0 beside braziers, roots and walls. |

## The fire stream

| Row | Recorded |
|---|---|
| W-F1 | The pyromancers' "green bolts" are `wint_fire` npcs (7373), not projectiles. A healthy pyromancer at a lit brazier adds one on the brazier's centre (its south-west tile + 1,1) **every 2 ticks** (2359 of 2408 same-tile gaps). |
| W-F2 | It is added facing its path (spawn angle north-west from the south-east lane, north-east from the south-west, and so on) with a matching `face_angle`, walks one tile a tick diagonally at the storm, and is deleted at **T + 8** (1075 of 1075 complete lives) on the tile two (Chebyshev) from the storm's centre 1630,4007: (1632,4005), (1628,4005), (1628,4009), (1632,4009). |
| W-F3 | **5 energy (of 3500) per fire, taken on T + 7**, the tick it reaches that tile. 7 fires make 1%: the Wiki's 1% per 14 ticks per pyromancer. |
| W-F4 | The pyromancer turns to the storm (`face_angle`) with every fire (697 of 697). She plays `quest_lunar_cure_plant` (4432) on 528 of 1317 fires (2 in 5), and says "Yemalo shi cardito!" only on a cast tick, 130 of 528 (1 in 4). |
| W-F5 | After a relight the stream starts 1..12 ticks later (mostly 3..11). After a heal it restarts too. |
| W-F6 | With no brazier lit the energy rises **+5 every 5 ticks** (3262: 2745 → 2860 over 112 ticks). That is the Wiki's 1% per 35 ticks, delivered in fifths. Fires already in flight still land after their brazier goes out. |

## The storm's attacks

Each kind runs on its own **5-tick clock**. Every gap between two throws of one
kind is a multiple of 5, and the phases differ between kinds and between
rounds. Every throw is `wyvern_skeleton_travel_breath` (501) from 1630,4007
with angle 64 and end height 50. Heights below are raw: divide by 4 for this
tree's projectile units. Map-anim heights and delays are used as recorded.

| Row | Attack | Recorded |
|---|---|---|
| W-A1 | Brazier, extinguish (79) | T: throw to the brazier's south-west tile, start 0, end **90**, start height 0. `wyvern_skeleton_impact_breath` (502) delay 90, height 60 there. One `clanwars_snowfalling` (shape 10, random rotation) on its centre. T + 2: the snow goes. **T + 4: `wint_brazier`, area sound 2300 (`gas_hiss`), range 2.** T + 5 or T + 6 (29/28): "Light this brazier!". |
| W-A2 | Brazier, break (37) | T: throw end **120**, start height **150**. 502 delay 120, height 70. Snow on the centre and its four neighbours (a plus). T + 3: the snow goes. **T + 4: `wint_brazier_broken`, area sound 2299 (`gas_explosion`), range 2.** Anyone within one tile of the 3x3 takes brazier damage with "The brazier is broken and shrapnel damages you." (12 of 12 at distance 1). **T + 5: the pyromancer takes a hit (base 10)** with area sound 513 (`human_hit`), range 10. T + 5 or T + 6 (12/10): "Fix this brazier!". |
| W-A3 | Pyromancer (40) | T: throw to her tile, end **120**, start height 0. 502 delay **115** there. One snow on her tile. T + 3: the snow goes. **T + 6: the hit (base 8)**, area sound 513, range 10. |
| W-A4 | Area (8) | T: throw end **200**. Snow on the target tile and its four **diagonals** (an X, 7 of 7). T + 3: four `wint_area_attack_spot` (1310) drops onto the corners from a tile up to 4 away (start 50, end 90, heights 400 → 0). `wint_area_attack_impact` (1311) delay 90 on the corners, 502 delay 90 on the centre, and a second throw (end **150**). **T + 6: `wint_area_attack_snow` (random rotation) on the corners, `wint_area_attack_icicle` (rotation 0) on the centre, area sound 1487 (`explosion`), range 2.** Everyone in the 3x3 is hit, with "The freezing cold attack of the Wintertodt's magic hits you." T + 13: all five are removed. |
| W-A5 | Standard (56) | On its own 5-tick clock: "The cold of the Wintertodt seeps into your bones." (spam) on T, **the hit on T + 1** (55 of 56). Gaps are 10, 15, 20 ... (a 10-tick cooldown). Never south of z 3988. |

### Rates (per slot of the kind's clock; e = energy / 3500)

| Row | Kind | Fitted | Evidence |
|---|---|---|---|
| W-R1 | Standard | e / 4 per eligible player | 0.229 × e over 669 slots |
| W-R2 | Area | e / 10, at a random eligible player | 3 hits over 33 energy-weighted slots. **Fewest samples of any rate here.** |
| W-R3 | Brazier | (1 − e) / 3, at a random lit brazier, **whatever the number lit** | lit 1: .30/.22/.17/.12; lit 4: .43/.23/.13/.03 by energy fifth |
| W-R4 | Break, given a brazier throw | (1 − e) / 2 | 38%, 33%, 16% of throws at e 0–20, 20–40, 40–60% |
| W-R5 | Pyromancer | (1 − e) / 8, at a random healthy one | .085/.06/.043 at e 0–20/20–40/40–60% |

These follow the Wiki's rule (Mod Ash): standard attacks are more likely at
high energy and brazier/pyromancer attacks at low energy. The divisors are
measured, not published.

## Pyromancers

| Row | Recorded |
|---|---|
| W-P1 | 14 hitpoints. The bar reads 13, 9, 5 and 3 of 30 at 6, 4, 2 and 1 hitpoints. That is `1 + hp × 29 / 14`; the engine's old `hp × 30 / 14` read one short on all four (fixed engine-wide: `ToriRSServer_HealthbarFill`). |
| W-P2 | A hit from full is **max(base, random(14))**: base 8 for the pyromancer attack (14 of 20 hits; 9–13 once or twice each), base 10 for a break (14 of 20; 12 ×4, 13 ×2). A second hit always takes the rest (2, 4, 6), so the second hit downs her. |
| W-P3 | Her damage splat is `hitsplat_damage_other` (17) for every viewer (59 of 59), since no player dealt it. Heals are `hitsplat_heal` (6). |
| W-P4 | The killing hit at T. T + 1: "Arg, it got me!", `dream_spell_start` (7627), area sound 512 (`human_death`), range 3. **T + 3: she becomes `wint_wizard_down`** (10 of 10). A heal before then cancels the fall. |
| W-P5 | Down: one of "We are doomed.", "I think I'm dying.", "Ugh, help me!", "My flame burns low.", "Mummy!" (evenly), **every 10..14 ticks** (474 gaps, uniform). The first comes 8..10 ticks after she falls. |
| W-P6 | Help is instant: the dose and the heal land on the click's tick (29 of 37). There is no player animation. She stands again (transformation, seq −1, heal 14). If her brazier is out, "Light this brazier!" follows the next tick. On an unhurt pyromancer the reply is "No need! I'm fine, thanks." (chathead `chathap1`) plus "You don't need to do that." |
| W-P7 | Each pyromancer is added facing the storm (spawn angle). |

## The round

| Row | Recorded |
|---|---|
| W-L1 | E (energy 0): every brazier goes to `wint_brazier`, the storm to `wint_snow_storm_idle`, and countdown 7980 to 100. Every pyromancer gets the standing form's transformation (sent even if she is already standing) and says "We can rest for a time." |
| W-L2 | Settlement, in order: "You have gained 9900 Firemaking XP." · "Your subdued Wintertodt count is: <col=ff0000>N</col>." · "You have a new high score! N" · "You're owed an additional N rewards from the reward cart. You're now owed M rewards." Crossing 500 points mid-round: "You have helped enough to earn a supply crate." |
| W-L3 | **S = E + 100** (583 → 683, 1078 → 1178): the storm becomes `wint_snow_storm` and the energy 3500. S + 1: each pyromancer says "Light this brazier!". |

## The player

| Row | Action | Recorded |
|---|---|---|
| W-C1 | Chop | On arrival: "You swing your axe at the roots." (spam) and the axe animation. **The first root one tick later, then one every 3 ticks**, every time (198 of 199 gaps; the Wiki's "skilling timer for bruma roots is 3 game ticks"; the axe and level only gate it). Each root: "You get a bruma root." (spam), area sound 2735 (`woodchop`) range 1 on the cutter, and `tracking_logs_chopped` + 1. Between rounds: "There's no use for bruma roots at this time." A full inventory, the tick after the last root: "Your inventory is too full to hold any more roots.", the swing stopped, synth 2277 (`pillory_wrong`). |
| W-C2 | Fletch | `human_fletching` (1248). The first kindling 3 ticks in, then one every 4 with a knife (153 of 163 gaps), each with "You carefully fletch the root into a bundle of kindling." (spam) and the animation again. |
| W-C3 | Light | `wint_light_brazier_bruma` (7174). Lit 3 ticks after it starts (50 of 84; 34 at 2), with synth 2596 (`fire_lit`) and "You light the brazier." A downed pyromancer: "Heal the Pyromancer before lighting the brazier." |
| W-C4 | Fix | `human_poh_build` (3676) with synth 930 (`poh_build_metal`). Fixed 2 ticks after it starts (18 of 29), "You fix the brazier." |
| W-C5 | Feed | `human_pickuptable` (832) with synth 2596 at delay 15. The item goes 3 ticks later with synth 2596, then the next item. Ends with "You have run out of bruma roots."; when the brazier goes out under you, "The brazier has gone out." |
| W-C6 | Herbs | `picking_low` (2282). A herb 3 ticks later with synth 2581 (`pick`) and "You pick a bruma herb." (spam), then every 3 (25 of 25). |
| W-C7 | Mix | No animation, no sound. The first potion at once, the next 2 ticks on, then one every 3. "You combine the bruma herb into the unfinished potion." (spam). |
| W-C8 | Drink | `human_eat`, synth 2401 (`liquid`), "You drink some of your potion." + "You have N doses of potion left." (spam), +30% Warmth. |
| W-C9 | Cold hit | Hitsplat **75 to the victim** (sprite `hitmark_42`, a bright snowflake). The Wiki's "darker on other players" is 76 (`hitmark_43`); other players' splats are hidden in these transcripts. The bar is **healthbar 79** at `1 + warmth × 29 / 1000` (72 of 72), not the Hitpoints bar. Each hit plays one of synths 518–521 (`human_hit_1..4`) at delay 20. |
| W-C10 | Warmth | The loss is in the varbit's tenths and the splat shows tenths / 10: a 10 at FM 99 is 1000 → 899, an area 30 is 455 → 152. Mod Ed's formula, floored in tenths. |
| W-C11 | Death | 0 Warmth: "<col=ef1020>The cold of the Wintertodt has overcome you!", then a `hitsplat_damage_me` for every hitpoint on the standard bar, and `human_death` the next tick (732 tick 89). |
| W-C12 | Doors | Every fade: `fade_overlay:message` blanked, the overlay opened, 948, `hpbar_hud:hp` hidden and `hpbar_hud:container` emptied (2249). In: fade (`fade_overlay` 948, 0,255 → 0,0 over 50) on T. The teleport and the fade back on T + 2, to a random tile in x 1628..1633, z 3979..3984. The overlay closes and the HUD opens on T + 4. Out: the same, to x 1630..1632, z 3955..3959. Mid-round, "Are you sure you want to leave?" first ("Leave and lose all progress." / "Stay."); between rounds no question is asked. Under 4 warm items: "<col=ff8e32>You feel the cold wind strike you as you enter; perhaps you should find some warmer clothes." |
| W-C13 | Peek | A message box: "The Wintertodt has N% energy left. There is 1 player within the prison." / "There are no players within the prison." |
| W-C14 | Drop | "The herb shatters as it hits the floor." |
| W-C15 | Brew'ma | A message box, the count in words: "Brew'ma quickly mixes two Rejuvenation potions." |

## What the map does

| Row | Found |
|---|---|
| W-M1 | The prison floor is a bridge deck. Its locs are on cache level 1 (`m25_62.jl2`) and are walked at level 0, which is what every recorded coordinate shows. The server already shifts them. The client keeps them at cache level 1, so a scriptrun selector names them at level 1 (`test/bosses/wintertodt_round.lua`). |
| W-M2 | Each brazier is ringed by `wint_inviswall` (24720, `blockwalk=1`) on the outside of its 3x3. An ordinary op can never reach one, so lighting, fixing and feeding are approach triggers (`[aploc1,wint_brazier*]`) taken from within a tile of the footprint. The same walls are why varbit 5354 reads 0 beside a brazier. |
| W-M3 | The Warmth and countdown varbits sit on varps 4487 and 1142, which the generated layer had server-only. Jagex transmits both, and the HUD's Warmth bar and timer read them, so both are now authored `transmit=yes`. |

## Explained, not sent

- `firework` (2396) + `burst` (2384, delay 25), range 1, at other players'
  tiles: level-up effects (players at the roots, and everyone on E + 1 when
  the round's XP lands). The recorder is 99 in every skill and never makes
  one. They are the engine's level-up, not Wintertodt's.
- `clobber` (2531), range 1, at tiles beside the braziers between rounds:
  another player's action there. Its message is not in the recorder's view.

## Not recorded (Wiki or unverified)

- The agility gap (`wint_pillar_jump`, pillars at 1628/1630/1632, z 4023):
  60 Agility and 18 XP are the Wiki's. No session uses it, so the animation
  (`human_spot_jump`) and the two-tile hop are unverified.

- Lighting with a tinderbox rather than the bruma torch. The recorder always
  wore `wint_torch_offhand`, and 7174 states `righthand=wint_torch`, so it
  draws a torch whatever the player holds. A tinderbox lighter plays
  `human_createfire` (733), the tinderbox brazier light in RSProx 1694 (226.1,
  "You light the brazier with your tinderbox."); not recorded at Wintertodt.
- The Peek text between rounds.
- The last-dose drink line.
- The drop line for items other than the herb.
- The area-attack rate (W-R2), which rests on three hits.
