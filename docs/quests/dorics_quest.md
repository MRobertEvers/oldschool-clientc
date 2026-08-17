# Doric's Quest modernization audit

Status: `audit-pending` — the native three-state route, dynamic journal,
completion scroll, Mining XP, coin reward, and anvil unlock are recognisable and
normally reachable with pre-obtained materials. The current implementation
omits the bronze pickaxe, current dialogue and immediate-ready route, exposes a
non-atomic completion window, and has no quest-specific regression coverage. It
is not `verified-modern`.

Audited: 2026-08-17

Governing plan: [Quest modernization plan](../QUEST_MODERNIZATION_PLAN.md). This
record applies that plan's Gates A–D to the native quest, Doric's actor, both
start affordances, standard ore acquisition, the completion transaction, the
anvil and whetstone, downstream Devious Minds and Falador Diary behavior,
journal, and debug adapter. It is an audit and implementation specification,
not evidence that the quest is complete.

## 1. Authoritative references

Revisions were resolved through the OSRS Wiki API on 2026-08-17. The Wiki
article and quick guide define mechanics, requirements, route, and rewards; the
transcript defines choices, ready/not-ready branches, directions, item hand-in,
and interaction text. Cache metadata remains authoritative for identity,
storage, symbolic gamevals, and client-facing end state.

