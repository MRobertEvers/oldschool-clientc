# Chambers of Xeric — encounter-by-encounter Near-Reality port plan

Companion to `COX_PLAN.md` (architecture, build order, §11 tick corpus, §13
parity pass) and `COX_MECHANICS.md` (the tick-level reference). This document is
narrower and more mechanical: **one pass per encounter, porting the
RSPS-NEAR-REALITY implementation into this tree**, with a named file list on
both sides so each pass can be picked up cold.

Written 2026-08-18, after the §13 assembly pass. That pass made all fifteen
rooms *reachable*; this one makes each of them *right*.

---

## 0. Brief and rules of engagement

**Brief.** Go encounter by encounter through the Chambers of Xeric. For each,
read the Near-Reality implementation in full, diff it against what this tree
does, and port the difference. Near-Reality is treated as correct. Where it
disagrees with an official source, the official source wins.

### 0.1 The precedence ladder

Apply top-down. A lower rung never overturns a higher one; it fills a gap the
higher rung is silent on.

| Rank | Source | Notes |
| --- | --- | --- |
| 1 | **Jagex statements** — Mod Ash / Mod Kieren quotes in `{{CiteTwitter}}` / `{{CiteDiscord}}` | These are the server's own constants. `tools/fetch_cox_wiki.sh` pulls them as raw wikitext because every rendered view collapses them to footnote markers. |
| 2 | **OSRS Wiki article text**, fetched `?action=raw` | Infoboxes, the Strategies pages, `Chambers_of_Xeric/Challenge_Mode`. |
| 3 | **Measured community data** — `COX_PLAN.md` §11.2 tick corpus, the two recorded-raid transcripts in `sources/`, RuneLite plugin constants in `sources/runelite/` | Measurement beats prose when the prose is vague ("a few ticks"). |
| 4 | **RSPS-NEAR-REALITY** (`~/Documents/git_repos/RSPS-NEAR-REALITY/near-reality-server-main`) | The default. Where 1–3 are silent, port it verbatim in behaviour. |
| 5 | This tree's existing code | Only where nothing above speaks. Never a reason to keep something 1–4 contradicts. |

**Extend the corpus first.** `tools/fetch_cox_wiki.sh` fetches 28 pages plus the
weirdgloop `monsters.json` attack-speed table (the only source stating a speed
for every CoX npc). Its page list has no entry for the meat tree, the raid fish,
the storage units or the lore books — add the pages an encounter needs to that
list at the start of its pass, so the corpus grows with the port rather than
being re-fetched ad hoc.

Near-Reality is a **Zenyte fork**; its only CoX-specific additions over stock
Zenyte are `ScalingMechanics.java` and `Raids1BypassTask.java`. Everything else
under `content/chambersofxeric` is Kris' Zenyte code, ~7k lines of room/map
plumbing plus ~9k lines of npc and Olm logic.

### 0.2 The four standing traps

These are not hypothetical — each one has already cost a pass in this tree.

1. **Deleted content.** Zenyte is a 2017–2019 codebase. `SMALL_SCAVENGER_RUNT`
   spawns a Scavenger runt, removed 31 Jan 2019; its historical ids 7546/7547
   are `ram_bartender` and `hunting_ojibway_trap_npc_off` in rev239, so the
   verbatim port puts a bartender in the raid. **Before porting any named npc,
   loc or item, check the wiki page for `{{Gone}}` and for a Changes section.**
2. **Structurally right, numerically stale.** Same lesson as Pyramid Plunder.
   The shape of a Zenyte encounter is nearly always correct; its constants may
   predate several balance passes. Port the shape, re-derive the numbers from
   rungs 1–3.
3. **Zenyte contradicts itself on projectiles.** `getProjectileDuration` and
   `getTime()` disagree — this was already caught for the ToB Maiden's storm,
   where the hit landed three ticks before the projectile drew. Any projectile
   ported from Zenyte needs its arrival time cross-checked against the Wiki's
   hit-delay table (`1 + floor((1+distance)/3)` for standard magic) or a
   measurement.
4. **Room-local vs instance-local addressing.** Every coordinate in a Zenyte
   room class is absolute in a static-region raid. In this tree a room is a
   stamped template at an arbitrary grid cell, so every one becomes
   `~cox_room_local(rx, rz, level, lx, lz)` or `~cox_here_local(lx, lz)`.
   Anything that reads absolute is correct only while the raid is one room —
   which is exactly the bug §13 found. **`npc_findallany` re-points the active
   npc, so hoist the room origin into a local before two lookups.**

### 0.3 What "port" means

Behaviour parity, not transliteration. Zenyte is an OO server with
`WorldTask`s, per-npc classes and a combat-script registry; this tree is
RuneScript over the mock230 engine with `[ai_timer]`, `[ai_queue]`,
`[ai_opplayer2]` and instance registers. The deliverable per encounter is:

- the same **tick schedule** (first attack tick, period, transition delays),
- the same **target selection** rule,
- the same **damage and prayer-multiplier** arithmetic,
- the same **spawn set, positions and counts** at every party scale,
- the same **death, cleanup and points** behaviour,

and a harness gate for each of those that can actually fail.

---

## 1. Where things stand

Line counts are a rough proxy for depth, not a target. The point of the table is
which encounters are thin.

| Encounter | Near-Reality | This tree | Read |
| --- | --- | --- | --- |
| Great Olm | 3,232 (`greatolm/` 25 files) | 1,026 (`cox_olm.rs2`) | Deepest on both sides; §11 already audited it. Port is *differential*. |
| Tekton | 558 (`Tekton.java` + 4 combat scripts + room) | 355 (`cox_tekton.rs2`) | Closest to parity of the combat rooms. |
| Thieving / creature keeper | 545 + `Bat` 180 + `CorruptedScavenger` 142 | ~55 of `cox_puzzles.rs2` | **Thinnest gap in the raid.** |
| Vasa Nistirio | 834 (`VasaNistirio` 607 + room 227) | 152 | Large gap. |
| Vespula | 803 (`Vespula` 321 + `AbyssalPortal` 139 + `LuxGrub` 179 + `VespineSoldier` 194 — room 288 on top) | 170 | Large gap; four interacting npcs. |
| Vanguards | 676 (`Vanguard` 302 + room 374) | 113 | Large gap; the sync/reset rule is the whole fight. |
| Muttadiles | 930 (`LargeMuttadile` 355 + `SmallMuttadile` 223 + `MeatTree` 64 + room 352) | 119 | Large gap. |
| Ice demon | 653 (`IceDemon` 296 + `IcefiendNPC` 56 + room 301) | 118 | Large gap. |
| Guardians | 451 (`RaidGuardianNPC` 244 + room 207) | 140 | Moderate. |
| Crabs (jewelled) | 494 (`JewelledCrab` 123 + `EnergyFocus` 206 + `Crystal` 80 + room 168 minus overlap) | 111 | Moderate. |
| Lizardman shamans | 488 (`LizardmanShaman` 220 + room 268) | ~60 of `cox_minions.rs2` | Large gap. |
| Skeletal mystics | 274 (`SkeletalMystic` 141 + `DarkAltarRoom` 133) | ~60 of `cox_minions.rs2` | Moderate. |
| Tightrope (deathly) | 202 (`DeathlyNPC` 85 + room 117) | ~35 of `cox_puzzles.rs2` | Moderate. |
| Scavenger rooms | 345 (`ScavengerRoom` 78 + `Large` 115 + `Small` 21 + `ScavengerBeast` 131) | 66 | Moderate. |
| Resource / farming | 493 (`ResourcesRoom` 171 + `skills/` 7 files) | 205 + `cox_herblore.rs2` 245 | Farming + herblore done; fishing/cooking/woodcutting absent. |
| Raid shell, map, points | 3,231 (`Raid`, `RaidArea`, `map/`, `Scaling`, point cap) | 443 + `cox_layout.rs2` 263 + `cox_points.rs2` 108 + `cox_scaling.rs2` 69 | Structure ported §13; lifecycle hooks thin. |
| Parties / storage / rewards / books | ~1,900 | `cox_rewards.rs2` 202, rest absent | §13.5 items 1–4. |

---

## 2. Source topology — the Near-Reality tree

Root: `~/Documents/git_repos/RSPS-NEAR-REALITY/near-reality-server-main/core/src/main/java/com/zenyte/game/content/chambersofxeric/`
(abbreviated **`NR/`** below; `NR-K/` is the Kotlin sibling under
`.../kotlin/com/zenyte/game/content/chambersofxeric/`).

```
NR/
  Raid.java                    1060  raid lifecycle, floors, party, points, timers
  RoomController.java            38  per-room tick dispatch
  ScalingMechanics.java         126  *** NR-specific: party scaling ***
  CombatPointCapCalculator.java  76  per-npc points cap
  RaidOverlay.java               36  the raid HUD
  ChambersStatisticsLogger.java      run logging
  Raids1BypassTask.java              *** NR-specific ***
  map/                          ~1900  RaidRoom, RaidArea, MapAlgorithm, RaidMap,
                                       RoomGeneration, MapPalette, BossChunk,
                                       WrapperChunk, ChunkDirection, RaidPattern,
                                       BossPattern, LayoutRoom, LayoutTypeRoom,
                                       RoomType, ChambersOfXericArea
  room/                         ~2900  one class per room type (18 files)
  npc/                          ~4200  one class per raid npc (24 files)
    combat/tekton/               ~450  Tekton's four combat scripts
  greatolm/                     ~3200  Olm, claws, room, and 15 attack scripts
  party/                         ~800  RaidParty + three interfaces
  storageunit/                   ~700  private/shared storage + interfaces
  rewards/                       ~600  reward tables and the rewards interface
  books/                         ~350  the seven lore books
  score/, parser/                      scoreboard, log model
NR-K/ChambersCommands.kt              debug commands
```

