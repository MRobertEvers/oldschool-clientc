# Cold War modernization audit

Status: `audit-pending` — the native quest row, state varbits, principal cast,
items, scenery, journal dispatch, shared completion call, and a recognisable
0–135 story skeleton exist. The production route is nevertheless not
completable from its real start. Larry never performs the first trip to the
Iceberg; the first outer KGP agent is hidden by a varbit that only that hidden
agent can set; the crush-course is replaced by one message without moving the
player; the suit cannot be removed when bongos must be crafted; and the war-room
and pen doors narrate movement without moving or opening. Major mechanics are
also substituted with prose or chat menus: the bird-hide cutscene, native
eight-emote greeting interface, three-report debrief, agility course, Ping and
Pong cutscene, Pescaling Pax capture, and Icelord combat. This is a legacy
outline, not a modern or playable quest implementation.

Audited: 2026-08-17

Governing plan: [Quest modernization plan](../QUEST_MODERNIZATION_PLAN.md). This
record applies Gates A–D to the complete route, all native state and side-state,
three Larry actors, shared Fred and dairy-cow operations, POH clockmaking,
transformation lifecycle, reports, KGP Headquarters, combat, rewards,
post-quest unlocks, downstream Larry ownership, journal, debug adapters, and
map reachability. It is an implementation specification, not verification
evidence.

## 1. Authoritative references

The Wiki article and guide define mechanics, requirements, rewards, and the
current route. The transcript defines the dialogue/cutscene and re-talk
contract. Revisions were resolved through the OSRS Wiki API on 2026-08-17.

