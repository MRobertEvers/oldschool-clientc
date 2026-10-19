# Path of Crondis

Read from 8 visits: 14 (solo, Entry) and 1852 (2, 2, 2, 3, 6, 6, 6 raiders,
Expert). Ticks are relative to entry (E) unless marked. Full inventory:
[crondis.out](crondis.out). The parts shared by every room (entry, HUD, song,
leaving) are in the [README](README.md).

**Ours:** `minigame_toa/scripts/toa_crondis.rs2`, rebuilt to these recordings 2026-10-18; see [Ours vs recorded](#ours-vs-recorded) at the end.

## Layout at entry (E+0)

| What | Id | Tiles |
|---|---|---|
| Player teleported to | | (3953..3954, 5279..5281) |
| Palm | npc `toa_crondis_tree_1` (11700), on `invisible_type8_blocking_size5` (32740) | (3934,5278) |
| Water containers (ground) | obj `toa_crondis_water_container` (27295) | (3934,5273), (3938,5287) |
| Column traps, south | loc `toa_crondis_column_trap` (45414) shape 10 rot 0 | (3929,5255), (3943,5255) |
| Column traps, north | same, rot 2 | (3929,5304), (3943,5304) |
| Water sources | `toa_crondis_water_source` (45398), rot 0 south / rot 2 north | (3926,5250), (3940,5250), (3926,5306), (3940,5306) |
| Crocodile walls | `toa_wall02_crocodiles04` (45434) | rot 1 (3924,5271), (3948,5271); rot 2 (3922,5273), (3922,5287); rot 3 (3923,5289), (3948,5289); rot 0 (3950,5273), (3950,5287) |
| Spear traps | 40 trap locs, below | |

Room song: `song_733` "Test of Resourcefulness", from E+3.

**The start (S, "Challenge started").** The raider arrives at x 49/50 and the
room starts on the tick they step west past x 47, onto x 45/46 (8 of 8), 2 to
21 ticks after arriving. Before S the palm and containers are already down and
every one of the 80 trap locs' idle is re-sent every tick (80 a tick, 8 of 8);
nothing else moves. The container can be
destroyed: `confirmdestroy` (584) with clientscript 814
`(27295, 1, "Are you sure you want to destroy this item?", "Another will be nearby.")`.

## Spear traps

Each trap is a pair: an activator loc that plays `crondis_spear_trap_activate`
(9563) and, 3 ticks later (2,809 of 2,809), a spear loc 2–3 tiles in that plays
`crondis_spear_trap_spear02` (9565) with area sound
`toa_crondis_croc_spear_trap_spear_expand_04` (6493) loops 1 range 5 delay 14
on the spear's tile. Between firings a trap's idle,
`crondis_spear_trap_idle` (9562), is re-sent every tick: counting c from the
activation, the spear's on c 6-9 and the activator's on c 7-9 (1,912 of 2,691
activator cycles and 1,978 of 2,583 spear cycles show exactly this; the rest
miss single ticks or sit at the edge of the recording player's view). After S a trap is silent until 2 ticks before its first
firing, then both its locs idle (8 of 8).

Activators are rot 2 at x 3925 and 3939 (spears at 3927 and 3941) and rot 0
at x 3932 and 3946 (spears at 3929 and 3943). **Every trap fires every 10
ticks, at a fixed phase** (identical in all 8 visits). Phase 0 is the first
firing, on S+3 (8 of 8; 5–24 ticks after entry):

| Activator x | z → phase |
|---|---|
| 3925 (rot 2) | 5258→2, 5260→1, 5262→1, 5264→0, 5266→0; 5293→0, 5295→2, 5297→4, 5299→6, 5301→8 |
| 3932 (rot 0) | 5259→2, 5261→2, 5263→1, 5265→1, 5267→0; 5294→0, 5296→2, 5298→4, 5300→6, 5302→8 |
| 3939 (rot 2) | 5258→1, 5260→2, 5262→3, 5264→4, 5266→5; 5293→2, 5295→5, 5297→8, 5299→1, 5301→4 |
| 3946 (rot 0) | 5259→5, 5261→4, 5263→3, 5265→2, 5267→1; 5294→2, 5296→5, 5298→8, 5300→1, 5302→4 |

## Orbs and column beams

The two lanes are x 3927–3931 (west) and 3941–3945 (east). Each has a column
trap at its south end (orbs on row 5257) and north end (row 5303).

1. **Orbs:** `toa_crondis_orb` (45403) shape 10 rot 0 is added on 2 or 3 of
   the lane's five columns at an end, every 5 ticks per end (161 of 165 at the
   south ends), at all four ends together on S+1, S+6, ... (167 of 167 orb
   ticks; the 49 other ticks with orbs at two ends are re-sends to a raider
   walking into view, and their "removals" come 1-2 ticks later). Area sound `mory_vanstrom_orb_spawn` (4424) loops 1 range 14 on
   the trap's column (x 3929/3943).
2. **Beam:** 2 ticks later (166 of 212), down exactly those columns (152 of 155),
   map_anim `crondis_column_trap_anim` (2129) on 10 tiles with delay 10, 20, …,
   100. From the south it runs z 5258 → 5267, from the north 5302 → 5293.
3. **Orb removed** 3 ticks after it was added (171 of 220).

Orb columns per end, as lane columns 0-4 (766 ends): three columns only as
(1,2,3) 133, (0,2,4) 98, (0,2,3) 92, (1,2,4) 74; two columns as (2,3) 70,
(1,2) 66, (0,2) 59, (2,4) 52, (1,3) 36, (1,4) 33, (0,4) 29, (0,3) 24. Never
(0,1), (3,4) or a lane's other triples. The pairs are those four patterns with
one column dropped: a pair that keeps column 2 is in two patterns' drops and
comes up twice as often (62 against 30 on average). The column trap loc itself
is re-sent (same id) about every 4 ticks (the same view re-sends).

## What a spear or beam does to a raider

Fitted against every tick the recording player stood in a lane (20 hits):

- **When.** Against the tile the raider stands on before moving that tick, a
  spear's three tiles are hot on c 4-6 (the 3 ticks after its spear anim), and
  a beam's row r (0 at the orb) on beam + 2 + (r + 1) / 3, the tick its anim
  (delay 10 x (r + 1) cycles) reaches the tile. After a spear hit spears miss
  that raider for 2 ticks; after a beam hit, beams do; the other kind still
  hits. This predicts 19 of the 20 hits and no hit that did not happen.
- **Damage.** 4-7 times the raid's damage factor: Entry (x1.0) 4, 5, 5, 5, 6;
  Expert, at the 250% cap, 10, 12, 15, 17 (15 of 15 are floor(4..7 x 2.5)).
  Hitsplat 16 and the player's `human_hit_*` synth delay 20.
- **Stats.** Defence -6 and Agility -3 (4 of 4 clean solo hits); no Prayer.
- **Water.** Half of it ("Water spills out of your container.", spam, synth
  `liquid` delay 10) when the container has any; a spilled full one pours 50.
- **Perfect Crondis** fails on the spill, the next tick, naming the raider; the
  15 recorded hits on an empty-handed raider never failed it.

## Water and the palm

| Event | Recorded |
|---|---|
| Fill at a source | player `human_pickupfloor` (827); "You fill your container."; synth `toa_crondis_fill_container` (6522) delay 10; the source becomes `toa_crondis_water_source_empty` (45399), same rotation. Sometimes also "While you're there, you take a drink of refreshing water!" |
| Source refills | `toa_crondis_water_source` again **14 ticks** later (29 of 41; the rest were longer). The empty loc is re-sent every 3–4 ticks until then. |
| Spill (hit by a trap/beam) | "Water spills out of your container."; synth `liquid` (2401) delay 10. In 14: "You have failed <col=0cc919>Perfect Crondis</col>: Z-Kris lost water from their container." |
| Pour | `human_pickupfloor`; "You empty a lot of water onto the palm!" (+100 hp) or "You empty a reasonable amount of water onto the palm." (+50); palm `hit` type 11; synths `toa_crondis_water_tree_02` (6534) and `toa_crondis_tree_grow_02` (6516), both delay 15 |
| Stage change | the palm retypes `toa_crondis_tree_1` → `_2` → `_3` → `_4` → `_5` (11700–11704) on the same tick, stage = 1 + 4 × water / base (32 of 32 retypes), with `level_change` 0 and `enabled_ops` 0b11111. A pour across two quarters skips a stage (3 → 5 in 14) |
| Crocodile bite | `hit` type 15 on the palm: 3, 3, 3, 3, 4, 5, 5 in 14 (Entry), 10, 10, 11 in 1852 (Expert); synth `toa_crondis_water_lost_02` (6529) delay 15 when it drops a stage (3 → 2). (This row was first read as a decay: the area sound `crocodile_attack` on the crocodile's tile comes on the same ticks.) |
| Grow synth | `toa_crondis_tree_grow_02` reaches every raider on every pour (a raider 8 tiles away hears another's); `toa_crondis_water_tree_02` only the pourer |

The palm's overhead headbar is type 11 (120 wide), fill = 120 × water / base
on every send. The palm's health is the boss bar: `hpbar_hud` (303) with `varp1683_hpbar_hud_npc`
= the palm's npc id, `varb12401_hpbar_hud_boss` 1, `varb6100_hpbar_hud_basehp`
= 200 × raiders, `varb6099_hpbar_hud_hp` = water; clientscripts 2376 and 2887
(fade in) drive it; components 13/14/15 get colours 132/623/853.

## Crocodiles

| | Recorded |
|---|---|
| Npc | `toa_crondis_crocodile` (11705) |
| Spawn tiles | (3925,5285), (3925,5274), (3946,5274), (3946,5285), one per croc, at the walls |
| First wave | E+48..67 |
| Waves | every 46 ticks (solo: +53, +99, +145, +192; 6-man: +53, +99) |
| Per wave | 1 (solo, 2 raiders), 2 (3 raiders), 3 (6 raiders): ⌈raiders / 2⌉ |
| Wave timing | waves on S+46, S+92, ... ; each crocodile 0-5 ticks into its wave (27 crocodiles, 10 on the wave tick) |
| Rising | faces the palm's middle (3936,5280) on its first tick, the palm itself 4 ticks later |
| Movement | `crawl`, a tile every 2 ticks, the first 5 ticks after rising (27 of 27): 4 tiles straight off the wall, then diagonal to a tile by the palm: WWWW777, EEEE333, EEEE999, WWWW111 (4 arrivals); WWWW77WW to (3938,5276) once and WWWW7NW7 once, both from the SE wall with a raider fighting them |
| Hitpoints | 30 in every raid (headbar 30 wide; every kill sums to 30) |
| Attack | `croc_attack_merge` (9804) on the palm, every 7 ticks (5 of 5 gaps), the first on arriving (2 of 4) or 1-2 ticks after; area `crocodile_attack` (386) loops 1 range 4 on its tile; the palm's type-15 splat on the same tick; synth `crocodile_hit` (388) is a raider hitting it. Never a raider |
| Death | `spotanim -1` (clear) and `croc_death` (2038) 1-3 ticks after the killing splat (1 in 10 of 18 kills), `enabled_ops` 0b00000, headbar empty; area `mummy_ash_death` (638) loops 1 range 8; removed **4 ticks** later (27 of 27) |

## Finishing (F = the pour that fills the palm)

- F: the pour, the palm → `toa_crondis_tree_5`, "Challenge complete: Path of
  Crondis…", `invisible_type8_nonblocking` (29733) shape 10 rot 3 added at
  (3922,5279), (3922,5280), (3922,5281) (the exit), song 147 + jingle 295.
- F+1..3: every living crocodile dies (`croc_death`, ops 0b00000), removed 4
  ticks after; every orb is deleted (F+2/3).
- The rest is the common leaving sequence.

## Ours vs recorded

Rebuilt 2026-10-18: `toa_crondis.rs2`, the Crondis blocks of `toa.constant`,
`toa.npc` and `toa.varp`, `toa_timing.rs2`'s three Crondis procs, the room
start radius in `toa_raid.rs2`, and two engine additions (`npc_setmovespeed`
2 = crawl, sent as NPC_INFO's crawl step; `hpbar_hud_npc`, an npc pinned to a
raider's health overlay ahead of their combat target). Checked with
`test/raids/toa_crondis_tells.lua` (scriptrun, `--fixture
test/raids/fixtures/fresh_lumbridge.ini`, solo, raid level 0; the raider stands
in a lane, fills, pours, lets the crocodiles bite, kills them with
`::toacrocs` and pours until the palm is full) against the rules above, and
`test/raids/toa_selftest.lua` ("toarun OK - 59 checks passed").

| | Status |
|---|---|
| Start | **fixed**: the room started on the entry tile (11 from the fight tile, inside the shared start radius 12); now 7, so it starts at x 46 and west. Before it: palm and containers down (2 ticks before S in the test), all 80 trap idles every tick |
| Spear traps | **fixed** (were NR's tables: the SE corridor a tick early on every trap, the north rows reversed): the recorded 40-phase table; every activation, spear and idle in the test's log (1,533 / 1,520 / 10,706) on the tick the rules give, none off; spear sound delay 14 range 5 on the spear's tile |
| Orbs and beams | **fixed** (were 200 animated poison-tile locs, which Jagex never sends, and orbs on uniformly random columns): orbs at all four ends on S+1+5k, removed 3 ticks later (761 of 761); only the twelve recorded column sets; the end's spawn sound range 14; beams on S+3+5k down the orb columns, delay 10 (r + 1) (7,710 of 7,710) |
| Hits | **fixed** (was a stat drain and halving with no damage, prayer drained, a guessed bite window): the fitted windows and per-kind 2-tick immunity, 4-7 x the damage factor (test: 20 hits, 4-7 at raid level 0), Defence -6, Agility -3, half the water with the spam message and `liquid` |
| Perfect Crondis | **fixed**: lost on a spill, not on damage; the failure message the next tick. (Ours sends it to the spiller; whether Jagex sends it to the party is not recorded) |
| Palm | **fixed** (was 175 + 125 a raider and an invented message): 200 a raider; water on the palm's hitpoints above a headbar reserve, so `healthbar_11` and the boss overlay read water / base; pours +100 / +50 with the recorded messages, splat 11, both synths; stage = 1 + 4 × water / base on the pour's tick |
| Boss overlay | **fixed**: the palm on every raider's overlay (`hpbar_hud_npc`, boss bar), base 200 from S+2 (recorded S+0/1: the engine opens the panel a tick before feeding it), following every pour and bite |
| Sources | **fixed**: refill 14 ticks (was 3); fill anim, message, synth delay 10 |
| Containers | **fixed** tiles (30,25) and (34,39) (were (32,26), (32,38)) |
| Crocodiles | **fixed** (were 2 a wave every 30 from 67, fighting raiders, 18-36 max hits): waves on S+46k, ceil(raiders / 2), 0-5 ticks into the wave; rise facing the palm's middle, crawl a tile every 2 ticks from +5, 4 straight and 3 diagonal to the wall's stand (test: 6 of 8 reached theirs; one was killed a tile short of the other stand, one was still crawling when the test ended), bite on arriving and every 7 (`croc_attack_merge`, area sound range 4, type-15 splat on the palm, 3-5 x the damage factor, the stage-lost synth); 30 hitpoints; death 1 tick after the kill at the palm, removed 4 after (`death_delay=4`) |
| Completion | **fixed**: exit locs at (18,31..33) rot 3, song 147 and jingle 295, overlay handed back. The song is then replaced on the same tick by Zebak's (736): see the shared item below |
| Scriptrun | the loc-anim and map-anim rings the headless client (and the live drive ring) keep for 40 ticks held 256 / 1,024: Crondis overflowed them in 8 ticks and, with the capacity asserts compiled out, corrupted the heap. Now 4,096 / 2,048 |
| Open: crocodile path detail | a crocodile whose stand another holds takes the wall's other stand (the SE wall's recorded (34,28), mirrored); Jagex's path choice between the two is not explained by the recordings (both alternate paths had a raider fighting the crocodile). Ours does not turn to face the palm on +4 (no npc-faces-npc command) |
| Open: bite size at Expert | Entry bites 3-5 fit 3-5 x the factor; Expert's 10, 10, 11 do not all fit one factor (ours gives 7, 10 or 12 at the cap) |
| Open: crocodile death sound | Jagex's `mummy_ash_death` is an area sound range 8; ours is the engine's npc death synth (`death_sound`) to raiders within 12 |
| Open: "refreshing water" | "While you're there, you take a drink of refreshing water!" on 2 of 23 fills; what decides it and what the drink does is not in the packets. Not modelled |
| Open: container respawn | the recordings only show re-sends to a raider walking into view; ours puts a taken container back after 25 ticks |
| Open: party | the tells test is solo; the party rules (base 200 x raiders, crocodiles per wave) are checked by `::toarun` only |
| **Open, every room** | `~toa_challenge_complete` takes the party to Zebak on F and frees the room, so Jagex's F+1..F+3 crocodile deaths, the orb deletions and the leaving sequence never happen here; "Challenge complete" lacks "Duration ... Total ..." |
