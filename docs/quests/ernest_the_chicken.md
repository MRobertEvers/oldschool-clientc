# Ernest the Chicken modernization audit

Status: `audit-pending` — the native quest row, four-state progress carrier,
six lever bits, nine door transforms, quest actors, manor and basement maps,
all machine parts, journal, modern completion API, and downstream Animal
Magnetism prerequisite exist. A fresh player can collect the parts and reach
the nominal completion path, but the implementation is not safe or complete:
the final scene permanently retypes a shared world NPC because this runtime
ignores `npc_changetype` durations, settlement marks the quest complete before
the reward queue and final dialogue, the advertised Killerwatt-plane entrance
has no outbound handler, and Mazchna can assign Killerwatts before the quest.
The fountain omits canonical damage and use-on branches, the locked closet
requires a noncanonical use-item action, abnormal basement exits do not reset
the puzzle, and multiple current dialogue/recovery contracts are missing.

Audited: 2026-08-17

Governing plan: [Quest modernization plan](../QUEST_MODERNIZATION_PLAN.md). This
record applies that plan's Gates A–D to Veronica, Professor Oddenstein, the
manor and basement routes, all three part transactions, the transformation and
reward settlement, post-quest portal service, Animal Magnetism, and Killerwatt
Slayer consumers. It is an implementation specification, not completion
evidence.

## 1. Authoritative references

The current OSRS Wiki revisions below are pinned so implementation and review
use stable route, dialogue, item, reward, and downstream contracts.

