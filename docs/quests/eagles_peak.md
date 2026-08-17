# Eagles' Peak modernization audit

Status: `audit-pending` — the cache-native quest row, permanent state,
production NPCs and scenery, three puzzle-room scripts, completion call, Hunter
gates, Fancy Clothes Store, and three remote eyrie spawns exist. The current
route is not modern or verified: its golden room is an any-order visual
substitute for the lever-and-collision puzzle, the feather door treats its
native bitfield as a counter and accepts duplicate feather types, the guard
does not authorize against the door state, and the journal claims the quest is
not implemented. The finale does not consume Nickolaus's disguise or grant the
promised box trap, and the post-quest eagle system ignores ropes, route
unlocking, obstacles, travel presentation, and diary credit.

Audited: 2026-08-17

Governing plan: [Quest modernization plan](../QUEST_MODERNIZATION_PLAN.md). This
record applies that plan's Gates A–D to the quest root, cache-authored puzzle
state, world spawns, Asyff's shared shop subject, the optional kebbit encounter,
the ferret/box-trap/rabbit lifecycle, all three giant-eagle routes, diary hooks,
the dynamic journal, completion, and the At First Light prerequisite. It is an
implementation specification, not completion evidence.

## 1. Authoritative references

These OSRS Wiki revisions are pinned so implementation and review use a stable
route, dialogue, puzzle, item, reward, and transport contract.

