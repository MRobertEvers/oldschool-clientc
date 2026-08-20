# Clock Tower modernization audit

Status: `audit-pending` — Brother Kojo, all four cog spawns, rat poison, the
Clock Tower and dungeon maps, correct and decoy spindles, ladders, stairs,
secret wall, levers, gates, trough, rats, ogres, journal, completion call, and
broad 0–8 route exist. A clean player can plausibly complete the intended
route, but the implementation remains a legacy port rather than a verified
modern quest. Native finish states 6 and 7 are silent, the primary progress
counter can diverge from a second authored colour bitfield, spindle and reward
transactions are not atomic, completion can lose coins or duplicate coins and
quest points, the black-cog interaction omits a confirmed glove alternative,
only five of eleven poison-pen rats receive the death sequence, and Kojo's
watch/Treasure Trails dialogue is shadowed by the quest-only Talk-to handler.

Audited: 2026-08-17

Governing plan: [Quest modernization plan](../QUEST_MODERNIZATION_PLAN.md). This
record applies that plan's Gates A–D to membership/start handling, every native
state, the authored colour/cooling state, all three dungeon zones, all four cog
pickups and spindles, alternate black-cog cooling methods, rat poison, levers,
gates, NPC queues, completion, rewards, souvenir cogs, journal text, Kojo's
post-quest and shared interactions, music, map links, and the generic completion
adapter. It is an implementation specification, not completion evidence.

## 1. Authoritative references

These pinned OSRS Wiki revisions define the current route, dialogue, item,
reward, dungeon, and shared-NPC contract.