Two non-obvious entry points:

- **`map/RaidRoom.java`** is the room roster *and* the template address table.
  Its `(staticChunkY, height)` column decodes onto this cache's templates:
  chunk→tile ×8, tile→square ÷64, a room is 4 chunks. Its three x values
  (408/412/416) are the CCW/THRU/CW variant triple.
- **`RoomController.java`** is only 38 lines but it is the seam every room class
  hangs off; read it before any room class.

---

## 3. Target topology — this tree

Root: `OSRS-Content/osrs239-content/server/scripts/minigames/minigame_cox/`

```
configs/cox.constant   958  ids, tables, tick constants
configs/cox.npc        724  authored npc blocks (stats, flags, anims)
configs/cox.varp       412  raid + per-encounter variables
scripts/
  cox.rs2              443  raid shell, instance build, entry/exit
  cox_layout.rs2       263  floor generation, room homes, populate
  cox_scaling.rs2       69  party scaling
  cox_points.rs2       108  points and caps
  cox_rewards.rs2      202  reward rolls
  cox_tekton.rs2       355
  cox_olm.rs2         1026
  cox_vasa.rs2         152
  cox_vespula.rs2      170
  cox_vanguards.rs2    113
  cox_muttadiles.rs2   119
  cox_guardians.rs2    140
  cox_minions.rs2      136  shamans + mystics
  cox_icedemon.rs2     118
  cox_crabs.rs2        111
  cox_puzzles.rs2      114  tightrope + thieving
  cox_scavengers.rs2    66
  cox_resource.rs2     205  farming/gourd/geyser/energy well
  cox_herblore.rs2     245
  cox_bats.rs2          90  hunter
  cox_selftest.rs2     879  the gate suite
```

Adjacent, outside the lane:
`server/scripts/player/scripts/consumption/cox_potion.rs2` (drinking raid
potions), `server/scripts/player/configs/consumption/cox_potion.varp`.

Tooling: `tools/cox_verify.sh` (39 checks), `tools/cox_sim.sh` (the tick loop),
`tools/cox_check_timers.py`, `tools/cox_template_survey.py`,
`tools/cox_sprite_sheets.sh`, `tools/fetch_cox_wiki.sh`,
`tools/cox_compile_check.sh`.

---

## 4. Port order

Infrastructure first — every encounter pass depends on it, and porting an
encounter onto the wrong scaling or points seam means porting it twice.

| Stage | Content | Why here |
| --- | --- | --- |
| **A** | Shared infrastructure: scaling, point caps, room lifecycle, the raid-npc base behaviours | Every encounter reads these. |
| **B** | Combat rooms, ascending difficulty: Scavengers → Guardians → Skeletal mystics → Lizardman shamans → Vanguards → Muttadiles → Vasa → Vespula → Tekton | Tekton last of the combat set: it is closest to parity, so it is the cheapest re-verification of stage A. |
| **C** | Puzzle rooms: Tightrope → Crabs → Ice demon → Thieving | Thieving last; it is the largest single room class in the source. |
| **D** | Resource room remainder: fishing, cooking, woodcutting, antipoison | Independent of everything else; can run in parallel with B/C. |
| **E** | Great Olm — differential pass against `greatolm/` | Deepest on both sides; wants stage A settled and the trace channels quiet. |
| **F** | Non-encounter systems: parties, storage units, reward chest, books, relic, talisman, pets, floor progression | §13.5. Not encounters; scheduled after so a raid can be *cleared* before it can be *farmed*. |

Each stage-B/C/E pass is the same seven steps:

1. **Read** every NR file in the dossier, end to end. Not grep — the tick
   arithmetic hides in `processNPC` bodies.
2. **Extract** a mechanic table: spawn set, positions, counts per scale, attack
   period, first-attack tick, target rule, projectile ids/heights/durations,
   prayer multipliers, transition triggers, death/cleanup, points.
3. **Cross-check** every number in that table against rungs 1–3. Record each
   disagreement in a numbered correction row (the `P#` convention from §13.2).
4. **Check for deleted/changed content** on every named id.
5. **Port**, converting absolute coordinates to room-local and `WorldTask`s to
   `[ai_timer]`/`[ai_queue]`/instance registers.
6. **Gate**: add checks to `cox_selftest.rs2`, and mutation-prove each new gate
   — break the constant, watch the check go red, restore it. A gate that cannot
   fail is not a gate.
7. **Run** `tools/cox_verify.sh`, `tools/cox_sim.sh`, `tools/cox_check_timers.py`,
   plus a live teleport into the room *at its centre* (a 32-tile room's corner
   is outside every notice range, which reads identically to a dead `ai_timer`).

---

## 5. Encounter dossiers

The file list, per room. Paths are relative to the two roots in §2 and §3.

### 5.0 Shared infrastructure (stage A)

| Side | Files |
| --- | --- |
| NR | `Raid.java`, `RoomController.java`, `ScalingMechanics.java`, `CombatPointCapCalculator.java`, `RaidOverlay.java`, `npc/RaidNPC.java`, `map/RaidArea.java`, `map/RaidRoom.java`, `map/MapChunk.java`, `map/RoomGeneration.java`, `map/MapAlgorithm.java`, `map/RaidMap.java`, `map/MapPalette.java`, `map/RoomType.java`, `map/LayoutRoom.java`, `map/LayoutTypeRoom.java`, `map/BossChunk.java`, `map/WrapperChunk.java`, `map/ChunkDirection.java`, `map/RaidPattern.java`, `map/BossPattern.java`, `map/ChambersOfXericArea.java` |
| Ours | `scripts/cox.rs2`, `scripts/cox_layout.rs2`, `scripts/cox_scaling.rs2`, `scripts/cox_points.rs2`, `configs/cox.varp`, `configs/cox.constant` |

What to port: `RaidNPC`'s shared behaviours (xp modifier, points multiplier,
freeze immunity flags, `isEntityClipped`), the point cap per npc type from
`CombatPointCapCalculator`, and `ScalingMechanics`' party curves. Confirm the
scaling formulas against the wiki's published `Chambers_of_Xeric` scaling
section first — this is NR's own addition and therefore the least
wiki-corroborated file in the tree.

Also settle here: whether room entry/exit, per-room "cleared" state and the
overlay update belong in `~cox_room_populate` or a new per-room tick proc
mirroring `RoomController`.

### 5.1 Tekton — `TEKTON(660, 1)`

| Side | Files |
| --- | --- |
| NR | `room/TektonRoom.java`, `npc/Tekton.java`, `npc/combat/tekton/TektonScript.java`, `TektonCombatScript.java`, `TektonEngageScript.java`, `TektonRetreatScript.java`, `TektonSmithScript.java` |
| Ours | `scripts/cox_tekton.rs2`, `configs/cox.npc` (`raids_tekton_*` five forms), `configs/cox.constant` |
| Official | `Tekton` wiki page (the Enraged section is on the same page), `COX_MECHANICS.md` §3 |

Focus: §3.1 records that sources conflict on attack timing — resolve it with
NR's `processNPC` as a third data point, but the wiki wins. The anvil cycle
(`TektonSmithScript`, `setOnceReturnedToAnvil`) and the melee prayer multiplier
are the two things this tree's version is most likely to have approximated.
`isRoomEmpty` / `resetToDefaultState` is the "everyone left the room" path we
have no equivalent for.

### 5.2 Vasa Nistirio — `VASA_NISTIRIO(660, 0)`

| Side | Files |
| --- | --- |
| NR | `room/VasaNistirioRoom.java`, `npc/VasaNistirio.java`, `npc/Crystal.java` |
| Ours | `scripts/cox_vasa.rs2`, `configs/cox.npc`, `configs/cox.constant` |
| Official | `Vasa_Nistirio`, `Glowing_crystal` wiki pages |

Focus: the crystal cycle is a state machine —
`refreshNextCrystal` → `walkToCrystal` → `sendProjectiles` → `sendExplosions`,
plus `teleportTargets` / `getNearbyLocation` / `getDistantLocation` for the
teleport-and-siphon phase and `walkToCenter` / `sendExplosionAttack` for the
opening. `retrieveTargets` is the target-set rule. Our 152 lines almost
certainly collapse several of these into one timer.

### 5.3 Vespula — `VESPULA(660, 2)`

| Side | Files |
| --- | --- |
| NR | `room/VespulaRoom.java`, `npc/Vespula.java`, `npc/AbyssalPortal.java`, `npc/LuxGrub.java`, `npc/VespineSoldier.java` |
| Ours | `scripts/cox_vespula.rs2`, `configs/cox.npc`, `configs/cox.constant` |
| Official | `Vespula`, `Abyssal_portal`, `Vespine_soldier`, `Cavern_grubs` wiki pages |

Focus: four interacting npcs. `LuxGrub.isCycleHealable` / `feed` / `getSoldier`
is the grub→soldier promotion; `Vespula.getFarthestUnstingedGrub` and
`isSkipStinging` are the sting target rule; `AbyssalPortal.getPossibleTargets`
and `getPillarObject` tie the portal to its pillar loc. Note from §13: Vespula's
2-tick portal is one of the two encounters that flooded the shared trace varp —
give it a private channel as part of this pass.

### 5.4 Vanguards — `VANGUARD(664, 0)`

| Side | Files |
| --- | --- |
| NR | `room/VanguardRoom.java`, `npc/Vanguard.java` |
| Ours | `scripts/cox_vanguards.rs2`, `configs/cox.npc`, `configs/cox.constant` |
| Official | `Vanguard` wiki page, `COX_MECHANICS.md` §7 |

Focus: the health-desync reset is the entire encounter, and it lives in the
room class, not the npc. `getRandomTiles` / `addWalkStep` /
`isPathfindingEventAffected` is the rotation walk. Three prayer multipliers
(`getMagic/Melee/RangedPrayerMultiplier`) plus `setTransformation` — check each
against the wiki's stated 50% reduction rules.

