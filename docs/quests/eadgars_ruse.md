# Eadgar's Ruse modernization audit

Status: `audit-pending` — the local tree contains the native 0–110 primary
ladder, most route items and scenery, Eadgar/Burntmeat/Sanfew dialogue, a
dynamic journal, parrot and troll-potion recipes, and a completion scroll. It
is not startable through normal play: Sanfew requires `%troll_freed_eadgar`,
but no production script writes that bit and Eadgar's Troll Stronghold cell
door has no handler. An injected state can traverse a simplified route, but
the storeroom patrol puzzle is replaced by an unrestricted crate plus random
damage, completion is not transactional, the potion omits 77.5 Herblore XP,
Trollheim Teleport is usable before completion, and the advertised tablet,
diary, cultivation, and goutweed-exchange rewards are absent or broken.

Audited: 2026-08-17

Governing plan: [Quest modernization plan](../QUEST_MODERNIZATION_PLAN.md). This
record applies Gates A–D to both prerequisite quests, Sanfew, Eadgar,
Burntmeat, Parroty Pete, Tegid, all sixteen primary values, Pete/item side
state, every quest-item recipe and recovery path, Troll Stronghold traversal,
the prison rack, storeroom key/door/guards/crate, completion and migration,
Trollheim teleport surfaces, goutweed stealing/growing/exchange, downstream
quests and diary events, shared NPC dispatch, journal/debug tools, and
real-client verification. It is an implementation specification, not evidence
that the quest is complete.

## 1. Authoritative references

The current article, quick guide, and transcript define the critical path,
boostable requirement, two valid early orderings, dialogue knowledge, recovery,
storeroom patrol, completion, and rewards. Item/NPC, teleport, farming, diary,
and downstream pages define the permanent contract. Revisions were resolved
through the OSRS Wiki API on 2026-08-17.

