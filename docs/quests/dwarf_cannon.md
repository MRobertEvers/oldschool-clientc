# Dwarf Cannon modernization audit

Status: `audit-pending` — the local tree contains the native 0–11 quest
ladder, six per-player railing transforms, the tower/cave route, a dynamic
journal, current cache assets for the toolkit puzzle, Nulodion's six-line
shop, and a cannonball recipe. It is not completable through normal play and
its permanent reward is not implemented. Nulodion never advances state 9 to
10; Lawgof awards XP and quest points from state 9 but writes state 10 instead
of the cache end state 11. The railing and cannon repairs preserve old
LostCity mechanics, Lollk is a shared global NPC, item grants and hand-ins are
not transactional, and there is no player multicannon assembly, loading,
firing, ownership, decay, pickup, or recovery system.

Audited: 2026-08-17

Governing plan: [Quest modernization plan](../QUEST_MODERNIZATION_PLAN.md). This
record applies Gates A–D to Captain Lawgof and Nulodion, all eleven primary
states, six railing bits, hammer/railing/remains/toolkit/notes/mould ownership,
tower and cave access, Lollk concurrency, the cache-native toolkit interface,
completion and migration, the post-quest multicannon, cannonball production,
downstream quests and diary events, journal/debug tools, and real-client
verification. It is an implementation specification, not evidence that the
quest or cannon system is complete.

## 1. Authoritative references

The current article, quick guide, and transcript define the critical route,
inventory requirements, recovery dialogue, toolkit puzzle, hand-in, and
rewards. The multicannon, Nulodion, Combat Achievements, and steel-cannonball
pages define the much larger permanent unlock. Revisions were resolved through
the OSRS Wiki API on 2026-08-17.

