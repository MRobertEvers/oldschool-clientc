# CoX supplies: scavengers, resource rooms, herblore, storage, energy wells

The supplies unit's spec, audit and fix log (2026-10-18). Every row names its
source. Sources are under `docs/minigames/cox/sources/` (wiki pages pinned in
`manifest.tsv`, Jagex newsposts in `newsposts/`, video transcripts in
`transcripts/` and `../synq_transcript.md`). Ranking used for every ruling:
**Jagex > recorded data (videos of the live game) > wiki > plugins > Near-Reality
(NR) > our code.**

Content files: `cox_scavengers.rs2`, `cox_resource.rs2`, `cox_herblore.rs2`,
`cox_bats.rs2`, `player/scripts/consumption/cox_potion.rs2`, configs
`cox_resource.{constant,varp,loc,npc}`, `cox_herblore.constant`,
`cox_bat.constant`, `cox_potion.varp`, and the supply block of `cox.constant`.

Short source keys used below:

| key | file |
|---|---|
| W:CoX | `wiki_Chambers_of_Xeric.wikitext` (Resources, Gaining Points, Storage units) |
| W:Strat | `wiki_Chambers_of_Xeric_Strategies.wikitext` |
| W:Pots | `wiki_Chambers_of_Xeric_Potions.wikitext` |
| W:Food | `wiki_Chambers_of_Xeric_Food.wikitext` |
| W:<item> | the item's own page, e.g. `wiki_Xeric_s_aid_plus.wikitext` |
| J:2017-01-06 | `newsposts/wiki_Update_Chambers_of_Xeric_Tweaks.wikitext` |
| J:2017-01-12 | `newsposts/wiki_Update_Raids_Improvements.wikitext` |
| J:2019-01-31 | `newsposts/wiki_Update_Chambers_of_Xeric_Kebos_and_Collection_Log_Changes.wikitext` (+ `..._Revisited` blog) |
| J:2020-11-11 | `newsposts/wiki_Update_The_Nightmare_and_Chambers_of_Xeric_QoL.wikitext` |
| J:2023-11-29 | `newsposts/wiki_Update_Chambers_of_Xeric_Changes.wikitext` |
| Synq | `../synq_transcript.md` (2026 solo guide) |
| Tubby / EdB0ys | `transcript_tubby_solo_cox.md` / `transcript_edb0ys_cox.md` |
| Beleti | `transcripts/yt_1aFJyKKdjn8.md` (solo CM, 2026-02-12) |
| NR | `~/Documents/git_repos/RSPS-NEAR-REALITY/near-reality-server-main/.../chambersofxeric/` |

---

## 1. Scavenger rooms

| quantity | value | source |
|---|---|---|
| npc | Scavenger beast 7548/7549 (50/50 per spawn) | W:Scavenger_beast; NR ScavengerBeast |
| runts | none: removed 31 Jan 2019, both rooms hold beasts | J:2019-01-31 ("Only the larger Scavengers will now appear"); W:Scavenger_runt |
| stats (solo, maxed) | 30 hp, att/str 120, def 45, crush max 13, 4 ticks, aggressive | W:Scavenger_beast infobox |
| xp | 10% of normal combat xp | NR getXpModifier 0.1 |
| count | 1 solo; 2 at 2-4, 3 at 5-7, 4 to 15, ... 8 | NR ScalingMechanics.getScavengerCount (wiki: "determined by the amount of players", no numbers) |
| spawn | random free square within 5 of the anchor, not within 2 of a live beast; walkable | NR findSpawnTile |
| respawn | 2 ticks after death, **near players** | NR getRespawnDelay 2; J:2020-11-11 "spawn more consistently, with a preference for locations near players" |
| drops | bones always + **2 rolls** on an 18-weight table | J:2023-11-29 "only roll two drops"; W:Scavenger_beast |
| table | rod, iron pickaxe, iron axe, butterfly net, hammer, tinderbox 1/18 each; lockpick 2/18; cave worms 30-50 2/18; secondary **bundle** 6/18 (juice 5-14 + stinkhorn 3-11 + cicely 3-6 together); mallignum root planks x2 2/18 | W:Scavenger_beast; J:2023-11-29 |
| drop owner | the most-damage player | NR getDropRecipient; J:2020-11-11 message naming the recipient (text unknown) |
| large-room shortcut | one of rocks (Mining, pickaxe), rotten sapling (Woodcutting, axe), boulder (Strength); random | W:CoX; W:Strat; NR LargeScavengerRoom |
| shortcut level | team average +-2, cap 99 (solo: own base -2..+2) | W:Strat "average respective level of the team, plus or minus 2, capped at 99" (NR: lowest..highest+2 -- outranked) |
| shortcut check | current level (boosts count) | Synq 0:10:34 "drink a strength boosting potion before pushing the boulder"; Mod Ash on the boulder: summed Strength of adjacent pushers |
| shortcut points | level x 5 | W:CoX Gaining Points |
| shortcut time | unpublished; "faster than running around" since 2023 | Mod Ash (W:Boulder); J:2023-11-29 -- **ours**: clears on the first check, 2 ticks |
| after clearing | Rocks -> Rubble (op Cross), sapling -> Rotten stump (op Cross), boulder pushed aside | cache loc ops; W:Rocks/W:Rotten_sapling; NR boulder rolls 3 tiles |
| shortcut tiles | one per door variant: (8,6) CCW, (18,22) THRU, (25,8) CW room-local | NR blockingObjectTiles; W map pins |
| energy well | in every scavenger room | W:CoX; W:Strat |