| Reference | Pinned revision | Use in this audit |
| --- | --- | --- |
| [Eadgar's Ruse](https://oldschool.runescape.wiki/w/Eadgar%27s_Ruse?oldid=15303550) | 15303550, 2026-08-16 | Identity, requirements, route, patrol puzzle, rewards, and downstream requirements |
| [Eadgar's Ruse/Quick guide](https://oldschool.runescape.wiki/w/Eadgar%27s_Ruse/Quick_guide?oldid=15022387) | 15022387, 2025-11-10 | Exact item route, dialogue choices, state order, and storeroom timing |
| [Transcript:Eadgar's Ruse](https://oldschool.runescape.wiki/w/Transcript%3AEadgar%27s_Ruse?oldid=15263291) | 15263291, 2026-07-14 | Offer, re-talks, Pete knowledge, item hand-ins, recovery, full-inventory behavior, and finale |
| [Troll Stronghold](https://oldschool.runescape.wiki/w/Troll_Stronghold?oldid=15231622) | 15231622, 2026-06-11 | Direct prerequisite and optional Eadgar rescue side objective |
| [Troll Stronghold (location)](https://oldschool.runescape.wiki/w/Troll_Stronghold_%28location%29?oldid=14852628) | 14852628, 2025-02-17 | Shared traversal, prison, kitchen, storeroom, patrol behavior, and later herb patch |
| [Goutweed](https://oldschool.runescape.wiki/w/Goutweed?oldid=15183815) | 15183815, 2026-04-22 | Quest/post-quest acquisition, exchange, and later quest consumers |
| [Gout tuber](https://oldschool.runescape.wiki/w/Gout_tuber?oldid=15219023) | 15219023, 2026-05-28 | Level-29 cultivation, completion gate, patch exclusions, and harvest behavior |
| [Trollheim Teleport](https://oldschool.runescape.wiki/w/Trollheim_Teleport?oldid=15134848) | 15134848, 2026-02-25 | Level/runes/XP, completion gate, landing point, tablet, and diary task |
| [Scroll of redirection](https://oldschool.runescape.wiki/w/Scroll_of_redirection?oldid=15241308) | 15241308, 2026-06-27 | Trollheim tablet conversion and quest gate |
| [Fremennik Diary](https://oldschool.runescape.wiki/w/Fremennik_Diary?oldid=15267932) | 15267932, 2026-07-20 | Hard task for successfully casting Trollheim Teleport |
| [Sanfew](https://oldschool.runescape.wiki/w/Sanfew?oldid=15221877) | 15221877, 2026-05-30 | Shared quest dialogue and weighted one-for-one goutweed exchange |
| [Eadgar](https://oldschool.runescape.wiki/w/Eadgar?oldid=15003284) | 15003284, 2025-10-13 | Quest owner, recovery dialogue, and post-quest behavior |
| [Burntmeat](https://oldschool.runescape.wiki/w/Burntmeat?oldid=15017330) | 15017330, 2025-11-06 | Fake-man hand-in, burnt-meat reward, key disclosure, and shared later quests |
| [Parroty Pete](https://oldschool.runescape.wiki/w/Parroty_Pete?oldid=14998473) | 14998473, 2025-10-03 | Two knowledge branches required before making alco-chunks |
| [Drunk parrot](https://oldschool.runescape.wiki/w/Drunk_parrot?oldid=15183394) | 15183394, 2026-04-22 | Drop semantics and stage-dependent replacement |
| [Dirty robe](https://oldschool.runescape.wiki/w/Dirty_robe?oldid=15184216) | 15184216, 2026-04-22 | Tegid acquisition and fake-man ingredient |
| [Troll thistle](https://oldschool.runescape.wiki/w/Troll_thistle?oldid=15184747) | 15184747, 2026-04-22 | Moving plant and wither-on-drop behavior |
| [Troll potion](https://oldschool.runescape.wiki/w/Troll_potion?oldid=15185541) | 15185541, 2026-04-22 | Level-31 recipe and 77.5 Herblore XP |
| [Fake man](https://oldschool.runescape.wiki/w/Fake_man?oldid=15183391) | 15183391, 2026-04-22 | Replacement/drop-trick behavior and Burntmeat hand-in |
| [Storeroom key](https://oldschool.runescape.wiki/w/Storeroom_key?oldid=15185703) | 15185703, 2026-04-22 | Drawer source and one-time door unlock |
| [Dream Mentor](https://oldschool.runescape.wiki/w/Dream_Mentor?oldid=15292367) | 15292367, 2026-08-10 | Direct quest prerequisite and goutweed consumer |
| [My Arm's Big Adventure](https://oldschool.runescape.wiki/w/My_Arm%27s_Big_Adventure?oldid=15289163) | 15289163, 2026-08-06 | Direct quest prerequisite, shared Burntmeat, and later patch ownership |

Transition aid only: Quest Helper at commit
[`5ea99d5`](https://github.com/Zoinkwiz/quest-helper/tree/5ea99d5ea9ba3fb096ebe7b5ed02d80883e9819d/src/main/java/com/questhelper/helpers/quests/eadgarsruse)
observes primary values 0, 10, 15, 20, 25, 30, 50, 60, 70, 80, 85,
86, 87, 90, and 100; both Pete dialogue conditions; Eadgar's rescue bit;
its referenced route zones, items, NPCs, and scenery; and the three reward
classes.
`python3 tools/questhelper_extract.py eadgarsruse --check` resolves dbrow 62
and every referenced gameval. Quest Helper is a state/test oracle, not proof of
local server behavior.

The local `../LostCity_Server` checkout at `0b6a0cb7` is useful provenance, not
a canonical target. Its omitted `quest_eadgar.rs2` contains substantially more
complete Pete flags, bulk hand-ins, parrot/fake-man recovery, key consumption,
door traversal, patrol sight lines, club projectile, knockout/teleport queues,
and completion flow. It still targets an older engine/revision and carries
open TODOs. Reuse its symbolic evidence and tested engine shapes only after
comparison with the pinned current contract.

## 2. Canonical contract

Eadgar's Ruse is a members-only, intermediate, medium Troll-series quest
released 5 October 2004. It starts with Sanfew upstairs in Taverley. Starting
requires completed Druidic Ritual and Troll Stronghold plus current, boostable
level 31 Herblore. Troll Stronghold completion is the prerequisite; rescuing
Eadgar is an optional side objective in that earlier quest and may still need
to be done after Eadgar's Ruse starts. There is no mandatory new combat in this
quest, although Trollheim travel is dangerous and the metadata recommends
combat level 50.

A canonical run must:

1. use the standardized `Start the Eadgar's Ruse quest?` offer, accept a
   boosted level 31 with the normal warning, require both predecessor end
   states, and write 0→10 only after acceptance;
2. permit Burntmeat-first and Eadgar-first routes, using 20 and 25 to converge
   safely on Eadgar's state-30 parrot plan;
3. record both Parroty Pete clues, reject vodka+chunks before they are known,
   consume one pair to make alco-chunks, and implement every hatch flavor,
   catch, duplicate, bank, drop, and recovery branch;
4. show the parrot to Eadgar, hide it under the prison rack, preserve its
   intermediate chatter, then bulk or partially hand in exactly one supported
   log, five raw chickens, ten grain, and one dirty robe with accurate counts;
5. let Tegid give or drop the robe after either canonical threat, prevent
   inventory/bank duplication, and recover every lost critical item through
   its current owner;
6. dry and grind the moving troll thistle, enforce current level 31 when
   combining ground thistle with a ranarr potion (unf), award exactly 77.5
   Herblore XP once, and give the troll potion to Eadgar;
7. retrieve the trained parrot, atomically exchange it for a recoverable fake
   man, give one fake man to Burntmeat, receive one burnt meat, and learn the
   key location;
8. issue one inventory-scoped storeroom key, consume it on the one-time
   state-90→100 unlock, and thereafter let that player traverse both sides of
   the door without another key;
9. run the actual repeating patrol/sight puzzle: a detecting guard interrupts
   the action, throws the club, deals 0–6 unblockable damage, and sends only
   that player to the entrance; a successful crate search grants goutweed and
   invokes the crate guard with correct timing; and
10. atomically consume one goutweed at Sanfew and complete 100→110 exactly
    once with 1 quest point, 11,000 Herblore XP, the Trollheim spell/tablet
    unlocks, permanent storeroom access, cultivation, theft, and a functional
    one-for-one herb exchange.

The reward is not just the state-110 scroll. Server-authoritative spell and
tablet gating, the Hard Fremennik diary cast, level-29 gout-tuber cultivation,
post-quest storeroom patrol, and Sanfew's standard-herb-table exchange are part
of Gate D. Dream Mentor and My Arm's Big Adventure must see completion; later
goutweed consumers must receive items only from legitimate sources.

## 3. Native identity and persistence

| Field | Native value / expected behavior |
| --- | --- |
| Quest metadata ID | 62 |
| Dbrow | `quest_eadgarsruse` |
| Type / difficulty / length | Members; intermediate; medium |
| Series | Troll, third entry |
| Release | 5 October 2004 |
| Start | `sanfew` (NPC 5044), coordinate 315903331 |
| Primary carrier | `%eadgar_quest`, native clean permanent varp 335 |
| Local side carriers | `%eadgar_bits`, `%eadgar_grain`, `%eadgar_chickens`, server-authored permanent transmitted varps |
| Start / end | 0 / 110 |
| Requirements | Druidic Ritual; Troll Stronghold; boostable Herblore 31 |
| Rewards | 1 QP; 110000 raw Herblore XP (11,000 displayed), plus 775 recipe XP (77.5 displayed) |

The dbrow correctly records both quest prerequisites, boostability, level 31,
the intermediate/medium identity, release date, end state, quest point, and
11,000 reward XP. Runtime start code does not consume that metadata and
disagrees with it.

### 3.1 Primary ladder

| `%eadgar_quest` | Native checkpoint | Current local result |
| ---: | --- | --- |
| 0 | Not started / Sanfew offer | Sanfew requires base 31 and the unreachable rescue bit, not the Troll Stronghold end state; no standard offer |
| 10 | Started / find Eadgar or Burntmeat | Route dialogue exists if state is injected |
| 15 | Eadgar first / visit Burntmeat | Works broadly |
| 20 | Burntmeat first / visit Eadgar | Works broadly |
| 25 | Both consulted / return to Eadgar | Converges with 20 into the parrot plan |
| 30 | Learn Pete's clues and catch parrot | Pete clues are never persisted or required; several hatch/recovery branches are absent |
| 50 | Hide shown parrot under prison rack | Hide works, but lost-parrot recovery and intermediate rack chatter are absent |
| 60 | Report hidden parrot | Eadgar advances to 70 |
| 70 | Deliver fake-man ingredients | Only one-item-at-a-time Use-on-NPC works; Talk never performs current bulk hand-in |
| 80 | Make/deliver troll potion | Recipe ignores level 31 and awards no 77.5 XP |
| 85 | Fetch trained parrot | Rack works but refuses rather than dropping on full inventory |
| 86 | Return parrot to Eadgar | Missing parrot is redirected to the rack rather than canonical Eadgar recovery |
| 87 | Deliver fake man | Handoff works if owned; Eadgar never replaces a lost fake man |
| 90 | Learn key location / unlock storeroom | Drawer works inventory-only; door retains/requires key and uses a temporary global transform |
| 100 | Storeroom unlocked / obtain and return goutweed | Crate is unrestricted and patrol AI is absent |
| 110 | Complete | Reachable only from injected progress; written before XP/QP/scroll, with incomplete reward unlocks |

The values are native and must not be renumbered. Values 20 and 25 represent
valid order-sensitive convergence, not obsolete states. State 60 is the
post-hide report, and state 100 is the durable per-player door entitlement;
neither should be collapsed into inventory guesses.

### 3.2 Side state

`%eadgar_bits` is a packed contract, not a generic scratch integer:

| Bit | Intended purpose | Current local result |
| ---: | --- | --- |
| 0 | Pete clue: prior vodka incident | Declared as a constant but never set or tested |
| 1 | One accepted log | Used as delivered-item progress |
| 2 | One accepted dirty robe | Used as delivered-item progress |
| 3 | Historical scarecrow range/reserved side state | No named local owner; must not be overwritten casually |
| 11 | Pete clue: parrots prefer pineapple chunks | Declared as a constant but never set or tested |

`%eadgar_chickens` and `%eadgar_grain` are durable partial-delivery counters.
Clamp them to 0–5 and 0–10, validate bit ownership, and never whole-write the
packed field. Ingredient commits and counter/bit changes must be one
transaction, so a crash cannot consume an item without recording it or record
it without consumption.

The Troll Stronghold bit `%troll_freed_eadgar` is also durable shared state.
No production write exists in this tree. Repair its canonical owner—the cell
door interaction in Troll Stronghold—rather than forging it in Sanfew's
dialogue. Eadgar's Ruse start must still gate on Troll Stronghold's end state,
not on whether this optional rescue side objective was completed earlier.

### 3.3 Save migration and reward reconciliation

Preserve all declared primary values and valid partial deliveries. Migration
must account for this local build's unreachable start and non-atomic finale:

- state 0 with Troll Stronghold complete remains startable regardless of the
  rescue bit; the later route can require and repair the rescue naturally;
- state 10+ proves Eadgar's Ruse was already started/imported and may safely
  reconcile Eadgar's rescue/access side state without resetting the quest;
- state 30 preserves Pete clue bits, while state 50+ may mark both clues
  satisfied because the player already produced and showed the parrot;
- state 70 preserves valid log/robe bits and clamped chicken/grain counts;
  state 80+ proves all four ingredient requirements were delivered;
- state 85/86/87 must recover the logical parrot/fake-man actor from the
  primary state without duplicating banked or live inventory items;
- state 100+ proves the storeroom is permanently unlocked and must not depend
  on a retained legacy key;
- provenance-marked local state 85+ may receive the omitted 77.5 potion XP
  once; imported canonical saves must not receive it again;
- state 110 is reward-ambiguous because local code writes it before 11,000 XP,
  QP/count, jingle, and scroll. Recompute QP/completed count from end states,
  but use a versioned reward ledger or one documented grandfather policy for
  XP rather than guessing from total Herblore XP; and
- recognize state 110 written by My Arm debug tools as a prerequisite fixture,
  not proof that any Eadgar reward phase ran.

Do not use a naive XP floor: legitimate players already need level 31 and may
have arbitrary Herblore XP, while debug/imported state-110 saves may have none.
Migration and reward delivery must be idempotent across repeated login, crash,
debug completion, and schema upgrades.

## 4. Current implementation surface

The direct `quest_eadgar` root contains eight files and 615 lines. Its core
narrative is split across the 297-line Troll Stronghold Eadgar owner and the
194-line Taverley Sanfew owner, making 1,106 immediate narrative lines before
shared Cooking, Herblore, Magic, Farming, diary, door, combat, and downstream
owners.

| File | Lines | Current responsibility |
| --- | ---: | --- |
| `quest_eadgar/configs/eadgar_bits.constant` | 5 | Two delivered-item bit positions |
| `quest_eadgar/configs/quest_eadgar.constant` | 22 | Native primary ladder plus unused Pete bit positions |
| `quest_eadgar/configs/quest_eadgar.varp` | 23 | Primary, packed, grain, and chicken permanent carriers |
| `scripts/eadgar_druid_washing.rs2` | 35 | Simplified Tegid robe dialogue/grant |
| `scripts/eadgar_journal.rs2` | 159 | Dynamic journal across every declared state |
| `scripts/eadgar_troll_chief_cook.rs2` | 218 | Burntmeat dispatch, rack, drawers, door, simplified crate |
| `scripts/eadgar_troll_thistle.rs2` | 79 | Moving thistle and dry/grind/mix hooks |
| `scripts/eadgar_zoo_keeper_aviary.rs2` | 74 | Pete dialogue, alco-chunks, and hatch catch |
| `quest_troll/scripts/troll_eadgar.rs2` | 297 | Eadgar route dialogue, partial delivery, potion/parrot/fake-man handling |
| `areas/area_taverly/scripts/sanfew.rs2` | 194 | Shared Druidic Ritual/One Small Favour owner, quest start/finale/exchange |
| `skill_cooking/...` | shared | Vodka/chunks and fire-drying dispatch |
| `skill_herblore/...` | shared | Thistle grinding and ranarr-vial dispatch |
| `skill_magic/scripts/spells/teleport.rs2` | 141 | Unconditionally accepts Trollheim spell clicks |
| `skill_construction/scripts/poh_portal_nexus.rs2` | shared | Correctly gates nexus destination 32 on state 110 |

Gate A also owns or consumes:

- Druidic Ritual and Troll Stronghold end states, Eadgar's cell key/door and
  rescue bit, Trollheim/Stronghold/prison travel, climbing boots, and the
  shared route NPCs;
- all quest item, hatch, rack, drawer, storeroom door/crate, guard, moving
  thistle, animation, projectile, sound, fade, and stunned assets;
- inventory/bank transactions, ground-item fallback, dialogue knowledge,
  private interruption queues, patrolling NPC AI, line-of-sight, damage,
  teleport, door transforms, logout, world hop, and concurrent players;
- shared Sanfew and Burntmeat dispatch for Druidic Ritual, One Small Favour,
  My Arm's Big Adventure, and Making Friends with My Arm;
- Trollheim spellbook casting, POH portals/nexus, scroll redirection/tablets,
  Fremennik diary events, goutweed farming and herb-drop exchange;
- Dream Mentor, My Arm's Big Adventure, Dragon Slayer II, and every other
  goutweed consumer; and
- shared quest rewards, journal/quest list, generic quest cheat, My Arm debug
  prerequisites, and stale port/audit records.

The route uses symbolic names, native primary values, the shared completion
scroll, and a detailed journal. Several shared item-use hooks avoid duplicate
triggers. Those are useful foundations; they do not prove the prerequisite,
transactions, patrol, or reward contract.

The current `QUESTHELPER_CONTENT_PORT_QUEUE.md` claim that all seven gaps were
fixed and the quest is fully playable is contradicted by the unreachable
rescue bit, omitted patrol and recipe XP, completion ordering, and missing
reward services. `CONTENT_PORT_QUEUE.md` and the Magic comments openly record
the Trollheim spell gate as deferred. Reconcile all status labels only after
Gate D passes.

## 5. Start, prerequisites, and shared Troll Stronghold route

The dbrow's prerequisite metadata is correct, but Sanfew's runtime predicate is
not. It checks completed Druidic Ritual, `stat_base(herblore) >= 31`, and
`%troll_freed_eadgar = true`. This creates three defects:

- base-stat checking rejects a valid boosted level 31 and omits the canonical
  warning that another boost will be needed for the potion;
- it accepts a player who freed Eadgar before finishing Troll Stronghold,
  despite Troll Stronghold being the direct quest prerequisite; and
- it rejects a completed Troll Stronghold player who did not perform the
  optional Eadgar rescue, even though that rescue can be done after starting.

More seriously, the rescue predicate can never become true normally. The cell
key can be obtained, and the cache exposes `troll_celldoor_eadgar` with an
Unlock option, but there is no handler for that loc and no production write to
`%troll_freed_eadgar`. The bit is only read by Sanfew, the Troll Stronghold
journal/guard/Eadgar dialogue, and related checks. The start is therefore
unreachable without an imported/debug-mutated save.

Repair Troll Stronghold ownership first: validate the correct cell/key and
quest stage, unlock the per-player objective, consume or preserve the key per
current behavior, transform/traverse the door safely from both sides, write
the rescue bit exactly once, and update Eadgar's prison/cave visibility and
Troll Stronghold journal. Then make Sanfew gate on both predecessor end states
and current boosted Herblore, use the standard offer, and write 0→10 only on
Yes. Test all Sanfew shared-quest precedence so One Small Favour or Druidic
Ritual cannot swallow the Eadgar offer/finale permanently.

The climbing-boot/Trollheim/Stronghold maplink surface must be verified from
the real spawns. Quest Helper resolves the expected doors, stairs, cave
entrance, prison zones, Berry, cell key, and Eadgar, but resolution is not
behavioral proof.

## 6. Eadgar, Burntmeat, and parrot knowledge

States 10, 15, 20, and 25 broadly support both canonical early orderings. The
local dialogue is heavily compressed: Eadgar's initial four-way tree becomes
a two-choice quest prompt, re-talk/default menus and parrot plan details are
missing, and the state-20/25 convergence does not preserve the full dialogue.
Modernize against the transcript while retaining the valid ordering values and
shared mountain-goat-stew service.

Parroty Pete exposes the two correct clues, but neither branch sets its
declared bit. `~eadgar_make_alco_chunks` ignores both bits and the quest phase,
so vodka and pineapple chunks can be combined without ever speaking to Pete.
Conversely, the hatch artificially restricts catching to states 10–30. The
canonical behavior and local LostCity evidence distinguish knowledge, current
parrot ownership in inventory/bank, whether Eadgar has seen it, and post-loss
recovery.

Implement one owner-aware parrot state machine:

- Pete's vodka-history and pineapple-food choices set bits 0 and 11
  independently and in either order;
- combining before both clues gives current feedback and consumes nothing;
- raw liquor and plain chunks used on the hatch give their distinct messages,
  with plain chunks consumed as current behavior requires;
- a successful alco-chunk catch validates capacity and inventory/bank
  uniqueness, consumes one alco-chunk, plays the parrot/Pete scene, and grants
  one parrot atomically;
- Drop releases the parrot without a ground item;
- before Eadgar has seen it, another clue-qualified hatch catch replaces it;
  after state 50, Eadgar owns recovery even when the player banked/lost it;
- showing the parrot must not consume it, and state 50 cannot be committed
  without a recoverable logical parrot; and
- full inventory, banked items, dialogue close, relog, duplicate packet, and
  simultaneous players cannot lose or duplicate the parrot.

At state 50 the rack hide currently deletes the parrot and then writes state
60 without a protected transaction. Searching at states 60–80 incorrectly
says nothing is present; current behavior plays one of three untrained parrot
lines. Restore the chatter, exact hide message, state ownership, and safe
resume. The rack is shared scenery but the hidden-parrot state is per player.

## 7. Fake-man ingredients and troll potion

After reporting the hidden parrot, current Eadgar takes all currently supplied
ingredients through Talk and accurately reports what was accepted/remaining.
The local port instead requires using each log, robe, chicken, and grain on
Eadgar individually. Ordinary Talk only repeats a static list. The category
test also accepts every `firemaking_logs` member without proving the current
supported log set.

Create a single partial bulk hand-in transaction that:

- calculates only still-needed quantities and chooses a supported log by
  current priority;
- locks exact slots, consumes at most 1 log, 1 robe, 5 total chickens, and 10
  total grain, then commits matching bits/counters;
- supports multiple trips and the transcript's accurate owned/accepted text;
- ignores banked ordinary ingredients but checks inventory+bank before Tegid
  issues another unique robe;
- supports both Tegid threat choices and current full-inventory ground fallback
  without granting a phantom item; and
- advances 70→80 once, only when all requirements are durably satisfied.

The local troll thistle correctly changes among five symbolic positions and
withers when the raw/dried item is dropped. Picking moves the NPC even if an
unchecked inventory add fails; dry and mix likewise mutate with unchecked
delete/add pairs. The ground-thistle recipe has no membership, phase, or
current Herblore check and awards no XP. A player who met base 31 at the local
start can therefore mix at any later level and receives 0 instead of 77.5 XP.

Modernize the chain as three atomic transformations. The final mix must check
current/boosted Herblore 31, consume one ground thistle and one ranarr potion
(unf), create one troll potion, and grant exactly 775 raw XP only after commit.
Closing/relogging or sending both item-use directions must not double the
recipe. Eadgar's potion hand-in consumes exactly one and writes 80→85 in one
transaction.

At state 85, rack retrieval should advance and grant/drop the parrot together;
the local full-inventory refusal disagrees with the transcript's ground
fallback. At state 86, Eadgar must recover a missing trained parrot himself,
then atomically convert it into one fake man. At state 87, he must replace a
lost fake man (including the canonical drop-trick behavior) rather than merely
saying to take the missing item to Burntmeat.

Burntmeat's shared dispatcher correctly prioritizes Eadgar's Ruse before My
Arm's Big Adventure and Making Friends with My Arm under ordinary states.
Keep one trigger owner and regression-test every quest combination. The
fake-man hand-in currently writes state 90 before deleting it and adding burnt
meat; reorder it as a protected exact transaction with one guaranteed freed
slot and preserve the key-disclosure choice/re-talk.

## 8. Storeroom key, patrol, and goutweed

The key/door implementation disagrees with the primary ladder. Drawer Search
checks only inventory, so banking the key permits duplicates. Door Open checks
for a key before considering state, never consumes it, applies a temporary
global `loc_change`, and requires the retained key on every later closed-door
interaction. State 100 is supposed to be the durable unlock.

Use an inventory+bank uniqueness check for state 90, current no-space text,
and a one-time exact key transaction. Opening from outside with the key commits
90→100 and consumes one key; state 100/110 opens without it, and the inside
face always permits exit. Door animation/collision must use the shared modern
door owner without one player's quest entitlement becoming another player's
state change.

The advertised guard puzzle does not exist. Cache NPC, thistle/guard
animations, club projectile, stunned spot animation, and scenery are present,
but the current server has no `_troll_sguard` or `eadgar_storeroom_guard` AI
handler. The crate instead:

- has no quest or unlock-state predicate;
- always grants goutweed if one slot is free;
- rolls a flat one-in-three flavor event and 0–6 damage;
- never detects actual sight, interrupts movement/dialogue, throws a club,
  fades/stuns, or teleports the player to the entrance; and
- leaves the player beside the crate, enabling immediate unlimited gathering.

Because the door transform is global and the crate is unrestricted, another
player can expose the room and a prequest player can obtain quest/downstream
goutweed. Full inventory is refused instead of using the current ground-item
fallback.

Port the patrol as a modern world-NPC system, not a random crate penalty.
Patrolling guards follow their four-step cycles and evaluate current/future
view squares; the sleeping crate guard reacts after a successful search. Each
player needs an independent interruption queue so one player's detection does
not close, damage, or teleport another. Validate line-of-sight, safespots,
movement interpolation, exact 0–6 damage, protection-prayer immunity, logout,
death, world hop, overlapping detections, and crate-dialogue cancellation.
State 100 or 110 authorizes the crate; each committed search grants one item
or current ground fallback before the guard response. Preserve any
live-evidenced brief multi-click window rather than inventing either a strict
one-per-trip rule or unlimited stationary harvesting.

## 9. Finale, rewards, and permanent services

Sanfew accepts a goutweed from any state at least 90. Supporting a legitimately
obtained goutweed is reasonable, but the mutation is unsafe: he deletes the
item, writes state 110, advances Herblore, and only then calls the shared QP/
scroll helper. A crash can therefore leave consumed goutweed with state 90,
or a completed state 110 with no XP, QP/count, jingle, or scroll. The helper is
not a quest-state setter and is not itself idempotent.

Use one protected reward transaction:

- lock and consume exactly one goutweed;
- durably record the 11,000-XP entitlement and state 110;
- derive exactly 1 QP and one completion-count increment from the dbrow;
- play one completion jingle and mount one reward scroll; and
- make repeated talk, packet, queue, relog, migration, and debug calls no-ops.

The reward icon may remain goutweed after real-client comparison with the
pinned scroll. Burnt meat is an earlier route reward, not another finale add.

Every permanent service needs repair:

- **Trollheim spell:** the standard spellbook button currently goes directly
  to `~magic_teleport`, consuming 2 fire + 2 law runes and awarding 68 Magic
  XP with no Eadgar predicate. Add a server-side state-110 check before every
  rune/XP mutation; client visibility is not authorization.
- **POH destinations:** portal nexus destination 32 already checks state 110.
  Verify portal chambers and every menu/direct packet use the same policy.
- **Redirected tablet:** no scroll-redirection or Trollheim-tablet creation/use
  script was found. Implement the one-scroll + one house-tablet conversion,
  state-110 gate, refund/reversion behavior, and tablet destination checks.
- **Hard Fremennik diary:** the diary framework has only generic counters; a
  successful standard Trollheim spell cast does not emit the exact hard-task
  event. Emit it after teleport commit, never on a blocked/failed/tablet/POH
  teleport unless the current task explicitly accepts that source.
- **Storeroom theft:** keep the real puzzle available during state 100 and
  after completion.
- **Cultivation:** no gout-tuber farming row was found. Add quest-complete and
  Farming-29 gates, ordinary herb-patch lifecycle/yield/disease, and the
  current Troll Stronghold/Weiss patch exclusions.
- **Sanfew exchange:** the post-quest branch currently deletes one goutweed
  and gives nothing while claiming the exact reward is undocumented. The
  pinned Sanfew/Goutweed pages explicitly define one herb from the standard
  128-weight herb drop table. Exchange one-for-one atomically with capacity
  checks and the shared authoritative table.

A reward scroll that advertises these services while the spell is pre-unlocked
and the other services are absent is a Gate D failure.

## 10. Downstream and shared-owner integration

Dream Mentor and My Arm's Big Adventure correctly hard-check `%eadgar_quest >=
110` in their normal prerequisite procs. Retest states 100 versus 110 and
reward-migrated completions. Dragon Slayer II and Dream Mentor consume
goutweed; once the unrestricted crate is fixed, verify all legitimate storage,
farming, and Gnome Restaurant sources remain usable without manufacturing an
extra direct quest prerequisite.

Burntmeat is shared in chronological order by Eadgar's Ruse, My Arm's Big
Adventure, and Making Friends with My Arm. Sanfew is shared by Druidic Ritual,
Eadgar's Ruse, One Small Favour, and Sanfew Serum dialogue. Keep one trigger
for each NPC and centralize dispatch by active stage. Test overlapping active
quests, post-quest branches, item-on-NPC operations, and no-return dialogue so
one quest does not shadow another permanently.

My Arm's `::myarmsbigadventure` and `::mabarun` debug procs directly set
`%eadgar_quest = 110` to manufacture their prerequisite. They do not grant
11,000/77.5 XP, QP/count, side-state consistency, or rewards. Replace that
write with an explicit, idempotent prerequisite fixture or isolate it in an
ephemeral test save. A downstream debug tool must not corrupt Eadgar's normal
save contract.

The Hard Fremennik Diary requires a successful Trollheim spell cast. Its exact
task is not represented by incrementing a generic hard counter blindly; use a
named one-shot event and durable task bit, then let the diary aggregate derive
the count.

## 11. Journal, debug tools, and stale records

The journal is detailed and wired for all declared values, including partial
ingredient counts and potion/goutweed inventory. It inherits runtime defects:
before start it says rescuing Eadgar is a requirement rather than displaying
the dbrow's completed Troll Stronghold contract; inventory-only checks can
misdescribe banked potion/goutweed; and state 110 claims the Trollheim spell
was taught even though it was already server-usable before the quest.

Repair state and item ownership first, then update journal text from the same
authoritative predicates. Preserve native historical wording where verified,
but do not encode implementation workarounds or use the journal as state.

The generic `::complete quest_eadgarsruse` has no dispatch arm. The My Arm
debug tools are the only found direct completion writes. Add an Eadgar fixture
that can prepare each checkpoint and an idempotent fully rewarded completion,
including rescue/access, Pete bits, item counters, key entitlement, recipe and
reward ledger, QP/count, and cleanup. Never make unrelated quest debug commands
the supported completion path.

Update or supersede these records after implementation:

- `QUESTHELPER_CONTENT_PORT_QUEUE.md` says `audited-fixed`, all seven gaps
  landed, and the quest is fully playable;
- `CONTENT_PORT_QUEUE.md` says the journal/route slices are done but correctly
  records the Trollheim spell gate as deferred;
- `SKILLS_CONTENT_PORT_QUEUE.md` still treats quest mixtures as blocked;
- Magic source comments explicitly say the Eadgar spell gate was dropped; and
- `DOORS_GATES_QUEUE.md` correctly marks Eadgar's cell-door Unlock deferred.

Documentation and compile success are not runtime evidence. Do not mark the
quest `audited-ok` until the prerequisite, route, permanent rewards, and
concurrent patrol pass Gate D.

## 12. Modernization sequence

1. Freeze the pinned primary/side/item/patrol/reward contract and capture
   current client evidence for boosted start, both early orderings, Pete bits,
   hand-ins, recovery, guard timing, completion scroll, and every unlock.
2. Add versioned quest/reward migration; reconcile rescue/access, primary
   values, Pete/delivery state, omitted potion XP, ambiguous state-110 rewards,
   QP/count, and debug-written completions.
3. Repair Troll Stronghold's Eadgar cell door/rescue owner, then replace
   Sanfew's start predicate and offer with the canonical metadata contract.
4. Modernize Eadgar/Burntmeat convergence and Pete's two knowledge branches;
   make alco-chunks, hatch behavior, parrot ownership, rack state, Drop, and
   recovery transactional.
5. Replace item-on-NPC-only delivery with current partial bulk hand-in, exact
   counters, Tegid uniqueness/full-inventory behavior, and safe supported logs.
6. Modernize thistle dry/grind/mix and potion hand-in with level 31, 77.5 XP,
   interruption safety, trained-parrot recovery, fake-man recovery, and exact
   Burntmeat transaction.
7. Make key issuance unique, consume it on durable 90→100 unlock, and implement
   correct two-sided modern door behavior.
8. Port the actual storeroom patrol/detection/club/knockout/teleport system with
   concurrent-player isolation and state-gated crate transactions.
9. Make Sanfew's 100→110 hand-in exactly once, then enforce Trollheim spell,
   tablet/scroll, diary, storage, cultivation, and herb-exchange rewards.
10. Regression-test downstream/shared NPC owners, replace corrupting debug
    writes, reconcile stale records, run Gate D, and update status only with
    attached real-client and concurrency evidence.

Most quest dialogue and recipes fit current RuneScript/config machinery. The
patrol may require general modern NPC route/sight/interruption support, but it
must not be replaced with quest-local random damage or global player queues.

## 13. Verification matrix

### Static and pack checks

- Run `python3 tools/questhelper_extract.py eadgarsruse --check` against the
  pinned helper commit.
- Resolve dbrow 62; all sixteen primary checkpoints; rescue/Pete/item carriers;
  both prerequisites; Sanfew, Eadgar, Burntmeat, Pete, Tegid, guards, thistle;
  every quest item; cave/prison/kitchen/rack/drawers/door/crate/route loc;
  animations, projectile, sounds, overlay, spell/tablet, reward scroll, and
  completion jingle.
- Fail on duplicate shared NPC/item triggers, raw entity IDs, whole writes to
  `eadgar_bits`, start without both quest end states, a missing rescue write,
  unchecked item commits, unrestricted crate, random substitute patrol,
  rewards without 100→110, or an ungated advertised unlock.
- Verify each Sanfew/Burntmeat/Eadgar/Pete branch and all spell/tablet/portal/
  crate entry points authorize server-side.
- Run `make -C src mock230-scripts` and the revision-239 pack check.

### Start, route, and dialogue tests

- Exercise 0→10→15/20/25→30→50→60→70→80→85→86→87→90→100→110 from real
  spawns, including both Burntmeat/Eadgar orderings, every refusal/re-talk,
  optional Eadgar rescue timing, and post-quest dialogue.
- Test Druidic Ritual/Troll Stronghold incomplete/complete independently,
  Eadgar rescued/not rescued, Herblore base/current 30/31/boosted/drained,
  members/free worlds, standard offer warning, decline, and repeated packets.
- Unlock Eadgar's cell with correct/wrong/no key, both door faces, already
  rescued, full inventory, relog, and two players. Verify one durable rescue
  bit and correct Troll Stronghold journal/NPC visibility.
- Traverse every expected Trollheim/Stronghold/prison path with climbing boots,
  alternatives, route danger, logout, and shared later-quest states.

### Parrot, ingredients, and potion tests

- Ask neither/each/both Pete clues in both orders; combine both item-use
  directions before/after knowledge, prequest/postquest, inventory/bank parrot,
  and repeated packets. Verify exact consumption and no duplicates.
- Use liquor, plain chunks, and alco-chunks on the hatch; test 0/1 free slot,
  Pete proximity, Drop, relog, bank, Eadgar-seen/not-seen recovery, and two
  simultaneous players.
- Hide/search the rack at every adjacent state, all three chatter lines,
  dialogue close, full inventory ground fallback, drop/despawn, and repeated
  clicks. One player's parrot state must not affect another's.
- Deliver 0–5 chickens, 0–10 grain, robe, and every supported/unsupported log
  in all orders and trip splits; test Talk versus item-use compatibility,
  exact counts, banked/surplus items, Tegid's two choices, duplicates, capacity,
  relog, and interruption after every mutation.
- Pick/dry/grind/mix thistle with full inventory, both use directions, wrong
  fire/source/item, state boundaries, current Herblore 30/31/boosted/drained,
  duplicate packets, and relog. Verify one potion, exact inputs, and 77.5 XP.
- Give potion, retrieve/lose/recover trained parrot, create/lose/recover/drop-
  trick fake man, and hand it to Burntmeat. Verify one burnt meat and one state
  transition at each phase.

### Storeroom and concurrency tests

- Search drawers with key absent/in inventory/in bank, 0/1 free slot, state
  89/90/100/110, duplicate packets, and relog; issue at most one logical key.
- Open the door from both faces before/after unlock, with wrong/no/duplicate
  key, two players, movement interruption, temporary scene rebuild, logout,
  and reconnect. Consume one key once and persist access per player.
- Run every patrol cycle and safespot with walk/run, diagonal/future tiles,
  blocked line-of-sight, protection prayers, zero/low HP, dialogue/interface
  open, logout/world hop, death, and overlapping guard detections.
- Test two or more concurrent players at independent tiles and the same crate;
  detection, damage, close, queue, and teleport must target only the caught
  player.
- Search the crate prequest, states 90/100/110, without door entitlement, with
  0/1 free slot, banked goutweed, repeated clicks, and during detection. Verify
  exact grants/ground fallback and current post-search guard behavior.

### Completion, migration, and reward tests

- Interrupt after every finale phase. Resume with state 110, exactly 11,000
  reward XP, 1 QP, one completed-count increment, one jingle, one scroll, and
  one consumed goutweed.
- Repeat Sanfew, completion queue, packets, relogs, migration, and debug
  completion; every later reward attempt is a no-op.
- Migrate every primary value, rescue bit, all Pete/item-bit combinations,
  chicken/grain values below/at/above bounds, state 85+ with/without recipe XP,
  state 110 with every partial reward phase, QP/count drift, banked/surplus
  quest items, retained keys, and My Arm debug provenance. Run migration twice
  and compare durable results byte-for-byte.
- Verify journal, quest-list color, prerequisites, and owned/missing item text
  at every checkpoint and after bank/relog.

### Permanent unlock and downstream tests

- Cast Trollheim Teleport at states 100/110, Magic 60/61, exact/missing runes,
  free/members world, interrupted/blocked teleport, forged button packet, and
  diary already/not complete. Consume runes/award 68 XP/teleport/emit diary
  only after all gates commit.
- Create, use, refund, and revert Trollheim tablets with state 100/110, exact
  scroll/house tablet, every menu/direct packet, capacity, duplicate requests,
  POH portal/nexus routes, and relog.
- Steal goutweed post-quest, exchange 1 and many with Sanfew, full inventory,
  every 128-weight herb-table bucket, repeated packet, and overlapping shared
  quest dialogue. Every committed goutweed yields exactly one herb.
- Plant gout tuber at Farming 28/29 and state 100/110 in ordinary, Troll
  Stronghold, and Weiss patches; test disease, compost lives, harvest yield,
  dig-up, diary/cape modifiers, and relog.
- Verify Dream Mentor and My Arm's Big Adventure reject 100 and accept 110;
  test shared Burntmeat/Sanfew dialogue combinations, Dragon Slayer II and
  Dream Mentor goutweed recipes, and the Hard Fremennik diary one-shot event.

### Real-client evidence

Capture the prerequisite failures and boosted warning; standard offer; both
early orderings; optional cell rescue; Pete clues and every hatch response;
parrot catch/drop/recovery; rack chatter; partial bulk ingredient text; Tegid
choices/full inventory; thistle transformations and 77.5 XP; trained-parrot and
fake-man recovery; Burntmeat reward/key disclosure; key consume/permanent door;
all patrol cycles, detection, 0–6 damage, fade/stun/teleport, successful crate;
Sanfew hand-in and state-110 scroll/XP/QP/journal; blocked/unblocked Trollheim
spell and tablet; diary event; goutweed theft/growing/exchange; concurrent
players; relog recovery; migration; and idempotent debug completion.

`verified-modern` requires the visible 0–110 quest, reachable prerequisites,
exactly-once recipe/finale, real storeroom patrol, and every advertised
permanent reward to pass. An injected state, compile-clean dialogue port,
random crate hit, or displayed reward scroll is not sufficient.
