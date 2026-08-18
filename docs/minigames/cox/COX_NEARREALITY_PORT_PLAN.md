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
