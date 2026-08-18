# Tombs of Amascut — full implementation plan

> **Status, 18 August 2026: steps 1-3 built and gated; steps 4-7 not started.**
>
> | Step | State |
> |---|---|
> | 1 instancing and geometry | **done** — all twelve squares, the three plane-1 rooms, Nexus-plus-one-room instancing, room/path/tile/music tables. `::toarooms` builds every one of them for real |
> | 2 invocations and scaling | **done** — the 44-row table generated out of the cache's own structs, the exclusive-category and prerequisite rules, and the raid/path/team factors. The generator and `::toarun` independently reconstruct the 600 ceiling |
> | 3 points and rewards | **done** as arithmetic — the ledger, the multiplier table with its three-down cap, the death penalty, the scaled raid level and the unique curve. Not yet wired to a chest |
> | 4 challenge rooms | not started |
> | 5 path bosses | not started |
> | 6 the Wardens | not started |
> | 7 Combat Achievements | not started |
> | 8 `::toarun` | **32 checks**, proven able to fail, wired into `mock230 --selftest` alongside `::toarooms` |
>
> **The selftest is proven able to fail**, twice and in the two places it
> matters. Changing one invocation's raid-level modifier from +50 to +45 was
> caught by two independent checks — `invocation 12 raid level 45` and
> `raid level ceiling is 585, wanted 600`. Adopting the reference
> implementation's team scaling (+90% for every extra member) was caught as
> `team factor 4-man is 370, wanted 340` and `Zebak 8-man is 4234, wanted 3364`,
> which are exactly the reference's own numbers.
>
> **Three things fell out of building it**, all recorded in §12:
>
> 1. **A script cannot ask `map_blocked` about a tile it teleported to in the
>    same script** — `[E1]`. Every tile within six of every ToA entry, in eleven
>    rooms on both planes, reads blocked, which looked like instances having no
>    collision. `::toaprobe`'s third control settles it the other way: standing
>    inside a freshly built room, the tile underfoot reads blocked and the
>    Lumbridge tile the player just left still reads **open**. The scene has not
>    moved. `mock230_scene_walk_blocked` answers "blocked" for anything outside
>    the one currently-built scene, and the scene is rebuilt on the tick
>    boundary rather than inside `p_teleport`. Nothing to do with instances, and
>    it decides how every room here has to be written.
> 2. **The 55% unique ceiling is unreachable on one player's points.** A solo
>    raider caps at 64,000 reward points, which at raid level 400 is 17.2%. The
>    cap binds only on a group's summed points.
> 3. **The Wiki's pickaxe table and Mod Ash's statement of the same rule
>    disagree at exactly Mining 99.** Ash wins — he is describing the code — so
>    an unboosted 99 takes the middle band.
>
> Original status line, kept because the plan below is still the plan:
> **research complete, nothing built.**
> This document is the plan; the evidence behind it is
> [`ENCOUNTERS.md`](ENCOUNTERS.md) (mechanics), [`ASSET_INDEX.md`](ASSET_INDEX.md)
> (every cache id) and [`SOURCES.md`](SOURCES.md) (where all of it came from,
> pinned). Nothing in this file invents a number: values are tagged `[cache]`,
> `[wiki]`, `[nr]`, `[toa-plugin]`, or `[Mn]` for a disclosed gap.

## 0. The three findings that shape the plan

**1. The cache already contains the entire raid.** All twelve map squares, 183
npcs, 1,357 locs, 86 items, 244 sequences, 946 sound effects, eleven music
tracks, ten interfaces, five inventories and 94 varbits are in `cache.osrs239`
and verified present. Unlike the RS2012 QBD port or the Summoning lane, **no
asset authoring, no map building and no interface authoring is required.** This
is a pure server-content job. [cache]

**2. The invocation system is cache data too.** All 46 invocations live in
`all.struct` with their index, category, raid-level modifier, numeric argument,
prerequisite and description (§ENCOUNTERS §0). Hand-authoring that table would
be the [exporter-owns-generated-configs](../../../CLAUDE.md) mistake in a new
place. **Generate it**, the way `tob_nylo.dbrow` is generated. The check that the
read is correct is arithmetic: one invocation from each of the four exclusive
categories plus every non-exclusive toggle sums to exactly **600**, the
documented ceiling.