### 5.5 Muttadiles — `MUTTADILES(664, 1)`

| Side | Files |
| --- | --- |
| NR | `room/MuttadileRoom.java`, `npc/LargeMuttadile.java`, `npc/SmallMuttadile.java`, `npc/MeatTree.java` |
| Ours | `scripts/cox_muttadiles.rs2`, `configs/cox.npc`, `configs/cox.constant` |
| Official | `Muttadile` wiki page, `COX_MECHANICS.md` §8 |

Focus: `stopFeeding` / `setUnleashed` / `iteratePath` / `getMovementDirection`
and `isAcceptableTarget` — the meat-tree heal, the leash, and the
"large one only unleashes when the small dies or you cross the line" rule.
`MeatTree` is 64 lines and is a loc-plus-heal in our terms.

### 5.6 Lizardman shamans — `LIZARDMEN_SHAMAN(656, 0)`

| Side | Files |
| --- | --- |
| NR | `room/LizardmanShamanRoom.java`, `npc/LizardmanShaman.java` |
| Ours | `scripts/cox_minions.rs2` (`~cox_shaman_count`, `~cox_spawn_shamans`, `raids_lizardshaman_a/b`), `configs/cox.npc` |
| Official | `Lizardman_shaman_(Chambers_of_Xeric)` wiki page, `COX_MECHANICS.md` §9 |

Focus: the spawn special (`BlockingSpawn`, `RSPolygon` — the spawn is a *shape*,
not a radius) and `handleOutgoingHit` / `isTolerable`. **Do not adopt NR's
`getShamanCount`** — §13.2 P9 already recorded that the wiki's table runs to 5
and Zenyte's stops at 4; this tree's existing count is correct.

### 5.7 Skeletal mystics — `DARK_ALTAR_ROOM(656, 1)`

| Side | Files |
| --- | --- |
| NR | `room/DarkAltarRoom.java`, `npc/SkeletalMystic.java` |
| Ours | `scripts/cox_minions.rs2` (`~cox_mystic_count`, `~cox_spawn_mystics`, `raids_skeletonmystic_a/b/c`), `configs/cox.npc` |
| Official | `Skeletal_Mystic` wiki page, `COX_MECHANICS.md` §10 |

Focus: `isToxinImmune`, `onFinish`, the magic/melee prayer multipliers and the
`isEntityClipped` line-of-sight rule. `DarkAltarRoom` also owns the altar loc.

### 5.8 Guardians — `GUARDIANS(656, 2)`

| Side | Files |
| --- | --- |
| NR | `room/GuardiansRoom.java`, `npc/RaidGuardianNPC.java` |
| Ours | `scripts/cox_guardians.rs2`, `configs/cox.npc`, `configs/cox.constant` |
| Official | `Guardian_(Chambers_of_Xeric)` wiki page, `COX_MECHANICS.md` §4 |

Focus: `flinch`, `stomp`, `getDamageModifier` (pickaxe-only damage),
`getPointsMultiplier`, and the `ForceMovement` on the stomp. §13.2 P2 already
moved this room to cell row 0; the gate exists (check 37) — leave it.

### 5.9 Jewelled crabs — `CRAB_PUZZLE(668, 2)`

| Side | Files |
| --- | --- |
| NR | `room/CrabPuzzleRoom.java`, `npc/JewelledCrab.java`, `npc/EnergyFocus.java`, `npc/Crystal.java` |
| Ours | `scripts/cox_crabs.rs2`, `configs/cox.npc`, `configs/cox.constant` |
| Official | `Jewelled_Crab`, `Chambers_of_Xeric/Strategies` (crabs section), `COX_MECHANICS.md` §12 |

Focus: `EnergyFocus.advanceDirection` / `atCrystal` / `checkPlayers` /
`getGraphics` is the beam walk and is the half our 111 lines are thinnest on.
`JewelledCrab.isEquippableHammer` / `smash` / `setTime` is the stun. The colour
mapping we already have (`~cox_crab_colour_for_style`).

### 5.10 Tightrope — `DEATHLY_ROOM(668, 1)`

| Side | Files |
| --- | --- |
| NR | `room/DeathlyRoom.java`, `npc/DeathlyNPC.java`, `npc/Crystal.java` |
| Ours | `scripts/cox_puzzles.rs2` (`~cox_spawn_tightrope`, `~cox_tightrope_step_on`, `raids_tightrope_barrier`, `~cox_tightrope_clear_guards`), `configs/cox.npc` |
| Official | `Deathly_ranger`, `Deathly_mage`, `Chambers_of_Xeric/Strategies` (tightrope), `COX_MECHANICS.md` §13 |

Focus: the deathly mage/ranger prayer multipliers, the `RenderAnimation` and
`ForceTalk` flavour, and the crystal at the far end. Short port.

### 5.11 Ice demon — `ICE_DEMON(668, 0)`

| Side | Files |
| --- | --- |
| NR | `room/IceDemonRoom.java`, `npc/IceDemon.java`, `npc/IcefiendNPC.java` |
| Ours | `scripts/cox_icedemon.rs2`, `configs/cox.npc`, `configs/cox.constant` |
| Official | `Ice_demon` wiki page, `COX_MECHANICS.md` §11 |

Focus: `getStage`/`setStage` is the kindling→burn→fight ladder;
`handleIngoingHit` / `removeLifepoints` is the fire-spell requirement;
`canAttack` / `isAttackable` gate the pre-lit phase. The icefiend is the other
encounter that was overwriting Olm's trace varp — private channel here too.

### 5.12 Thieving / creature keeper — `CREATURE_KEEPER(672, 0)`

| Side | Files |
| --- | --- |
| NR | `room/CreatureKeeperRoom.java` (545 lines — the largest room class), `npc/CorruptedScavenger.java`, `npc/Bat.java` |
| Ours | `scripts/cox_puzzles.rs2` (`~cox_keystone_obj`, `~cox_thieving_*`, `raids_thievingchest_beast_active`), `configs/cox.constant` |
| Official | `Ancient_chest`, `Keystone_crystal`, `Corrupted_scavenger`, `Chambers_of_Xeric/Strategies` (thieving), `COX_MECHANICS.md` §14 (§14.1 solved the exact chest positions) |

Focus: **the biggest single gap in the raid.** The chest set, the trough, the
grub deposit loop, the bat/scavenger punishment, the `ChestLootingAction`
per-chest state and the party-scaled requirement. Our §14.1 chest positions are
already measured — keep them over anything NR says.

### 5.13 Scavenger rooms — `SMALL_SCAVENGER_RUNT(652, 0)`, `LARGE_SCAVENGER_BEAST(652, 1)`

| Side | Files |
| --- | --- |
| NR | `room/ScavengerRoom.java`, `room/SmallScavengerRoom.java`, `room/LargeScavengerRoom.java`, `npc/ScavengerBeast.java` |
| Ours | `scripts/cox_scavengers.rs2`, `configs/cox.npc`, `configs/cox.constant` |
| Official | `Scavenger_beast` wiki page; **`Scavenger_runt` is `{{Gone}}`** |

Focus: §13.2 P1 and P3 already landed (both rooms hold beasts; one bone plus two
rolls on an 18-weight table). What remains is `LargeScavengerRoom`'s
`BlockingObject` layout and whatever supply-crate behaviour the room class
carries beyond the drop table.

### 5.14 Resource room — `RESOURCES_A(680, 0)`, `RESOURCES_B(680, 1)`

| Side | Files |
| --- | --- |
| NR | `room/ResourcesRoom.java`, `skills/RaidFishing.java`, `skills/RaidFish.java`, `skills/RaidWoodcutting.java`, `skills/RaidFarming.java`, `skills/RaidRakingAction.java`, `skills/GourdPicking.java`, `skills/RaidHerblore.java`, `npc/CaveSnake.java`, `npc/Bat.java` |
| Ours | `scripts/cox_resource.rs2`, `scripts/cox_herblore.rs2`, `scripts/cox_bats.rs2`, `configs/cox.constant` |
| Official | `Chambers_of_Xeric` (resources), `Overload_(Chambers_of_Xeric)`, Mod Ash quotes on Xeric's aid and overload |

Focus: §13.5 items 5 and 6 — fishing (seven fish tiers), cooking (`4 + 8×tier`
points), woodcutting, and the antipoison recipe cell. Farming, gourd picking,
raking and the herblore grid are already built and gated; **the herblore
research in §13 outranks NR's** (tier ladders 47/59/70, 52/65/78, 60/75/90;
overload 500 ticks; 50 damage as five hits of 10) — do not let a port regress
those. `CaveSnake` is the fishing-spot hazard we have no equivalent for.

### 5.15 The Great Olm

| Side | Files |
| --- | --- |
| NR | `greatolm/GreatOlm.java`, `OlmRoom.java`, `GreatOlmClaw.java`, `LeftClaw.java`, `RightClaw.java`, `ClawCombatHandler.java`, `OlmCombatScript.java`, `OlmAnimation.java`, `CrystalCluster.java`, `FireWallNPC.java`, `AcidPool.java`, and `greatolm/scripts/`: `StandardAttack`, `Sphere`, `Swap`, `Lightning`, `CrystalBurst`, `CrystalBomb`, `FallingCrystal`, `TransitionalFallingCrystals`, `AcidSpray`, `AcidDrip`, `DeepBurn`, `FireWall`, `LifeSiphon`, `LeftClawProtection` |
| Ours | `scripts/cox_olm.rs2`, `configs/cox.npc` (`olm_head`, `olm_head_spawning`, `olm_hand_left/right`, spawning forms), `configs/cox.varp`, `configs/cox.constant` |
| Official | `Great_Olm`, `Perfect_Olm_(Solo)`, `Chambers_of_Xeric/Challenge_Mode`, `COX_MECHANICS.md` §2, `COX_PLAN.md` §11.2 tick corpus |

