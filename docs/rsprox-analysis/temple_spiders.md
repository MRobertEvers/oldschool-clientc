# Temple spiders and chinchompas (Forthos Dungeon) from RSProx

Content: `OSRS-Content/osrs-content/server/scripts/` —
`areas/area_forthos/configs/temple_spider.npc` (sounds, respawn),
`areas/area_forthos/scripts/forthos_doors.rs2` (the eastern Temple Door),
`drop_tables/scripts/temple_spider.rs2` (the table),
`quests/quest_insearchofknowledge/scripts/insearchofknowledge_locs.rs2`
(`~isok_page_drop`: the page tertiary),
`skill_combat/configs/bas/chinchompas_manual.obj` (throw and equip sounds),
`skill_combat/scripts/player/player_ranged.rs2` (the throw's explode synth and
slot-1 clear), `maps/multiway.csv` (the den), `areas/world/configs/m28_155.spawn`
(the egg spawns). Test: `test/combat/temple_spiders_chinning.lua`.

## Sources

| session | rev | what it holds |
|---|---|---|
| index 4517 | 242.1 w496 | 744 kills, red chinchompas, the door ×4, 27 pages (9/9/9), a hard scroll box |
| index 4518 | 242.1 w496 | 185 kills; the bank/GE trip for red chinchompas, the equip (`equip_fun`) |
| index 4520, 4521 | 242.1 w496/w494 | 258 + 327 kills; the door ×6 |
| index 4522 (= 4523) | 242.1 w370 | 42 kills (4523 transcribes to the same stream) |
| index 4528 | 242.1 w370 | 818 kills, grey chinchompas, a hard scroll box |

All `--bas`; 2,374 kills, 8,464 throws, 51,000 spider swings. The wiki's
Temple Spider and Forthos Dungeon pages read end to end; where they disagree the
recording wins.

## Action inventory (rsprox_actions.py; one verdict per row)

| action | verdict |
|---|---|
| `opnpc2 hosdun_spider` (693 + 588 + 735 + ...) | this content: the fight below |
| `oploc1 hosdun_east_door` | this content: the Temple Door (fixed: it had no binding) |
| `oploc1 hosdun_entrance_spiral_steps_hole` | generic maplink: lands 1800,9968, `maplink.dbrow` agrees |
| `oploc1 hosdun_entrance_ladder` | generic maplink: lands 1702,3575, agrees |
| `opobj3` coins, runes, herbs, eggs, scroll box, pages, key, weapons | generic pickup |
| prayers 15/19/20, prayer restore / ranging / antipoison drinks, Low Alchemy | generic, the recorder's kit |
| Kourend / Varrock teleport tablets | generic tablets (§2b audited elsewhere) |
| bank, Grand Exchange, bonds, skill guide, quest list | generic, not this content |
| equip red/grey chinchompas (`op=3`) | this content: `equip_fun` (fixed) |
| `pinball_*`, `dwarf_city_director_*` (4520, random event / other) | not this content |

Teleports: tablets out, the hole in, the ladder out; no other move.

## The Temple Door (4520 [51..59], 17 of 17 passes, every one from the west)

| tick | recorded | ours (ts0) | verdict |
|---|---|---|---|
| O = L+1 | step 1824 -> 1825 (walk), `inviswall` at 1825,9973 rot 0, `hosdun_grubby_door_open` at 1824,9973 rot 1, synth `door_open` (62) | same (blackknight walk-through shape) | fixed |
| O+1 | "The door slams shut and locks behind you." | same | fixed |
| O+2 | door back, leaf gone | temp locs, duration 2 | fixed |
| O+1 | `multiway_indicator` 1 | 1 at t20 | fixed (multiway zones) |

From the east: no recording (the wiki: one-way east until a temple key unlocks it).

## The spider

| rule | recorded | ours | verdict |
|---|---|---|---|
| attack seq `spider_update_attack`, every 4 | 23,313 of 23,956 gaps | 4 | matched |
| max hit (unprayed) | 6 (57 swings, 46% zero) | str 50, bonus 0 -> 6 | matched |
| Protect from Melee | 2,917/2,917 zeros (4521), 26,000 overall | prayed 0 | matched |
| `insect_attack` (537) per swing, `insect_hit` (539) per splat dealt, `insect_death` (538) per death | 27,728 / 38,284 / 2,370 of 2,374 | 537, 539 on every splat, 538 34/34 deaths | fixed (all three were `null`) |
| death seq at D, despawn D+3 | 2,374/2,374 | npc_death 75, seq 76, free 79 | matched |
| loot on the despawn tick, despawn 200, reveal 100 | 2,308 drops | obj_add at 79, 200 | matched |
| respawn: despawn + 49 | 2,285 of 2,300 | 50 -> 49 (25/25 after) | fixed |
| `enabled_ops 0b00000` at D, `0b11111` on the add | 327 / 321 | engine | matched |
| aggressive, hunt 5, wander 5 | spiders walk to the player (12,022 steps) | as generated | not diffed per tile |