| Reference | Pinned revision | Use in this audit |
| --- | --- | --- |
| [Clock Tower](https://oldschool.runescape.wiki/w/Clock_Tower?oldid=15166584) | 15166584, 2026-04-06 | Identity, requirements, route, floor mapping, rewards, and souvenir behavior |
| [Clock Tower/Quick guide](https://oldschool.runescape.wiki/w/Clock_Tower/Quick_guide?oldid=15117400) | 15117400, 2026-01-31 | Exact entrances, levers, cog order, spindles, and return route |
| [Transcript:Clock Tower](https://oldschool.runescape.wiki/w/Transcript%3AClock_Tower?oldid=15263249) | 15263249, 2026-07-14 | Start/refuse/re-talk, black cog, rat gate, placement, completion, and post-quest dialogue |
| [Transcript:Brother Kojo](https://oldschool.runescape.wiki/w/Transcript%3ABrother_Kojo?oldid=15081152) | 15081152, 2025-12-08 | Complete shared Kojo dialogue, including watch and Treasure Trails branches |
| [Brother Kojo](https://oldschool.runescape.wiki/w/Brother_Kojo?oldid=14767230) | 14767230, 2024-10-13 | Actor ownership, watch service, and hard-clue role |
| [Clock Tower (building)](https://oldschool.runescape.wiki/w/Clock_Tower_%28building%29?oldid=15302409) | 15302409, 2026-08-16 | Surface building, floors, and entrances |
| [Clock Tower dungeon](https://oldschool.runescape.wiki/w/Clock_Tower_dungeon?oldid=15267035) | 15267035, 2026-07-18 | Three-zone topology, monsters, rat pen, one-way exits, and music |
| [Black cog](https://oldschool.runescape.wiki/w/Black_cog?oldid=15227375) | 15227375, 2026-06-06 | Fire-room pickup, cooling methods, black spindle, and duplicates |
| [Blue cog](https://oldschool.runescape.wiki/w/Blue_cog?oldid=15227377) | 15227377, 2026-06-06 | Long-passage/cell route, blue spindle, and duplicates |
| [Red cog](https://oldschool.runescape.wiki/w/Red_cog?oldid=15227376) | 15227376, 2026-06-06 | Ogre-room pickup, red spindle, and duplicates |
| [White cog](https://oldschool.runescape.wiki/w/White_cog?oldid=15227374) | 15227374, 2026-06-06 | Poisoned-rat route, white spindle, and duplicates |
| [Rat poison](https://oldschool.runescape.wiki/w/Rat_poison?oldid=15183454) | 15183454, 2026-04-22 | Trough item and cross-quest availability |
| [Ice gloves](https://oldschool.runescape.wiki/w/Ice_gloves?oldid=15214105) | 15214105, 2026-05-20 | Equipped black-cog alternative |
| [Smiths gloves (i)](https://oldschool.runescape.wiki/w/Smiths_gloves_%28i%29?oldid=15190929) | 15190929, 2026-04-22 | Confirmed upgraded-ice-glove alternative |
| [Jug of water](https://oldschool.runescape.wiki/w/Jug_of_water?oldid=15229681) | 15229681, 2026-06-08 | Main-article alternate whose exact live behavior requires reconciliation |
| [Watch](https://oldschool.runescape.wiki/w/Watch?oldid=15183407) | 15183407, 2026-04-22 | Kojo's coordinate-clue service and acquisition order |
| [Sextant](https://oldschool.runescape.wiki/w/Sextant?oldid=15183406) | 15183406, 2026-04-22 | Watch prerequisite and coordinate-clue toolchain |
| [Chart](https://oldschool.runescape.wiki/w/Chart?oldid=15183405) | 15183405, 2026-04-22 | Watch prerequisite and coordinate-clue toolchain |
| [Ratcatchers](https://oldschool.runescape.wiki/w/Ratcatchers?oldid=15292483) | 15292483, 2026-08-11 | Legitimate later use for an extra rat poison |

The revisions were resolved through the OSRS Wiki API on 2026-08-17. They
identify Clock Tower as quest #29, a members, novice, very-short quest released
17 June 2002. It has no skill or quest prerequisite and no enemy must be
defeated. The practical route requirement is surviving a run past three level
53 ogres. A water container or cold gloves are needed for the black cog, with
a bucket and well available beside Brother Cedric during the quest route.

The current reward contract is exactly 1 quest point and 500 coins. There is no
XP, item unlock, transport, spell, shop, or downstream-quest prerequisite.
The Clock Tower dungeon plays `Alone`; that is an area-music contract rather
than a quest-completion unlock.

There is one source discrepancy to resolve before implementation. The pinned
main quest article lists a jug of water alongside a bucket, ice gloves, and
Smiths gloves (i). The black-cog item page and transcript name bucket/ice
gloves, while Quest Helper names bucket/ice/Smiths but not a jug. Smiths gloves
(i) are confirmed by the item page and helper and are unquestionably missing
from the script. Treat jug support as a live-capture question rather than
silently accepting or rejecting one Wiki sentence.

Transition aid only: the local Quest Helper checkout's
[`ClockTower.java`](https://github.com/Zoinkwiz/quest-helper/blob/5ea99d5ea9ba3fb096ebe7b5ed02d80883e9819d/src/main/java/com/questhelper/helpers/quests/clocktower/ClockTower.java)
at commit `5ea99d5ea9ba3fb096ebe7b5ed02d80883e9819d` confirms native states
0–7, routes 5–7 back to Kojo, identifies the four target spindle coordinates,
models placed colours from journal/chat evidence, and records all major zones,
entrances, items, levers, gates, and alternatives.
`python3 tools/questhelper_extract.py clocktower --check` exits 0. It resolves
the quest row, Kojo, 13 item symbols, 14 loc symbols, and 47 route coordinates.
It cannot prove server-side state consistency, operation ownership, inventory
atomicity, queue idempotence, or multiplayer isolation.

## 2. Native quest identity and player contract

| Field | Native value / expected behavior |
| --- | --- |
| Cache quest ID | 29 |
| Type | Members quest; no series |
| Difficulty / length | Cache 0 / 0; Wiki novice / very short |
| Release date | 17 June 2002 |
| Start | Brother Kojo inside the Clock Tower south of East Ardougne |
| Requirements | No quest or skill requirement; survive level 53 ogres; accepted black-cog cooling method |
| Primary state | Low bits 0–3 of transmitted permanent `%cogquest` |
| Native side state | Bit 4 of `%cogquest` records the poisoned-rat western gate |
| Authored side state | Permanent non-transmitted `%cog_bits`: cooled plus four placed colours |
| Quest points | 1 |
| Reward | 500 coins |
| Combat | No required kill; hostile ogres and other dungeon monsters |
| End state | 8 |

The native `quest_clocktower` dbrow has the correct members flag, difficulty,
length, Ardougne location, release, start coordinate/NPC, quest point, and end
state. It contains no requirement, required-item, coin, XP, or unlock metadata.
Gate A should express the current water/glove requirement and 500-coin reward
through the schema's established fields where possible, while keeping the
server authoritative for actual interaction rules.

### Primary state inventory

| State | Canonical/native phase | Current use / mismatch |
| ---: | --- | --- |
| 0 | Not started | Kojo offers a legacy three-choice menu; cog pickup is denied |
| 1 | Accepted; no cogs placed | Written after acceptance |
| 2 | One cog placed | Written by incrementing the previous value |
| 3 | Two cogs placed | Written by incrementing the previous value |
| 4 | Three cogs placed | Written by incrementing the previous value |
| 5 | Four cogs placed; return to Kojo | Written on fourth placement; Kojo starts completion |
| 6 | Native finish/reward resume state | Quest Helper routes to Kojo; no constant, journal branch, or Kojo switch case exists |
| 7 | Native finish/reward resume state | Quest Helper routes to Kojo; no constant, journal branch, or Kojo switch case exists |
| 8 | Complete | Queue writes 8 before coins and shared quest-point/completion work |

The clean implementation writes 1→2→3→4→5 by counting successful placements,
then jumps directly 5→8 in `queue(cog_complete)`. It never writes 6 or 7.
Nevertheless, those values are part of the current native state ladder and are
explicitly active in Quest Helper. A player imported or captured at 6/7 receives
no dialogue at all because Kojo's exact switch has neither case nor default;
the journal also falls back to the generic cog hunt. Establish the live meaning
of 6 and 7 during implementation and make both recoverably finish the quest.

### Side-state inventory

| Field / bit | Meaning | Current behavior |
| --- | --- | --- |
| `%cogquest` bit 4 | Rat poison used; western gate available | Set immediately on trough use and preserved by low-range progress writes |
| `%cog_bits` bit 0 | Black cog has been cooled | Permanent once set, including future duplicate pickups |
| `%cog_bits` bit 1 | Blue cog placed | Prevents a second placement and drives journal text |
| `%cog_bits` bit 2 | Black cog placed | Prevents a second placement and drives journal text |
| `%cog_bits` bit 3 | White cog placed | Prevents a second placement and drives journal text |
| `%cog_bits` bit 4 | Red cog placed | Prevents a second placement and drives journal text |

`%cogquest` is cache-native and transmitted. `%cog_bits` is an authored,
non-transmitted permanent varp added because the native primary count does not
identify which colours were placed. The journal exposes its colour facts back
to the client as rendered text; Quest Helper consequently synchronizes from
journal/chat evidence rather than a native colour varbit.

This is a valid reason for supplemental state, but the two records currently
have no invariant tying them together. On a clean save:

```text
primary progress = 1 + count(blue, black, white, red placed bits)
```

for states 1–5. Each spindle instead increments whatever primary value happens
to be stored. Kojo completes based on primary state 5 without checking all four
colour bits. Reset/debug/import inconsistencies can therefore create states 5
with missing colours, states 1–4 with too many colours, or 6/7 with no recovery
dialogue. Modernization should validate/repair this composite at safe entry
points and set primary progress from the committed colour set, not blind
arithmetic on an unverified counter.

## 3. Implementation surface

Paths below are relative to `OSRS-Content/osrs239-content/`.

### Quest-owned files

The root contains 591 lines across three configs and seven scripts.

| Path | Present responsibility | Audit result |
| --- | --- | --- |
| `server/scripts/quests/quest_cog/configs/quest_cog.constant` | States 0–5/8 and side-bit positions | Omits native states 6/7 and their ownership |
| `server/scripts/quests/quest_cog/configs/quest_cog.varp` | Native `%cogquest` overlay and authored `%cog_bits` | Correct persistence/transmission shape; composite invariant is unenforced |
| `server/scripts/quests/quest_cog/configs/quest_cog.npc` | Rat combat animation/sound params | Base rat gets sounds; all three variants get combat animations |
| `scripts/brother_kojo.rs2` | Start, re-talks, finale, post-quest | Broad quest dialogue; old start menu, missing 6/7 and shared Kojo services |
| `scripts/cogs.rs2` | Four pickups, black-cog cooling, one-cog limit | Broadly functional; missing Smiths gloves, disputed jug, water use requires a second click |
| `scripts/quest_cog_spindles.rs2` | Correct and incorrect spindle use | Correct floor/colour mapping; writes state before deleting item and blindly increments progress |
| `scripts/quest_cog_food_trough.rs2` | Poison transaction and rat death queues | Opens player gate immediately; queues only one of three rat variants |
| `scripts/quest_cog_gates_and_levers.rs2` | Rat-pen lever/gate mutations and western gate traversal | World-shared temporary loc mutation; no concurrency/relog evidence |
| `scripts/quest_cog.rs2` | Progress helpers and queued completion | Shared completion API present; reward/points transaction is non-atomic and non-idempotent |
| `scripts/cog_journal.rs2` | Dynamic quest journal | Detailed per-colour text; typo and no 6/7/composite repair guidance |

There is no quest-local debug start/reset or interaction-driven route test.
The generic `::complete` arm correctly uses the low-range state setter, refuses
values at or above 8, and lets its caller award the row's quest point. By design
it does not grant the 500 coins or reconstruct puzzle state. It is useful for
prerequisite preparation but is not evidence that the gameplay route or reward
transaction works.

### Shared and cross-directory surfaces

| Path / subsystem | Relationship | Audit result / required ownership |
| --- | --- | --- |
| `configs/all.dbrow` | Native quest metadata | Correct identity/start/end; missing item/reward metadata |
| `configs/all.varp` | Cache-native `%cogquest` | Correct carrier; quest overlay supplies persistence/transmission policy |
| `configs/all.npc` | Kojo and three rat variants | All native assets resolve and have correct broad stats/ops |
| `configs/all.obj` | Four cogs, rat poison, water containers, gloves, trail tools | All core and confirmed alternate assets resolve |
| `configs/all.loc` | Spindles, levers, gates, trough, ladders, stairs, secret wall, well | Core map assets resolve; exact operation routing must remain coordinated with shared owners |
| `maps/m40_50.jl2`, `maps/m40_150.jl2` | Surface tower and dungeon placement | Correct target/decoy spindles, gates, levers, trough, and traversal locs are placed |
| `server/scripts/areas/world/configs/m40_50.spawn` | Kojo and nearby bucket | Kojo at 2569,3249 and bucket at 2616,3255 are spawned |
| `server/scripts/areas/world/configs/m40_150.spawn` | Dungeon NPC/item spawns | Four cogs, rat poison, 11 poison-pen rats, ogres, and other monsters are spawned |
| `ladders_stairs` map-link system | Tower/dungeon entrances and floor traversal | Verified rows cover local origins around the relevant stairs/ladders; no quest hard-coded teleports are needed |
| `general_use/scripts/door_walkthrough_fallback.rs2` | Blue-cell push wall and ordinary doors | `secretdoor2` is owned by the shared walkthrough path |
| `general_use/scripts/water_sources.rs2` | Bucket/jug filling | Nearby well can produce the appropriate filled container |
| `minigame_warriorsguild/.../warriorsguild_doors.rs2` | Exact `spiralstairs` owner | Correctly falls through to `~climb(1)` outside Warriors' Guild, preserving Clock Tower map-link routing |
| `quests/scripts/questpoints.rs2` | Points, completed count, scroll, jingle | Modern shared completion exists but assumes each caller invokes it exactly once |
| Treasure Trails / navigation-tool content | Kojo watch and hard clue | Native rows/items/dialogue exist; production Kojo handler never delegates to them |
| Ratcatchers | Extra rat poison reuse | Ground item remains independently obtainable; avoid quest cleanup that removes legitimate extras |

## 4. World reachability and route topology

### 4.1 Core production content is placed

The start actor is spawned at 2569,3249 in `m40_50.spawn`, matching the native
dbrow start and Wiki location. The dungeon spawn file contains:

- red cog at 2583,9613 among three level 53 ogres;
- blue cog at 2574,9633 in the separate cell;
- black cog at 2613,9639 in the fire room;
- white cog at 2578,9655 behind the rat gate;
- rat poison at 2564,9662;
- eleven Clock Tower-specific rats in the poison pen; and
- the remaining dungeon monsters needed for environmental risk.

The dungeon Wiki counts twelve dungeon rats overall: the eleven poison-pen
variants plus the ordinary rat in the blue-cog cell. The route does not require
killing any of them in combat.

The map itself places the four valid target locs exactly where Quest Helper
expects:

| Cog | Target spindle | Coordinate / floor |
| --- | --- | --- |
| Black | `brokeclockpole_black` | 2570,9642, dungeon basement |
| Red | `brokeclockpole_red` | 2568,3243, ground floor |
| Blue | `brokeclockpole_blue` | 2569,3240, first floor |
| White | `brokeclockpole_white` | 2567,3241, second/top floor |

Each floor also has three same-coloured-looking semantic decoys from the
`clockpole_*` family; use-on them correctly reports that the cog does not fit.
The term `brokeclockpole` is a cache symbol, not proof of a missing visual
transform: target and decoy records intentionally share the clock-spindle model
and differ by semantic role/placement.

### 4.2 Shared navigation appears connected

The main Tower ladder, external long-passage ladder, cell exit, rat-area exit,
and all three Clock Tower floors have cache locs and generated map-link rows.
The secret wall has a shared exact Push owner. This removes the most common
legacy-port blocker: the quest is not relying on a debug teleport or an unused
label to connect its zones.

Gate D still requires a real-client traversal because map-link rows are keyed
on the player's origin tile, and several entrances have multiple valid approach
tiles. The static evidence proves intended coverage, not that collision,
animation, and landing are correct from every approach.

### 4.3 Membership needs an explicit acceptance test

The dbrow correctly marks the quest members-only, but Kojo's handler does not
test `map_members` or use a shared quest-start member gate. East Ardougne is
normally members geography, which may prevent practical access on a free world,
but geography is not a substitute for a tested start contract. Verify nonmember
interaction and add the standard refusal only if the world/content layer does
not already enforce it.

## 5. Canonical route versus current behavior

| Phase | Required behavior | Current behavior |
| --- | --- | --- |
| Start | Talk to Kojo; modern Start quest? Yes/No; accept/refuse and re-talk | Full old conversation plus three-option loop, including a reward question absent from the current transcript |
| Carry rule | Carry at most one coloured cog at a time | Correct inventory-only check across all four colours |
| Red | Run past ogres; take red cog; place on ground-floor red spindle | Spawn, pickup, map route, correct/incorrect spindle handling present |
| Blue | Enter separate ladder near zoo; follow tunnel; push wall; take cog; exit cell; place on first floor | Spawn and shared wall/ladder/map-link route present |
| Black | Enter fire room; cool/take cog with accepted water or gloves; place on basement spindle | Bucket and ice gloves work; water only cools and requires another Take; Smiths gloves missing; jug disputed |
| White | Take poison; set rat-pen levers; poison trough; all pen rats die; western gate loosens; take cog; one-way exit; place on top floor | Puzzle opens route; only five of eleven rats get death queue and gate eligibility is set before scene completes |
| Placement | Correct colour/floor consumes one cog and records progress exactly once; wrong spindle does not | Correct broad result, but side/progress writes happen before item deletion and derive from unchecked old progress |
| Finale | At all four cogs, Kojo thanks player and completion safely grants 500 coins/1 QP | State 5 works; native 6/7 are silent; state 8 is written before unchecked reward/shared completion |
| Post-quest | Kojo praises repair; souvenir cogs cannot be re-picked after completion | Correct quest post-dialogue and pickup denial at state 8 |
| Shared Kojo | Give watch when chart+sextant conditions are met; service hard clue | Entirely absent/shadowed by exact quest Talk-to owner |

## 6. Cog acquisition and placement transactions

### 6.1 One-cog carry rule and souvenir behavior

`~can_pickup_cog` rejects pickup before start, after state 8, and while any of
the four cogs is already in inventory. This matches the core route rule without
incorrectly checking the bank: the restriction is on carrying, not lifetime
ownership. Ground pickup delegates to the generic object pickup path, which
should retain its normal full-inventory behavior.

The Wiki explicitly permits souvenir duplicates: after placing a cog and before
completion, the player may return and pick up another, but after completion a
spawn or dropped duplicate cannot be picked up. The current state-8 denial
preserves that behavior and completion does not delete retained cogs. Do not
“clean up” souvenirs during modernization. Test all four colours, ground drop,
logout, death, and completion while carrying a duplicate.

### 6.2 Black cog alternatives are incomplete

Current behavior has two branches:

- wearing `ice_gloves` sets the permanent cooled bit, shows a message, and
  immediately delegates to pickup; and
- using `bucket_water` deletes it, adds an empty bucket, sets cooled, and shows
  a message, but does not pick up the cog.

The second branch contradicts the quick guide's warning that completing the
interaction is what picks up the cog: the player must click Take again here.
More importantly, `smithing_uniform_gloves_ice` resolves in the cache and Quest
Helper and corresponds to the Wiki's Smiths gloves (i), but the worn-item check
does not include it.

Modernize this as one protected “try cool and take” transaction:

1. confirm quest state, one-cog rule, source item/equipment, and inventory
   capacity;
2. consume/replace a water container atomically where applicable;
3. commit the cooled fact only with successful method handling;
4. complete pickup in the same interaction; and
5. leave no path that consumes water but fails to explain a full inventory.

Include Smiths gloves (i). Resolve jug support with a live current-client
capture because the pinned Wiki sources disagree. If supported, convert
`jug_water` to the correct empty jug through the same transaction.

The cooled bit is permanent, so later duplicate black cogs can be taken without
another cooling item. Verify that against live behavior before retaining it;
if the spawn should reheat, the current bit has the wrong lifetime.

### 6.3 Spindle writes are ordered unsafely

Each correct spindle currently:

1. sets its colour bit;
2. increments primary progress if below 8; and
3. deletes the cog last.

Use-on dispatch normally proves the item existed when the trigger started, but
the ordering still violates the quest's transaction invariant under repeated
packets, interruption, future helper reuse, or inconsistent imported saves.
It can record a placement before proving consumption. The progress increment
also trusts the old counter rather than the resulting colour set.

Create one shared placement procedure parameterized by colour, target, and item.
It should validate active state and exact item, reject already-placed and wrong
targets without mutation, delete exactly one cog, then atomically commit the
colour set and derived primary state. It must never advance 5→6 because a
missing side bit was repaired late, and it must refuse/repair state 8 rather
than consume a souvenir.

## 7. Rat pen, gates, and multiplayer ownership

### 7.1 Six poison-pen rats survive the scripted death

The poison pen contains eleven quest rats:

- five `clocktower_rat`;
- three `clocktower_rat2`; and
- three `clocktower_rat3`.

The trough search finds nearby NPCs but queues death only when
`npc_type = clocktower_rat`. The six variant-2/3 rats therefore remain alive.
This contradicts the Wiki and transcript, both of which describe the rats
dying, and can leave hostile NPCs in the route after the western gate becomes
available.

Target the native family/category or enumerate all three symbolic variants.
The sequence must handle rats already fighting, path-blocked rats, dead/despawned
rats, another player's simultaneous poison event, and respawn policy. Do not
infer scene completion merely from one variant reaching its final queue.

### 7.2 Door eligibility commits before the scene

Trough use immediately deletes rat poison and sets `%cogquest` bit 4 before
queuing any rat movement/death. `ctratgatec` tests only that bit, so the player
can traverse as soon as the messages finish even if queues fail or most rats
remain alive. Repeated trough use consumes more poison and reruns the scene
without checking the bit.

The modern transaction should consume one poison exactly once, start the
protected scene, and commit the durable gate state at the canonical point. A
relog or region change during presentation must resume to a valid state: either
unsolved with poison retained/recoverable or solved with the gate usable. Avoid
a transient state that can consume unlimited Ratcatchers supplies.

### 7.3 Lever and gate locs are world-shared

The two levers use `loc_del`/`loc_add` for 500 ticks, and the first lever also
temporarily replaces the rat-cage gate with `prisondooropen`. These are map
objects, not player-owned transforms. Two players can therefore observe and
toggle the same lever/gate presentation while their poison/western-gate facts
remain different permanent player state.

This may approximate live shared-world scenery, but it needs explicit
multiplayer evidence. Test:

- two players approaching from opposite sides;
- one toggling each lever in rapid succession;
- logout while a temporary replacement is active;
- replacement expiry while a player occupies the doorway;
- one player's poisoned state with another's unpoisoned state; and
- rat respawn while lever/gate locs remain changed.

Use current scoped/player loc machinery if live behavior is personal; otherwise
retain world sharing with atomic compare-and-swap style checks and collision-safe
expiry. The present 500-tick mutation and hard-coded coordinates should not be
accepted without that proof.

The western `Go-through` helper directly teleports across the gate after a
message. Replace or validate it against the modern gate/door traversal helper,
including correct side detection and no landing inside blocked collision.

## 8. Completion and reward integrity

### 8.1 State 5 completion is not validated against colours

Kojo enters the finale solely because low primary progress equals 5. He does
not require all four placed bits. A corrupt/imported save can therefore complete
without the journal's four facts. Conversely, valid-looking colour bits with a
wrong primary counter cannot finish. The finale must validate the composite,
repair only unambiguous legacy states, and refuse ambiguous states with a
diagnostic/recovery path.

### 8.2 The queued completion is neither atomic nor idempotent

`queue(cog_complete, 0, 0)` calls a body that:

1. writes state 8;
2. blindly adds 500 coins; and
3. calls `~quest_complete_rewards`, which adds 1 quest point, increments the
   completed quest count, paints the scroll, and plays the novice jingle.

Coins are stackable, so a full inventory succeeds when a coin stack already
exists. With 28 occupied slots and no coin stack, the grant can fail after the
quest is already complete. There is no coin recovery branch.

The body also has no internal completed-state guard. If repeated interaction or
duplicate queue delivery schedules it twice before the first queue changes the
state, the second delivery can add another 500 coins, another quest point, and
another completed-count increment. The shared completion procedure assumes its
caller supplies exactly-once ownership; this caller does not prove it.

Modern completion must:

- reserve effective space for the coin stack or create a documented recovery
  entitlement before committing;
- acquire an exactly-once completion guard synchronously;
- validate state 5/6/7 and all four colours;
- grant 500 coins once;
- invoke shared completion once; and
- make replay repair presentation/state without adding coins, points, or
  completed count.

Test a pre-existing coin stack, no stack/full inventory, rapid double Talk-to,
duplicate queued delivery, logout before/after the guard, reconnect during the
scroll mount, and replay against 5/6/7/8.

### 8.3 States 6 and 7 require a migration policy

Do not simply alias 6/7 to state 5 without evidence. Capture a current client
through Kojo's finale and determine whether those values represent dialogue,
reward, scroll, or post-completion checkpoints. Then provide idempotent resume
handlers. Existing saves at 6/7 must not remain silent or be awarded twice.

The generic `::complete quest_clocktower` is already state-and-QP idempotent:
it writes 8 through the range setter, and a second call returns already
complete before awarding another point. Keep that contract. It intentionally
does not award the coin reward, so a gameplay completion test must exercise the
real Kojo path rather than substituting the cheat.

## 9. Brother Kojo as a shared actor

The quest's exact `[opnpc1,brother_kojo]` handler owns every Talk-to click and
dispatches only on Clock Tower progress. It therefore shadows two current Kojo
responsibilities documented by the pinned NPC/transcript pages:

1. when the player has the chart and sextant after the navigation-tool
   prerequisites, Kojo gives a watch; and
2. Kojo is a hard Treasure Trails cryptic/challenge target whose clock question
   answer is 22.

The cache contains `trail_chart`, `trail_sextant`, `trail_watch`, Kojo clue
helper rows, and the full NPC dialogue reference, but no production script
grants the watch and no Kojo clue dialogue is wired. The quest file header
explicitly marks the trails/watch arm deferred.

Build one Kojo dispatcher with clear precedence among active clue interaction,
watch entitlement, Clock Tower start/in-progress/finale, and ordinary
post-quest dialogue. The quick guide notes that Kojo may give the watch first
and the player then speaks again to start Clock Tower. Preserve that behavior;
do not make completing Clock Tower a prerequisite for a navigation tool that
is not quest-gated. Every item grant needs normal no-space and duplicate checks.

The broader Treasure Trails subsystem may remain a separate modernization
workstream, but Clock Tower cannot be marked verified while its exact handler
silently prevents a current shared-NPC service.

## 10. Dialogue, journal, music, and presentation

The current start dialogue is a faithful old LostCity-era branch, including
“How much reward are we talking?” and recursion back to the menu. The pinned
current transcript instead uses the modern `Start the Clock Tower quest?`
Yes/No selection and does not list that reward-negotiation branch. Use the
standard modern quest-start helper, retain current accept/refuse text, and
verify whether the removed branch should remain as optional pre-accept lore.

Kojo's in-progress lines correctly distinguish zero, one, two, three, and four
cogs on a clean primary ladder. They must be driven by validated colour count
so inconsistent saves do not receive misleading progress. Add explicit 6/7
resume and retain the correct post-quest praise.

The journal is unusually detailed for a legacy quest: it lists every placed
and unplaced colour from `%cog_bits`, gives a return-to-Kojo state at 5, and
shows completion at 8. Required fixes are:

- spell “Ardougne” correctly (current text says `Argougne`);
- handle 6/7 as finish/resume rather than another cog hunt;
- detect and report/recover composite counter/colour inconsistencies;
- mention the one-cog carry rule and black-cog method only where helpful; and
- preserve legitimate extra rat poison and souvenir cogs in item guidance.

The dungeon Wiki names `Alone` as its music. The cache has `music_alone`, but
the quest root does not explicitly play or unlock it. Verify area music on
entry/re-entry and after relog; do not add a quest-specific track trigger if the
world music system already owns the region. Completion should continue using
the shared novice quest-complete jingle selected from dbrow difficulty.

The current port dropped lever/rat sounds. Restore only cache-resolved,
current-client-confirmed effects through the appropriate scene owner; sound is
P2 after state, item, reward, and multiplayer correctness.

## 11. Prioritized defect ledger

### P0 — completion/reward integrity

1. Completion writes state 8 before an unchecked 500-coin grant; a full
   inventory without a coin stack can permanently lose the reward.
2. The queued completion has no exactly-once guard; duplicate delivery can add
   coins, quest points, and completed-count entries repeatedly.

### P1 — state, route, item, or shared-actor correctness

1. Native states 6 and 7 are active finish states but Kojo and the journal have
   no branch, silently blocking those saves.
2. Primary count and authored colour bits can diverge; placements blindly
   increment old progress and Kojo validates only state 5.
3. Correct-spindle handlers write colour/progress before deleting the cog.
4. Smiths gloves (i) do not cool the black cog; the water interaction does not
   pick up the cog in the same action. Jug behavior is unresolved.
5. Only five of eleven poison-pen rats receive movement/death queues.
6. Rat-door state commits before scene success, repeated poison is consumed,
   and interruption/multiplayer behavior is unverified.
7. World-shared 500-tick lever/gate loc mutations have no concurrency, logout,
   collision, or expiry evidence.
8. Kojo's exact quest handler shadows watch acquisition and hard-clue service.

### P2 — dialogue, metadata, presentation, and tooling debt

1. Start uses an old three-choice loop instead of the current quest-start
   selection; the reward branch is absent from the current transcript.
2. Dbrow omits the black-cog item method and 500-coin reward metadata.
3. Journal misspells Ardougne and has no composite-state diagnostics.
4. Lever/rat sound presentation is dropped.
5. No quest-local start/reset harness or interaction-driven automated route
   covers the real maps.
6. Dungeon `Alone` playback has not been captured in a real client.

## 12. Modernization work packages

### Package 0 — native contract and state invariant

- Add constants/policy for native states 6/7 after live capture.
- Define a single placed-colour count/validation procedure over `%cog_bits`.
- Reconcile primary state from committed colour facts at safe boundaries and
  provide conservative repair for legacy/imported saves.
- Add current item/reward metadata to the dbrow without inventing skill or quest
  requirements.

### Package 1 — Kojo dispatcher and modern start

- Centralize clue, watch, quest, and ordinary Kojo Talk-to precedence.
- Use the standard members/start/accept/refuse interface and current transcript.
- Preserve all clean progress re-talks, add 6/7 recovery, and make watch grants
  no-space/duplicate safe.

### Package 2 — cog acquisition and placement

- Centralize the one-cog pickup predicate and preserve souvenir semantics.
- Implement one protected black-cog cooling/take transaction, including Smiths
  gloves (i) and live-validated jug behavior.
- Replace four duplicated spindle bodies with one atomic colour-aware helper.
- Validate wrong spindle, already placed, completed, repeated-use, full
  inventory, death, drop, and duplicate-spawn behavior.

### Package 3 — rat scene and gate ownership

- Queue all three native rat variants and define respawn/scene ownership.
- Make poison consumption and durable gate state exactly once and resumable.
- Audit both levers and both gate paths under simultaneous players.
- Move to scoped locs if current live behavior is personal, or harden the
  shared-world mutation if it is global.
- Retain legitimate extra rat poison for Ratcatchers.

### Package 4 — atomic completion

- Validate all four colours and native finish state.
- Acquire an exactly-once completion guard before any queued/re-entrant path.
- Grant one 500-coin reward with full-inventory recovery/space handling.
- Invoke `~quest_complete_rewards` exactly once and make 5/6/7/8 replay safe.

### Package 5 — journal, music, adapters, and tests

- Correct dynamic journal text for colours, finish states, and repairs.
- Add a non-destructive quest setup/reset harness that establishes real route
  preconditions without bypassing interactions.
- Capture area music, navigation, completion scroll, and Kojo shared branches.
- Retain the generic state/QP-only `::complete` policy and test it twice.

## 13. Verification matrix

### Gate A — static contract

- Quest dbrow, journal arm, completion call, cheat arm, start actor, spawn, and
  end state resolve.
- Every cog, spindle, entrance, lever, gate, trough, rat variant, water/glove
  method, reward, music row, and trail item resolves symbolically.
- Primary and side-state readers/writers are enumerated; no undeclared parallel
  progress state exists.
- Trigger-owner report proves Kojo and all loc/item operations have intentional
  precedence and delegation.
- `python3 tools/questhelper_extract.py clocktower --check` remains green.

### Gate B — clean gameplay route

1. Start from Kojo on a members world; refuse once and accept through the
   current quest-start UI.
2. Complete the four cogs in several orders, carrying only one at a time.
3. Exercise the long-passage wall/cell route and every ladder/stair landing.
4. Cool/take the black cog with bucket, ice gloves, Smiths gloves (i), and the
   live-confirmed jug policy.
5. Open the rat pen, poison once, observe all eleven pen rats die, traverse the
   loosened gate, and place the white cog.
6. Verify each correct spindle consumes exactly one item and each decoy leaves
   item/state unchanged.
7. Finish through Kojo, receive exactly 500 coins and 1 quest point, and observe
   the modern scroll/jingle.
8. Verify post-quest Kojo, all navigation, watch service, and souvenir rules.

### Gate C — interruption, inconsistency, and multiplayer cases

- Full inventory with and without a coin stack at completion.
- Rapid double Talk-to, duplicate completion queue, scroll interruption, logout,
  reconnect, death, and region change around states 5/6/7/8.
- Every inconsistent primary × colour-bit combination, with explicit repair or
  refusal expectations.
- Repeated correct/wrong spindle use, missing item at commit, retained duplicate,
  dropped duplicate, death pile, banked cog, and post-completion pickup.
- Black-cog cooling cancellation, full inventory, water conversion, relog after
  cooling, and duplicate pickup.
- Repeated poison, extra Ratcatchers poison, relog during rat movement/death,
  rat already dead/in combat/path-blocked, and rat respawn.
- Two-player lever, gate, poison, and rat-scene interleavings, including loc
  expiry with players on both sides.
- Nonmember Kojo interaction and every Kojo precedence combination involving
  chart, sextant, watch, clue, quest state, and no inventory space.

### Gate D — evidence required before `verified-modern`

- `make -C src torirsserver-scripts` and intended-cache
  `ToriRSServer_Pack --check-only` results.
- Passing targeted state, spindle, black-cog, rat-scene, loc-concurrency,
  completion-idempotence, reward-space, Kojo-dispatch, journal, and cheat tests.
- Real-client clean-route capture from Kojo through scroll and post-quest talk.
- Real-client captures for all navigation origins, `Alone`, every black-cog
  method, eleven-rat scene, two-player loc behavior, watch grant, and state 6/7
  recovery.
- State/item/quest-point/completed-count deltas proving exact-once completion.
- Updated dossier status and a precise record of any remaining cosmetic
  deviation.

## 14. Exit criteria

Clock Tower may move from `audit-pending` to `verified-modern` only when the
real map route works from Kojo through all four cogs; native states 0–8 and the
authored colour facts remain consistent and resumable; black-cog alternatives
match current live behavior; the rat/lever/gate scene works for every native
rat variant and concurrent players; correct spindle and completion transactions
are atomic/idempotent; exactly 500 coins and 1 quest point are awarded once;
souvenir cogs and extra rat poison retain their documented behavior; Kojo's
watch/clue services coexist with quest dialogue; the journal and music are
correct; and all Gate D evidence is recorded.

Until then, the present implementation should be treated as a broadly playable
legacy outline with unverified completion and shared-world safety, not as a
modernized quest.