Focus: this is a **differential** pass, not a rewrite. Our implementation was
already audited against the tick corpus in §11 and is the most-tested thing in
the lane. Read the fifteen attack scripts one by one and diff each against the
matching `~cox_olm_*` proc; adopt NR only where §11 left a `🔧` or a gap.
`LeftClawProtection` and `ClawCombatHandler` are the clearest candidates —
per-claw damage routing is thinner on our side. Apply trap 3 to every projectile
in `scripts/`.

### 5.16 Rooms with no encounter

| Room | NR | Ours |
| --- | --- | --- |
| `RAID_START(648, 0)` | `room/EntranceRoom.java` | `scripts/cox.rs2` |
| `FLOOR_END_DOWNSTAIRS(644, 0)`, `FLOOR_START_UPSTAIRS(712, 0)` | `room/FloorEdgeRoom.java` | **absent** — §13.5 item 7, floor progression |

`FloorEdgeRoom` is 36 lines and closes the "nothing walks a player from floor 1
to floor 2 to Olm" gap. Cheap, high value, do it in stage A.

---

## 6. Non-encounter systems (stage F)

| System | NR files | Ours |
| --- | --- | --- |
| Parties | `party/RaidParty.java`, `RaidPartyInterface.java`, `RaidingPartyInterface.java`, `RaidingPartiesInterface.java` | absent |
| Storage units | `storageunit/Storage.java`, `StorageUnit.java`, `StorageInterface.java`, `StorageInventoryInterface.java`, `PrivateStorageInterface.java` | absent |
| Rewards | `rewards/RaidRewards.java`, `RaidReward.java`, `RaidNormalReward.java`, `RaidRareReward.java`, `ChallengeRaidNormalReward.java`, `RaidRewardsInterface.java` | `cox_rewards.rs2` (rolls exist, chest not wired) |
| Books | `books/` × 7 (`TektonsJournal`, `NistiriosManifesto`, `VanguardJudgement`, `HoundmastersDiary`, `CreatureKeepersJournal`, `DarkJournal`, `TransdimensionalNotes`) | absent |
| Talisman / relic / pets | `plugins/item/XericsTalisman.java`, `XericsWisdomItem.java`, `plugins/renewednpc/ChambersOfXericBossPetNPC.java` | absent |
| Overlay / score / logging | `RaidOverlay.java`, `score/Scoreboard.java`, `score/Score.java`, `ChambersStatisticsLogger.java`, `parser/` × 7 | partial |
| Debug | `NR-K/ChambersCommands.kt` | `::coxolm`, `::coxvasa`, … in the encounter files |

Reward beam colours and the unique table are already researched in
`COX_PLAN.md` §5 — that research outranks NR's tables.

---

## 7. Verification protocol

Per encounter, before the pass is called done:

1. `sh tools/cox_compile_check.sh` — compiles.
2. `sh tools/cox_verify.sh` — all checks green, including the new ones.
3. **Every new gate mutation-proved.** Break the constant it reads, confirm red,
   restore. Record the mutation in the pass write-up.
4. `sh tools/cox_sim.sh` — the tick loop, with the encounter's own trace channel
   read rather than the shared `%cox_trace_action` (which is last-writer-wins
   and useless in a populated raid).
5. `python3 tools/cox_check_timers.py` — no `ai_timer` left unarmed.
   An unarmed timer is the single most common silent death in this tree.
6. **A live run**: teleport into the room *at its centre*, watch a full cycle,
   kill it, confirm cleanup and points.

Two assertions worth adding globally, learned from the ToB:

- Any tile-scan gate must be proven non-vacuous — a scan that can only return
  zero looks exactly like a passing test.
- Any boss that `npc_changetype`s needs `hitpoints` stated on **every** form,
  and heals clamped to the *scaled* max.

---

## 8. Risk register

| # | Risk | Mitigation |
| --- | --- | --- |
| R1 | Porting a stale constant from a 2017 codebase | Precedence ladder §0.1; re-derive every number from rungs 1–3. |
| R2 | Porting deleted content | Check `{{Gone}}` and the Changes section for every named id. |
| R3 | Absolute coordinates surviving the port | Grep each new file for `~cox_coord` and template-absolute literals; the room-local helpers are `~cox_room_origin_of`, `~cox_room_local`, `~cox_here_local`. |
| R4 | Zenyte's self-contradicting projectile timing | Cross-check arrival against the wiki hit-delay table or a measurement. |
| R5 | Trace-varp collisions as more encounters tick | Private channel per encounter, allocated as part of each pass. |
| R6 | `MOCK230_MAPINSTANCE_VARS` exhaustion | The ToB already hit this at 64 and it was raised to 128. Fifteen CoX rooms with private registers will approach it — budget register ranges per room *before* stage B, not during. |
| R7 | Engine gaps surfacing mid-pass (as four did during the ToB) | Expect them. An engine fix is in scope for a pass; work around it only with a written reason. |
| R8 | "None of this has been played" (§13.5) | Stage F's floor progression makes a clearable raid possible; schedule one full manual clear before declaring the port done. |

---

## 9. Deliverable per pass

A section appended to this document, in the shape of §13.2/§13.3:

- a numbered correction table (what NR had / what the official source says / where),
- what was built,
- the gates added and how each was mutation-proved,
- what is still missing.

---

## 10. Pass 1 — stage A, and the position port — 2026-08-18

Branch `worktree-cox-nr-port`. Written in the format §9 asks for.

### 10.1 The finding that shaped the pass

**No encounter could know which room it was in.** Every CoX room ships in three
authored door variants and `~cox_layout_build_floor` picks one when it stamps —
then discarded the choice. `~cox_layout_populate_floor` called
`~cox_room_populate(room, rx, rz, level)` with no variant, so nothing that
populated a room could name a spawn tile: the same `(lx, lz)` is a wall in one
layout and open floor in another.

The consequence was that **every encounter added its whole pack at the room's
south-west corner** — four shamans deep on one tile, two guardians inside each
other, Vespula on top of her own portal. Nothing was red. A stack of npcs is a
legal state, they were all alive, and each fight ran; the guardians' entire
mechanic — walk between them, get stomped — simply had no gap to walk into.

This is the same shape as §13.1's "the raid was one room": a subsystem whose
output nothing checks, failing in a way that is not an error anywhere.

### 10.2 The conversion is exact, and that is the enabling fact

`RaidArea.getLocation` subtracts `staticChunkX * 8` and `staticChunkY * 8`
before use, `staticChunkY` is published per room in `RaidRoom.java`, and the
three `staticChunkX` values 408 / 412 / 416 **are** the CCW / THRU / CW
variants. So a Zenyte absolute coordinate is a room-local tile, and it belongs
to whichever of the three bases puts it inside the 32×32 box.

`tools/cox_nr_locs.py` does that conversion and refuses to guess: if no base
fits, or more than one does, it says so. Across all 17 room classes the dump
(`sources/nr/room_coords.txt`) has two ambiguous entries — both polygon corners,
resolvable by field index — and eight non-hits, all explained.

`tools/cox_template_survey.py --find <loc>` is the other half: where a named loc
sits inside its template cell **in the cache we ship**. That gives the port a
corroboration step, and it immediately earned its place (P4 below).

### 10.3 Corrections made against the sources

| # | What the tree had | What the source says | Where |
| --- | --- | --- | --- |
| P1 | Every pack added at the room corner | Zenyte authors per-variant positions for all of them | ten spawn procs |
| P2 | **The crab room spawned nothing at all.** `~cox_room_populate` listed it among rooms that "hold no npcs at load", on the reasoning that the crabs appear when the crystal is struck | They do not — the crabs are in the room from the start and are what the beam bounces off. The only `npc_add` for a crab was in a debugproc, so a generated crab room was empty with an unsolvable puzzle in it | `cox_crabs.rs2`, `cox_layout.rs2` |
| P3 | Vasa lit the same corner crystal every time | "will run to **one of the four** corners" — the other three were decorative and the room was a fixed read | `cox_vasa.rs2`, `%cox_vasa_crystal_index` |
| P4 | One anvil tile served all three Tekton variants | `--find raids_tekton_anvil` reads (14,22) / (7,13) / (12,22) out of m51_82 and m52_82. The single value was the CCW one, so two layouts in three sent him to hammer empty floor — and `npc_walk` to an empty tile succeeds | `cox.constant`, `cox_tekton.rs2` |
| P5 | The meat tree was one hardcoded tile | Per-variant. In two layouts of three the large Muttadile walked to open water to feed, and ate, because nothing on the feeding path checks a tree is there | `cox_muttadiles.rs2` |
| P6 | Four icefiends at every scale | Zenyte: `min(party, 4)`. Each guards **its** brazier, so a solo raid contests one, not four | `cox_icedemon.rs2` |
| P7 | Rangers and mages shared one count | They scale on **separate** ladders — rangers step at 4 and 7, mages at 4 and above 9 — so a team of eight meets four rangers and three mages. One shared count is wrong for one of the two at every scale from 7 to 9. The wiki's "groups of 2-4" bounds both | `cox_puzzles.rs2` |
| P8 | Shamans and mystics spawned only the `_a` id | Zenyte rolls across the id set per spawn (`random(7604, 7606)` for mystics). One id made every pack move in lockstep | `cox_minions.rs2` |
| P9 | Zenyte's mystic count caps at 6, because it *removes* tiles from a list of six | The official table runs to 12. Kept the official count and cycled the positions, with the derived tiles marked as ours | `cox_minions.rs2` |
| P10 | `ScalingMechanics`' stat multipliers | **Not adopted.** Its normal-mode multiplier is 0.8, so its base stats are calibrated 1.25× — porting the shape onto this tree's wiki-calibrated bases would make every monster 0.8× wrong at solo, contradicting the Tekton page's stated 300 | unchanged |
| P11 | Zenyte's `getShamanCount` (`<4→2, ≤7→3, else 4`) | Unchanged from §13.2 P9: the official table ends at 5 and this tree already matched it | unchanged |