## 2. Resource rooms

Two per raid: Resources A (template m51_85/m52_85 **plane 0**) is the fishing room,
Resources B (**plane 1**) the bat room. The cache's plane-0 templates carry three
`raids_floor_water_edge1_fishing` (Bubbles) locs per variant and plane 1 none; NR
spawns bats only for RESOURCES_B. W:Food's "50% bats / 50% fishing" is which of
the two a raid deals.

| furniture (template locs) | behaviour | source |
|---|---|---|
| Gourd tree (Pick, Pick-lots) | Pick: one empty gourd every 2 ticks until full; Pick-lots: every free slot after 2 ticks | NR GourdPicking; Beleti 0:16:19 "pick all ... You're going to have 25" |
| Geyser (Use; item on it) | fills every empty gourd | W:Geyser (left-click Use since 19 Oct 2022) |
| Old tools (Take, Take Rake/Spade/Seed Dibber) | one of each, only if not carried | W:Old_tools |
| Herb patch x2 | see 2.1 | |
| Energy well (Touch) | run energy to 100 | W:Energy_well; NR EnergyWell (seq 832, "You feel replenished.") |
| Storage hotspot | see 4 | |

### 2.1 Farming

| quantity | value | source |
|---|---|---|
| plant | seed **used on** the patch; needs a seed dibber | W:Golpar_(plant) etc. "A seed dibber is needed to plant the seed"; cache patch has only op2 Inspect |
| levels / plant xp / pick xp | golpar 27/4/10, buchu 39/6/15, noxifer 55/12/30 (per herb) | W:Pots; W:<herb>_(plant) |
| grow | 30 s = 50 ticks, four stage locs | W:CoX; J:2023-11-29 sped up; cache ids 29997-30011 |
| pick | a **spade** is needed | Tubby 0:35:12; EdB0ys 1:01:11 (NR: none -- outranked) |
| yield | one herb every 2 ticks; 3 lives; a life kept with p = min(0.95, 0.65 + (Farming - plant level)/250) | NR HerbHarvestAction. Wiki: "scales with Farming level" (no formula). Recorded: Beleti brews 21 buchu potions from his patches ("you just need the one, but sometimes it breaks"); our probe: 12 buchu from one patch at 99 |
| party scaling | more herbs in bigger raids (optimal at 14+) | J:2017-01-06, Revisited blog -- solo is the floor, not modelled beyond NR's rule |
| full pack | the herb goes to the floor | J:2017-01-06 |
| clear | op3 on a grown patch, spade | cache op3 Clear; NR HerbClearAction |
| points | **none** for farming | W:CoX "Gaining Points" lists no farming entry |

### 2.2 Weeds (floor start/end rooms, m51_80)

| quantity | value | source |
|---|---|---|
| tool | rake | W:Weeds |
| loop | continues until cancelled; one rake per 3 ticks | W:Weeds; NR RaidRakingAction (NR stops at 5 -- outranked) |
| per rake | 1-3 of one seed, never the seed given last | W:Weeds loot; J:2019-01-31 "rotate between the different seeds" |

### 2.3 Fishing (Resources A)

