# Finishing Agility

Plan to take `OSRS-Content/osrs239-content/server/scripts/skill_agility/` from
its current rooftops-plus-six-shortcuts port to the complete current
[Agility](https://oldschool.runescape.wiki/w/Agility) skill.

The OSRS Wiki snapshot checked on **2026-08-17** is the gameplay authority for
levels, XP, obstacle order, failure rules, rewards, and requirements. The
checked-in cache is the naming and interaction authority:

- `configs/all.loc` (+ `all.loc.compack` for the id↔name map) for obstacle and
  shortcut scenery, their ops, and multilocs;
- `configs/all.npc` for course NPCs (`gnometrainer`, `gunnjorn`,
  `rooftops_grace`, `werewolf_skullballboss`, `agilityarena_clerk`,
  `agilityarena_tickettrader`);
- `configs/all.obj` for rewards and equipment;
- `configs/all.seq` / `all.spotanim` for obstacle animations;
- `server/scripts/areas/world/configs/*.spawn` for authored NPC placement.

Do not infer the feature set from `meta.ini` saying revision 239. This cache
already carries Varlamore's Colossal Wyrm course
(`varlamore_wyrm_agility_*`), the Hallowed Sepulchre (`sepulchre_*`,
`hallowed_*`), Prifddinas (`prif_agility_*`), Shayzien
(`shayzien_agility_*`) and Pollnivneach (`rooftops_pollnivneach_*`). Use the
records that are actually present, and resolve every name from
`all.loc.compack` rather than copying an id out of a reference server.

Where a Wiki summary table disagrees with the obstacle's own dedicated page,
record the conflict and use the dedicated page — the same rule the Hunter plan
established. Two are already known and are called out in §1.3 and §4.2.

Primary references:

- [Agility](https://oldschool.runescape.wiki/w/Agility)
- [Agility course](https://oldschool.runescape.wiki/w/Agility_course) (the
  authoritative enumeration of every course)
- [Agility training](https://oldschool.runescape.wiki/w/Agility_training)
- [Agility/Experience table](https://oldschool.runescape.wiki/w/Agility/Experience_table)
  (per-obstacle XP for every obstacle in the game)
- [Shortcuts](https://oldschool.runescape.wiki/w/Shortcuts)
- [Mark of grace](https://oldschool.runescape.wiki/w/Mark_of_grace) ·
  [Graceful outfit](https://oldschool.runescape.wiki/w/Graceful_outfit)
- [Run energy](https://oldschool.runescape.wiki/w/Run_energy)
- [Giant squirrel](https://oldschool.runescape.wiki/w/Giant_squirrel) ·
  [Agility cape](https://oldschool.runescape.wiki/w/Agility_cape)

This document supersedes rows **#92–#99** of
[SKILLS_CONTENT_PORT_QUEUE.md](SKILLS_CONTENT_PORT_QUEUE.md), which record the
same gaps at one line each.

## Reference corpora, and where each one runs out

| corpus | covers | does not cover |
|---|---|---|
| `LostCity_Server/content/scripts/skill_agility/` (2004) | `agility.rs2` (170), `gnome_course.rs2` (110), `barbarian_course.rs2` (156), `wilderness_course.rs2` (178), `shortcuts.rs2` (271) | everything post-2004: rooftops, marks of grace, modern courses |
| `LostCity_Server/content/scripts/minigames/game_agilityarena/` | `agilityarena.rs2`, `agilityarena_zones.rs2` (286), clerk + ticket trader, plus configs and interfaces — a complete Brimhaven arena | the 2024 Brimhaven voucher / graceful recolour layer |
| `Kronos184-Fixed_2/.../skills/agility/` | 8 rooftop courses (already ported), `MarkOfGrace.java`, `AgilityPet.java`, `Shortcuts.java`, `shortcut/{RopeSwing,SteppingStone,Grappling,Stile,ClimbingRocks,UnderwallTunnel,LooseRailing,CrumblingWall}.java` | Pollnivneach, and every non-rooftop modern course |
| `2009scape/Server/src/main/content/global/skill/agility/` | `AgilityHandler`/`AgilityCourse`/`AgilityShortcut` framework, `pyramid/*` (4 files incl. the rolling block NPC), `brimhaven/*` (arena + 5 trap classes), `WildernessCourse.kt`, `BarbarianOutpostCourse.kt`, `GnomeStrongholdCourse.kt`, `YanilleAgilityDungeon.kt`, `shortcuts/grapple/*` (8 files), `StileShortcut`, `PipeShortcut`, `SteppingStoneShortcut`, `RockClimbShortcut`, `MonkeyBarShortcut` | Ape Atoll, Werewolf course, Werewolf Skullball, Penguin, Dorgesh-Kaan, Pollnivneach, Prifddinas, Shayzien, Colossal Wyrm, Hallowed Sepulchre |

**Author-from-wiki-and-cache (no corpus at all):** Ape Atoll, Werewolf course,
Werewolf Skullball, Penguin, Dorgesh-Kaan, Pollnivneach rooftop, Prifddinas,
Shayzien basic + advanced, Colossal Wyrm basic + advanced, Hallowed Sepulchre.
That is eleven of the twenty-four courses and is the bulk of the work.

---

## 0. Measured repository state

`skill_agility/` is **1,433 lines** of script plus a 1,004-line generated
dbrow. Everything in it is either the Gnome Stronghold course, one of eight
rooftops, or a shortcut.

| file | lines | what it is |
|---|---|---|
| `scripts/agility.rs2` | 188 | shared helpers: `~agility_climb_up`, `~agility_force_move`, `~agility_walk`, `~clipped_telewalk`, `~agility_exactmove`, `~agility_sound_for_seq`, `~agility_delay_fail`, `~forcemove`/`~forcewalk`/`~forcewalk2`, the `~set_*_bas` family, `~change_merged_loc` |
| `scripts/gnome_course.rs2` | 116 | 7 obstacles + `~update_gnome_varp` lap tracker + trainer barks |
| `scripts/rooftop_*.rs2` (8) | 698 | Draynor, Al Kharid, Varrock, Canifis, Falador, Seers', Rellekka, Ardougne |
| `scripts/agility_shortcuts_osrs.rs2` | 124 | `~agility_mark_of_grace` + 6 hand-authored shortcuts (Falador wall, GE tunnel ×2, Lava Dragon Isle, Lava Maze, Deep Wilderness crevice) |
| `scripts/maplink_agility.rs2` | 29 | `~maplink_agility` + the three `[oploc<n>,_maplink_agility]` category triggers |
| `configs/maplink_agility.dbrow` | 1,004 | **generated** — 143 level-gated teleport rows |
| `configs/maplink_agility.{dbtable,loc}` | 206 | schema (`src`/`dest`/`loc`/`level`) + the `category=maplink_agility` overlay |
| `configs/agility.varp` | 46 | gnome lap progress + 5 pipe/ledge busy-timer varps (Yanille and Brimhaven ones are declared but unused) |
| `configs/agility.param` | 26 | `start_coord`, `end_coord`, `fail_coord`, `agil_level_req`, `obstacle_low_fail`, `obstacle_high_fail`, `agil_xp`, `dir` — declared, but **no course reads them**; every course hardcodes |

Elsewhere in the tree:

- `minigames/game_agilityarena/` — 86 lines, dialogue only. Both NPCs end in
  `mes("The Agility Arena isn't open for business yet.")`.
- `areas/area_barbarian_outpost/scripts/gunnjorn.rs2` — the course NPC exists,
  the course does not.
- `general/scripts/enchanted_jewellry/ring_of_endurance.rs2` — charge tracking
  is complete; its own header records that the passive 15 % drain reduction
  has nowhere to attach.
- `docs/SKILLING_SOUNDS.md` §4.12 — the full agility sound catalogue by
  obstacle kind, already mapped, including the layer-W rooftop rows.
- `docs/MAPLINKS.md` §3.3 — how the 143 shortcut rows were harvested, and why
  57 verified rows were deliberately dropped rather than bound unsafely.

### 0.1 XP audit of what is already implemented

Summing every `stat_advance(agility, …)` plus the XP argument of
`~agility_force_move` / `~agility_climb_up` per course, against the Wiki's
stated lap total (XP values in tree are tenths):

| course | in tree | wiki lap total | delta |
|---|---|---|---|
| [Draynor](https://oldschool.runescape.wiki/w/Draynor_Village_Rooftop_Course) | 120.0 | 120 | ✅ |
| [Al Kharid](https://oldschool.runescape.wiki/w/Al_Kharid_Rooftop_Course) | 180.0 | 216 | ❌ −36 |
| [Varrock](https://oldschool.runescape.wiki/w/Varrock_Rooftop_Course) | 238.0 | 269.7 | ❌ −31.7 |
| [Canifis](https://oldschool.runescape.wiki/w/Canifis_Rooftop_Course) | 240.0 | 240 | ✅ |
| [Falador](https://oldschool.runescape.wiki/w/Falador_Rooftop_Course) | 440.0 | 586 | ❌ −146 |
| [Seers' Village](https://oldschool.runescape.wiki/w/Seers%27_Village_Rooftop_Course) | 570.0 | 570 | ✅ |
| [Rellekka](https://oldschool.runescape.wiki/w/Rellekka_Rooftop_Course) | 780.0 | 780 | ✅ |
| [Ardougne](https://oldschool.runescape.wiki/w/Ardougne_Rooftop_Course) | 793.0 | 889 | ❌ −96 |
| [Gnome Stronghold](https://oldschool.runescape.wiki/w/Gnome_Stronghold_Agility_Course) | 86.5 | 110.5 | ❌ −24 (LostCity 2004 values, never reconciled) |

Four rooftops and the Gnome course pay the wrong XP. Falador is also missing
obstacles outright — the Wiki lists thirteen, the script has eleven XP-paying
steps.

---

## 1. Correctness defects that must be fixed before any new course lands

### 1.1 There is no lap. Every obstacle is independent

Only the Gnome course tracks progress (`%gnome_course_progress`, advanced only
when `completed <= progress + 1`, paying 39 XP and resetting at 7). Every
rooftop instead folds the lap bonus into the last obstacle's `stat_advance`
and never checks that the player did the preceding ones — Draynor's crate pays
79 XP to anyone who clicks it, from any approach, forever.

OSRS awards the completion bonus only for a full in-order lap, and this is
also what gates marks of grace and the pet roll. Every course therefore needs
the Gnome course's progress model generalised (§6.1) before its bonus is
trustworthy.

### 1.2 No obstacle can be failed

`~agility_delay_fail` exists — animation, `stumble_loop` sound, facing, delay,
telejump, damage, message, death/phoenix/ring-of-life handling — and is called
from **nowhere**. Every rooftop obstacle succeeds unconditionally at every
level, so the level-scaled success curves that define the skill are absent.

Per the Wiki, failure stops at: Canifis 64, Falador 66, Seers' 79, Rellekka
85, Pollnivneach 85, Prifddinas 91, Ardougne 95, Barbarian Outpost 93,
Ape Atoll 75, Agility Pyramid 75. Canifis' documented curve — 88.28 % success
at 40, scaling linearly to 100 % at 64 — is the shape to implement (§6.2).

### 1.3 Marks of grace use the wrong model entirely

`~agility_mark_of_grace($level_req)` is `random(200) >= level_req → nothing`,
i.e. a flat per-final-obstacle roll of `level_req/200`, taken from Kronos. The
[Mark of grace](https://oldschool.runescape.wiki/w/Mark_of_grace) page states a
different mechanic on every axis:

| axis | in tree | OSRS |
|---|---|---|
| trigger | last obstacle of the course | lap completion |
| rate | `level_req/200` | 2/6 baseline; **2/3** Canifis and Ardougne; 2/5 Rellekka |
| pacing | none | a 3-minute cooldown (2 minutes on Ardougne with the elite Kandarin/Ardougne diary) |
| placement | the player's tile | one of the course's own 5–8 published spawn tiles |
| lifetime | `^lootdrop_duration` | 10 minutes, restarting when another mark stacks on the tile |
| visibility | whatever `obj_add` does | per-player; only the owner sees their mark |
| level penalty | none | 80 % reduction when unboosted level ≥ course requirement + 20 — **except Canifis, which the Wiki documents as a live Jagex bug exempting it**; reproduce the bug, it is current behaviour |
| courses | rooftops only | rooftops **plus** Gnome Stronghold, Shayzien basic and advanced, Barbarian Outpost, Ape Atoll, Werewolf and Dorgesh-Kaan (added 8 May 2024) |

### 1.4 143 shortcuts are silent teleports

`~maplink_agility` checks the level and calls `p_teleport`. No animation, no
sound, no XP, no failure, no arrival delay, no direction check. That was the
deliberate and correct scope for the maplink importer (see
[MAPLINKS.md](MAPLINKS.md) §3.3 — a wrong skill gate is worse than an unfixed
shortcut), but it is not the finished shortcut. Several of those rows are
obstacles that award XP and can hurt the player: the level-51 Edgeville pipe
(10 XP), the level-63 Forthos strange floor (10 XP, 5 on a fail, damage), the
level-46 Zanaris jutting wall (9.9 XP).

### 1.5 The parameter table is dead

`agility.param` declares exactly the schema a data-driven obstacle needs
(`start_coord`, `end_coord`, `fail_coord`, `agil_level_req`,
`obstacle_low_fail`, `obstacle_high_fail`, `agil_xp`, `dir`) and not one
course reads it. Either the courses move onto it (§6.4, recommended) or it
should be deleted; leaving a declared-but-unread schema invites a future
course to half-adopt it.

### 1.6 Run energy — now a feature flag; the modifier hooks are still missing

**Landed.** The formula pair is an era decision, selected by
`run_energy_model` in the feature table
([features.h](../src/features/features.h), `enum ToriRS_RunEnergyModel`) and
implemented in [mock230_runenergy.c](../src/net/mock/mock230_runenergy.c):

| model | drain per running tick | restore per idle tick |
|---|---|---|
| `classic` (0, the `lostcity` era) | `67 + 67·kg/64` | `agility/6 + 8` |
| `osrs2025` (1, the `osrs` and `server_routed` eras) | `floor((60 + 67·kg/64) · (1 − agility/300))` | `floor(agility/10) + 15` |

Zero stays the 2004 pair, per the table's zero-is-classic rule; the `osrs` era
selects the modern one because every Agility number in this document is a
current-wiki number. Server-only — the client is sent a percentage and computes
nothing — so there is no client half to keep in step. Overridable per boot with
`MOCK230_RUN_ENERGY=classic|osrs2025`, which is what makes the two answers
measurable back to back on one account, and reported in the boot's own feature
line (`run_energy=osrs2025`). Verified by `make -C src test-run-energy` (the
arithmetic, as literals from each reference) and by the world selftest (the
wiring: that the tick charges this player's weight and level through the
selected model, once).

Still open, and what A5 now means: the three multipliers the modern model
takes — graceful ×1.3 restore, stamina ×0.3 drain, charged ring of endurance
×0.85 drain. `ring_of_endurance.rs2`'s own header records that no drain-rate
hook exists for content to attach to; stamina is the one modifier already
wired, through the `stamina_active` varbit read in the tick.

[Run energy](https://oldschool.runescape.wiki/w/Run_energy) gives the current
rules (8 January 2025 rework):

```
drain per running tick   = floor((60 + 67 * clamp(weight,0,64) / 64) * (1 - agility/300))
restore per idle tick    = floor(agility/10) + 15
full graceful set        restore = floor(1.3 * R)
stamina potion           drain   = floor(0.30 * drain)
charged ring of endurance (>=500 charges) drain = floor(0.85 * drain)
```

Both halves of that pair are implemented and selected by the flag above. The
modifiers are the remainder, and they are engine work, not content (§8).

---

## 2. Courses — the complete inventory

Every course the [Agility course](https://oldschool.runescape.wiki/w/Agility_course)
index enumerates. "Cache" is what `all.loc` already carries for it.

### 2.1 Rooftop courses

| Course | Lvl | XP/lap | Fails until | Marks | Cache | Status |
|---|---|---|---|---|---|---|
| [Draynor Village](https://oldschool.runescape.wiki/w/Draynor_Village_Rooftop_Course) | 1 (was 10 before May 2024) | 120 | never (May 2024) | ✅ | `rooftops_draynor_*` | in tree; **level gate says 10, must be 1** |
| [Al Kharid](https://oldschool.runescape.wiki/w/Al_Kharid_Rooftop_Course) | 20 | 216 | tightrope 1 + zip line, 1–5 damage | ✅ | `rooftops_kharid_*` | in tree; XP short by 36 |
| [Varrock](https://oldschool.runescape.wiki/w/Varrock_Rooftop_Course) | 30 | 269.7 | clothes line (3–8), wall (2–5) | ✅ | `rooftops_varrock_*` | in tree; XP short by 31.7 |
| [Canifis](https://oldschool.runescape.wiki/w/Canifis_Rooftop_Course) | 40 | 240 | gap 3, until 64 | ✅ 2/3, no level penalty (bug) | `rooftops_canifis_*` | in tree; XP correct |
| [Falador](https://oldschool.runescape.wiki/w/Falador_Rooftop_Course) | 50 | 586 | hand holds (3–8), until 66 | ✅ | `rooftops_falador_*` | in tree; **2 obstacles missing**, XP short by 146 |
| [Seers' Village](https://oldschool.runescape.wiki/w/Seers%27_Village_Rooftop_Course) | 60 | 570 | gap 1 + tightrope, until 79 | ✅ | `rooftops_seers_*` | in tree; XP correct |
| [Pollnivneach](https://oldschool.runescape.wiki/w/Pollnivneach_Rooftop_Course) | 70 | 890 (1,016 with hard Desert diary) | market stall, until 85 | ✅ | `rooftops_pollnivneach_{basket,marketstall,hangingbanner,gap,tree,treetop,wallclimb,monkeybars_*,line}` — 11 locs, complete | **absent** |
| [Rellekka](https://oldschool.runescape.wiki/w/Rellekka_Rooftop_Course) | 80 | 780 (920 with hard Fremennik diary) | tightrope 1, until 85 | ✅ 2/5 | `rooftops_rellekka_*` | in tree; XP correct, diary variant missing |
| [Ardougne](https://oldschool.runescape.wiki/w/Ardougne_Rooftop_Course) | 90 | 889 | gaps 1 + 2, until 95 | ✅ 2/3, 2-min timer with elite diary | `rooftops_ardougne_*` | in tree; XP short by 96 |

### 2.2 Non-rooftop courses

| Course | Lvl | Access requirement | XP/lap | Cache | Corpus | Status |
|---|---|---|---|---|---|---|
| [Gnome Stronghold](https://oldschool.runescape.wiki/w/Gnome_Stronghold_Agility_Course) | 1 | — | 110.5 (86.5 in tree) | `gnome_log_balance1`, `obstical_net2/3`, `climbing_branch`, `balancing_rope`, `climbing_tree{,2}`, `obstical_pipe3_{1,2}` | LC + 2009scape + Kronos | in tree, wrong XP, marks missing |
| [Shayzien basic](https://oldschool.runescape.wiki/w/Shayzien_Agility_Course) | 1 | — | 153.5 | `shayzien_agility_*` (39 locs) | none | **absent** |
| [Agility Pyramid](https://oldschool.runescape.wiki/w/Agility_Pyramid) | 30 | desert heat; waterskins / Desert amulet 4 / circlet of water | 722 + level-scaled doorway bonus (`300 + level×8`, capped ~1,000 at 88) | `agility_pyramid_*` (30+ locs incl. `..._climbing_rocks`, `..._gap1-3`, `..._ledge*`, `..._low_wall`, `..._door_*_hotspot`) | 2009scape `pyramid/*` incl. `RollingBlock`/`MovingBlockNPC` | **absent** |
| [Penguin](https://oldschool.runescape.wiki/w/Penguin_Agility_Course) | 30 (boostable) | [Cold War](https://oldschool.runescape.wiki/w/Cold_War) partial + clockwork suit | 540 | `peng_agility_crushcourse_piece01-*`, `peng_agility_crashtest_*` | none | **absent** — highest giant-squirrel rate in the game |
| [Barbarian Outpost](https://oldschool.runescape.wiki/w/Barbarian_Outpost_Agility_Course) | 35 | [Alfred Grimhand's Barcrawl](https://oldschool.runescape.wiki/w/Alfred_Grimhand%27s_Barcrawl) | 153.3 Agility + 41.3 **Strength** | `agility_obstical_net_barbarian`, `agility_obstical_pipe_barbarian`, `barbarian_log_balance1`, `balancing_ledge3`, `obstical_ropeswing*`, `agility_crumblywall1/2` | LC `barbarian_course.rs2`, 2009scape `BarbarianOutpostCourse.kt` | **absent**; NPC `gunnjorn` already scripted |
| [Shayzien advanced](https://oldschool.runescape.wiki/w/Shayzien_Agility_Course) | 45 | crossbow + mith grapple for the zipline | 508 | `shayzien_agility_*` | none | **absent** |
| [Ape Atoll](https://oldschool.runescape.wiki/w/Ape_Atoll_Agility_Course) | 48 (unboostable) | [Monkey Madness I](https://oldschool.runescape.wiki/w/Monkey_Madness_I) ch. 2 + ninja/Kruk greegree worn | 580 (300 of it the lap bonus) | `mm_agility_wall_top*`, `mm2_monkeybars_*`, skull-slope + tropical-tree locs to resolve | none | **absent** |
| [Colossal Wyrm basic](https://oldschool.runescape.wiki/w/Colossal_Wyrm_Agility_Course) | 50 | [Children of the Sun](https://oldschool.runescape.wiki/w/Children_of_the_Sun) | 633 | `varlamore_wyrm_agility_basic_*` + `..._end_zipline_trigger` + `..._termites` | none | **absent** |
| [Wilderness](https://oldschool.runescape.wiki/w/Wilderness_Agility_Course) | 52 to enter the gate (obstacles 49) | Wilderness, singles-plus | 571.4 | `obstical_pipe1/2`, `obstical_ropeswing*`, `crumbled_wall`, `wildy_agility_pillar` (the dispenser) | LC `wilderness_course.rs2`, 2009scape `WildernessCourse.kt` | **absent** |
| [Hallowed Sepulchre](https://oldschool.runescape.wiki/w/Hallowed_Sepulchre) | 52 / 62 / 72 / 77 / 87 per floor | [Sins of the Father](https://oldschool.runescape.wiki/w/Sins_of_the_Father) | 575 / 925 / 1,600 / 2,875 / 5,725 per floor | `sepulchre_*` (103 locs), `hallowed_*` (148 locs) incl. every coffin variant | none | **absent** — largest single slice |
| [Werewolf](https://oldschool.runescape.wiki/w/Werewolf_Agility_Course) | 60 | [Creature of Fenkenstrain](https://oldschool.runescape.wiki/w/Creature_of_Fenkenstrain) + ring of charos | 350 + 380 stick bonus | `werewolf_steping_stone`, `werewolf_hurdle_*`, `werewolf_skull_climb_1/2`, `werewolf_slide_*` | none | **absent** |
| [Colossal Wyrm advanced](https://oldschool.runescape.wiki/w/Colossal_Wyrm_Agility_Course) | 62 | Children of the Sun | 1,053.6 | `varlamore_wyrm_agility_advanced_*` | none | **absent** |
| [Dorgesh-Kaan](https://oldschool.runescape.wiki/w/Dorgesh-Kaan_Agility_Course) | 70 | [Death to the Dorgeshuun](https://oldschool.runescape.wiki/w/Death_to_the_Dorgeshuun) + light source | 2,750 incl. the Turgall delivery bonus | `dorgesh_caves_cable*`, `dorgesh_caves_monkeybars_*`, `dorgesh_caves_pipe_agility_*` | none | **absent** |
| [Prifddinas](https://oldschool.runescape.wiki/w/Prifddinas_Agility_Course) | 75 | [Song of the Elves](https://oldschool.runescape.wiki/w/Song_of_the_Elves) | 1,285.2 (+82 and a crystal shard per portal used) | `prif_agility_*` (26 locs incl. `..._shortcut_portal*`, `..._dark_hole_active/inactive`) | none | **absent** |

### 2.3 Obstacle clusters that are not lap courses

| Place | Levels | XP | Cache | Status |
|---|---|---|---|---|
| [Yanille Agility Dungeon](https://oldschool.runescape.wiki/w/Yanille_Agility_Dungeon) | ledge 40 (22.5), pipe 49 (7.5), monkeybars 57 (20), rubble 67 (5.5) | as listed; ledge/monkeybars fail for up to 15 damage into the spider level | `balancing_ledge3`, `monkeybars_end1/2`, `climbingcaverocks1/2` | rubble scripted; ledge/pipe/monkeybars absent. `yanille_obstacle_pipe_used` / `yanille_ledge_used` varps already declared and unused |
| [Brimhaven Dungeon](https://oldschool.runescape.wiki/w/Brimhaven_Dungeon) obstacles | pipes 1/22/34, log balances 1/30, stepping stones 12/56 | 8.5–10 | `karam_dungeon_*` | pipes/logs/stones partly scripted, XP not awarded |
| [Edgeville Dungeon](https://oldschool.runescape.wiki/w/Edgeville_Dungeon) pipes | 51 and 60 | 10 each | `varrock_dungeon_pipe_sc` | maplink row only (no XP) |
| Wintertodt pillar gap | 60 Agility + 50 Firemaking | 18 | — | absent; belongs with Wintertodt |
| [Yama's Lair](https://oldschool.runescape.wiki/w/Yama%27s_Lair) stones | 75 | 0 | `id:*` (Mokhaiotl pillars) | absent; belongs with that boss |

---

## 3. Minigames and non-course XP sources

| Activity | Req | Mechanic | Cache | Corpus | Status |
|---|---|---|---|---|---|
| [Brimhaven Agility Arena](https://oldschool.runescape.wiki/w/Brimhaven_Agility_Arena) | 1 Agility, 200 gp entry | 25 pillars / 24 dispensers, one arrow-flagged per minute; tag it for a ticket + voucher + `30 × floor(level/10)` XP capped at 300; consecutive-tag streak rule; elite Karamja diary = 10 % double | `agilityarena_*` locs (39), `agilityarena_overlay.if`, `agilityarena_rewards.if`, `agilityarena_ticket`, `agilityarena_ticket_new`, `agilityarena_voucher` | LC `agilityarena.rs2` + `agilityarena_zones.rs2`; 2009scape `brimhaven/*` traps | dialogue stubs only |
| [Werewolf Skullball](https://oldschool.runescape.wiki/w/Werewolf_Skullball) | 25 Agility, Creature of Fenkenstrain, ring of charos | tap 1 / kick 4 / shoot 9 tiles through 10 goals; 750 XP under 4 min, −8 XP per 3 s over | `werewolf_goal_{left,mid,right}`, `werewolf_arrow`, `werewolf_spot`, NPC `werewolf_skullballboss`, `waa_skullball` | none | **absent** |
| [Gnome Ball](https://oldschool.runescape.wiki/w/Gnome_Ball) | — | 5 goals, 4/5/6/7/30 XP | `gnomeball_*` | none | absent |
| [Rogues' Den](https://oldschool.runescape.wiki/w/Rogues%27_Den) maze | 50 Agility **and** 50 Thieving, unboostable, empty inventory | contortion bars, pendulums, ledges, floor blades, spinning blades, guards, mosaic + gear doors; reward is the rogue outfit | `roguesden_obstacle_*` | none | absent — Thieving-owned, Agility-gated |
| [Barbarian Fishing](https://oldschool.runescape.wiki/w/Barbarian_Fishing) | 15/30/45 | passive Agility + Strength alongside Fishing (5/6/7 XP) | — | — | **already live** (FISHING_COMPLETION_PLAN S9) |
| [Blast Furnace](https://oldschool.runescape.wiki/w/Blast_Furnace) belt | — | 1 XP/tick pumping | — | — | absent, Smithing-owned |
| [Underwater](https://oldschool.runescape.wiki/w/Underwater_Agility_and_Thieving) chests/clams | — | 4.5 XP | — | — | absent, Fossil Island-owned |
| Toy mouse | — | 3 XP per wind/catch | — | — | absent |
| Quest rewards | — | [The Tourist Trap](https://oldschool.runescape.wiki/w/The_Tourist_Trap), [Recruitment Drive](https://oldschool.runescape.wiki/w/Recruitment_Drive), [The Grand Tree](https://oldschool.runescape.wiki/w/The_Grand_Tree), [The Depths of Despair](https://oldschool.runescape.wiki/w/The_Depths_of_Despair) — 19,700 XP between them, enough for level 1→26 | — | — | verify each quest's reward block pays Agility |

---

## 4. Items, currencies and requirement gear

### 4.1 Graceful and marks

[Graceful outfit](https://oldschool.runescape.wiki/w/Graceful_outfit) — bought
from **Grace** (`rooftops_grace`, present in cache, no script) in the Rogues'
Den for marks of grace (`grace`):

| piece | marks | weight | energy restore |
|---|---|---|---|
| `graceful_hood` | 35 | −3 kg | +3 % |
| `graceful_top` | 55 | −5 kg | +4 % |
| `graceful_legs` | 60 | −6 kg | +4 % |
| `graceful_gloves` | 30 | −3 kg | +3 % |
| `graceful_boots` | 40 | −4 kg | +3 % |
| `graceful_cape` | 40 | −4 kg | +3 % |
| **full set** | **260** | −25 kg | 20 % + 10 % set bonus = **30 %** |

Recolours (all present in `all.obj` as `graceful_*_<variant>` plus `_worn`
pairs): five Kourend house dyes at 90 marks each behind their own quest, Great
Kourend at 90 behind all five, `graceful_*_hallowed` (Sepulchre dark dye),
Brimhaven blue for 250 vouchers, Varlamore for 650 termites, plus the
Trailblazer/Adventurer league and speedrun variants that have no source in
this tree. Recolours are re-paid on every switch, not unlocked once.

Also from Grace: `pack_amylase` at 10 marks per pack of 100 `amylase`
crystals, the [stamina potion](https://oldschool.runescape.wiki/w/Stamina_potion)
ingredient.

### 4.2 Activity currencies and their shops

| Currency | Obj | Shop / spender | Notable prices |
|---|---|---|---|
| Mark of grace | `grace` | Grace, Rogues' Den | graceful 260, amylase pack 10 |
| Agility arena ticket | `agilityarena_ticket`, `agilityarena_ticket_new` | Jackie (`agilityarena_tickettrader`) | 345 XP each, 379.5 with Karamja gloves |
| Brimhaven voucher | `agilityarena_voucher` | Jackie | toadflax 3, snapdragon 10, amylase pack 60, pirate's hook 800, graceful recolour 250 |
| Hallowed mark | `hallowed_mark` (+ `_2/_3/_4/_5/_25` stack denominations) | Sepulchre reward chest | crystal shard 1, token 10, grapple/focus/symbol/hammer/sack 100 each, hallowed ring 250, dark dye 300, dark acorn 3,000 — **the Graceful page's "1,800 hallowed marks" for the dark set is the 6× dye cost; the dedicated shop page's 300/dye is authoritative** |
| Termite | `varlamore_wyrm_agility_termite` | Worm Tongue's Wares | teleport scroll 40, amylase pack 100, graceful crafting kit 650, calcified acorn 900 |
| Wilderness agility ticket | `wildy_agility_token` | dispenser | 200 XP each, +5 %/10 %/15 % at 11/51/101 redeemed at once |
| Agility cape | `skillcape_agility`, `_hood`, `_trimmed` | Cap'n Izzy No-Beard, 99,000 gp at 99 | acts as a graceful cape (−4 kg, +3 %); once per day restores energy to 100 % and gives a 1-minute stamina effect |
| Pet | `skillpetagility` | — | rolled per lap at `1/(B − level×25)`; static 1/35,000…1/2,000 per Sepulchre floor; Penguin course has the best base |

### 4.3 Gear and consumables the skill depends on

| Item | Why | In cache |
|---|---|---|
| Stamina potion (`1..4dosestamina`, extended `..2stamina`) | 70 % drain reduction, 2 min | ✅ (extended not drinkable yet — pre-existing gap noted in `ring_of_endurance.rs2`) |
| Energy / super energy potion | flat restore | ✅ |
| `summer_pie` | +5 Agility boost | ✅ (boost not wired) |
| Spicy stew (agility spice) | ±0–5 | ✅ |
| `ring_of_endurance` | 15 % drain reduction at ≥500 charges; doubles stamina dose | charges done, passive absent |
| Boots of lightness, spotted/spottier cape | weight reduction | verify names |
| Ring of charos(a) | Werewolf course + Skullball | ✅ |
| Ninja/Kruk greegree | Ape Atoll course | ✅ |
| Clockwork suit | Penguin course | ✅ |
| Crossbow + mith grapple | 15 grapple shortcuts + Shayzien advanced zipline | ✅ |
| Climbing boots, rope, long rope ×2, nose peg, light source, machete | individual shortcuts (§5.2) | ✅ |
| Karamja gloves | +10 % arena XP, elite = double tickets | ✅ |
| Waterskins / Desert amulet 4 / circlet of water | Agility Pyramid heat | ✅ |

### 4.4 Diary interactions

Falador easy (Goblin Village tight gap), Falador medium (Motherlode dark
tunnel), Falador hard (Heroes' Guild crevice), Desert hard (Pollnivneach
drying line 540→666), Fremennik hard (Rellekka pile of fish 475→615),
Kandarin hard (Seers' Camelot teleport loop), Ardougne elite (mark timer
3 min→2 min at 50 %), Karamja elite (arena double tickets), Karamja easy
(Cairn Isle rock slide), Wilderness medium (Deep Wilderness crevice — already
enforced), Desert elite (Kalphite Lair crevice), Lumbridge/Draynor (under-wall
tunnels).

---

## 5. Shortcuts

### 5.1 What the harvest actually contains

`tools/data/shortest_path/transports/agility_shortcuts.tsv` — the RuneLite
shortest-path dataset already vendored for [MAPLINKS.md](MAPLINKS.md) — holds
**572 tile rows**, which collapse to **183 distinct (level, obstacle, verb,
requirement) groups** once resolved through `all.loc.compack`. Cross-referenced
against every `[oploc<n>,…]` binding in the tree and the generated
`maplink_agility.dbrow`:

| | groups |
|---|---|
| course obstacles, already scripted | 40 |
| course obstacles, absent (Gnome Stronghold rock shortcut) | 1 |
| true shortcuts, hand-scripted | 14 |
| true shortcuts, level-check-and-teleport only (`maplink_agility` category) | 47 |
| true shortcuts, **entirely absent** | 81 |

The 81 absent groups are the headline number: **44 % of the game's Agility
shortcuts do nothing at all when clicked.**

### 5.2 Shortcut classes that need more than a level check

These are the ones that cannot be served by adding another `maplink_agility`
row, listed with what each actually needs.

1. **Grapple shortcuts (15 tile rows, 6 groups)** — Falador wall (11/19/37),
   Catherby rocks (32/35/35), Water Obelisk crossbow tree (36/39/22), Yanille
   wall (39/21/38), Karamja strong tree (53/42/21), the Lum broken raft
   (8/37/19), and the Observatory rope/door (23/28/24 behind
   [Observatory Quest](https://oldschool.runescape.wiki/w/Observatory_Quest)).
   Three simultaneous skill gates, a **worn** crossbow plus mith grapple, a
   distinct fire-and-swing animation with a projectile, and several are
   one-way. `parse_agility_level` deliberately rejects every one of them, so
   none exist in tree. 2009scape's `shortcuts/grapple/` (8 files, including
   `AbstractOneWayGrapple`/`AbstractTwoWayGrapple` and `WallGrappleInterface`)
   is the reference.
2. **Monkeybars** (`monkeybars_end1` at 15, `monkeybars_end2` at 57,
   Dorgesh-Kaan, Ape Atoll, Pollnivneach) — mount / loop / dismount seq triple
   with `monkeybars_on`/`_loop`/`_off` sounds (SKILLING_SOUNDS §4.12), a
   multi-tile `p_exactmove`, and a fall on failure.
3. **Ziplines / teeth-grip** (`rooftops_kharid_slide_side` 20,
   `agility_shortcut_icon` 74 on Fossil Island, Werewolf deathslide,
   Colossal Wyrm finale, Shayzien advanced) — long exactmove with a held
   animation; the Werewolf one is the only failable obstacle on its course and
   deals a flat 30+ damage that does **not** scale with hitpoints.
4. **Stepping-stone chains** — Lumbridge Swamp (3 XP success / 1 XP fail),
   Champions' Guild, Karamja waterfall, Wilderness course (fail only on the
   third stone, `floor(HP×0.2)+1` damage). Each stone is its own loc and its
   own hop; the chain is not one teleport.
5. **Pipes with occupancy timers** — the Gnome pattern
   (`%gnome_obstacle_pipe_used = map_clock + 12`) generalises to Yanille
   (`yanille_obstacle_pipe_used`, declared, unused), Brimhaven
   (`karam_dungeon_pipe_used`/`2`, declared, unused), Edgeville, Taverley.
6. **Spikey chain, Slayer Tower** (`slayertower_sc_chainbottom`/`_chaintop`) —
   two distinct level tiers on the *same* locs (61 and 71), damage on the
   climb, and a nose-peg/slayer-helmet requirement on the abyssal-demon side.
7. **Strange floor** (Fremennik Slayer Dungeon 43, Forthos 63, Taverley 80) —
   awards XP twice (`5 × 2`), damages on failure.
8. **Dense forest, Tirannwn** (52 tile rows across 5 locs, level 56) — the
   single largest group in the harvest; a Regicide-gated multi-entrance mesh.
9. **Varbit-gated shortcuts** — Burthorpe tight gap (4462), Corsair pillar
   (6076), Heroes' Guild crevice (4464), Darkmeyer walls (10449/10450),
   Fenkenstrain bridge (4441+5023), the two hot/cold clue rocks keyed on 7255,
   the Lost Tribe hole (532>3). Each needs its gating quest/diary state to
   exist before the shortcut can be honest.
10. **Item-gated shortcuts** — climbing boots (Troll rocks 15), rope
    (Mountain Daughter boulder 10), 2× long rope (Darkmeyer wall 63), 1× long
    rope (Viyeldi 91), machete-bypass vine (Kharazi 79, Legends' Quest),
    light source (Dorgesh-Kaan).
11. **One-way shortcuts** — Yanille climbing rocks (5, out only), Brimhaven
    pipe (22), crossbow tree (36), dense essence boulder (49), Chasm of Fire
    gap (73) and chain (83), Trollheim wilderness rocks (64). The direction
    check must come from the placed loc's own coord, not from a symmetric
    dbrow pair.
12. **Cross-skill shortcuts** — Wintertodt gap (60 Agility + 50 Firemaking),
    Mausoleum bridge (69 Agility + 59 Construction to build), Meiyerditch
    advanced cave (93 Agility + 78 Mining), Rogues' Den maze (50 + 50
    Thieving).
13. **Stiles** — mid-era LostCity content
    (`shortcuts.rs2`), not in the OSRS harvest at all; keep deferred and say
    so, rather than inventing placements.

### 5.3 The full harvested inventory

Generated from `agility_shortcuts.tsv` × `all.loc.compack` × an `[oploc*]`
scan of the whole content tree. "Kind" separates lap-course obstacles from
true shortcuts; "Repo status" is *scripted* (a hand-authored trigger exists),
*maplink row* (level check + teleport only), or **absent** (clicking it does
nothing).

<!-- generated: tools/data/shortest_path/transports/agility_shortcuts.tsv x configs/all.loc.compack x [oploc*] scan -->
<!-- summary: {('course', 'scripted'): 40, ('shortcut', 'maplink row'): 47, ('shortcut', 'scripted'): 14, ('shortcut', '**absent**'): 81, ('course', '**absent**'): 1} -->
| Lvl | Obstacle (wiki menu target) | Verb | Cache loc name(s) | Extra requirement | Kind | Repo status |
|---|---|---|---|---|---|---|
| 1 | Balancing rope | Walk-on | `balancing_rope` | — | course | scripted |
| 1 | Crate | Climb-down | `rooftops_draynor_crate` | — | course | scripted |
| 1 | Gap | Jump-up | `rooftops_draynor_leapdown` | — | course | scripted |
| 1 | Leaves | Jump | `regicide_pitfall_side` | — | shortcut | maplink row |
| 1 | Log Balance | Walk-across | `karam_dungeon_bamboo_logbalance1`, `karam_dungeon_bamboo_logbalance3` | — | shortcut | scripted |
| 1 | Log balance | Walk-across | `gnome_log_balance1` | — | course | scripted |
| 1 | Narrow wall | Balance | `rooftops_draynor_wallcrossing` | — | course | scripted |
| 1 | Obstacle net | Climb-over | `obstical_net2` | — | course | scripted |
| 1 | Obstacle pipe | Squeeze-through | `obstical_pipe3_2` | — | course | scripted |
| 1 | Pipe | Squeeze-through | `karam_dungeon_pipe`, `karam_dungeon_pipe2` | — | shortcut | scripted |
| 1 | Rock | Climb | `hosidiusquest_rock_snake` | — | shortcut | **absent** |
| 1 | Rough wall | Climb | `rooftops_draynor_wallclimb` | — | course | scripted |
| 1 | Stepping stone | Jump-across | `swamp_cave_steppingstone_a`, `swamp_cave_steppingstone_b` | — | shortcut | maplink row |
| 1 | Stepping stone | Jump-from | `karam_dungeon_stone1`, `karam_dungeon_stone2` | — | shortcut | scripted |
| 1 | Sticks | Pass | `regicide_trap_woodspring` | — | shortcut | maplink row |
| 1 | Tightrope | Cross | `rooftops_draynor_tightrope_1`, `rooftops_draynor_tightrope_2` | — | course | scripted |
| 1 | Tree branch | Climb | `climbing_branch` | — | course | scripted |
| 1 | Tree branch | Climb-down | `climbing_tree` | — | course | scripted |
| 1 | Tripwire | Step-over | `regicide_trap_tripwire` | — | shortcut | maplink row |
| 1 | Wall | Jump-up | `rooftops_draynor_wallscramble` | — | course | scripted |
| 4 | Wall | Jump | `xbows_fai_falador_castle_walls_battlement`, `xbows_fai_falador_castle_walls_crenalation`, `xbows_yanille_castlewall_battlement` | — | shortcut | maplink row |
| 5 | Climbing rocks | Climb-up | `watchshortcut` | — | shortcut | **absent** |
| 5 | Crumbling wall | Climb-over | `fai_falador_castle_crumble_mid` | — | shortcut | scripted |
| 8 | Broken Raft | Grapple | `xbows_raft_br` | 8 Agility + 37 Ranged + 19 Strength, Crossbow + Mith Grapple | shortcut | **absent** |
| 10 | Rocks | Climb | `ds2_corsair_cove_shortcut` | — | shortcut | **absent** |
| 10 | Rope -> Boulder | Use | `mdaughter_cliff_boulder` | Rope, varbit 260>0 | shortcut | **absent** |
| 11 | Wall | Grapple | `xbows_fai_falador_castle_arches_hillskew`, `xbows_fai_falador_castle_walls_hillskew` | 11 Agility + 19 Ranged + 37 Strength, Crossbow + Mith Grapple | shortcut | **absent** |
| 13 | Fence | Jump-over | `lumbridge_sc_fencejump` | — | shortcut | maplink row |
| 13 | Hole | Squeeze-through | `lost_tribe_cavewall_hole_walldecor` | varbit 532>3 | shortcut | scripted |
| 14 | Tight-gap | Manoeuvre-past | `burthorpe_diary_shortcut` | varbit 4462=1 | shortcut | maplink row |
| 15 | Pillar | Jump-to | `ds2_ogre_corsair_dungeon_shortcut` | varbit 6076=1 | shortcut | maplink row |
| 15 | Rocks | Climb | `troll_climbingrocks` | Climbing Boots | shortcut | **absent** |
| 15 | Rocks | Climb | `zqclimbingrocks` | — | shortcut | **absent** |
| 15 | across Monkeybars | Swing | `monkeybars_end1` | — | shortcut | **absent** |
| 16 | Castle wall | Climb-under | `yanille_castlewall_sc` | — | shortcut | maplink row |
| 16 | Hole | Climb-into | `yanille_castlehole_sc` | — | shortcut | maplink row |
| 17 | Crack | Squeeze-through | `zeah_cata_crack` | — | shortcut | scripted |
| 18 | Broken window | Climb-through | `dugupsoil_slayer_2`, `slayertower_window_shortcut_through` | — | shortcut | maplink row |
| 18 | Crevice | Enter | `hosidiusquest_crackin`, `hosidiusquest_crackout` | — | shortcut | scripted |
| 18 | Rock | Climb | `gargboss_healsphere_med` | — | shortcut | **absent** |
| 18 | Stepping stone | Cross | `hosidiusquest_stone` | — | shortcut | scripted |
| 18 | Trellis | Climb-up | `qip_watchtower_trellis_base` | — | shortcut | **absent** |
| 20 | Cable | Swing-across | `rooftops_kharid_rope_swing` | — | course | scripted |
| 20 | Gap | Jump | `rooftops_kharid_leapdown` | — | course | scripted |
| 20 | Log balance | Walk-across | `mine_log_balance1` | — | shortcut | **absent** |
| 20 | Roof top beams | Climb | `rooftops_kharid_wallclimb_2` | — | course | scripted |
| 20 | Rough wall | Climb | `rooftops_kharid_wallclimb` | — | course | scripted |
| 20 | Tightrope | Cross | `rooftops_kharid_tightrope_1`, `rooftops_kharid_tightrope_4` | — | course | scripted |
| 20 | Tropical tree | Swing-across | `rooftops_kharid_bamboo_tree_top` | — | course | scripted |
| 20 | Zip line | Teeth-grip | `rooftops_kharid_slide_side` | — | course | scripted |
| 21 | Underwall tunnel | Climb-into | `varrock_sc_tunnel_east`, `varrock_sc_tunnel_west` | — | shortcut | scripted |
| 23 | Door | open | `id:255277` | 23 Agility + 28 Strength + 24 Ranged, Observatory Quest | shortcut | **absent** |
| 23 | Rocks | Grapple | `xbows_rock_grappled` | 23 Agility + 24 Ranged + 28 Strength, varbit 5810=1 | shortcut | **absent** |
| 23 | Rope | Climb | `xbows_rope_diagonal_obs` | 23 Agility + 28 Strength + 24 Ranged, Observatory Quest | shortcut | **absent** |
| 24 | Broken wall | Climb-over | `av_lowwall_climb_1`, `av_lowwall_climb_2` | — | shortcut | maplink row |
| 25 | Rocks | Climb | `ep_climbing_rocks01` | — | shortcut | **absent** |
| 26 | Underwall tunnel | Climb-into | `falador_sc_castlewall_north`, `falador_sc_castlewall_south` | — | shortcut | maplink row |
| 28 | Stone | Jump-to | `zeah_cata_stepstone` | — | shortcut | scripted |
| 29 | Rocks | Climb | `mount_karuulm_shortcut_rocks_low` | — | shortcut | **absent** |
| 30 | Clothes line | Cross | `rooftops_varrock_clothesline` | — | course | scripted |
| 30 | Edge | Jump-off | `rooftops_varrock_finish` | — | course | scripted |
| 30 | Gap | Leap | `rooftops_varrock_leapdown`, `rooftops_varrock_leaptobalcony`, `rooftops_varrock_leaptoruins` … | — | course | scripted |
| 30 | Ledge | Hurdle | `rooftops_varrock_stepuproof` | — | course | scripted |
| 30 | Rocks | Climb-down | `ds2_corsair_shortcut_bottom`, `ds2_corsair_shortcut_top` | — | shortcut | **absent** |
| 30 | Rough wall | Climb | `rooftops_varrock_wallclimb` | — | course | scripted |
| 30 | Stepping stones | Cross | `zqrockjump1`, `zqrockjump2`, `zqrockjump3` | — | shortcut | maplink row |
| 30 | Wall | Balance | `rooftops_varrock_wallswing` | — | course | scripted |
| 31 | Stepping stone | Jump-onto | `lumbridge_sc_stepstone` | — | shortcut | maplink row |
| 32 | Rocks | Grapple | `xbows_rock_hilltop_basic` | 32 Agility + 35 Ranged + 35 Strength, Crossbow + Mith Grapple | shortcut | **absent** |
| 32 | Stepping stone | Cross | `shilo_river_steppingstone` | — | shortcut | maplink row |
| 33 | Log balance | Walk-across | `ardougne_log_balance_left_sc`, `ardougne_log_balance_right_sc` | — | shortcut | **absent** |
| 33 | Tunnel | Climb-into | `av_tunnel_1` | — | shortcut | maplink row |
| 35 | Obstacle pipe | Squeeze-through | `agility_obstical_pipe_barbarian` | — | shortcut | maplink row |
| 35 | Trellis | Climb | `garden_trellis_concave_shortcut` | Garden of Tranquillity | shortcut | **absent** |
| 36 | Crossbow Tree | Grapple | `xbows_beach_to_island_tree_basic` | 36 Agility + 39 Ranged + 22 Strength, Crossbow + Mith Grapple | shortcut | **absent** |
| 36 | Stepping stone | Cross | `av_stepstone_1` | — | shortcut | maplink row |
| 37 | Rocks | Climb | `gnome_stronghold_sc_rock_bottom`, `gnome_stronghold_sc_rock_top` | — | course | **absent** |
| 38 | Rocks | Climb | `alkharid_mine_sc_bottom`, `alkharid_mine_sc_top` | — | shortcut | **absent** |
| 39 | Wall | Grapple | `xbows_yanille_castlewall` | 39 Agility + 21 Ranged + 38 Strength, Crossbow + Mith Grapple | shortcut | **absent** |
| 40 | Balancing ledge | Walk-across | `balancing_ledge3` | — | course | scripted |
| 40 | Gap | Jump | `rooftops_canifis_jump`, `rooftops_canifis_jump_2`, `rooftops_canifis_jump_3` … | — | course | scripted |
| 40 | Log balance | Walk-across | `tlati_north_river_log_balance_1` | — | shortcut | **absent** |
| 40 | Obstacle pipe | Squeeze-through | `obstical_pipe4` | — | course | scripted |
| 40 | Pole-vault | Vault | `rooftops_canifis_polevault` | — | course | scripted |
| 40 | Stepping stone | Cross | `zeah_lake_shortcut_hosidius`, `zeah_lake_shortcut_shayzien` | — | shortcut | maplink row |
| 40 | Tall tree | Climb | `rooftops_canifis_start_tree` | — | course | scripted |
| 41 | Rocks | Climb | `asc_troll_mountain_climbrock_1`, `av_scramble_1` | — | shortcut | **absent** |
| 42 | Crevice | Squeeze-through | `dwarf_mines_sc_wall_crack` | — | shortcut | maplink row |
| 42 | Underwall tunnel | Climb-into | `draynor_diary_under_wall_e`, `draynor_diary_under_wall_w` | — | shortcut | maplink row |
| 43 | Rock | Climb | `tlati_tree_area_shortcut_top` | — | shortcut | **absent** |
| 43 | Rocks | Climb | `asc_troll_mountain_climbrock_2`, `troll_mountain_shortcut_climbingrocks1`, `troll_mountain_shortcut_climbingrocks2` | — | shortcut | **absent** |
| 44 | Rocks | Climb | `asc_troll_mountain_climbrock_3` | — | shortcut | **absent** |
| 45 | Log balance | Walk-across | `av_balance_1`, `av_balance_2` | — | shortcut | **absent** |
| 45 | Log balance | Cross | `regicide_logbalance1_start`, `regicide_logbalance2_start`, `regicide_logbalance3_start` | — | shortcut | **absent** |
| 45 | Rock | Climb | `proudspire_climbing_rocks_dark01_op` | — | shortcut | **absent** |
| 45 | Stepping stone | Cross | `zeah_saltpetre_shortcut` | — | shortcut | maplink row |
| 46 | Crevice | Use | `agility_shortcut_icon` | varbit 6312=1 | shortcut | **absent** |
| 46 | Jutting wall | Squeeze-past | `fairy_sc_juttingwall` | — | shortcut | maplink row |
| 47 | Rocks | Climb | `asc_troll_mountain_climbrock_4`, `ralos_rise_sc` | — | shortcut | **absent** |
| 47 | Stepping stone | Cross | `snakeboss_crate` | — | shortcut | **absent** |
| 48 | Log balance | Walk-across | `slayer_river_sc_logbalance_1`, `slayer_river_sc_logbalance_3` | — | shortcut | **absent** |
| 49 | Boulder | Jump | `archeuus_runestone_shortcut_boulder` | — | shortcut | **absent** |
| 50 | Edge | Jump | `rooftops_falador_edge` | — | course | scripted |
| 50 | Gap | Jump | `rooftops_falador_gap_1`, `rooftops_falador_gap_2`, `rooftops_falador_gap_3` | — | course | scripted |
| 50 | Hand holds | Cross | `rooftops_falador_handholds_start` | — | course | scripted |
| 50 | Jagged wall | Jump-over | `crumbled_wall` | — | shortcut | maplink row |
| 50 | Ledge | Jump | `rooftops_falador_ledge_1`, `rooftops_falador_ledge_2`, `rooftops_falador_ledge_3a` … | — | course | scripted |
| 50 | Rocks | Climb | `great_conch_cliff_shortcut_town_bottom`, `great_conch_cliff_shortcut_town_top` | — | shortcut | **absent** |
| 50 | Rough wall | Climb | `rooftops_falador_wallclimb` | — | course | scripted |
| 50 | Stepping stone | Cross | `fairy_island_nature_grotto_shortcut` | — | shortcut | maplink row |
| 50 | Tightrope | Cross | `rooftops_falador_tightrope_1`, `rooftops_falador_tightrope_2`, `rooftops_falador_tightrope_3` | — | course | scripted |
| 51 | Obstacle pipe | Squeeze-through | `varrock_dungeon_pipe_sc` | — | shortcut | maplink row |
| 52 | Door | Open | `balancegate52a` | — | shortcut | **absent** |
| 52 | Rocks | Climb | `archeuus_runestone_shortcut_midgrey_bottom` | — | shortcut | **absent** |
| 53 | Strong Tree | Grapple | `xbows_jungletree_karamja_basic` | 53 Agility + 42 Ranged + 21 Strength, Crossbow + Mith Grapple | shortcut | **absent** |
| 54 | Rock | Climb | `aldarin_cliff_shortcut_bottom` | — | shortcut | **absent** |
| 55 | Stepping stone | Cross | `misc_diary_steppingstone` | — | shortcut | maplink row |
| 56 | Dense forest | Enter | `regicide_cross_over1`, `regicide_cross_over1_tyras_camp`, `regicide_cross_over2` … | — | shortcut | maplink row |
| 56 | Stepping stone | Cross | `karamja_dungeon_stepping_stone_end` | — | shortcut | maplink row |
| 57 | Broken Fence | Jump | `viking_pike_defence_broken` | — | shortcut | maplink row |
| 57 | across Monkeybars | Swing | `monkeybars_end2` | — | shortcut | **absent** |
| 58 | Weathered wall | Jump-down | `ectopool_sc_raildown` | — | shortcut | maplink row |
| 58 | Weathered wall | Jump-up | `ectopool_sc_wallclimb` | — | shortcut | maplink row |
| 59 | Rocks | Climb | `elves_overpass_sc_rocks_bottom`, `elves_overpass_sc_rocks_top` | — | shortcut | **absent** |
| 60 | Edge | Jump | `rooftops_seers_leapdown` | — | course | scripted |
| 60 | Gap | Jump | `rooftops_seers_jump`, `rooftops_seers_jump_1`, `rooftops_seers_jump_2` | — | course | scripted |
| 60 | Rocky handholds | Climb | `godwars_climbing_rocks_down`, `godwars_climbing_rocks_up` | — | shortcut | **absent** |
| 60 | Stepping stone | Jump-to | `mos_les_stepping_stone` | — | shortcut | **absent** |
| 60 | Tightrope | Cross | `rooftops_seers_tightrope` | — | course | scripted |
| 60 | Tunnel | Enter | `cavewall_shortcut_royal_titans_east` | — | shortcut | **absent** |
| 60 | Wall | Climb-up | `rooftops_seers_wallclimb` | — | course | scripted |
| 61 | Crevice | Squeeze-through | `slayer_dungeon_2_sc_wall_crack` | — | shortcut | maplink row |
| 61 | Spikey chain | Climb-up | `slayertower_sc_chainbottom` | — | shortcut | **absent** |
| 61 | Spikey chain | Climb-down | `slayertower_sc_chaintop` | — | shortcut | **absent** |
| 62 | Rocks | Climb | `mount_karuulm_shortcut_rocks` | — | shortcut | **absent** |
| 62 | Stepping stone | Cross | `necropolis_stepping_stone_1`, `necropolis_stepping_stone_2` | — | shortcut | maplink row |
| 63 | Loose Railing | Squeeze-through | `deepdungeonlooserailing` | — | shortcut | maplink row |
| 63 | Strange floor | Jump-over | `hosdun_agility_shortcut`, `priestperil_tomb_cornerl` | — | shortcut | maplink row |
| 63 | Wall | Climb | `darkm_outer_wall_2h_shortcut` | varbit 10449=1 | shortcut | **absent** |
| 63 | Wall | Climb | `darkm_outer_wall_3h_shortcut` | varbit 10450=1 | shortcut | **absent** |
| 64 | Down Rope anchor | Climb | `fossil_volcano_agility_rope_top` | — | shortcut | **absent** |
| 64 | Rocks | Climb | `trollheim_wildy_climb_rocks` | — | shortcut | **absent** |
| 64 | Up Rope anchor | Climb | `fossil_volcano_agility_rope_bottom` | — | shortcut | **absent** |
| 65 | Ornate railing | Squeeze-through | `morytania_railing_sc_fence_1`, `morytania_railing_sc_fence_2` | — | shortcut | maplink row |
| 65 | Rocks | Climb | `morytania_climbingrocks_sc_bottom`, `morytania_climbingrocks_sc_top` | — | shortcut | **absent** |
| 66 | Climbing rocks | Climb-up | `tavelryshortcut` | — | shortcut | **absent** |
| 66 | Jutting wall | Squeeze-past | `fairy_sc_juttingwall` | — | shortcut | maplink row |
| 66 | Stepping stone | Jump-to | `lumbridge_diary_desert_shortcut` | — | shortcut | maplink row |
| 67 | Crevice | Use | `heroes_guild_shortcut_from_fountain`, `heroes_guild_shortcut_to_fountain` | varbit 4464=1 | shortcut | maplink row |
| 67 | Pile of rubble | Climb-down | `climbingcaverocks2` | — | shortcut | scripted |
| 67 | Pile of rubble | Climb-up | `climbingcaverocks1` | — | shortcut | scripted |
| 68 | Rocks | Climb | `elves_overpass_sc_rocks_bottom`, `elves_overpass_sc_rocks_top`, `ice_mountain_shortcut_bottom` … | — | shortcut | **absent** |
| 69 | Bridge | Cross | `fenk_bridge_multi_north`, `fenk_bridge_multi_north_mirror`, `fenk_bridge_multi_south` … | varbit 4441=2;5023=2 | shortcut | **absent** |
| 69 | Inconspicuous rocks (master) | Jump-over | `hh_master005` | varbit 7255=138 | shortcut | **absent** |
| 69 | Rocks | Climb | `archeuus_runestone_shortcut_grey_shortcut_north` | — | shortcut | **absent** |
| 69 | Rocks | Jump-over | `darkm_wall_rock_shortcut` | varbit 7255=138 | shortcut | **absent** |
| 70 | Big window | Enter | `kharid_bigwindow` | — | shortcut | maplink row |
| 70 | Broken wall | Climb | `kharid_poshwall_topless` | — | shortcut | **absent** |
| 70 | Obstacle pipe | Squeeze-through | `taverly_dungeon_pipe_sc` | — | shortcut | maplink row |
| 70 | Rocks | Jump-up | `taverley_dragon_jumpup` | — | shortcut | **absent** |
| 70 | Rocks | Jump-down | `taverley_dragon_jumpdown` | — | shortcut | **absent** |
| 70 | through Hole | Climb | `fossil_shortcut_basecamp_a`, `fossil_shortcut_basecamp_b` | — | shortcut | **absent** |
| 71 | Rock | Climb | `proudspire_climbing_rocks_grey03_op` | — | shortcut | **absent** |
| 71 | Spikey chain | Climb-up | `slayertower_sc_chainbottom` | — | shortcut | **absent** |
| 71 | Spikey chain | Climb-down | `slayertower_sc_chaintop` | — | shortcut | **absent** |
| 71 | Stepping stone | Cross | `polli_stepping_stone` | — | shortcut | maplink row |
| 72 | Stepping stone | Cross | `wilderness_chaos_temple_shortcut` | — | shortcut | **absent** |
| 72 | Tunnel | Enter | `cavewall_shortcut_wyvern_north`, `slayer_cave_tunnel_right` | — | shortcut | maplink row |
| 73 | Rocks | Climb | `archeuus_runestone_shortcut_grey_top`, `diary_troll_climbingrocks` | — | shortcut | **absent** |
| 74 | Stepping stone | Cross | `wilderness_lava_dragons_shortcut` | — | shortcut | scripted |
| 74 | Zip line | Teeth-grip | `agility_shortcut_icon` | — | shortcut | **absent** |
| 76 | Stepping stone | Cross | `snakeboss_steppingstone` | — | shortcut | **absent** |
| 79 | Rocks | Climb | `shortcut_shilo_rocks_bottom`, `shortcut_shilo_rocks_top` | — | shortcut | **absent** |
| 79 | Vine | Climb | `kharazi_shortcut_vine_diag1_live`, `kharazi_shortcut_vine_end_live` | — | shortcut | **absent** |
| 80 | Strange floor | Jump-over | `taverly_dungeon_floor_spikes_sc` | — | shortcut | maplink row |
| 81 | Broken window | Climb-through | `dugupsoil_slayer_2` | — | shortcut | **absent** |
| 81 | Ivy | Climb-up | `slayer_corpse1` | — | shortcut | **absent** |
| 82 | Stepping stone | Cross | `wilderness_lava_maze_northern_shortcut` | — | shortcut | scripted |
| 82 | Tunnel | Enter | `cavewall_shortcut_wyvern_west` | — | shortcut | **absent** |
| 84 | Rocks | Climb | `crandor_shortcut_bottom`, `crandor_shortcut_top` | — | shortcut | **absent** |
| 85 | Rocks | Climb | `dagannoth_waterbirth_rock_climb_agility_shortcut_bottom`, `dagannoth_waterbirth_rock_climb_agility_shortcut_top`, `elves_overpass_sc_rocks_bottom` … | — | shortcut | **absent** |
| 86 | Crevice | Squeeze-through | `kalphite_wall_shortcut` | — | shortcut | maplink row |
| 96 | Crevice | Squeeze-through | `legends_quest_cave_shortcut` | Legends' Quest | shortcut | **absent** |

---

## 6. Cross-cutting systems to build once

### 6.1 Lap state

Generalise `~update_gnome_varp`. One `scope=temp` varp per course holding an
obstacle-index counter, advanced only when the obstacle being cleared is the
next one (`completed <= progress + 1`, the Gnome rule, which correctly allows
a repeat of the current obstacle but not a skip), reset to 0 on the lap bonus
and on leaving the course zone. Courses whose route branches (Prifddinas
portals, Colossal Wyrm basic vs advanced, Shayzien basic vs advanced) need a
bitmask rather than a counter, because the advanced fork rejoins the shared
finale.

The lap boundary is the hook for: the completion XP, the mark of grace roll,
the pet roll, the Werewolf stick, the Wilderness ticket, the Prifddinas
crystal shard, and the Colossal Wyrm termite scoop. Nothing else should ever
pay them.

### 6.2 Success/failure model

One proc, `~agility_success(int $level_req, int $level_stop)`, returning a
boolean from a linear interpolation between the obstacle's base success rate
at its requirement and 100 % at its stop level — the shape the
[Canifis](https://oldschool.runescape.wiki/w/Canifis_Rooftop_Course) page
publishes explicitly (88.28 % at 40 → 100 % at 64). `stat_random` is the wrong
primitive here: it is a `(low, high)` roll against level, not a published
success curve, and using it would silently invent rates.

Failure then routes through the existing `~agility_delay_fail` with the
course's own fall coord and damage rule. Three damage rules exist and must not
be conflated:

- **flat small** — rooftops, 1–5 or 2–8 per the obstacle's page;
- **HP-proportional** — Wilderness course: `floor(HP × 0.15) + 1` (ropeswing,
  log balance) and `floor(HP × 0.2) + 1` (third stepping stone);
- **flat large, non-scaling** — Werewolf deathslide, 30+.

### 6.3 Marks of grace, rebuilt

Per §1.3. Concretely: a `%agility_mark_cooldown` timestamp varp, a per-course
`markspawn` dbtable of spawn coords (5–8 rows per course), a rate column
(2/6, 2/3, 2/5), the ≥req+20 80 % reduction with the documented Canifis
exemption, a 10-minute despawn, and **per-player** ground placement. Verify
first whether this tree's `obj_add` is already owner-scoped; if it is not, the
mark is visible to everyone and that is an engine gap, not a content one
(§8.3).

### 6.4 Obstacle data, not obstacle code

`agility.param` already declares the right schema. The 24 courses will
otherwise become ~4,000 lines of near-identical trigger bodies. Recommended
shape, mirroring what `maplink_agility` already does for shortcuts:

```
[agility_obstacle]                 // dbtable
column=loc,loc,INDEXED,REQUIRED
column=course,int,REQUIRED         // course id
column=index,int,REQUIRED          // position in the lap
column=level,int,REQUIRED
column=xp,int,REQUIRED             // tenths
column=stop_level,int              // fails below this
column=base_success,int            // percent×100 at level==level
column=dest,coord,REQUIRED
column=fail_coord,coord
column=damage_rule,int
column=seq,seq
```

with one generic `[oploc1,_agility_obstacle]` category trigger doing
level-check → arrive-delay → success roll → anim+sound (via the existing
`~agility_sound_for_seq`) → move → XP → lap advance. Obstacles with genuinely
bespoke behaviour (pipes with occupancy, the Penguin ice slide, the Sepulchre
statues, grapples) keep hand-authored triggers; the table carries the ~150
that do not. `pack/category.pack` needs one hand-allocated id, the same way
`8206`/`8207` were allocated for the maplink categories.

### 6.5 Animations and sounds

[SKILLING_SOUNDS.md](SKILLING_SOUNDS.md) §4.12 already maps every obstacle
kind to its sound, including the layer-W rooftop rows, and notes the job size
as 113 `anim(` call sites. New courses must extend `~agility_sound_for_seq`
rather than sounding per call site, so a new obstacle of a known kind is
sounded for free. Seq names to resolve per course from `all.seq` — the known
families are `human_walk_logbalance{,_ready,_stumble}`,
`human_walk_sidestep{,l}`, `human_climbing{,_down,_ready}`,
`human_doublepipesqueeze`, `human_longcrawl`, `human_reachforladder`,
`human_spot_jump`, `agility_shortcut_wall_jump{,2}`,
`agility_shortcut_wall_jumpdown{,2}`, `agilty_shortcut_{enter,exit}_hole`,
`agilty_shortcut_tunnel_walk`, plus monkeybar, zipline and grapple families
still to be identified.

### 6.6 Course NPCs and barks

`gnometrainer` already barks per obstacle. The same treatment is due for
`gunnjorn` (Barbarian), the Agility trainer who takes the Werewolf stick,
Turgall (Dorgesh-Kaan delivery), `agilityarena_clerk` / `agilityarena_tickettrader`
(Brimhaven), `rooftops_grace` (the shop), Worm Tongue (termites), and the
Sepulchre's Zul-Cheray.

---

## 7. Implementation slices

Each slice is independently landable and independently verifiable. Slices
A1–A5 must precede the rest; the course slices after them are parallel.

| # | Slice | Depends on | Deliverable |
|---|---|---|---|
| **A1** | Lap state + success/failure model (§6.1, §6.2) | — | `~agility_lap_advance`, `~agility_success`, damage rules wired into `~agility_delay_fail`; the 8 rooftops start failing at their published levels |
| **A2** | Marks of grace rebuilt (§6.3) | A1 | cooldown, per-course rates and spawn tiles, level penalty + Canifis exemption, 10-min despawn, per-player visibility |
| **A3** | XP reconciliation | A1 | Al Kharid, Varrock, Falador (incl. its 2 missing obstacles), Ardougne, Gnome brought to the published per-obstacle values; Draynor's gate 10→1 |
| **A4** | Obstacle dbtable (§6.4) | A1 | `agility_obstacle` dbtable + generic category trigger + a category id in `pack/category.pack`; the 8 rooftops migrated onto it as the proof |
| **A5** | Run energy modifiers (§8.1) | — | **the era flag and both formula pairs have landed** (§1.6); what remains is graceful ×1.3 restore and ring-of-endurance ×0.85 drain, and a content-visible seam for them |
| **A6** | Pollnivneach rooftop | A4 | the ninth rooftop; 890/1,016 XP with the hard Desert diary variant |
| **A7** | Rellekka + Pollnivneach diary XP variants | A6 | Fremennik hard 780→920, Desert hard 890→1,016 |
| **A8** | Barbarian Outpost | A4 | LC port; 153.3 Agility **+ 41.3 Strength** per lap, fails until 93, `gunnjorn` barks |
| **A9** | Wilderness course | A4, A8 | LC port; gate at 52, obstacles at 49, HP-proportional damage, `wildy_agility_token` tiers, the 150k-coin dispenser deposit, the lap counter and its −10-on-logout rule |
| **A10** | Agility Pyramid | A4 | 2009scape port; 7 obstacles + climbing rocks + doorway (`300 + level×8`), the rolling-block NPC, desert heat, `pyramid_top` → Simon Templeton 10k |
| **A11** | Brimhaven Agility Arena | A4, A18 | LC `agilityarena_zones.rs2` port + the modern layer: 24 dispensers on a 1-minute rotation, the arrow, streak rule, `30 × floor(level/10)` capped 300, tickets + vouchers, Jackie's shop, Karamja-glove and elite-diary bonuses, Cap'n Izzy's cape sale |
| **A12** | Yanille dungeon + dungeon obstacle XP | A4 | balancing ledge 40, pipe 49, monkeybars 57 (the two declared-but-unused busy varps finally used); Brimhaven/Edgeville/Taverley pipe and log XP |
| **A13** | Shayzien basic + advanced | A4 | 153.5 and 508 XP/lap; the advanced zipline's crossbow+grapple gate |
| **A14** | Ape Atoll | A4 | greegree-worn gate, unboostable 48, 580 XP, unfailable at 75 |
| **A15** | Werewolf course + Skullball | A4 | ring-of-charos trapdoor, 5 obstacles, the 90-second stick and its 380 XP hand-in, the deathslide's flat 30 damage; Skullball as its own tap/kick/shoot state machine with the 750 XP-minus-8-per-3s curve |
| **A16** | Penguin course | A4 | Cold War gate + clockwork suit, the crusher NPC, the 4-segment icicles with "tread softly", the ice slide; never unfailable |
| **A17** | Dorgesh-Kaan | A4 | light-source gate, the cable/monkeybar route, the 6-part retrieval and Turgall's delivery bonus, the two return routes |
| **A18** | Graceful, Grace's shop, amylase | A2, A5 | `rooftops_grace` shop at the published mark prices, all recolour variants and their re-payment rule, `pack_amylase` → stamina potion path, the 30 % set bonus applied through A5's hook |
| **A19** | Colossal Wyrm basic + advanced | A4 | both routes, the shared zipline finale, termite scoops and blessed bone shards, Worm Tongue's shop, the Varlamore recolour |
| **A20** | Prifddinas | A4 | 12 obstacles, the 6 random portals (+82 XP and a crystal shard each), tightrope failures until 91 |
| **A21** | Hallowed Sepulchre | A4, A18 | 5 floors with their own level gates and XP, the carried-over timer, wizard/knight/crossbowman/priest statue hazards, strange tiles, coffins and their five skill challenges, hallowed marks and the full reward shop, the ring of endurance and strange old lockpick as drops |
| **A22** | Shortcuts: the 81 absent groups | A4 | by class per §5.2 — grapples, monkeybars, ziplines, stepping-stone chains, occupancy pipes, spikey chains, strange floors, dense forest, varbit- and item-gated, one-way |
| **A23** | Shortcut upgrade pass | A22 | give the 47 `maplink_agility` rows their animation, sound, XP and failure where the Wiki states one; drop them from the generated table as they graduate, exactly as the importer's collision scan already expects |
| **A24** | Cape, pet, skill guide, emote | A1 | `skillcape_agility` sale at 99 + its daily energy restore and graceful-cape equivalence, `skillpetagility` rolled per lap at `1/(B − level×25)` with per-course bases and the Sepulchre's static per-floor rates, the Agility skill-guide dbrows, the skillcape emote |
| **A25** | Adjacent XP sources | — | audit that The Tourist Trap / Recruitment Drive / The Grand Tree / The Depths of Despair pay their Agility rewards; Gnome Ball goals; Blast Furnace belt; Wintertodt pillar gap; underwater chests/clams |

Explicitly **out of scope**, with owners: Rogues' Den maze (Thieving), Yama's
Lair stones (that boss), Wintertodt pillars (Wintertodt), Blast Furnace belt
(Smithing), underwater agility (Fossil Island), Barbarian fishing (already
live in Fishing).

---

## 8. Engine-side work

### 8.1 Run energy modifiers (blocking A18, and the reason Agility matters)

The formulas themselves are done: `run_energy_tick` now calls
[mock230_runenergy.c](../src/net/mock/mock230_runenergy.c) through the era
flag (§1.6), and `player_weight_grams` already computes weight correctly from
cache opcode 75 (stackables excluded, negatives preserved), so a
weight-reducing set's negative total reaches the model and is clamped there.

What is left is the three multipliers, which need a content-visible seam:
worn-graceful detection and the stamina timer are content state, and
`ring_of_endurance.rs2`'s header already records that no drain-rate hook
exists. `SS_OP_RUNENERGY` (2100) and `SS_OP_HEALENERGY` (2026)
are implemented in `mock230_scripts.c`; a `%runenergy_drain_scale`-style
varp read by the tick, or an engine-side worn-item check, are the two options —
prefer the engine-side check for graceful (it is cache data) and a timer varp
for stamina.

### 8.2 Obstacle movement primitives

`p_exactmove`, `p_locmerge`, `p_telejump`, `p_teleport`, `p_arrivedelay` and
the `bas` family are all already exercised by the rooftops, which is most of
what the new courses need. Newly required: multi-segment exactmoves for
monkeybars and ziplines, and NPC-driven obstacles (the Penguin crusher and the
Pyramid rolling block are NPCs, not locs).

### 8.3 Per-player ground objects

Marks of grace, the Werewolf stick and the Wilderness tickets are all
owner-visible ground spawns with their own lifetimes. Verify what
`obj_add(coord, …, ^lootdrop_duration)` currently does about ownership before
A2; if the tree has no owner-scoped ground obj, that is the one genuine engine
prerequisite in this plan.

### 8.4 Timers and interfaces

The arena's 1-minute dispenser rotation and the Sepulchre's carried-over floor
timer both need a reliable server clock display. `agilityarena_overlay.if` and
`agilityarena_rewards.if` are already in the content pack; the Sepulchre's
timer interface must be located in `all.if`/`interfaces/` before A21 starts.

---

## 9. Verification

Per slice:

1. **Isolated clean-overlay RuneScript compile** — the count must not fall.
   The Hunter plan's baseline convention applies: record the script count
   before and after.
2. **Config/spawn loader clean** — new `.loc`/`.varp`/`.dbrow`/`.spawn` files
   load without error (the missing `==== NPC ====` header class of failure).
3. **`::agilityrun`** — a new debugproc in the `mock230 --selftest` family,
   modelled on `::hunterrun` and the fishing harness, asserting:
   - every course's per-obstacle XP against the published table, and its lap
     total;
   - lap ordering: skipping an obstacle must not pay the completion bonus;
   - the success curve at the requirement level, one level below the stop
     level, and at the stop level (must be 100 %);
   - mark of grace: cooldown honoured, rate per course, the ≥req+20 reduction,
     Canifis exempt, spawn tile is one of the published set;
   - shortcut level gates: one under, one at, for every gated shortcut;
   - grapple shortcuts reject a missing crossbow, a missing grapple, and each
     of the three skill gates independently;
   - the run-energy modifiers on top of `make -C src test-run-energy`'s
     per-model formula checks: graceful, stamina and the ring, each alone and
     combined, under both flag values.
4. **Headless client matrix** — for one obstacle of each animation family
   (log balance, sidestep, wall scramble, pipe, monkeybars, zipline, grapple,
   rope swing), confirm the anim plays, the sound fires and the player ends on
   the right tile. `docs/headless-*` recipes apply.

Per the repo's testing rule: prove each new assertion can fail by mutating the
implementation, not the constant — see
`serverscript-guard-testing-confounds` and
[VERIFY_BLOCKER](verify-blocker-and-failing-test.md)'s convention.

---

## 10. Open questions and blockers

1. **Owner-scoped ground objects** (§8.3) — must be settled before A2. Every
   mark of grace, stick and ticket depends on it.
2. **Quest gates.** `quests/` already contains Cold War, Monkey Madness I,
   Death to the Dorgeshuun, Creature of Fenkenstrain, Sins of the Father,
   Song of the Elves, Children of the Sun, Garden of Tranquillity, Legends'
   Quest, Regicide and My Arm's Big Adventure, so nearly every course gate has
   real state to read. **Observatory Quest and Shilo Village have no quest
   directory**, so their three shortcuts (23-level grapple rope/door, 79-level
   Shilo rocks) must gate closed and be listed rather than opened by default.
3. **Diary state.** Only `%wilderness_diary_medium_complete` is demonstrably
   used today. The Desert hard, Fremennik hard, Kandarin hard, Ardougne elite,
   Karamja elite and Falador easy/medium/hard interactions in §4.4 each need a
   diary varbit that exists; where one does not, implement the base value and
   record the deferred variant rather than guessing.
4. **Graceful recolour cost conflict** (§4.2) — the Graceful outfit page's
   1,800 hallowed marks versus the Sepulchre shop's 300 per dye. Take the
   dedicated shop page and record the conflict, per the Hunter plan's rule.
5. **Newest-content coverage.** The harvested shortcut set includes areas
   (Vampyrium, Wyrmscraig, The Great Conch, Grimstone, Deepfin Mine, Stalker
   Den, Ruins of Mokhaiotl) whose locs resolve in this cache but whose owning
   regions are not built in this tree. Their shortcuts should be authored only
   where the destination coord is reachable; otherwise list them as
   dependency-blocked.
6. **Ape Atoll's unboostable 48** — needs `stat_base`, not `stat`, and the
   greegree check must read the *worn* item, not the inventory.
7. **Extended stamina potions are not drinkable in this tree at all**
   (recorded in `ring_of_endurance.rs2`). A18 either fixes that or inherits
   the gap explicitly.

---

## 11. Reference index

Courses — [Gnome Stronghold](https://oldschool.runescape.wiki/w/Gnome_Stronghold_Agility_Course) ·
[Draynor](https://oldschool.runescape.wiki/w/Draynor_Village_Rooftop_Course) ·
[Al Kharid](https://oldschool.runescape.wiki/w/Al_Kharid_Rooftop_Course) ·
[Varrock](https://oldschool.runescape.wiki/w/Varrock_Rooftop_Course) ·
[Canifis](https://oldschool.runescape.wiki/w/Canifis_Rooftop_Course) ·
[Falador](https://oldschool.runescape.wiki/w/Falador_Rooftop_Course) ·
[Seers'](https://oldschool.runescape.wiki/w/Seers%27_Village_Rooftop_Course) ·
[Pollnivneach](https://oldschool.runescape.wiki/w/Pollnivneach_Rooftop_Course) ·
[Rellekka](https://oldschool.runescape.wiki/w/Rellekka_Rooftop_Course) ·
[Ardougne](https://oldschool.runescape.wiki/w/Ardougne_Rooftop_Course) ·
[Shayzien](https://oldschool.runescape.wiki/w/Shayzien_Agility_Course) ·
[Agility Pyramid](https://oldschool.runescape.wiki/w/Agility_Pyramid) ·
[Penguin](https://oldschool.runescape.wiki/w/Penguin_Agility_Course) ·
[Barbarian Outpost](https://oldschool.runescape.wiki/w/Barbarian_Outpost_Agility_Course) ·
[Ape Atoll](https://oldschool.runescape.wiki/w/Ape_Atoll_Agility_Course) ·
[Colossal Wyrm](https://oldschool.runescape.wiki/w/Colossal_Wyrm_Agility_Course) ·
[Wilderness](https://oldschool.runescape.wiki/w/Wilderness_Agility_Course) ·
[Hallowed Sepulchre](https://oldschool.runescape.wiki/w/Hallowed_Sepulchre) ·
[Werewolf](https://oldschool.runescape.wiki/w/Werewolf_Agility_Course) ·
[Dorgesh-Kaan](https://oldschool.runescape.wiki/w/Dorgesh-Kaan_Agility_Course) ·
[Prifddinas](https://oldschool.runescape.wiki/w/Prifddinas_Agility_Course) ·
[Yanille Agility Dungeon](https://oldschool.runescape.wiki/w/Yanille_Agility_Dungeon)

Activities — [Brimhaven Agility Arena](https://oldschool.runescape.wiki/w/Brimhaven_Agility_Arena) ·
[Werewolf Skullball](https://oldschool.runescape.wiki/w/Werewolf_Skullball) ·
[Gnome Ball](https://oldschool.runescape.wiki/w/Gnome_Ball) ·
[Rogues' Den](https://oldschool.runescape.wiki/w/Rogues%27_Den) ·
[Barbarian Fishing](https://oldschool.runescape.wiki/w/Barbarian_Fishing)

Items and mechanics — [Mark of grace](https://oldschool.runescape.wiki/w/Mark_of_grace) ·
[Graceful outfit](https://oldschool.runescape.wiki/w/Graceful_outfit) ·
[Amylase crystal](https://oldschool.runescape.wiki/w/Amylase_crystal) ·
[Stamina potion](https://oldschool.runescape.wiki/w/Stamina_potion) ·
[Ring of endurance](https://oldschool.runescape.wiki/w/Ring_of_endurance) ·
[Agility cape](https://oldschool.runescape.wiki/w/Agility_cape) ·
[Giant squirrel](https://oldschool.runescape.wiki/w/Giant_squirrel) ·
[Run energy](https://oldschool.runescape.wiki/w/Run_energy) ·
[Shortcuts](https://oldschool.runescape.wiki/w/Shortcuts) ·
[Agility/Experience table](https://oldschool.runescape.wiki/w/Agility/Experience_table)

In-repo — [MAPLINKS.md](MAPLINKS.md) §3.3 (the shortcut harvest) ·
[SKILLING_SOUNDS.md](SKILLING_SOUNDS.md) §4.12 (obstacle sounds) ·
[SKILLS_CONTENT_PORT_QUEUE.md](SKILLS_CONTENT_PORT_QUEUE.md) #92–99 (superseded
by this document) · [PORTING_GUIDE.md](PORTING_GUIDE.md) (corpus rules) ·
[SERVER_QUEUES.md](SERVER_QUEUES.md) (queue/ordering primitives the Sepulchre
timer will need)
