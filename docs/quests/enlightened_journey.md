# Enlightened Journey modernization audit

Status: `audit-pending` — the native quest row, permanent quest/route varbits,
world actors, six balloon sites, route map, flight-control interface, obstacle
models, crash-site actor, quest journal, completion helper, and broad dialogue
and transport scaffolding exist. The current route is only a narrated
approximation. It compresses and shifts the native state machine, omits the
willow-sapling Farming phase, does not consume the first test balloon, replaces
both experiments and the maiden-flight puzzle with messages, ignores flight
weight/pet/Firemaking checks and every failure path, can lose completion
rewards, unlocks all routes for free, and makes every later flight a free
teleport. Log storage, route-unlock puzzles, transport costs, Entrana item
restrictions, colored origami balloons, reward recovery, diary hooks, and the
Monkey Madness II prerequisite contract are absent or incorrect.

Audited: 2026-08-17

Governing plan: [Quest modernization plan](../QUEST_MODERNIZATION_PLAN.md). This
record applies that plan's Gates A–D to the quest root, revision-239 cache
state/interfaces/world placements, shared item-use and Farming owners, Entrana
access, the complete balloon network, reward/recovery lifecycle, diaries, and
downstream quest dialogue/prerequisites. It is an implementation specification,
not completion evidence.

## 1. Authoritative references

The current OSRS Wiki revisions below are pinned so implementation and review
use stable route, dialogue, puzzle, item, reward, and consumer contracts.