**3. The Theatre of Blood is the template, and it is finished.**
`server/scripts/minigames/minigame_tob/` is 5 configs and 18 scripts, ~17.5 kLOC,
with a selftest proven able to fail. Everything ToA needs structurally — one
instance per room, a party in instance registers, a watchdog-driven progression,
a points-to-loot chest, a performance board, Combat Achievements through
`~ca_task_complete` — already exists there in a shape that worked. This plan
copies that shape deliberately and says so at each step.

**Where ToA is genuinely harder than ToB**, and therefore where the risk is:

* **Two instances are not enough.** ToB builds one room at a time. ToA's four
  paths are taken *in any order* and a party can be split across a challenge and
  a boss room; the reward room is a third. The instance pool is
  `MOCK230_MAPINSTANCE_MAX = 8`, which is enough, but the "one room at a time"
  simplification does not carry over. **§2 is the first real engineering task.**
* **Difficulty is a runtime parameter, not a mode constant.** ToB has three
  modes. ToA has 46 independent switches feeding one scalar, four independent
  path levels, and a team-size curve — and every npc in the raid reads all three.
  **§3 must land before any boss.**
* **Five distinct puzzle rooms**, four of which are pure logic with no combat
  analogue anywhere in this tree.

## 1. Deliverables and where they go

Following the ToB layout exactly:

```
OSRS-Content/osrs239-content/server/scripts/minigames/minigame_toa/
  configs/
    toa.constant          every id, tile, tick and threshold, each with a
                          provenance tag or an [Mn]
    toa.npc               overlay pinning moverestrict/huntmode for the raid npcs
    toa.varp              the party/instance registers this raid needs
    toa_invocation.dbtable / .dbrow    generated from all.struct, never hand-edited
    toa_apmeken.dbtable  / .dbrow      the eight-wave baboon table
  scripts/
    toa.rs2               entry points, lobby, the obelisk and the party board
    toa_raid.rs2          instancing, path selection, progression, exits
    toa_party.rs2         party formation, invocation selection, raid level
    toa_invocations.rs2   reading the generated table; applying each switch
    toa_scaling.rs2       raid level / path level / team size -> npc stats
    toa_points.rs2        the point ledger, multipliers, MVP, death penalty
    toa_supplies.rs2      Helpful Spirit, the three bundles, the supply bag
    toa_crondis.rs2  toa_zebak.rs2
    toa_scabaras.rs2 toa_kephri.rs2
    toa_het.rs2      toa_akkha.rs2
    toa_apmeken.rs2  toa_baba.rs2
    toa_wardens.rs2       all three phases
    toa_vault.rs2         sarcophagus, chests, scoreboard, books
    toa_rewards.rs2       the loot table
    toa_timing.rs2        the tick model as pure functions, checkable early
    toa_selftest.rs2      ::toarun, wired into mock230 --selftest
tools/
  toa_cache_dump.py       written; regenerates every sources/cache_*
  toa_fetch_wiki.py       written; re-pulls the pinned wiki pages and the CA list
  gen_toa_invocations.py  to write: all.struct -> toa_invocation.dbrow
```

## 2. Step 1 — instancing and room geometry

**Goal:** a party can enter the tombs, stand in the Nexus, walk into any of the
four challenge rooms and back, with each room a private instance.

Each room is a **whole map square** (§ASSET_INDEX §1), not a sub-rectangle, which
is simpler than ToB's per-room templates. Three rooms are on **plane 1** — Akkha,
both Warden squares — so every "which room is this player in" test keys on the
square **and the plane**, never x/z alone. This is the shape of bug that
[`bridge-deck-three-level-spaces`](../../../CLAUDE.md) already cost this tree
once.

Tasks:

1. `~toa_room_square(room)` / `~toa_room_entry(room)` / `~toa_room_fight_tile(room)`,
   modelled on `tob_room_square` and friends. The twelve squares and the
   reference's spawn tiles are in §ENCOUNTERS §0.