| Reference | Pinned revision | Use in this audit |
| --- | --- | --- |
| [Doric's Quest](https://oldschool.runescape.wiki/w/Doric%27s_Quest?oldid=15240932) | 15240932, 2026-06-27 | Identity, requirements, route, rewards, and downstream requirements |
| [Doric's Quest/Quick guide](https://oldschool.runescape.wiki/w/Doric%27s_Quest/Quick_guide?oldid=14457895) | 14457895, 2023-08-26 | Minimal route, unnoted quantities, mine guidance, and conditional danger |
| [Transcript:Doric's Quest](https://oldschool.runescape.wiki/w/Transcript%3ADoric%27s_Quest?oldid=15263205) | 15263205, 2026-07-14 | Anvil/whetstone openings, accept/refuse, directions, ready-now branch, and hand-in dialogue |
| [Doric](https://oldschool.runescape.wiki/w/Doric?oldid=14889085) | 14889085, 2025-04-22 | Actor identity and other uses |
| [Doric's hut](https://oldschool.runescape.wiki/w/Doric%27s_hut?oldid=14925340) | 14925340, 2025-06-25 | Anvil/whetstone location and unlock context |
| [Bronze pickaxe](https://oldschool.runescape.wiki/w/Bronze_pickaxe?oldid=15182812) | 15182812, 2026-04-22 | Acceptance grant and ordinary item behavior |
| [Clay](https://oldschool.runescape.wiki/w/Clay?oldid=15183474) | 15183474, 2026-04-22 | Required unnoted material; excludes soft clay |
| [Copper ore](https://oldschool.runescape.wiki/w/Copper_ore?oldid=15183316) | 15183316, 2026-04-22 | Required unnoted material and acquisition |
| [Iron ore](https://oldschool.runescape.wiki/w/Iron_ore?oldid=15182636) | 15182636, 2026-04-22 | Required unnoted material and level-15 mining route |
| [Dwarven Mine](https://oldschool.runescape.wiki/w/Dwarven_Mine?oldid=15241726) | 15241726, 2026-06-28 | Doric's directions and nearby self-acquisition route |
| [Rimmington mine](https://oldschool.runescape.wiki/w/Rimmington_mine?oldid=15057599) | 15057599, 2025-11-22 | Alternate self-acquisition route |
| [Falador Diary](https://oldschool.runescape.wiki/w/Falador_Diary?oldid=15295882) | 15295882, 2026-08-13 | Easy task using Doric's anvil |

Transition aid only: the local Quest Helper checkout is pinned at commit
[`5ea99d5`](https://github.com/Zoinkwiz/quest-helper/tree/5ea99d5ea9ba3fb096ebe7b5ed02d80883e9819d/src/main/java/com/questhelper/helpers/quests/doricsquest).
Its `doricsquest` helper maps states 0 and 10 to Doric, requires six clay, four
copper ore, and two iron ore, and records 1 quest point, 1,300 Mining XP, 180
coins, and the anvil unlock. It resolves all four item gamevals and Doric's NPC
gameval.

`tools/questhelper_extract.py doricsquest --check` currently reports only an
aliasing false positive: it derives `quest_doricsquest`, while revision 239's
actual row is `quest_dorics` (packed row 30). The state and gameval extraction
is otherwise useful. Fix or override that audit-tool alias before treating its
non-zero exit as a content failure; the native dbrow was manually verified.

## 2. Native quest identity and state contract

| Field | Native value / expected behavior |
| --- | --- |
| Cache quest ID | 11 |
| Dbrow / packed row | `quest_dorics` / 30 |
| Type | Free-to-play starter quest |
| Difficulty / length | Novice / very short |
| Release | 6 April 2001 |
| Start actor | `doric`, native NPC 3893, world-spawned at 2952,3451 |
| Start metadata | Packed coordinate 48369018, approximately 2952,3450; the live spawn/Wiki differ by one tile |
| Prerequisites | None |
| Required items | 6 unnoted clay, 4 unnoted copper ore, and 2 unnoted iron ore, all presented together |
| Recommended skill | 15 Mining, boostable, only when mining iron personally |
| Acceptance item | 1 bronze pickaxe |
| Primary state | `%doricquest`, native permanent transmitted varp 31 |
| End state | 100 |
| Quest points | 1 |
| Rewards | 1,300 Mining XP, 180 coins, and permanent use of Doric's anvils |
| Required combat | None; Dwarven Mine scorpions are optional route hazards |
| Downstream | Direct prerequisite for Devious Minds; prerequisite for the Falador Easy Diary anvil task |
| Recommendation metadata | Starter/recommendable; “Gain early game Mining XP.” |

The cache supplies the correct display name, free-to-play flag, difficulty,
length, release date, start actor/coordinate, quest points, end state,
recommended Mining level, XP, and early-game recommendation. The implementation
must not introduce a parallel progress carrier or duplicate metadata policy.

### State inventory

| State | Canonical phase | Current implementation |
| ---: | --- | --- |
| 0 | Not started; full Doric conversation and accept/refuse | Four-option legacy dialogue; acceptance writes 10 |
| 10 | Accepted; collect or present all materials | Inventory re-talk and all-at-once hand-in exist |
| 100 | Complete; anvils unlocked and ordinary Doric dialogue | Shared completion queue reaches 100; anvil gate reads it |

These values agree across the native dbrow, Quest Helper, quest scripts, dynamic
journal, smithing gate, Devious Minds prerequisite, and debug adapter. No state
migration is required for ordinary saves. Name all three states explicitly;
the current dispatcher treats every value other than 0 and 10 as post-quest,
whereas the anvil and journal require `>= 100`. Corrupt or future intermediate
values therefore present contradictory behavior and need an explicit fallback.

The local `%doricquest` overlay correctly declares `transmit=yes`, `scope=perm`,
and `protect=no`. Older `docs/CS2_UNIMPLEMENTED_VARPS.md` text claiming the
server declaration is not transmitted is stale and contradicted by the live
file and `port/cs2_varps.map`.

## 3. Implementation and ownership surface

Paths in this section are relative to
`OSRS-Content/osrs239-content/` unless stated otherwise.

### Quest-owned files

| Path | Present responsibility | Audit result |
| --- | --- | --- |
| `server/scripts/quests/quest_doric/configs/quest_doric.constant` | Completion and QP constants | End state is correct; start/in-progress names are absent; QP constant is unused because the dbrow owns QP |
| `server/scripts/quests/quest_doric/configs/quest_doric.varp` | Native varp 31 server declaration | Correct permanent and transmitted carrier |
| `server/scripts/quests/quest_doric/scripts/quest_doric.rs2` | Doric dialogue, hand-in, coins, XP, state, and completion scroll | Core route exists but carries obsolete dialogue, missing branches/items, and a split reward transaction |
| `server/scripts/quests/quest_doric/scripts/doric_journal.rs2` | Three-state dynamic journal | Correct renderer and broad states; omits unnoted/item-progress/pickaxe guidance |

The root is only 134 lines across two configs and two scripts. Its small size is
appropriate for a very short quest, but the old queue's “full dialogue” claim
is demonstrably false against the pinned transcript.

### Shared owners and consumers

| Path | Relationship | Modernization requirement |
| --- | --- | --- |
| `configs/all.dbrow` | Native quest metadata and end state | Preserve row `quest_dorics`; derive QP/XP/UI metadata from it |
| `areas/world/configs/m46_53.spawn` | Real Doric at 2952,3451 | Keep the base spawn; no login or temporary duplicate is needed |
| `skill_smithing/scripts/smithing/smithing.rs2` | Both click-Anvil and item-on-anvil entry points | Shared `%doricquest >= 100` gate is correct; replace generic denial with canonical Doric interaction and add diary hook |
| `skill_smithing/configs/smithing_sources.loc` | Recognises `dorics_anvil` as a real anvil | Retain symbolic category ownership |
| `quest_deviousminds/scripts/deviousminds_monk.rs2` | Direct prerequisite consumer | Keep the native completion predicate; its own start audit must correct other requirement behavior |
| `quest_deviousminds/scripts/deviousminds_items.rs2` | Owns `devious_whetstone` operations | Coordinate Doric's conversational start option and canonical pre-use message without duplicate loc triggers |
| `skill_mining/configs/mine.dbrow` and `rocks.loc` | Clay/copper/iron levels, XP, products, and rock categories | Shared mining supports the optional collection route; exercise real mine placements and ladder in integration |
| `skill_mining/scripts/mining.rs2` | Pickaxe, active-level, inventory, mining, depletion, XP, and resume lifecycle | Active Mining makes level 15 boostable as required; no quest-specific bypass is needed |
| `ladders_stairs/` | Dwarven Mine entrance/exit | `fai_dwarf_trapdoor_down` is category-routed through generic ladder/maplink machinery; verify both directions |
| `interface_questjournal/scripts/quest_journal.rs2` | Routes `quest_dorics` to `~doric_journal` | Keep this modern dbrow dispatch |
| `quests/scripts/questpoints.rs2` | Table-derived QP, completion count, scroll, and novice jingle | Keep the shared lifecycle; call it once inside the final transaction |
| `quests/scripts/quest_cheat.rs2` | `::complete quest_dorics` sets 100 and awards QP once | Correct state-only debug policy; verify first and repeated calls and do not treat it as reward evidence |
| `interface_diaries/` and smithing recipe data | Falador Easy requires blurite limbs on Doric's anvil | Recipe exists; no Doric/Falador task-completion hook was found |

No duplicate `[opnpc1,doric]`, raw entity IDs, quest-owned temporary entities,
legacy IF1 panel opens, or server-side quest-list component routing were found.
The implementation already uses symbolic names, `~p_choice*`, the native
journal, and the shared completion scroll. Modernization should preserve these
foundations.

## 4. Canonical playable route

1. Talk to Doric north of Falador. The player may ask to use either his anvils
   or his whetstone; Doric explains that he makes pickaxes and needs materials.
   The player may accept or refuse.
2. On acceptance Doric gives one bronze pickaxe. If the player does not already
   carry all materials, Doric offers directions or an immediate goodbye. With
   Mining below 15, the directions explain the iron limitation and buying from
   another player.
3. Obtain six clay—not soft clay—four copper ore, and two iron ore. All must be
   unnoted and carried simultaneously. They may be mined, traded, bought, or
   obtained from drops; the quest does not require the player to mine them.
4. If all materials were already carried at acceptance, the transcript's
   coincidence branch immediately hands them in and completes the quest. Exact
   quantities add one extra line; surplus quantities still satisfy the hand-in.
5. Otherwise, re-talk to Doric. Without all materials he repeats the exact
   quantities and again offers directions. With all materials he removes
   exactly six clay, four copper ore, and two iron ore and completes the quest.
6. The single final commit grants 180 coins, 1,300 Mining XP, 1 quest point,
   state 100, completion count/scroll/jingle, and the permanent anvil unlock
   exactly once.

There is no combat, bespoke instance, puzzle panel, cutscene, quest-item death
policy, or lost-item replacement route. The bronze pickaxe and materials are
ordinary tradeable objects; do not invent unique ownership restrictions.

## 5. Dialogue and interaction audit

### Initial Doric conversation

The current four-choice top-level menu omits the transcript's fifth option,
“I want to use your whetstone.” The other branches are only partially faithful:

| Branch | Canonical behavior | Current behavior / defect |
| --- | --- | --- |
| Anvils | Doric says he makes pickaxes and needs materials | Says he makes amulets, then enters the offer |
| Whetstone | Doric explains it is for advanced smithing, then enters the same offer | Missing entirely |
| Insult | Doric rebukes the player's manners | Present and close to current transcript |
| Landscape | Doric discusses solitude and offers Dwarven Mine / “Will do” choices | Replaced with one old “fine town” line and no submenu |
| What do you make? | Pickaxes; running order with Nurmof; optional “Who's Nurmof?” directions | Says amulets, claims none to sell, and ends |

The offer should use the current start-question presentation and exact Yes/No
semantics. Refusal correctly leaves state 0, but the acceptance dialogue is an
older version: it omits the bronze pickaxe, mine directions choice, low-Mining
advice, and ready-now branch.

### Re-talk and ready-now paths

At state 10, the current all-material check correctly requires at least six
`clay`, four `copper_ore`, and two `iron_ore` in inventory. Notes and soft clay
do not match those symbolic objects, and exactly the requested quantities are
removed. Partial materials are not consumed, which agrees with the all-at-once
contract.

When materials are missing, however, the script prints one reminder and closes.
It omits the transcript's “Where can I find those?”/“Certainly, I'll be right
back!” menu, Dwarven Mine directions, and sub-15 Mining advice. On initial
acceptance with all materials, it always writes 10 and ends; the player must
click Doric again, bypassing the canonical coincidence dialogue and immediate
completion.

Modernization should route both initial-ready and later-ready paths into one
authoritative hand-in procedure. Preserve the exact-quantity-only extra joke,
but accept and remove the minimum quantities when the player carries surplus.
Add all reachable transcript choices; do not keep obsolete amulet dialogue just
because it came from a historically valid LostCity-era source.

### Anvil and whetstone entry points

The transcript makes attempted anvil use before completion a Doric conversation:
he objects, then lets the player ask permission and accept/refuse the quest.
The current shared smithing gate instead prints “You must complete Doric's Quest
to use this anvil” for both Anvil and item-on-anvil. The access restriction is
correct, but the interaction and alternative start trigger are missing.

The native whetstone's attempted use should print the canonical “You should
probably ask before using that.” Doric's Quest does not turn the whetstone into
a generic sharpening station; its later real gameplay is owned by Devious
Minds. Coordinate the Talk-to option and loc message with that quest's exact
handler rather than declaring a second conflicting trigger.

## 6. Acceptance item and material acquisition

### Bronze pickaxe

The current script never references `bronze_pickaxe`, despite both the article
and transcript saying Doric hands one over on acceptance. Add it exactly once
when the quest moves from 0 to 10, including the ready-now route. It is an
ordinary object, so no bank/ground/global duplicate suppression or replacement
ledger is appropriate.

The transcript does not document what happens when acceptance has no inventory
space. Live-trace this case before fixing policy. Whatever the native result,
the implementation must not silently discard the pickaxe, duplicate it on
re-talk, or advance to a state whose promised item was never delivered. Cover
inventory-full acceptance, existing bronze pickaxe, dialogue interruption, and
logout immediately after the state/item commit.

### Materials and mining

The shared mining table correctly represents:

| Material | Required quantity | Mining level | Shared XP value | Quest acceptance |
| --- | ---: | ---: | ---: | --- |
| Clay | 6 | 1 | 5 XP | `clay` only; not `softclay`, not notes |
| Copper ore | 4 | 1 | 17.5 XP | `copper_ore` only; not notes |
| Iron ore | 2 | 15 | 35 XP | `iron_ore` only; not notes |

`mining.rs2` checks the active Mining level, so a boost to 15 can mine iron as
the Wiki allows. It also validates a usable pickaxe, inventory space, success
roll, depletion/respawn, output, XP, animations, sounds, and action resume. The
Dwarven Mine trapdoor is categorised into the shared ladder system; the base
cache supplies standard rocks in Dwarven and Rimmington mines.

These shared definitions are promising static evidence, not an end-to-end
proof. Test a new account following Doric's directions with the granted bronze
pickaxe: enter and exit the Dwarven Mine, mine all three resources, handle
aggressive scorpions, return, and hand in. Also exercise Rimmington and a
non-mining acquisition path. Buying or receiving the items must remain valid;
15 Mining is recommended, never a quest start or hand-in requirement.

## 7. Completion, rewards, and permanence

The present hand-in does the following in separate phases:

1. deletes all twelve materials;
2. schedules `doric_quest_complete` through a player queue;
3. continues dialogue and gives 180 coins outside that queue; then
4. the queue writes 100, grants 1,300 Mining XP, awards QP/completion count,
   paints the completion scroll, and plays the shared novice jingle.

The amounts and tenths conversion are correct: `stat_advance(mining, 13000)` is
1,300 XP, and `inv_add(inv, coins, 180)` is the canonical coin reward. Removing
twelve items leaves enough inventory capacity for a coin stack, so ordinary
full-inventory completion is not itself a slot problem.

The ordering is nevertheless not atomic or idempotent. Logout, queue loss, or
duplicate delivery between item deletion, coin grant, and the queued state/XP/QP
commit can strand a state-10 player without materials, omit rewards, or replay
the callback. The callback itself has no `state == 10` guard. Move validation,
exact item deletion, coins, XP, state, and shared completion call into one
guarded final commit after the final dialogue. If the player disconnects before
it runs, no irreversible mutation should have occurred; if it runs twice, the
second invocation must be a no-op.

The reward scroll string omits “180 coins” even though the coins are delivered.
Add the canonical line. The Wiki lists the bronze pickaxe among quest benefits
because it is given at acceptance; live-trace whether the current in-game
completion scroll repeats it before adding that display line.

`~quest_complete_rewards` derives the one quest point from the dbrow, increments
the completed count, paints the modern scroll, and selects the novice jingle.
The authored `^doric_questpoints` constant is therefore unused duplicate policy
and should be removed. The state gate normally prevents a second organic claim,
but transition tests must include repeated Talk-to and duplicate queued resume.

The generic `::complete quest_dorics` path intentionally sets 100 and awards the
dbrow's quest point without XP, coins, or scroll; its second call is a no-op.
That is appropriate setup-cheat policy, but it cannot count as gameplay reward
evidence and creates a completed state distinguishable only by reward history.
Do not retrospectively re-award normal rewards to every existing state-100
account without evidence.

## 8. Unlocks and downstream consumers

### Doric's anvils

The permanent reward is genuinely wired. Both clicking `dorics_anvil` and using
a bar on it call `~smithing_anvil_gate_ok`, which rejects `%doricquest < 100`.
At 100, the normal bar picker and item-on-anvil recipe path proceed. This shared
predicate is a good modern owner and must remain the single access policy.

Verification must cover prequest click/use dialogue, state 10 denial, state 100
click/use success, every supported bar menu, hammer/level/material failures,
logout, and debug completion. Doric's anvil must not unlock from the acceptance
state merely because Doric promises future access.

### Devious Minds

The local Devious Minds start predicate reads `%doricquest == 100`, so the
direct quest dependency is present. That quest's own audit found broader start,
state, travel, and completion defects, but Doric's side of the prerequisite is
the correct native carrier/value. Test the consumer with 0, 10, 99, 100, and a
post-completion value to settle whether the shared predicate should be `>= 100`
rather than exact equality.

### Falador Easy Diary

The current diary requires smithing blurite limbs on Doric's anvil. The smithing
database contains the blurite-limb recipe and the anvil gate enforces Doric's
Quest. However, no Falador Easy task-specific hook was found in smithing or the
diary runtime; the diary subsystem currently exposes aggregate counters and
journals without this Doric action binding.

Add the task hook to the successful blurite-limb production event at the real
Doric anvil, not to quest completion or menu opening. It must fire once, only at
the correct loc/product, and only under the diary's own eligibility policy.
This is a downstream shared-system gap, not permission to change the quest
reward to auto-complete the diary task.

## 9. Journal and evidence policy

The journal is correctly registered by dbrow and rendered through
`~quest_journal`. It distinguishes 0, in-progress, and complete, uses the
correct title, and agrees with end state 100. Improve the in-progress page to
clarify unnoted items, exclude soft clay, and optionally show carried/remaining
quantities without consuming or advancing state. Include Doric's granted
pickaxe and directions only as actionable context, not fabricated progress.

The dispatcher should use named states and a coherent unknown-state policy.
Currently state 20 receives post-quest Doric dialogue, while the journal still
says materials are needed and the anvil remains locked. Tests should explicitly
inject representative invalid values so future additions cannot silently gain
completion-like dialogue.

No quest-specific self-test, transition test, headless start-to-reward smoke,
reconnect test, or live interface capture was found. The standard mining and
smithing self-tests validate table/products broadly but do not exercise Doric's
dialogue, item grant, hand-in, queue lifecycle, state, QP, scroll, or diary
hook. The global combat contract has no Doric entry, appropriately: the quest
has no mandatory or quest-owned enemy.

Stale trackers must be reconciled after implementation. `CONTENT_PORT_QUEUE.md`
claims “full dialogue”; `QUESTHELPER_CONTENT_PORT_QUEUE.md` calls prequest anvil
dialogue and side branches deferred while marking the quest done. Neither label
is verification evidence under the governing plan.

## 10. Modernization work order

1. **Capture remaining behavior.** Live-trace full-inventory acceptance,
   existing-pickaxe acceptance, completion-scroll lines, post-quest dialogue,
   and the exact anvil/whetstone interaction timing.
2. **Name and guard state.** Add symbolic 0/10/100 constants, coherent invalid
   state behavior, and remove the redundant QP constant.
3. **Port the current transcript.** Replace amulets with pickaxes; add the
   whetstone, landscape, Nurmof, directions, low-Mining, refusal, re-talk, exact
   quantity, surplus, ready-now, and post-quest-confirmed branches.
4. **Grant the pickaxe safely.** Implement the traced inventory-full policy and
   guarantee exactly one acceptance grant without making it a quest-unique item.
5. **Unify the hand-in.** Route both ready-now and later-ready conversations to
   one guarded atomic commit for materials, coins, XP, state, QP/count, scroll,
   and jingle; add the missing reward-scroll coin line.
6. **Modernize shared interaction routing.** Make prequest anvil use enter the
   Doric offer, preserve Devious Minds' whetstone ownership, and retain one
   shared anvil access predicate.
7. **Wire downstream evidence.** Add the Falador Easy blurite-limb completion
   hook and test Devious Minds against the canonical completion predicate.
8. **Expand journal and tests.** Cover all three states, inventory progress,
   optional acquisition routes, reconnect/repeat behavior, debug completion,
   completion UI, and post-quest unlocks.

No C or protocol work is justified. Every required change fits existing
RuneScript/config dialogue, inventory, queue, journal, completion, mining,
smithing, and diary machinery.

## 11. Verification matrix

| Area | Required automated/integration evidence |
| --- | --- |
| Metadata | Dbrow 30, quest ID 11, F2P/novice/very-short flags, start actor/coord, QP 1, XP 13000 tenths, end state 100, transmitted permanent varp |
| Start menu | All five top-level choices, anvil and whetstone routes, accept/refuse, repeat refusal, no prerequisites |
| Pickaxe | Normal grant, full inventory, existing pickaxe, ready-now acceptance, interruption/relog, no duplicate grant |
| Directions | Dwarven Mine and Nurmof dialogue, Mining below/at 15, boosts/drains, return/goodbye branches |
| Materials | Each quantity boundary, unnoted vs noted, clay vs soft clay, partial sets, exact set, surplus, banked items, all-at-once removal |
| Acquisition | Doric's bronze pickaxe; Dwarven ladder both ways; clay/copper/iron mining; Rimmington alternative; trade/drop sources; optional scorpion danger |
| Ready-now | All items before start completes in one conversation; exact-quantity joke; surplus variant; same final transaction as re-talk |
| Completion | Materials/180 coins/1300 XP/state100/QP/count/scroll/jingle once; logout before commit; duplicate callback; repeated Talk-to |
| Reward UI | 180 coins, 1,300 Mining XP, anvil unlock, correct QP line/icon/jingle; trace pickaxe display policy |
| Anvil | Prequest click and use-on offer/deny; state10 deny; state100 click/use; ordinary smithing regressions |
| Whetstone | Canonical pre-use message; Doric Talk-to start branch; no collision with Devious Minds behavior |
| Journal | States 0/10/100, invalid values, unnoted clarification, partial counts, completion agreement |
| Devious Minds | Requirement fails below100 and passes at/after native completion according to shared policy |
| Falador Diary | Blurite limbs only, real Doric anvil only, successful production only, exactly-once task credit |
| Debug | First `::complete quest_dorics` sets state/QP without normal rewards; second call is a no-op; anvil unlocks |
| Real client | World-spawned Doric → accept → pickaxe → acquire → hand-in → modern reward scroll → post-quest anvil and dialogue |

## 12. Gate verdict

| Gate | Result | Evidence |
| --- | --- | --- |
| A — source coverage | **Pass for audit** | Native row/varp, four quest files, Doric spawn, journal/cheat/completion, mining/smithing/whetstone, Devious, and diary surfaces are inventoried; Wiki revisions are pinned |
| B — modern machinery | **Partial** | Symbolic names, modern choices, native journal, shared completion, and permanent varp exist; the player queue is not transactionally safe and shared anvil dialogue is reduced to a generic gate |
| C — critical path | **Fail** | Missing bronze pickaxe and current dialogue/ready-now route; split hand-in/reward mutations; incomplete anvil/whetstone behavior and Falador Diary hook |
| D — verification | **Fail** | Quest Helper alias prevents a clean checker result, no quest transition/reconnect/headless tests or captures exist, and global skill tests do not prove this quest |

Release classification remains `audit-pending`. The quest can become
`verified-modern` only after the current transcript and acceptance item are
implemented, the final transaction is replay-safe, both permanent consumers
are verified, the Quest Helper alias/check is resolved, and the full matrix has
replayable evidence.