**A provenance note worth recording.** `cox_scaling.rs2` attributes its formulas
to the wiki's Talk page. That page was re-fetched this pass and contains none of
them — it is four unrelated threads and a stack of feedback templates. The
formulas may have been there once; today they are uncorroborated. They were left
in place (nothing better exists, and they at least anchor solo exactly) but the
citation should not be read as live.

### 10.4 What was built

- `~cox_room_variant_at(rx, rz, level)` recovers the variant from the cell, and
  `~cox_here_variant` from wherever an npc is standing — so an `[ai_timer]` can
  ask too, which matters because an encounter needs its geometry long after the
  tick that spawned it.
- `~cox_pack_local` / `~cox_room_packed` carry a spawn table as single ints.
  Two parallel x/z tables that drift place an npc on a tile no author wrote.
- Per-variant positions for **ten rooms**: shamans, mystics, guardians, Tekton's
  anvil, Vespula (plus her portal, four grubs and four soldier spawns), the
  three Vanguards, both Muttadiles and the meat tree, Vasa and his four
  crystals, the ice demon with four braziers and their icefiends, the tightrope
  pairs, and the crabs with their light focus and crystal.
- `tools/cox_nr_locs.py`, `--find` on the template survey, and the full dump.

### 10.5 The harness was reporting false passes

Mutating `^cox_points_per_damage` from 5 to 7 — the exact mutation whose escape
is documented in `mock230_world.c` beside the `::coxrun` check — left
`tools/cox_verify.sh` **green**. So did collapsing a spawn table.

The cause was not in the checks. `mock230 --selftest` was segfaulting in the
collision section hundreds of lines earlier, because a fresh worktree has no
`cache.osrs239` (an untracked artifact at the repo root). No scene loaded, so
nothing CoX ever executed — and `cox_verify.sh`'s only test was that no CoX
failure line appeared in the log. None had.

"Passed" and "never ran" have to be distinguishable:

- the selftest now prints `mock230 selftest: Chambers of Xeric` before the
  block, the way every other section already announces itself;
- `cox_verify.sh` requires that marker and refuses to report a result without
  it, printing the tail of the run instead;
- and it fails up front when `cache.osrs239` is missing.

**Everything verified before this fix was verified against nothing.** Any green
result in this tree's CoX history that predates a check of the marker should be
treated as unknown rather than as a pass.

### 10.6 Verification

`tools/cox_verify.sh` — 45 checks green (was 39). `tools/cox_check_timers.py` —
37 timer npcs, 39 armed. `tools/cox_sim.sh` — green.

Six new gates, every one mutation-proved after the harness fix:

| # | What it pins | Mutation that turns it red |
| --- | --- | --- |
| 40 | No two members of a pack share a tile | collapse any table to one tile |
| 41 | Every authored tile is inside the 32×32 box | push one to `lx 40` |
| 42 | The three variants are three *different* rooms | alias two of them |
| 43 | The `(lx,lz)` packing round-trips and is asymmetric | make it additive |
| 44 | Tekton's anvil matches what **this** cache places | move it one tile |
| 45 | Puzzle-room counts stay inside their published bounds across parties 1-20, and the two tightrope ladders differ | alias the mage step to the ranger step |

Check 40 is the one that would have caught the bug this pass exists to fix.

### 10.7 Still missing after this pass

1. **Nothing here has been played.** The gates are arithmetic and table shape;
   none of them stands in a room and looks. A monster now spawns at an authored
   tile, but whether that tile is walkable *in this cache* is unchecked for
   every room without a marker loc — which is every combat room.
2. Stage B's actual mechanics: the fights themselves are still the thin versions
   in §1's table. This pass moved the monsters; it did not port `processNPC`.
3. Stage A's remaining items: `CombatPointCapCalculator` (per-room point caps),
   `RoomController`'s lifecycle seam, and `FloorEdgeRoom` / floor progression.
4. The 77 non-CoX failures in `mock230 --selftest` on this branch. They are the
   *absence* of another session's uncommitted `src/world` and `src/net/mock`
   work rather than a regression from this pass — the worktree branched from
   HEAD — but that has not been A/B'd against the shared checkout.

---

## 11. Pass 2 — stage B, the combat room mechanics port — 2026-08-19

Eight combat rooms, each ported independently against its own NR dossier/plan:
scavengers, guardians, skeletal mystics, lizardman shamans, vanguards,
muttadiles, vasa, vespula, tekton. §10 moved the monsters onto authored tiles;
this pass ports what they *do* — `processNPC`, prayer interactions, HP/CM
scaling, room-level state machines — on top of that geometry.

| Room | Status | Gates | `cox_verify.sh` |
| --- | --- | --- | --- |
| Scavengers | OK | #49–53 | green |
| Guardians | OK | #54–63 | green |
| Skeletal mystics | OK | #64–73 | green |
| Lizardman shamans | OK | #74–87 | green |
| Vanguards | OK | #88–99 | green |
| Muttadiles | OK | #100–107 | green |
| Vasa Nistirio | OK | #108–118 | green |
| Vespula | OK | #119–131 | green |
| Tekton | **NEEDS ATTENTION** | #132–142 | not obtained this session (§11.10) |

94 new `::coxrun` gates (48 → 142), every one but tekton's mutation-proved
against a real `mock230 --selftest` run: broken, confirmed red at its own
check number, restored, reconfirmed green.

### 11.1 The finding that shaped the pass

The plan's own literals could not be trusted at face value. Every room turned
up at least one place where re-deriving a number from this engine's actual C
source, or from a live run, contradicted what the dossier said or assumed:
scavengers' `combat_xp_multiplier` unit, guardians' active-player pointer
hazard, mystics' RNG-calibrated tolerance band, vanguards' `npc_findallany`
signature, tekton's untested operator combination. Re-verifying a plan's
"VERIFY live" flags against the engine, not against the plan's own text, kept
surfacing bugs the plan itself could not have caught from the Java side alone.

### 11.2 A pervasive bug, found six times, fixed in five files

`npc_findallany(coord, npc_type, distance)` — used throughout this package as
if the middle argument filtered by type — does not take a type argument at
all. The real signature is `(coord, distance, checkvis)`; a type value passed
there silently lands in the distance slot. Every room that touched it found
this independently, the same way: a live run returned hundreds or thousands
of npcs instead of the two or three the code expected.

Fixed, this pass, in code this pass wrote or wired live:

| File | Where |
| --- | --- |
| `cox_scavengers.rs2` | new collision-avoidance spawn finder (own new code, caught before commit) |
| `cox_vanguards.rs2` | `~cox_vanguard_hp` / `_is_dead` / `_heal_one` (pre-existing, dead until this pass wired `~cox_vanguard_check_heal` live) |
| `cox_minions.rs2` | lizardman shaman + skeletal mystic code (written correctly from the start, using `npc_findall`) |
| `cox_muttadiles.rs2` | HP-scaling code (written correctly from the start) |
| `cox_vespula.rs2` | new selftest gates (initially copied the bad idiom, caught at gate #123+, fixed) |

Confirmed still present, pre-existing, **not** fixed — out of scope for the
room each was found in:

- `cox_guardians.rs2` — `~cox_guardian_dist_from`, `~cox_guardian_pushback_anim`,
  `~cox_guardian_anyone_adjacent`, `~cox_guardian_set_hp`
- `cox_points.rs2` — `~cox_points_cap_scan`, which every room's point cap
  depends on
- `cox_puzzles.rs2` — the tightrope scan

Point caps and HP-set procs across most of this raid may currently be scanning
far more npcs than intended. This is real and likely significant; fixing five
established call sites in files this pass did not otherwise touch was judged
out of scope room-by-room and is flagged here instead of anywhere partial.

### 11.3 Scavengers

| # | What the tree had | What the source says | Where |
| --- | --- | --- | --- |
| S1 | Duplicate `raids_scavenger_beast_a/_b` block pair | One block, better-commented | `cox.npc` |
| S2 | `maxrange` 32 (`RaidNPC`'s own default) | NR `ScavengerBeast` overrides it to 4 | `cox.npc` |
| S3 | No xp modifier | NR's 10% xp is `param=combat_xp_multiplier,100` — confirmed live that `combat.rs2` reads this param in thousandths (1000 = 100%), so 100 is correct, not a typo for 1000 | `cox.npc` |
| S4 | `^cox_scav_small_count` / `^cox_scav_large_count`, a size split | NR's table has no size split at all | `cox.constant` |
| S5 | `respawnrate` assumed sufficient | Dead weight on any npc this package `npc_add`s — `mock230_world.c`'s own selftest asserts a killed `npc_add` npc stays dead. A queued respawn is the real primitive | `cox_scavengers.rs2` |
| S6 | Large-room shortcut blocking-object tiles unsurveyed | Surveyed live with `tools/cox_nr_locs.py` against NR's `LargeScavengerRoom.blockingObjectTiles` | `cox.constant` |

**Built:** `~cox_scavenger_count` (NR's 8-bracket party table, shared by both
rooms), `~cox_scavenger_npc` (50/50 id roll), a scatter-with-collision-avoidance
spawn finder (radius-5 random search, up to 10 attempts, min 2-tile gap — the
first genuine runtime-random spawn plumbing in this package), a queued 2-tick
respawn bound to the killing player (mirrors `godwars_chamber.rs2`'s
`~gwd_private_schedule_respawn`), and the large-room shortcut mechanic: real
cache loc names (`raids_corridor_rocks`/`roots`/`boulder` + `_cleared`
variants, all already registered), a level roll
(`min(99, random(lowest, highest+2))`), and an OURS-flagged click → queued
resolve → loc-state-gated clear paying `level*5` points directly, bypassing
the room's combat point cap (the thieving-grubs/crab-crystals convention).
`coxgoto_scavsmall`/`coxgoto_scavlarge` debugprocs added to `cox.rs2`, and a
`clearqueue(cox_scavenger_respawn)` safety net to `~cox_clear_session`.

**Gates #49–53**, all mutation-proved (mutate → red → restore → green):
party-count table at all 13 boundaries, npc-id 50/50 variety, shortcut kind
round-trip, the respawn-tick literal, and a live gate that builds a real
scratch `map_instance` and spawns through the real production procs. That
live gate's first *unplanned* run — before any deliberate mutation — came
back "found 2200" instead of 2: the §11.2 bug, caught for real.

**Still missing:** no `npc_maxrange` accessor exists, so the maxrange=4 change
is source-verified only, not selftest-gated. No gate for the shortcut's
level-99 clamp (no existing precedent for mocking a player's skill level in
this test file). `tools/cox_sim.sh` was not exercised — no `^cox_trace_scav_*`
codes exist and scavengers were never in that harness's scope.

### 11.4 Guardians

| # | What the tree had | What the source says | Where |
| --- | --- | --- | --- |
| G1 | `~cox_guardian_scale_damage` (pickaxe gate) written but never called | Wired into the real `player_hit_npc_prepare` funnel — also fixes combat XP for free | `cox_guardians.rs2`, `rs2012_td_player_hit.rs2` |
| G2 | Single always-stomp attack | 50/50 melee-vs-stomp roll, forced stomp when nobody is adjacent | `cox_guardians.rs2` |
| G3 | Stomp centred on `npc_coord`, same tick | Centred on the attacked player's captured tile, resolves one tick later (the dodge window) | `cox_guardians.rs2` |
| G4 | No flinch | Halves the attack countdown once per attackrate window, via a new per-npc var slot | `cox_guardians.rs2`, `cox.constant` |
| G5 | No pushback | Movement, 15–30 damage, 40 points, flavour attack anims, reading Pass 1's previously-unused blocked-tile/push-tile data | `cox_guardians.rs2`, `cox.constant` |

Correction G2 was found by reading the mock230 C engine directly: `huntall`/
`huntnext` repoint the same "active player" `queue*()` depends on, so the
adjacency scan has to be followed by a re-hunt on the captured target tile
before dispatch — an active-player VM-pointer hazard, not just a logic gap.

**Built:** as above, plus a corrected npc-var-slot table entry and
pushback/flinch/attack-speed constants in `cox.constant`, and closed out
`COX_MECHANICS.md`'s Guardian HP-formula and attack-speed TODOs.

**Gates #54–63**, all mutation-proved. Two real self-test bugs surfaced and
were fixed in the process: gate #57 (flinch cooldown) was tautological as
first written — two calls in one synchronous tick can't observe `map_clock`
moving, so a missing-guard mutation left both readings identical and the gate
passed under mutation. Rewritten to seed the cooldown var into the future and
assert flinch leaves it untouched. Gate #61 (pushback) discovered the live
selftest fixture has 10 hitpoints and that `damage()` clamps hitpoints to
base on every call — a real pushback hit fires the engine's own death
trigger synchronously, before content's own death-check runs, which cannot be
undone from script. Fixed by splitting `~cox_guardian_pushback_apply` into
separately-callable move/roll sub-procs and gating the fixture player out of
the damage path entirely. A third slip — a diagnostic value smuggled through
the failure code and left in place — produced one false green pass; caught by
diffing against a saved backup, which became a standing check for the rest of
this pass.

**Still missing:** the stomp's captured-epicenter and the queue*-delay dodge
window are documented but not independently gate-provable — a synchronous
debugproc cannot observe a real one-tick defer; the closest achievable
equivalent (the stomp's Chebyshev boundary, gate #60) is gated instead.
Pushback movement is `p_telejump` (instant), not a client-animated slide — no
`ForceMovement` primitive exists in this engine, flagged rather than silently
approximated. The pushback's flavour attack anims play but do not force
either guardian's actual combat target onto the player — no primitive exists
for that either. Guardian target-sharing (hit either statue, pull both) is
out of scope per the plan. `tools/cox_sim.sh` has no guardians section
(pre-existing tool gap, not closed here).

### 11.5 Skeletal mystics

| # | What the tree had | What the source says | Where |
| --- | --- | --- | --- |
| M1 | One attack style, implied | Melee/fire/Vulnerability split, per-npc style state machine (`processNPC`'s reach-stuck counter, a post-attack 50/50 walk-reach-gated reroll) | `cox_minions.rs2` |
| M2 | No prayer interaction | Symmetric Protect-from-Melee/Magic halving | `cox_minions.rs2` |
| M3 | No party/CM scaling on maxhit | Fire exact per NR's published formula; melee approximated from `RaidNPC.aggressiveLevelMultiplier` (no stage-A raid-combat-level input yet); vuln defaulted to fire (NR's `combatDefinitions.getMaxHit()` is opaque) | `cox_minions.rs2` |
| M4 | No proactive engagement | `forceAggressive`, with `checkvis` matching the current style — the one CoX room where `isEntityClipped=true` | `cox_minions.rs2` |
| M5 | No exit block | Mark of Power loc, spawned with the pack, removed only when the last mystic's `onFinish`-equivalent runs, tracked via new `%cox_mystics_alive` varp | `cox_minions.rs2`, `cox.varp` |
| M6 | Three decorative portal locs missing | Added | `cox_minions.rs2` |
| M7 | Mark/portal tiles hand-guessed | Re-derived with `tools/cox_nr_locs.py` | `cox.constant` |

**Built:** as above.

**Gates #64–73**, all mutation-proved — run against an isolated
`git worktree add --detach HEAD` snapshot with the CoX package overlaid,
since the shared tree was mid-edit by other sessions for most of this room's
work. Gate #69's original tolerance band (a textbook statistical guess) did
not reliably separate the correct 1-in-5 roll from a denom-5-to-4 mutation,
because this engine's selftest RNG is deterministically seeded — the mutated
draw happened to land inside the guessed band. Recalibrated to the two
actually-measured values (187 vs 230) instead of a formula, then reverified.

**Still missing:** Vulnerability's true max hit is unresolved (NR's own
method is opaque), defaulted to the fire formula. Sound wiring (five ids)
skipped per the plan's explicit permission, pending a lane-wide npc-cast
sound helper. Melee's raid-combat-level term is approximated at 1.0 (solo).
`checkvis`/proactive engagement is implemented and reasoned through, but per
this file's own documented boundary ("not a substitute for playing the
room"), cannot be gated by a debugproc.

### 11.6 Lizardman shamans

| # | What the tree had | What the source says | Where |
| --- | --- | --- | --- |
| L1 | No exit block | Tendril gate: 2 `blockwalk=all` `raids_lizardshaman_blocker` npcs + tendril wall locs, CCW getting 2 extra per the wiki's "one or two sets" | `cox_minions.rs2` |
| L2 | Poison splash not reworked | Centred on the target's tile via `~map_projectile`, 2-tile radius, 20–40 roll, Shayzien tier-5 reduction, severity-12 poison at 1-in-3 on land | `cox_minions.rs2` |
| L3 | No minion "spawn special" | 1-in-60/tick, ~33-tick cooldown, a 3-minion cluster that chases and self-destructs for `5+min(5, distance*2)` | `cox_minions.rs2` |
| L4 | Partial attack-style roll | Full `LizardmanShaman.attack()` roll (ranged/poison/melee/jump), jump's cooldown doubled to 8 ticks | `cox_minions.rs2` |
| L5 | Assumed fresh cache/pack registration needed | Reused two already-registered npc records (`raids_lizardshaman_blocker` 7575, `zeah_lizardshaman_spawn` 6768) and an already-authored loc — no namespace gap | `cox_minions.rs2` |

**Built:** as above. `~cox_shaman_count`/`^cox_shaman_min`/`^cox_shaman_max`/
`^cox_shaman_step_first`/`^cox_shaman_step_party` deliberately untouched per
the task's own instruction; the existing shaman-count gate (#26) still passes
unchanged.

**Gates #74–87** (14), all individually mutated, confirmed red at the exact
plan-suggested break, restored, and diffed byte-identical against a
pre-mutation backup before moving on.

**Still missing:** Slayer helm(i)/hard Kourend diary substitution for the
Shayzien reduction not ported (no diary-check primitive found). Shaman
attack anim ids not set — no source names them and this cache's
`npc_combat` ledger has no entry for either record. The jump attack uses
`npc_walk`, not a true teleport — this engine has no npc teleport primitive
at all. The jump's blocked-landing reroll fraction (50/50) is OURS, not
sourced. Gate #74 could not use `map_blocked` as originally envisioned — a
freshly built scratch instance has no scene built around it within one
synchronous debugproc call; the gate verifies via npc existence/position
instead, which does not catch a hypothetical `blockwalk` mutation in
`cox.npc` itself. The "Shayzien Specialist" Combat Achievement was
deliberately not wired — its real name/reward is unconfirmed on the live
wiki. Camera-shake for the spawn special is dropped, disclosed rather than
silently omitted. `cox_guardians.rs2`'s own `npc_findallany` misuse
confirmed present and flagged (§11.2), not fixed here.

### 11.7 Vanguards

| # | What the tree had | What the source says | Where |
| --- | --- | --- | --- |
| V1 | No dormant→active state machine | Dormant spawn → wake/rise (2-tick, still shelled) → style-roll opens the shell → active combat | `cox_vanguards.rs2` |
| V2 | Attack rolls not implemented per style | Melee: 3 independent single-target rolls. Magic/ranged: one guaranteed AoE hit on the target's tile plus two random splash tiles in a radius-3 box | `cox_vanguards.rs2` |
| V3 | No prayer gating | Each style's own praying-check gates a 33%-remaining reduction; CM scaling live via the shared `~cox_scale_challenge` | `cox_vanguards.rs2` |
| V4 | Shuffle countdown decremented 3x too fast | Fixed to a single room-wide decrement | `cox_vanguards.rs2` |
| V5 | Shuffle used self-only `npc_changetype` — the three npcs desynced | Fixed via one tile-indexed npc var, shared and rotated across all three | `cox_vanguards.rs2` |
| V6 | No stomp-in-path mechanic | 3–6 damage on anyone in a shelled vanguard's path during shuffle | `cox_vanguards.rs2` |
| V7 | Shell reopened without confirming real arrival | Reopens only once all three have genuinely arrived | `cox_vanguards.rs2` |
| V8 | Death handling ignored the shuffle/heal-threshold survive rule | Survives at 1hp during a shuffle, or while the group is still over the 40%/33.3% heal-spread threshold | `cox_vanguards.rs2` |
| V9 | `~cox_vanguard_check_heal` fully written, never called | Wired to run every active tick from all three forms | `cox_vanguards.rs2` |
| V10 | `~cox_vanguard_heal_all` only printed a message | Now actually heals | `cox_vanguards.rs2` |

**Built:** as above. `configs/all.seq` needed no new entries — the cache
already ships every named vanguard animation the plan asked for. New npcvar
slot `^cox_npcvar_vanguard_slot` is 7, not the plan draft's suggested 1 (1
collides with `^cox_guardian_var_flinch_until`).

**Gates #88–99** (12), each mutation-proved against a harness that
temporarily disabled checks 1–87's dispatch lines so a vanguard mutation
couldn't be masked by an earlier shared-formula check (several gates share
`~cox_scale_challenge` with pre-existing checks). Two real production bugs
surfaced and were fixed: `~cox_vanguard_on_death` called `~cox_vanguard_hp`
three times while gathering the group's HP, and each call's own
`npc_findall` left a *different* npc active by the time the heal ran — the
vanguard meant to survive at 1hp was silently left dead. And
`~cox_vanguard_hp`/`_is_dead`/`_heal_one` carried the §11.2 `npc_findallany`
bug, harmless while dead code, load-bearing the moment it was wired live. A
third, engine-level trap: `npc_damage`'s flinch/block anim defers `npc_del`
by one phase, so two npc-spawning gates within 32 tiles of each other could
grab a prior gate's not-yet-reaped leftover — fixed by spacing damage-using
gates more than 32 tiles apart. Gate #94 (stomp range) was tautological as
first written, validating rolls against the same constants they were rolled
from; fixed to check literal bounds.