2. Decide the instance lifetime. ToB frees a room on the way out. ToA cannot, in
   general: **`[D1]`** — either keep the Nexus plus at most one path room live
   (cheap, but a party that splits across a challenge and its boss breaks), or
   keep the Nexus plus one instance per *path in progress* (up to five live, well
   inside the pool of 8). **Recommendation: the second.** The pool affords it and
   the first is a rule the game does not have.
3. Barriers and the entry gate per room; the challenge only starts when someone
   crosses, exactly as in ToB, so a boss standing idle is the correct behaviour
   before entry.
4. The teleport crystals: `toa_teleport_crystal_continue` **45137** between rooms
   and `_wardens` **45138** into the vault. [cache]
5. Music per room from §ENCOUNTERS §12 — eleven `midi_song` calls and the eleven
   unlock varp bits.

**Acceptance:** `::toarun` builds all eleven rooms, teleports through each in
turn, and reports the instance count never exceeding the chosen budget.

## 3. Step 2 — difficulty: invocations, raid level, path level, team size

**This gates every boss, so it comes before any of them.**

1. `tools/gen_toa_invocations.py` reads `all.struct`, selects the 46 structs with
   params 1159 and 1160, and writes `toa_invocation.dbrow`: index, struct id,
   name, category, raid-level modifier, numeric argument, prerequisite. Committed
   output; regenerate, never hand-edit. Skip the two Leagues rows (category 15).
2. Party state: the 46 invocation bits, four path levels, the raid level. The
   cache names the client half already — `toa_client_raid_level` **14380**,
   `toa_client_crondis/scabaras/het/apmeken_level` **14376–14379**,
   `toa_client_p0..p7` **14346–14353**, `toa_client_partyslot` **14354**.
   [cache]
3. Enforce the category rules on toggle: categories 3, 4, 5 and 6 are exclusive
   (one active at a time); `param_1346` names a prerequisite that must be on.
4. `~toa_raid_level` = sum of active `param_1162`. Mode from it: Entry 0–149,
   Normal 150–299, Expert 300+. [wiki] [toa-plugin]
5. `~toa_scale_hp / _damage / _accuracy / _defence`, from §ENCOUNTERS §0:

   ```
   levelFactor  = pathLevel > 0 ? 0.08 + (pathLevel - 1) * 0.05 : 0
   raidFactor   = 1 + (raidLevel / 5) * 0.02
   damage       = min(2.5, raidFactor + levelFactor)      // +150% cap
   accuracy     = raidFactor
   defence      = base * raidFactor
   teamFactor   = 1 + 0.9 * min(2, n - 1) + 0.6 * max(0, n - 3)
   hitpoints    = base * raidFactor * teamFactor * (1 + levelFactor)
   ```

   **The `teamFactor` line is the one place this plan knowingly departs from the
   reference implementation**, which uses 0.9 for every extra member. The wiki
   states 90% for the 2nd and 3rd and 60% for the 4th and beyond, and the wiki
   wins. Recorded in [`SOURCES.md`](SOURCES.md) §3.
6. Path levels: `Pathseeker` / `Pathfinder` / `Pathmaster` set all four to 1/2/3
   on entry (`param_1299` carries the number). `Walk the Path` grants four
   level-ups during the raid — two to random other paths after the first path is
   cleared, one after the second, one after the third — with the message *"You
   hear a mysterious rumbling coming from the Path of [god]."* [wiki]

**Acceptance:** `::toarun` reconstructs the 600 ceiling from the generated table,
checks each exclusive category rejects a second member, checks each prerequisite,
and checks the scaling formulas at raid level 0 / 150 / 300 / 400 / 500 / 600 and
team sizes 1 / 3 / 4 / 8 against hand-computed values in the test.

## 4. Step 3 — the raid frame: party, points, supplies, vault

Independent of any individual fight, and all of it has a ToB analogue.

1. **Party and lobby.** The Grouping Obelisk, the invocation board (interface
   **776**), the party list (**772**) and details (**774**). ToB's "walk in
   together" simplification is acceptable here too as a first cut; the invocation
   board is not optional, because the raid has no difficulty without it.
