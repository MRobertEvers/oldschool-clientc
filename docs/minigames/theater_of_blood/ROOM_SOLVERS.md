# The Theatre of Blood room solvers, and the Normal trio relay: THE PLAN

Status, 2026-10-09: plan. Written: the shared base module
(`script/plugins/quest_driver/raid_solve_tob.lua`), a first Maiden draft
(`raid_solve_tob_maiden.lua`, `test/raids/_solve_tob_maiden.lua`), the planner's
`watchers` constraint and the scriptrun ring asserts. One Maiden seed was run
BEFORE this plan was finished; what it showed is section 9 and it is the last
run until every room's implementation plan below is agreed.

The brief (owner, 2026-10-09):

- from-scratch solvers for Maiden, Bloat, Nylocas, Sotetseg and Xarpus, in the
  architecture of Verzik's three (`raid_solve_verzik_p1/p2/p3.lua`);
- Normal mode, a trio, every seat in the wiki's budget **Learner/Void** setup,
  with the prayer unlocks set on the player;
- one relay: the ToB lobby, the notice board's party, the six rooms, the supply
  chests, the Dawnbringer, the reward chamber, the exit back to the lobby;
- read the guides and the lessons, plan each room, THEN run the tests; do not
  thrash.

The per-room specs, written from the content with a file:line for every fact
(lesson 1), are `solver_specs/<room>.md`. `solver_lessons.md` is the checklist;
section 8 maps every lesson to where it is applied.

## 0. The run protocol (how this work avoids thrashing)

1. A room is coded IN FULL from its implementation plan (section 4) before its
   first run. No run is used to discover a fact the content or the spec states.
2. The facts the spec could NOT settle are listed per room as **probes**: each
   is a row the room test prints from the tick log on its first run (a
   measured number next to the spec's prediction), never a code path that
   guesses. The first run of a room is a probe run on ONE seed.
3. After a run, the whole tick log and every seat's trace are read before any
   edit; every finding is written down (section 9 or the room's section), and
   the fixes go in as ONE batch. Then one seed again, then 16.
4. A failure whose cause is in the content or the engine is written down and
   fixed at its source, never worked round in a solver (lesson 50, 51: read the
   content before blaming the solver, and do not "fix" real behaviour).
5. Scriptrun first, on a private build (`PLATFORM_OBJ_BASE=build_rooms`); one
   live seed per room at the end, compared row for row with `tlcmp.py`.

## 1. The architecture (every room)

One file per room, `raid_solve_tob_<room>.lua`, exporting
`t.raid.<room>_solve(opts) -> result, detail, S`, called by every seat at the
room's entry. Each server tick (the base module's loop, as Verzik P3):

1. **MEASURE** -- base: my tile, every npc row on the SERVER tile (26), the
   raiders by pid with names, hitpoints, prayer, lit prayers, boosted stats;
   room: its own facts keyed by what makes them distinct (20).
2. **CLOCK** -- the room's schedule in absolute ticks, anchored on the first
   animation/projectile seen; a guess before that only keeps raiders safe (4).
3. **CONTEXT** -- the schedule names the context; the context hands
   `api_drive.plan` a goal and constraints with absolute windows in the tick
   mapping of lesson 2: a decision on T reads the end of T-1; my click after
   seeing d moves me in d+1; plan step k is my tile at the end of d+k.
4. **PLAN** -- `collision_plan` in C on both lanes (24): stay or one or two
   steps a tick, forbids, zones, pulls, chasers, watchers, goal, edge.
5. **ORDER + EMIT** -- one click from the plan's first step (8, 13, 18, 21,
   47); one panel channel a tick, the prayer first (32); only what changed.

Shared choices are computed by every seat from the same view, never sent (5,
6). The barrier verbs are used only BETWEEN rooms.

### 1.1 Base-module rules every room inherits

- **The start.** The leader clicks `tob_arena_barrier` and answers "Yes, begin
  the fight." when the room's start rule says go (blocking is fine: no hazard
  runs before the start). A member crosses with a NON-blocking loc op
  (`api_drive.world_op("loc", barrier, 1)`) on the tick after it sees the
  leader inside, and re-reads its own tile (seed 1: `click_loc` waits for a
  walk, a started barrier teleports, and the wait timed out).
- **Reach is a gradient, never a flat price.** The "in reach of my target"
  term is a pull (weight x gap beyond reach) plus a small in-reach bonus, so a
  plan that cannot reach this horizon still moves toward the target (seed 1: a
  flat cost made every tile equal, the plan stood, no order went out, and the
  raider stood 300 ticks).
- **A target I cannot hit is not my target.** Every room's target rule drops a
  target with no reachable tile, and falls through to the next.
- **An attack order follows a moving npc** (47). Ranged reach is judged at the
  target's PREDICTED next tile; when the plan moves, the order is a walk.