| quantity | value | source |
|---|---|---|
| gear | fishing rod + cave worms (1 per catch) | W:Food; W:Bubbles |
| fish | the best tier the Fishing level allows | J:2017-01-06 "The type of fish received is based on your Fishing level" |
| tiers | Pysk..Kyren, levels 1/15/30/45/60/75/90, xp 20..38, heal 5..23 | W:Food |
| cadence / chance | 4 ticks; min(round(0.6 x (level - tier level)) + 30, 70)% | NR RaidFishing |
| cave snake | one of the three spots; fishing it: "This seems like a very bad moment to fish here.", the snake rises (seq 7333), 3 ticks later bites everyone adjacent for 3-6 and steals one cooked raid food, dives (seq 4039) at 5 | W:Cave_snake; NR CaveSnake |
| snake spot | moves "occasionally" | W:Bubbles; NR every 50 ticks |

### 2.4 Bats (Resources B)

| quantity | value | source |
|---|---|---|
| count | min(6, 2 + party/8): solo 2 | NR; W:CoX "two to three" |
| tier | random among the best three the Hunter level allows | NR; J:2017-01-06 "lower tier bats will also spawn" |
| catch | butterfly net (or 99 Hunter barehanded); levels 1/15/../90, xp 5..29 | W:Food; W:Guanic_bat |
| respawn | 2 ticks | NR Bat |

### 2.5 Food

Cooked fish/bat heals 5 + 3 x tier; cooked on a **fire** from kindling; the cook gets
4 + 8 x tier points per piece eaten (+50% if eaten by someone else; soft cap 20 x
party, hard 40 x party). W:Food, W:CoX "Cooking Food". Xeric's aid heals more per
slot, so prep rarely cooks (W:CoX Food).

## 3. Herblore

### 3.1 Recipes and levels

| herb | (-) / std / (+) | + stinkhorn | + endarkened juice | + cicely |
|---|---|---|---|---|
| golpar | 47 / 59 / 70 | elder | kodai | twisted |
| buchu | 52 / 65 / 78 | revitalisation | Xeric's aid | prayer enhance |
| noxifer | 60 / 75 / 90 | - | - | antipoison |

Source: W:Pots, W:CoX, each potion's page. A herb is used on a **water-filled gourd
vial while the secondary is carried** (each page). The tier is the best rung the
**Herblore level** reaches (boostable, Recipe boxes). One potion per 2 ticks (Recipe
`ticks = 2`). Overload = noxifer + elder + kodai + twisted, **4 doses each, no vial**
(W:Overload Recipe; NR); (-) and std accept "any variant" of the components, (+)
needs (+) components; a 90 Herblore player with std components makes a std
overload (W:CoX "downgrade accordingly for overloads").

| xp per potion | (-) | std | (+) | source |
|---|---|---|---|---|
| elder/kodai/twisted | 6.5 | 10 | 13 | Recipe boxes |
| revitalisation/aid/enhance | 13.5 | 20 | 26.5 | Recipe boxes |
| antipoison | 13.5 | 20 | 26.5 | W:Antipoison_minus/_potion/_plus |
| overload | 33.5 | 50 | 66.5 | W:Overload pages |
| cleaning golpar/buchu/noxifer | 1 / 2 / 3 (levels 47/52/60) | | | W:Pots; W:Golpar |

### 3.2 Effects

| potion | (-) | standard | (+) | source / ruling |
|---|---|---|---|---|
| elder | Att/Str/Def 4+10% | 5+13% | 6+16% | W:Elder pages |
| twisted | Ranged **and Defence** 4+10% | 5+13% | 6+16% | W:Twisted pages (NR: Ranged only -- outranked) |
| kodai | **Magic only** 4+10% | 5+13% | 6+16% | W:Kodai pages, NR |
| overload | all five 4+10% | 5+13% (+17 at 99) | 6+16% | W:Overload pages; 50 damage as 5 x 10 (W:Overload_plus), reapplied every 25 ticks for 500, +50 hp at the end; refused at <=50 hp (EdB0ys 1:03:22; NR "at least 50") |
| Xeric's aid HP | 1+7% | 2+12% | 3+17% | Mod Ash 4 Jan 2021 "HP: TIER plus ((5 x TIER)+2) percent". W:Xeric's aid (+) says 5+15%: **conflict, ruled for Jagex**; both give +19 at 99 |
| Xeric's aid Def | 1+14% | 2+18% | 3+22% | Mod Ash "TIER plus ((4 x TIER)+10)"; (+) page 5+20%; both +24 at 99 |
| Xeric's aid drain (Att/Str/Rng/Mag) | 1+7% | 2+9% | 4+10% | each page |
| revitalisation | 6+20% | 8+25% | 11+30% | (+): W:Revitalisation_plus; std: W:CoX "Standard potions have the effects of their super variants outside" (super restore); (-): NR only. All stats but Hitpoints |
| prayer enhance | count and interval unpublished; 290 s | same | floor(Prayer/2)+31 points, one per floor(500/count) ticks (6 at 82+) | Mod Ash 5 Feb 2023 (for "a strong one"); W:Prayer_enhance pages; Synq 1:30:08 "you gain one prayer point every time you drink it" |
| antipoison | cure + 90 s | 6 min | 9 min | each page |
| gourd | breaks on the last dose | | | W:CoX "Gourds automatically break upon drinking the last dose" |