## The chinchompa throw (8,464 throws)

| field | recorded | ours | verdict |
|---|---|---|---|
| seq | `human_chinchompa_attack_pvn` (7618) | 7618 | matched |
| attack synth | `throwingaxe` (2706), every throw | 2706 | fixed (no sound before) |
| projectile | `big_chinchompa_grenade` (909) / `chinchompa_grenade` (red / grey), start 21, end 32 + 5d, angle 15, progress 11, heights 163 -> 146 | 21, 37 at d1 | matched (heights x4: 164 -> 144) |
| impact | `firewave_impact` (157) height 92 on the primary, delay = endtime | 157, 92, 37 | matched |
| explode synth | `chinchompa_explode` (360), delay = endtime, every throw | 360 delay 37 | fixed |
| slot-1 clear, height 96 | 1,048 per 1,041 throws (4521) | `spotanim_pl_slot(null, 96, 0, 1)` | fixed |
| defend seq, delay = endtime, every spider splashed | 30,236 at 37 | the wire's last seq of the tick is the delay-37 one (the tick log also shows the funnel's delay-0 row first) | matched on the wire |
| splats | throw + 2 at d 1..5 | throw + 2 | matched |
| cadence | 4 accurate (2,391), 3 rapid (4,145) | engine attack rate | matched |
| multiway | indicator 1 from x 1825 inward; every blast splashes | 16 zones added | fixed |
| equip sound | `equip_fun` (2238), red and grey | stated | fixed |

## The drop table (2,374 kills; per-item counts in `drop_tables/scripts/temple_spider.rs2`)

| row | wiki | recorded | ours |
|---|---|---|---|
| coins 400..600 | 17/100 | 413 (1/5.7) | 17/100 |
| elemental runes 30..50 | 5/100 each | 110..127 each | 5/100 |
| chaos/cosmic/death/nature 10..15 | 2/100 each | 31..56 | 2/100 |
| law | 4..5, 1/100 | 19, all 5 | 5 |
| soul 5 | 1/100 | 35 | 1/100 |
| adamant longsword / med helm, rune dagger / med helm | 4, 4, 1, 1 /100 | 87, 99, 22, 17 | same |
| herbs | 15/100 | 338 (14.2%) | `~randomherb` |
| uncommon seeds | 6/100 | 153 (6.4%) | `~troll_uncommon_seed` |
| red spiders' eggs (noted) | 3..8, 8/100 | 184, all 3..5 | 3..5 |
| superantipoison(2) | 8/100 | 191 | 8/100 |
| weapon poison(+) | 2/100 | 0 (47 expected) | nothing |
| gem table (nature talisman) | 1/100 | 9 uncut gems | `~troll_gem_drop(true)` |
| nothing | 3/100 | 169 (7.1%; some loot fell out of view) | 5/100 |
| grubby key | 1/100 | 22 (1/108) | 1/100 |
| hard clue | 1/200 | 2 scroll boxes (the cap) | `trail_clue_drops.dbrow` (unchanged) |
| tattered page | 1/30 a type | 83 = 1/28.6, one roll | 1/30 one roll |

Page drops: `ownership=self, neverturnpublic=true`, despawn 200, with
"<col=ef1020>Untradeable drop: Tattered temple page</col>" (83 of 83). The type:
9/9/9 in 4517, then 37 temple, 19 moon, 0 sun over 4518..4528. Fitted rule: the
type is weighted by the pages its tome still needs (sun full and moon half full
gives 2:1). The tome counts are not on the wire, so this is a fit.

The den's 11 red spiders' eggs moved to their recorded tiles (10 of 11 were off).

## How it was verified

- Private build `make -C src OPT=1 PLATFORM_OBJ_BASE=build_s08 sscompile torirsserver`;
  scripts with `sscompile --src <content>/server/scripts --out <scratch>/pack`;
  server pack `cachepack pack --server-only`; `pack/*.alloc` unchanged.
- `TORIRSSERVER_DEV=1 ... torirsserver --scriptrun test/combat/temple_spiders_chinning.lua
  --fixture fresh_lumbridge.ini`: SCRIPTRUN PASS; its tick log read against the
  tables above (29 deaths, 132 throws).

## Open

1. The east side of the Temple Door and the temple key's unlock are not recorded.
2. The page-type weighting is a fit; a recording that hands pages in would decide it.
3. Weapon poison(+) never dropped in 2,374 kills; dropped from the table on that count.
4. The tick log shows a delay-0 defend before the delay-37 one on splashed
   spiders (the shared hit funnel's `npc_defend`); the wire carries the later.
5. The zone ambience of the dungeon (druids, the bone burner, the other rooms) is
   not part of this audit.