| Reference | Pinned revision | Use in this audit |
| --- | --- | --- |
| [Eagles' Peak](https://oldschool.runescape.wiki/w/Eagles%27_Peak?oldid=15241038) | 15241038, 2026-06-27 | Identity, boostable requirement, route, all three puzzles, rewards, unlocks, downstream requirements, and 2018 box-trap change |
| [Eagles' Peak/Quick guide](https://oldschool.runescape.wiki/w/Eagles%27_Peak/Quick_guide?oldid=14641249) | 14641249, 2024-04-22 | Ordered interactions, exact golden-room sequence, disguise use, camp cutscene, and transport setup |
| [Transcript:Eagles' Peak](https://oldschool.runescape.wiki/w/Transcript%3AEagles%27_Peak?oldid=15263366) | 15263366, 2026-07-14 | Acceptance/refusal, re-talks, Asyff branches, loss/replacement, puzzle feedback, Nickolaus hand-in, and finale dialogue |
| [Kebbit (Eagles' Peak)](https://oldschool.runescape.wiki/w/Kebbit_%28Eagles%27_Peak%29?oldid=15199339) | 15199339, 2026-04-28 | One-time level-13 encounter, threaten-or-kill alternatives, and guaranteed silver-feather result |
| [Eagle transport system](https://oldschool.runescape.wiki/w/Eagle_transport_system?oldid=15212928) | 15212928, 2026-05-19 | Rope requirement, four eyries, route setup, destination gates, and travel semantics |
| [Eagle (giant)](https://oldschool.runescape.wiki/w/Eagle_%28giant%29?oldid=15212951) | 15212951, 2026-05-19 | Guard/eagle role and the three destination mappings |
| [Growing vine](https://oldschool.runescape.wiki/w/Growing_vine?oldid=15201730) | 15201730, 2026-04-29 | Supported cane/spar items, staged growth, notification, and jungle-eyrie access |
| [Box trap](https://oldschool.runescape.wiki/w/Box_trap?oldid=15259884) | 15259884, 2026-07-10 | Partial-quest placement gate, Hunter checks, trap ownership, catch polling, and supported prey |
| [Ferret](https://oldschool.runescape.wiki/w/Ferret?oldid=15206912) | 15206912, 2026-05-06 | Box-trap acquisition, quest use, rabbit flushing, and level-scaled preservation chance |
| [White rabbit](https://oldschool.runescape.wiki/w/White_rabbit?oldid=15152536) | 15152536, 2026-03-20 | Completion gate, hole-to-hole route, snare capture, 144 XP, and rabbit-foot reward |
| [Golden feather](https://oldschool.runescape.wiki/w/Golden_feather_%28Eagles%27_Peak%29?oldid=15185630) | 15185630, 2026-04-22 | Door use, loss recovery, and legitimate duplicate acquisition by banking |
| [Feathered journal](https://oldschool.runescape.wiki/w/Feathered_journal?oldid=15282353) | 15282353, 2026-07-30 | Optional readable dungeon lore and Arthur Artimus provenance |
| [At First Light](https://oldschool.runescape.wiki/w/At_First_Light?oldid=15271675) | 15271675, 2026-07-22 | Downstream prerequisite relationship |

The sources identify Eagles' Peak as quest number 115, released 28 November
2006. It is a short, novice, members' quest with one boostable requirement:
27 Hunter, required to start. It awards two quest points, 2,500 Hunter XP, a
box trap, access to box traps and the eagle transport system, and the ability
to use captured ferrets to flush white rabbits after completion.

Transition aid only: the local Quest Helper checkout's
[`EaglesPeak.java`](https://github.com/Zoinkwiz/quest-helper/blob/5ea99d5ea9ba3fb096ebe7b5ed02d80883e9819d/src/main/java/com/questhelper/helpers/quests/eaglespeak/EaglesPeak.java)
at commit `5ea99d5ea9ba3fb096ebe7b5ed02d80883e9819d` confirms primary
states 0, 5, 10, 15, 20, 25, 30, and 35; the two door feather bits plus the
silver tracking value; all five mechanical-bird bits; room coordinates; and
the exact ordered puzzle actions. It guides state tests but does not override
the Wiki, transcript, or revision-239 cache.

`python3 tools/questhelper_extract.py eaglespeak --check` resolves
`quest_eaglespeak` and every referenced item, NPC, loc, and varbit. That proves
symbol availability, not gameplay correctness.

## 2. Native quest identity and player contract

The cache-native `quest_eaglespeak` dbrow and pinned sources define this
contract:

| Field | Native value / expected behavior |
| --- | --- |
| Cache quest ID | 122; OSRS release-order number 115 |
| Type | Members' quest |
| Difficulty / length | Novice / short |
| Release date | 28 November 2006 |
| Start / finish | Charlie at Ardougne Zoo |
| Prerequisites | None |
| Required level | 27 Hunter, boostable and required to start |
| Required items | Yellow dye, swamp tar, and 50 coins |
| Generated quest items | Bird book, metal feather, ten eagle feathers, two fake beaks, two eagle capes, bronze/silver/golden feathers, odd bird seed, ferret, and box trap |
| Combat | Level-13 kebbit; optional because Threaten is the no-combat alternative |
| Primary state | `%eaglepeak_quest`, cache varbit on `eaglepeak`, bits 0–5 |
| Secondary state | Puzzle/route fields on native `eaglepeak` and `eaglepeak2` varps |
| End state | 40 |
| Quest points | 2 |
| XP reward | 2,500 Hunter XP (`25000` raw tenths) |
| Immediate rewards | One box trap; box-trap access; rabbit-flushing access; eagle transport access |
| Required for | At First Light; medium Desert, Fremennik, and Western Provinces tasks; partial medium Kourend & Kebos progression |

The dbrow correctly marks the requirement boostable and supplies the start NPC,
coordinate, end state, quest points, XP, recommendation, and members flag. The
quest should consume those facts through shared start/journal/completion
machinery; copying them into dialogue branches is not enforcement.

Both cache carriers already exist as permanent transmitted varps. No parallel
quest-progress variable and no quest-specific C state are needed.

## 3. Implementation surface

Paths below are relative to `OSRS-Content/osrs239-content/`.

### Quest-owned files

| Path | Present responsibility | Audit result |
| --- | --- | --- |
| `server/scripts/quests/quest_eaglepeak/configs/eaglepeak.constant` | Primary values, dialogue values, item counts, puzzle values, and coordinates | Uses native values, but omits state 30 and assigns remote destinations without their entry/unlock transaction |
| `server/scripts/quests/quest_eaglepeak/configs/eaglepeak.varp` | Declares `eaglepeak` and `eaglepeak2` permanent/transmitted | Correct native ownership; retain |
| `scripts/eaglepeak.rs2` | Charlie, start, books, metal feather, cavern, Nickolaus shout, feathers, completion, debug setup | Real triggers are present, but boost policy, dialogue, item recovery, completion transaction, and reward delivery are wrong or incomplete |
| `scripts/asyff.rs2` | Shared Asyff Talk/Trade and disguise recipe | Real shop opens; quest recipe demands four empty slots unnecessarily, replacement pricing/quantity is wrong, and transactions are not failure-safe |
| `scripts/bronze_room.rs2` | Net, four winches, bronze feather, tunnels | Broad global loc scanning and 500-tick transformations combine per-player bits with shared scenery; pre-trigger winch policy and visual lifecycle are incomplete |
| `scripts/silver_room.rs2` | Pedestal trail, rocks, kebbit, silver feather, tunnels | Trail values exist, but spawned kebbit is global, kill completion is absent, and the silver result is moved to a pedestal instead of the tunnel/drop contract |
| `scripts/gold_room.rs2` | Seed holder, feeders, levers, reset, golden feather, tunnels | Explicitly implements an any-order soft path; gates do not constrain travel, bird movement is a temporary visual, and completion uses the wrong state predicate |
| `scripts/feather_door.rs2` | Feather insertion and stone-door crossing | Treats a bitfield as a counter, accepts duplicate types, and opens without independently proving gold, silver, and bronze |
| `scripts/sneak.rs2` | Guard disguise check, nest Nickolaus, campsite delegation | Guard does not require door completion or damage an undisguised player; Nickolaus does not receive his second disguise |
| `scripts/camp.rs2` | Ferret lesson and recovery | Temporary world scenery substitutes for the cutscene, no box trap is delivered, and Nickolaus gives infinite free replacement ferrets |
| `scripts/eyries.rs2` | Post-quest desert/jungle/polar travel | Direct teleport on Quick-travel; no rope use, lasso state, route unlock, obstacle, diary, animation, or cutscene |

The root totals 1,337 lines across eleven files. This is a substantial scaffold,
not an absent quest, but line count and successful packing do not make its
soft paths correct.

### Mandatory shared and cross-directory surfaces

| Path / subsystem | Relationship | Audit result / required ownership |
| --- | --- | --- |
| `server/scripts/interface_questjournal/scripts/quest_journal.rs2` | Dynamic quest-list dispatcher | No Eagles' Peak arm exists; dbrow 122 falls to `~quest_journal_unwritten` and tells the player the world does not run the quest |
| `server/scripts/quests/scripts/quest_cheat.rs2` | `::complete` adapter | Idempotently writes state 40 only; appropriate for registry setup, not evidence of XP, box-trap, route, or diary delivery |
| `server/scripts/quests/scripts/questpoints.rs2` | Shared QP, completed-count, jingle, and modern reward scroll | Correctly paints the dbrow reward; it does not grant the icon item and cannot make the preceding non-atomic quest mutation safe |
| `server/scripts/shop/varrock/scripts/fancy_clothes_store.rs2` | Asyff's ordinary Trade service | `~openshop` is live; old queue claims that the shop remains blocked are stale |
| `server/scripts/areas/world/configs/m40_51.spawn` | Charlie | Correct production spawn at 2607,3264,0 |
| `server/scripts/areas/world/configs/m36_54.spawn` | Campsite Nickolaus | Correct persistent `eaglepeak_nickolaus_campsite`; local trigger is now wired |
| `server/scripts/areas/world/configs/m31_77.spawn` | Dungeon Nickolaus, guard, three hub eagles, optional journal object | Real NPC names are now wired, but `ep_transport_book01` has no Read handler |
| `m53_149`, `m39_145`, and `m42_159` spawns | Desert boulder/eagles, jungle eagles, and polar eagles | Remote actors exist; desert boulder has no handler and vine/entry progression is not quest-owned |
| `server/scripts/skill_hunter/scripts/box_trap.rs2` | Ferret and chinchompa trapping | Strong owner-slot machinery exists, but it gates all placement at state 40 instead of the canonical partial checkpoint; its debug proc directly completes the quest |
| `server/scripts/skill_hunter/scripts/rabbit_hole.rs2` | Ferret flushing and white-rabbit spawn | Correctly requires completion in broad policy, but rabbit movement is a nearest-snare soft spawn rather than the authored random-hole route; its debug proc also directly completes the quest |
| `server/scripts/skill_hunter/configs/box_trap.dbrow` | Ferret catch row | Correct named ferret row: Hunter 27, 115 XP, ferret item, and shaking-box loc |
| `server/scripts/ladders_stairs/` | Remote cave/vine traversal | Generic categories/maplinks exist, but quest name handlers and missing vine/boulder policy leave access incoherent |
| `server/scripts/quests/quest_atfirstlight/` and its dbrow | Downstream quest | Native prerequisite row references Eagles' Peak; that quest's own audit records that its start currently does not enforce either prerequisite |
| diary subsystem | Three post-quest travel tasks | Repository searches found no eagle-route diary trigger; direct teleport therefore cannot award the required tasks |

### Cache-native machinery already available

The revision-239 cache carries more structure than the scripts use:

- `%eaglepeak_quest` is a six-bit primary field with end state 40;
- `%eaglepeak_unblocked_jungle` and `%eaglepeak_unblocked_desert` persist remote
  route setup;
- four golden-room gate bits and five mechanical-bird bits drive authored
  transforms;
- `%eaglepeak_puzzle2_tracking` has values 0–6 and supplies silver-door state;
- four bronze winch bits, aggregate winch range, and net-trap bit hold the
  bronze puzzle;
- `%eaglepeak_eagledoor_feather1` and `_feather3` are the two bits of the
  `eagledoor_status` field, while silver is independently represented by
  tracking value 6;
- `%eaglepeak_jungle_vine` has four visible growth stages;
- `%eaglepeak_nickolaus_chat` has the helper-confirmed 3/4/5 sequence;
- all three room-entry bits exist; and
- the door, levers, trail segments, vines, cave entrances, transport eagles,
  boulder animations, quest items, sequences, and production spawns are named.

Modernization should make these fields authoritative and remove global visual
substitutes. It should not invent another counter, teleport-only route, or
parallel progress carrier.

## 4. Native state model and current reachability

| Primary state | Required phase | Current implementation / defect |
| ---: | --- | --- |
| 0 | Charlie offers the quest; 27 Hunter may be boosted; player can refuse | Real trigger and two refusals exist, but `stat_base(hunter)` rejects a valid temporary boost; dialogue is a short paraphrase |
| 5 | Search camp books, receive/read Bird book | Works inventory-locally; book is deleted on Read, campfire/equipment inspection and authored text are absent |
| 10 | Recover/use metal feather and open rocky outcrop | Works broadly; books issue a metal feather for every later state, including after the key is permanently consumed and after completion |
| 15 | Enter, meet Nickolaus, make two disguises, solve three rooms, insert three distinct feathers | All pieces have scripts, but the golden puzzle is a soft substitute, silver kill route is absent, door identity is not enforced, and room/door authorization is not tied into guard progression |
| 20 | Pass the guard in one worn disguise; give Nickolaus the second set | Guard writes 20 and teleports; no door-state check or undisguised damage. Nickolaus never checks or removes the spare beak/cape |
| 25 | Meet Nickolaus at camp and choose the ferret lesson | Choice exists and can defer, but state 30 is skipped |
| 30 | Ferret lesson/cutscene in progress or ready to resolve | Native/helper checkpoint is unused; the temporary box sequence cannot recover safely across interruption |
| 35 | Own the caught ferret and box trap; return ferret to Charlie; lost ferret must be recaught | Ferret is granted but box trap is not. Nickolaus gives free replacements because the shared trap gate incorrectly remains locked until 40 |
| 40 | Complete; XP/QP/reward scroll; rabbit and transport unlocks | State and XP are written before shared reward presentation; no box trap is granted; journal still says unimplemented; eagle routes remain noncanonical |

The legitimate broad route can reach the completion call, especially through
debug setup or noncanonical puzzle interactions, but it is not a proven
end-to-end implementation of the Wiki route. Physical collision may incidentally
keep a normal player from clicking the guard before the stone door, but the
guard handler itself has no such invariant. Modern quest authorization must be
state-based and testable, not dependent on click-range accidents.

## 5. Puzzle and rescue contract

### Bronze room

Required behavior:

- first touching the pedestal raises it in the net;
- winches cannot make progress before the trap is sprung;
- all four distinct winches can be operated in any order, once each;
- completing all four lowers/breaks the trap for that player;
- taking and losing the bronze feather has a recoverable, non-duplicating
  policy; and
- relogging or sharing the room with another player does not combine one
  player's bits with another player's temporary world loc.

Current code correctly records four separate winches and does not grant the
feather early. It nevertheless operates winches whenever the quest is at 15,
even before checking that the trap has sprung; performs a 9-by-9 world loc scan
from whichever winch was clicked; and changes a shared loc for 500 ticks while
the completion state is per player. Two players can therefore observe scenery
that disagrees with their own bits. Use native transform ownership or a proper
player instance, and test pre-trigger, partial, complete, relog, and concurrent
players.

### Silver room

Required behavior:

- Hunter 27 is checked at pedestal inspection, including a truthful below-level
  branch;
- pedestal, east rocks, north-east rocks, and opening advance values 1–4;
- wrong rocks remain inert and correct rocks have reinspection text;
- the one-time level-13 kebbit can be threatened with the authored choice or
  killed through ordinary combat;
- either result makes the silver feather available at the encounter location;
- the player can decline combat XP via Threaten; and
- losing the feather allows direct tunnel recovery without repeating the
  encounter.

Current code never checks Hunter at the pedestal, even though a drained player
or malformed state can reach it. It spawns one global timed kebbit found by
radius; another player can suppress, threaten, or delete that actor. Only
`opnpc3` is owned. Killing the attackable kebbit does not set value 5 or provide
its guaranteed feather, contradicting both current Wiki pages; reopening the
tunnel merely spawns another kebbit. The code then changes a distant pedestal
and takes the feather there rather than supporting the documented drop/tunnel
reclaim. This also conflicts with the existing `quest_bosses.md` row, which
incorrectly says a kill must not advance. Correct that shared manifest when
implementing the encounter.

### Golden room

Required behavior:

- six odd bird seeds are available;
- four named levers operate the authored pairs of wing gates;
- five metal birds move only to a reachable feeder, changing real collision;
- the canonical sequence must work exactly, while wrong moves remain
  recoverable;
- putting seed in an occupied feeder has no effect and does not consume seed;
- reset asks for confirmation, restores all gates/birds/feeders, and reports a
  no-op when already reset; and
- the golden pedestal is reachable because of world state, not because four
  arbitrary boolean flags are set.

The file explicitly says `Soft: any-order fill` and `Full lever-gated bird
collision pathing deferred`. Lever bits animate cache scenery but are not
preconditions for feeder use. Feeders set bits directly and add a temporary
metal bird beside the clicked feeder. The completion predicate requires
mechanical birds 1–4 and ignores bit 5, rather than proving the fifth bird and
the corridor state. Its mapping also does not reproduce Quest Helper's
ordered condition chain: the normal route through feeder 2/2a can hit an
already-fed rejection without setting the final required bird, while an
unrelated feeder variant can satisfy the soft predicate. Reset has no Yes/No
confirmation and does not reconcile temporary birds. This room needs a full
state/collision reconstruction against the cache, not another message-level
patch.

### Stone door, guard, and Nickolaus

`eagledoor_status` is not a generic count. Its low/high bits are the native
gold and bronze insertion fields; silver insertion is value 6 of the tracking
field. Current code increments the two-bit field for every metal feather. As a
result, inserting silver mutates the gold bit, a later insertion can toggle
that bit while incrementing, and three copies of one feather type unlock the
door. Each type must instead be rejected if its own field is already set, then
atomically consumed and written to its native field. Opening requires both
gold/bronze bits and silver value 6.

The guard must require the complete door invariant and the worn beak/cape. A
failed disguise attempt should reproduce the eagle's small hit/knockback rather
than only print advice. At the nest, Nickolaus must verify and atomically remove
one spare fake beak and eagle cape. Without those items he gives the replacement
guidance; only a successful hand-in advances to the camp.

## 6. Narrative and item lifecycle oversight matrix

| Priority | Oversight | Consequence | Required correction |
| --- | --- | --- | --- |
| Critical | Golden room is an explicit any-order soft simulation | Defining puzzle geometry, lever dependencies, collision, failure, and reset are absent; canonical sequence is not a reliable path | Reconstruct the authored five-bird/four-gate state machine from native transforms and test every canonical step |
| Critical | Door counter accepts duplicate types | One room's feather can unlock all three recesses; native gold/bronze/silver ownership is corrupted | Write/check each native field independently and require their conjunction |
| Critical | Nickolaus receives no disguise | Quest advances without the second beak/cape and leaves duplicate quest outfits | Require and consume one spare set at the nest |
| Critical | Box trap is never granted | Reward text/icon claim an item the player never receives; released quest ferret cannot be canonically replaced | Deliver the trap during the lesson/reward lifecycle and add an exact inventory test |
| Critical | Box placement is locked until completion | Current OSRS permits partial completion; state-35 player cannot catch a replacement ferret | Gate box placement at the canonical post-lesson checkpoint, while rabbit flushing remains completion-gated |
| Critical | Silver kill alternative has no completion/drop hook | A player choosing Attack gets no silver feather and must respawn/threaten another kebbit | Own one-time combat death and guaranteed owner-local silver result; retain Threaten no-XP route |
| High | Guard trusts chat/outfit but not feather-door state | Handler can authorize rescue when invoked without the three-room contract | Require exact door state before crossing; test malformed/debug state |
| High | Start uses base Hunter instead of boosted Hunter | Valid boosted level 27 players are refused despite dbrow/Wiki | Use current effective Hunter for the start gate and retain base-level journal wording |
| High | Completion is non-transactional | Ferret is removed and state 40 written before XP/QP/scroll; interruption can leave a completed but under-rewarded player | Build an idempotent guarded completion transaction with reward ledger/order appropriate to engine guarantees |
| High | Quest journal is absent | Every state, including 40, displays “This world does not run this quest yet.” | Add dbrow-122 dispatch and state/substate-aware journal |
| High | Per-player puzzle bits drive global temporary scenery/NPCs | Concurrent players can see, suppress, delete, or inherit another player's encounter visuals | Use native varbit transforms, player-owned actors, or proper instances |
| High | Eyries ignore all setup requirements | Completion gives unrestricted direct teleports without rope, boulder, vine, Agility, or route unlocks | Implement lasso use, remote-entry setup, native unlock bits, and exact return semantics |
| High | Eagle rides do not award diary tasks | Three required medium tasks remain impossible through this route | Call the shared diary owners only after successful outbound travel to the matching destination |
| Medium | Asyff requires four empty slots before consuming four input stacks | A valid inventory with the ingredients but fewer than four unrelated empty slots is rejected | Compute net capacity; the four consumed input slots make room for the four unstackable outputs |
| Medium | Asyff replacement contract repeats the initial bundle | Transcript charges 25 coins and materials for another single costume; code requires initial quantities/cost and creates two sets | Separate first two-costume recipe from one-costume replacement recipe |
| Medium | Metal feather recovery stays open after permanent use/completion | Books mint unnecessary later copies instead of only recovering a lost active key | Bound recovery to the pre-open phase and ownership policy |
| Medium | Camp replacement mints free ferrets | Bypasses the Hunter lesson, box trap, and partial unlock | Direct the player to catch another wild ferret with the supplied trap |
| Medium | Feathered journal has no Read handler | A real spawned lore item with native Read op does nothing | Implement its authored readable text without coupling it to progress |
| Medium | Dialogue is a synopsis | Many Charlie/Nickolaus/Asyff topics, refusal/re-talk branches, puzzle messages, and post-quest topics are missing | Rebuild from the pinned transcript, keeping side topics non-mutating |
| Medium | State 30 is skipped | Cutscene interruption/relog cannot distinguish accepted lesson from delivered items | Restore the native/helper checkpoint and define resumable cutscene delivery |
| Medium | Debug procs mutate permanent prerequisites | Hunter debug commands set quest 40 without QP/rewards; quest debug commands create impossible mixed substates | Make debug setup explicit isolated fixtures or assertions; never treat it as production verification |
| Low | Old queue documents claim the Asyff shop is blocked and puzzles fully match | Reviewers can accept stale conclusions despite live contradictory comments/code | Update queue status only after implementation/tests; retain audit as source of truth meanwhile |

### Item lifecycle contract

| Item | Canonical acquisition/use | Modern replacement and capacity behavior |
| --- | --- | --- |
| Bird book | Inspect campsite books at state 5; Read releases metal feather | Do not duplicate while book/feather is owned; full inventory retries safely; reading should use authored text and transition once |
| Metal feather | Falls from book; consumed by rocky outcrop | Books replace only while entrance still needs the key and no owned copy exists; after opening it is no longer generated |
| Eagle feathers | Take ten after agreeing to help; consumed by Asyff | Stackable, cap/re-talk should respect inventory/bank ownership as policy dictates; initial tailoring consumes exactly ten |
| First disguise bundle | Ten feathers, dye, tar, and 50 coins produce two beaks and two capes | Net-capacity transaction; one set is worn by player and one is consumed by Nickolaus |
| Replacement disguise | Asyff makes another costume for materials plus 25 coins | Separate one-beak/one-cape recipe; no free or two-set duplication |
| Odd bird seed | Take six and consume through feeders | Stackable; occupied feeder does not consume; reset and loss remain recoverable without arbitrary cap deadlock |
| Bronze feather | Complete four-winches puzzle | Reject duplicate in inventory, allow documented loss/reclaim, and seat only in bronze door field |
| Silver feather | Threaten or kill one kebbit | Owner-local result; tunnel reclaim after loss; seat via tracking value 6 |
| Golden feather | Complete full collision puzzle | Loss/reclaim and documented banked duplicates remain possible; door still rejects a second golden insertion |
| Box trap | Nickolaus's lesson leaves one for the player/reward | Present before a state-35 lost-ferret recovery; do not confuse reward-scroll icon with item grant |
| Ferret | Caught during lesson, then given to Charlie | Releasing/losing it requires catching another wild ferret with the box trap; Nickolaus supplies guidance, not unlimited items |
| Feathered journal | Ground spawn in dungeon | Readable optional lore; no state or inventory reward side effect |

## 7. Completion, Hunter, transport, and downstream contracts

### Completion transaction

Charlie must require state 35 and exactly one ferret. A successful transaction
must remove the ferret, ensure the player retains/receives one box trap, award
2,500 Hunter XP and two quest points once, increment completed count once,
write state 40 once, play the novice jingle, and paint the modern dbrow reward
scroll. Repeated Talk-to and `::complete` must not repeat XP, QP, trap, or
completed-count changes.

The current order is `inv_del` -> state 40 -> XP -> shared QP/scroll. The shared
helper is modern presentation machinery, but it has no rollback or idempotence
guard. Modernization should use the repository's strongest proven quest reward
transaction pattern and add failure injection at every yield/capacity boundary.

### Box traps and rabbits

The shared box-trap system already has owner slots, trap limits, polling,
expiry, success/failure, bait/smoke bits, and a ferret dbrow. Keep that owner
rather than scripting a second quest-only trap. Change only the quest-access
policy to the canonical partial checkpoint and verify that later chinchompa
uses inherit it.

Rabbit flushing remains completion-gated. Its existing 81–100% ferret
preservation approximation matches the published endpoints, but rabbit spawn
selection is explicitly soft: it chooses the nearest owned laid snare rather
than emerging from a random hole and travelling toward another hole. That is a
Hunter-system modernization dependency, not a reason to fake quest completion.
The eventual test must verify 144 XP, raw rabbit, rabbit foot, trap ownership,
ferret loss/retention, and unrelated holes/players.

### Eagle transport system

The hub contains desert, jungle, and polar eagles only after completion through
cache NPC transforms. Correct transport is a point-to-point system, not a menu
or unrestricted Quick-travel teleport:

| Route | Hub destination | Remote setup required before route is usable |
| --- | --- | --- |
| Polar | Rellekka Hunter area | Reach remote eyrie using its 35 Agility cliff route; ropes spawn inside |
| Desert | Uzer Hunter area | Push the NPC boulder from outside with 45 Strength (boostable), then persist `%eaglepeak_unblocked_desert`; rope spawn inside |
| Jungle | Feldip Hunter area | Use teasing stick, thatching spar, or garden cane on young vine; progress `%eaglepeak_jungle_vine` over the authored wait and persist jungle access |

Every ride requires using a rope to lasso the matching eagle before
Quick-travel becomes meaningful. Implement the correct consumed/retained rope
lifecycle after confirming it against live behavior or an additional primary
source; do not guess from the word “requires.” A ride needs protected movement,
animation/cutscene, interruption cleanup, correct landing, return route, and
the matching diary callback. The remote cave exits must not override generic
maplinks in a way that bypasses setup or always returns to one hard-coded hub
tile.

### Downstream consumers

- At First Light's native dbrow correctly lists Eagles' Peak, but its current
  start script does not enforce prerequisites. Modernize that owner under its
  own audit and add a cross-quest assertion.
- Medium Desert, Fremennik, and Western Provinces diary credit belongs to the
  successful corresponding eagle ride, not quest completion or debug state.
- Medium Kourend & Kebos has Eagles' Peak in its aggregate prerequisite set;
  verify the shared diary calculation rather than adding a quest-local write.
- Hunter equipment/spell sources can create box traps, but possession must not
  bypass the partial-quest placement gate.

## 8. Modernization implementation plan

### Wave 1 — authoritative state, journal, and start

1. Add `~eaglespeak_journal` and dbrow-122 dispatch using primary state plus
   Nickolaus, room, feather, door, and item ownership substates.
2. Centralize named predicates for started, entrance open, all three distinct
   door feathers, rescue authorized, lesson complete, and quest complete.
3. Fix Charlie's start to accept effective boosted Hunter 27, preserve explicit
   refusal, and render requirement/help/re-talk dialogue from the pinned
   contract.
4. Bound Bird book/metal feather recovery to the active phase and implement the
   optional Feathered journal.

### Wave 2 — disguise and rescue authorization

1. Preserve Asyff's live Trade service while rebuilding quest topics from the
   transcript.
2. Implement a net-capacity, atomic initial two-costume transaction and the
   separate 25-coin one-costume replacement transaction.
3. Make Nickolaus's shout/re-talk distinguish no request, materials in progress,
   costumes made, and “bring it over” states.
4. Gate guard crossing on full door state plus worn player set; add failed
   eagle damage/knockback.
5. Require and consume Nickolaus's spare set before state 25.

### Wave 3 — rebuild all three puzzle rooms

1. Bronze: require sprung trap before winches, drive exact player-visible
   net/pedestal transforms, and remove broad global scans.
2. Silver: bind an owner-local one-time kebbit, support Threaten and ordinary
   kill results, place/recover the feather canonically, and preserve no-combat
   completion.
3. Gold: derive a transition table for all four levers, five birds, feeder
   locations, wing gates, and reset from cache plus Quest Helper; implement
   collision-authoritative movement and the exact Wiki sequence.
4. Door: replace increment logic with independent gold/bronze bits and silver
   value 6; reject already-inserted types and make door crossing bidirectional
   and collision-correct.
5. Add relog and two-player tests to each room before integrating rescue.

### Wave 4 — camp, reward, and Hunter unlocks

1. Restore state 30 as the resumable lesson/cutscene checkpoint.
2. Build the Nickolaus demonstration with protected camera/actor/scenery
   ownership and deterministic cleanup on skip, logout, death, or region leave.
3. Deliver both ferret and box trap exactly once; ensure a full inventory waits
   safely without advancing.
4. Change shared box-trap placement to the exact partial checkpoint and remove
   free replacement ferrets.
5. Make Charlie completion idempotent and failure-safe, then verify exact XP,
   QP, item, completed-count, jingle, scroll, and state.

### Wave 5 — transport and downstream integration

1. Implement desert boulder Strength/check/animation and native unlock bit.
2. Implement jungle support-item use, staged vine timer/relogin reconstruction,
   notification, climb, and native unlock bit.
3. Verify the polar cliff/maplinks and all remote/hub spawns.
4. Implement rope lasso and protected eagle rides in both directions.
5. Award each diary task only on its exact successful travel event.
6. Verify At First Light and aggregate diary prerequisites against completed
   state without quest-local duplication.

### Wave 6 — narrative and shared-system cleanup

1. Restore reachable transcript branches and authored puzzle feedback without
   allowing dialogue to mutate unrelated substates.
2. Remove stale `Soft`, `deferred`, and “shop blocked” claims only when their
   systems are genuinely complete.
3. Replace permanent-state debug mutations in Hunter demos with isolated test
   setup or explicit developer-only fixtures that cannot be mistaken for
   end-to-end proof.
4. Correct the shared quest-combat manifest's kebbit-kill assertion.

## 9. Verification matrix

### Static and build gates

- `python3 tools/questhelper_extract.py eaglespeak --check` remains clean.
- Quest manifest contains one completion call, one journal arm, one cheat arm,
  and the correct root/external files.
- No unresolved symbols, raw cache IDs, legacy IF1 modal opens, or undisclosed
  `soft`/`deferred` markers remain.
- `make -C src mock230-scripts` succeeds.
- `mock230_pack --check-only` succeeds against the intended revision-239
  cache.

### Start, state, and item cases

- Hunter 26 refuses; boosted effective 27 accepts; unboosted/base 27 accepts;
  refusal leaves state 0.
- Every primary state 0/5/10/15/20/25/30/35/40 renders a truthful journal and
  re-talk.
- Book, metal feather, eagle feathers, each disguise set, each metal feather,
  seed, trap, and ferret have full-inventory, banked, dropped/destroyed, lost,
  and repeated-click coverage where applicable.
- Initial Asyff crafting succeeds with no unrelated empty slots once its four
  consumed input slots can hold the four outputs.
- Nickolaus cannot advance without the spare beak and cape and consumes exactly
  one of each.

### Puzzle and concurrency cases

- Bronze winches before trap do not progress; each permutation of four distinct
  post-trigger winches succeeds; duplicate winch does not; relog reconstructs.
- Silver wrong/correct rocks, Hunter drain, Threaten decline/Taunt, kill, lost
  drop, tunnel reclaim, death, relog, and two simultaneous players are covered.
- Golden canonical eleven-step sequence succeeds; occupied feeders preserve
  seed; invalid ordering remains recoverable; reset Yes/No/no-op/full reset is
  exact; collision prevents walking through closed wings.
- Gold, silver, and bronze insert in all six orders; every duplicate-type
  attempt is rejected; no one- or two-type combination opens the door.
- Guard cannot be invoked through malformed chat/outfit state, undisguised
  crossing damages safely, and complete door plus outfit crosses once.

### Finale and unlock cases

- Lesson refusal, acceptance, skip, full inventory, logout at every cutscene
  yield, death, and re-talk resume without item duplication or loss.
- State 35 can place a box trap and catch a replacement ferret; state 34 and
  lower cannot; rabbit holes remain blocked until 40.
- Charlie with no ferret does not progress; successful hand-in gives exactly
  2,500 Hunter XP, two QP, one completed count, one box trap total, one jingle,
  and state 40.
- Repeating completion dialogue and `::complete quest_eaglespeak` is a no-op
  for XP/QP/items/count.
- Desert 44 Strength fails and boosted 45 succeeds; jungle tool alternatives
  and growth/relog work; polar route enforces 35 Agility externally.
- Every eagle requires its route setup and rope, lands at the exact partner,
  returns correctly, cleans up after interruption, and awards only its matching
  medium diary task once.
- At First Light remains blocked before state 40 and eligible after state 40
  when its other requirements are satisfied.

### Live-client evidence

Capture the modern quest overview and journal at every primary checkpoint; the
rocky-outcrop transform; Nickolaus chasm scene; first and replacement Asyff
transactions; each puzzle initial/partial/complete/reset state; every door
insertion order; guard failure/success; nest hand-in; resumable camp cutscene;
completion scroll; partial box-trap use; rabbit flush; desert boulder; jungle
vine stages; and all six outbound/return eagle rides. Test two clients together
in each puzzle/encounter room.

## 10. Likely change surface

Modernization is expected to touch:

- all quest-owned script files and constants under `quest_eaglepeak`;
- `interface_questjournal/scripts/quest_journal.rs2`;
- shared box-trap access policy and its tests;
- rabbit-hole behavior/tests if the quest is held to full post-quest fidelity;
- desert boulder, jungle vine, remote cave/maplink, eagle travel, and diary
  owners;
- the quest combat contract for the kebbit alternative;
- At First Light prerequisite tests; and
- generated/static quest audit manifests and stale queue notes after proof.

No engine C change is justified by the present audit. Native varbits,
transforms, owner-scoped traps, queues, teleports, modern journal/reward UI, and
named cache assets already exist. Escalate to engine work only if a minimal
reproduction proves that player-local scenery, owned NPC encounter state, or a
protected ride/cutscene cannot be expressed through the established shared
RuneScript machinery.

## 11. Audit verdict

Eagles' Peak should not be labelled `verified-modern`. It is a useful
cache-native scaffold with a connected start and completion call, but its
defining puzzle, door, item, reward, and transport contracts contain critical
shortcuts or omissions. The highest-value sequence is: add the truthful
journal and invariants; rebuild the golden room and distinct-feather door;
enforce/consume both disguises; restore box-trap/ferret delivery and partial
access; then implement the actual rope-and-route eagle network with diary
credit. Only an end-to-end real-client run plus the negative/concurrency matrix
above can advance the status to `verified-modern`.