2. **The point ledger** (`toa_points.rs2`), from §ENCOUNTERS §0: start 5,000,
   room cap 20,000, total cap 64,000, the per-npc multiplier table, MVP
   `300 × teamSize`, death `max(20%, 1000)`, and the final subtraction of the
   starting 5,000. The multipliers must live in `toa.constant` keyed by npc id,
   not scattered through the boss scripts, because the Wardens' P2 multiplier is
   also **capped at three downs** and that cap belongs next to the number.
3. **The Helpful Spirit** after two paths (npc **11694**): three bundles, the
   eleven consumables, the supply bag (obj 27314, invs 807–811, interfaces 777
   and 778), and the three `Helpful Spirit` invocations scaling quantities to
   66% / 33% / 10% with a floor of one. §ENCOUNTERS §1.
4. **The vault**: sarcophagus and eight chests (varbits 14356–14360, 14370–14373;
   inv 811; interface 771), the Rewards Niche, the six books, the scoreboard
   (interfaces 775 and 482) with the eighteen title varbits.
5. **The loot table** (`toa_rewards.rs2`), §ENCOUNTERS §11 — the pre-roll dung
   under 1,500 points, the seven uniques with their raid-level-varying weights,
   three common rolls with the quantity formula, the tertiaries with their
   bad-luck mitigation, the jewel "guarantee an unowned one" rule, and the
   guaranteed challenge rewards.

**Acceptance:** `::toarun` drives a synthetic point ledger through a full raid
(four rooms, one death, one MVP) and checks the total; and rolls the loot table
10,000 times at raid levels 150 / 300 / 450 checking the unique rate against
`1% per (10500 - 20 × RL)` capped at 55%.

## 5. Step 4 — the four challenge rooms

Deliberately before the bosses: they are lower risk, they exercise the
instancing, and two of them (Het, Apmeken) feed points and a mechanic the bosses
reuse.

### 5.1 Path of Het — `toa_het.rs2`

The most mechanical and the best first room. Mirrors, barriers, the 9-tick beam,
Orbs of Darkness, the pickaxe statue tiers, and the 15-tick mining window with
its documented damage table. §ENCOUNTERS §6. Every loc id is in
§ASSET_INDEX §3.

`[M9]` The puzzle **generator** is not documented anywhere: the wiki says the
beam "can always be solved with 1–3 mirrors plus some barrier removal" but not
how a layout is drawn. The duckblade plugin contains a *solver*
(`features/het/solver/HetSolver.java`), which constrains the shape of a layout
but does not produce one. Ship a fixed set of hand-checked layouts, disclose it,
and leave the generator as an open task.

### 5.2 Path of Scabaras — `toa_scabaras.rs2`

Five puzzles, each fully specified in §ENCOUNTERS §4, and each with a solver in
the plugin that pins the rules. The cache ships the memory-game buttons and tiles
as individual locs (45356–45373), so the boards are placement, not geometry.
Solo starts four of nine matching pairs solved and moves the blocking statue.

### 5.3 Path of Crondis — `toa_crondis.rs2`

175 water units solo, +125 per player; the two trap families on a fixed cycle;
the halve-on-hit rule; the crocodiles' three-tier aggression and their flat
18/36 damage that ignores raid level. §ENCOUNTERS §2.

`[M10]` The trap **cycle period** is not stated anywhere. The wiki proves it is
fixed — a 32.4-second no-damage solo clear depends on it — but gives no number.
Ten `crondis_poison_tile_activate_*` sequences in the cache suggest ten phases;
that is an inference, not a measurement.

### 5.4 Path of Apmeken — `toa_apmeken.rs2`

The eight-wave table (§ENCOUNTERS §8) as a generated dbtable, the seven baboon
types with their fixed health and their "counter style always max hits" rule, and
the Apmeken's Sight quick-time layer: pillars, vents, corruption, with the
required count equal to party size and damage both for missing an action and for
performing an unneeded one.

## 6. Step 5 — the four path bosses

In this order, cheapest mechanic surface first:

