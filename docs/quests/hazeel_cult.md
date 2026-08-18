# Hazeel Cult modernization audit

Status: `audit-pending` — the Hazeel branch can reach a reward scroll on an
ideal run, but the implementation is not a valid modern Hazeel Cult. The port
uses two private replacement varps instead of the cache's native secondary
state, bypasses several route gates, and reports successful item hand-offs
after failed adds. The Ceril branch appears statically unable to complete:
searching the upstairs cupboard looks for Ceril, Jones, and the guard near the
player, while the port only creates those actors downstairs and never moves
them into the scene. Completion writes the terminal state before settling any
reward, and neither the five-Kudos claim nor route-sensitive Secrets of the
North continuity is implemented.

Audited: 2026-08-17

Governing plan: [Quest modernization plan](../QUEST_MODERNIZATION_PLAN.md). This
record applies that plan's Gates A-D to both canonical branches, native state,
dialogue, mansion and sewer traversal, valves and raft routing, actor
visibility, combat ownership, item transactions, cutscenes, completion,
recovery, journal/admin adapters, and downstream consumers. It is an
implementation specification, not completion evidence.

## 1. Authoritative references

The current OSRS Wiki revisions below are pinned so implementation and review
use stable route, dialogue, item, recovery, reward, and integration contracts.

