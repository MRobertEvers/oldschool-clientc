# Imp Catcher modernization audit

Status: `audit-pending` — the cache-native quest row and varp, Wizard Mizgog
and Wizard Grayzag transformations, reachable Wizards' Tower route, all four
beads, exact 5/128 bead rolls, ordinary pre-quest bead acquisition, the basic
offer/decline/re-talk route, correct numerical rewards, repeat amulet exchange,
journal dispatch, quest cheat, and POH status adapter are present. Ordinary
gameplay can complete the quest. It is not yet a current, recoverable modern
implementation: accepting with all beads fails to complete in the same
conversation, the current quest-offer contract and special dialogue are
missing, the four-bead payment is separated from reward settlement by a queued
boundary, the 2006 ending cutscene and its unique jingle are absent, completion
has no exactly-once receipt, and the postquest purchase silently consumes beads
without the current confirmation choice. Shared imp combat also lacks the
current Water weakness and uses the wrong periodic teleport probability.

Audited: 2026-08-17

Governing plan: [Quest modernization plan](../QUEST_MODERNIZATION_PLAN.md). This
record applies that plan's Gates A-D to acceptance, pre-collected and acquired
beads, ordinary imp combat and drops, the four-item hand-in, the ending
cutscene, reward settlement, postquest amulet purchases, Wizard Grayzag,
journals/admin adapters, and every direct consumer found. It is an
implementation specification, not evidence that the quest has been
modernized.

## 1. Authoritative references

The current OSRS Wiki revisions below are pinned so implementation and review
use stable requirements, dialogue, acquisition, combat, presentation, reward,
and integration contracts.