| Reference | Pinned revision | Use in this audit |
| --- | --- | --- |
| [Cold War](https://oldschool.runescape.wiki/w/Cold_War?oldid=15292358) | 15292358, 2026-08-10 | Identity, requirements, full route, emotes, combat, rewards, and downstream prerequisite |
| [Cold War/Quick guide](https://oldschool.runescape.wiki/w/Cold_War/Quick_guide?oldid=15107413) | 15107413, 2026-01-17 | Exact item order, travel, equipment restrictions, course, control panel, and finale |
| [Transcript:Cold War](https://oldschool.runescape.wiki/w/Transcript%3ACold_War?oldid=15263373) | 15263373, 2026-07-14 | Accept/refuse/re-talks, cutscenes, reports, Fred branches, debrief, band, capture, and completion |
| [Larry](https://oldschool.runescape.wiki/w/Larry?oldid=14928398) | 14928398, 2025-06-27 | Actor locations, suit service, travel, post-quest, and sequel reuse |
| [Clockwork suit](https://oldschool.runescape.wiki/w/Clockwork_suit?oldid=15232789) | 15232789, 2026-06-13 | Construction recipe, transformation restrictions, destruction/replacement, and reuse |
| [Snowy bird hide](https://oldschool.runescape.wiki/w/Snowy_bird_hide?oldid=14996090) | 14996090, 2025-09-28 | Hide transform and repeat observation behavior |
| [Penguin (Cold War)](https://oldschool.runescape.wiki/w/Penguin_%28Cold_War%29?oldid=15095342) | 15095342, 2025-12-27 | Zoo operative, greeting, cod/ring branch, and Ardougne report |
| [Sheep (penguins)](https://oldschool.runescape.wiki/w/Sheep_%28penguins%29?oldid=15010521) | 15010521, 2025-10-27 | Lumbridge greeting, phrase, Fred task, and Lumbridge report |
| [Mission report](https://oldschool.runescape.wiki/w/Mission_report?oldid=10654895) | 10654895, 2019-10-21 | Three distinct report variants and debrief ownership |
| [Mission report (Ardougne)](https://oldschool.runescape.wiki/w/Mission_report_%28Ardougne%29?oldid=15184435) | 15184435, 2026-04-22 | First report identity and readable content |
| [Mission report (Lumbridge)](https://oldschool.runescape.wiki/w/Mission_report_%28Lumbridge%29?oldid=15184436) | 15184436, 2026-04-22 | Second report identity and readable content |
| [Mission report (fake)](https://oldschool.runescape.wiki/w/Mission_report_%28fake%29?oldid=13972868) | 13972868, 2020-12-07 | Noodle's third report |
| [Kgp id card](https://oldschool.runescape.wiki/w/Kgp_id_card?oldid=15184432) | 15184432, 2026-04-22 | Noodle exchange and KGP entry gate |
| [KGP Headquarters](https://oldschool.runescape.wiki/w/KGP_Headquarters?oldid=15302199) | 15302199, 2026-08-15 | Headquarters topology, rooms, course, pen, and post-quest access |
| [Control panel (Cold War)](https://oldschool.runescape.wiki/w/Control_panel_%28Cold_War%29?oldid=15286162) | 15286162, 2026-08-03 | Guard-distraction and war-room door contract |
| [Cowbells](https://oldschool.runescape.wiki/w/Cowbells?oldid=15263890) | 15263890, 2026-07-15 | Steal-cowbell operation and post-quest availability |
| [Penguin bongos](https://oldschool.runescape.wiki/w/Penguin_bongos?oldid=15263897) | 15263897, 2026-07-15 | Mahogany-plank/leather recipe and playable-in-suit rule |
| [Penguin Agility Course](https://oldschool.runescape.wiki/w/Penguin_Agility_Course?oldid=15239936) | 15239936, 2026-06-26 | Quest course mechanics, failures, and permanent unlock |
| [Icelord](https://oldschool.runescape.wiki/w/Icelord?oldid=15199467) | 15199467, 2026-04-28 | Level 51 combat and 40 Attack XP per quest kill |
| [Making Friends with My Arm](https://oldschool.runescape.wiki/w/Making_Friends_with_My_Arm?oldid=15265992) | 15265992, 2026-07-17 | Required-for relationship and shared Rellekka Larry ownership |

The current contract is a members, intermediate, medium quest, released 29
January 2007, and the first Penguin-series quest. It starts with Larry at
Ardougne Zoo, has no quest prerequisite, ends at state 135, and awards 1 quest
point, 5,000 Agility XP, 2,000 Crafting XP, and 1,500 Construction XP. It also
unlocks repeat clockwork-suit construction, the Penguin Agility Course, and
continued cowbell/bongo acquisition. Completion is required for Making Friends
with My Arm.

The 40 Attack XP sometimes described as a reward is not completion XP. The
current Wiki warns that **each** of the one to three Icelords killed during the
quest gives 40 Attack XP. The dbrow is therefore correct to omit Attack from
`stat_xp_awarded`; the combat death path must award it.

The Wiki currently contradicts itself on 30 Crafting: the requirements template
marks it unboostable and required to start, while its attached footnote says it
can be boosted to start and to make the suit but that bongos require unboosted
30. The cache row has no boostability column and the implementation uses
`stat_base`. Preserve the current conservative base-level gate until a live
OSRS capture or corrected authoritative revision resolves the contradiction;
record that evidence in Gate D rather than silently choosing the footnote.

Transition aid only: the local Quest Helper checkout's Cold War implementation
at commit [`5ea99d5`](https://github.com/Zoinkwiz/quest-helper/tree/5ea99d5ea9ba3fb096ebe7b5ed02d80883e9819d/src/main/java/com/questhelper/helpers/quests/coldwar)
confirms native states 0–130, relevant zones, items, NPCs, and the native
interface-223 emote components. `python3 tools/questhelper_extract.py coldwar
--check` exits 0 and resolves the quest row, 25 item symbols, 14 NPC symbols,
13 loc symbols, 9 varbits, and the guide coordinates. It cannot prove server
travel, transforms, transactions, combat deaths, or post-quest unlocks.

## 2. Native quest identity and state contract

| Field | Native value / expected behavior |
| --- | --- |
| Cache quest ID | 126 |
| Type / series | Members quest; Penguin series #1 |
| Difficulty / length | Cache 1 / 2; Wiki intermediate / medium |
| Start | Larry (`peng_larry_zoo`) at Ardougne Zoo |
| Required skills | 10 Hunter, 30 Agility, 30 Crafting, 34 Construction, 15 Thieving |
| Additional access | Crafting table 3 or 4; no quest prerequisite |
| Primary state | `%peng_quest`, bits 0–7 of transmitted `%peng_var` |
| Quest points | 1 |
| Completion XP | Agility 5,000; Crafting 2,000; Construction 1,500 |
| Encounter XP | 40 Attack XP per Icelord killed during the quest |
| Permanent unlocks | Penguin suit construction/use, Agility Course, cowbells/bongos |
| Downstream | Making Friends with My Arm |
| End state | 135 |

### Primary state inventory

| State | Canonical/native phase | Current implementation |
| ---: | --- | --- |
| 0 | Not started | Larry checks base stats and offers a two-choice start |
| 5 | Materials / first Iceberg trip | Materials are checked, but no teleport or travel is offered |
| 10 | Observation complete | Written after a two-message substitute for the cutscene |
| 15 | Leave Iceberg / talk after observation | Rellekka Larry handles this directly |
| 20 | Larry at Rellekka | Constant exists but no production read or write uses it |
| 25 | Construct clockwork suit | POH table handler replaces the ordinary clockmaking operation |
| 30 | Test suit on Iceberg | Zoo Larry advances here; no trip to the Iceberg exists |
| 35 | Infiltrate Ardougne Zoo | Larry toggles one varbit without validating equipment or item lifecycle |
| 40 | Read/report to Larry | Report may never have been added; Larry advances anyway |
| 45 | Visit Lumbridge agents | Greeting menu available if the player somehow reaches the actors |
| 50 | Obtain passphrase at zoo | Ring is accepted from inventory, although Wiki requires it equipped |
| 55 | Return to disguised penguins | Dialogue advances without persisting/validating the phrase |
| 60 | Question Fred | Shared Fred dispatcher correctly owns the exact Talk-to operation |
| 65 | Receive outpost intelligence | Report addition is retryable, but the script incorrectly requires a cowbell first |
| 70 | Greet outer KGP agent | Outer KGP is invisible at its initial varbit value; no greeting/password exists |
| 75 | First Noodle conversation | Requests tar and five feathers |
| 80 | Noodle exchange | Can advance without ID/report; adds wrong report variant |
| 85 | Show ID to outer KGP | Checks ID only; no greeting or password recovery |
| 90 | Interior debrief / start course | No three-report debrief; directs player to a soft-skipped door |
| 95 | Course complete, instructor | Door writes this without movement or obstacles |
| 100 | Report training to Larry | Instructor writes this; reaching either actor is unproved |
| 105 | Army report / return inside | Larry advances here after a compressed report |
| 110 | Ask guarded KGP / find Ping and Pong | Any KGP actor advances; clearance scene is absent |
| 115 | Acquire instruments | Bongos require suit off, but no reachable removal service exists |
| 120 | Guard distracted / use panel | Ping/Pong consume items and set the guard varbit to 2 |
| 125 | Captured in Icelord pen | War-room door writes state without opening, moving, or cutscene |
| 130 | Escape and report to Larry | Any Talk-to/Attack on one Icelord writes this without combat |
| 135 | Complete | State is written before XP/quest-point lifecycle and has no idempotence guard |

State 20 is not cosmetic debt: Quest Helper routes it to Larry at Rellekka,
while the implementation jumps 15→25. Modernization must establish the live
15/20 transition rather than retaining an invented shortcut.

### Native side-state inventory

| Varbit | Native shape | Current use and mismatch |
| --- | --- | --- |
| `%peng_multi_hide` | 2 bits | 0 none, 1 frame, 2 snowy hide; matches cache variants correctly |
| `%peng_multi_kgp` | 2 bits | Conflates outer KGP/Noodle visibility with the later control-room guard; the quest starts at 0 while both required outer NPCs render only at 1 |
| `%peng_emote_1..3` | 4 bits each | Stores only values 1–4, although native interface/animations and Wiki define eight emotes |
| `%peng_emote_check` | 2 bits | Native current-emote index; never used |
| `%peng_doing_greeting` | 1 bit | Set around three blocking chat menus; no logout/busy cleanup path |
| `%peng_emote_chat` / `%peng_chat_check` | native bits | Unused despite greeting/dialogue machinery |
| `%peng_transmog` | 1 bit | Toggled as a boolean message; no item/equipment/animation/location invariant |
| `%peng_agility_state` | 2 bits | Explicitly abandoned by the soft-skip course |
| `%peng_pong_chat` | 1 bit | Documented as hand-in state but never read or written |
| `%peng_teleport_check` | native bit | Unused; suit removal inside Headquarters never ejects the player |

`loc_var_audit.py --var peng_multi_hide` reports a transmitted carrier, one
placed base loc, and correct native variants: unbuilt, frame, snowy, blown-up,
hidden. Value 2 is therefore a valid snowy hide. The same tool finds no loc
transform for `%peng_multi_kgp`; its critical transforms are NPC definitions:
outer `peng_multi_kgp` and Noodle both resolve to `-1` at value 0 and appear at
value 1, while the booth agent disappears at value 2.

## 3. Implementation surface

The quest root contains 1,211 lines across one config and eight scripts.

| Path | Present responsibility | Audit result |
| --- | --- | --- |
| `server/scripts/quests/quest_coldwar/configs/coldwar.constant` | Native contract, states, items, coordinates | Useful inventory; embeds stale claims that cache overrides Wiki and soft-skips are acceptable |
| `scripts/coldwar_larry.rs2` | Three Larry actors, start, travel, suit service, completion | Broad dialogue skeleton; missing required trips/toggles and skips state 20 |
| `scripts/coldwar_shared.rs2` | Requirements, greeting, suit toggle, completion | Four-emote chat substitute; unsafe transform and non-idempotent completion |
| `scripts/coldwar_birdhide.rs2` | Hide frame/snow transforms | Core material transaction works; no dig-near alternative or observation scene |
| `scripts/coldwar_clockwork.rs2` | POH clockwork and suit recipe | Shadows normal table use and allows no replacement outside exact state 25 |
| `scripts/coldwar_zoo.rs2` | Zoo door, penguin, report, cod/ring | Door does not open; report can be lost; ring equipment is not checked |
| `scripts/coldwar_lumbridge.rs2` | Sheep agents, Fred label, cowbell | Cowbell bound to Milk rather than Steal-cowbell; post-quest unlock absent |
| `scripts/coldwar_outpost.rs2` | KGP route, course, band, panel, finale | Contains most deterministic blockers and all major soft-skips |
| `scripts/coldwar_journal.rs2` | State journal | Broad route text; omits actual greeting code and treats unknown state as complete |
| `scripts/coldwar_debug.rs2` | Reset and `::cwrun` | Scripted success path masks production blockers and incompletely resets side-state |

Mandatory shared/cross-directory surfaces include:

| Surface | Relationship / modernization requirement |
| --- | --- |
| `configs/all.dbrow` | Correct identity, start, end, requirements, quest point, and three completion XP rows |
| `configs/all.varp` / `all.varbit` | Native `peng_var`/`peng_var2` carriers and all progress/side varbits |
| `configs/all.obj` | Three reports, KGP ID, two suit states, book, cowbells, bongos, and required materials all resolve |
| `configs/all.npc` | Native multi-NPC visibility, eight emote animations, Larry/KGP/cast definitions, and combat actors |
| `configs/all.loc` | Hide transforms, zoo/Headquarters doors, course obstacles, panel, chasm, and POH tables |
| `areas/world/configs/m40_51.spawn` | Zoo Larry and zoo penguin |
| `areas/world/configs/m42_58.spawn` | Rellekka Larry |
| `areas/world/configs/m41_62.spawn` | Iceberg Larry, hidden outer KGP, and hidden Noodle |
| `areas/world/configs/m41_162.spawn` | Interior KGP/booth, Ping/Pong, and four Icelords |
| `areas/world/configs/m41_63.spawn` | Agility instructor |
| `areas/lumbridge/scripts/fred_the_farmer.rs2` | Correct additive Cold War dispatch while preserving Sheep Shearer and One Small Favour |
| `skill_crafting/scripts/leather/leather.rs2` | Shared reverse-direction leather→mahogany-plank dispatch for bongos |
| `doors/scripts` and `doors/configs/doors.loc` | Modern open/close transforms for zoo and KGP doors; quest exact handlers currently shadow them |
| `ladders_stairs` | Some native course steps already have traversal metadata; use rather than replacing with prose |
| generated NPC combat configs | All four Icelords already have attack/defend/death animations and stats |
| `quests/scripts/questpoints.rs2` | Modern completion lifecycle, but caller must supply idempotent transaction |
| `quests/scripts/quest_cheat.rs2` | End-state adapter only; correctly not gameplay/reward evidence |
| `quest_makingfriendswithmyarm` | Reuses Rellekka Larry; Cold War must retain additive delegation after completion |

No separate production script implements the Penguin Agility Course, post-quest
cowbell theft, suit item operations, or Icelord quest deaths. Cache assets and
generated combat parameters are present, but assets are not behavior.

## 4. Reachability and canonical route audit

| Phase | Canonical behavior | Current behavior / consequence |
| --- | --- | --- |
| Start and travel | Accept, gather materials, re-talk to zoo Larry, confirm teleport to Iceberg | State 5 only says to head there; neither zoo nor Rellekka Larry teleports. **Hard blocker.** |
| Build hide | Use 10 oak planks, 10 steel nails, hammer; use/dig with spade | Material deletion and 0→1→2 transform exist; dig-near option absent |
| Observe | Enter repeatable cutscene and note three of eight random emotes | Talking Larry rolls three of only four and prints their names; hide cannot replay after state 10 |
| Return / suit | Boat to Rellekka, talk Larry, read book, build via normal POH clockwork menu, return with Larry | Boat only returns to Rellekka; state 20/book/read behavior absent; exact table op blocks ordinary clockmaking; no outbound trip |
| Zoo disguise | Require suit plus empty hands/cape, animate/transform, open pen, use native emote panel | Only a bool is set; no equipment/item check; door prints a message without opening; three chat menus replace native panel |
| Reports / phrase | Read Ardougne report; Lumbridge greeting; obtain phrase with raw cod or **equipped** ring | Reports have no Read handlers; full inventory can lose first report; ring works merely in inventory |
| Fred / outpost clue | Remove suit, question Fred via bully or warn, return to sheep agents for report | Fred branch is safely shared but offers only bully; suit state is ignored; script invents a cowbell prerequisite for the report |
| KGP approach | Return to Iceberg, suit up, greet outer agent, give password, be sent to Noodle | Suit service/travel absent; outer agent and Noodle are hidden until that hidden agent changes `%peng_multi_kgp`. **Hard blocker.** |
| Noodle / debrief | Exchange tar/feathers for ID and fake report; give all three reports to interior KGP | State can advance without items; duplicate Ardougne report is added instead of fake report; debrief neither checks nor removes reports |
| Crush-course | Traverse water, moving ice, stones, icicle shelves, ice climb and slide with failures/damage | Door prints the entire course, writes 95, and does not move player to instructor. **Hard blocker.** |
| Report / instruments | Leave suit, report army, re-enter, fail clearance, ask Ping/Pong, craft while human | No general suit removal/ejection exists; bongos explicitly refuse while suited. **Hard blocker at state 115.** |
| Band / panel | Hand in items, play cutscene, guard moves, use panel, enter east war room | One message replaces song; panel records nothing, so it is optional; door message does not open/move |
| Capture | Pescaling Pax scene, anti-magic reveal, surrender suit, move to Icelord pen | Two messages set state and clear bool; suit item is retained and player is not moved. **Hard blocker.** |
| Icelords / escape | Kill 1–3 level 51 Icelords, receive 40 Attack XP each, open west door, use east chasm | Talk-to or Attack on any one actor instantly writes 130; no combat/XP; pen door does not open/move |
| Completion | Escape to surface, talk Larry, atomically grant rewards/unlocks | Three XP awards and shared call exist, but state commits first and repeated entry can duplicate rewards/quest points |

The implementation's hard-coded avalanche is not a substitute for travel. It
accepts any state from 70 onward and always teleports into the debrief room,
even before ID clearance; it has no reverse direction. Removing the suit inside
Headquarters is canonically an emergency exit to Larry, but no item/operation
can clear `%peng_transmog` there.

## 5. Mechanic and lifecycle oversights

### 5.1 Greeting machinery is the wrong interface and wrong domain

There are eight native penguin emotes—Shiver, Cheer, Spin, Wave, Clap, Preen,
Bow, and Flap—with corresponding cache animations. Native interface group 223
exposes the emote choices and Quest Helper reads components `8 + emote value`,
driven by `%peng_emote_check`. The script instead generates 1–4 and labels them
Wave/Bow/Cheer/Dance; Dance is not one of the eight. It asks for all three via
ordinary `p_choice4` menus and reports failure only after the third choice.

Modernization must use the current named subinterface mounting pattern, arm all
eight operations, animate each entered emote, advance `%peng_emote_check`, fail
immediately on a wrong selection, and clear modal/greeting state on completion,
cancel, logout, death, region change, or remount. The hide scene and journal
must preserve the same three values; repeated hide observation before moving on
must not reroll the sequence.

### 5.2 Suit state has no invariant

`~coldwar_wear_suit` and `~coldwar_remove_suit` only change one bit and print a
message. They do not require the suit item, empty weapon/shield/cape slots,
validate Larry/location/state, play native transform animations/spotanims,
control the suit NPC/player appearance, eject a human from Headquarters, or
handle death/logout. The canonical capture gives the suit to Pescaling Pax;
the current code retains it. The script also permits continued disguise after
loss/destruction and offers no replacement construction after state 25.

Define one player-owned transformation service with explicit enter, leave,
forced-eject, capture, logout/death repair, and post-quest paths. Do not scatter
raw `%peng_transmog` writes across dialogue.

### 5.3 Item transactions can advance after failed grants

- Zoo greeting advances 35→40 even when a full inventory prevents report 1.
- Noodle deletes materials, conditionally adds two items, then always advances
  80→85. Partial stacks can leave zero free slots, losing both required items.
- Noodle adds `peng_report_1`, whose cache description is “Zoo penguin's
  report,” instead of `peng_report_3`, “My fake report.”
- The interior KGP consumes none of the three reports and does not validate the
  fake report or ID as a complete debrief transaction.
- POH and bongo recipes delete inputs before conditional output addition. Their
  current inputs usually free space, but the modern helper should still make
  the transaction explicit and test repeated/busy delivery.
- The first suit, ID, reports, cowbell, and bongos lack complete lost-item and
  banked-item replacement matrices.

The Wiki documents a live no-space warning around Noodle's third report, but
the modernization plan requires this server not to create an irreversible
state. Preserve canonical dialogue while refusing to commit the state unless
all mandatory outputs are owned or safely recoverable.

### 5.4 Shared operations are shadowed

The quest binds `oploc1` on `fat_cow`, whose native op1 is **Milk** and op2 is
**Steal-cowbell**. Thus the required right-click action has no quest handler,
while ordinary milking is replaced with quest text for every player. The
handler also allows only states 60–65, contradicting the permanent reward.

The zoo door and `peng_base_door` already have modern door category/next-stage
metadata. Exact quest handlers print prose instead of delegating to the door
open/walkthrough machinery, so collision remains. The two POH table handlers
similarly return “Nothing interesting happens” outside state 25 and shadow the
ordinary clockwork toy interface for every player using those tables.

Refactor these as additive policy around shared owners: op2 cowbell theft with
the correct Cold War/post-quest gate, shared Milk untouched; allowed quest
doors call the modern door/traversal primitive; the clockwork penguin is an
option in the existing POH menu rather than the owner of table op1.

### 5.5 Combat and course are already expressible

The constant file claims the engine lacks a zone requirement primitive and
therefore blesses a one-message course. That is not a valid Gate C exception.
The cache contains all course locs/animations, `%peng_agility_state`, instructor
spawn, and ladder/step traversal metadata. The repository already has player
queues, loc operations, damage, movement, and map-link patterns.

All four Icelords have generated combat stats and death/attack/defend animation
params. The quest overrides both Talk-to and Attack to narrate a kill. Restore
normal combat ownership and add a protected quest death hook that awards 40
Attack XP once per qualifying death, tracks the native/random 1–3 release
condition, and opens the exit only after the condition is met. Test other
players' kills, retargeting, death, teleport/re-entry, and post-quest Icelords.

## 6. Completion, rewards, and post-quest behavior

`~coldwar_quest_complete` writes 135, clears the transform bit, awards the three
correct completion XP values, then calls `~quest_complete_rewards`. A repeated
call at state 130—or re-entry after an interrupted reward path—can duplicate XP,
quest points, completed count, and scroll. Writing 135 first can also leave a
player permanently complete without all rewards after interruption.

Modern completion must be one idempotent transaction/resume state:

1. validate escape state and required finale facts;
2. normalize transformation/captured item state;
3. commit each XP/quest-point/completed-count grant exactly once;
4. write terminal state only when durable reward facts are complete; and
5. render the scroll safely on replay without granting again.

The reward text currently advertises three unlocks that do not exist:

- `fat_cow` blocks cowbells outside the mid-quest window;
- bongo crafting requires exact state 115 instead of completion too; and
- no production course obstacle loop, repeat access policy, or post-quest suit
  construction path is implemented.

The `::complete` adapter correctly sets only the native end state for
prerequisite preparation. It must not fabricate inventory, side-state, XP, or
unlock evidence. Preserve the completed Rellekka Larry delegation to Making
Friends with My Arm while rebuilding Cold War's own post-quest service menu.

## 7. Dialogue, journal, cutscenes, and presentation

The dialogue is heavily compressed but uses modern `p_choice` helpers and
symbolic assets. The main missing contract is behavioral, not word-for-word
fidelity:

- start needs the current confirmation/travel/follower refusal and re-talks;
- the bird hide must show the two penguins and repeat the same greeting;
- Fred must support both bully and warn outcomes while preserving Sheep Shearer;
- the KGP greeting, cabbage password, ID rejection, and three-report debrief are
  absent;
- Army Commander, Pescaling Pax, band performers/cutscene variants, suit
  surrender, and captured placement are absent; and
- post-quest Larry, course, cowbell, bongo, suit, and Icelord dialogue/services
  need current ownership.

The journal broadly follows 0–130, but it never prints the three emotes, calls
the third report merely “a report,” provides no item-loss recovery, and its
catch-all `else` renders `QUEST COMPLETE!` for every unknown/corrupt state—not
only values at or above 135. State 20 has no branch. Modernize it to exact state
ranges, native greeting names, partial inventory facts, recovery directions,
and an explicit terminal predicate.

The native soundtrack/cutscene contract includes `Espionage`, `Have an Ice
Day`, `The Penguin Bards`, and `Penguin Plots`, plus native transform and
anti-magic synths listed in `synth_coldwar`. None is orchestrated by the quest
scripts. Add music only through area/cutscene ownership; do not treat soundtrack
presence as a completion unlock.

## 8. Prioritized defect ledger

### P0 — deterministic completion blockers / irreversible progress

1. State 5 has no zoo-Larry teleport or other production first trip to the
   Iceberg.
2. State 30 likewise has no return trip with the finished suit.
3. Outer KGP and Noodle are invisible at `%peng_multi_kgp=0`, but only talking
   to the invisible KGP sets it to 1.
4. Zoo, course, war-room, and pen exact door handlers do not open/walk/move;
   course and capture destinations are therefore unreachable.
5. Noodle can commit state 85 without ID or fake report.
6. State 115 requires suit-off bongo crafting, but the production route has no
   general suit removal after state 45.
7. Completion has no idempotence/resume guard and can duplicate permanent XP,
   quest points, and completed count.

### P1 — core mechanic, item, combat, or shared-owner correctness

1. Four invented greeting choices replace eight native emotes/interface 223;
   code replay, immediate failure, and interruption cleanup are absent.
2. No bird-hide observation cutscene or stable repeat observation exists.
3. Suit service ignores item ownership, equipment slots, transformations,
   capture, ejection, death/logout, and replacements.
4. First and third reports can be lost; wrong third report is created; three
   reports are never read/consumed/debriefed.
5. Ring of charos(a) is accepted from inventory rather than equipped.
6. Milk is hijacked and Steal-cowbell is unimplemented; permanent cowbell and
   bongo unlocks are absent.
7. Exact POH table handlers shadow ordinary clockmaking and lack replacement or
   post-quest construction.
8. The entire agility course/failure/damage/ejection route is soft-skipped.
9. Band, control panel fact, war-room/Pax capture, and suit surrender are
   message-only or absent.
10. Icelord combat is overridden; one interaction clears the state and awards
    no 40 Attack XP.
11. Avalanche entry can bypass clearance and has no reverse/forced-exit policy.
12. State 20 is skipped, `%peng_pong_chat`/`%peng_agility_state` are unused, and
    debug reset does not normalize all native side-state.

### P2 — metadata, narrative, journal, and evidence debt

1. Wiki Crafting boostability contradiction requires live capture/resolution.
2. Start membership/follower/busy behavior is untested.
3. Dialogue omits legitimate Fred, KGP, Army Commander, Pax, report, band, and
   post-quest branches.
4. Journal hides the greeting and treats corrupt states as complete.
5. Native animation, spotanim, synth, and music assets are unused.
6. `::cwrun` narrates/forces success and must not be accepted as route evidence.
7. Stale config comments explicitly approve critical soft-skips and say “cache
   wins” over current Wiki mechanics; remove them when the behavior is rebuilt.

## 9. Modernization work packages

### Package 0 — contract, state, and reproducible route harness

- Pin the references above in the generated manifest and reconcile state 20,
  Crafting boostability, all side-varbit meanings, and report identities.
- Add invariant checks for visible required actors, transform/item ownership,
  report set, guard state, course state, capture state, and terminal rewards.
- Replace `::cwrun` as evidence with checkpoint setup plus production-operation
  assertions; make reset clear every Cold War varbit and quest item safely.

### Package 1 — start, travel, hide, and native greeting

- Implement zoo Larry's materials re-talk and confirmed direct Iceberg trip,
  follower/busy refusal, return boat, state 15/20 Rellekka conversation, book,
  and finished-suit return trip.
- Build the repeatable protected bird-hide cutscene using the native 0/1/2 loc
  transform and stable eight-emote code.
- Mount and fully arm the native emote interface; animate and clean up every
  success, failure, logout, and remount path.

### Package 2 — clockwork suit and surface infiltration

- Integrate the suit recipe/replacements into shared POH clockmaking.
- Centralize Larry transformation, slot checks, appearance, forced exits,
  item/capture lifecycle, and post-quest reuse.
- Repair the zoo door through shared door machinery; implement readable/retryable
  Ardougne/Lumbridge reports, equipped-ring/raw-cod branches, both Fred choices,
  and additive shared actor operations.
- Put cowbell theft on op2 without disturbing Milk and keep it available after
  completion.

### Package 3 — KGP entry, reports, and crush-course

- Separate/sequence outer actor visibility from the later guard-moved fact;
  implement greeting, cabbage password, ID failure, Noodle's atomic ID/fake
  report exchange, avalanche clearance, and interior three-report debrief.
- Implement every course obstacle with current movement/queue/damage machinery,
  `%peng_agility_state`, reset/failure, suit-off ejection, and instructor finish.
- Prove every door/map link and both inside/outside recovery directions in a
  real client.

### Package 4 — instruments, war room, combat, and escape

- Restore suit leave/re-enter, KGP clearance refusal, Ping/Pong discovery,
  post-quest bongo crafting, instrument hand-in, song cutscene, guard movement,
  and durable control-panel fact.
- Implement the east war-room traversal, Pescaling Pax/anti-magic cutscene,
  suit surrender, captured pen placement, teleport/re-entry rule, and cleanup.
- Restore real Icelord combat, per-kill 40 Attack XP, 1–3 release condition,
  pen-door traversal, chasm escape, and post-quest access.

### Package 5 — atomic completion, unlocks, journal, and regression tests

- Make completion idempotent and resume-safe around all three XP rewards,
  quest point, count, scroll, jingle, and terminal state.
- Implement and test permanent suit, cowbell, bongo, Agility Course, Icelord,
  and Making Friends with My Arm integrations.
- Update journal/re-talks/music and remove stale soft-skip claims only after
  their production mechanics and evidence exist.

No quest-specific C change is justified by this audit. Current RuneScript,
configs, player/NPC queues, doors, loc transforms, movement, interfaces, combat
hooks, and shared quest lifecycle cover the required behavior. Add a general
engine primitive only if an isolated reproduction proves an actual VM gap.

## 10. Verification matrix

### Gate A — static contract

- `questhelper_extract.py coldwar --check` remains clean.
- Pack/compiler resolves every symbolic obj/NPC/loc/interface/animation/synth.
- State read/write report accounts for 0–135, state 20, all greeting/course/
  guard/transform bits, and the Making Friends Larry dispatcher.
- Trigger ownership proves Milk, Steal-cowbell, POH tables, doors, leather,
  Fred, and Larry each have one compositional owner.
- Map/spawn audit proves all actors and every travel/course/pen target.

### Gate B — clean gameplay route

- Start once with exact stats, reject, accept, re-talk with partial/full
  materials, follower present, and travel from the real zoo actor.
- Complete hide, repeat the same observation, solve all eight possible emote
  values at zoo/Lumbridge/KGP, fail each position, and recover.
- Craft mechanism/suit in own and another POH without breaking other toys;
  validate all slot restrictions and suit enter/leave locations.
- Complete raw-cod and equipped-ring branches, both Fred choices, all three
  reports, ID exchange/debrief, every course obstacle/failure, instruments,
  panel, capture, 1/2/3-Icelord releases, escape, and finale.
- Verify exactly 40 Attack XP per quest Icelord kill and exactly the three
  completion XP awards plus one quest point.

### Gate C — interruption, loss, and multiplayer cases

- Full/partial-stack inventory at every report, ID, suit, cowbell, bongo, and
  reward transaction; bank/drop/destroy/death replacement at every state.
- Logout/disconnect/death/teleport during each cutscene, greeting position,
  transform, course obstacle, band scene, capture, combat, chasm, and reward.
- Spam/double-click Larry, Noodle, Ping/Pong, panel, doors, Icelords, and finale;
  no duplicate item, XP, quest point, or completed count.
- Two players independently observe codes, transform, move the guard, run the
  course, enter the war room, fight Icelords, and complete without sharing loc,
  NPC, queue, or kill credit incorrectly.
- Corrupt/import checkpoints for every primary state and side-state either
  repair to a safe invariant or provide a clear recovery path.

### Gate D — evidence required before `verified-modern`

- Clean script/config build and focused automated tests pass.
- Save/reload tests prove permanent progress, rewards, and unlocks.
- Real-client captures show every trip and collision boundary: zoo pen,
  Iceberg boat/avalanche, Headquarters doors, full course, war room, pen, and
  chasm.
- Captures prove native greeting panel remount/ops, player transformation,
  cutscenes, combat deaths/XP, inventory-full behavior, and modern completion
  scroll.
- Live OSRS or corrected Wiki evidence resolves 30 Crafting start boostability.
- Post-quest captures prove repeat suit creation/use, cowbells, bongos, course,
  Icelords, and Making Friends with My Arm's Larry branch.

## 11. Exit criteria

Cold War can move from `audit-pending` to `verified-modern` only when a clean
member account completes the real 0→135 route with no cheat/debug state writes;
all travel, native emotes, transforms, reports, course obstacles, cutscenes,
combat, and escape are production mechanics; inventory/loss/logout/death and
multiplayer cases are recoverable; rewards and unlocks are exact and
idempotent; shared Fred/cow/table/door/Larry behavior remains intact; Making
Friends with My Arm still delegates correctly; and the Gate D evidence is
attached to the manifest/audit record.