- **Prayer timing.** A protect prayer pressed after seeing d is in force in the
  npc phase of d+1 (prayer.lua header: a press made between ticks T-1 and T is
  in force for tick T's phase_npcs). Every deadline below uses that; one probe
  row confirms it on the first run of Maiden (the storm reads at launch).
- **Supplies channel**: prayer points < 25 a restore; hitpoints < 60 food, a
  brew under 45 with no food; the boost redosed at +5; the boost prayer only
  with 40+ points.

## 2. The kit: Learner/Void, one loadout, the prayer unlocks

`sources/wiki_Theatre_of_Blood_Strategies.wikitext` "Learner" (:93-126) and the
Learner/Void example inventory (:294-330). Every seat carries the same set.

- **Worn**: `game_pest_melee_helm`, `elite_void_knight_top`,
  `elite_void_knight_robes`, `pest_void_knight_gloves`, `abyssal_tentacle`
  (`::~charge` 10000), `dragon_parryingdagger`, `zenyte_amulet_enchanted`,
  `tzhaar_cape_fire`, `dragon_boots`, `nzone_berzerker_ring`.
- **Ranged switch**: `game_pest_archer_helm`, `toxic_blowpipe_loaded`
  (`::blowpipe dragon_dart`; range 5, rate 3), `zenyte_necklace_enchanted`,
  `avas_assembler`. Two-handed: the defender goes to the pack (one free slot).
- **Mage switch**: `game_pest_mage_helm`, `toxic_tots_charged` (range 7, rate
  4; `::~charge`), `occult_necklace`, `ma2_saradomin_cape`.
- **Other**: `dragon_warhammer`, `nzone_salve_amulet_e`.
- **Supplies**: `br_4dose2restore` x5, `br_4dose2combat` x2,
  `br_4doserangerspotion` x2, `br_4dosepotionofsaradomin` x4, `anglerfish` x4;
  one slot free. Topped up at the chests (section 6).
- **Prayer unlocks** (`::setvar`, every seat): `varb3888_kr_quest
  ^kr_complete` and `varb3909_kr_knightwaves_state 8` (Piety, Chivalry);
  `varb5451/5452/5453_prayer_*_unlocked 1` (Rigour, Augury, Preserve);
  `varb16097/16098` (Deadeye, Mystic Vigour). Content finding: nothing in the
  content checks these yet (`[proc,prayer_checks]` gates on level only), and
  the Knight Waves value 8 is the OSRS convention, not stated in this content
  (Knight Waves is not wired: `kr_bmp.rs2:851`). Owner's call whether the
  gates should be added (section 10).

A style switch is the set's held ops in ONE tick, the attack re-pressed after
them (OPHELD clears it).

**The freezer** (Maiden; the wiki's trio has exactly one, :612 "in solo to trio
there is one freezer", and Ice Barrage is "essentially mandatory", :600): the
seat that leads the orb order (seat 1, the leader: "let the freezers lead in
orb order", :597) also carries Ancient Magicks (`::setvar varb4070_spellbook
1`) and the Ice Barrage runes. The Learner inventory keeps them in a rune
pouch (:321-330); there is no pouch-loading cheat, so the runes ride loose
(`waterrune`, `deathrune`, `bloodrune`: three stacks, two supply slots fewer
on that seat). The wiki's void freezer reaches a 100% freeze at +54 magic
accuracy with Augury (:248), and "may need to take off their defender and
boots" (:249): the freezer's cast set is the mage switch with the defender
and boots removed.

## 3. Engine, content and API work BEFORE the rooms

| # | item | room | state |
|---|---|---|---|
| B1 | **Maiden respawns her 70% wave every tick** (seed 1: retype 8361->8361 and six crabs every tick from t308, 3014 crabs in 503 ticks). `~tob_maiden_thresholds` latches in an npc var (`^tob_var_spawned` via `~tob_nset`) written after `npc_changetype`; hypothesis: the engine's changetype clears npc vars (or rehydrates after the script), so the latch reads 0 next tick. Fix at the source (engine or content), with a selftest. | Maiden | found, not fixed |
| G1 | line of sight in the planner (`watchers`, the near-edge rule) | Bloat | built |
| G2 | scriptrun's map-graphic ring 256 -> 1024, full = assert; projectile ring full = assert | Bloat | built |
| G3 | `locs()` lists ground decoration changes on both lanes (Maiden trails, Sotetseg lit tiles, Xarpus exhumeds/pools) | Maiden, Sotetseg, Xarpus | scriptrun: by the specs' reading of scriptrun_core.c; a probe row per room |
| G4 | `api_drive.plan` on Sotetseg's realm (plane 3, a second instance): both lanes take `collision_maps[player level]` | Sotetseg | probe row |
| G5 | the projectile `target` field for a player on the live lane | Sotetseg, Xarpus | probe at the live run |
| G6 | **a spell cast on an npc in scriptrun**: `api_drive.spell_arm` (the spell's target mode) and the npc's "Cast" row (OPNPCT) are client verbs scriptrun answers `unsupported` (torirs_server_scriptrun.c:2219). Port both on the client's semantics (refusals included: no runes, tab hidden), so the freezer's barrage is the same press on both lanes. | Maiden | to build |
| B3 | **the content's freeze chance ignores Void and Augury.** `~tob_matomenos_freeze_chance` (tob_maiden.rs2:1985-1994) is a curve on the worn magic BONUS alone (100% at +140). The wiki (wiki_Nylocas_Matomenos.wikitext:168, :448) scales it on the magic ACCURACY ROLL, hidden bonuses included (Void, Augury): 100% at a roll of (base Magic + 9) x 204. Under the content a void freezer (about +50) freezes about 35% of casts; under the wiki, 100%. Fix the content to the wiki's roll (wiki outranks; ruling recorded), with a selftest of the void and ancestral rows of the wiki's table. | Maiden | found, not fixed |
| B2 | non-blocking member crossing (base) | all | to write |

## 4. The rooms: implementation plans

Each plan lists: the facts the code relies on (from the spec), STATE, the
CLOCK, each CONTEXT with its exact constraints (window, tier, cost), the TARGET
rule, the ORDER rule, the non-movement channels, the PROBES of the first run,
and the PASS row. Tiles are local to the room origin unless absolute is said.

### 4.1 Maiden (`solver_specs/maiden.md`) -- blocked on B1, B3, G6

Facts: body SW (26,28) size 6, centre (29,31); attack every 10 from A1 = start
+ 9; storm at the raider nearest the centre (ties to the leader), Protect from
Magic read at the throw; blood: a pool per raider tile at the end of B-1 plus
two extras a tick later, pool lands B + floor((50+15d)/30) (extras +25
cycles), hurts reading the end of the tick before for 11 ticks; trails
(`tob_maiden_blood`) live 30 ticks on each spawn tile; crabs leak into x24..32
z26..34.

- **STATE**: base origin; `pools` {tile, t0 = land-1, t1 = land_extra+9} keyed
  (tile, B); `trails` {tile, seen}; `slug_last` per slot; counters.
- **CLOCK**: A1 from the first `maiden_attack_*` seq; B = the blood seq's tick
  (the projectile's first sight when the seq was missed).
- **ROLES**: seat 1 FREEZER (leads the orb order; the guide's trio has one,
  :612); seats 2 and 3 DPS. The DPS MELEE her with the tentacle in the kit's
  worn void melee set and Piety (owner, 2026-10-09: "tentacle whip melee void
  for the dps roles"; the guide's :646 reserves melee for a scythe -- the
  owner's ruling stands), opening with a dragon warhammer special (:617-640).
  The freezer ranges her with the blowpipe from gap 5 ("the freezer(s) should
  range Maiden from a distance", :646).
- **STATIONS and the TANK**: the storm goes to the raider nearest her centre
  (29,31); every melee tile on her east and north faces is Chebyshev 3 from it,
  on her south and west faces 4. The tank melees from the east face (32,31),
  the other DPS from the south face (30,27, beside the crabs' arrival), and they
  swap every 6 storms (every seat counts the same storms). The freezer at
  (36,31), Chebyshev 7, never tanks.
- **CONTEXT PREWAVE** (the freezer, within 4% above a threshold): the cast set
  on, Augury, at (38,30) where both 1s are in spell range, no swing -- so the
  first barrage goes out at +1 (seed m2: switching and walking after the spawn
  put it at +5 and the wave leaked).
- **CONTEXT FREEZE** (the freezer, a wave up): the unfrozen crab that can still
  be saved (ticks to leak >= 2) with the fewest ticks left, ties to the biggest
  3x3 clump -- which reproduces the solo freezer's order (:653
  "hover ... S1's spawn ... clumping 3s and 4s should be prioritised over
  freezing a single S2 or N2"): freeze S1 (or N1 if no S1) on the first tick
  possible, then the 3s/4s as they clump in front of her (+11 and +16, :651),
  choosing each primary to catch the most crabs in its 3x3; then barrage the
  clump until it is dead or nearly. A crab is frozen only if the cast lands by
  its last-safe tick (spec: C+5 for the 1s, C+8 the 2s, C+12..16 the 3s/4s);
  a frozen crab is re-frozen before frozen+32. Before each cast the cast set is
  worn (mage helm, trident, occult, imbued cape, defender and boots off) and
  Augury lit; between waves the ranged switch and Rigour come back.
  Movement: a cast tile within 10 of the primary (the plan's reach pull), the
  same pool/trail/slug constraints.
- **CONTEXT CRABS** (the two DPS): "DPS roles should kill the stray nylocas
  before getting back on Maiden" (:651): BOTH on the most urgent crab the
  freezer is not covering (focus fire; seed m2: one raider alone did not kill a
  2 before its leak at +10), then the frozen clump, then her.
- **CONSTRAINTS, every context**: pools forbid [t0,t1] DAMAGE 12; trails forbid
  [seen-1, seen+28] DAMAGE 9 (nearest under the cap, within 2H+1); each slug
  within 10: zone hi 0 on its tile at now, and along its last step at now+1,
  now+2 (DAMAGE 9, then 2 at hi 1); body lethal; edge the arena box.
- **TARGET** (DPS): see CRABS; then the nearest slug within 10; then her.
  Frozen is perceived from the npc's `ice_barrage_impact` spotanim on it and
  no step since (the spec's perception table), remembered with the cast tick.
- **ORDER**: base rule, range 5, reach at the target's predicted next tile.
- **CHANNELS**: Protect from Magic always; the DPS Piety and super combats;
  the freezer Rigour (Augury while casting) and a ranging potion; a restore
  only for prayer points or a drain of 10+ (the storm drains the stat behind
  the highest attack bonus; restoring every point drank 38 doses, seed m2).
- **PROBES** (first run, one seed): (e) every barrage cast: frozen or not, and
  the freeze's ticks (one cast on a crab each wave is enough to check the B3
  fix); (a) the storm's launch tick vs my prayer bit
  (a storm on a prayed raider hits <= 18); (b) each pool's first damage tick vs
  B + floor((50+15d)/30) (spec: 170 cycles -> +5); (c) trail loc rows seen on
  the tick they are laid; (d) per crab point, spawn -> leak tick vs the spec's
  C+7/10/14/18.
- **PASS**: her death; no deaths; 0 pool and trail damage; every storm <= 18 +
  2c; leaks <= 5 (Blert median 5, max 13), every freeze cast landing; room <=
  204 ticks.

### 4.2 Bloat (`solver_specs/bloat.md`)

Facts: 5x5, walks the 5-wide ring (corners SW (24,24) SE (35,24) NE (35,35) NW
(24,35)), 1 a tick (>= 60%), 2 a tick 40-60%, alternating below 40% on every
attack; turns only when the 32-walking-tick cooldown is out, then 1 in 17 a
tick; flies every walking tick at every raider he SEES (near-edge rule),
reading the end of T-1 (and spec Q1: his own tile before or after the step);
hands every 6 (4 below 40%), shadow on D, impact D+3 reading the end of D+2;
down at T: stomp T+29 reading the end of T+28 (gap <= 3 and seen), rise T+33
(a walking tick: step + flies reading the end of T+32 + the due volley).

- **STATE**: `lap` (his SW tiles in walking order, learned before the start:
  he circles before the fight; 44 steps); his index on it, direction, speed,
  walking ticks since the last reversal; `down_at`; `shadows` {tile, land}
  keyed (tile, first sight).
- **CLOCK**: phase WALK/DOWN; down on the seq `tob_bloat_sleep`; stomp/rise
  ticks from it; turn possible when the cooldown estimate <= 0 (32 from the
  start, 32 from each seen reversal, walking ticks only); hands from first
  sight + 3.
- **PREDICTION**: his SW tile at the end of d+k for k = 0..H along the lap in
  the current direction at the current speed; the reversed branch too when a
  turn is possible; both speeds near 60/40% or below 40%.
- **CONTEXT PRE**: every seat at the entry learns the lap and the origin
  (`tob_bloat_chamber` - (30,30)). START RULE (leader): his predicted
  footprints for start..start+6 do not see the crossing tile (39,31) nor the
  first hidden tile of the east leg (by the watcher test, computed with a
  zero-move plan); members cross on the tick they see the leader in.
- **CONTEXT WALK**: watchers = his predicted footprint at d+k-1 AND d+k (Q1:
  both readings), both directions when a turn is possible, windows [d+k,d+k],
  LETHAL; shadows forbid at land-1 (middle tiles charged) LETHAL; teammates'
  tiles zone gap <= 3 SOFT (fly spread); a pull toward his predicted down site
  once the down window opens (eligible <= now + 2). H = 6.
- **CONTEXT DOWN** (T+1..T+27): goal beside his footprint (melee), the side
  nearest; the stomp: footprint gap 0..3 at the end of T+28 is LETHAL (the
  content also needs sight, but gap >= 4 alone is the simple sufficient rule;
  a run of 2 ticks covers gap 1 -> 4).
- **CONTEXT RISE** (T+28..T+33): watchers on his down footprint for
  [T+28,T+32] and his first rise steps (both directions if a turn is possible)
  at [T+32,T+33] LETHAL.
- **TARGET**: him, only in DOWN (attacks while he walks cost flies and flip his
  speed below 40%).
- **ORDER**: base, melee; the last attack press is the one whose swing still
  lets the run out (a press stands me beside him; the plan's lethal stomp zone
  from T+28 pulls me out from T+26).
- **CHANNELS**: Protect from Missiles while he walks (pressed by the decision
  after seeing T+31, in force from T+32's npc phase; the first fly after the
  rise reads at T+33); Piety in the down; super combat at +5. The tentacle is
  worn (the kit's own melee).
- **PROBES**: (a) Q1 -- every fly row's tick vs his tile at the end of T-1 and
  at the end of T (which footprint saw the raider); (b) the lap length and
  corners from the pre-fight walk (44?); (c) the stomp's reach (gap 3,
  Chebyshev?) from any stomp row; (d) the sight test vs the server's: a
  `watchers` prediction for every fly target (scriptrun's rayCastLine and the
  server's must agree on the tank's 29 blockers).
- **PASS**: 0 fly, hand and stomp hits; no deaths; <= 3 downs on 14/16; median
  room <= 160 ticks.

### 4.3 Nylocas (`solver_specs/nylocas.md`)

Facts: supports exist from the start (room tick 0); waves on the 4-tick cycle
with the content's stalls and the 12/24 cap (corpses count until despawn); a
nylo's style is its npc id (`incoming`/`fighting`, small/big); flickers change
at birth+5 and +7; aggros swap to `fighting` at the lane mouth and hunt the
nearest raider (melee reach 1 diagonals included, ranged/magic 8), swing every
3 with the prayer read at the swing; every nylo detonates at birth+51 (small) /
+52 (big) on everyone within 2 of its footprint, reading the end of the tick
before, 1..18/21 unprayable; a big leaves two smalls at its despawn; ONE
wrong-style hit nulls a raider on that nylo for life (no reflect in Normal);
Vasilias lands L = first tick >= last despawn + 16 on the cycle, melee at L+2,
switches at M+9+10k to one of the other two styles, two attacks a form, reach 8,
a wrong style on her reflects and heals her.

- **STATE**: room tick r from the supports' first sight; `nylos[slot]` {birth,
  style (from the id), big, aggro, chain prediction}; my `nulled` set (from
  `tob_nylocas_shielded` on my target's tile or the "no effect" message);
  live count; boss L, M, switch ticks, colour.
- **ROLES by seat**: 1 RANGER (blowpipe, west station (27,25)), 2 MELEE
  (tentacle, south (31,21)), 3 MAGE (trident, east (36,25)). The tentacle and
  the trident hit one target: no splash, no nulling by splash.
- **TARGET** (every seat computes all three): candidates = live nylos of MY
  style at my earliest swing tick (a flicker counts by its predicted style at
  d+1), not nulled by me, in or at the box; priority aggro near a raider >
  chewer at the weakest support under 40% > nylo within 6 ticks of detonating
  within 4 of a raider > newest; ties lower slot; keep the target until it
  dies or stops being mine. Cross-help only after 3 idle ticks, the lowest pid
  helper, wield-then-attack in one tick.
- **CONTEXT WAVES/CLEANUP**: reach pull to my target; detonation zones (gap 0..2
  of the footprint at [det-1, det-1], DAMAGE 18/21) for every nylo detonating in
  the horizon; aggro melee zones (gap <= 1 on its swing ticks, DAMAGE 9) when
  my overhead is not its style; station pull 0.2; edge the box.
- **CONTEXT BOSS_DUE / BOSS**: Protect from Melee by L+1; on each predicted
  switch tick T (confirmed by the npc id at the end of T) emit in ONE tick:
  the new protect prayer, the new style's switch set, the attack; never press
  with a mismatched style; stand within my weapon's reach and within 8 of her
  so she never walks.
- **CHANNELS**: protect prayer = the style of the most aggros in reach of me
  (melee only within 1), else Melee; the boost prayer of my style; eat above 51
  when a support is low (a collapse is 1..50, room-wide).
- **PROBES**: (a) the NPC_INFO view covers every spawn tile from every station
  on the spawn tick (birth = first sight); (b) detonation ticks +51/+52 vs the
  log; (c) the flicker ticks; (d) the boss's first attack after a switch (+2/+3)
  vs my prayer bit.
- **PASS**: 0 supports lost; 0 wrong-style hits (no shielded graphic from a
  bot, no reflect, no wrong-style heal); 0 detonation hits; room <= 472 (Blert
  max, median 412); <= 120 damage a raider; no deaths.

### 4.4 Sotetseg (`solver_specs/sotetseg.md`)

Facts: 5x5 at (13,38), never moves; an attack every 5 from start + 6; a ball
(always magic, prayer read at LANDING, flight floor((20+36+8d)/30)) that on
landing splits into one grey (Missiles) and one red (Magic) at the other two
(landing by pid: L+r if the recipient's pid is higher, L+r+1 if lower); melee
only on a target within 1 (50%); the death ball after 10 counted balls, seq
like a ball, projectile 1604, lands T+15 split among raiders within 1 of the
target (target read end of T+14, lower pids end of T+15, higher end of T+14);
mazes at <= 1998 and <= 999 hp, the runner = the lowest living pid, everyone
stunned 5, teleported on P+3; the runner sees the whole path (loc 33035 on
level 3) on P+4; a wrong grid tile rags everyone within 1; a tornado walks the
path once the runner is past row 2; the maze ends on the first tick % 4 with
nobody on either grid.

- **STATE**: origin (boss SW - (13,38)); slots; balls counted (1606 from his
  source tile); `incoming` {style, land} keyed (spotanim, target, first sight);
  `death_ball` {target pid, T}; maze {P, runner, path, tornado}.
- **ROLES**: all three on the blowpipe at stations gap 4 from him -- E (22,40),
  W (8,40), NW (9,45) -- never within 1 (no melee ever).
- **CONTEXT FIGHT**: reach pull (gap <= 5) + station pull; the footprint gap
  0..1 zone LETHAL at every slot-1 end (no melee licence).
- **CONTEXT DEATH BALL**: everyone's require-zone gap 0..1 of the target's
  tile over [T+13, T+15] LETHAL; the target holds its tile (a walk order to
  its own tile over the window, 47); disperse from T+16.
- **CONTEXT MAZE-RUNNER** (my level = 3): the path from the lit locs ordered by
  row then along each run; forbid every grid cell not on it (LETHAL, middle
  tiles charged: the wiki's no-skip rule); the tornado's predicted tile per tick
  (spawn the tick after I first end past row 2; one path tile a tick) forbid
  LETHAL with its neighbours on the path; goal the north exit row; no step
  before P+5. NEVER the south-step shortcut (section 10).
- **CONTEXT MAZE-WAITER**: forbid the arena grid LETHAL; hold at (15,20).
- **CONTEXT POST-MAZE**: back to the stations; first attack at end+1.
- **CHANNELS**: Protect from Magic always; Protect from Missiles only for a
  grey landing on me next tick with no red landing on me that tick, back to
  Magic after it; Rigour; never let a ball land unprayed (the 5-tick lock).
- **PROBES**: (a) G4 -- the runner's first plan on plane 3 answers "ok" with a
  path on the lit tiles; (b) G3 -- the lit tiles seen on P+4; (c) every
  ball/ricochet landing tick vs the formula and the pid rule; (d) the
  death-ball read ticks per pid.
- **PASS**: no deaths; 0 unprayed ball or ricochet hits; every death ball
  shared by 3 (each splat <= 41); 0 rags; 0 tornado hits; maze proc to the
  boss's return <= 35 (Blert median 28).

### 4.5 Xarpus (`solver_specs/xarpus.md`)

Facts: exhumeds (loc 32743) E_k = E_0 + 8k, twelve, each needing a raider on it
at the ends of E+2..E+9; stand-up U = R+117 (5x5 at (32,33)); spits S_k = U+7+4k
at a random raider (never the last) reading the end of S-1, landing S+3 (S+4
from the outer ring) with a 3x3 splash reading the end of L-1, a permanent
pool (loc 32744), and 1 then 2 chains at the other two raiders' end-of-L-1
tiles landing L+2; screech at hp <= 937, turns every 8 from it to a quadrant
not the last; a swing from the faced quadrant on T+1..T+7 is retaliated per
hitsplat (50..75+).

- **STATE**: origin (feeding SW - (33,34), combat SW - (32,33)); exhumeds {tile,
  E, owner}; landings {tile, read = L-1} keyed (tile, L); pools; the stack's
  tile sequence; Q, turn ticks, the faced quadrant.
- **CONTEXT EXHUMES**: the free raider with the fewest route steps takes a new
  exhumed (ties pid; a raider whose own still needs it stays unless the new one
  is within 2 steps); require-zone on its tile over [E+2, E+9] DAMAGE 12; idle
  raiders pull to three homes chosen before the fight to cover the floor; off
  his 3x3, and off the 5x5 by the end of U.
- **CONTEXT SPIT -- THE STACK**: all three on P_k at the end of S_k - 1, all
  three run two tiles to P_{k+1} on S_k (the decision after seeing S_k - 1);
  P_{k+1}: Chebyshev 2 from P_k, not a pool, not under him, 3..6 from his
  centre, not within 1 of a pending landing at its read tick; every seat
  computes the same sequence from the same view. Constraints: require-zone on
  P_{k+1} over [S_k, S_k+3] DAMAGE; splash zones per pending landing; pools
  forbid DAMAGE (middle tiles charged: over-cautious, accepted); footprint
  LETHAL.
- **CONTEXT SCREECH**: keep the stack; on the decision after seeing T-1 for a
  predicted turn T, no attack order (a walk to my own tile) so no swing falls
  on T+1; on seeing T, if the faced quadrant is the stack's, the stack walks
  into the nearest other quadrant during T+1 and re-presses at T+2; else
  re-press at once. Zone: the faced quadrant over [T+1, T+7] LETHAL.
- **TARGET / ORDER**: him from P2 (blowpipe, range 5 covers the stack's tiles);
  re-press after every move.
- **END**: at his death seq the leader (seat 1) searches the skeleton for the
  Dawnbringer (the relay's; the room test does it too).
- **CHANNELS**: no protect prayer matters (every hit is typeless); Rigour from
  U; ranging potion at +5.
- **PROBES**: (a) the first spit's landing vs S+3 (the spec's Chebyshev
  `distance()` assumption); (b) a pool added on L read by the sweep on L or
  L+1; (c) the screech tick vs the hit that crossed 25%; (d) a walk on T+1
  cancels a swing due on T+1 (the plan avoids needing it: no order is out).
- **PASS**: heal orbs 0 on 14/16; 0 splash, pool, stomp and retaliation hits;
  P2 <= 148 ticks (Blert max, median 108); the Dawnbringer in seat 1's pack.

### 4.6 Verzik

`raid_solve_verzik_p1/p2/p3.lua` as they are, in the same kit. Relay deltas,
each a small change made and swept on its own: P1's Dawnbringer comes from the
Xarpus skeleton (held by seat 1, which P1's custody already starts with);
P2's "a poisonous hit bursts the Athanatos" is met by the blowpipe's venom
(a ranged switch on the east raider at the Athanatos) instead of the
serpentine helm. Note: the working tree currently carries another session's
uncommitted Verzik crab calibration (`tob_verzik.rs2`, `tob.constant`); P2 fails
the relay smoke against it (12-18 slams a raider, 2 deaths). Verzik work waits
for that session to land.

## 4.7 Calibration against Blert (Maiden and her crabs; the Nylocas room)

Owner, 2026-10-09: "Using blert's data, statistically analyze the behavior of
maiden and the nylocas and ensure that the behavior matches statistically."
A solver is only as right as the npc it plays against, so each room's content
is held to Blert's Normal trio rooms BEFORE its solver is judged. Method
(lessons 28, 48-54; memory: per-entity per-tick timelines, never medians):

1. **Data**: 30 Normal (mode 11) trio (scale 3) completed rooms' event streams
   per stage (Maiden 10, Nylocas 12) from blert.io, one request per 3 s,
   cached under `build/blert/<room>/<uuid>.json` (+ `.meta`), as Verzik's.
2. **Ours**: the same events from our tick logs: the 16 scriptrun seeds of the
   room test.
3. **One timeline per entity**, printed for both sides and set side by side:
   - Maiden: attack ticks from the start (first attack, cadence, the blood
     throw's share and cooldown, attacks per room), the storm's target against
     every raider's distance to her centre at T-1;
   - her crabs: per wave the spawn points used (and the scuffed shift), per
     crab its first step, every step tick, its path, its arrival/leak tick, a
     freeze's start and end (still 20+ ticks), death; the leak's heal;
   - her blood spawns: spawns per throw, every step (tick, direction, distance
     to the nearest raider -- do they walk at raiders?), the trail's lifetime
     per tile (Blert's trail runs);
   - Nylocas: wave spawn ticks against the cycle and the cap, each nylo's lane
     walk, first swing, style flips, detonation tick, split spawns; the boss's
     landing, switch ticks, attacks per form, targets.
4. **Rules from the exceptions**: every disagreement is either a content bug
   (fixed in the content with a selftest, citing Blert) or a solver choice
   (written down, as lesson 54). A room's solver work resumes only on
   calibrated content.

## 5. Tests

- `test/raids/_solve_tob_<room>.lua`, on the `_solve_verzik_p1.lua` shape: the
  void kit with the unlocks, `t.raid.enter("tob", room, {mode = "normal"})`,
  the ready barrier, `::synctimers`, `t.raid.<room>_solve`, the leader's
  measures from the tick log: the room's PROBE rows and one `<room>.measure`
  row with the PASS criteria.
- `test/raids/_solve_tob_relay.lua`: section 6.
- Sweeps: `rsweep.sh` (scratchpad), one seed, then 16.

## 6. The relay (lobby to lobby)

`raid_solve_tob_relay.lua`, on the generic drive verbs (`t.party.form / apply
/ accept / ready / follow_in`, `t.player.click_loc`, `t.world.loc_near`):

1. **Lobby**: the trio at Ver Sinhaza in the kit; the leader forms a Normal
   party at the notice board; the members apply and are accepted; the door's
   ready check; everyone follows in.
2. **Each room**: barrier `pre`, `t.raid.<room>_solve` on every seat (it starts
   the room), barrier `post`.
3. **Between rooms**: the cleared barrier is a gate; the passage
   (`tob_dungeon_walkway_exit_clickbox`, Xarpus' `tob_dungeon_xarpus_arena_door_exit`)
   carries every raider in the old room into the next (`~tob_carry_party`).
4. **Supply chests** after Bloat and after Sotetseg (`tob_midway_chest_closed`
   -> `tob_midway_stores`, a points store; `varb6460_tob_midwaychest_points`):
   each seat buys restores first, then food, for the rooms ahead.
5. **Dawnbringer**: after Xarpus, seat 1 searches the skeleton.
6. **Verzik**, then the trapdoor (`tob_dungeon_verzik_throne_door_opened`), the
   reward chest opened by every seat.
7. **Exit**: the reward room's exit back to the ToB lobby; the test ends with
   every seat in Ver Sinhaza.

## 7. The order of work

1. B1 (Maiden's wave latch) and B3 (the freeze roll) at their source, each
   with a selftest; G6 (the spell cast on scriptrun, client semantics); B2
   (member crossing) and the base's reach-as-gradient rule.
2. All five room solvers and tests written from section 4 (no runs).
3. Room by room in raid order: ONE probe seed, read everything, one batch of
   fixes, one seed, 16 seeds.
4. Verzik deltas (after the other session lands its content).
5. The relay; 16 relay seeds.
6. One live relay seed against scriptrun (`tlcmp.py`).
7. What each room taught, appended to `solver_lessons.md`.

## 8. The lessons, and where each is applied

| lessons | rule | applied |
|---|---|---|
| 1 | spec from the content first | `solver_specs/*.md`, each fact cited |
| 2, 19 | the tick mapping; read ticks per queue and pid | every window in section 4; Sotetseg's ricochet and death-ball pid rule; Xarpus' L-1 reads |
| 3, 26, 31 | wake on `server_tick`; the server's tick; server tiles | base loop and measure |
| 4 | a guessed clock only keeps raiders safe | every CLOCK anchors on the first animation/projectile |
| 5, 6 | count from what everyone sees; one rule, one view | Maiden crab seats, Nylocas targets, Xarpus exhumeds and the stack, the death-ball stack |
| 7 | paths in steps, not tiles | the planner and `api_drive.route` only |
| 8, 13, 18, 21, 47 | a press moves you; an order swings from where it stands; offer "walk then attack"; a refused press stands still; an order follows a moving npc | base ORDER; reach at the predicted next tile; walk orders inside every hold |
| 9, 24 | horizon search, one planner, many contexts | `api_drive.plan` per context |
| 10, 11, 32 | IO at tick boundaries; budget headroom; a tab settles in its call | base emit, `TORIRS_SCRIPTRUN_STEP_BUDGET=200000`, pre-fight searches (Bloat's lap) |
| 12, 17 | many seeds; the tick log, not the bot; compare to Blert room by room | PASS rows from the log; 16 seeds; Blert bars per room |
| 14, 15 | zero damage is not a finish; heal windows | Maiden pools heal her; Nylocas wrong-style heals; Xarpus orbs heal |
| 16 | check the kit after every phase change | the switch sets re-checked every tick (Maiden, Nylocas boss, Sotetseg, Xarpus) |
| 20 | key on what is distinct | pools (tile, B), shadows (tile, first sight), landings (tile, L), nylos (slot, birth) |
| 22 | act on the predicted slot | Nylocas switch ticks, Xarpus turn ticks, Bloat's rise |
| 23 | make the hazard impossible for everyone | Sotetseg: nobody within 1 at a slot, so no melee |
| 25 | read the assignment, not the comment | the specs cite the code lines, not the comments |
| 27 | a standing mode is a frozen npc | Bloat's lap learned from his real walk |
| 28, 49 | measure the npc the way Blert does | PROBE rows; Maiden crab leak ticks, Bloat's lap |
| 29 | a trip is priced by its return | Bloat's last swing vs the stomp; Xarpus exhumed hand-offs |
| 30-46 | lane identity | nothing here may differ by lane: server tiles, server tick, named copies in `world_op`, no side channels |
| 48-54 | the crab calibration | Verzik only; its content is another session's |

## 9. What seed 1 showed (Maiden, run before this plan; recorded, not chased)

- B1 above: her 70% wave every tick, 3014 crabs.
- The leader started the room (t74) and took no damage; Protect from Magic held.
- The members crossed (they stood at x48, inside) but `click_loc` timed out
  waiting for a walk the teleport made: B2.
- With a crab target out of reach, every tile had the same flat reach cost,
  the plan stood and no order went out for 300 ticks: the gradient rule in 1.1.

## 10. Rulings made, and rulings for the owner

- **Sotetseg's maze shortcut**: nothing in the content stops the runner stepping
  south off the realm grid when its stun ends, which ends the maze unrun. The
  solver does NOT use it; it is a content gap (OSRS needs the portal), for the
  owner.
- **Sotetseg's run skipping**: the content checks end-of-tick tiles only; the
  wiki forbids skipping. The planner charges middles, so the solver obeys the
  wiki either way.
- **Prayer unlocks**: the kit sets them; the content does not check them. Add
  the gates (`[proc,prayer_checks]`)? Owner's call.
- **Maiden's freeze roll** (B3): the wiki's accuracy-roll rule (Void and Augury
  count) over the content's bonus-only curve -- the wiki outranks; to be fixed in
  the content with a selftest.
- **Nylocas wave 30 south**: our table spawns melee where the game spawns magic;
  the solver reads the npc id.
- **Maiden's crab walk target**: the content's SE tile; leak ticks are a probe.