| Reference | Pinned revision | Use in this audit |
| --- | --- | --- |
| [Imp Catcher](https://oldschool.runescape.wiki/w/Imp_Catcher?oldid=15293702) | 15293702, 2026-08-12 | Identity, route, acquisition, immediate-completion branch, rewards, and history |
| [Imp Catcher/Quick guide](https://oldschool.runescape.wiki/w/Imp_Catcher/Quick_guide?oldid=14649872) | 14649872, 2024-05-05 | Ordered actions, exact items, dialogue choices, and optional combat |
| [Transcript:Imp Catcher](https://oldschool.runescape.wiki/w/Transcript%3AImp_Catcher?oldid=15263201) | 15263201, 2026-07-14 | Offer/refusal, pre-collected-bead branch, re-talks, hand-in, cutscene, and finale |
| [Wizard Mizgog](https://oldschool.runescape.wiki/w/Wizard_Mizgog?oldid=15196221) | 15196221, 2026-04-25 | Start NPC, postquest exchange, and hard-clue role |
| [Transcript:Wizard Mizgog](https://oldschool.runescape.wiki/w/Transcript%3AWizard_Mizgog?oldid=15257734) | 15257734, 2026-07-08 | Current postquest choices, exchange confirmation, and Treasure Trails dialogue |
| [Wizard Grayzag](https://oldschool.runescape.wiki/w/Wizard_Grayzag?oldid=15130118) | 15130118, 2026-02-18 | Supporting NPC identity and non-attackable current role |
| [Transcript:Wizard Grayzag](https://oldschool.runescape.wiki/w/Transcript%3AWizard_Grayzag?oldid=15131169) | 15131169, 2026-02-20 | Not-started, started, completed, and anniversary-event dialogue |
| [Imp](https://oldschool.runescape.wiki/w/Imp?oldid=15271036) | 15271036, 2026-07-21 | Standard/GWD stats, teleport, drops, Water weakness, Hunter, and locations |
| [Black bead](https://oldschool.runescape.wiki/w/Black_bead?oldid=15183311) | 15183311, 2026-04-22 | Tradeability, quest hand-in, and magic-box bait use |
| [Red bead](https://oldschool.runescape.wiki/w/Red_bead?oldid=15183461) | 15183461, 2026-04-22 | Tradeability, quest hand-in, and magic-box bait use |
| [White bead](https://oldschool.runescape.wiki/w/White_bead?oldid=15183312) | 15183312, 2026-04-22 | Tradeability, quest hand-in, and magic-box bait use |
| [Yellow bead](https://oldschool.runescape.wiki/w/Yellow_bead?oldid=15183317) | 15183317, 2026-04-22 | Tradeability, quest hand-in, and magic-box bait use |
| [Amulet of accuracy](https://oldschool.runescape.wiki/w/Amulet_of_accuracy?oldid=15240538) | 15240538, 2026-06-27 | Reward, equipment stats, tradeability, and repeat creation |
| [Mizgog's Beads (Imp Catcher)](https://oldschool.runescape.wiki/w/Mizgog%27s_Beads_%28Imp_Catcher%29?oldid=15261726) | 15261726, 2026-07-12 | Ending-cutscene jingle, duration, and cache ID 133 |
| [Wizards' Tower](https://oldschool.runescape.wiki/w/Wizards%27_Tower?oldid=15121852) | 15121852, 2026-02-06 | Start location, floors, and access routes |
| [Magic box](https://oldschool.runescape.wiki/w/Magic_box?oldid=15185581) | 15185581, 2026-04-22 | Bead-bait consumer, Hunter 71, +3% bait, and no-smoking rule |
| [Hard cryptic clues](https://oldschool.runescape.wiki/w/Treasure_Trails/Guide/Cryptic_clues/Hard?oldid=15263120) | 15263120, 2026-07-14 | Wizard Mizgog clue target and shared dialogue ownership |

These sources define a free-to-play, novice, short quest released 16 February
2001 with no skill or quest prerequisites. One unnoted black, red, white, and
yellow bead is required. The beads may be collected before starting and may be
obtained from ordinary imps, the Grand Exchange, or player trade; killing imps
is optional except for accounts that cannot trade. Mizgog accepts only the full
four-colour set, never partial hand-ins.

The current contract includes:

- `Start the Imp Catcher quest?` followed by `Yes.` or `No.` after the polite
  request branch;
- a special dialogue and immediate hand-in/completion when all four beads are
  already in inventory at acceptance;
- no quest-specific enemy or kill credit: every bead remains an ordinary,
  tradeable item and every eligible source is valid;
- each colour on the standard imp table at exactly 5/128, independently of
  quest state;
- an ending cutscene in which Mizgog places the beads on his table and casts a
  spell, accompanied by the 18-second cache-ID-133 `Mizgog's Beads` jingle;
- one quest point, 875 Magic XP, and one amulet of accuracy; and
- a repeatable postquest exchange of one full bead set for one amulet, with an
  explicit `I have them with me!` / `Maybe later.` choice before consumption.

Transition aid only: Quest Helper's
[`ImpCatcher.java`](https://github.com/Zoinkwiz/quest-helper/blob/5ea99d5ea9ba3fb096ebe7b5ed02d80883e9819d/src/main/java/com/questhelper/helpers/quests/impcatcher/ImpCatcher.java)
at commit `5ea99d5ea9ba3fb096ebe7b5ed02d80883e9819d` treats states 0 and
1 as the same acquisition/turn-in route, recognizes every bead, both tower
floors and Mizgog, lists optional level-2 imp combat, and confirms all three
rewards. It explicitly expects the current `Give me a quest please.` and
`Yes.` choices, allowing one-conversation completion with pre-collected beads.
The file last changed in
`df6b74e41c9ae7f57a3b9c6451422db75b4c8313` on 2025-08-09.
Running `python3 tools/questhelper_extract.py impcatcher --check` resolves the
quest row, five items, Mizgog, both staircase locs, and all seven world points.
Quest Helper cannot prove server transactions, cutscene playback, drop
ownership, reward idempotency, or shared-NPC dispatch.

The local LostCity source was inspected only to identify the old lineage of
the port. Its start writes state 1 without a modern Yes/No boundary, its
completion is the same queued four-delete shape, and it predates the current
postquest exchange. It is not an OSRS authority and must not override the
pinned current sources or osrs239 cache.

## 2. Native quest identity and state contract

| Field | Native value / expected behavior |
| --- | --- |
| Cache row | `quest_impcatcher`; quest metadata ID 9 |
| Implementation root | `server/scripts/quests/quest_imp` |
| Type / difficulty / length | Free-to-play quest / novice / short |
| Release | 16 February 2001 |
| Start | `wizard_mizgog` at 3103,3164, plane 2 |
| Primary state | Native permanent/transmitted varp `%imp`, varp 160 |
| State 0 | Not started; pre-collected beads remain ordinary items |
| State 1 | Accepted; collect or return the complete set |
| State 2 | Complete; Mizgog and his table transform to postquest forms |
| Requirements | No skills, quests, combat, or membership |
| End / quest points | State 2 / 1 QP |
| Direct XP | Magic 8,750 tenths (875 XP) |
| Item reward | One `amulet_of_accuracy` |

The dbrow correctly records free-to-play status, novice difficulty, short
length, Misthalin location, release, start coordinate/NPC, state 2, one quest
point, 875 Magic XP, no prerequisites, and early-game onboarding metadata.
These values should remain data-driven.

The native `%imp` carrier is also the cache transformation source:

| Subject | State 0 | State 1 | State 2 | Audit result |
| --- | --- | --- | --- | --- |
| `wizard_mizgog` shell | `wizard_mizgog_quest` | `wizard_mizgog_quest` | `wizard_mizgog_post` | Correct native presentation; post form exposes `Purchase Amulet` as operation 3, but no handler exists |
| `qip_imp_catcher_magic_table` | table before | table before | table with beads | Correct persistent world presentation; ending animation never uses the authored table/sequence assets |

Only values 0-2 are authored. Capture `%imp`, all four inventory bead counts,
amulet ownership domains, Magic XP, QP/count settlement, current dialogue,
table transformation, and cutscene/audio state at acceptance and completion.
Exercise full inventory, disconnect, logout, restart, duplicate bead sets,
concurrent players, immediate completion, and repeat purchases.

### Required migration rules

| Existing shape | Risk | Migration rule |
| --- | --- | --- |
| State 0 with some/all beads | Normal pre-collection | Preserve every bead; offer the current quest and immediately complete after Yes when the full set is carried |
| State 1 with no/some beads | Normal progress | Preserve state and ordinary inventory; do not invent missing beads or require kills |
| State 1 with all beads | Ready to finish | Perform one atomic full-set exchange through Mizgog |
| State 1 missing beads after an interrupted current hand-in | Current delete-before-queue failure window | Repair only from transaction evidence; state alone cannot prove which beads were paid |
| State 2 with beads still in inventory | Extra or newly acquired tradeable items | Never delete them during migration; completion consumes exactly one evidenced set |
| State 2 without amulet | Legitimate loss/trade or interrupted settlement | Do not grant it merely from state; use settlement evidence for first-reward repair and the normal repeat exchange otherwise |
| State 2 missing XP or QP | Possible current partial settlement/admin fixture | Repair only independently evidenced missing components; no blind replay |
| State >2 | Invalid/stale value | Quarantine with telemetry; do not treat it as an authored hidden state |

Admin fixtures must distinguish “set dialogue state”, “ready with a bead set”,
“interrupt completion after payment”, and “fully settle completion”. A
state-only fixture is not a legitimate bead payment or reward receipt.

## 3. Implementation surface

The direct quest root contains four files and 96 lines in the current
OSRS-Content tree. Most player-visible behavior is owned by shared area, NPC,
drop, Hunter, quest UI, and cache configuration outside that root.

| Path / subsystem | Present responsibility | Audit result |
| --- | --- | --- |
| `quest_imp.constant` | States 0-2 and one QP constant | State constants are correct; QP constant is unused because the dbrow is authoritative |
| `quest_imp.varp` | Registers native `%imp` | Correct permanent/transmitted carrier; comment documents rev254-to-osrs239 mapping |
| `imp_journal.rs2` | Three-state journal and bead checklist | Functional, but tells players to kill imps and does not acknowledge trade/GE sourcing or settlement recovery |
| `quest_imp.rs2` | Deferred completion queue | Correct numerical rewards; unsafe ordering, no receipt, and no cutscene |
| Wizard Tower `wizard_mizgog.rs2` | Offer, re-talks, four-bead delete, postquest exchange | Old dialogue shape; no immediate completion, current confirmation, op3 handler, atomic transaction, or exact finale |
| Wizard Tower `wizard_grayzag.rs2` | State dialogue plus legacy combat AI | State branches exist but are abridged; stale attack/imp-summon machinery remains for a non-attackable current NPC |
| Lumbridge `imp.rs2` | Standard imp periodic teleport | Timing range exists; probability, effects, combat recency, and current combat overlay differ |
| Standard imp drop table | Main drop roll | Each bead is correctly 5/128 and fiendish ashes come from the native death-drop param; standard tertiary drops are absent |
| GWD imp drop table | GWD/Wilderness variants | Bead rolls plus some contextual tertiaries exist under a separate owner |
| World spawns | Ordinary imps and tower NPCs | 83 standard imp spawns across 25 authored spawn files; Mizgog and Grayzag are at the tower top |
| Native NPC/loc transforms | Mizgog and table pre/post forms | Correct and player-local through `%imp`; cutscene sequences/synth assets are unused |
| Shared stair/maplinks | Wizards' Tower floors | Both Quest Helper staircase locs resolve and route infrastructure exists |
| Shared completion helper | QP/count, scroll, generic difficulty jingle | Non-idempotent additive award; no quest-specific settlement receipt; does not play the ending-cutscene jingle |
| Quest list / POH status | Journal and house display | Both correctly dispatch/read `%imp` |
| Quest cheat | State-only completion fixture | Sets state 2 and common cheat awards QP/count; intentionally omits XP, beads, amulet, and presentation |
| Combat manifest | Optional level-2 imps | Correct high-level statement, but source/gameval/handler/loot/test fields are empty |
| Content port queue | Historical “done” claim | Describes presence of Mizgog/journal/drops, not current fidelity or safe settlement |

The cache already contains `qip_imp_catcher_wizard`, spell, laugh, chicken and
scratch sequences; the before/after magic-table locs; `mizgog_beads` and
`mizgog_placebeads` synths; all item/NPC forms; and the special jingle mapping.
The missing finale is an unwired presentation/state transaction, not a missing
asset problem.

## 4. Route, dialogue, and reachability audit

| Segment | Current implementation | Required behavior / oversight |
| --- | --- | --- |
| Tower route | Mizgog spawn and shared stairs are present | Retain route; add a start-to-top-floor smoke test for both stair choices |
| Initial menu | `Give me a quest!` or old “quiet friends” branch | Broadly valid, but reconcile against current transcript and clue/event ownership |
| Polite request | Uses old Grayzag summoning wording | Current dialogue says Grayzag “enlisted an army”; port exact current branch |
| Acceptance | Choice labels are `I'll try.` / long refusal | Use the current `Start the Imp Catcher quest?` Yes/No commit boundary; this also restores Quest Helper choice matching |
| Accept with all beads | Writes state 1 and ends | Play the special surprise/accusation exchange and continue directly into completion in the same conversation |
| Accept without full set | Writes state 1 and confirms | Correct state outcome; preserve refusal as state 0 and never alter beads |
| No/some bead re-talk | Distinguishes none versus some | Broadly correct; preserve exact current lines and keep all partial beads |
| Full-set re-talk | Announces reward, deletes items, queues completion | Use the exact “check they really are MY beads” line and one atomic settlement path |
| Ending | Two messages after scheduling the queue | Run the authored table/spell cutscene locally, play special jingle 133, then show handoff/completion presentation |
| Postquest dialogue | Quests, amulet, and old quiet-friends options | Current options are quests, interesting spells, and amulet; add exact branches and keep clue dialogue higher priority when applicable |
| Postquest NPC op3 | Cache says `Purchase Amulet` | Add a handler that enters the same validated exchange path as dialogue |
| Grayzag | Three short state responses | Restore player name/response/threat and captured anniversary branches; guarantee non-attackability |

There is no route blocker and no quest-private combat instance is appropriate.
Ordinary imps are intentionally shared world actors and beads are ordinary
tradeable loot. Modernization must preserve that openness while making the
Mizgog cutscene player-local: one player's state and table transformation must
not alter another player's view or lock the shared NPC.

## 5. Bead acquisition and item transaction contracts

### Ordinary acquisition

The standard imp table rolls one integer in `[0,128)` and assigns black, red,
white, and yellow to four non-overlapping five-value ranges. This gives each
colour the canonical 5/128 probability and exactly one main-table result per
kill. The quest code does not gate drops on `%imp` and accepts pre-collected or
traded beads, which is correct. No implementation may add a quest-only kill,
guaranteed quest bead, dry-streak mitigation, colour order, or NPC ownership
requirement.

The surrounding ordinary imp contract is incomplete:

- the local overlay has Attack 2 while the pinned current record has Attack 1;
- it has no `elemental_weakness=water` / 10% parameters;
- the timer rolls 50% every random 50-200 ticks, while the current contract is
  a 25% attempt every 30-120 seconds;
- teleport smoke effects are explicitly omitted because the server map
  spot-animation packet was unavailable when ported;
- its combat-recent suppression approximates history with current mode; and
- standard imps omit current contextual tertiaries such as the 1/25 ensouled
  imp head and 1/5000 champion scroll.

Those are shared NPC/drop-owner tasks, but this quest's self-sufficient route
depends on the bead rolls and normal loot ownership remaining correct. Test
the four frequencies, exactly-one main roll, eligible killer ownership, death
drop, teleport during/outside combat, and simultaneous attackers.

### Initial quest hand-in

Current code checks all four inventory items, shows a message, executes four
independent deletes, then schedules `imp_quest_complete` with `queue(..., 0)`.
The queue is deliberately held while dialogue is active; consequently the
player can occupy a state-1/all-beads-paid interval with no durable payment or
settlement receipt. A disconnect, server stop, or failed queued callback at
that boundary has no principled recovery path. The completion queue then
writes state 2 before granting the amulet, XP, and additive QP/count helper.

Replace this with one quest transaction:

1. preflight state 0/1 eligibility and exactly one unnoted bead of each colour;
2. acquire a per-player settlement lock and create an attempt ID;
3. reserve/consume the four-item set exactly once and write a payment receipt;
4. run or resume the player-local cutscene from a durable phase boundary;
5. grant the amulet, Magic XP, QP/count, and completion presentation with
   independent receipts;
6. commit state 2 at the documented point and release the lock; and
7. on reconnect, resume presentation or settle only the missing receipted
   component without restoring/consuming a second set.

Four consumed non-stackable items create enough inventory space for the
amulet, but capacity should still be calculated from post-consumption state
rather than assumed. Extra bead copies remain untouched.

### Repeat amulet exchange

The repeat route currently deletes all four beads and adds the amulet as soon
as the player chooses the broad amulet topic. It omits the current explicit
`I have them with me!` / `Maybe later.` confirmation and ignores the cache's
`Purchase Amulet` operation. A player can therefore lose a set merely by
exploring dialogue, and the four sequential deletes have no transaction ID.

Both Talk-to and Purchase Amulet must call one atomic recipe/exchange service:
require state 2; preflight one inventory bead of each colour; ask for current
confirmation; consume exactly one set; add exactly one amulet; and commit once.
Cancellation, missing colours, disconnect, duplicate clicks, and concurrent
operations must leave the inventory unchanged. The exchange is repeatable and
must not forbid purchase merely because an amulet exists in inventory, bank,
equipment, or Grand Exchange.

## 6. Completion and reward audit

The numerical rewards are correct: state 2, one amulet, 875 Magic XP, and one
quest point through the dbrow-driven completion helper. The amulet cache record
also has the current +4 Stab, Slash, Crush, Magic, and Ranged attack bonuses and
is tradeable, noteable, and wearable.

Correct values do not make the current settlement safe. The completion queue
has no lock or receipt, writes state first, ignores whether item delivery
succeeded, and calls a helper that always increments QP and completed-quest
count. Re-entry cannot distinguish an ordinary state-2 player who traded away
the amulet from one whose first grant failed. Replaying the queue can duplicate
XP, QP, count, and reward.

Use independent durable receipts for:

1. initial four-bead payment;
2. ending-cutscene phase/completion;
3. first amulet delivery;
4. 875 Magic XP;
5. one quest point and completed-quest count; and
6. completion scroll plus required jingles.

The table/NPC transformation may derive from state 2, but state 2 alone cannot
serve as proof that every economic reward was delivered. The repeat exchange
must use its own transaction IDs and must never mutate first-completion
receipts.

## 7. Journal, admin, provenance, and tests

The journal is correctly registered and gives a readable four-colour checklist.
It should stop saying the items must be collected “by killing Imps”; the
authoritative route permits pre-collection, trade, and the Grand Exchange.
Add a ready-to-hand-in entry, a paid/presentation-resume entry if settlement is
multi-phase, and a diagnostic entry for invalid state rather than treating
every non-0/1 value as completed.

The generated POH adapter correctly returns not started, started, and complete.
The global `::complete` arm writes state 2 and the common command adds QP/count
while intentionally omitting beads, amulet, XP, and presentation. Preserve that
as an explicitly state-only prerequisite fixture. Add separate deterministic
fixtures for state 0 with no/partial/full beads, state 1 with every colour
subset, post-payment interruption, cutscene resume, fully settled completion,
and repeat purchase.

`CONTENT_PORT_QUEUE.md` marks the slice done because Mizgog, the journal, row,
and bead drops exist. That historical presence claim must not be used as
modernization evidence. The combat manifest correctly calls imp kills optional
but leaves source audits, gamevals, handlers, loot contract, known gaps, and
tests empty. No Imp Catcher-specific executable tests were found.

## 8. Downstream consumers and shared owners

| Consumer / owner | Current implementation | Required contract / finding |
| --- | --- | --- |
| Standard imp drop table | Supplies all four colours at 5/128 | Preserve quest-independent rolls; add current weakness/teleport/tertiary coverage under the shared owner |
| Player trade / Grand Exchange | Items are cache-tradeable | Quest must accept any legitimate unnoted set without provenance checks |
| Magic box Hunter trap | Accepts any bead and records bait | Correct item family, but it allows setup at Hunter 27 while catch requires 71 and incorrectly permits smoking; current Magic box requires 71 and cannot be smoked |
| Amulet equipment | Cache provides five +4 attack bonuses | Preserve stats and ordinary trade/drop/note/equip behavior |
| Postquest `Purchase Amulet` | Cache operation exists, handler absent | Route op3 and dialogue to the same confirmed atomic four-bead recipe |
| Hard cryptic clue | Cache clue target row names Mizgog | No server clue handler was found; future clue dispatch must take priority without bypassing or corrupting quest dialogue |
| Wizard Grayzag | Reads `%imp` for dialogue; legacy combat overlay/AI remains | Keep state dialogue, remove or prove unreachable stale combat, and preserve current non-attackability |
| Quest points / onboarding | No direct quest prerequisite reads found | Generic QP totals and early-game recommendation are the only progression consumers found; do not invent a hidden prerequisite |

Magic-box bait is an economic consumer of the same tradeable items. Quest
tests must therefore cover a bead being intentionally consumed as bait before
hand-in, and ensure the journal/turn-in simply reports the missing colour. The
quest must never reserve all beads globally or prevent non-quest uses.

## 9. Modernization order

### P0 — make payment and rewards recoverable

1. Define an atomic four-bead transaction and durable settlement receipt.
2. Collapse state-0-with-full-set and state-1 hand-in into the same resumable
   completion path, preserving the special acceptance dialogue.
3. Make amulet, XP, QP/count, state, and presentation exactly-once across
   disconnect, restart, duplicate click, and replay.
4. Give the repeat amulet exchange its own confirmation and atomic recipe,
   shared by Talk-to and Purchase Amulet.

### P1 — restore current route and shared behavior

1. Port the current offer prompt, special full-set branch, Mizgog re-talks,
   postquest spell option, exchange choices, and Grayzag dialogue.
2. Wire the native table/sequence/synth assets into the 2006 ending cutscene
   and play jingle 133 at the captured phase.
3. Correct standard imp Attack, Water weakness, teleport probability/effects,
   combat-recency semantics, and current contextual drops without making kills
   mandatory.
4. Fix Magic box Hunter-71 setup and no-smoking policy while preserving any
   bead's +3% bait modifier.
5. Add explicit hard-clue/Mizgog operation dispatch and overlap tests.
6. Update journal, admin fixtures, combat manifest, and port-status provenance.

### P2 — hardening and maintenance

1. Add property tests for all 16 carried-colour subsets, extra copies, and
   arbitrary source provenance.
2. Add fault injection at every payment/cutscene/reward receipt boundary.
3. Add multiplayer tests for shared Mizgog, player-local transforms/cutscene,
   ordinary imp loot ownership, and simultaneous repeat exchanges.
4. Add telemetry for state-1 paid/no-receipt histories, invalid states,
   settlement repair, and duplicate transaction IDs.

## 10. Verification matrix

| Area | Required automated evidence |
| --- | --- |
| Offer | Every dialogue option; explicit Yes/No; refusal leaves state 0; Quest Helper choice match |
| Pre-collected route | All four at state 0 completes in one conversation with special dialogue; partial subsets only start |
| Acquisition | Trade/GE/pre-start acceptance; every bead 5/128; no state gate; normal killer loot ownership; extra copies retained |
| Imp behavior | Current stats, demon tag, 10% Water weakness, 25%/30-120-second teleport, smoke, combat suppression, respawn, contextual drops |
| Hand-in | All 16 colour subsets; only full set accepted; exactly one of each consumed; full inventory; duplicate click; disconnect/restart |
| Cutscene | Player-local table/actor sequences, synths, jingle 133, interruption/resume, second player unaffected |
| Settlement | State, amulet, 875 XP, 1 QP/count, scroll/jingles exactly once at every injected failure point |
| Repeat exchange | Talk-to and op3 parity; confirmation/cancel; every missing-colour case; multiple sets; full inventory; replay safety |
| Shared NPCs | Grayzag three quest states/events/non-attackability; Mizgog hard-clue versus quest/postquest priority |
| Consumers/UI | Journal subsets/recovery; POH status; state-only and fully settled fixtures; Magic box bait and no-smoking behavior |

Minimum manual smoke should complete once from no beads and once with a full
pre-collected set; source each colour through ordinary combat and player trade;
disconnect after payment and during the cutscene; run two players at Mizgog
simultaneously; trade away the first amulet; buy two replacements; bait a magic
box with a needed bead; and verify journal, table, Grayzag, hard-clue dispatch,
XP, QP, and completed count after every path.

## 11. Definition of done

Imp Catcher may move from `audit-pending` to `modernized` only when:

- both no-bead and pre-collected-bead routes complete through the current
  dialogue without a second conversation, debug command, or forced imp kill;
- refusal, every partial set, and all ordinary bead sources preserve exact
  state and item ownership;
- initial hand-in and repeat purchase are confirmed where required, atomic,
  duplicate-safe, and recoverable across disconnect/restart;
- the ending cutscene uses the native table/sequence/synth assets and special
  jingle while remaining local to the completing player;
- state 2, one initial amulet, 875 Magic XP, one QP/count, and completion
  presentation are independently receipted and exactly-once;
- standard imp bead rates and current combat/teleport behavior pass executable
  tests while combat remains optional;
- Mizgog Talk-to/Purchase Amulet/clue and Grayzag quest/event operations pass
  an explicit shared-owner matrix;
- journal, POH status, admin fixtures, combat manifest, and provenance agree
  with the authoritative state model; and
- route, transaction, fault-injection, item-source, consumer, and multiplayer
  tests pass.