Boss drops (not ours, for the supply plan): Tekton 2 overload (+), revit (+), enhance
(+); Vasa overload (+), 2 aids (+), twisted (+); Vespula overload (+), 2 aids, revit,
enhance; small Muttadile overload, enhance; big Muttadile + aid, revit; Vanguards
overload (+) and 2 aid+elder / aid+2 revit+twisted / aid+enhance+kodai; Ice demon
secondaries 2p+5 / 2p+5 / p+4; thieving 2p+3 / 2p+3 / p+2 (W:Strat; J:2023-11-29).

## 4. Storage units

| quantity | value | source |
|---|---|---|
| where | floor ends, resource rooms, thieving, ice demon, outside Olm | W:CoX; W:Strat |
| tiers | small/medium/large/massive: Construction 30/60/90/99, **2 planks per upgrade** (2/4/6/8 cumulative), 150 xp each; capacity 250/500/1,000/1,500 shared, 30/60/90/120 private; tiny (unbuilt) 25 private | W:Tiny_storage_unit; W:CoX table; J:2017-01-12 |
| tool | hammer, dragon warhammer or elder maul | W:CoX |
| points | 100 per tier to the builder (+10 to the plank finders) | W:CoX; J:2017-01-12 |
| shared | an upgrade applies to every storage unit in the raid | W:Tiny_storage_unit |

## 5. Energy wells

Scavenger rooms, resource rooms and the floor ends; touch -> run energy 100 (W:CoX,
W:Strat, J:2023-11-29 "peppered Energy Pools throughout the Raid").

---

## 6. Audit (2026-10-18) and fixes

| mechanic | ours before | OSRS | verdict / fix |
|---|---|---|---|
| floor exit in the resource room | `loc_add raids_descentto1/bossentrance` at the centre | floor-end room | **removed** (root moved it) |
| A vs B room | one proc, no distinction, no fish/bats spawned | A fishing, B bats | **fixed**: B spawns bats; A's Bubbles get Fish handlers |
| scavenger spawn tile | random +-5 of centre, no walkability test | free walkable square | **fixed** (`map_findsquare` lineofwalk) |
| scavenger respawn | anywhere +-5 of centre | near players | **fixed**: anchored on the killer's tile |
| shortcut level | own current level +0..+2 | base +-2, cap 99 | **fixed** |
| shortcut tools | none | pickaxe / axe | **fixed** (+ swing / push anims) |
| shortcut level storage | one player varp per raid (2 large rooms overwrite) | per room | **fixed**: per floor in `^cox_var_supplies`, varp mirrors |
| Rubble / stump Cross | no handler (dead end) | Cross over | **fixed** (`p_exactmove` to the opposite tile) |
| planting | `op1 Plant` added by our .loc; no dibber | seed on patch, dibber | **fixed**: `[oplocu,raids_patch_empty]`, dibber; .loc override removed |
| grow timing | 13+13+12+12 delays = 54 ticks | 50 ticks | **fixed** 12+12+11+11 |
| harvest | instant 3..8 herbs (ours), no spade, 20 points/herb | 2-tick picks, 3 lives, spade, no points | **fixed** |
| Clear | none | op3, spade | **added** |
| xp (farming/cleaning/brewing) | whole numbers passed to `stat_advance` (tenths): 0.4 xp plants; flat 2.5 xp brews | per page | **fixed** (x10; per-family/tier table) |
| weeds | one click: 1 of each seed, loc deleted | rake, loop, 1-3 of a rotating seed, loc stays | **fixed** |
| old tools | no handler | rake/spade/dibber | **added** |
| gourd Pick-lots | missing | fill all | **added**; Pick loops |
| geyser | op1 only | + item on geyser | **added** |
| energy well | heal, no anim | anim 832 | ok (message NR's) |
| brewing | single brew, overload consumed a water vial and returned 3 empty gourds; antipoison never brewable; secondary-on-vial ignored | make menu, 2 ticks, overload no vial, antipoison | **fixed** (skill_multi menu) |
| kodai | Magic + Defence | Magic | **fixed** |
| revitalisation | 8+25 / 10+30 / 12+35 combat+prayer only | 6+20 / 8+25 / 11+30 all stats | **fixed** |
| Xeric's aid drain | 2+9% every tier | 1+7 / 2+9 / 4+10 | **fixed** |
| prayer enhance | +1 per 10 ticks for 458 ticks, every tier | (+) Mod Ash; 290 s | **fixed** (std/(-) interval ours, flagged) |
| overload hp gate | none | > 50 hp | **fixed** |
| empty gourd | returned on last dose | breaks | **fixed** |
| cave snake | none | rises/bites/steals | **added** (seed-derived spot, no timer) |
| raid food | no eat handlers | heals 5+3t | **eat added**; cooking NOT (below) |
| storage build/upgrade | none | planks, hammer, Construction, 100 pts, raid-wide | **added** (op5) |
| storage Shared / Private | none | two stores + interfaces | **wired (round 2)**: the cache's interfaces and clientscripts (1589 shared, 404 private, 1614 side), shared = enum_1666 items as an "other" inventory, capacities by tier; solo scope (the store is the raider's own, emptied when a new raid first opens it) |
| scavenger placement | at the raid build, `map_findsquare` around the centre | a walkable tile | **fixed (round 2)**: the build's collision scene does not hold far rooms, so `map_findsquare` answered its anchor -- the centre, a pillar in some variants (6/24 small rooms had their beast in a wall). Now placed by a queue once the raider stands in the room, a line of walk from his tile (a random search tile once put a beast in a walled pocket), rejecting blocked tiles, never outside the room; a beast that chased over the room's edge and died next door respawns in its own room |
| drop-owner message | none | message naming the recipient | not done (text unknown) |