| Reference | Pinned revision | Use in this audit |
| --- | --- | --- |
| [Ernest the Chicken](https://oldschool.runescape.wiki/w/Ernest_the_Chicken?oldid=15240928) | 15240928, 2026-06-27 | Identity, requirements, complete route, rewards, pre-start item collection, and Animal Magnetism relationship |
| [Ernest the Chicken/Quick guide](https://oldschool.runescape.wiki/w/Ernest_the_Chicken/Quick_guide?oldid=15176696) | 15176696, 2026-04-15 | Ordered interactions, basement route, door use, and hand-in |
| [Transcript:Ernest the Chicken](https://oldschool.runescape.wiki/w/Transcript%3AErnest_the_Chicken?oldid=15263198) | 15263198, 2026-07-14 | Start/refusal, Professor branches, fountain behavior, seven partial-item combinations, and completion ordering |
| [Transcript:Veronica](https://oldschool.runescape.wiki/w/Transcript%3AVeronica?oldid=15263191) | 15263191, 2026-07-14 | Exact start and post-quest dialogue |
| [Transcript:Professor Oddenstein](https://oldschool.runescape.wiki/w/Transcript%3AProfessor_Oddenstein?oldid=15248067) | 15248067, 2026-07-02 | Standard dialogue, post-quest portal explanation, insulated-boots warning, choices, and entry |
| [Fountain (Draynor Manor)](https://oldschool.runescape.wiki/w/Fountain_%28Draynor_Manor%29?oldid=14893763) | 14893763, 2025-05-02 | Piranha gate and pressure-gauge source |
| [Pressure gauge](https://oldschool.runescape.wiki/w/Pressure_gauge?oldid=15186538) | 15186538, 2026-04-22 | One-damage bite, drop-trick duplicates, and completion cleanup |
| [Rubber tube](https://oldschool.runescape.wiki/w/Rubber_tube?oldid=15186541) | 15186541, 2026-04-22 | Locked-room source and legitimate post-quest duplicates |
| [Oil can](https://oldschool.runescape.wiki/w/Oil_can?oldid=15186083) | 15186083, 2026-04-22 | Basement source and legitimate post-quest duplicates |
| [Key (Ernest the Chicken)](https://oldschool.runescape.wiki/w/Key_%28Ernest_the_Chicken%29?oldid=15186908) | 15186908, 2026-04-22 | Quest-only acquisition, closet use, Drop behavior, and retention after completion |
| [Poisoned fish food](https://oldschool.runescape.wiki/w/Poisoned_fish_food?oldid=15183936) | 15183936, 2026-04-22 | Combination and fountain use |
| [Killerwatt plane](https://oldschool.runescape.wiki/w/Killerwatt_plane?oldid=15125632) | 15125632, 2026-02-12 | Post-quest portal access and return route |
| [Portal machine](https://oldschool.runescape.wiki/w/Portal_machine?oldid=15201722) | 15201722, 2026-04-29 | Draynor entrance ownership and post-quest access |
| [Killerwatt](https://oldschool.runescape.wiki/w/Killerwatt?oldid=15215878) | 15215878, 2026-05-23 | Level-37 attack gate, passive-to-hostile transform, attacks, and insulated-boots effect |
| [Insulated boots](https://oldschool.runescape.wiki/w/Insulated_boots?oldid=15183083) | 15183083, 2026-04-22 | Optional protection rather than an attack requirement |
| [Mazchna/Slayer assignments](https://oldschool.runescape.wiki/w/Mazchna/Slayer_assignments?oldid=15248953) | 15248953, 2026-07-02 | Current Killerwatt assignment requirements: 37 Slayer, 50 combat, and this quest |
| [Animal Magnetism](https://oldschool.runescape.wiki/w/Animal_Magnetism?oldid=15292390) | 15292390, 2026-08-11 | Direct quest prerequisite and shared Draynor Manor consumer |

The sources identify Ernest the Chicken as a free-to-play, novice, very-short
quest released 21 January 2001. It has no formal skill or item prerequisite;
the player only needs to survive an aggressive level-22 skeleton. It awards
four quest points, 300 coins, and members-only access to the Killerwatt plane.
It is directly required for Animal Magnetism. All three machine parts may be
obtained before starting the quest.

Transition aid only: Quest Helper's
[`ErnestTheChicken.java`](https://github.com/Zoinkwiz/quest-helper/blob/5ea99d5ea9ba3fb096ebe7b5ed02d80883e9819d/src/main/java/com/questhelper/helpers/quests/ernestthechicken/ErnestTheChicken.java)
at commit `5ea99d5ea9ba3fb096ebe7b5ed02d80883e9819d` confirms primary
states 0/1/2, every actor/item/zone, the six native lever bits, a convenient
maze route, four quest points, 300 coins, and the portal/Slayer unlocks. Its
`ChatMessageRequirement` for poisoned fish is a client-helper convenience, not
proof that server recovery state should be session-only. `python3
tools/questhelper_extract.py ernestthechicken --check` resolves every referenced
dbrow, NPC, loc, object, and varbit.

## 2. Native quest identity and contract

| Field | Native value / expected behavior |
| --- | --- |
| Cache row | `quest_ernestthechicken`; dbrow pack index 44, quest metadata ID 7 |
| Type / difficulty / length | Free-to-play quest / novice / very short |
| Release / location | 21 January 2001 / Draynor Manor |
| Start | Veronica; native coordinate 3109,3330,0, world spawn 3110,3330,0 |
| Primary state | `%haunted`, permanent varp 32 |
| Canonical values | 0 not started; 1 accepted/finding Ernest; 2 Professor explained the parts; 3 complete |
| Lever state | `%ernestlever`, permanent varp 33; native varbits `ernestlever_a..f` at bits 1–6 |
| Door state | `%ernestdoors`, permanent varp 668; nine native door varbits at bits 0–8 |
| Fountain state | `%haunted_manor_fountain_poisoned`; server-authored permanent state because no cache carrier exists |
| Ernest actor | `ernest_multichicken`; `%haunted` values 0–2 show `ernest_the_chicken`, value 3 hides it |
| End state / quest points | 3 / 4 |
| Reward | 300 coins; access to the Killerwatt plane |
| Downstream | Animal Magnetism start gate; Mazchna's Killerwatt assignment pool; portal and Killerwatt combat |
| Speedrun metadata | Native `speedrun_ernestthechicken` row 3453 |

No replacement primary, lever, or door variable is justified. The authored
fountain fact is a legitimate server-only addition: the cache has no matching
varbit, while poisoning must survive the gap between feeding the fish and a
later search. It must be documented, migrated, and tested like permanent state
rather than removed merely because Quest Helper cannot observe it.

The native `ernest_multichicken` wrapper is specifically designed for
per-player visibility: incomplete players see the chicken and complete players
do not. Modernization must preserve that contract. A shared NPC retype is not
an equivalent transform.

## 3. Implementation surface

The direct root contains 439 lines across five files. The two mandatory shared
actor scripts add 170 lines.

| Quest-owned path | Present responsibility | Audit result |
| --- | --- | --- |
| `configs/quest_haunted.param` | Legacy `lever_index` param declaration | Symbolic but unused; current handlers bind each lever explicitly, so remove it or restore one data-driven owner |
| `configs/quest_haunted.constant` | Four primary states and four quest points | Values agree with the native row |
| `configs/quest_haunted.varp` | Primary, lever, door, and fountain declarations | Native carriers are correct; fountain is explicitly unresolved/server-authored |
| `scripts/haunted_journal.rs2` | Dynamic dbrow journal for states 0–3 | Uses the modern journal API; partial item display is inventory-only |
| `scripts/quest_haunted.rs2` | Manor entry, items, fountain, bookcase, maze, transform, and reward queue | Route-shaped but contains most gameplay, isolation, and settlement defects |
| `areas/draynor/scripts/veronica.rs2` | Start, in-progress, and post-quest dialogue | State routing works; several current transcript strings/actions differ |
| `areas/draynor/scripts/professor_oddenstein.rs2` | Discovery, item checks, machine scene, and side dialogue | Completes the state machine, but final ordering is unsafe and post-quest portal dialogue is absent |

Mandatory shared/cache surfaces:

| Path / subsystem | Relationship | Audit result / required ownership |
| --- | --- | --- |
| `configs/all.dbrow`, `all.varp`, `all.varbit` | Metadata and state schema | Correct row, end state, QP, six lever and nine door fields |
| `configs/all.loc` | Manor locs, multilocs, machine assets, portal, and ladders | Correct puzzle wrappers exist; updated machine animations and smoke effect are available but unused |
| `configs/all.npc` and `areas/world/configs/m48_52.spawn` | Veronica, Professor, chicken wrapper, skeleton | Actors and level-22 skeleton are correctly placed; the wrapper is corrupted by the completion retype |
| `areas/world/configs/m48_52.spawn`, `m48_152.spawn` | Poison, fish food, spade, rubber tube, oil can | All item spawns are present at Quest Helper/Wiki coordinates |
| shared doors and ladders | Manor back door and ordinary floors | Back door and stairs use shared symbolic machinery; the front double door and closet bypass it |
| `quests/scripts/questpoints.rs2` | QP/count/scroll/jingle settlement | Modern API exists but is called by an unguarded queue before the final conversation finishes |
| `interface_questjournal/scripts/quest_journal.rs2` | Journal dispatch | Correctly dispatches `quest_ernestthechicken` to `~haunted_journal` |
| `quests/scripts/quest_cheat.rs2` | Administrative completion adapter | Writes state 3 only; it does not prove route, reward, portal, or scene behavior |
| `quest_animalmagnetism/scripts/anma.rs2` | Direct prerequisite | Correctly requires `%haunted >= 3` |
| Killerwatt portal/maplinks | Advertised unlock | Three return portal links exist; the Draynor-to-plane loc has no outbound maplink or op script |
| `skill_slayer/scripts/slayer_masters.rs2` | Assignment selection | Checks combat/Slayer levels and blocks only; it has no quest-requirement predicate |
| Killerwatt combat and gear scripts | Reward-area gameplay | Ball form never transforms on attack; insulated boots incorrectly zero player damage rather than reducing incoming ranged danger |
| `src/torirsserver/torirs_server_scripts.c` | `npc_changetype` host opcode | Explicitly discards duration, so the supposed 100-tick Ernest form never reverts |

There are no legacy IF1 quest panels, raw entity IDs, or old component-choice
handlers in the direct root. The defects are old world-mutation, queue,
interaction, and omission assumptions rather than an obsolete quest UI.

## 4. State and transition audit

| State | Canonical phase | Current behavior / defect |
| ---: | --- | --- |
| 0 | Not started; all three parts may still be obtained | Veronica acceptance writes 1 correctly. Spawns, key, fountain, and maze allow the documented pre-start collection. |
| 1 | Find Ernest and ask the Professor | Professor's required branch writes 2 after explaining all three parts. Several lines retain older wording, but no route skip occurs. |
| 2 | Collect/bring gauge, tube, and oil can in any order | Inventory checks work, but seven canonical partial-item responses are collapsed into generated list text; banked parts are not acknowledged, which is acceptable only if explicitly tested against live behavior. |
| 2 → 3 | Hand over parts, run machine, talk to Ernest, settle reward | Current code retypes the shared NPC, deletes one of each part, writes 3, and queues coins/QP/scroll **before** Ernest's dialogue. Logout, queue cancellation, or modal collision can leave an unrecoverable completed state or premature completion UI. |
| 3 | Complete; chicken absent; portal and post-quest dialogue available | Veronica is present, but Professor still gives pre-quest generic machine text. The Draynor portal is not enterable and Slayer assignment/combat consumers are wrong. |

The state values already match the cache and Quest Helper and must not be
renumbered. The final transition needs a resumable settlement fact or a
carefully ordered state-2 substate because the native primary varp has no spare
post-handover value.

## 5. Detailed lifecycle audit

### Start, manor access, and actor dialogue

Veronica offers the correct accept/refuse choices and does not write state 1
until her explanation has completed. Re-talks at states 1 and 2 and her
post-quest branch exist. Restore current transcript wording and actions,
including “That house looks spooky,” Veronica's separate shriek/dialogue, and
the precise punctuation/grammar corrected in 2016. These are narrative
differences, not blockers, but the transcript is authoritative.

The front `haunteddoorl/r` handler teleports one tile north, checks coordinate
side to prevent opening from inside, and prints slam messages. This preserves
the important one-way entrance rule, but the file admits that the legacy
double-door sequence was not ported. Use a shared, synchronized double-door
transaction with approach, animation, collision, cancellation, and both-leaf
ownership; retain the back door as the required exit. Test both leaves,
simultaneous users, clicking from inside, and interruption between opening and
crossing.

The bookcase opens by global `loc_change`/`loc_add` for three ticks and directly
teleports the player. Other nearby players can observe or collide with a scene
they did not trigger. The exit lever uses the same shared world mutation. Move
both to an atomic/player-isolated traversal or prove that the temporary public
change and collision are the intended shared-world behavior. A click from the
wrong side currently returns silently; supply the canonical no-op/message
behavior after live verification.

Professor Oddenstein's state-0 machine and house topics mostly follow the old
dialogue, but current text says “there's lots of dangerous equipment” and
“Nothing at the moment... It's broken.” After completion he must instead offer
“What does the portal machine do?” and explain the interdimensional rift. The
current `case 0,3` routing erases this post-quest change and therefore also
removes the warning context for the reward unlock.

### Pressure gauge and fountain transaction

The high-level poisoned-food transaction is present: either item-use direction
combines poison and fish food; poisoned food is consumed at the fountain; a
durable fact permits a later search; the gauge is unavailable after
completion. Input combination frees a slot before adding the result, so it is
capacity-safe.

The remaining interaction matrix is incomplete:

- Searching before poisoning prints the correct bite dialogue but never deals
  the canonical one hitpoint of damage, despite the runtime already supporting
  `damage(uid, hitsplat_damage, 1)`.
- Using ordinary fish food should consume it, show the eating messages, and
  finish with “Now they seem hungrier than ever!” Current code falls through to
  the generic use message.
- Using any non-food item on the live fountain should bite for one damage and
  show “Ow!” Current code gives only the generic use response.
- Trying to fill a water container after poisoning should refuse with “I don't
  think I want to use that water any more.” The fountain is not integrated
  with the water-source handler and has no quest-specific branch.
- Full inventory on gauge pickup is not messaged. The add fails without
  acknowledging success, although the durable poisoned state lets the player
  retry. Reserve a slot or inspect `inv_add` and present the exact result.
- Drop-trick gauge duplicates are canonical before completion. Settlement must
  destroy all duplicate gauges as the pinned item page specifies, including a
  defined bank policy; current code deletes only one carried gauge.

Do not replace `%haunted_manor_fountain_poisoned` with possession inference:
the player may feed the fish, log out, bank/drop items, and return later. Test
that durable state, repeated poisoned-food packets, full inventory, one
hitpoint remaining, death/reconnect, and post-completion search explicitly.

### Compost key and rubber tube

Using a spade on the compost heap supplies a key before completion, retains the
spade, and can be retried after loss. This correctly supports collection before
starting and prevents new keys after completion; the key itself is retained on
completion as the Wiki specifies. Restore the two canonical compost messages
and define inventory/bank/drop-trick duplicate policy from live behavior.

The closet is the route defect. Clicking Open always says locked even when the
key is carried; the player must use the key on the door. The guide simply
instructs the player to go through the door with the key, and the item page says
the key opens it. Implement normal Open with an inventory key, keep the
use-item direction if live accepts it, and retain the key. Full interaction
tests must cover no key, carried key, banked key, duplicate key, both sides,
and repeated clicks.

The rubber-tube spawn and aggressive level-22 `draynor_skeleton` are present.
Static ground-item ownership permits the documented pre-start pickup and the
documented post-quest duplicate when the key was kept. Do not add a blanket
quest-state gate that removes those canonical cases.

### Oil-can lever puzzle

The puzzle uses the correct cache architecture rather than temporary door
spawns: six per-player lever varbits drive nine per-player door multilocs. The
local formulas are the LostCity truth table shifted to native bit positions
1–6. The Wiki route and pinned Quest Helper path reach the oil can with the
implemented transitions; no incorrect formula was found in static review.
The oil-can ground spawn is at 3092,9755,0, and post-quest duplicates are
canonical.

The door child is intentionally clickable only while ajar, and traversal uses
the active loc's angle/shape. However, `~ernest_walk_through_door` teleports
across the threshold instead of using an interruptible walk/door transaction.
Replace it with the modern traversal primitive and test every door from both
sides at every open state, repeated packets, obstruction, and two players with
different varbits.

The Wiki says leaving and returning resets the levers. Normal climb-up calls
`~reset_haunted_levers`, but descent does not. Teleporting out, region removal,
or another abnormal exit leaves the permanent bits set, and a later descent
resumes the old puzzle. Reset on authoritative basement entry and on every
leave path, without clearing another player's state. Logout while still inside
should follow verified live behavior; it must not produce an impossible mix of
lever and door bits.

### Machine scene, shared actor, and completion settlement

The final scene is the critical engine defect. `~change_ernest` finds the
world-spawned chicken leaf and calls `npc_changetype(ernest, 100)`. In this
runtime, `SS_OP_NPC_CHANGETYPE` mutates the shared NPC record and explicitly
discards `duration`; no timed reversion occurs. One completion can therefore
leave the shared spawn as Ernest indefinitely, expose the human to unrelated
players, hide the expected chicken, and cause later completions to depend on
the corrupted actor. The cache's `%haunted` wrapper cannot restore per-player
semantics after the server has replaced the wrapper/leaf globally.

Build a player-scoped, resumable completion scene using the modern instance or
private-entity machinery. If that machinery lacks a general capability, add a
general engine primitive with ownership, duration/reversion, logout, region,
and cleanup tests; do not add a quest-specific C shortcut. A merely timed
global retype is still wrong because it leaks the scene for 100 ticks.

Revision 239 already supplies `draynor_machine_ready`,
`draynor_machine_active`, `draynor_ernest_smokepuff`, and named
`ernest_clunk`/`ernest_sprinkle` sounds. Current content uses only messages and
an invented ray line. Drive the real machine loc animation and smoke/actor
transform from the scoped scene. Sound remains conditional on completing the
shared `sound_synth` host support; record that engine dependency rather than
silently dropping named assets.

Settlement must be atomic and exactly once. The current order is:

1. retype shared chicken;
2. delete one gauge, oil can, and rubber tube;
3. write `%haunted = 3`;
4. queue 300 coins and `~quest_complete_rewards` at delay zero;
5. continue Ernest's dialogue and only then claim that he hands over coins.

That order contradicts the transcript and has no recovery path. The completion
queue can mount the scroll while dialogue is active, a cancelled queue can
leave state 3 without coins/QP, and direct/repeated invocation can add four QP,
increment completed count, and add coins again because neither queue nor
shared reward proc is idempotent.

Introduce a durable handover/settlement fact or equivalent transaction. Verify
all three items and scene ownership, consume the exact intended copies, run the
machine and Ernest dialogue, grant 300 coins, award QP/count once, write state
3 at the commit point, and present completion after Ernest's last line. Because
consuming three non-stackable parts creates capacity, the coin grant normally
fits; still test existing/no coin stack, maximum stack, full inventory,
disconnect at every scene tick, repeated Talk, and duplicate queued delivery.
If interruption occurs after handover, re-talk must resume rather than demand
the parts again or replay the scene/reward.

### Reward unlocks and downstream consumers

Animal Magnetism correctly calls `~anma_has_requirements`, which rejects
`%haunted < 3`. Preserve that direct prerequisite and test state 2/3 plus
administrative/migrated completion.

The Killerwatt-plane reward is currently nonfunctional. Cache locs and three
plane-to-Draynor maplinks exist, and the generated transport oracle identifies
the Draynor interdimensional rift at 3110/3111,3363,2 gated by varp 32 = 3.
There is no outbound `draynor_killerwatt_portal` maplink or `oploc1` handler,
and no script derives `slayer_killerwatt_portal_check` from completion. Bind
visibility and Enter to `%haunted = 3`, route to the canonical plane arrival,
and retain all three return tiles.

Entering without insulated boots is allowed after a warning and choices from
Professor Oddenstein; the old epilepsy warning is historical and must not be
restored. Wearing boots should enter directly. Current Professor dialogue has
none of these branches. Implement the current transcript exactly, including
where to obtain boots, cancel/stay, and proceed-anyway choices.

Mazchna's native task row already carries 50 combat, 37 Slayer, quantity
30–50, and weight 6. Assignment selection checks only the numeric requirements
and block list, so an incomplete player can receive an inaccessible task. Add
a shared data-driven quest/access predicate and require `%haunted = 3` for task
73; do not hard-code this only in Mazchna dialogue.

The reward area itself also needs integration repair:

- passive `slayer_killerwatt_ball` NPCs never transform into the hostile biped
  when attacked, despite cache animation/spot/sound assets;
- the generic Slayer-level gate correctly reads 37, but assignment and area
  access need their separate quest rules;
- `~slayer_cap_finish_damage` currently returns zero player damage when
  insulated boots are absent. The Wiki says boots protect from the creatures'
  stronger ranged attacks; they are not required to damage Killerwatts;
- verify two-tick melee/ranged behavior, prayer interaction, optional boots,
  transformation, drops, exit, death, and relog rather than treating successful
  teleport as proof of the unlock.

### Journal and administrative adapters

The journal uses the modern dbrow dispatch and renders sensible states 0–3.
At state 2 it marks only carried machine parts complete. Confirm live journal
bank semantics before changing that, then add any durable handover/recovery
substate so a reconnect never displays a misleading shopping list. Preserve
standard completed styling.

`::complete` writes state 3 only and is idempotent as a state adapter. It does
not add four quest points, coins, portal state, item cleanup, or scene effects.
Keep those concerns out of a destructive debug shortcut, but require the
post-quest login/reconciliation path to derive safe transforms and unlocks.
Test the adapter twice as the governing plan requires; never count it as a
fresh-route or reward test.

## 6. Migration and recovery

The primary values and native puzzle bits already have correct meanings; do
not renumber or reuse them. A modernization deployment must reconcile legacy
state deliberately:

1. Preserve `%haunted` 0–3 and the poisoned-fountain fact. Preserve valid
   pre-start machine parts and the retained closet key.
2. If a player is outside the basement, clear inconsistent/stale lever and
   door values together on next authoritative puzzle entry. Inside players
   must follow a tested resume-or-reset policy without trapping them behind a
   closed transform.
3. State-3 players may have lost coins/QP because the state was committed
   before a cancellable queue. Aggregate `%qp` and quest count cannot prove
   which quest paid them. Do not blindly replay rewards; use audit telemetry,
   an explicit migration ledger, or support reconciliation for ambiguous
   saves.
4. Remove pressure-gauge duplicates at/after verified completion according to
   the item contract. Do not remove the retained key or legitimate post-quest
   rubber tubes/oil cans.
5. A currently running world may already have the shared NPC permanently
   retyped. Reinstantiate/reset the `ernest_multichicken` world spawn during
   deployment before enabling the scoped completion scene.
6. Derive portal visibility/access from `%haunted = 3` on login and every stage
   commit. Never treat the unrelated strobe-warning bit as completion.
7. Stop new pre-quest Killerwatt assignments. If a legacy player already has
   one, provide an explicit finish/reset policy so the migration does not
   strand an active task.
8. Preserve Animal Magnetism progress. Its prerequisite was checked only at
   start, so do not roll it back merely because other Ernest reward facts need
   reconciliation.

## 7. Modernization sequence

### Gate A — state, interactions, and deterministic puzzle

1. Lock the native 0–3, lever, door, actor-wrapper, and fountain contracts in
   transition tests; add migration coverage before changing handlers.
2. Restore exact Veronica/Professor/partial-item dialogue and normal closet
   Open behavior while retaining valid pre-start collection.
3. Complete the fountain use matrix, one-damage bite, capacity responses,
   water-container refusal, and gauge duplicate lifecycle.
4. Replace front door, bookcase, closet, basement doors, and reset paths with
   authoritative interruptible traversals; verify the nine-door truth table.

### Gate B — scoped machine scene and safe settlement

1. Replace the shared permanent NPC retype with a player-owned instance/private
   scene and add general engine support only where proven missing.
2. Use cache-native machine animation, smoke transform, and eventually named
   sounds with cleanup on logout, death, and region change.
3. Add a durable handover/resume fact and make part consumption, 300 coins,
   QP/count, state 3, and completion presentation atomic and idempotent.
4. Reconcile legacy state-3 saves and pressure-gauge duplicates without
   deleting canonical retained/post-quest items.

### Gate C — portal and post-quest experience

1. Restore Professor's post-quest portal explanation and insulated-boots
   warning/choice tree.
2. Bind portal visibility and Draynor-to-plane travel to state 3; retain return
   links and correct cancellation/death behavior.
3. Implement passive-to-hostile Killerwatt transformation and correct their
   two-style/two-tick combat and optional-boots protection.
4. Remove the incorrect no-boots outgoing-damage cap and verify the level-37
   attack gate independently.

### Gate D — consumers and integration

1. Add the Ernest completion predicate to Mazchna task selection while
   preserving combat 50, Slayer 37, weight 6, and 30–50 quantity.
2. Reverify Animal Magnetism states and the shared manor/bookcase topology.
3. Repair journal/admin/login reconciliation and remove stale “dropped” or
   “not ported” claims once each capability has evidence.
4. Run fresh, migrated, pre-start-item, loss/recovery, isolation, portal,
   Slayer, and downstream scenarios through real interactions.

## 8. Verification matrix

| Area | Required checks |
| --- | --- |
| Start | Accept/refuse; repeated refusal; state written after dialogue; states 0/1/2/3 Veronica text; native/spawn marker coordinates |
| Pre-start collection | Each machine part individually and all three together at state 0; no accidental quest start; later Professor hand-in |
| Manor entry | Both front leaves; outside/inside; simultaneous users; slam/collision/cancellation; back-door-only exit |
| Bookcase/secret room | Both bookcase leaves and sides; two players; lever exit; interruption; no leaked loc/collision state |
| Fish-food combination | Both item-use directions; missing/repeated inputs; full inventory; pre/post quest policy |
| Fountain | Search live/dead/after gauge/after complete; exact one damage; 1 HP; ordinary food; arbitrary item; four water containers; full inventory; logout; drop-trick duplicates |
| Key/tube | No spade; spade retained; inventory/full/bank/drop policies; Open and use-key; both door sides; key retained; skeleton aggression/combat; post-quest tube duplicate |
| Puzzle state | All 64 lever combinations and nine expected door bits; published route; alternate valid routes; every door both directions; oil-can/Telekinetic Grab restriction |
| Puzzle reset | Normal ladder exit/re-entry; teleport; logout in/outside basement; death; region rebuild; state/door atomicity; two players with different states |
| Partial hand-in | No parts; all three single-part and all three two-part combinations; banked items; exact current transcript; no consumption before full set |
| Machine scene | Real loc animation/smoke; player-private Ernest; two simultaneous completers; shared incomplete observer; missing actor; every tick cancellation/logout/death/region change; cleanup |
| Settlement | State 0/1/2/3 entry guard; full and maximum coin stack; duplicate parts/gauges; exact one 300-coin grant; four QP/count/scroll/jingle once; repeated Talk/queue/login |
| Migration | Every primary state; poisoned fact 0/1; all lever/door combinations inside/outside; ambiguous state-3 rewards; stuck world NPC; item duplicates across inventory/bank |
| Portal dialogue | Post-quest topic; boots worn/not worn; every warning option; no obsolete epilepsy warning; repeated entry |
| Portal travel | States 0–2 hidden/refused; state 3 visible; both Draynor tiles; all three return tiles; full/reconnect/death/teleport; no cross-player leakage |
| Killerwatts | Slayer 36/37 and combat/task independence; ball transform; melee/ranged cadence; prayers; boots absent/worn; player damage unaffected; drops/exit/relog |
| Slayer assignment | Mazchna truth table for combat 49/50, Slayer 36/37, quest 2/3, block list, weights, legacy active task |
| Animal Magnetism | Ernest states 2/3; all other prerequisite combinations; already-started quest preserved; shared bookcase/manor access |
| Journal/admin | Every state and handover substate; carried/banked/lost items; standard completion; `::complete` twice; login-derived portal state without reward replay |

Required static evidence includes a clean RuneScript/config build, duplicate
trigger and unresolved-symbol scans, puzzle truth-table tests, world-mutation
ownership review, no unexpected numeric IDs, and `python3
tools/questhelper_extract.py ernestthechicken --check`. Required runtime evidence
is a command-free fresh 0→3 playthrough, a full pre-start-parts route, all
fountain and closet branches, normal and abnormal puzzle resets, two concurrent
players during the machine scene, interruption at every settlement boundary,
portal warning/travel, Killerwatt combat, Mazchna assignment gating, and Animal
Magnetism integration. A completion scroll, state write, successful compile,
or debug command alone is not route proof.

## 9. Definition of done

Ernest the Chicken is modernized only when a fresh player can accept or refuse
Veronica, collect any or all three parts before or after starting, traverse
every manor and puzzle interaction with correct failure/reset behavior, recover
from item loss/full inventory/reconnect, and hand the parts to Oddenstein in a
player-isolated machine scene. Ernest, 300 coins, four quest points, completed
count, jingle, state 3, and the completion scroll must settle exactly once in
canonical order under interruption and repeated delivery. No shared NPC or loc
may remain corrupted. Completion must unlock the current Professor dialogue
and a working Killerwatt-plane portal, current Killerwatt combat and optional
insulated-boots behavior, Mazchna's correctly gated assignment, and Animal
Magnetism without relying on debug state or stale legacy assumptions.