**Still missing:** party-size HP/stat scaling not wired — no opcode exists to
raise a live npc's base/max hitpoints past `npc_statheal`'s clamp-at-authored
or `npc_statadd`'s 255 ceiling, and `~cox_olm_party_size` is a hardcoded
`return(1)`. Exit-crystal loc clearing not implemented — no equivalent loc
found via template survey; this room carries no identifying loc at all.
Drops deferred to stage F per the plan. Two unrelated pre-existing failures
(a ToB Verzik content-contract violation; concurrent-session breakage in
files this task never opened) confirmed unrelated and left untouched.

### 11.8 Muttadiles

| # | What the tree had | What the source says | Where |
| --- | --- | --- | --- |
| MU1 | `raids_dogodile` (surfaced/attackable large form) never reached by `npc_changetype` — the encounter could not be finished by combat once the small one died | Real tick-counted state machine: walks the submerged npc 5 tiles, transforms at tick 4/5, deliberately ungated by any `isLaunched`-equivalent per the NR bug-correction note | `cox_muttadiles.rs2` |
| MU2 | Small muttadile's attack entirely unscripted despite having combat stats | Wired `[ai_opplayer2,raids_dogodile_junior]` | `cox_muttadiles.rs2` |
| MU3 | No party HP scaling — every party fought the solo 250hp floor | Wired via `cox_scaling.rs2`'s `~cox_scale_hp` (the guardian pattern) | `cox_muttadiles.rs2` |
| MU4 | `^cox_mutta_ranged_maxhit` 44 | 45, plus CM variants for every maxhit including the small form's own 28/30 (declared, never scripted) | `cox.constant` |
| MU5 | No stomp-vs-single-target split | Implemented via `huntall` headcount | `cox_muttadiles.rs2` |
| MU6 | `~cox_mutta_check_meal` fully set up, never called — the meat-tree heal mechanic was dead code | Wired into a new `~cox_mutta_meal_tick`, trigger threshold fixed to compare against `npc_basestat(hitpoints)` | `cox_muttadiles.rs2` |

**Built:** as above, using the correct 4-arg `npc_findall` throughout — noted
inline that `cox_guardians.rs2`'s own HP-set proc misuses `npc_findallany`
the same way (§11.2, left alone).

**Gates #100–107** (8), each mutation-proved by hand. Gate #106's first
design was self-caught as a false-positive risk: the original mutation
(`>0`→`>=0`) did not turn the gate red because the test sequence never
reached the boundary the two spellings disagree on; rewritten to seed the
style lock at 1 so the boundary is actually exercised, then reconfirmed red
under the same mutation.

**Still missing, per the plan's own guidance:** tendril room-entry gating
and its 3 blocking scenery objects; the meat-tree loc swap and the actual
woodcutting chop-down action (a substantial sub-feature); the exit-blocking
crystal and death/loot/room-finish cleanup; the anti-kite leash (no
LOS-equivalent primitive confirmed); the bite-heal-amount reconciliation
against NR's literal figure (left as this tree's existing 25%-per-bite
formula — no rung 1–3 source states one). Visual parity (projectile/gfx ids,
walk-out-of-water anim) omitted — no muttadile-specific seq exists in this
cache. Earth-elemental weakness and the bronze-axe room amenity deferred.
Separately: this engine's `npc_statheal` clamps healing at the npc's
cache-authored base hitpoints with no script-reachable way to raise it, so
the guardian-style HP-scaling pattern this file also uses cannot actually
exceed `cox.npc`'s authored 250 for any party — a pre-existing engine
limitation shared with `cox_guardians.rs2`, out of scope to fix here.

### 11.9 Vasa Nistirio

| # | What the tree had | What the source says | Where |
| --- | --- | --- | --- |
| VA1 | Special attack fired once, did not repeat on crystal timeout | `%cox_vasa_stage`/`%cox_vasa_substage` state machine (announce → teleport → explosion resolve → crystal cycle → return-to-centre) loops to a fresh special only when a timeout armed `%cox_vasa_explosion_pending`; a timely kill goes straight to the next crystal | `cox_vasa.rs2` |
| VA2 | Stunned players in place | Physically relocated via `p_teleport` into measured, walkability-checked rings (2–8 nearby, 10–24 distant) | `cox_vasa.rs2` |
| VA3 | Damage computed per player | One shared `(hp-5)` pool banked at the teleport moment, divided once at resolve | `cox_vasa.rs2` |
| VA4 | 12-tile Vasa-relative hunt radius | Room-wide 32, matching Great Olm's convention | `cox_vasa.rs2` |
| VA5 | Corner draw could repeat within one special | Draws each corner without replacement via four boolean flags (no bitwise opcodes in this dialect), reset only on a fresh special | `cox_vasa.rs2` |
| VA6 | Unconditional `[ai_opplayer2]` attack block | Unconditional-every-tick stomp + walk-gated (not attack-gated) spark hazard | `cox_vasa.rs2` |
| VA7 | No ranged-immune/magic-multiplier on the crystal | Added via the shared `player_hit_npc_prepare` funnel, plus accuracy-side stab/slash/crush defence params | `rs2012_td_player_hit.rs2`, `cox.npc` |
| VA8 | Dossier proposed an `hitpoints=0` poll for death cleanup | Wired instead to `[ai_queue3,raids_vasanistirio_*]`, this engine's real death trigger — confirmed in `mock230_world.c`, already used by this file's own crystal-kill handler and by `cox_tekton.rs2`/`cox_vanguards.rs2` — a deliberate, evidenced deviation | `cox_vasa.rs2` |
| VA9 | Walking-form timer armed at a 10-tick interval | Moved to 1-tick (regen rerouted through `~cox_regen_counted`) — the special's sub-tick table and the stomp/spark cadences need per-tick granularity a 10-tick timer cannot give | `cox_vasa.rs2` |

**Built:** every `[ai_timer]`/`[ai_queue3]` body extracted into a named
`[proc,...]` specifically so `::coxrun` drives the real dispatch logic, not a
copy of it; hitbox/gating checks split into pure predicates so no gate risks
damaging the live selftest player.

**Gates #108–118** (11), all mutation-proved against a genuinely running
`mock230 --selftest` (not just a compile check). Getting there cost real
time: `MOCK230_SCRIPTS`/`MOCK230_CONTENT` are not honored by the general
selftest path (only one GWD-specific branch reads them) — mutations had to
be recompiled into the live tree's actual `server/scripts/build`.

**Still missing:** **GIT STATE WARNING** — a concurrent session's commit
(`9dec26bb3a`, "quest content and raids") captured a broken intermediate
state of `cox_vasa.rs2` mid-mutation-test (the P20 sparks guard removed).
The current working tree holds the correct, fully restored, verified-passing
version and differs from `HEAD` by exactly that one hunk (`git diff` on this
file shows only that restoration) — do not `git checkout`/`reset` this file
without preserving it first. Cosmetic gaps deferred: no sound effects, no
tick+4 telegraph substep, "stun blocks prayer" approximated via the existing
freeze primitive rather than a new stun system. The decorative lit/unlit
corner crystal locs are unwired — not registered in `pack/loc.server` and
outside the dossier's concrete plan. `^cox_vasa_boulder_maxhit=25` remains
flagged unverified — NR's own base is a scaling function, not a flat number,
and no rung 1–3 source states one.