| Reference | Pinned revision | Use in this audit |
| --- | --- | --- |
| [Hazeel Cult](https://oldschool.runescape.wiki/w/Hazeel_Cult?oldid=15285220) | 15285220, 2026-08-01 | Identity, requirements, both routes, rewards, one-time items, and downstream requirement |
| [Hazeel Cult/Quick guide](https://oldschool.runescape.wiki/w/Hazeel_Cult/Quick_guide?oldid=15289620) | 15289620, 2026-08-07 | Ordered actions, valve solution, fake ending, and branch-specific item sequence |
| [Transcript:Hazeel Cult](https://oldschool.runescape.wiki/w/Transcript%3AHazeel_Cult?oldid=15263255) | 15263255, 2026-07-14 | Dialogue topology, state comments, full-inventory outcomes, actor scenes, and completion boundaries |
| [Ceril Carnillean](https://oldschool.runescape.wiki/w/Ceril_Carnillean?oldid=15239676) | 15239676, 2026-06-25 | Start, Ceril-route accusation, postquest state, and later death |
| [Clivet](https://oldschool.runescape.wiki/w/Clivet?oldid=15199770) | 15199770, 2026-04-28 | Branch decision, poison and mark hand-offs, location changes, and later continuity |
| [Alomone](https://oldschool.runescape.wiki/w/Alomone?oldid=15199483) | 15199483, 2026-04-28 | Route-dependent forms, level-13 combat, death drop, ritual, and later continuity |
| [Butler Jones](https://oldschool.runescape.wiki/w/Butler_Jones?oldid=15196499) | 15196499, 2026-04-25 | Mansion dialogue, conspiracy, arrest, and route-dependent persistence |
| [Hazeel](https://oldschool.runescape.wiki/w/Hazeel?oldid=15239700) | 15239700, 2026-06-25 | Resurrection scene and Secrets of the North continuity |
| [Carnillean armour](https://oldschool.runescape.wiki/w/Carnillean_armour?oldid=15182934) | 15182934, 2026-04-22 | Chest source, drop trick, hand-in, equipment, and no postquest replacement |
| [Hazeel's mark](https://oldschool.runescape.wiki/w/Hazeel%27s_mark?oldid=15216785) | 15216785, 2026-05-25 | Hazeel-route cosmetic item and no postquest replacement |
| [Hazeel scroll](https://oldschool.runescape.wiki/w/Hazeel_scroll?oldid=15187022) | 15187022, 2026-04-22 | Hidden chest source, destroy/recovery behavior, and ritual consumption |
| [Chest key (Hazeel Cult)](https://oldschool.runescape.wiki/w/Chest_key_%28Hazeel_Cult%29?oldid=15186925) | 15186925, 2026-04-22 | Crate source, chest use, and temporary-item lifecycle |
| [Scruffy](https://oldschool.runescape.wiki/w/Scruffy?oldid=15196682) | 15196682, 2026-04-25 | Poison outcome, dog visibility, and grave state |
| [Ardougne Sewers](https://oldschool.runescape.wiki/w/Ardougne_Sewers?oldid=15267033) | 15267033, 2026-07-18 | Cave, raft, islands, hideout topology, and later reuse |
| [Secrets of the North](https://oldschool.runescape.wiki/w/Secrets_of_the_North?oldid=15277596) | 15277596, 2026-07-28 | Required-quest relationship and reconciliation of either Hazeel Cult outcome |

These sources define a members, novice, very-short quest released 15 August
2002. There are no prerequisite quests, skills, or mandatory supplied items.
The Ceril route requires defeating Alomone (level 13), while the Hazeel route
requires no combat; combat level 10 is recommended. Both routes award one
quest point, 1,500 Thieving XP, and 2,000 coins. The Ceril route also pays five
coins at the fake ending and permits the player to retain Carnillean armour by
using the drop trick. The Hazeel route gives Hazeel's mark. Those branch items
are obtainable only during the quest and cannot be reclaimed afterward.
Historian Minas can award five Kudos, and Hazeel Cult is required for Secrets
of the North.

Transition aid only: Quest Helper's
[`HazeelCult.java`](https://github.com/Zoinkwiz/quest-helper/blob/5ea99d5ea9ba3fb096ebe7b5ed02d80883e9819d/src/main/java/com/questhelper/helpers/quests/hazeelcult/HazeelCult.java)
and
[`HazeelValves.java`](https://github.com/Zoinkwiz/quest-helper/blob/5ea99d5ea9ba3fb096ebe7b5ed02d80883e9819d/src/main/java/com/questhelper/helpers/quests/hazeelcult/HazeelValves.java)
at commit `5ea99d5ea9ba3fb096ebe7b5ed02d80883e9819d` confirm primary states
0, 2-7, and 9; the seven support bits used to route the helper; all relevant
zones and object placements; the branch-specific items; and reward amounts.
The quest file last changed in `354ccc5f751fa44348a13b8ca5aac9654b7ea097`
on 2025-10-02, and the valve helper last changed in
`712efb2cb346037da0fc626ac6594b4567cfcfc7` on 2025-08-15. Running
`python3 tools/questhelper_extract.py hazeelcult --check` resolves every item,
NPC, object, named support varbit, and the quest dbrow. Quest Helper cannot
prove exact server writes, actor ownership, cutscene timing, item atomicity,
or save migration; its five valve flags are client configuration, not native
server state.

## 2. Native quest identity and state contract

| Field | Native value / expected behavior |
| --- | --- |
| Cache row | `quest_hazeelcult`; quest metadata ID 34 |
| Type / difficulty / length | Members quest / novice / very short |
| Release / series | 15 August 2002 / Mahjarrat series #2 |
| Start | `sir_ceril_carnillean_vis` at the Carnillean Mansion |
| Primary state | `%hazeelcultquest`, native permanent/transmitted varp 223 |
| Proven values | 0 not started; 2 started; 3 decision conversation; 4 branch selected; 5 poison used; 6 branch task complete; 7 armour/scroll phase; 9 complete |
| Secondary state | Native varp 3748, `hazeelcult_secondary`, with nineteen occupied bits |
| End / quest points | State 9 / 1 QP |
| Requirement policy | No start requirements; level-13 Alomone only on the Ceril branch; combat 10 recommended |
| XP | `stat_xp_awarded=(Thieving,15000)` in tenths, exactly 1,500 XP |
| Currency | 2,000 coins at real completion; five additional coins at the Ceril fake ending |
| Branch items | Hazeel's mark on the Hazeel route; optional retained Carnillean armour on the Ceril route |
| Direct consumers | Quest journal/list, Historian Minas Kudos, Secrets of the North, mansion/sewer actor and scenery transforms |

The dbrow correctly records end state 9, one quest point, recommended combat
10, and Thieving XP. Its direct and indirect prerequisite fields both contain
the cache's no-requirement sentinel. Runtime acceptance should still use the
generic quest-offer machinery so the quest confirmation and low-combat warning
match the current transcript.

The native secondary carrier already describes the lifecycle the port needs:

| Native field | Bits | Intended responsibility / audit result |
| --- | ---: | --- |
| `%hazeelcult_clivet_location` | 0-1 | Branch/location transform for entrance and hideout Clivet; never written |
| `%hazeelcult_alomone_vis` | 2-3 | Noncombat, attackable, defeated/chest-searchable Alomone states; never written |
| `%hazeelcult_alomone_met` | 4 | First hideout conversation; never written |
| `%hazeelcult_given_armour` | 5 | Armour returned to Ceril; never written |
| `%hazeelcult_jones_cutscene` | 6 | Evidence/arrest cutscene completed; never written |
| `%hazeelcult_jones_location` | 7-8 | Butler present, removed, or jailed transform; never written |
| `%hazeelcult_poison_success` | 9 | Ceril has confirmed Scruffy was poisoned; never written |
| `%hazeelcult_given_amulet` | 10 | Hazeel's mark delivery settled; never written |
| `%hazeelcult_sewer_chat` | 11 | One-time sewer/raft conversation; never written |
| `%hazeelcult_found_armour` | 12 | Armour recovered from the hideout chest; never written |
| `%carnillean_dog_vis` | 13-14 | Scruffy/live/grave presentation; never written |
| `%hazeelcult_given_scroll` | 15 | Scroll handed to Alomone; never written |
| `%hazeelcult_hazeel_cutscene` | 16 | Resurrection scene settled; never written |
| `%hazeelcult_given_poison` | 17 | Initial poison delivery/recovery ownership; never written |
| `%hazeelcult_returned_to_ceril` | 18 | Ceril re-talk checkpoint on the loyal route; never written |

The current port redeclares the primary varp correctly, then authors permanent
server-only `%hazeelcult_side` and `%hazeelcult_valves` at allocated varps 5927
and 5928. The side varp duplicates information the native support bits were
designed to expose to NPC transforms and downstream content. The valve varp
stores five normalized “correct” flags rather than physical left/right states.
Neither is understood by native multinpcs, native multilocs, the client, or an
imported current-era save. Conversely, a save carrying correct native support
bits defaults to the authored “good” value and can be routed down the wrong
branch.

### Required state capture and migration

Capture varp 223, all nineteen native support bits, relevant inventory/bank/
worn/ground ownership, and actor/loc transforms after every canonical action:
accept/refuse; each Clivet topic and final choice; failed/successful poison
delivery; each valve direction; prohibited and partial raft trips; poison use;
Ceril's result conversation; mark delivery; Alomone meeting/death; armour
search/hand-in; fake completion; evidence/arrest; key and scroll acquisition;
ritual; interruption/re-login at every yielded scene; and real completion.

Migration must be versioned and must not guess from primary state alone:

| Existing shape | Risk | Migration rule |
| --- | --- | --- |
| State 0-3 with authored side 0 | Side 0 is merely the default, not a Ceril commitment | Preserve undecided status; derive no branch until a native branch bit or state-4 evidence exists |
| State 4-8 with authored side 0/1 | Locally authoritative route but no native transforms | Translate the route and completed milestones into captured native values; retain authored values read-only through one compatibility release |
| Imported native support bits with default authored side | Current scripts may reinterpret a Hazeel save as Ceril | Prefer valid native support evidence; quarantine contradictory saves for deterministic reconciliation |
| Authored valve flags | They encode correctness, not direction | Map each object identity to its physical direction; `sewervalve3` is the left-turn object and the other four are right-turn objects |
| State 7 Ceril | May be legitimately awaiting evidence or locally stranded by missing actors | Restore the correct actor/cutscene checkpoint; do not auto-complete |
| State 7 Hazeel with scroll absent | May be failed add, destroyed item, banked item, or already handed in | Reconcile native `given_scroll`, all ownership domains, and ritual marker; allow canonical replacement only when none apply |
| State 9 without XP/QP/coins | Current completion writes state first | Use settlement history/markers; repair each missing component once without replaying settled components |
| State 9 with no route item | Often canonical because branch items are missable and non-reclaimable | Never grant armour or mark merely because the quest is complete |

Do not delete the authored fields until migration metrics show no remaining
saves depend on them. They must cease being authoritative immediately after
the migration boundary.

## 3. Implementation surface

The direct quest root has 1,300 lines across eleven scripts and four config
files. It is a LostCity port with a few current-OSRS reconciliations, notably
moving Carnillean armour from Alomone's drop table to the nearby chest. It also
depends on world placement, native multinpcs/multilocs, inventory and ground
items, combat death credit, dialogue/cutscene machinery, journal dispatch,
quest completion, the museum, and Secrets of the North.

| Path / subsystem | Present responsibility | Audit result |
| --- | --- | --- |
| `quest_hazeelcult.constant` | Primary values, branch constants, QP | Primary values match known states; branch state duplicates the native carrier |
| `quest_hazeelcult.varp` | Primary, side, and valve persistence | Primary is correct; two authored permanent varps bypass native support state |
| `quest_hazeelcult.npc` | Alomone combat overlay | Correct level-13-scale stats and null generic drop; route form is still manually spawned |
| `ceril_carnillean.rs2` | Actor spawning, start, branch dialogue, armour hand-in | Uses private raw variants, old dialogue, and a fake-completion queue; never stages the actors upstairs |
| `clivet.rs2` | Actor spawning, branch decision, poison/mark delivery | Route-shaped but bypasses native location/item bits and lacks transactional delivery |
| `claus_the_chef.rs2` | Family dialogue and poison-on-range | Consumes poison and advances state; does not change dog/grave state |
| `alomone.rs2` | Route forms, fight, death credit, ritual, Hazeel | Owner-private combat is a good direction; form transitions, ritual settlement, and actor lifecycle bypass native state |
| `quest_hazeelcult_locs.rs2` | Cave, valves, raft, chest, wall, cupboard, key/scroll | Broad route is present; access gates, cutscene ownership, physical states, and several item failures are wrong |
| Butler/guard/cultist/Philipe scripts | Branch dialogue | Large LostCity dialogue surface; no native presentation state and no Henryeta owner |
| `hazeelcult_journal.rs2` | Quest journal | Registered and branch-aware only through authored side; item ownership partially substitutes for missing support bits |
| `quest_hazeelcult.rs2` | Fake and real completion | State-first, non-idempotent, and not item/currency safe |
| native varp/varbits/dbrow | Persistent server/client contract | Complete cache contract is present but secondary state is orphaned by runtime |
| native actor configs | Ceril/Jones/Alomone/Clivet/dog transforms | Present; port spawns resolved variants directly instead of driving transforms |
| native mansion loc configs | Chest, bookcase, dog grave, later-quest variants | Present; Hazeel Cult support bits do not drive the dog/grave and some SOTN multilocs are shared |
| Secrets of the North | Required downstream story | Explicit soft-skip; delegates shared NPC handlers but ignores Hazeel Cult outcome and support bits |
| Varrock Museum | Five Kudos via Historian Minas | Kudos varbits/assets exist, but no Hazeel Cult claim owner was found |
| journal / quest cheat | Journal dispatch and admin completion | Journal dispatch works; cheat writes only state 9 and cannot settle or reconcile route/support state |
| automated tests | Route, multiplayer, interruption, and rewards | No Hazeel Cult tests found |

This is not a missing-assets problem. The cache already contains the primary
and secondary carriers, route-aware Alomone and Clivet multinpcs, Jones
location forms, Scruffy and grave transforms, cutscene variants, the coffin,
all temporary and branch items, mansion scenery, and later-quest actors. The
modern implementation should restore authoritative placements and drive those
assets through per-player state instead of maintaining a parallel quest model.

## 4. Route reachability

### Shared opening and sewer puzzle

| Phase | Canonical transition | Current behavior / defect |
| --- | --- | --- |
| Start | Talk to Ceril, receive quest confirmation, get a low-combat warning below combat 10, accept or refuse | Direct dialogue writes state 2; no generic offer/warning interface; much of the 2026 transcript is replaced by older LostCity text |
| Cave | Enter, meet Clivet, explore questions, defer or make an explicit final choice | Cave is always usable, which is fine; decision topology is much shorter and writes no native location bits |
| Branch choice | Ceril refusal moves Clivet; Hazeel acceptance gives/retries poison with full-slot feedback | Branch is written to authored side; poison `inv_add` is unchecked and reports no canonical failed-delivery message |
| Valves | Five independent physical directions: cache object `sewervalve3` left, the other four right | Correct solution is encoded despite different Wiki numbering; physical direction is collapsed into “correct” bits and no current-direction inspection is possible |
| Raft gate | Before Clivet permits pursuit, he stops the player; later the raft reaches an island for each correct prefix | Any player, including state 0, can pre-solve valves and ride to the hideout; raw teleports replace raft movement/cutscene state |
| Return | Any non-entrance raft returns to the entrance | Implemented as a single unconditional teleport; placement identity and travel presentation are collapsed |

The valve solution must be reviewed by object placement, not by the prose
number shown on the Wiki map. Quest Helper identifies `sewervalve3` at the
north-of-cave valve and turns it left; `sewervalve1`, `2`, `4`, and `5` turn
right. The current normalization implements that solution. Modernization
should preserve it while storing or deriving actual direction so inspection,
animation, import, and replay have a truthful state.

### Ceril branch

| Phase | Canonical transition | Current behavior / defect |
| --- | --- | --- |
| Refuse Clivet | Clivet boards the raft and disappears for this player | Deletes a private raw NPC and writes state 4/side 0; native Clivet location is untouched |
| Confront Alomone | Noncombat Alomone reveals Jones, then becomes attackable | Port spawns the attackable two-option variant immediately, so attack/chest retaliation can precede the disclosure and native form transition |
| Kill | Owner's level-13 Alomone dies, drops one bones, and becomes absent/searchable | Player-owned spawn and hero credit are good; bones are public, native Alomone state is untouched, and death state is redundantly queued |
| Armour | Search chest after kill; full inventory preserves it; drop trick works until hand-in | Space check and global ownership helper are good; native found-armour bit is never written |
| Hand-in | Give worn/inventory armour, receive five coins and fake reward screen, accuse Jones | Armour deletion works across inventory/worn; coin add is unchecked and fake settlement has no replay marker |
| Evidence | Search upstairs cupboard; Ceril, Jones, and guard perform the arrest scene | Handler searches radius 6 from upstairs. The only authored actors are created downstairs and never moved; it returns when Ceril is not found, leaving state 7 |
| Completion | Evidence scene settles real reward and persistent Jones state | Unreachable under the authored actor placement; if forced, it deletes a found Jones actor directly instead of writing native cutscene/location state |

The evidence scene is the first hard blocker on the Ceril route. The mansion
spawn proc creates Ceril at `0_40_51_6_6`, Jones at `0_40_51_8_7`, and the
guard at `0_40_51_10_8`. The cupboard is upstairs. No script moves those
actors, creates cutscene copies at the cupboard, or changes their plane. The
cupboard handler displays its discovery message, then returns on a failed
`npc_find`; state remains 7. Even if the engine's radius search ignored plane,
the Ceril spawn is farther than the requested radius from the cupboard area.
This must be proven with a runtime test, but the static ownership graph has no
valid scene path.

### Hazeel branch

| Phase | Canonical transition | Current behavior / defect |
| --- | --- | --- |
| Poison | Use poison on the basement range; poison is consumed and Scruffy dies | Primary advances to 5 and poison is deleted; dog/grave state never changes, so the world does not reflect the outcome |
| Confirm result | Talk to Ceril to learn Philipe fed the meal to Scruffy | Dialogue exists, but writes no `poison_success`; Clivet accepts the result immediately, so Ceril can be skipped |
| Mark | Clivet gives Hazeel's mark with full-slot retry behavior and records delivery | Inventory-only duplicate check; unchecked add; no full-slot message; banked/worn marks can duplicate; native delivery bit is untouched |
| Meet Alomone | Reach hideout, meet noncombat Alomone, learn of scroll/Jones, advance to 6 | Dynamic owner-private variant and primary transition work; native met/visibility state is untouched |
| Key | Talk to Jones, search basement crate; full inventory preserves key | Jones conversation is not required by code; crate ignores bank/ground ownership and unchecked add narrates success when full |
| Secret room | Knock wall, climb ladder, unlock chest with retained key | Broad access exists from state 6; wall is a direct teleport with no animation; key is correctly retained |
| Scroll | Search chest; full inventory preserves scroll and state; destroyed scroll can be replaced | Unchecked add still advances state 6 to 7, so it can claim delivery after failure; inventory-only duplicate check permits bank duplicates |
| Ritual | Give scroll, witness private Hazeel scene, consume scroll, complete | Scroll remains owned through the whole ritual and is deleted only by the later completion queue; native hand-in/cutscene markers are absent |
| Completion | Settle once after the cutscene | Hazeel is owner-private, but completion is queued independently of native ritual state and is not interruption/replay safe |

The implementation's ideal Hazeel path is traversable, but it is permissive:
Ceril's poison-result checkpoint, Jones's clue, Hazeel's mark, and several
presentation transitions are not gates. Failed adds can produce a misleading
primary state, and a banked temporary item can create duplicates because most
handlers inspect only the inventory.

## 5. Detailed lifecycle audit

### Dialogue, branch commitment, and actor presentation

Rebuild dialogue from the pinned current transcript, including the generic
quest offer, combat recommendation, Clivet's question loop, “need more time”
paths, branch labels, and state-specific re-talks. The current scripts preserve
the story outline but are visibly an older revision: wording, choice topology,
speaker sequence, and several current re-talks differ. Henryeta appears in the
current transcript but has no Hazeel Cult handler. Dialogue is part of the
state contract because it controls when branch choice, poison delivery,
Alomone combat, Jones disclosure, and completion become committed.

Restore the cache's authoritative static actor shells. Drive Clivet,
Alomone, Jones, Scruffy, and the grave with the native support bits so each
player sees the correct form without public deletion. Use a scoped cutscene
controller for Clivet's departure, Jones's arrest, and Hazeel's resurrection.
Cutscene actors may be private copies, but the scene must revalidate the player
state after every yield and settle a durable checkpoint before presentation is
dismissed. Never use a broad `npc_find` as the prerequisite for a permanent
quest transition.

The current dynamic spawn approach has one good intent—one player's Alomone
fight or arrest must not alter another player's quest—but it resolves the
already-multivar actors to raw variants and therefore disconnects client state.
Its “find, then add, then set owner” order also needs a multiplayer proof:
verify that an existing other-player-owned actor cannot suppress the caller's
spawn and that a public/static actor cannot be selected or deleted.

### Valves, raft, cave, and shared topology

Give each valve a placement-aware owner that can inspect and toggle its actual
left/right state. Persist the state per player if that is the current OSRS
contract; otherwise capture and reproduce the precise reset scope. Preserve
the correct object-to-direction mapping and the one-, two-, three-, and
four-island outcomes. The raft must refuse pre-authorized travel with Clivet's
current dialogue, lock the player during movement, and arrive at exact safe
tiles. Interrupted travel, re-login, and clicking another raft placement must
not skip islands or strand the player.

Cave entry itself may remain available before quest start because current
dialogue handles that visit. Hideout access may not. The return raft should be
placement-aware rather than treating every non-start raft as the same object.
The secret wall, ladder, basement, cupboard, chest, and later Secrets of the
North variants share geometry; register explicit state windows and let the
later quest override only during its own valid phase.

### Items and transaction boundaries

Every quest-item hand-off needs the same transaction pattern:

1. Revalidate quest state, branch, actor/loc placement, and ownership after
   the last dialogue or delay.
2. Check all relevant ownership domains: inventory, worn, bank, and
   owner-private ground item. Temporary items may intentionally use a narrower
   domain only when the current recovery contract says so.
3. Preflight capacity and stacking. Emit the pinned full-inventory message and
   leave progress unchanged when delivery fails.
4. Add/delete the item and verify the result.
5. Only then write the native delivery milestone and any primary transition.

Apply this to poison, Hazeel's mark, Carnillean armour, the chest key, Hazeel
scroll, five fake-ending coins, and 2,000 completion coins. Preserve these
specific canonical rules:

- poison and mark can be retried before their delivery markers settle;
- the chest key is not consumed when opening the chest;
- a destroyed/lost key or scroll can be recovered while its quest step still
  requires it;
- the armour chest supports the drop trick until Ceril accepts one set;
- armour and mark are missable one-time branch items and never postquest
  replacements;
- Hazeel scroll is consumed when Alomone accepts it, before the ritual becomes
  externally complete;
- a full inventory never advances the scroll milestone or claims a key/mark
  was received.

`~obj_gettotal(carnillean_armour)` is a useful start for the drop trick because
it appears intended to cover more than inventory. Its exact domains and
owner-private ground semantics must be tested. The other item handlers should
use one shared ownership/recovery helper rather than inventing different
inventory-only rules.

### Alomone combat and death ownership

Keep Alomone at the native level-13 stats and one-bones modern drop. Present
the one-option form until the Ceril-route confrontation finishes, then use the
native attackable form. The fight must be player-owned or instance/scoped,
with damage attribution, death, state transition, and chest permission tied
to that owner. A second player must not attack, kill, suppress, or inherit the
encounter. Leaving, death, logout, and re-entry need explicit reset rules.

Do not publish owner quest loot through an unconditional public `obj_add`.
Bones may follow normal loot visibility rules, but ownership and duration must
match the engine's standard drop pipeline. On death, atomically write the
native Alomone defeated/chest-searchable state once. Remove the second queued
state callback unless it is documented as a necessary death-engine hand-off;
if retained, make it idempotent and bind it to the player's encounter token,
not merely an NPC UID that may already be deleted.

### Cutscenes and completion settlement

The Ceril fake ending and the two real endings are three distinct durable
boundaries:

- Ceril armour hand-in: delete one armour, grant five coins, write
  `given_armour`, and enter state 7 exactly once;
- Ceril evidence/arrest: stage private actors, write Jones cutscene/location,
  then invoke real settlement;
- Hazeel ritual: consume scroll, write given-scroll and resurrection markers,
  then invoke real settlement after the scene can safely resume or skip.

Replace the current completion queue, which writes state 9 before deleting the
scroll, awarding XP, adding coins, and incrementing QP. Use a replay-safe
settlement record with independent durable markers for route precondition,
temporary-item consumption, 1,500 Thieving XP, 2,000 coins, one QP, completion
count/journal, and presentation. Re-entry should finish only missing
components. State 9 is the final publication step, not the first operation.

Do not make currency delivery depend on a free slot. Use the engine's standard
quest reward policy: stack where possible and use the canonical safe fallback
if a new stack cannot be inserted. Never silently lose either the five coins
or the 2,000 coins while showing a reward screen.

## 6. Recovery and downstream consumers

| Situation | Required recovery | Current behavior / gap |
| --- | --- | --- |
| Full inventory at poison | Stay on branch step; Clivet reports failure and retries | State/branch advances; add fails silently; re-talk happens to retry |
| Lost poison before range | Clivet replaces one while poison is still required | Inventory-only retry exists; banked poison can duplicate; no native marker |
| Full inventory at mark | Do not settle delivery; report and retry | Add fails silently; state 5 remains, but no durable item milestone |
| Skip Ceril after poison | Clivet must wait for result confirmation | Clivet accepts immediately because `poison_success` is unused |
| Death/logout during Alomone | Owner encounter resets/resumes without cross-player credit | Dynamic duration spawn has no explicit encounter/recovery contract |
| Full inventory at armour | Leave chest searchable | Correct explicit check; native found state absent |
| Lost/dropped armour before hand-in | Recover via chest according to ownership/drop-trick rules | Global helper may support this; exact domains untested |
| Interrupt fake ending | Armour and five coins settle once; evidence remains | Queue has no marker and can lose/repeat currency depending on interruption |
| Interrupt evidence scene | Resume from a durable scene checkpoint | Scene depends on nearby actors and has no cutscene checkpoint |
| Full inventory at key/scroll | Report failure; do not advance; retry | Both narrate success after unchecked adds; scroll advances to state 7 |
| Destroy key/scroll | Canonical re-obtain path while still needed | Inventory-only source checks permit retries but also bank duplicates |
| Interrupt ritual | Scroll and resurrection settle exactly once; resume/skip presentation | Scroll remains until state-first completion; no native scene marker |
| Interrupt real completion | Finish missing XP/coins/QP/state once | No settlement marker; state 9 can permanently hide missing rewards |
| Historian Minas | Award five Kudos once after completion | No Hazeel Cult claim implementation found |
| Secrets of the North | Require completion; reconcile both choices; Hazeel is awake either way | Current quest is a broad soft-skip and does not consume route/support state |
| Admin completion | Establish coherent route-neutral or selected fixture and settlement | Cheat writes only state 9 |

Current Secrets of the North behavior is not acceptable downstream evidence.
Its script explicitly labels the Hazeel-cult segment a soft-skip, advances
through placeholder dialogue, and does not read `%hazeelcult_side`, varp 223,
or the native secondary bits. Current OSRS awakens Hazeel regardless of the
old choice but changes the introduction: Alomone summons him for a Hazeel-side
player, while a player who killed Alomone goes directly to Clivet and Hazeel.
Modernization must preserve that reconciliation and test shared actor and
bookcase ownership across both quests.

The museum integration needs its own durable claim bit or existing generic
quest-history entry. Completion itself should not silently add Kudos if the
canonical action is talking to Historian Minas. Test first claim, repeated
claim, imported completed saves, and admin-completed saves.

## 7. Modernization sequence

### Phase 0 — capture, ownership map, and save safety

1. Capture exact native primary/support writes for both routes, all recovery
   cases, and all yielded scenes on current OSRS.
2. Inventory every actor and loc placement in the mansion, cave, islands, and
   hideout, including Hazeel Cult and Secrets of the North transforms.
3. Document standard engine helpers for private actors, cutscene staging,
   private ground items, transactional quest rewards, and settlement markers.
4. Add a versioned reconciliation reader for authored side/valves and native
   support state before changing live scripts.
5. Freeze current-state fixtures for undecided, every primary value on both
   routes, locally stranded state 7, and partially settled state 9.

Exit: captured evidence resolves every native bit/value used by the rewrite,
and migration is deterministic for all known local save shapes.

### Phase 1 — native state, dialogue, and transactions

1. Make varp 223 plus `hazeelcult_secondary` authoritative; stop new writes to
   authored side/valves.
2. Restore static multivar actor/loc shells and exact placement ownership.
3. Port current dialogue topology and generic quest-offer behavior.
4. Implement physical valve state, correct raft gating, and placement-aware
   travel.
5. Replace poison, mark, armour, key, and scroll operations with checked,
   recoverable transactions.

Exit: both branches reach their pre-completion checkpoint with native support
state, truthful presentation, and no false-success item transitions.

### Phase 2 — owned combat and resumable cutscenes

1. Implement Alomone's noncombat-to-attackable transform and owner-bound fight.
2. Stage Clivet departure, Jones arrest, and Hazeel resurrection with scoped
   actors and durable cutscene checkpoints.
3. Drive Scruffy/grave and Jones location from native fields.
4. Add logout, death, region-leave, and multiplayer recovery for each scene.

Exit: no broad NPC search/delete controls quest progress, and concurrent
players can occupy opposite branches without influencing one another.

### Phase 3 — atomic completion and consumers

1. Implement replay-safe fake and real settlement records.
2. Migrate stranded/partially rewarded saves without granting missable branch
   items.
3. Implement Historian Minas's five-Kudos claim.
4. Replace the Secrets of the North Hazeel-cult soft-skip with route-aware
   reconciliation and enforce Hazeel Cult as a prerequisite.
5. Modernize journal and admin fixtures to use the same state/settlement model.

Exit: both routes settle all rewards exactly once, and downstream content
observes a coherent canonical history.

## 8. Required tests

### State, migration, and dialogue

- Fresh state 0 shows the correct start journal and current quest offer.
- Accept/refuse and low-combat warning do not mutate state prematurely.
- Every Clivet question/defer choice resumes at the correct decision state.
- Branch choice writes exact captured native Clivet/support values.
- Imported native Hazeel-route saves are not interpreted as authored side 0.
- Every authored side/valve fixture migrates deterministically.
- Contradictory native/authored fixtures are quarantined or reconciled by the
  documented precedence rule.
- Journal text is correct for every primary value and both branches without
  using item possession as a substitute for settled support state.

### Valves, raft, and mansion topology

- Each physical valve reports and toggles its actual direction.
- `sewervalve3` left and the other four right produces the solved route.
- Zero and each one-to-four correct prefix reaches the canonical outcome.
- State 0, 2, and 3 cannot use the raft to bypass Clivet authorization.
- Return raft placement returns safely; intermediate placements cannot spoof
  the entrance or final hideout.
- Re-login during raft movement resumes or safely restores the source state.
- Secret wall, ladders, cupboard, key chest, and shared SOTN variants dispatch
  only in their valid windows.

### Ceril branch and combat

- Refusing Clivet transforms only that player's Clivet.
- Alomone begins noncombat and becomes attackable only after confrontation.
- Two players on opposite branches see independent Alomone forms.
- A non-owner cannot damage, kill, or receive credit/loot.
- Death/logout/leave before and during combat resets or resumes canonically.
- Owner kill drops exactly one bones and unlocks only the owner's chest.
- Full-inventory armour search does not mutate state; retry works.
- Drop trick yields another set only before the hand-in milestone.
- Worn and inventory armour hand-in each delete exactly one set.
- Fake ending grants exactly five coins once across every interruption point.
- Cupboard evidence always stages Ceril, Jones, and guard without relying on
  ambient actors; relog at every line resumes safely.
- Jones disappears/is jailed only for the correct player and the route reaches
  real completion.

### Hazeel branch and item recovery

- Full-inventory first poison delivery emits the canonical failure and retries.
- Banked/ground poison cannot create unauthorized duplicates.
- Using poison consumes exactly one and updates Scruffy/grave presentation.
- Clivet refuses to accept success until Ceril's confirmation bit is set.
- Full-inventory mark delivery does not settle; retry grants exactly one.
- Alomone's first conversation writes met state and advances once.
- Jones clue is available at the correct checkpoint.
- Full-inventory key and scroll searches do not advance and can be retried.
- Key use does not consume the key; destroyed key/scroll recovery is canonical.
- Banked key, scroll, or mark cannot duplicate.
- Scroll hand-in consumes exactly one before the ritual marker settles.
- Relog/interrupt at every ritual yield neither duplicates Hazeel nor loses
  completion.

### Completion and downstream consumers

- Each route awards exactly 1,500 Thieving XP, 2,000 coins, one QP, and one
  completion count.
- Ceril receives five additional coins; Hazeel does not.
- Full inventory cannot silently lose either currency grant.
- Failure injected after every settlement operation resumes missing work once.
- State 9 is published only after durable reward settlement.
- No completion/recovery path postquest grants Hazeel's mark or Carnillean
  armour.
- Historian Minas grants five Kudos once, including migrated and admin saves.
- Secrets of the North start requires Hazeel Cult completion.
- Both old branches produce their correct SOTN introduction and converge on an
  awakened Hazeel without resurrecting/killing Alomone incorrectly.
- Quest cheat fixtures establish explicit branch/support/settlement shapes and
  never masquerade a state-only write as normal completion.

## 9. Acceptance evidence

Gate A requires a state/ownership table from fresh, migrated, interrupted, and
admin fixtures; exact primary/support writes from current OSRS; and a source
map for every actor, loc, item, and downstream owner.

Gate B requires automated route traces for both branches, all valve prefixes,
every full-inventory and lost-item recovery, Alomone death/re-entry, both
cutscenes, fake completion, and real settlement. Each trace must assert native
state and inventory/actor presentation after every transition.

Gate C requires two-player tests with opposite branches in the same mansion
and sewer, plus logout/death/interruption injection during Alomone, Jones's
arrest, Hazeel's ritual, and each completion operation. No public NPC deletion,
shared fight credit, duplicate route item, or lost reward is acceptable.

Gate D requires a pinned-reference review against the article, quick guide,
and transcript; exact reward and metadata checks; journal/admin parity;
Historian Minas proof; and both route-sensitive Secrets of the North hand-offs.
Record the current Wiki revision IDs and the Quest Helper commit in the test
artifact.

The dossier may move to `modernized` only when all four gates have checked
evidence. A compiling script, an ideal Hazeel-route reward scroll, or a manually
set state 9 is not completion evidence.

## 10. Prioritized findings

### P0 — reachability, state, and settlement integrity

1. Ceril completion depends on upstairs `npc_find` calls for actors only
   authored downstairs; the evidence scene returns and leaves state 7.
2. Real completion writes state 9 before scroll deletion, XP, coins, QP, and
   completion presentation, with no replay-safe settlement marker.
3. Nineteen bits of native secondary state are entirely unused; two authored
   permanent varps create an incompatible parallel quest model.
4. Key, scroll, mark, and poison adds are unchecked; the scroll can advance the
   primary state after failed delivery, and banked items can duplicate.
5. The Hazeel route can skip Ceril's poison-result confirmation because
   `hazeelcult_poison_success` is never written or checked.
6. Fake-ending coins and armour hand-in have no durable once-only transaction.
7. Current state-9 saves may be partially rewarded and cannot be repaired from
   state alone.

### P1 — canonical route, ownership, and recovery

1. The raft can be used before Clivet authorizes pursuit, including by a
   not-started player who pre-solves the valves.
2. Ceril-route Alomone spawns attackable before the canonical confrontation
   transforms him.
3. Dynamic resolved NPC variants bypass native Clivet, Alomone, Jones, dog,
   grave, and cutscene presentation.
4. Broad actor searches/deletion and “find before set owner” spawning require
   multiplayer safety proof.
5. Scruffy never disappears and the grave never appears after poisoning.
6. Valve direction, raft movement, secret-wall traversal, and cutscenes are
   collapsed to server-only correctness flags and teleports.
7. Current dialogue is an older LostCity topology and omits current choices,
   re-talks, full-slot text, Henryeta, and the quest-offer warning.

### P2 — downstream completeness and diagnostics

1. Secrets of the North soft-skips the cult segment and ignores the old route,
   native support state, and canonical introduction reconciliation.
2. No Hazeel Cult Historian Minas five-Kudos claim owner was found.
3. Journal text depends on authored side/item presence and cannot represent
   imported or partially settled native states reliably.
4. The quest cheat publishes state 9 without route, support, items, rewards, or
   settlement history.
5. There are no automated Hazeel Cult route, transaction, multiplayer,
   migration, or downstream tests.

## 11. Evidence still required before implementation

- Current-server captures of all nineteen support fields for both routes,
  especially exact multi-value Clivet, Alomone, Jones, and dog states.
- Exact valve persistence/reset behavior and the relationship between physical
  directions and any hidden server fields.
- Engine proof of `npc_find` plane/radius and owner filtering, confirming the
  static Ceril blocker and multiplayer spawn risk.
- Exact private/public bones visibility for an owner-bound Alomone kill.
- Exact destroy/replacement rules for poison, mark, key, scroll, and armour
  across inventory, bank, worn, death, and private ground domains.
- Canonical interruption behavior for Clivet departure, Jones arrest, Hazeel
  resurrection, fake completion, and the real reward screen.
- The repository's intended generic quest-currency fallback when inventory is
  full and no coin stack exists.
- Historian Minas's canonical claim bit/table and how imported completions are
  recognized.
- Exact Secrets of the North actor/location sequence for both Hazeel Cult
  outcomes, including Alomone's absence on the Ceril route.