Probe (scriptrun, seed 5, `::maxstats`; `/private/tmp/claude-501/-Users-matthewevers-Documents-git-repos-3draster/161e2e26-be26-45f2-96e7-4b10a354aafa/scratchpad/probe_supplies.lua`, readout `::coxsupplies`):
tools, Pick, geyser, plant, grow (47 ticks seen), harvest (12 grimy buchu at 99),
clean, brew Xeric's aid (+), drink, snake refusal, a kyren catch, energy well, floor-
end storage build (planks 4->2), bat catch press, roots shortcut cleared, a scavenger
kill dropping bones, weeds raking, overload (+) brewed and drunk. **0 aborts, 0
`no primary player bound`, 0 content-contract lines** in stdout.

### Still wrong or unsettled

1. **Storage stores** are wired for a solo raider; one store SHARED between raiders
   (OSRS: "anyone can use it") needs a party owner row. A build elsewhere updates
   other hotspots only when they are next touched.
2. ~~Resource-room A storage hotspot unreachable~~ -- not collision: the round-1 probe's
   `loc_near` is level-blind and picked the copy on floor 2 straight above (same x,z,
   level 1). The level-0 hotspot (template (11,25) angle 3, `forceapproach=23` -> open
   side south after the turn) opens Private storage from its south tile.
3. **Cooking** (kindling from the room's Sapling `raids_woodsource_roots`, a fire,
   raw->cooked) and the cook's points need firemaking/cooking table rows in files this
   unit does not own; Ice demon's kindling has the same need.
4. Unpublished, ours and flagged: shortcut resistance/time; prayer enhance (-)/std
   counts; revitalisation (-) (NR); herb yield party scaling beyond solo; bat count 2
   (NR) vs wiki "two to three".
5. Snake bites only the fisher who woke it (others adjacent are not hit).
6. ~~invented fishing-spot npc~~ deleted (round 2: record, `pack/npc.server` line,
   generated block).
8. **Cooking** needs, outside this unit: 14 rows (7 fish, 7 bats: raw/cooked/burnt,
   levels 1..90, xp 10..28, burn chances) in `skill_cooking/configs/cooking_generic.dbrow`
   (`[oplocu,_cooking_fire]` in `skill_cooking/scripts/cooking.rs2` reads it); kindling
   (`raids_wood`) as a lightable log in `skill_firemaking` (the tinderbox handler and its
   log table), and a Woodcutting handler for the room's Sapling `raids_woodsource_roots`
   -> `_depleted` (shared with the Ice demon's kindling); the cook's points need an obj var
   naming the cook, read by `cox_resource.rs2`'s eat handler.
7. `^cox_var_supplies` (instance register 30) is new; the root should know it is taken.