### 11.10 Vespula

| # | What the tree had | What the source says | Where |
| --- | --- | --- | --- |
| VE1 | 4-state grub chain, hp 125 | HP-driven decay/sting/feed FSM, grub hp 30 | `cox_vespula.rs2` |
| VE2 | Vespula sting logic unimplemented | Walk+lockout sting FSM, farthest-eligible-grub selection, skip-stinging | `cox_vespula.rs2` |
| VE3 | Grounding/enrage checks written but dead | Wired live | `cox_vespula.rs2` |
| VE4 | Portal drain used a radius | Tile-exact target, hit-detection poll driving tendril spawn + the enrage window | `cox_vespula.rs2` |
| VE5 | Portal combat range wrong | Corrected to 6 | `cox_vespula.rs2` |
| VE6 | No melee-block/ranged-magic-immunity gate | Added, mirroring `godwars_bosses.rs2`'s aviansie pattern via `[opnpc2,...]` | `cox_vespula.rs2` |
| VE7 | No guaranteed kill-drop | Guaranteed 4-item drop including Overload(+)(4) | `cox_vespula.rs2` |
| VE8 | Portal death had no pillar-swap/boil-burst/tendril-removal/message | Added | `cox_vespula.rs2` |
| VE9 | Vespine soldier ground/self-destruct timers missing | Added | `cox_vespula.rs2` |
| VE10 | This encounter flooded the shared trace varp (§13 note) | New private Vespula trace channel, mirroring Olm/Tekton | `cox.rs2` |
| VE11 | Survey tiles (portal hit tiles, tendrils, boils) not derived | Derived by solving each door variant's chunk base from 4 decoded anchor tiles, cross-validated against 2 independent NR source arrays — both matched exactly | `cox.constant` |

**Built:** as above; several `ai_timer`/`opnpc2` bodies refactored into
named, independently-callable procs specifically so the new gates could
drive real production logic.

**Gates #119–131** (13), all mutation-proved. One live bug found while
writing the gates themselves: the new selftest code initially copied the
§11.2 `npc_findallany` idiom, and at gate position #123+ (scene no longer
guaranteed clean) it picked up stray leftover npcs from earlier tests
instead of the freshly-spawned scratch grub. Fixed to `npc_findall` with an
explicit type filter.

**Still missing:** the Medivaemia-blossom harvest interaction is not
implemented — no `[oploc1,raids_vespula_herb]` handler exists anywhere in
this package. `VespineSoldier.rise()`'s "heals the boss and portal to full"
behaviour, noticed while reading source, is not in the given
corrections/plan list and was deliberately not added — flagged for a future
pass rather than silently ported. The enraged-Vespula AoE poison sub-effect
is dropped — no npc-applies-player-poison seam exists in this file. The
pillar loc's exact co-location with the portal's survey tile is flagged
unverified in-code. Two concurrent-session auto-commits swept most of this
room's work mid-task; one small correctness fix (the grub-sting target-hp
formula, restored to max-relative after a mutation test) remains
uncommitted in the current working tree, verified green as-is.

### 11.11 Tekton — NEEDS ATTENTION

| # | What the tree had | What the source says | Where |
| --- | --- | --- | --- |
| T1 | Undirected 6×6 hit box | Direction-aware wedge: front row + right column | `cox_tekton.rs2` |
| T2 | No Protect-from-Melee interaction | Halving implemented | `cox_tekton.rs2` |
| T3 | One re-engagement band | Separate, shorter enraged re-engagement band | `cox_tekton.rs2` |
| T4 | Anvil-return unconditional | No-reachable-target anvil-return scan added | `cox_tekton.rs2` |
| T5 | Unconditional room-wide spark hit | Positional, dodgeable spark AoE (captured target tile, radius, one-tick delay) | `cox_tekton.rs2` |
| T6 | No passive hazard | Smoke-pillar hazard added | `cox_tekton.rs2` |
| T7 | Room-empty state did not reset to dormant | Reset to the dormant `raids_tekton_waiting` form wired — the pass's named focus item | `cox_tekton.rs2` |
| T8 | No elemental weakness | Water elemental weakness added to all six `raids_tekton_*` records | `cox.npc` |
| T9 | `^cox_tekton_reengage_distance` declared, unread | Wired into `~cox_tekton_in_reach` | `cox_tekton.rs2` |

Correction #12 (elder maul/DWH/BGS special-attack interaction) and
correction #9 (exit-blocking crystal, entrance fire, combat-stance block)
were left unimplemented per the plan's own guidance — #12 needs a read of
`player_special_attack.rs2`'s per-weapon hook shape outside this pass's file
list, and #9's Zenyte loc ids resolve to nothing this cache authors. No
scaffolding or constants were added for either, to avoid the "declared but
unread" trap correction #11 itself warned about.

**Built:** as above. Several production procs (`in_reach`, `room_empty`'s
safe-tile check, the smoke cadence gate, the spark per-target split, the
spark radius) refactored to take explicit parameters instead of reading
active-npc/live-clock state internally, matching this file's own
established testability convention.

**Gates #132–142** (11). Gate #142 (elemental weakness) genuinely caught a
real bug live, not a deliberate mutation: the first implementation combined
two `!` comparisons with `|` inside one `if(...)` — a pattern with no other
precedent in this codebase — and a full-tree run reported failure #142 while
#132–141 passed. Fixed by splitting into a small per-form helper proc,
matching the `npc $fighting_form`-parameter pattern already used elsewhere
in this file. A second real bug, found by code review rather than a live
run: `~cox_tekton_has_reachable_target`'s `huntall(npc_coord, 3, 0)` — a
range copied from the task's own pseudocode — was narrower than
`~cox_tekton_in_reach`'s actual box, which reaches `size-1+^cox_tekton_reengage_distance`
(5) tiles past the anchor on the far corner; `huntall` filters by plain
Chebyshev distance from the SW anchor (confirmed by reading `SS_OP_HUNTALL`),
so a legitimately-in-reach player near that corner would never reach the
scan loop. Fixed to compute the hunt range from `nc_size(...)` and the
reengage constant directly.

**Verification: not fully obtained this session.** `tools/cox_compile_check.sh`
is clean on every run, and `tools/cox_check_timers.py` reports every
`ai_timer` armed. But no fresh, fully green `tools/cox_verify.sh` run exists
after the last two fixes above: across roughly a dozen attempts over ~15
minutes, the shared tree was persistently broken by two other,
actively-editing sessions (`minigame_toa/toa_kephri.rs2` — unknown constant
`^toa_kephri_phase_dead`; `quest_totem/totem_mansion.rs2` — `human_climbstairs`
is not a command), and `cox_verify.sh`'s own isolated-tree fallback is
separately blocked by a pre-existing, already-committed bug in
`ported_rs558_ancient_curses/scripts/curses.rs2` (confirmed present at `HEAD`
via `git show`, unrelated to this task). None of these three files were
touched by this pass. Consequently gates #132–133 and #135–141 were
design-reviewed by hand rather than mutated-and-restored live — lower
confidence than every other gate in this pass, disclosed rather than
presented as equivalent.

**Still missing:** correction #12 and #9, unimplemented (above). Magic
damage reduction (80%) and ranged immunity for Tekton are entirely
unimplemented at any rung — this needs a general incoming-player-damage-
modifier engine seam that does not exist anywhere in this tree (the identical
gap blocks Vasa's crystal; `cox_guardian_scale_damage` is the one candidate
shared hook and it is dead code). Documented as an explicit OPEN item in
`COX_MECHANICS.md` §3.4 so a later pass does not assume this port covered
it. The spark's cosmetic origin tile (which room tile it visually flies
from) is not ported — no sourced projectile/gfx id was found. Other
concurrent sessions' auto-commits swept most of this pass's uncommitted
edits into their own commits; all content is intact, and only the
`has_reachable_target` range fix remains uncommitted as of this report. A
fresh `tools/cox_verify.sh` run, once the shared tree is quiet, is the
outstanding item for this room.

### 11.12 What's still missing across the pass

1. **Tekton has no confirmed fully-green `cox_verify.sh` run** for its final
   state (§11.11) — the one room in this pass that does not close cleanly.
2. **The §11.2 `npc_findallany` bug remains live** in `cox_guardians.rs2`,
   `cox_points.rs2` (`~cox_points_cap_scan` — every room's point cap depends
   on it), and `cox_puzzles.rs2` (tightrope). A dedicated pass to fix all
   five established call sites, not just the ones this pass's own new code
   touched, is warranted.
3. **Nothing here has been played**, same caveat as §10.7 — every gate is
   arithmetic, table shape, or a scratch-instance live check; none stands in
   a room with a real party and watches the fight.
4. **Stage A's remaining items are still open**: `CombatPointCapCalculator`'s
   per-room caps beyond what §11.2 already flags as scanning wrong,
   `RoomController`'s lifecycle seam, and `FloorEdgeRoom`/floor progression.
5. **A general incoming-player-damage-modifier seam** is now a confirmed gap
   in two places (Tekton's magic/ranged resistance, Vasa's crystal) rather
   than one — the next room to need it should build the shared hook instead
   of a third one-off.
6. **Deferred sub-features**, recorded per-room above rather than repeated
   here: muttadiles' meat-tree woodcutting minigame and tendril gating,
   vespula's herb-harvest interaction, vanguards' exit-crystal and drops,
   shamans' Combat Achievement tracking — none silently dropped, all flagged
   in their own section.
7. **Concurrent-session churn was constant and, in Vasa's case, committed a
   broken intermediate file to `HEAD`** (§11.9). The working tree is correct;
   the git history for that one file is not, until the current working-tree
   state is committed over it.