| Reference | Pinned revision | Use in this audit |
| --- | --- | --- |
| [Dwarf Cannon](https://oldschool.runescape.wiki/w/Dwarf_Cannon?oldid=15166602) | 15166602, 2026-04-06 | Identity, requirements, route, rewards, and downstream requirements |
| [Dwarf Cannon/Quick guide](https://oldschool.runescape.wiki/w/Dwarf_Cannon/Quick_guide?oldid=14957845) | 14957845, 2025-08-03 | Exact critical path, choices, toolkit order, and two-item finale |
| [Transcript:Dwarf Cannon](https://oldschool.runescape.wiki/w/Transcript%3ADwarf_Cannon?oldid=15263269) | 15263269, 2026-07-14 | Offer, re-talks, recovery, failure effects, optional cannon ops, and completion |
| [Dwarf multicannon](https://oldschool.runescape.wiki/w/Dwarf_multicannon?oldid=15300990) | 15300990, 2026-08-14 | Assembly, loading, accuracy, targeting, XP, decay, recovery, and prohibited areas |
| [Transcript:Nulodion](https://oldschool.runescape.wiki/w/Transcript%3ANulodion?oldid=15240039) | 15240039, 2026-06-26 | Full-set purchase, separate-parts shop, cannon information, and lost-cannon recovery |
| [Nulodion](https://oldschool.runescape.wiki/w/Nulodion?oldid=15213364) | 15213364, 2026-05-20 | Shop stock, prices, and discounted full-set service |
| [Captain Lawgof](https://oldschool.runescape.wiki/w/Captain_Lawgof?oldid=15012950) | 15012950, 2025-11-01 | Quest ownership and location |
| [Lollk](https://oldschool.runescape.wiki/w/Lollk?oldid=14995931) | 14995931, 2025-09-28 | Rescue identity and post-rescue route |
| [Railing](https://oldschool.runescape.wiki/w/Railing?oldid=15297059) | 15297059, 2026-08-13 | Hammer requirement, Crafting success curve, and four failure outcomes |
| [Dwarf remains](https://oldschool.runescape.wiki/w/Dwarf_remains?oldid=15183305) | 15183305, 2026-04-22 | Tower pickup, unique ownership, and hand-in |
| [Toolkit](https://oldschool.runescape.wiki/w/Toolkit?oldid=15227373) | 15227373, 2026-06-06 | Puzzle interface, duplicate recovery, and post-repair deletion |
| [Nulodion's notes](https://oldschool.runescape.wiki/w/Nulodion%27s_notes?oldid=15184092) | 15184092, 2026-04-22 | Duplicate recovery and required final hand-in |
| [Ammo mould](https://oldschool.runescape.wiki/w/Ammo_mould?oldid=15207157) | 15207157, 2026-05-06 | Quest grant, shop source, and cannonball tool ownership |
| [Steel cannonball](https://oldschool.runescape.wiki/w/Steel_cannonball?oldid=15301346) | 15301346, 2026-08-14 | Level, output, XP, mould variants, timings, granite upgrade, and partial-quest wording |
| [Instruction manual](https://oldschool.runescape.wiki/w/Instruction_manual?oldid=15282352) | 15282352, 2026-07-30 | Post-quest manual ownership and current explanatory surface |
| [Combat Achievements](https://oldschool.runescape.wiki/w/Combat_Achievements?oldid=15296909) | 15296909, 2026-08-13 | Cannon capacities of 35, 45, and 60 after medium, hard, and elite tiers |
| [Between a Rock...](https://oldschool.runescape.wiki/w/Between_a_Rock...?oldid=15292292) | 15292292, 2026-08-10 | Direct quest prerequisite and ammo-mould consumer |
| [Morytania Diary](https://oldschool.runescape.wiki/w/Morytania_Diary?oldid=15280663) | 15280663, 2026-07-29 | Medium task for making cannonballs at Port Phasmatys |
| [Goblin Cave](https://oldschool.runescape.wiki/w/Goblin_Cave?oldid=15267039) | 15267039, 2026-07-18 | Cave route, crate room, and shared-world access |
| [Land of the Goblins](https://oldschool.runescape.wiki/w/Land_of_the_Goblins?oldid=15292373) | 15292373, 2026-08-10 | Later quest sharing the unconditional cave entrance |

Transition aid only: Quest Helper at commit
[`5ea99d5`](https://github.com/Zoinkwiz/quest-helper/tree/5ea99d5ea9ba3fb096ebe7b5ed02d80883e9819d/src/main/java/com/questhelper/helpers/quests/dwarfcannon)
observes active states 0–10, all six railing transforms, tower/cave zones,
remains, Lollk's crate, the toolkit interface and five relevant varbits, both
Nulodion items, and the three reward classes. `python3
tools/questhelper_extract.py dwarfcannon --check` resolves the dbrow and every
referenced gameval. Quest Helper is a state/test oracle, not proof of local
server behavior.

The local `../LostCity_Server` checkout is useful provenance, not a canonical
target. It contains the omitted `cannon_setup.rs2`, `cannon_fire.rs2`, original
Nulodion transitions, and completion queue, but also preserves manual assembly,
a fixed 30-ball capacity, and older combat/decay rules. Modernization may reuse
its symbolic assets and engine patterns only after comparison with the pinned
current contract.

## 2. Canonical contract

Dwarf Cannon is a members-only, novice, short quest released 27 May 2003. It
starts with Captain Lawgof south of the Coal Trucks and north-west of the
Fishing Guild. There are no quest, skill, or mandatory combat requirements. A
hammer is the only required item and must be supplied by Lawgof when absent;
another lies in the nearby building.

A canonical run must:

1. show the standardized `Start the Dwarf Cannon quest?` Yes/No offer, grant
   six stackable railings, and grant a hammer if the player does not own one;
2. require both a hammer and one railing for each of six independent repairs,
   use the Crafting-only success curve, consume a railing only on success, and
   let Lawgof replace railings when none remain;
3. send the player through both watchtower ladders, let them take one set of
   dwarf remains with safe full-inventory behavior, and require Lawgof to
   receive the remains before assigning the Goblin Cave search;
4. leave the Goblin Cave generally accessible, reveal Lollk only at the
   appropriate crate and for the triggering player, commit rescue safely even
   if the remaining dialogue is skipped, and return the player to Lawgof;
5. grant and replace the toolkit, open cache interface 409 when it is used on
   the broken cannon, pair hook→spring, pliers→safety switch, and toothed
   tool→gear, then delete all carried toolkits when Lawgof verifies the repair;
6. advance 9→10 only after Nulodion successfully gives both his notes and an
   ammo mould, replace either missing item independently, and require both at
   Lawgof's final hand-in;
7. consume one notes+mould pair atomically and complete 10→11 exactly once with
   1 quest point, 750 Crafting XP, Black Guard membership, and the ability to
   purchase/use a dwarf multicannon and make cannonballs; and
8. expose a complete post-quest Nulodion service: discounted 750,000-coin set,
   separately priced parts, mould/manual stock, cannon information, and valid
   lost-cannon recovery.

The fixed quest cannon's Fire and Pick-up options are optional transcript
interactions: Fire acknowledges that it now works, while Pick-up is refused by
Lawgof. Neither advances the quest. The player does not have to fight goblins
or fire a player-owned cannon to complete the quest.

The permanent reward is a combat system, not merely four shop items. Current
assembly, ownership, loading, rotation, target geometry, accuracy, XP, capacity,
decay, world-hop/death persistence, prohibited zones, boss destruction, and
Nulodion recovery are part of the acceptance boundary because the reward
explicitly promises that the player may use the cannon.

## 3. Native identity and persistence

| Field | Native value / expected behavior |
| --- | --- |
| Quest metadata ID | 47 |
| Dbrow | `quest_dwarfcannon` |
| Type / difficulty / length | Members; novice; short |
| Release | 27 May 2003 |
| Start | `lawgof2` (NPC 5191), coordinate 42077572 |
| Primary carrier | `%mcannon`, native clean permanent varp 0 |
| Side carrier | `%mcannonmulti`, native permanent/transmitted varp 1 |
| Cannon carriers | `dropcannon`, `rockthrower`, and `ownedmcannon`, native varps 2–4 |
| Start / end | 0 / 11 |
| Rewards | 1 QP; 7500 raw Crafting XP (750 displayed XP) |

The dbrow correctly records members, novice, short, the release date, start
NPC/coordinate, end state, quest point, and Crafting XP. Its recommendation
text has a minor grammatical error: `Learn how to use a Dwarf Multicannons.`

### 3.1 Primary ladder

| `%mcannon` | Native checkpoint | Current local result |
| ---: | --- | --- |
| 0 | Not started / Lawgof offer | Starts, but uses an old custom offer and omits the conditional hammer |
| 1 | Fix six railings | Per-player transforms work; item/tool gates, failure table, recovery, and transaction order are wrong |
| 2 | Check the watchtower | Ladder route exists; remains pickup representation needs real-client proof |
| 3 | Find the Goblin Cave | Lawgof writes this after deleting remains; cave entry advances it |
| 4 | Find Gilob's son | Crate spawns one global Lollk and progression waits for full dialogue |
| 5 | Return to Lawgof | Correct broad checkpoint; toolkit grant is unchecked |
| 6 | Repair the cannon | First Inspect merely advances to 7; using the toolkit does not open the cache interface |
| 7 | Cannon repair active | Random four-part text menu replaces the current three-pair puzzle |
| 8 | Cannon repaired / report | Fixed loc appears; toolkit is retained; accepting the errand advances to 9 |
| 9 | Speak to Nulodion | Both items are granted unchecked, but the required write to state 10 is missing |
| 10 | Return with notes and mould | Production reaches this only after already awarding rewards; journal/NPC semantics disagree |
| 11 | Complete | Unreachable through the normal route; only the generic quest cheat writes it |

The values themselves are native and should not be renumbered. The
implementation must restore their ownership and transition boundaries rather
than inventing a replacement ladder. Quest Helper intentionally groups 2/3,
4/5, and 9/10 under inventory/zone-aware conditional steps; tests must cover
both values instead of assuming one visible screen per integer.

### 3.2 Packed repair/railing state and cannon carriers

`mcannonmulti` is not a scratch integer. The cache packs tool selection and
quest transforms into it:

| Bit(s) | Cache name / purpose | Current local use |
| --- | --- | --- |
| 0 | `mcannonmulti_tool1`, toothed-tool selection | Incorrectly persisted as old "pipe fixed" progress |
| 1 | `mcannonmulti_tool2`, pliers selection | Incorrectly persisted as old "barrel fixed" progress |
| 2 | `mcannonmulti_tool3`, hook selection | Incorrectly persisted as old "axle fixed" progress |
| 3 | `mcannon_safety_on` | Incorrectly used as old "shaft fixed" progress |
| 4 | `mcannon_spring_set` | Never read or written locally |
| 5–10 | `mcannon_railing1_fixed` … `mcannon_railing6_fixed` | Correct per-player loc transforms |
| 10 | `mcannon_taken_corpse` alias | Overlaps railing 6 and has no safe independent meaning |

Tool-selection bits are interface state, not four durable repaired components.
The current puzzle needs durable spring/safety completion and a final correct
gear action; selection must clear when the interface closes or a repair
finishes. Never whole-write `%mcannonmulti`, and do not build new logic on the
bit-10 corpse alias.

The cache also names the historical cannon stage, loaded-ammo, and owned-coordinate
carriers. The local server has no script that uses them. A modern cannon owner
record additionally needs world/generation, assembly stage, ammunition type and
count, setup/break deadlines, reclaim status, and actor identity. Validate
whether the native carriers can safely hold each datum; add an explicit
versioned server record for anything they cannot represent rather than
overloading packed or unrelated varps.

### 3.3 Save migration and reward reconciliation

Migration must preserve primary values 0–11 but cannot treat legacy state 10
as unambiguous. In the intended ladder, 10 means "Nulodion supplied both items;
return to Lawgof." In this local build, the only production write to 10 occurs
after `stat_advance(crafting, 7500)` and `~quest_complete_rewards`, so those
accounts have seen rewards but are still below the dbrow end state. Imported
or manually prepared saves may use the intended meaning instead.

Use a one-time migration/reward ledger with these rules:

- recompute quest points and completed count from authoritative end states;
- for locally provenance-marked state-10 rewards, promote to 11 without adding
  XP again and mark the transaction delivered;
- for genuine pre-finale state 10, preserve the final hand-in and grant the
  reward through the new idempotent transaction;
- when provenance is unavailable, log the ambiguity and apply one documented
  grandfather policy rather than guessing from total Crafting XP alone;
- a state-11 account below 750 total Crafting XP can safely be raised to the
  750-XP floor; at or above that floor, do not add another 750 without proof;
- state 2+ should have all six railing transforms consistent with the completed
  fence; state 1 preserves its actual six-bit partial progress;
- reset old state-7 "component" bits into a clean canonical puzzle, while
  state 8+ proves repair and may receive canonical completed side state;
- preserve banked/surplus quest items, cannon parts, cannonballs, and higher
  legitimate XP; cleanup should occur through normal destroy/hand-in rules;
  and
- if imported cannon-owner carriers describe no matching live actor, convert
  them to a reclaimable record while preserving loaded ammunition, never to
  free duplicate parts or silent deletion.

The migration and reward adapter must be idempotent across repeated logins,
crashes, debug completion, and future schema versions.

## 4. Current implementation surface

The direct quest root contains 13 files and 671 lines. Adding the cannonball
recipe and Nulodion shop makes the immediate route/reward surface 756 lines,
before shared completion, combat, diary, and downstream quest owners.

| File | Lines | Current responsibility |
| --- | ---: | --- |
| `quest_mcannon/configs/cannon.dbrow` | 13 | Historical dwarf-zone coordinate table, currently unused |
| `quest_mcannon/configs/quest_mcannon.constant` | 18 | Native state 0–11 constants |
| `quest_mcannon/configs/quest_mcannon.varp` | 5 | Primary varp declaration |
| `scripts/mcannon_commander.rs2` | 185 | Lawgof/Nulodion route, item grants, broken finale, and shop delegation |
| `scripts/mcannon_railings.rs2` | 94 | Six railing checks, transforms, failure roll, and item deletion |
| `scripts/mcannon_ladder.rs2` | 18 | Quest-gated second watchtower ladder |
| `scripts/mcannon_remains_book.rs2` | 22 | Ground-item remains pickup and legacy manual pages |
| `scripts/mcannon_cave_guard.rs2` | 33 | Cave/mudpile travel, state-3 entry advance, and guard dialogue |
| `scripts/mcannon_crate_child.rs2` | 47 | Global Lollk spawn, dialogue, state advance, and deletion |
| `scripts/mcannon_broken_cannon.rs2` | 89 | Old random text-menu repair |
| `scripts/mcannon_doors.rs2` | 39 | Quest-gated cannon/Nulodion doors |
| `scripts/nulodions_notes.rs2` | 8 | Legacy notes mesbox |
| `scripts/mcannon_journal.rs2` | 100 | Dynamic journal for all declared states |
| `skill_smithing/scripts/smelting/cannonballs.rs2` | 61 | Four steel balls and 25.6 XP per bar, with an unreachable state-11 gate |
| `shop/misc/configs/nulodion__1.inv` | 13 | Four parts, manual, and mould stock |
| `shop/misc/scripts/nulodion__1.rs2` | 11 | Unconditional Trade-op shop open |

Gate A also owns or consumes:

- cache interface 409 (`mcannon_interface`) and its tool, spring, safety, gear,
  close-button, model, and CS1 assets;
- `%mcannonmulti` plus `dropcannon`, `rockthrower`, and `ownedmcannon`;
- all cannon-part/item/loc/projectile/animation/sound configs and the current
  granite and double-mould objects;
- Lawgof/Nulodion/guard static spawns, tower/cave scenery, transformed fence,
  remains representation, and the shared Goblin Cave entrance;
- shared quest completion, points/count, reward scroll, journal, Drop/Destroy,
  inventory transactions, combat hit rolls, private loc/NPC actors, timers,
  world hopping, logout, death, and zone restrictions;
- Nulodion's shop and post-quest dialogue services;
- Between a Rock..., Land of the Goblins, cannonball Smithing, Combat
  Achievements, Morytania diary events, and all activities that restrict or
  destroy cannons; and
- generic quest cheat plus stale content/skill/Quest Helper audit records.

Core quest triggers are symbolic and unique by opcode/type. The route uses no
raw numeric entity IDs and already has per-player cache loc transforms for the
six railings, a dynamic journal, modern `~p_choice*`, and the shared completion
scroll. Those are useful foundations. They do not offset the missing state
transitions or reward system.

The comments and queue records claiming the quest is `audited-fixed`, "fully
ported," and complete after the 2026-08-12 commander addition are contradicted
by current code. The same tree's `SKILLS_CONTENT_PORT_QUEUE.md` correctly marks
the dwarf multicannon itself blocked. Both statements must be reconciled after
implementation, not used as acceptance evidence.

## 5. Start and railing repair

Lawgof's broad recruitment story and accept/refuse choices are present, but the
local start is a paraphrased old branch rather than the current standardized
quest offer. On acceptance it closes dialogue, performs an unchecked six-rail
add, and advances state. It never checks for or grants the hammer required by
the current transcript. Verify the nearby static hammer spawn, but do not make
that optional source substitute for Lawgof's conditional grant.

The six loc transforms and Crafting success formula (`stat_random(crafting,
60, 150)`) match the native cache and current success range. The surrounding
contract does not:

- no railing or hammer is required before the success roll;
- success marks the transform before an unchecked `inv_del`, so a missing or
  stale source slot can create a repaired fence for free;
- Lawgof never replaces railings when an unfinished player owns none;
- every failure deals `base Hitpoints / 10`, capped at 3, whereas the current
  four equiprobable outcomes are 1 damage, 2 damage, -1 Crafting, or -1
  Strength; and
- each attempt inserts an old Try/Leave menu that is absent from the current
  direct Inspect flow.

Create one railing transaction that validates exact stage, unfixed transform,
hammer ownership, concrete railing slot, range, and busy state before rolling.
On success, delete exactly one railing and set exactly one bit atomically. On
failure, keep the item/bit unchanged and select the canonical four-way effect
without killing the player or draining below the engine's normal floor. A
stale/repeated packet must not repair twice.

Lawgof should replace one railing only when at least one fence remains broken
and the player owns none in the inventory scope used by live behavior. Full
inventory must produce the current refusal and leave state/items unchanged.
Dialogue and inventory commits need protected continuations so relogging at any
line cannot duplicate six railings, a hammer, or stage progress.

## 6. Watchtower, remains, cave, and Lollk

The generic first ladder, quest-gated second ladder, reverse ladders, cave
entrance, and mudpile exit all resolve. Entering the cave is intentionally
unconditional because the location remains public and is reused by Land of the
Goblins; only the exact state-3 transition belongs to Dwarf Cannon.

The tower deserves an explicit client test. The cache places a
`mcannonremains_multiloc` scenery transform and Quest Helper targets that loc,
but the only local handler is `[opobj3,mcannonremains]` for a ground item. The
text sources do not prove whether a correctly owned ground object is also
spawned at runtime. Confirm that a real state-2 player can see, click, and
receive one remains; if the cache loc is the live click target, add its exact
loc handler and private item transfer rather than relying on an unrelated
ground-object opcode. Full inventory should leave the remains recoverable.

Lawgof's hand-in checks inventory, plays dialogue, then performs an unchecked
delete followed by a state write. Revalidate and atomically consume one remains
with 2→3. Loss before hand-in must leave the tower source available. Repeated
handoff, bank-only ownership, slot movement, full inventory pickup, Drop,
Destroy, death, and reconnect all need coverage.

Lollk is currently a global timed NPC. The crate searches a radius for any
`dwarfchildtw1`, spawns one global child for 120 cycles, and routes any player
who can talk to him through the same handler. Consequences include:

- one player's child makes another player's crate appear empty;
- another state-4 player can use the first player's child to advance;
- either dialogue can delete the shared NPC; and
- rescue is written only after the full conversation, despite the current
  quick guide explicitly allowing the player to skip finishing it.

Commit rescue when the correct player unties the child, then play dialogue and
walk-off as a best-effort presentation. Use a player-owned NPC/scene or a
generation-keyed actor registry so concurrent players cannot find, talk to, or
delete each other's Lollk. An interrupted scene must resume or conclude at the
state-5 return checkpoint without spawning duplicates.

## 7. Cannon repair interface

Revision 239 contains the complete IF1 repair surface: interface 409, three
tool components, safety switch, spring, gear, firing mechanism, models, and
CS1 reads for tool-selection varbits. Quest Helper resolves all of them. No
server script opens the interface or handles its button packets.

Instead, the local first Inspect changes state 6→7. Further inspections run an
older random minigame that says five components—pipe, gun, barrel, axle, and
shaft—are damaged but offers only pipe, barrel, axle, shaft, and None. Each of
four successful random repairs grants 1.2 Crafting XP and persists a tool or
safety bit as component progress. A final extra Inspect is required to notice
all four bits and write state 8. This is old machinery, not the current quest.

Replace it with the native interface contract:

1. using a toolkit on the state-6/7 broken cannon opens interface 409 and enters
   or resumes state 7;
2. clicking one tool selects exactly that tool and clears the other selection
   bits;
3. hook on spring sets `mcannon_spring_set`; pliers on safety sets
   `mcannon_safety_on`; toothed tool on gear completes the final repair after
   the required pairings;
4. wrong pairings provide current feedback without progress, XP, or state
   corruption;
5. the final correct pairing closes the interface and commits state 8 once,
   with no extra random success roll or incremental Crafting XP; and
6. close, logout, reconnect, duplicate button, wrong interface, remote packet,
   and malformed selection are safe.

Lawgof must replace a missing toolkit at states 6–7 subject to capacity, and
must delete all inventory copies when acknowledging state 8 as the current
item page specifies. Add the fixed quest cannon's Fire and Pick-up transcript
handlers without sharing them with player-owned cannons at other coordinates.

## 8. Nulodion, final hand-in, and completion

The current state machine breaks at Nulodion. At state 9 he adds notes and a
mould but never writes state 10. If notes are present, his re-talk skips all
replacement logic even when the mould is missing. Both adds are unchecked, so
one free slot can create a split state that the dialogue cannot repair
directly.

Grant both items only after reserving two slots (or exact freed/stackable
capacity), then atomically write 9→10. At state 10, inspect each inventory item
independently and replace only what is missing, matching the transcript. Banked
duplicates are allowed by the canonical notes/toolkit behavior, so recovery
must be deliberately inventory-scoped and safe against banking/re-talk loops.

The local Lawgof finale is attached to state 9, checks only the notes, consumes
neither item, grants 750 XP, calls `~quest_complete_rewards`, and writes state
10. That shared proc adds one QP and one completed-count increment and paints
the scroll; it does not write quest state. The result is a rewarded but
incomplete quest, a state-10 journal that still asks for Lawgof, a permanently
locked state-11 cannonball recipe, and blocked downstream completion checks.
An interruption before the final state write can also replay the non-idempotent
reward path.

Move the finale to state 10 and use one protected transaction:

- lock and revalidate one notes slot and one mould slot;
- consume exactly one of each;
- durably record the 750-XP reward entitlement and state 11;
- derive exactly 1 QP and one completed-count increment from the quest dbrow;
- play one completion jingle and mount one reward scroll; and
- route every repeated talk, queue, packet, login resume, or debug call to a
  no-op/post-quest branch.

Trace the real reward scroll before choosing its rotating model. Passing
`nulodions_notes` currently displays an item that canonical Lawgof receives;
use the pinned scroll's model or hide the slot rather than treating a convenient
quest item as a reward.

Post-quest Lawgof should use current dialogue. Nulodion and his direct Trade op
must both enforce state 11 server-side; a locked building door is not a
sufficient authorization boundary.

## 9. Permanent dwarf multicannon reward

There are zero local triggers for `twpart1` Set-up or for the player cannon's
base, stand, barrels, full cannon, load, fire, empty, or pickup options. The
four parts can be bought, but they cannot become a cannon. The native cannon
varps, locs, models, sequences, projectile, sound, category, and `dwarf_zones`
table exist; the server behavior does not.

Implement the reward as one shared, owner-safe combat subsystem with at least:

### Purchase and assembly

- Nulodion's 750,000-coin dialogue purchase must atomically require six free
  slots, remove the coins, and add four parts, one mould, and one manual;
- the separate shop retains five of each part at the current 200,625-coin
  individual price, plus manual and mould stock;
- setup requires state 11, all four parts, membership, one-cannon ownership,
  a valid world/zone, and a clear 3×3 footprint;
- placing the base automatically assembles base→stand→barrels→furnace from the
  inventory with current animations, while interruption either resumes safely
  or returns the exact committed pieces; and
- another player cannot assemble, load, fire, empty, repair, or pick up the
  owner's cannon.

### Ammunition, rotation, and combat

- clicking/reloading consumes the selected supported ammunition atomically,
  prefers granite when both types are present, and never mixes types inside a
  partially loaded cannon;
- capacity is 30 normally, 35/45/60 after medium/hard/elite Combat
  Achievements, with exact boundary and downgrade tests;
- the barrel turns 45 degrees per tick through all eight directions, checks
  current target wedges/line of sight, and spends at most one ball per valid
  shot for that facing;
- single- and multi-combat selection, large-NPC south-west tiles, overlapping
  double-shot squares, immunity, attribution, aggression, and kill/drop credit
  match current combat rules;
- steel/granite max hits are 30/35; damage gives 2 Ranged XP per damage and no
  Hitpoints XP; and
- accuracy uses the player's selected style/equipment contract against heavy
  ranged defence, while excluding target-specific bonuses such as a Slayer
  helmet unless current evidence says otherwise.

Do not copy the old upstream `npc_hunt`/hit-roll path blindly. Route damage,
ownership, deaths, drops, multicombat, immunity, and achievements through the
modern combat/event owners so cannon kills cannot bypass encounter rules or
credit the wrong player.

### Lifetime and recovery

- after 25 minutes deployed, the cannon breaks and can be repaired by its
  owner; after another 10 minutes unrepaired, it disappears into recoverable
  state;
- pickup returns all four exact parts and remaining ammunition subject to an
  atomic capacity check;
- logout, world hop, death (including Wilderness death), server restart,
  region unload, and owner disconnect preserve or reconcile the live actor
  without duplication;
- Nulodion refuses recovery while a valid staged/live cannon still exists,
  otherwise restores the correct parts and stored ammunition for a decayed or
  destroyed cannon; and
- prohibited zones and boss/activity destruction use centralized named policy
  and current messages rather than one incomplete quest-local coordinate list.

This is a shared modern-engine feature even though Dwarf Cannon owns the
unlock. If the engine lacks persistent player-owned loc actors or durable
deadline reconciliation, add those as general tested capabilities rather than
encoding cannon ownership in global scenery and timers.

The instruction manual and Nulodion's notes also need current text. The local
manual describes manually adding each part and a fixed 30-round world; current
assembly is automatic and capacity can reach 60. Update all pages only after
the implemented mechanics are final.

## 10. Cannonball production and diary event

The local single-mould recipe has two good corrections: one steel bar becomes
four steel cannonballs and awards exactly 25.6 Smithing XP. It also checks
members, level 35, mould, and input before mutation. In ordinary production it
is unusable because state 11 cannot be reached.

The remaining contract is incomplete:

- it hard-requires state 11, while the current Steel cannonball page says the
  ordinary mould obtained during partial Dwarf Cannon progression is enough;
  the quest and general cannonball pages use broader completion wording;
- Nulodion's missing 9→10 transition means even the granted mould has no
  canonical partial stage locally;
- `double_ammo_mould` exists but has no eight-ball/two-bar recipe;
- the loop uses a fixed six-delay presentation rather than current single-mould
  Make-X timing (initial 8, then 10 ticks per the pinned item page);
- current ancient-furnace acceleration, granite-dust conversion, cancellation,
  inventory mutation, and batch UI behavior are absent; and
- no Morytania medium event is emitted for a successful batch at the Port
  Phasmatys furnace.

Resolve the Wiki's partial/completion wording conflict with live-client
evidence before fixing the central predicate. At minimum, distinguish the
quest-granted state-10 mould path, completed state 11, and double-mould source;
do not simply preserve a gate whose end state is unreachable. Inputs must be
locked before animation and consumed only when output capacity is guaranteed.
The diary event fires after a committed valid Port Phasmatys batch, never on
opening a menu, failing a gate, crafting elsewhere, or receiving dropped balls.

## 11. Downstream quests and shared owners

Between a Rock... canonically requires Dwarf Cannon. Its local prerequisite
proc deliberately checks only Fishing Contest because an older audit found
Dwarf Cannon unstartable. The commander file later fixed 0→1 and 8→9 but not
10→11, so that soft-skip remains stale and exploitable. Once state 11 and
migration are reliable, restore the hard Dwarf Cannon check, correct its dbrow
metadata, and retest states 10 versus 11.

The Morytania medium diary's eighth task requires a valid cannonball batch at
Port Phasmatys. The generic diary framework contains only area/tier counters;
no quest-specific cannonball event owner was found. Implement the exact task
rather than incrementing a generic count from every furnace.

Land of the Goblins shares `mcannoncave`. Preserve unconditional physical
entry and compose quest-specific NPC/scenery dispatch inside the cave. Dwarf
Cannon must advance only its state-3 player, while later quest actors must not
hide Lollk, change the crate, or make the exit conditional.

Player cannons affect virtually every combat/activity owner. Audit Slayer and
kill credit, single/multi zones, instanced bosses, quest bosses, minigames,
Combat Achievements, immunity lists, restricted maps, death, loot, and NPC
retaliation. A successful projectile must use the same authoritative actor and
encounter policies as ordinary player attacks.

## 12. Journal, debug tools, and stale records

The journal is wired and has entries for every declared value. It correctly
reads the six railing bits and inventory remains. It cannot describe the local
rewarded-but-incomplete state 10: after the broken finale it still says to take
notes to Lawgof. State 11's completion entry is never reached normally. Repair
the state machine first, then update journal text for exact current locations,
owned/missing quest items, toolkit status, and permanent unlocks.

`::complete quest_dwarfcannon` writes only state 11. It grants no Crafting XP,
QP, completed count, side-bit consistency, item cleanup, reward ledger, cannon
entitlement migration, or scroll. Replace direct writes with an idempotent
quest fixture that can explicitly prepare a checkpoint or execute a complete
rewarded state. Test tooling must never leave a normal save in an impossible
state.

Update or supersede these contradictory records after implementation:

- `QUESTHELPER_CONTENT_PORT_QUEUE.md` says `audited-fixed` and claims a real
  completion call, but the actual caller writes state 10;
- `SMITHING_COMPLETION_PLAN.md` and cannonball comments call Dwarf Cannon fully
  ported and impose an unreachable end-state gate;
- `CONTENT_PORT_QUEUE.md` and `docs/quests/between_a_rock.md` still describe the
  pre-commander 0→1/8→9 gap but correctly warn that 10→11 was absent; and
- `SKILLS_CONTENT_PORT_QUEUE.md` correctly marks player multicannon setup/fire
  blocked and must remain so until the shared subsystem passes Gate D.

Documentation labels are not runtime evidence. Do not change them to
`audited-ok` merely because scripts compile or the reward scroll appears.

## 13. Modernization sequence

1. Freeze the pinned state/item/interface/cannon contract and capture current
   client evidence for remains pickup, repair UI, completion scroll, smelting
   unlock, cannon targeting, and recovery.
2. Add a versioned reward/migration ledger; reconcile legacy states 0–11,
   state-10 ambiguity, side bits, QP/count, XP floor, and orphan cannon owners.
3. Modernize Lawgof's start, hammer/railing grant, six repair transactions,
   recovery, failure table, remains hand-in, and every full-inventory path.
4. Verify/fix the tower remains click target and replace global Lollk with a
   player-owned, interruption-safe rescue scene.
5. Wire interface 409, correct tool selection/pairing, state 7→8, toolkit
   recovery/deletion, and fixed quest-cannon flavor ops.
6. Restore Nulodion 9→10, individual item recovery, atomic 10→11 hand-in, and
   exactly-once rewards; update the journal, scroll, post-quest dialogue, and
   debug adapter.
7. Build the shared modern player-cannon subsystem, Nulodion full-set/recovery
   services, owner persistence, combat integration, restrictions, and decay.
8. Correct single/double-mould production, current timing and auxiliary ammo
   paths, then wire the Port Phasmatys diary event.
9. Restore Between a Rock...'s prerequisite, verify Land of the Goblins cave
   coexistence, and audit every cannon-aware combat/activity owner.
10. Run Gate D, attach real-client and concurrent-player evidence, reconcile
    stale audit records, and change status only when both quest and permanent
    reward pass.

The short quest can be repaired with current RuneScript/config/interface
machinery. The persistent cannon may justify a shared engine capability for
owned dynamic loc actors and deadline recovery, but not quest-specific global
state or a thin visual stub.

## 14. Verification matrix

### Static and pack checks

- Run `python3 tools/questhelper_extract.py dwarfcannon --check` against the
  pinned helper commit.
- Resolve dbrow 47; primary/packed/cannon carriers; all six rail transforms;
  Lawgof, Nulodion, Lollk, guards; remains, hammer, railing, toolkit, notes,
  both moulds, four cannon parts, steel/granite balls; tower/cave/door/cannon
  locs; interface 409 components; animations, projectile, sound, reward scroll,
  and completion jingle.
- Fail on duplicate triggers, raw entity IDs, whole writes to `mcannonmulti`,
  a Nulodion grant without 9→10, rewards without 10→11, unchecked quest-item
  commits, global Lollk, text-menu cannon repair, or an advertised cannon unlock
  without setup/fire/ownership handlers.
- Verify every public Nulodion Trade/talk arm and cannon op enforces state and
  owner server-side.
- Run `make -C src mock230-scripts` and the revision-239 pack check.

### Quest state, dialogue, and item tests

- Exercise 0→1→2→3→4→5→6→7→8→9→10→11 from the real spawns with Yes/No,
  every refusal, re-talk, optional fixed-cannon op, and post-quest dialogue.
- Start with hammer absent/present, 0/1/2 free slots, banked hammer, six rails
  already owned, and repeated acceptance packets; verify exact non-duplication.
- Test all 720 railing orders, every bit already fixed, no railing, no hammer,
  recovery, success/failure boundaries at Crafting 1/99, four failure effects,
  zero/low HP, full inventory, slot mutation, relog, and duplicate packets.
- Verify both ladders, remains visibility/click type, one-copy behavior, full
  inventory, pickup/drop/destroy/death/recovery, atomic hand-in, and reconnect.
- Rescue Lollk with two concurrent players, skipped/closed dialogue, relog,
  repeated crate clicks, timed actor cleanup, and later Land of the Goblins
  state; no player may observe or advance through the other's child.
- Exercise every repair-interface tool/part pairing, selection switch, close,
  wrong interface/button, missing/recovered/duplicated toolkit, logout, and
  repeated final click. Only the three correct pairings advance; no incremental
  XP is awarded.
- Give/recover notes and mould with 0/1/2 free slots, each item missing, both
  banked, duplicates, slot movement, Drop/Destroy, reconnect, and repeated
  dialogue. Lawgof refuses every incomplete pair and atomically consumes one
  complete pair.

### Completion and migration tests

- Interrupt after every durable finale phase. Resume with state 11, exactly
  750 Crafting XP, 1 QP, one completion-count increment, one jingle, one scroll,
  and no retained handed-in pair or toolkit.
- Repeat Lawgof, completion queues, packets, relogs, and `::complete`; every
  later reward attempt is a no-op.
- Migrate every primary state, all railing/tool-bit combinations, local versus
  canonical state 10, state 11 from cheat, XP below/equal/above 750, incorrect
  QP/count, surplus/banked quest items, and orphan/live cannon-owner records.
  Run migration twice and compare byte-for-byte durable results.
- Verify journal and quest-list color at all 12 values, especially intended
  state 10 and completed state 11.

### Multicannon, Smithing, and downstream tests

- Purchase a full set with insufficient/exact/excess coins and 0–6 free slots;
  buy every separate shop line; repeat/reconnect each transaction without item
  or coin loss/duplication.
- Set up on clear/blocked/edge/instanced/restricted tiles, with missing parts,
  prequest state, two cannons, two players on one tile, and concurrent setup.
  Test automatic assembly interruption and pickup at each visible stage.
- Load steel/granite ammunition below/at/above capacity 30/35/45/60, mixed
  inventory, mismatched loaded type, full inventory pickup, Empty/Fire/reload,
  and another player's operations.
- Verify all eight facings, wedges, line of sight, 1×1 through large NPCs,
  overlap/double shots, single/multi combat, immunities, aggression, kill/drop/
  Slayer/achievement credit, max hits 30/35, 2× Ranged XP, zero Hitpoints XP,
  and current accuracy inputs.
- Break at 25 minutes, repair, disappear at 35, and reconcile pickup/logout,
  world hop, death/Wilderness death, server restart, region unload, boss
  destruction, Nulodion refusal, and valid reclaim with exact remaining ammo.
- Smith with single and double moulds at ordinary and ancient furnaces, all
  level/stage interpretations, Make-X timing/cancel, inventory limits, exact
  4/8 outputs and 25.6/51.2 XP, granite conversion, and duplicate packets.
- Verify the Morytania diary fires only after a valid Port Phasmatys batch;
  test Between a Rock... at states 10/11 and Land of the Goblins cave sharing.

### Real-client evidence

Capture the standardized offer; conditional hammer and six rails; every railing
failure class and recovery; tower remains pickup; private Lollk rescue; toolkit
interface and correct three pairings; fixed-cannon Fire/Pick-up flavor; Nulodion
grant/recovery; two-item hand-in; state-11 scroll/XP/QP/journal; full-set and
separate purchases; cannon assembly/loading/firing/pickup; concurrent ownership;
capacity tiers; combat XP/targeting; break/decay/world-hop/death/reclaim;
cannonball batches and diary event; downstream prerequisite; relog recovery;
and idempotent debug completion.

`verified-modern` requires the visible 0–11 quest, exactly-once finale, usable
permanent cannon, current cannonball production, and downstream ownership to
pass. A displayed reward scroll, purchasable parts, or a compile-clean old
LostCity port is not sufficient.
