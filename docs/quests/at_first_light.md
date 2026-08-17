# At First Light modernization audit

Status: `audit-pending` — the native 0–12 state model, journal, basic dialogue
route, quest objects, reward XP, and Master-tier Hunters' Rumours gate exist.
The legitimate route is nevertheless blocked at state 3 because Kiko has no
production spawn; the injured/healthy Fox and Atza are also absent. In
addition, quest-specific stair handlers override the Hunter Guild's shared
travel route and stop working before and after the quest. The remaining route
uses explicit soft skips for jerboa hunting and Atza's equipment repair,
consumes the wrong poultice ingredients, skips state 6, and permits completion
without repairing the cat bed.

Audited: 2026-08-16

Governing plan: [Quest modernization plan](../QUEST_MODERNIZATION_PLAN.md). This
record applies that plan's Gates A–D to the quest root, Hunter Guild world
spawns and stairs, box traps and embertailed jerboas, shared Hunters' Rumours
dispatch, item recovery, native transform bits, journal, and rewards. It is an
implementation specification, not completion evidence.

## 1. Authoritative references

These stable OSRS Wiki revisions define the required route, dialogue, item
lifecycle, rewards, and post-quest service.

| Reference | Pinned revision | Use in this audit |
| --- | --- | --- |
| [At First Light](https://oldschool.runescape.wiki/w/At_First_Light?oldid=15271675) | 15271675, 2026-07-22 | Identity, requirements, complete route, items, skill actions, rewards, and Master-rumour unlock |
| [At First Light/Quick guide](https://oldschool.runescape.wiki/w/At_First_Light/Quick_guide?oldid=15213493) | 15213493, 2026-05-20 | Ordered interactions, two-tail preparation, tool alternatives, and route checkpoints |
| [Transcript:At First Light](https://oldschool.runescape.wiki/w/Transcript%3AAt_First_Light?oldid=15104140) | 15104140, 2026-01-12 | Start choices, re-talks, Kiko branches, item recovery, full-inventory behavior, report text, and finale |
| [Hunters' Rumours](https://oldschool.runescape.wiki/w/Hunters%27_Rumours?oldid=15290612) | 15290612, 2026-08-08 | Rumour tiers, Wolf's Master-tier role, task service, and shared guild behavior |
| [Hunter Guild](https://oldschool.runescape.wiki/w/Hunter_Guild?oldid=15166362) | 15166362, 2026-04-06 | Guild layout, NPC roles, entrances, facilities, and quest relationship |

The sources identify the quest as number 162, released 20 March 2024. It is a
short, intermediate, members' quest with no combat. Completion awards one
quest point, 4,500 Hunter XP, 800 Construction XP, 500 Herblore XP, and access
to Master-tier Hunters' Rumours from Wolf.

Transition aid only: the local Quest Helper checkout's
[`AtFirstLight.java`](https://github.com/Zoinkwiz/quest-helper/blob/5ea99d5ea9ba3fb096ebe7b5ed02d80883e9819d/src/main/java/com/questhelper/helpers/quests/atfirstlight/AtFirstLight.java)
at commit `5ea99d5ea9ba3fb096ebe7b5ed02d80883e9819d` confirms the 0–11
active checkpoints, native side bits, relevant NPC/loc carriers, two-tail
route, and stair coordinates. It guides transition tests but does not override
the Wiki, transcript, or osrs239 cache.

`python3 tools/questhelper_extract.py atfirstlight --check` resolves every
named symbol and `quest_atfirstlight` in that helper.

## 2. Native quest identity and player contract

The cache-native `quest_atfirstlight` row and pinned sources define this
contract:

| Field | Native value / expected behavior |
| --- | --- |
| Cache quest ID | 191; OSRS release-order number 162 |
| Type | Members' quest |
| Difficulty / length | Intermediate / short |
| Series | None |
| Release date | 20 March 2024 |
| Start / finish | Guildmaster Apatura at the Hunter Guild |
| Prerequisites | Children of the Sun and Eagles' Peak complete |
| Required levels | 46 Hunter, 30 Herblore, and 27 Construction; all non-boostable and required to start |
| Required items | Needle or costume needle; hammer or Imcando hammer; two embertailed jerboa tails, or a box trap to catch them |
| Supplied locally | A normal needle and hammer can be collected along the route |
| Combat | None |
| Primary state | `%afl`, cache varbit on `afl_main`, bits 0–4, values 0–12 |
| End state | 12 |
| Quest points | 1 |
| XP rewards | 4,500 Hunter, 800 Construction, and 500 Herblore |
| Unlock | Master-tier Hunters' Rumours from Wolf, requiring 91 Hunter to use |

The dbrow's two prerequisite references are pack-row references, not the
quest rows' internal `id` fields. Pack rows `3450` and `37` correctly resolve
to `quest_childrenofthesun` and `quest_eaglespeak`; they must not be
"corrected" by decoding them through the unrelated internal ID column.

The configured XP constants, `45000`, `8000`, and `5000`, are correct because
`stat_advance` uses tenths. The constant file's comment calling this an
"un-tenthed mismatch" is stale and should be replaced with an unambiguous unit
statement.

## 3. Implementation surface

Paths below are relative to `OSRS-Content/osrs239-content/`.

### Quest-owned files

| Path | Present responsibility | Audit result |
| --- | --- | --- |
| `server/scripts/quests/quest_atfirstlight/configs/atfirstlight.constant` | Primary states, route coordinates, and XP constants | State and XP values match cache/Wiki; stair destinations are NPC-adjacent shortcuts rather than canonical landings, and the XP comment is wrong |
| `server/scripts/quests/quest_atfirstlight/configs/atfirstlight.varp` | Permanent cache carrier declaration | Correctly exposes native `afl_main`; no new quest varp is needed |
| `server/scripts/quests/quest_atfirstlight/scripts/atfirstlight.rs2` | Completion, journal, all NPC/loc/item triggers, and debug runner | A 408-line monolith containing the full route plus shared-NPC/stair overrides and deliberate soft skips |

The root totals 444 lines across three files. It is a route sketch connected
to real native state, not a faithful playable implementation. Splitting it by
start/guild, Fox/Atza route, items, journal, and completion will make shared
dispatch and transaction review tractable, but file count alone is not a
modernization goal.

### Mandatory shared and cross-directory surfaces

| Path / subsystem | Relationship | Audit result / required ownership |
| --- | --- | --- |
| `server/scripts/interface_questjournal/scripts/quest_journal.rs2` | Dynamic quest-list dispatcher | Calls the local journal; preserve the registry and expand state/substate guidance |
| `server/scripts/quests/scripts/quest_cheat.rs2` | `::complete` adapter | Writes state 12 only; useful for registry checks, never evidence for XP or service unlock delivery |
| Hunter Guild world spawn configs | Apatura, Verity, Wolf, Kiko, Fox, and Atza carriers | Apatura, Verity, and Wolf exist; no production spawn was found for Kiko, either Fox transform carrier, or Atza |
| `server/scripts/ladders_stairs/` | Hunter Guild surface/Burrow travel | Down stair has shared maplink rows, but the quest's name trigger wins dispatch; up stair needs a canonical shared reverse mapping |
| `server/scripts/skill_hunter/scripts/box_trap.rs2` and `configs/box_trap.dbrow` | Real jerboa capture | Owner-scoped box-trap machinery and the jerboa data row exist: level 39, 137 XP, one tail; no production jerboa habitat spawn was found |
| `server/scripts/skill_hunter/scripts/hunter_rumours.rs2` | Ordinary and Master rumours | Substantive service exists; Wolf's Master service is gated by `%afl >= 12`, Hunter level 91, and master hunter ID 6 |
| Hunter cape and milestone dispatch on Apatura | Shared NPC subjects | Both run ahead of the quest branch; subject arbitration is implicit and can consume the first click |
| Verity and Wolf rumour dispatch | Shared NPC subjects | Verity handles ordinary rumours; Wolf handles Master rumours after completion. Quest re-talk/post-quest subjects need deliberate coexistence |
| World needle and hammer spawns | Required tools | Normal needle at the guild and hammer near Atza exist; retain them and accept costume needle/Imcando hammer alternatives |

### Cache-native content already available

The cache contains more authored structure than the script uses:

- `afl_main` provides the primary state plus native side bits for report
  delivery, bed inspection, bed repair, Kiko distraction, equipment state,
  Master introduction, fur-sample receipt, mouse receipt, and Wolf dialogue;
- `afl_hunter_fox_multi` transforms from hidden to injured Fox through the
  active route and hides again at state 11;
- `afl_hunter_fox_normal` is hidden during the route and becomes the healthy
  post-quest Fox at states 11–12;
- Kiko and Atza have live NPC definitions, and the cat bed and Atza equipment
  pile have state-aware loc carriers;
- leaves, poultice, fur sample, trimmed fur, and report have quest-object
  definitions with Destroy operations, while the report also has Read; and
- dedicated Hunter Guild assets and shared Hunter mechanics exist outside the
  quest root.

Modernization should spawn and bind these native carriers and use their
transforms. It should not replace them with ad-hoc NPC additions, new parallel
varps, or quest-local fake Hunter interactions.

## 4. Native state model and current reachability

The cache, Wiki route, and Quest Helper agree on this primary sequence:

| State | Required phase | Current implementation / defect |
| ---: | --- | --- |
| 0 | Apatura offers help; requirements shown; explicit Yes/No acceptance | Clicking Apatura automatically starts after one short exchange; no prerequisite, base-level, or confirmation gate |
| 1 | Descend into the Burrow and ask Verity about the missing Fox/report | Dialogue exists; quest-specific stairs send the player to Verity rather than using the shared landing |
| 2 | Ask Wolf about Fox; receive the unwound toy mouse | Capacity check and native mouse-received bit exist; dialogue and shared subject arbitration are compressed |
| 3 | Wind mouse, distract Kiko, inspect bed, return to Wolf | **Blocked:** Kiko is not spawned. Mouse-on-bed, Pet/Catspeak dialogue, correct inspection story, and several recovery branches are absent |
| 4 | Find the injured Fox near pyre foxes and learn the poultice recipe | **Blocked:** neither injured Fox carrier is spawned |
| 5 | Pick both leaves, catch tails with box traps, make and give poultice | Recipe consumes two tails instead of one; jerboa habitat is absent; fake tail grant abuses Atza's equipment loc |
| 6 | Speak to the recovering Fox and receive a fur sample | Skipped: poultice hand-in directly grants sample and writes state 7; native sample bit is unused |
| 7 | Bring the sample to Atza | **Blocked:** Atza is not spawned; dialogue writes state 8 but neither consumes/records the sample nor exposes the equipment pile correctly |
| 8 | Repair Atza's equipment pile with either supported hammer | No repair action exists; an always-true dialogue condition soft-skips it and grants trimmed fur |
| 9 | Return trimmed fur to Fox; receive report and retain the fur | Sketch exists, but item ownership/recovery is inventory-only and the healthy/injured carrier transition is not represented in world spawns |
| 10 | Give report to Verity, then repair Kiko's bed with trimmed fur, one tail, and needle | Verity prematurely writes state 11; bed consumes only a tail, ignores trimmed fur, and can therefore be skipped |
| 11 | Return to Apatura | Completion checks neither report nor repaired-bed side bits and immediately sets state 12 |
| 12 | Complete; Wolf offers Master-tier rumours | XP/scroll and direct rumour gate exist; state is committed before the reward transaction is proven complete |

### Native side-bit meanings

All of these fields share `afl_main` and should remain the authoritative
substate model:

| Field | Bit range | Required meaning / current use |
| --- | ---: | --- |
| `%afl_report` | 5 | Verity received Fox's report; written, but primary state is advanced too early |
| `%afl_bedcheck` | 6 | Initial bed inspection completed; written correctly after Kiko distraction |
| `%afl_bedrepair` | 7 | Final bed repair completed; written by an incomplete recipe and not enforced at completion |
| `%afl_catdistract` | 8 | Kiko is distracted; written on mouse use, but reconstruction/relog policy is absent |
| `%afl_housetrapped` | 9–10 | 0 hidden/no-op, 1 equipment ready to repair, 2 repaired; misused as a jerboa soft-skip and never reaches native repaired state |
| `%afl_mastermet` | 11 | Master-rumour introduction state; unused by the quest |
| `%afl_sampletaken` | 12 | Fur sample received/recovery state; unused because state 6 is skipped |
| `%afl_mousetaken` | 13 | Wolf supplied the quest mouse; used |
| `%afl_wolf_dialogue` | 14 | Wolf conversation substate; unused |

### Deterministic NPC reachability blocker

Repository-wide spawn and trigger searches found no production placement for
`afl_kiko`, `afl_atza`, `afl_hunter_fox_multi`, or
`afl_hunter_fox_normal`. Kiko is the first required target at state 3, so the
route stops there. The Fox and Atza omissions would block states 4 and 7 even
if a player bypassed Kiko. Add canonical persistent carrier spawns, including
both Fox transform carriers at their authored coordinate; do not spawn only
the currently visible leaf type or the post-quest form will not transform
correctly.

### Shared stair regression

Name triggers take precedence over category/maplink handlers. The local
`[oploc1,hunterguild_stairs_down01_combined]` and
`[oploc1,hunterguild_stairs_up01]` therefore intercept the shared Hunter Guild
route. They teleport only while `%afl` is 1–11 and otherwise return without an
action, breaking ordinary Burrow access at states 0 and 12. The down stair
already has two maplink rows for its two tiles, but the override teleports to
Verity's coordinate rather than the stair landing. Replace the overrides with
shared bidirectional maplink behavior, add the missing canonical reverse row,
and test both stair tiles before, during, and after the quest.

## 5. Current versus required playable route

| Chapter | Current route | Modernized route contract |
| --- | --- | --- |
| Start | Apatura runs cape/milestone hooks, then auto-starts without checks | A shared subject dispatcher exposes quest/cape/milestone topics, shows both prerequisite quests and base stats, supports help/Yes/No/re-talk, and commits state 1 only on acceptance |
| Verity and Wolf | One line each; Wolf gives mouse | Preserve ordinary-rumour subjects, introduce rumours as authored, use native dialogue bits, capacity-safe mouse delivery, and correct lost-mouse explanation |
| Kiko and bed inspection | Wind held mouse, use on missing Kiko, click bed | Spawn Kiko; support winding prompt/animation, wound mouse on Kiko or bed, Pet and applicable Catspeak branches, stable distraction recovery, and identify the damaged fur section |
| Injured Fox | One-line recipe | Spawn transform carriers; reproduce re-talks and recipe explanation; use state 6 for post-treatment dialogue and sample delivery |
| Leaves and jerboas | Full inventory refuses leaves; equipment pile invents tails | Full inventory drops picked leaves to ground; populate the canonical jerboa habitat and use the shared player-owned box-trap lifecycle and real catch rolls |
| Poultice | Two leaves plus two tails | Two leaves plus exactly one tail, leaving the second of the prepared tails for the bed; atomic use-on conversion and correct wrong/missing-item feedback |
| Fur sample | Granted immediately with poultice and state skips to 7 | State 5 treatment commits state 6; follow-up grants the sample capacity-safely, records `%afl_sampletaken`, then commits state 7 |
| Atza equipment | Dialogue's `>= 0` condition always succeeds and grants trim | Sample handoff exposes pile at bit 1; hammer/Imcando hammer action repairs it and sets bit 2; Atza then supplies trimmed fur with full-inventory/recovery handling |
| Fox report | Inventory-only check trades no trimmed fur | Fox receives/inspects the work, supplies a readable report, and the player retains trimmed fur for bed repair; exact replacement/full-inventory branches remain available |
| Report and bed | Verity writes state 11; bed needs only tail+needle | Report sets side bit while primary remains 10; bed requires and consumes trimmed fur plus one tail, retains either needle, sets repair bit, then alone advances to 11 |
| Completion | Apatura trusts primary state and commits it before rewards | Guard on authoritative side bits; deliver XP, quest point, presentation, state, and Master-rumour availability idempotently and exactly once |

## 6. Narrative, state, and lifecycle oversight matrix

| Priority | Oversight | Evidence / consequence | Required correction |
| --- | --- | --- | --- |
| Blocker | Kiko, Fox, and Atza carriers are unspawned | Required production NPC names occur only in quest scripts/cache data, not world spawn configs | Add canonical carrier spawns and assert every transformed state before route work |
| Blocker | Quest stair triggers shadow the shared travel system | Name handlers win and do nothing at states 0/12; ordinary guild and post-quest Master-rumour access break | Remove quest-only policy from the loc names and complete shared round-trip maplinks |
| Blocker | Requirements are not checked | Comment explicitly defers Children of the Sun; no prerequisite or level checks exist | Check both quest completions and unboosted Hunter 46, Herblore 30, Construction 27 before offer acceptance |
| Critical | Poultice deletes two tails | Wiki requires one tail for poultice and one later for bed | Consume exactly one; verify the canonical two-tail end-to-end route |
| Critical | Jerboa acquisition is fake and likely unreachable | Real trap row exists, habitat spawn does not; Atza pile handler invents tails | Populate habitat and route through shared owner-scoped box traps; delete fake grant |
| Critical | State 6 is bypassed | Treatment writes 7 and grants fur immediately | Restore Fox follow-up and `%afl_sampletaken` lifecycle |
| Critical | Equipment loc state is never established | Atza does not set bit 1; `>= 0` is always true; hammer is ignored | Implement 0/1/2 native transform lifecycle and both supported hammers |
| Critical | Bed repair is optional and incomplete | Verity writes 11; Apatura completes; handler ignores trimmed fur | Keep state 10 through report, require exact three-part repair contract, gate completion on repair bit |
| High | Shared NPC subject order is accidental | Cape/milestone/rumour procs can consume Talk-to and post-quest quest dialogue disappears | Build explicit subject arbitration and regression-test every service combination |
| High | Item ownership is inventory-only | Banked sample, trim, or report can be duplicated; replacement branches do not share a policy | Define inventory/bank/ground ownership and idempotent free replacements at Fox/Atza |
| High | Full-inventory behavior differs from transcript | Leaves refuse instead of dropping; several grants silently defer or lose story sequencing | Use ground delivery where authored and explicit retry dialogue for sample/trim/report |
| High | Quest items advertise unsupported operations | Destroy is not bound for quest objects; report Read is missing | Implement destroy confirmation/loss policy and transcript-accurate report reading |
| High | Completion transaction is state-first | `%afl=12` precedes XP and presentation | Use an idempotent pending/claim pattern so reconnect cannot lose or duplicate rewards |
| Medium | Mouse/Kiko behavior is partial | No mouse-on-bed, Pet, Catspeak, animation, or correct lost-mouse explanation | Implement relevant transcript branches without making optional dialogue a route gate |
| Medium | Bed inspection tells the wrong story | It says there is no report rather than identifying damaged fur/cut sample | Restore the authored clue that motivates Fox and later repair |
| Medium | Dialogue is a synopsis | Choices, guild introduction, rumours explanation, re-talks, recovery, and finale are absent | Rebuild from the pinned transcript with state-safe checkpoints |
| Medium | Journal collapses several phases | Side bits, missing items, repair readiness, and recovery are not surfaced | Report the next legitimate action from primary plus native substate/item ownership |
| Medium | Debug runner is false evidence | `::aflrun` directly mutates every state and invents/deletes items without interacting with world systems | Retain only as a developer reset/registry aid or replace with assertions that use production handlers |
| Low | Completion icon is `coins` | Rewards contain no coins and the presentation obscures the actual unlock | Use an appropriate quest/reward presentation asset |
| Low | XP constant comment is misleading | Values already match tenths and Wiki amounts | Correct the documentation; retain values |

### Item lifecycle contract

| Item | Acquisition/use | Loss and replacement policy to implement |
| --- | --- | --- |
| Unwound/wound toy mouse | Wolf supplies one; player winds it; wound mouse distracts Kiko via Kiko or bed | Wolf does not replace a lost mouse; explain that another tradeable POH toy mouse can be obtained. Do not deadlock a player who brings one |
| Smooth/sticky leaves | Pick one of each during state 5 | With a full inventory, the picked leaf goes to ground. Destroy/drop/re-pick must remain safe |
| Jerboa tails | Catch two with box traps or bring/trade them | Poultice consumes one; final bed consumes one. Shared stackable-item and trap behavior applies |
| Makeshift poultice | Combine two leaves and one tail; give to injured Fox | Atomic conversion/hand-in; destruction returns player to recollection, not an advanced dead state |
| Fur sample | Fox supplies after treatment follow-up | Fox replaces it for free at the valid phase; full inventory produces explicit retry; bank ownership must prevent duplication |
| Trimmed fur | Atza supplies after the equipment repair | Atza replaces it for free at the valid phase; full inventory retry; it remains held after receiving Fox's report and is consumed by bed repair |
| Fox's report | Fox supplies after viewing trimmed fur | Fox replaces it for free before delivery; full inventory retry; Read shows authored text; Verity consumes it and sets the report bit |
| Needle / costume needle | Bring one or collect normal needle | Either satisfies bed repair and is retained |
| Hammer / Imcando hammer | Bring one or collect normal hammer | Either satisfies equipment repair and is retained |

## 7. Modernization implementation plan

### Wave 1 — restore shared-world reachability and the start gate

1. Add canonical persistent spawns for Kiko, Atza, injured Fox transform
   carrier, and healthy Fox transform carrier.
2. Assert each carrier's visible/hidden form across primary states 0–12.
3. Remove quest-owned stair name overrides, add the canonical up/down maplink
   pair, and preserve both down-stair tiles.
4. Create one shared Apatura/Verity/Wolf subject dispatcher that coexists with
   cape, milestone, ordinary-rumour, Master-rumour, quest, and post-quest
   dialogue.
5. Enforce both prerequisites and all three unboosted level requirements.
6. Implement authored offer/help/accept/refuse/re-talk dialogue and write state
   1 only after explicit acceptance.
7. Expand the journal for start-gate failures and exact next subject/route.

### Wave 2 — implement Kiko, Fox treatment, and real Hunter mechanics

1. Restore Wolf's introduction and native dialogue/mouse bits with
   capacity-safe one-time delivery and correct lost-mouse guidance.
2. Implement winding animation/transaction plus wound-mouse use on Kiko and
   the bed, Kiko Pet/applicable Catspeak dialogue, and reconnect-safe
   distraction state.
3. Correct bed inspection dialogue and require both distraction and the native
   inspection bit before Wolf advances to state 4.
4. Populate the canonical embertailed jerboa habitat and prove its existing
   box-trap dbrow through real owner-scoped trap placement, catch, failure,
   reset, XP, and tail collection.
5. Remove the `afl_housetrap_multi` tail grant; reserve that loc exclusively
   for Atza's equipment lifecycle.
6. Implement authored leaf pickup including ground fallback at full inventory.
7. Make the poultice from exactly one of each leaf plus one tail atomically.
8. Split treatment and sample receipt across states 5 and 6; use
   `%afl_sampletaken` and exact Fox recovery branches.

### Wave 3 — rebuild Atza, report, and bed transactions

1. Make Atza's sample handoff ownership-safe and reveal the equipment pile by
   setting `%afl_housetrapped=1`.
2. Bind the live pile use-on/action to normal and Imcando hammers, retain the
   tool, play the authored interaction, and commit bit 2 only on success.
3. Supply trimmed fur only after bit 2 with full-inventory retry and free,
   non-duplicating recovery.
4. Restore Fox's return dialogue, report grant, report Read, and the invariant
   that trimmed fur remains available for the bed.
5. Make report delivery consume only the report, set `%afl_report=1`, and keep
   primary state 10.
6. Implement bed repair as one atomic transaction requiring trimmed fur, one
   jerboa tail, and either retained needle; set `%afl_bedrepair=1`, then state
   11.
7. Add every quest item's Destroy/drop/reclaim behavior and test inventory,
   bank, and ground ownership.

### Wave 4 — narrative, completion, and Master-rumour integration

1. Rebuild Apatura, Verity, Wolf, Kiko, Fox, and Atza dialogue from the pinned
   transcript, including all choices, re-talks, recovery, and full-inventory
   responses.
2. Drive transforms and journal entries from the native primary/side state;
   do not infer permanent progress solely from current inventory.
3. Require state 11, report bit, and bed-repair bit at Apatura's completion
   commit; make XP, quest point, state, and presentation interruption-safe and
   exactly once.
4. Preserve the existing `%afl >= 12` Master-tier gate, Hunter 91 requirement,
   Wolf hunter identity, ordinary rumours, task settings, and post-quest
   dialogue through the shared dispatcher.
5. Replace `::aflrun` as completion evidence with automated route fixtures
   that invoke production handlers; keep any debug reset explicitly separate
   from acceptance testing.
6. Regression-test the Hunter Guild's stairs, cape/milestone dialogue,
   ordinary rumours, box traps, and area services for players at every quest
   state.

Do not add quest-specific C code for NPC transforms, maplinks, item use-on,
box traps, shared NPC subjects, ground delivery, or reward transactions. If a
general capability is missing, add the smallest reusable engine/service
primitive, prove it independently, and keep At First Light policy in
RuneScript/config data.

## 8. Verification contract

### Static and pack verification

- `python3 tools/questhelper_extract.py atfirstlight --check`;
- assert every `%afl` write belongs to 0–12 and each native side bit retains
  its cache transform meaning;
- assert Kiko, Atza, injured Fox carrier, and healthy Fox carrier each have one
  canonical world placement and expected visible forms at all states;
- assert both stair tiles and the reverse stair resolve through shared
  maplinks with no quest name-trigger override;
- assert no `Soft`, soft-skip tail grant, always-true equipment condition,
  direct debug completion claim, unchecked add/state pair, or inventory-only
  recovery assumption remains undisclosed;
- assert quest dbrow, journal dispatcher, end state, completion registry,
  prerequisites, reward amounts, and cheat adapter agree;
- assert the jerboa box-trap row resolves the intended NPC, level, XP, product,
  trap locs, and owner lifecycle;
- `make -C src mock230-scripts`; and
- `mock230_pack --check-only` against the intended cache.

### Automated route matrix

At minimum, test:

1. each prerequisite missing separately, Hunter 45/46, Herblore 29/30,
   Construction 26/27, boosts below the base threshold, drains above it,
   accept/refuse/help/re-talk, and each Apatura shared subject;
2. both down-stair tiles and the up stair at states 0–12, round-trip landing
   coordinates, simultaneous users, and every ordinary/post-quest guild
   service;
3. Verity/Wolf quest and rumour subject arbitration, zero/full inventory mouse
   delivery, repeated clicks, lost/banked/traded mouse, and a player-supplied
   replacement;
4. winding from both held operations, duplicate packets, Kiko versus bed use,
   unwound/wrong item, Pet/Catspeak branches, distraction, relog, and initial
   bed inspection before/after distraction;
5. Fox transform visibility and dialogue at states 0–12, all re-talks, both
   leaves, full-inventory ground delivery, drop/destroy/re-pick, and wrong
   bush phase;
6. shared box traps with no/one/multiple traps, bait-independent placement,
   jerboa success/failure/escape, trap ownership, full inventory, ground tail,
   XP boundary, relog, two simultaneous hunters, and rumour side effects;
7. poultice combinations in both use directions with zero/one/two tails,
   missing/wrong leaves, full inventory, duplicate packets, and proof that
   exactly one tail remains from the canonical two-tail preparation;
8. Fox treatment at state 5, separate state-6 follow-up, sample full inventory,
   destruction, bank, ground ownership, free replacement, and no duplicates;
9. Atza without sample, sample handoff, equipment transform 0/1/2, normal and
   Imcando hammer, missing/wrong/banked hammer, repeated repair, interruption,
   and trimmed-fur capacity/recovery cases;
10. Fox return with/without trim, report full inventory, Read/Destroy/drop/
    bank/replacement, retained trim, and correct injured-to-healthy transforms;
11. Verity delivery before report, duplicate packet, report consumption and
    side bit without primary advance, plus every report/rumour subject;
12. bed repair before report, before initial inspection, with each needle,
    missing trim/tail/needle individually, exactly full inventory, duplicate
    packet, exact item consumption, Kiko dialogue, and state 10-to-11 commit;
13. Apatura at state 11 with report/bed bits individually absent, completion
    interruption after each reward step, relog/retry, repeated click, exact XP
    and quest point once, and final presentation; and
14. Wolf below/at Hunter 91 after completion, Master introduction/settings,
    task request/completion, ordinary-rumour coexistence, stairs access, and
    no Master service before state 12.

### Live-client evidence

Capture a real-client run from the legitimate start through a Master-rumour
conversation without state/debug commands. Evidence must include:

- rejected start requirements, explicit acceptance, journal changes, and
  shared stairs in both directions;
- Verity and Wolf subjects, mouse receipt/winding, live Kiko interaction, bed
  inspection, and correct NPC transforms;
- two concurrent players using isolated box traps on live embertailed jerboas,
  the exact one-tail poultice recipe, treatment, separate sample handoff, and
  each full-inventory recovery path;
- Atza's live equipment-pile transform and hammer repair, trimmed-fur/report
  lifecycle, readable report, and the exact final bed recipe;
- completion interruption/reconnect boundaries, exact rewards once, repeated
  Apatura dialogue, and post-quest healthy Fox; and
- ordinary rumours and Wolf's level-gated Master-tier service remaining
  accessible through the same guild stairs and shared NPC dispatch.

Only after static checks, automated matrices, pack validation, and live-client
evidence pass may this record change from `audit-pending` to `modernized`.