| # | Boss | Why here |
|---:|---|---|
| 1 | **Zebak** | one attack loop, two alternating specials on fixed health thresholds, one enrage. No adds to schedule |
| 2 | **Ba-Ba** | five mechanics but all local; the boulder phase is a bounded generator with a documented constraint (cracked boulder moves ≤2 per column) |
| 3 | **Kephri** | the hardest scheduler in the raid — the swarm cadence is four batches at 4/3/2/1 ticks with a documented "only the first 18 of 28 arrive" |
| 4 | **Akkha** | four-quadrant state ×4 riders, four shadows charging independently, three specials every 7 attacks, and an enrage phase that is a different fight |

Each boss gets its own script, its numbers in `toa.constant`, and its own
`::toarun` section verifying the pure arithmetic (thresholds, cadences, damage
caps) **without an instance**, as `tob_maiden.rs2` and `tob_timing.rs2` do.

Open measurement tasks in this step, all disclosed rather than guessed:

* `[M2]` Spitting Scarab range — the strategies page says 8 tiles in one place
  and 10 in another.
* `[M3]` Akkha Memory Blast iteration count. The reference computes
  `4 + min(2, pathLevel / 2)`; no primary source states it.
* `[M11]` Zebak's Great Roar rock and jug **placement patterns**. The wiki
  describes the first two iterations qualitatively ("rocks in front, poison
  behind, solves to the sides"; "second shifts east") and guarantees at least one
  safespot jug, but gives no tile set.
* `[M12]` Kephri's shield-charge per swarm — "usually ~10% or lower for larger
  teams", with a ~115% overcharge cap. Not a number.
* `[M13]` Ba-Ba's Rock Throw damage, solo versus group. "Massive"; "less in a
  group".

## 7. Step 6 — the Wardens

Three phases, two rooms, and the largest single script in the raid.
§ENCOUNTERS §10.

* **P1** is a resource race, not a fight: the obelisk's 260 HP against the two
  Wardens' charge, with orb-blocking at 3 damage a hit deciding both the
  desynchronisation and **which Warden survives into P2**. Alternating
  UFOs → Charged Shot, with the solo/group damage caps stated per god.
* **P2** is the shield-and-core loop: 100% accuracy with the player's accuracy
  bonus converted to damage, the two-prayer rotation, `Divine Projectile` above
  55% and `Imprisonment` below, the three obelisk attack patterns with their
  colour telegraphs, and the core at ×5 damage with 21 / 29 / 37-tick exposures.
* **P3** is the slam loop, the four Energy Siphon batches with player-count-scaled
  health and layout, the two phantoms, and the enrage phase that eats the arena.

`[M14]` The Energy Siphon **time limit** — "dependent on group size and whether
`Insanity` is active" — has no stated value.
`[M15]` The obelisk's attack **pattern order** is "a set pattern, no repeat until
the set is exhausted"; the set is not enumerated.

## 8. Step 7 — Combat Achievements

51 tasks, ids **421–471**, through the tree's own `~ca_task_complete`, exactly as
ToB's 237–260 were done. Follow the ToB rule that the nine `Perfect ...` and four
`Perfection of ...` flags are **cleared by damage taken rather than set by its
absence**, so an unimplemented mechanic cannot award a task. Table:
[`sources/wiki_combat_achievements_toa.tsv`](sources/wiki_combat_achievements_toa.tsv).

## 9. Step 8 — `::toarun`

A selftest wired into `mock230 --selftest`, and — following the rule this tree
learned the hard way — **proven able to fail** by mutating a constant and
watching two independent checks catch it. Minimum coverage:

1. all twelve map squares resolve and the three plane-1 rooms report plane 1;
2. the generated invocation table reproduces the 600 ceiling, the four exclusive
   categories and the four prerequisites;
3. the scaling formulas at the raid levels and team sizes in §3;
4. the point ledger through a synthetic raid;
5. the loot table's unique rate at three raid levels;
6. the Apmeken wave census (8 waves, composition per §ENCOUNTERS §8);
7. Kephri's swarm cadence — that 28 spawns at 4/3/2/1-tick batches puts the 18th
   inside the window and the 19th outside;
8. Het's 15-tick window yielding 8 mining hits, and the pickaxe damage table;
9. the Warden core exposure thresholds and the ×5 multiplier;
10. every `[Mn]` in `toa.constant` still carries its tag — a test that fails if
    someone deletes a disclosure while keeping the guess.

## 10. Sequencing summary

| Step | Content | Gates |
|---:|---|---|
| 1 | instancing, twelve squares, barriers, music | everything |
| 2 | invocations, raid level, path level, scaling | every npc |
| 3 | party, points, supplies, vault, loot | the reward loop |
| 4 | Het → Scabaras → Crondis → Apmeken | — |
| 5 | Zebak → Ba-Ba → Kephri → Akkha | the Wardens (phantoms reuse them) |
| 6 | the Wardens, three phases | — |
| 7 | Combat Achievements | after every mechanic it tests |
| 8 | `::toarun` | grows with each step, not bolted on at the end |

Steps 4 and 5 are independent of each other once step 2 lands, so they can be
built in either order or in parallel; step 6 depends on step 5 because the P3
phantoms reuse the path bosses' behaviour and invocations.

## 11. What this plan deliberately does not do

* **Leagues invocations** (`Blazing Tombs I / II`, category 15, +200 each). Out
  of scope; the generator skips category 15 and says so.
* **The camel quest and the secret passage** (varbits 14441–14447, 14542) —
  content around the raid, not the raid.
* **Icthlarin's shroud tiers** — a completion-count accolade, trivially added
  once completions are counted, but not part of the raid loop.
* **The *Into the Tombs* miniquest framing**, which swaps Osmumten for Amascut in
  the Wardens' pre-fight dialogue. Ship the Osmumten path only.
* **Hard-coding a client**. Every interface, sprite and varbit the HUD needs is
  in the cache; if one does not arrive, the failure mode to look for first is the
  one this tree has hit repeatedly — a cache-authored hook that was never
  registered, baked, or was freed at runtime.

## 12. Open tasks, collected

| Tag | What is unknown | Where |
|---|---|---|
| `[D1]` | **decided** — the Nexus plus one room at a time. A party split across two paths is not modelled and `~toa_enter_room` says so | §2 |
| `[E1]` | **content constraint, not an engine defect.** `map_blocked` answers about the one currently-built scene, which is rebuilt on the tick boundary — so a walkability test in the same script as the teleport that arrived there always reads "blocked". Any mechanic that needs one (crocodile aggro, baboon waves, rolling boulders, the Crondis tile-skip) must run a tick later than the arrival. `::toaprobe`'s three controls are what establish this; re-run them before assuming otherwise | §2 |
| `[M1]` | puzzle-room completion point awards; only plugin estimates exist | §ENCOUNTERS §0 |
| `[M2]` | Spitting Scarab range: 8 or 10 tiles | §6 |
| `[M3]` | Akkha Memory Blast iteration count by path level | §6 |
| `[M4]` | Baboon Thrall hitpoints: cache says 2, wiki says 9 | §ENCOUNTERS §8 |
| `[M5]` | Tumeken's guardian pet rate by raid level | §ENCOUNTERS §11 |
| `[M6]` | ~900 named ToA sound effects with no known attack binding | §ENCOUNTERS §13 |
| `[M7]` | Warden P3 attack rate: cache says 5 / 1, wiki says 7 | §ASSET_INDEX §2.1 |
| `[M8]` | the varbit the invocation state is actually stored in | §ASSET_INDEX §8 |
| `[M9]` | Het mirror/beam **layout generator** | §5.1 |
| `[M10]` | Crondis trap cycle period | §5.3 |
| `[M11]` | Zebak Great Roar rock/jug placement patterns | §6 |
| `[M12]` | Kephri shield charge per swarm | §6 |
| `[M13]` | Ba-Ba Rock Throw damage, solo vs group | §6 |
| `[M14]` | Energy Siphon time limit | §7 |
| `[M15]` | the obelisk's P2 attack pattern set | §7 |

None of these blocks a step. Each one is a place where the implementation ships a
disclosed approximation with its tag attached to the constant, so that a later
measurement replaces a marked value rather than hunting for one.