| Reference | Pinned revision | Use in this audit |
| --- | --- | --- |
| [Enlightened Journey](https://oldschool.runescape.wiki/w/Enlightened_Journey?oldid=15292357) | 15292357, 2026-08-10 | Identity, requirements, full route, flight failures, rewards, post-quest network, and downstream uses |
| [Enlightened Journey/Quick guide](https://oldschool.runescape.wiki/w/Enlightened_Journey/Quick_guide?oldid=15198128) | 15198128, 2026-04-25 | Ordered dialogue, materials, repeated level checks, willow branches, weight/pet preparation, and first-flight controls |
| [Transcript:Enlightened Journey](https://oldschool.runescape.wiki/w/Transcript%3AEnlightened_Journey?oldid=15263363) | 15263363, 2026-07-14 | Three-stage acceptance, refusals/re-talks, experiments, sapling/replacement, full-inventory reward retry, and post-quest recovery |
| [Balloon transport system](https://oldschool.runescape.wiki/w/Balloon_transport_system?oldid=15267219) | 15267219, 2026-07-19 | Six destinations, route unlocks, skill/log costs, control semantics, weight restriction, ordinary travel, and log storage |
| [Log storage](https://oldschool.runescape.wiki/w/Log_storage?oldid=15302324) | 15302324, 2026-08-15 | Shared 100-per-type capacity, noted deposits, non-withdrawable stock, pre-unlock access, source preference, and one-log charges |
| [Origami balloon](https://oldschool.runescape.wiki/w/Origami_balloon?oldid=15184606) | 15184606, 2026-04-22 | 36 Crafting, 35 XP, eight colors, repeated manufacture, launch animation, and 20 Firemaking XP |
| [Balloon structure](https://oldschool.runescape.wiki/w/Balloon_structure?oldid=15186004) | 15186004, 2026-04-22 | Papyrus/wool intermediate and unlit-candle completion transaction |
| [Auguste's sapling](https://oldschool.runescape.wiki/w/Auguste%27s_sapling?oldid=15185627) | 15185627, 2026-04-22 | Initial sapling/apples, four-hour willow growth, ordinary-willow alternative, and 30,000-coin replacement |
| [Bomber jacket](https://oldschool.runescape.wiki/w/Bomber_jacket?oldid=15182908) and [Bomber cap](https://oldschool.runescape.wiki/w/Bomber_cap?oldid=15276018) | 15182908 / 15276018, 2026-04-22 / 2026-07-26 | Two-slot completion reward and recovery from Auguste |
| [Cap and goggles](https://oldschool.runescape.wiki/w/Cap_and_goggles?oldid=15184622) | 15184622, 2026-04-22 | Auguste's reversible bomber-cap/gnome-goggles combination service |
| [Ardougne Diary](https://oldschool.runescape.wiki/w/Ardougne_Diary?oldid=15295881) | 15295881, 2026-08-13 | Medium task for arriving at Castle Wars; the initial route-unlock flight does not count |
| [Varrock Diary](https://oldschool.runescape.wiki/w/Varrock_Diary?oldid=15293707) | 15293707, 2026-08-12 | Medium task for departing from Varrock by balloon |
| [Monkey Madness II](https://oldschool.runescape.wiki/w/Monkey_Madness_II?oldid=15303312) | 15303312, 2026-08-16 | Requires quest completion and the Grand Tree route, therefore 60 Firemaking |
| [Transcript:Cold War](https://oldschool.runescape.wiki/w/Transcript%3ACold_War?oldid=15263373) | 15263373, 2026-07-14 | KGP report dialogue conditional on Enlightened Journey completion |

The sources identify Enlightened Journey as quest number 114, released 6
November 2006. It is an intermediate, short, members' quest with no quest
prerequisite. Starting requires 20 quest points, 20 Firemaking, 30 Farming,
and 36 Crafting; all three skills are boostable. Crafting 36 is checked again
when completing the origami balloon and Firemaking 20 is checked again before
the maiden flight, so a boost expiring after acceptance must still block the
relevant action. The reward is one quest point; 4,000 Firemaking, 3,000
Farming, 2,000 Crafting, and 1,500 Woodcutting XP; the bomber jacket and cap;
the Entrana–Taverley balloon route; and repeatable origami-balloon crafting.

Transition aid only: Quest Helper's
[`EnlightenedJourney.java`](https://github.com/Zoinkwiz/quest-helper/blob/5ea99d5ea9ba3fb096ebe7b5ed02d80883e9819d/src/main/java/com/questhelper/helpers/quests/enlightenedjourney/EnlightenedJourney.java)
and five flight helpers at commit
`5ea99d5ea9ba3fb096ebe7b5ed02d80883e9819d` confirm primary states
0/5/6/10/20/40/60/70/80/90/100, actors/coordinates, interface 471, three
screen maps per route, route levels/logs, and rewards. Its Firemaking start
requirement is not marked boostable, contrary to the current Wiki; the Wiki
and native dbrow control that policy. `python3 tools/questhelper_extract.py
enlightenedjourney --check` resolves every referenced quest row, NPC, loc,
object, and varbit.

## 2. Native quest identity and contract

| Field | Native value / expected behavior |
| --- | --- |
| Cache quest row | `quest_enlightenedjourney`; dbrow pack index 42, quest metadata ID 121 |
| Type / difficulty / length | Members / intermediate / short |
| Release | 6 November 2006 |
| Start | Speak to Auguste (`zep_piccard`) at 2808,3355,0 on Entrana and complete three explicit acceptance conversations |
| Requirements | 20 quest points; 20 Firemaking, 30 Farming, 36 Crafting; all boostable and required to start |
| Primary state | `%zep_quest`, bits 0–7 of permanent varp `zep_var` |
| Canonical primary values | 0/5/6/10 acceptance; 20 first model; 40 weighted experiment; 60 mob debrief; 70 real-balloon materials/sapling; 80 basket materials complete; 90 basket built/flight; 100 landed; 200 complete |
| Construction substates | `%zep_rdye`, `%zep_ydye`, `%zep_sandbags`, `%zep_silk`, `%zep_logs`, `%zep_bowl`, `%zep_alldye`, `%zep_multi_basket` |
| Route/world state | `%zep_multi_piccard`, `%zep_multi_cast`, `%zep_multi_gno`, `%zep_multi_craft`, `%zep_multi_varr` |
| Flight session state | `%zep_if_sandbags`, `%zep_if_logs`, `%zep_if_screen_dist`, `%zep_if_balloon_height`, `%zep_if_current_map`, `%zep_login_bool`, `%zep_return_check` on `zep_if_var` |
| Sapling recovery | Cache-native `%zep_sapling_check` and `%zep_sapling_patch` fields exist on `zep_if_var` |
| End state / quest points | 200 / 1 |
| XP reward | 4,000 Firemaking; 3,000 Farming; 2,000 Crafting; 1,500 Woodcutting (raw tenths 40000/30000/20000/15000) |
| Item reward | Bomber jacket and bomber cap; requires two free inventory spaces before settlement |
| Immediate unlock | Entrana ↔ Taverley route and repeatable plain/colored origami balloons |
| Expansion unlocks | Crafting Guild, Varrock, Castle Wars, and Grand Tree routes earned independently from Entrana |
| Downstream | Two medium diary tasks; Monkey Madness II prerequisite; conditional Cold War dialogue |

No parallel permanent quest variable is justified. Revision 239 already has
the full original state carrier, four independent route bits, construction
trackers, flight-session counters, sapling fields, actor/loc transforms, and
both interfaces. Log storage was added to OSRS in 2018 and has all six native
`zep_storage` loc wrappers in this cache, but repository-wide searches find no
server handlers or named permanent quantities/preference. Modernization must
identify or deliberately author one shared account-scoped storage schema for
five log types plus the inventory/storage source preference; it must not
overload the four-bit quest-flight `%zep_logs`
field or silently invent one counter per location.

## 3. Implementation surface

The direct root contains 842 lines across five files.

| Quest-owned path | Present responsibility | Audit result |
| --- | --- | --- |
| `configs/enlightenedjourney.constant` | State/requirements/rewards, placements, and port rationale | Native metadata is useful, but the state interpretation and claims that states/routes/minigame may be collapsed are contradicted by Wiki, helper, cache, and active code |
| `scripts/ej_shared.rs2` | Auguste dialogue, hand-ins, maiden flight, completion, journal | Simplified route can reach 200, but uses shifted states, omits sapling/cutscenes/failures, overwrites sandbag credit, and has unsafe completion |
| `scripts/ej_crafting.rs2` | Test balloon, sandbags, basket | Missing repeated skill checks/XP, sapling/Farming lifecycle, construction scene, colored balloons, and launches; blocks legitimate multiple post-quest balloons |
| `scripts/ej_network.rs2` | Six-site post-quest flights | Every flight is a free narrated teleport; no destination gate, cost, weight, Entrana restriction, storage, route puzzle, unlock XP, or diary event |
| `scripts/ej_debug.rs2` | Reset, item arm, and synthetic walkthrough | Writes the simplified substates and invokes reward behavior; useful only for isolated setup, not acceptance evidence |

Mandatory shared/cache surfaces:

| Path / subsystem | Relationship | Audit result / required ownership |
| --- | --- | --- |
| `configs/all.dbrow`, `all.varp`, and `all.varbit` | Quest metadata and durable/session state | Correct native row/end state/rewards and adequate state schema; local constants must stop redefining semantics |
| `interfaces/zep_interface.if` and `zep_interface_side.if` | Full flight board and controls | Cache contains the obstacle grid, balloon models, sandbag/log counters, Sandbags, Logs, Relax, brown-rope Tug, red-rope Emergency tug, and Bail; server never mounts or handles them |
| `interfaces/zep_balloon_map.if` | Six-destination map | Cache has destination buttons and hover behavior; current scripts substitute five-choice dialogue menus |
| `configs/all.loc` and world placements | Balloon, basket, and log-storage transforms at all six sites | Required visual assets/placements exist and are driven by native route/basket/pilot fields; `zep_storage` has Store/Preferences/Check but no runtime handler |
| `areas/world/configs/*.spawn` | Auguste, assistants, crash actor | Entrana Auguste, Taverley wrapper, Castle/Crafting/Varrock assistants, Grand Tree wrappers, and `zep_piccard_crash` are placed; Grand Tree assistant visibility also interacts with `%mm2_zep_gnome_pilot` |
| `quest_golem/scripts/golem_portal.rs2` and Crafting stringing | Papyrus/wool shared use-on ownership | Existing handlers correctly delegate to `~ej_make_structure`; that proc must enforce canonical stage/skill/XP and both item-use directions |
| `skill_crafting/scripts/glass/glass.rs2` | Shared sandpit ownership | Delegates to `~ej_fill_sandbag`; filling is broadly correct but quest phase and empty/full lifecycle need exact tests |
| `skill_farming/` | Auguste's sapling, tree growth, protection, branches | Generic willow sapling row exists, but special sapling is not registered and repository search finds no branch-cutting or special replacement integration |
| `areas/port_sarim/scripts/monk_of_entrana.rs2` and Entrana return monk | Entrana access | Boat travel works, but the script explicitly defers the weapon/armour search and therefore permits restricted items despite its dialogue |
| `quests/scripts/questpoints.rs2` | Completion UI/QP/count | Correct shared presentation, but cannot make the preceding two-item reward/state transaction atomic |
| `interface_questjournal/scripts/quest_journal.rs2` | Quest-list dispatch | Correctly calls `~ej_journal` for the native row |
| `quests/scripts/quest_cheat.rs2` | `::complete` adapter | Writes state 200 only; does not establish route bits, pilot transforms, reward ownership, or consumer state |
| `quest_monkeymadnessii/scripts/monkeymadnessii.rs2` | Shared Auguste and Grand Tree route prerequisite | Incorrectly advances MM2 and sets `%zep_multi_gno=1` as a side effect without verifying EJ completion or the earned route |
| `interface_diaries/` | Ardougne/Varrock medium tasks | Diary framework exists, but no balloon arrival/departure event hook is present |
| `quest_coldwar/` | Conditional KGP dialogue | Local report sequence lacks the transcript's Enlightened Journey completion branch |

## 4. Primary state, transforms, and migration risk

| Canonical value | Canonical phase | Current local value / behavior |
| ---: | --- | --- |
| 0/5/6/10 | Three acceptance conversations and final requirements/material possession check | 0 offers one Yes/No and writes 10 immediately; native observable acceptance states and two choices are removed |
| 20 | Accepted and ready to craft/show first model | Local 10; crafting is allowed without the repeated 36 Crafting check |
| 40 | First model consumed/burned; bring 2 papyrus and potato sack | Local 20; model is shown but never removed and no experiment scene runs |
| 60 | Weighted test destroyed by flash mob; debrief Auguste | Local 40; only two messages represent the scene |
| 70 | Real balloon phase; sapling/apples and material hand-ins | Local 60; sapling/apple offer, Farming, replacement, and tree/branch lifecycle are absent |
| 80 | Dye/sandbags/silk/bowl complete; build basket | Local 70; aggregate advances despite no sapling path and `%zep_alldye` is unused |
| 90 | Basket complete; load logs and operate maiden flight | Local 80; ten logs lead to three text boxes and an unconditional teleport |
| 100 | Landed at Taverley; final talk/reward retry | Local 90; any visible Auguste wrapper can complete, including Entrana |
| 200 | Complete; Taverley Stan active; only base route unlocked | 200 is written before reward safety and all four expansion route bits are granted for free |

This is a cache-facing defect, not merely different constant names. The same
fields drive the Entrana basket/storage and Taverley Auguste/Stan transforms,
Quest Helper state, route availability, Cold War dialogue, diaries, and MM2.
The constant file's assertions that 0/5/6/10 are safely collapsible, 100 is a
UI-only state, and all routes may be granted at completion must be removed.

### Required one-time migration

Migrate saves before any modern handlers observe the old meanings:

| Old local state | Native target | Reconciliation |
| ---: | ---: | --- |
| 0 | 0 | No change |
| 10 | 20 | Preserve carried ingredients/frame; player may reacquire anything absent |
| 20 | 40 | Retire any residual first-model quest copy safely; do not let it replay the experiment |
| 40 | 60 | Preserve pending debrief |
| 60 | 70 | Preserve all material counters; offer the missing sapling/apples/replacement lifecycle on next Auguste interaction |
| 70 | 80 | Preserve completed dye/sandbag/silk/bowl fields and expose basket construction |
| 80 | 90 | Preserve built basket and loaded-log state only if an actual flight session exists |
| 90 | 100 | Preserve Taverley Auguste transform and require the final talk specifically at the Taverley actor/location |
| 200 | 200 | Never downgrade or replay XP/QP/rewards |

The sandbag overwrite bug has destroyed information: handing in, for example,
four bags twice consumes eight but records four. Migration can preserve the
encoded count but cannot infer prior batches. The repaired hand-in must add
`min(carried, 8-current)` atomically; affected saves need either explicit
operator evidence or to supply the recorded remainder. Do not guess from
inventory absence.

Legacy state-200 saves currently have all four expansion bits set without
completing route puzzles. Preserve already-set durable route bits to avoid
revoking access, but do not retroactively grant four 2,000-XP awards. New and
unfinished saves must earn routes canonically. Treat route bits as access
facts only—never infer quest completion from them—and never let MM2 or a diary
write an unlock bit.

## 5. Detailed lifecycle audit

### Entrana access, start, and model experiments

The Port Sarim monk warns that weapons and armour are forbidden, narrates a
search, and then teleports every player regardless of equipment. This breaks
the quest's initial access contract and later balloon arrivals to Entrana.
Implement one shared `~has_entrana_restricted_items` policy over inventory and
equipment categories, with the documented exceptions, and call it from both
boat directions and every balloon destination selection before charging a log
or starting flight. A rejection must not consume resources or mutate route
state.

Auguste's start is three conversations with choices “Yes! Sign me up.”,
“Umm, yes. What's your point?”, and “Yes.” The final advance requires the
start requirements and possession of three papyrus, a ball of wool, a full
potato sack, and an unlit candle. Current dialogue performs one gate and one
choice. Restore native 5/6/10 substates, refusal/re-offer branches, exact
requirement explanations, and the final material-presence check without
consuming those materials prematurely.

At state 20, papyrus plus wool creates a balloon structure; an unlit ordinary
or black candle completes it. Completing the model rechecks boostable 36
Crafting and awards 35 Crafting XP. Current proc has no skill check or XP and
blocks the action whenever any structure/balloon is held, which also prevents
the advertised repeatable post-quest product. Use a transactional recipe:
validate stage, effective skill, inputs, and output capacity; animate/delay;
consume one of each exact input; add one output; award XP once. Support both
use-on directions through the existing shared dispatchers. Auguste accepts
only a plain model for the first experiment; a dyed one must not advance state.

Showing the first model must consume/burn it in the authored experiment and
advance 20→40. The second talk consumes exactly two papyrus and the full
potato sack, then runs the flash-mob scene with the placed actors and jingle
before 40→60. Current first model remains in inventory and both scenes are
messages. Cutscenes need player-scoped actors/locs or an instance, protected
queues, cancellation/relogin settlement, and no public NPC leakage. The
debrief is a separate 60→70 talk, not part of the experiment transaction.

### Sapling, Farming, material hand-ins, and basket

After the mob debrief, Auguste can give his special willow sapling and a basket
of five apples, requiring two free slots. The sapling is a convenience rather
than a mandatory unique source: twelve tradable branches from any player-grown
willow are valid. If the special sapling is lost or dies before the quest is
complete, Auguste sells a replacement for 30,000 coins after checking
inventory and bank ownership and reserving a free slot. Current code gives
neither item and never references cache item `zep_plantpot_willow_sapling` or
the native sapling fields.

Register the special sapling with shared Farming as a willow sapling: level 30
at planting, 25 Farming XP, six 40-minute growth cycles, one basket of five
apples protection, disease/death/cure behavior, 1,456.5 health-check XP, normal
tree cutting/stump lifecycle, and willow-branch cutting/regrowth. At most six
branches are available initially; one regrows about every five minutes, so all
twelve require about one hour after checking health. Ordinary willow trees and
traded branches must remain valid. Keep growth state with the
authoritative Farming patch, using `%zep_sapling_check/%zep_sapling_patch`
only for quest recovery/ownership semantics established from the cache; do not
create a second timer or quest-private tree.

The real-balloon hand-ins are red dye, yellow dye, eight sandbags, ten silk,
and one empty bowl. They may be delivered incrementally and must never delete
more than the outstanding amount. Current dye/bowl paths are broadly usable,
silk insists on all ten at once, and sandbags overwrite rather than add the
prior count. Normalize all five through one atomic `outstanding/current`
helper, update `%zep_alldye` consistently if the native presentation needs it,
and compute the 70→80 aggregate after every successful delivery. Re-talk and
journal text should name only outstanding items.

Filling an empty sack at a sandpit consumes one sack and creates one sandbag;
emptying a sandbag should return the empty sack. Test full inventory, since the
one-for-one replacement should work without a spare slot. Once materials are
complete, using twelve willow branches on the Entrana frame should consume
exactly twelve, play the construction scene, set the canonical basket visual,
and remain idempotent. Current one-click transaction lacks the scene but is
otherwise close.

### Maiden flight and failure/recovery

Starting the maiden flight requires state 90, ten normal logs, a tinderbox,
effective Firemaking 20, total carried weight no greater than 40 kg, and any
follower placed in the inventory. Validate all of these before consuming the
ten logs. Current dialogue mentions weight but does not check weight, skill,
or pet and deletes logs before an unconditional success path.

Mount the cache-authored flight board and control side panel using the modern
named-subinterface pattern. Initialize the native session fields and arm every
server-handled op after each mount. Every action advances the balloon one
column to the right while changing height:

| Control | Height delta | Resource |
| --- | ---: | --- |
| Drop sandbag | +2 | One of the initial sandbags |
| Burn log | +1 | One of the initial logs |
| Relax | 0 | None |
| Tug brown rope | -1 | None |
| Emergency tug red rope | -2 | None |
| Bail | Failure settlement | No additional resource |

The first route spans three cache maps. Server state must own screen, column,
height, remaining sandbags/logs, collision, off-screen transition, landing,
and modal lifetime; clientscript only renders. A move into any obstacle or an
invalid final height fails. A correct final path lands at Taverley and writes
100. Quest Helper's route arrays are useful expected-path fixtures, but the
server should model collision rather than hardcode one accepted button list.

Failure is real gameplay and must be recoverable:

- bailing during the first screen returns safely to Entrana;
- failing/bailing over the ocean runs the crash scene, uses the crash-site
  Auguste/sharks/plank assets, and lets the player swim ashore;
- failing on the final screen runs the later crash/leave-site path near eastern
  Falador;
- every failure leaves the constructed balloon intact at state 90 and requires
  a fresh ten normal logs for another attempt.

Current code has no widget, obstacles, resource controls, bail, crash actor,
sharks, plank, failure destinations, retry policy, or reconnect behavior. On
logout, death, region change, or modal close, settle to the correct safe
failure state; never preserve a half-mounted interface that can duplicate a
landing or charge.

### Completion, rewards, and post-quest items

After a successful landing, only Taverley's Auguste at 2938,3422 may complete
the quest. Current generic fallback lets the permanently spawned Entrana
Auguste complete local state 90 as well. State 100 must preserve the landed
actor/balloon visual until the final conversation and must not expose ordinary
network travel yet.

The transcript requires two free inventory spaces for jacket and cap. Current
completion writes 200 first and then adds each item only if a slot happens to
be free, so zero or one free slot permanently loses one or both rewards. It
also has no idempotence guard. Settlement must:

1. validate state 100, Taverley actor/location, and two free slots;
2. pause with exact retry dialogue if capacity is insufficient;
3. reserve/add both items and award each XP/QP exactly once;
4. write 200 while keeping Taverley's Auguste present, preserve only the base
   Entrana–Taverley route, and show the shared completion scroll;
5. on the next Talk, run the canonical enterprise/origami explanation and
   searched return-to-Entrana choice, then settle the Assistant Stan transform.

Repeated Talk, duplicate button packets, logout at the scroll, or a direct
proc call must not duplicate rewards. Do not set the four expansion route bits
in this transaction.

After completion, Auguste on Entrana replaces a missing bomber jacket or cap
after inventory/bank/equipment checks and sufficient free space. He also
combines a bomber cap with gnome goggles into `zep_bomber_cap_goggles` and
supports the native Split lifecycle without losing either component. No local
handler provides replacement or combination; only the combined item's cache
definition and Wintertodt warm-item classification exist.

Repeatable origami balloons are available once the quest reaches the model
phase and remain available after completion. The same 36 boostable Crafting,
35 XP recipe supports plain, yellow, blue, red, orange, green, purple, pink,
and black output according to the applied dye. A tinderbox launches a finished
balloon, awards 20 Firemaking XP, and plays the matching setoff/projectile/fire
sequence. Revision 239 contains all item and animation variants. Current code
supports only one plain balloon, no dyes, no launch, and no XP.

### Balloon network and log storage

Quest completion unlocks only Entrana↔Taverley. Each additional route is
earned by selecting it from Entrana, passing its effective Firemaking/weight/
pet/Entrana-item checks, paying the initial logs, and completing its own
three-screen puzzle:

| Route | Native bit | Unlock requirement and initial fuel | Unlock reward | Later one-way cost |
| --- | --- | --- | ---: | --- |
| Entrana / Taverley | `%zep_multi_piccard` / quest completion | 20 Firemaking; 10 normal logs during quest | Quest reward | 1 normal log |
| Crafting Guild | `%zep_multi_craft` | 30 Firemaking; 10 oak logs | 2,000 Firemaking XP | 1 oak log |
| Varrock | `%zep_multi_varr` | 40 Firemaking; 10 willow logs | 2,000 Firemaking XP | 1 willow log |
| Castle Wars | `%zep_multi_cast` | 50 Firemaking; 10 yew logs | 2,000 Firemaking XP | 1 yew log |
| Grand Tree | `%zep_multi_gno` | 60 Firemaking; 3 magic logs | 2,000 Firemaking XP | 1 magic log |

All route Firemaking levels are boostable. The unlock puzzle is only launched
from Entrana; after success the native route bit/assistant/balloon/storage
transform changes and the 2,000 XP award settles once. A failed attempt
consumes the initial load and leaves the route locked. Subsequent trips use the
route map without a puzzle, still enforce the 40 kg/pet/Entrana restrictions,
and atomically consume one matching log. Current menus show all destinations
from every site even when their bits are locked, charge nothing, award no
unlock XP, and teleport to pilot coordinates rather than verified arrival
tiles.

The `zep_storage` object is shared across all six locations. From any storage
currently exposed, it accepts noted or unnoted normal/oak/willow/yew/magic
logs up to 100 of each, does not permit withdrawal, and accepts a log type
before its route or Firemaking level is unlocked. It offers a preference for
taking travel payment from inventory or storage. Check reports all balances.
Store must support partial capacity and
never consume overflow. Travel must select one source according to preference,
verify it, and debit exactly once only after all other gates pass. The current
repository has loc options but no handlers or backing state; this schema and
its migration/versioning must be designed as shared network state, not quest
construction state.

Use the native route-map interface rather than dialogue lists. Its destination
buttons should render locked/unlocked/current state, explain a missing level,
route, log, weight, pet, or Entrana restriction without charge, and re-arm on
every mount. Arrival and departure should publish typed balloon travel events
only after successful settlement.

### Downstream consumers and oversights

Monkey Madness II canonically requires both state 200 and the earned Grand
Tree route. Its current Auguste trigger instead prints “Soft-skip”, writes
`%zep_multi_gno=1`, and advances MM2. Replace that side effect with an
explicit prerequisite check before MM2 can enter the relevant stage; the EJ
route bit is read-only to MM2. The Grand Tree assistant's separate MM2
visibility field must not be confused with the route-unlock bit.

The Ardougne medium task is successful arrival at Castle Wars by balloon. The
initial ten-yew unlock flight does not count, so a previously locked player
must unlock, depart, and return (eleven yew logs plus the normal log for the
return path). Fire the diary event only for an ordinary settled arrival, not
the unlock puzzle, map selection, debug teleport, or failed flight.

The Varrock medium task is use of the balloon from Varrock. Current live OSRS
has an odd edge case where selecting a locked destination without travelling
can count after the Varrock route itself is unlocked; treat that as an
explicit compatibility test/decision, not an accidental generic button hook.
The normal implementation event should be a validated Varrock departure after
payment. No local diary event exists for either task.

Cold War's KGP report contains conditional dialogue about the player's hot-air
balloon flight when `%zep_quest >= 200`, alongside its Between a Rock branch.
The local Cold War simplification omits it. This is a read-only narrative
consumer and must never mutate EJ. Repository searches find no other legitimate
quest owner of `%zep_quest` or route bits besides EJ; shared consumers should
use named predicates rather than numeric copies.

## 6. Migration and modernization sequence

### Gate A — canonical state and transactions first

1. Replace local primary constants with native 0/5/6/10/20/40/60/70/80/90/
   100/200 semantics and correct every stale rationale comment.
2. Add the one-time old-save mapping above before enabling new reads; preserve
   native construction, route, pilot, basket, and sapling fields.
3. Split Auguste dialogue by actor/location and state, restoring start choices,
   material gates, experiments, sapling/recovery, landed state, and post-quest
   services.
4. Centralize atomic hand-in/reward helpers and make the journal derive exact
   missing items from native counters.

### Gate B — shared access, Farming, and cache-native UI

1. Implement the shared Entrana restricted-item predicate and apply it to boat
   and balloon arrivals without precharging.
2. Register Auguste's sapling in shared Farming and implement ordinary willow
   branch cutting/regrowth, gardener protection, disease/death, and replacement.
3. Mount `zep_interface`/`zep_interface_side` and `zep_balloon_map` with named
   modern slots, re-armed server ops, and native session fields.
4. Build server-owned route maps/collision/landing and cancellation-safe flight
   sessions over the cache grid; no quest-specific C shortcut is indicated.

### Gate C — narrative, failures, crafting, and recovery

1. Implement both experiment scenes, flash-mob actors/jingle, real-balloon
   construction scene, three maiden-flight outcomes, crash actors, plank/swim,
   and retry paths.
2. Correct model skill checks and XP; add all colors and launch/fire behavior.
3. Make completion two-slot-safe and idempotent; add bomber replacement and
   cap/goggles combine/split service.
4. Test loss, banking, full inventory, duplicate items, repeated packets,
   death, logout, reconnect, region change, and modal close at every consuming
   boundary.

### Gate D — network and consumers

1. Implement four independent route unlock puzzles, exact boostable levels,
   initial fuel, one-time XP, later costs, and arrival tiles.
2. Add shared five-type log storage, noted deposits, capacity, source
   preference, nonwithdrawal, and atomic charging at all six locs.
3. Remove MM2's route-granting soft skip and enforce both prerequisites without
   side effects; preserve its assistant visibility separately.
4. Publish and consume exact Ardougne/Varrock diary events and restore Cold
   War's conditional report dialogue.
5. Rewrite debug commands as setup/assertion adapters and add fresh-save,
   migrated-save, and consumer integration tests.

## 7. Verification matrix

| Area | Required checks |
| --- | --- |
| Entrana access | Restricted inventory/equipment and every exception; boat and balloon; rejection before cost/state change; return trip |
| Start | States 0/5/6/10; every refusal/re-offer; 20 QP; all three boosted requirements; expired boost; final ingredient possession; journal colors |
| First model | Both use-on directions; plain/black candle; 36 boost recheck; exact consumption; full inventory; 35 XP once; loss/rebuild; first model consumed by scene |
| Experiments | First and weighted scenes; exact 2 papyrus/potato transaction; flash-mob actors/jingle; cancellation/relog; debrief only after scene settlement |
| Sapling/Farming | Two free slots; inventory/bank ownership; 30,000 replacement; ordinary willow alternative; level/boost; plant/protect/disease/cure/death/growth/health XP; branch capacity/regrowth/trading |
| Hand-ins | Every order and batch size; sandbags 1+7/4+4/8; silk partial; dyes independently; exact outstanding deletes; aggregate; journal outstanding list |
| Basket | Exactly 12 branches; full inventory; animation/scene; transform; repeated use; logout boundaries |
| Maiden-flight gates | 20 Firemaking recheck; 10 normal logs; tinderbox retained; 40.0 kg boundary; follower inventory; no premature deletion |
| Maiden flight | All five vertical actions, counters, three maps, collisions, valid landing; bail on each screen; ocean/final crashes; plank/swim/exit; retry with new logs; disconnect/death/interface close |
| Completion | Only Taverley Auguste at state 100; 0/1/2 free slots; exact two items/four XP/1 QP once; repeated Talk/proc/relog; enterprise/travel follow-up before Stan transform; no expansion bits granted |
| Reward lifecycle | Lost/banked/equipped jacket/cap; one or both missing; capacity; cap+goggles combine; Split; Wintertodt classification |
| Origami balloons | Plain plus eight dyes; repeat quantities; 36 Crafting/35 XP each; invalid dyes; tinderbox launch/animation/20 Firemaking XP; no quest-state replay |
| Route unlocks | Each route locked/level/log/weight/pet gate; all solution and collision branches; failure stays locked; success bit/2,000 XP once; attempts from non-Entrana rejected |
| Ordinary network | Every directed pair; destination lock visibility; exact one-log type/source; verified arrival tile; <=40 kg; pet; Entrana restriction; no puzzle after unlock |
| Log storage | Six equivalent locs; noted/unnoted; all five types; 0/99/100/overflow; nonwithdrawal; Check; inventory/storage preference; missing preferred source; concurrent/repeated click charging |
| Diaries | Unlock flight does not count Ardougne; ordinary Castle Wars arrival counts once; Varrock departure compatibility decision; failure/selection/debug teleports do not count |
| Downstream | MM2 rejects 199, rejects route bit 0, accepts 200+Grand Tree bit, never grants bit; Cold War conditional dialogue at 199/200; Grand Tree assistant visibility |
| Migration | Every old primary value; partial material counters; residual model; state-90 landed location; state-200 reward ownership; legacy route-bit preservation; no XP/QP replay |

Required static evidence includes a clean RuneScript/config build, duplicate
trigger and unresolved-symbol scans, modern-interface ownership review, no
unexpected numeric IDs, and `python3 tools/questhelper_extract.py
enlightenedjourney --check`. Required runtime evidence is a command-free fresh
0→200 playthrough with at least one failed maiden flight, every crash/recovery
branch, all four route unlocks, all six storage sites, a migrated-save matrix,
and the diary/MM2/Cold War consumer tests. A debug state write, narrated
teleport, compile result, or completion scroll alone is not route proof.

## 8. Definition of done

Enlightened Journey is modernized only when a fresh eligible player can pass
the real Entrana search, complete all native acceptance states, craft and burn
both test balloons, receive/grow/replace the optional special willow sapling,
deliver materials incrementally without loss, construct the basket, operate
the cache-authored three-screen flight with real failure/recovery, land and
receive both rewards exactly once, and use/recover every advertised post-quest
item. The base route, four separately earned routes, exact log costs, weight
and Entrana restrictions, shared log storage, colored origami lifecycle,
diaries, MM2 prerequisite, Cold War dialogue, native transforms, journal, and
old-save migration must remain correct under full inventory, loss, banking,
death, logout, reconnect, repeated interaction, and duplicate packets.
