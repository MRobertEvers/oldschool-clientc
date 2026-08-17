# Children of the Sun modernization audit

Status: `audit-pending` — the cache quest row, native primary and guard
varbits, journal dispatch, completion call, dialogue helpers, quest NPC/loc
assets, and broad 0–24 phase outline exist. The production route is not
playable: no Children of the Sun NPC wrapper is spawned, so neither start NPC
exists in the world. Even after restoring spawns, the current script hides all
ten puzzle guards, replaces the stealth section and both narrative cutscenes
with soft-skips, omits Noah and the staged finale, bypasses native states, does
not arm Varlamore first travel, and can shadow ordinary Varrock Castle stairs.

Audited: 2026-08-17

Governing plan: [Quest modernization plan](../QUEST_MODERNIZATION_PLAN.md). This
record applies that plan's Gates A–D to world placement, both start NPCs,
accept/refuse/re-talk dialogue, delegation arrival, guard tailing and failure,
the bandit scene, the ten-guard marking puzzle, arrest and roof transitions,
interrogation, completion, music, Varlamore access, Regulus first travel,
downstream quest gates, the journal, and the cheat adapter. It is an
implementation specification, not completion evidence.

## 1. Authoritative references

These pinned OSRS Wiki revisions define the currently documented quest route,
dialogue, rewards, and permanent access contract.

| Reference | Pinned revision | Use in this audit |
| --- | --- | --- |
| [Children of the Sun](https://oldschool.runescape.wiki/w/Children_of_the_Sun?oldid=15241067) | 15241067, 2026-06-27 | Identity, full route, failure behavior, rewards, music, and dependent content |
| [Children of the Sun/Quick guide](https://oldschool.runescape.wiki/w/Children_of_the_Sun/Quick_guide?oldid=14750861) | 14750861, 2024-09-25 | Exact hide route and four correct impostors |
| [Transcript:Children of the Sun](https://oldschool.runescape.wiki/w/Transcript%3AChildren_of_the_Sun?oldid=15029036) | 15029036, 2025-11-15 | Start/refuse/re-talk branches, cutscenes, failure text, incorrect marks, arrest, interrogation, and completion dialogue |
| [Alina](https://oldschool.runescape.wiki/w/Alina?oldid=15258645) | 15258645, 2026-07-09 | Alternate start actor and post-start visibility |
| [Noah](https://oldschool.runescape.wiki/w/Noah?oldid=15258646) | 15258646, 2026-07-09 | Alternate start actor and tailing restart |
| [Sergeant Tobyn](https://oldschool.runescape.wiki/w/Sergeant_Tobyn?oldid=14770543) | 14770543, 2024-10-13 | Investigation, marking, arrest, roof, and completion ownership |
| [Guard (Children of the Sun)](https://oldschool.runescape.wiki/w/Guard_%28Children_of_the_Sun%29?oldid=15029038) | 15029038, 2025-11-15 | Suspicious guard and ten marking candidates |
| [Bandit (Children of the Sun)](https://oldschool.runescape.wiki/w/Bandit_%28Children_of_the_Sun%29?oldid=15197075) | 15197075, 2026-04-25 | House scene and interrogation target |
| [Prince Itzla Arkan](https://oldschool.runescape.wiki/w/Prince_Itzla_Arkan?oldid=15258655) | 15258655, 2026-07-09 | Delegation and interrogation actor |
| [Knight of Varlamore (Children of the Sun)](https://oldschool.runescape.wiki/w/Knight_of_Varlamore_%28Children_of_the_Sun%29?oldid=15197074) | 15197074, 2026-04-25 | Six-member delegation scene |
| [Varlamore](https://oldschool.runescape.wiki/w/Varlamore?oldid=15303616) | 15303616, 2026-08-17 | Region access dependency |
| [Quetzal Transport System](https://oldschool.runescape.wiki/w/Quetzal_Transport_System?oldid=15230850) | 15230850, 2026-06-10 | Regulus and transport lifecycle |
| [Regulus Cento](https://oldschool.runescape.wiki/w/Regulus_Cento?oldid=14961536) | 14961536, 2025-08-08 | First post-quest flight from Varrock |
| [Twilight's Promise](https://oldschool.runescape.wiki/w/Twilight%27s_Promise?oldid=15241069) | 15241069, 2026-06-27 | Immediate story continuation and full transport-system unlock |
| [The Burning Sun](https://oldschool.runescape.wiki/w/The_Burning_Sun?oldid=15253317) | 15253317, 2026-07-05 | Interrogation music unlock |

The sources were resolved through the OSRS Wiki API on 2026-08-17. They
identify Children of the Sun as quest #159, a members, novice, very-short
quest, the first Twilight Emissaries quest, released 10 January 2024. It has no
quest, skill, item, or combat requirements. The player starts by talking to
either Noah or Alina east of Varrock Square.

The reward contract is exactly 1 quest point and access to Varlamore through
the Quetzal Transport System and fairy rings. There is no item, coin, or XP
reward. The current article lists Twilight's Promise, At First Light, The
Ribbiting Tale of a Lily Pad Labour Dispute, Death on the Isle, Meat and Greet,
Ethically Acquired Antiquities, Shadows of Custodia, Scrambled!, and the Vale
Totems miniquest as dependent content.

Transition aid only: the local Quest Helper checkout's
[`ChildrenOfTheSun.java`](https://github.com/Zoinkwiz/quest-helper/blob/5ea99d5ea9ba3fb096ebe7b5ed02d80883e9819d/src/main/java/com/questhelper/helpers/quests/childrenofthesun/ChildrenOfTheSun.java)
at commit `5ea99d5ea9ba3fb096ebe7b5ed02d80883e9819d` confirms the native
0/2/4/6/8/10/12/14/16/18/20/22/24 ladder, the full suspicious-guard route,
five hiding points, ten marking candidates, four correct impostors, Castle
floor zones, roof route, and all ten guard varbits. Its live-capture notes also
record the completion and first-flight contract described in section 7.

`python3 tools/questhelper_extract.py childrenofthesun --check` exits 0. It
resolves the quest dbrow, 14 referenced NPC names, three locs, ten guard
varbits, every route coordinate, and both Castle zones. Symbol resolution does
not prove that an NPC is spawned, that a multinpc leaf is visible, or that a
state transition is reachable.

## 2. Native quest identity and player contract

| Field | Native value / expected behavior |
| --- | --- |
| Cache quest ID | 137 |
| Type | Members quest; Twilight Emissaries #1 |
| Difficulty / length | Cache 0 / 0; Wiki novice / very short |
| Release date | 10 January 2024 |
| Start | Noah or Alina, east of Varrock Square |
| Requirements | None |
| Primary state | `%vmq1`, bits 0–6 of transmitted permanent `vmq1_primary` |
| Side state | Ten two-bit guard selectors, completion type, and met-Alina bit on the same carrier |
| Quest points | 1 |
| XP / items / combat | None |
| Unlocks | Varlamore access by first Quetzal flight and fairy rings; downstream quest eligibility |
| End state | 24 |

The native `quest_childrenofthesun` dbrow has both start NPC ids, the correct
release, series, quest point, end state, and no requirements or rewards. Its
activity-adviser reason is “Gain access to the Kingdom of Varlamore.” The
current completion routine nevertheless passes `coins` to
`~quest_complete_rewards`; that invents an item/reward association absent from
the dbrow and Wiki and must be removed.

### Primary state inventory

| State | Canonical phase | Current use / mismatch |
| ---: | --- | --- |
| 0 | Initial Noah/Alina dialogue and start choice | Only Alina has a handler; compressed dialogue jumps directly to 6 |
| 2 | Start-dialogue checkpoint | Declared and journaled, never written |
| 4 | Accepted/start-dialogue checkpoint before arrival | Declared and journaled, never written |
| 6 | Delegation has arrived; tail the suspicious guard | Present only as a message and Talk-to shortcut; no moving guard or failure loop |
| 8 | Guard reached the house; trigger bandit scene | Door prints a soft-skip and writes 10 |
| 10 | Report the plot to Tobyn | Present in compressed form; writes 12 |
| 12 | Mark four of ten guards | Broken by hidden wrappers and incomplete Mark/Unmark handlers |
| 14 | Report marked suspects | Journal and Tobyn branch exist, but production never writes 14 |
| 16 | Reach the roof / begin interrogation finale | Current state 12 jumps directly here after a Tobyn talk |
| 18 | Interrogation checkpoint | Quest Helper routes to roof; never written |
| 20 | Interrogation checkpoint | Quest Helper routes to roof; never written |
| 22 | Finale/completion checkpoint | Quest Helper routes to roof; never written |
| 24 | Complete | Written on the first roof Tobyn talk, skipping the entire finale and first-travel grant |

The transcript proves that 16–22 are not interchangeable filler: Tobyn arrests
the suspects and escorts the player, Itzla arrives, the player and prince enter
the cell, the bandit reveals an assassination target, Itzla offers access and
departs, and Tobyn closes the quest. Exact ownership of each intermediate
native write should be confirmed with live var capture while implementing the
scene; do not retain the current one-conversation 16→24 collapse.

### Side-state inventory

| Field | Native meaning | Current problem |
| --- | --- | --- |
| `%vmq1_guard_1` … `%vmq1_guard_10` | 0 hidden, 1 unmarked, 2 marked, 3 no-op for each wrapper | All are reset to 0 and never initialized to 1, so every candidate is hidden |
| `%vmq1_questcomplete_type` | Completion presentation/type; live completion writes 2 | Normal completion writes 2; generic cheat does not |
| `%vmq1_met_alina` | Start-dialogue side state; live note observes a pre-start 0→1 after talking to Alina | Never read or written; Noah/Alina route identity is lost |
| `%vmq2_first_travel` | Regulus first-flight lifecycle | Completion must write 1; current quest never touches it |
| `%varlamore_visited` | Durable arrival/access fact | Should change on successful travel, not at quest completion; current Twilight's Promise handler writes it prematurely |

## 3. Implementation surface

Paths below are relative to `OSRS-Content/osrs239-content/`.

### Quest-owned files

The quest root contains only 324 lines: a 29-line constants file, a five-line
varp overlay, and a 290-line script.

| Path | Present responsibility | Audit result |
| --- | --- | --- |
| `server/scripts/quests/quest_childrenofthesun/configs/childrenofthesun.constant` | Primary values, guard values, and five coordinates | Correct broad values; names 2/4/18/20/22 generically and cannot establish their narrative ownership |
| `configs/childrenofthesun.varp` | Native carrier overlay | Correct transmitted permanent carrier and bit ranges |
| `scripts/childrenofthesun.rs2` | Entire route, journal, completion, puzzle, navigation, and debug | Broad outline only; absent world actors, three soft/collapsed critical paths, broken puzzle, missing unlock, and unsafe shared-loc overrides |

### Mandatory shared and cross-directory surfaces

| Path / subsystem | Relationship | Audit result / required ownership |
| --- | --- | --- |
| `configs/all.dbrow` | Native quest metadata | Correct identity, two start NPCs, no requirements/items/XP, 1 QP, and end state 24 |
| `configs/all.varp`, `configs/all.varbit` | `vmq1_primary`, `vmq2_primary`, and native fields | Layout is available and should remain the source of truth |
| `configs/all.npc` | Start actors, delegation, guard, bandits, Tobyn, Itzla, ten marking wrappers | Rich native transforms and cutscene leaves exist but are not placed or fully handled |
| `configs/all.loc` | Bandit door and Castle navigation | Door exists in the map; exact quest stair handlers currently shadow modern category/map-link behavior |
| `areas/world/configs/m50_53.spawn` | Varrock Square and south-east route | Contains no `vmq1_*` actor despite all core ground-floor coordinates being in this square |
| `areas/world/configs/m50_54.spawn` | Varrock Castle and roof | Contains no `vmq1_*` Tobyn, Itzla, bandit, delegation, or knight wrapper |
| `areas/world/spawn_report.txt` | Rejected source-dump placements | Correctly rejects several stale ids whose source names became unrelated VMQ1 actors; rejected coordinates are not authoritative replacements |
| `interface_questjournal/scripts/quest_journal.rs2` | Journal dispatcher | Correctly calls `~childrenofthesun_journal` |
| `quests/scripts/quest_cheat.rs2` | Generic completion adapter | Writes only `%vmq1=24`; misses completion type, first-flight entitlement, guard normalization, and met-Alina policy |
| `ladders_stairs/scripts/ladders.rs2` and `maplink.rs2` | Modern generic climb and exact destination routing | Quest's exact-name handlers take precedence and fail to fall through outside states 16–23 |
| `quest_twilightspromise` | First flight and immediate sequel | Varrock Regulus handler lacks Children completion/first-travel gates and conflates first arrival with starting the sequel |
| `skill_hunter/scripts/quetzal_transport.rs2` and related Quetzal content | Permanent travel network | Must consume the shared Varlamore/transport predicates rather than infer access from incidental location |
| Downstream quest roots | Required-for contract | Only Ribbiting Tale has an explicit `%vmq1>=24` production start check; several starts explicitly defer or omit it |

### Cache-native assets already available

- Noah and Alina world wrappers plus visible and cutscene leaves;
- ground and roof Sergeant Tobyn wrappers;
- moving and static suspicious-guard variants;
- four house-bandit wrappers and a roof-cell bandit wrapper;
- Prince Itzla, Servius, Furia, Ennius, King Roald, Aeonisig, and six knight
  cutscene actors;
- ten independent guard wrappers, each with hidden, Mark, Unmark, and no-op
  leaves;
- primary-state visibility ranges for the delegation, arrest, and finale;
- the bandit-house door and Castle stairs/ladder geometry; and
- native completion, journal, varbit, and Quetzal first-flight fields.

Modernization should connect these existing assets with symbolic configs,
owned temporary actors, protected player/NPC queues, current cutscene and
navigation services, idempotent state writers, and shared unlock predicates.
It should not replace them with a second progress variable or a debug-only
teleport path.

## 4. Native reachability and first blockers

### 4.1 No production start exists

`m50_53.spawn` and `m50_54.spawn` contain no `vmq1_*` entry, and a complete
production `.spawn` search finds none anywhere. Consequently:

1. neither `vmq1_alina` nor `vmq1_noah` is instantiated;
2. the normal player cannot click either dbrow start actor;
3. Tobyn and all ten marking candidates are also absent; and
4. the roof finale has no world actor even if state is written manually.

`::childrenofthesun` can teleport a developer to Alina's coordinate, but it
does not spawn Alina. `::cotsrun` writes the state ladder directly and therefore
tests only that assignments and the completion procedure execute. Neither is
evidence of a playable route.

The old spawn source cannot be copied by numeric id. The generator report shows
that ids now naming VMQ1 content were previously labelled unrelated actors,
including `vmq1_bag_guard_varrock`, guards 1–8, bandits, knights, and Itzla.
The importer's name-drift gate correctly omitted them rather than spawning
wrong NPCs. Restore placements from current-revision symbolic identities and
verified live coordinates, then regenerate the relevant world spawn files.

Known authoritative route coordinates from Quest Helper include Alina at
3225,3426; Tobyn at 3211,3437; the four impostors at 3208,3422, 3221,3430,
3246,3429, and 3237,3427; the six genuine guards at 3227,3424, 3218,3424,
3230,3430, 3206,3431, 3239,3433, and 3218,3433; and roof Tobyn at 3202,3473.
Noah, delegation/cutscene staging, and any persistent versus scene-local actor
placement still require current live capture. Do not infer them from rejected
legacy dump rows.

### 4.2 Restoring spawns exposes a second hard blocker

Each guard wrapper selects:

- value 0: hidden;
- value 1: visible unmarked leaf with `Mark`;
- value 2: visible marked leaf with `Unmark`; and
- value 3: visible no-op leaf.

The quest reset, debug command, and headless run set all ten fields to 0. The
state-10 Tobyn conversation writes primary state 12 but never initializes them
to 1. Even correctly spawned wrapper NPCs therefore disappear when the marking
phase begins. Guard initialization must be an exactly-once transaction at the
canonical transition into state 12, with a repair path for saves already at
12/14 whose ten values are still 0.

### 4.3 Shared Castle navigation is shadowed

The quest declares exact `[oploc1]` handlers for
`fai_varrock_stairs_taller_new_fix` and `fai_varrock_ladder_taller`. They move
the player only while `%vmq1` is 16–23 and otherwise return without calling the
generic climb/map-link route. Exact-name triggers outrank the shared climb
category, and the stairs already have verified map-link rows for their real
destinations. This can make ordinary Castle traversal do nothing for every
player outside the finale.

Remove the quest-owned permanent navigation triggers. The shared map-link
system should move players on every normal climb; quest logic should observe
arrival or use a scoped scene transition only if the canonical finale needs
special staging.

## 5. Canonical route versus current behavior

| Phase | Required behavior | Current behavior |
| --- | --- | --- |
| Start | Talk to Noah or Alina; members gate; accept/refuse; optional lore; “When will this delegation arrive?” | Only Alina; one compressed exchange and Yes/Not now; no Noah, lore, members response, or states 2/4 |
| Arrival | Delegation approaches Roald/Aeonisig; Alina identifies Itzla and Servius; bag guard leaves Castle | A message says a guard sets off; no actors, movement, camera, dialogue, or music |
| Tail | Moving guard follows full path; player stays close and out of sight; five hiding points | Talking once to the guard prints success, writes 8, and teleports to the door |
| Failure/retry | Spotted or too far fails; talk to Noah/Alina to restart | No sight, distance, failure, cleanup, or retry state |
| House | Automatic/door-triggered eavesdropping scene with five bandits and assassination plan | Door prints “Soft-skip” and writes 10 |
| Report | Full Tobyn report and instructions to identify four of ten guards | Compressed to two lines |
| Marking | Any visible guard can be marked/unmarked; at most four; wrong set is rejected | Correct four can only be marked; wrong six can only be unmarked; all are hidden initially |
| Arrest | Tobyn validates exact set, rejects genuine guards, arrests bandits, asks for help, escorts player | Correct-four-only check ignores wrong marks and jumps straight to 16 |
| Roof | Player reaches cell room through normal Castle navigation | Quest shadows the shared stairs and teleports at hard-coded points |
| Interrogation | Itzla introduction, cell scene, target revelation, Varlamore invitation, staged 16/18/20/22 resume | First Tobyn conversation at any 16–22 completes immediately |
| Completion | 1 QP, no item/XP, three music unlock opportunities, first-flight entitlement 1 | Completion type is written, but coins are passed and first travel is not armed |

## 6. Stealth and marking subsystem requirements

### Suspicious-guard scene

The active follower is the direct `vmq1_bag_guard` NPC, not the static
`vmq1_bag_guard_varrock` wrapper that is visible only at primary value 8.
Quest Helper detects an active attempt by the presence of that direct NPC and
records a path from Varrock Square to the south-east house with five hiding
tiles. The modern implementation needs one scene/session owner that:

1. spawns or claims exactly one guard for the player at state 6;
2. moves it through the measured path with look-back pauses;
3. checks both distance and line of sight during exposed windows;
4. fails once when spotted or too far, removes the actor, and leaves a clear
   Noah/Alina restart route;
5. resumes safely after relog, region leave, teleport, death, or disconnect;
6. prevents two attempts, duplicate timers, or another player's NPC from
   advancing the state; and
7. transitions exactly once to 8, cleans the moving actor, and exposes the
   static house-scene representation if needed.

Tile overlays are client guidance, not the server rule. The server must own the
actual visibility/distance checks and failure timing. Capture the live facing,
pause duration, sight cone, maximum following distance, restart dialogue, and
automatic final scene trigger before freezing tests.

### Ten-guard puzzle

The cache exposes only `Mark` or `Unmark` on operation 1. Current script also
binds nonexistent operation 2 and handles the wrong halves of the population:

- guards 1–4 bind unmarked leaf/parent to Mark, but never bind their marked
  leaves to Unmark;
- guards 5–10 bind marked leaves to Unmark, but never bind their unmarked
  leaves/parents to Mark; and
- `~cots_guards_marked` checks only that guards 1–4 equal 2, ignoring whether
  any of guards 5–10 are also marked.

The canonical contract allows any candidate to be toggled, refuses a fifth
mark with “You've already marked enough guards,” and lets Tobyn accept only the
exact four impostors. A single shared operation-1 handler should identify the
symbolic wrapper/leaf, toggle its own two-bit field atomically, count all ten,
enforce a maximum of four, and remain idempotent under duplicate packets.
Tobyn must reject any four-mark set containing a genuine guard without changing
the primary phase. Correct validation owns the arrest/escort transition and
the native 12→14→16 progression, subject to live state capture.

## 7. Completion, first flight, and permanent access

Quest Helper live capture records this native sequence:

| Event | Required writes |
| --- | --- |
| Quest completes | `%vmq1=24`, `%vmq1_questcomplete_type: 0→2`, `%vmq2_first_travel: 0→1` |
| Player chooses “Let's do it!” with Regulus | `%vmq2_first_travel: 1→2` |
| Flight arrives in Varlamore | `%varlamore_visited: 0→1`, `%vmq2_first_travel: 2→3` |
| Landing conversation finishes | `%vmq2_first_travel: 3→4` |

The current quest completion omits `%vmq2_first_travel=1`, leaving the native
Varrock Regulus/Quetzal wrappers hidden at selector value 0. The world spawn
search also finds no Varrock Regulus wrapper, so both the entitlement and its
actor must be restored.

Conversely, `quest_twilightspromise/scripts/twilightspromise.rs2` handles
`vmq2_quetzal_keeper_1op` without checking `%vmq1` or
`%vmq2_first_travel`. At Twilight's Promise state 0/2 it immediately says
“Let's do it!”, writes `%varlamore_visited=1`, teleports to Civitas illa
Fortis, and advances that sequel. If its actor is restored naïvely, it bypasses
Children of the Sun and skips the native 1→2→3→4 first-flight lifecycle.

One shared Varlamore-access service should own:

- eligibility (`%vmq1>=24`);
- first-flight offer, flight scene, landing conversation, and durable writes;
- travel interruption and duplicate-action protection;
- post-first-flight Varrock Quetzal operations;
- fairy-ring access at every applicable destination; and
- the boundary between region access from Children of the Sun and the full
  Quetzal Transport System reward from Twilight's Promise.

Children completion should arm, not simulate, the first flight. The cheat and
account-import adapter should establish the same durable entitlement without
granting an item or starting Twilight's Promise.

### Downstream prerequisite audit

| Dependent content | Current local gate |
| --- | --- |
| Twilight's Promise | Missing; Varrock Regulus can start/teleport at sequel state 0/2 |
| At First Light | Explicit `Deferred: Children of the Sun hard gate`; start soft-skips it |
| Ribbiting Tale | Production start correctly requires `%vmq1>=24` and Woodcutting 15 |
| Death on the Isle | No `%vmq1` reader found in quest root |
| Meat and Greet | No `%vmq1` reader found in quest root |
| Ethically Acquired Antiquities | Explicitly soft-skips the Children prerequisite |
| Shadows of Custodia | No `%vmq1` reader found in quest root |
| Scrambled! | No `%vmq1` reader found in quest root |
| Vale Totems | No implemented quest root found in the current inventory |

Region access alone may make some starts unreachable in ordinary play, but it
is not a sufficient prerequisite boundary: teleports, admin tools, imports,
future spawns, and already-positioned accounts can bypass geography. Each
implemented dependent start/completion boundary should call the shared
Children-complete predicate, and tests should assert both below-24 refusal and
24 acceptance.

## 8. Findings and priority

### P0 — canonical route is blocked or corrupts shared behavior

1. No required `vmq1_*` world actor is spawned; the real quest cannot start.
2. All ten marking wrappers default to hidden at the only puzzle phase.
3. Exact quest-owned Castle stair/ladder triggers can suppress ordinary shared
   navigation outside states 16–23.
4. Completion does not arm Regulus first travel, so the headline reward is not
   delivered.
5. Twilight's Promise has no Children/first-travel gate and can bypass the
   prerequisite and native arrival lifecycle once Regulus is placed.

### P1 — critical gameplay and narrative behavior is missing

1. Noah, alternate-start identity, `%vmq1_met_alina`, start states 2/4, lore,
   members response, refusal, and canonical re-talks are absent.
2. Delegation arrival is absent despite the full native actor set.
3. Tailing is one Talk-to and a teleport; movement, stealth, failure, retry,
   cleanup, and concurrency do not exist.
4. The bandit-house scene is a message-only soft-skip.
5. The puzzle cannot toggle every candidate, permits no wrong selection, does
   not enforce four total, and validates only the four correct bits.
6. State 14 and finale states 18/20/22 have no production writers.
7. Arrest, escort, Itzla introduction, interrogation, target reveal, invitation,
   and finale dialogue are all skipped.
8. Completion passes an incorrect `coins` reward and has no exactly-once
   transaction combining state, completion type, first-flight entitlement,
   quest points, music, and scene cleanup.
9. Eight of nine currently implemented/listed downstream units lack a verified
   explicit gate; Ribbiting Tale is the exception.

### P2 — fidelity, recovery, and maintainability

1. Restore the three music unlock moments: delegation arrival, suspicious
   guard following, and interrogation.
2. Replace generic state labels with measured semantic ownership after live
   capture.
3. Split start, tail scene, house scene, puzzle, finale, and permanent access
   into bounded files/services while retaining one writer per transition.
4. Add recovery/migration for existing state-12 saves with zeroed guard bits,
   state-24 saves without first travel, and cheat-completed accounts.
5. Remove `soft-skip`/`Deferred` claims only when their gameplay and failure
   tests pass.

## 9. Modernization work packages

### Package 0 — fixtures, placement, and state ownership

- Capture live state writes at 0/2/4, 12/14/16, 16/18/20/22/24 and the exact
  start-actor/met-Alina semantics.
- Add current-revision symbolic spawn/scene fixtures for both starters, Tobyn,
  all ten guards, house actors, delegation, and roof actors; regenerate rather
  than hand-edit generated world files.
- Define authoritative predicates for started, following, puzzle active, exact
  four selected, finale active, complete, first-flight pending, arrived, and
  full Quetzal-network access.
- Add migrations for zeroed puzzle selectors and completed saves lacking
  completion type/first-travel entitlement.

### Package 1 — start and delegation

- Implement Noah and Alina through one shared start conversation with the full
  optional lore, members response, accept/refuse, state 2/4 re-entry, and
  `%vmq1_met_alina` ownership.
- Stage the delegation arrival using the native Roald, Aeonisig, Itzla,
  Servius, Furia, Ennius, knight, Tobyn, and bag-guard assets.
- Make cutscene state writes and music unlocks protected, interruption-safe,
  and exactly once.

### Package 2 — tail and house investigation

- Implement the measured moving-guard path, look-backs, hide points, distance,
  line of sight, failure, retry, and attempt cleanup.
- Add relog, teleport, region-leave, death, concurrent-player, and duplicate
  timer tests around the scene owner.
- Implement the house eavesdropping scene and state-8→10 transition using
  native bandit/guard actors, then restore canonical re-talk dialogue.

### Package 3 — guard puzzle and arrest

- Initialize all ten selectors to visible/unmarked on the canonical state-12
  entry and repair stale state-12/14 accounts.
- Bind only real operation 1 and support Mark/Unmark for every candidate with a
  maximum of four total.
- Validate the exact four impostors, retain state on a wrong set, and implement
  the arrest, offer, escort, and 12→14→16 transitions.
- Remove exact Castle navigation overrides and use the shared map-link/climb
  system.

### Package 4 — interrogation, completion, and access

- Implement the staged roof/cell sequence across native states 16/18/20/22,
  including Itzla, bandit, Tobyn, interruption/re-entry, and The Burning Sun.
- Complete atomically at 24 with one quest point, completion type 2,
  first-travel value 1, no fake item/XP, correct journal, and clean scene state.
- Implement Regulus's native 1→2→3→4 first flight and fairy-ring access through
  shared predicates; keep Twilight's Promise's full-network unlock separate.
- Update `::complete`/imports and all implemented dependents to consume the same
  permanent contract.

## 10. Verification plan

### Static and build checks

- Assert a placed symbolic wrapper or explicit owned scene spawn for every
  required world actor; reject unresolved numeric-id imports.
- Assert exactly one writer for each primary transition and each first-flight
  transition.
- Assert all ten wrappers initialize to 1, operation 1 toggles all ten, no
  operation-2 ghost binding remains, and exact validation includes all ten.
- Assert the quest root no longer owns permanent Varrock Castle climb locs.
- Assert no coin/item/XP reward and explicit `%vmq2_first_travel=1` completion
  ownership.
- Run `python3 tools/questhelper_extract.py childrenofthesun --check`.
- Run `make -C src mock230-scripts` and the intended-cache pack/check-only
  target after implementation.

### Automated transition and scene tests

1. Start from both Noah and Alina; exercise members/non-members response,
   decline, optional lore loops, re-talk at 0/2/4, duplicate clicks, and exact
   state/met-Alina writes.
2. Interrupt the delegation scene at each yield and verify one actor set, one
   music unlock, correct cleanup, and safe resume into 6.
3. Tail successfully at every boundary distance; test every look-back/hiding
   point, spotted, too far, teleport, death, logout, reconnect, region leave,
   two players, and restart from both starters.
4. Trigger the house scene once by the canonical boundary; test early door,
   repeated door, interruption, actor cleanup, and exact 8→10.
5. Enter state 12 with every valid/corrupt selector combination; verify ten
   visible unmarked candidates, every Mark/Unmark toggle, four-mark limit,
   duplicate packets, wrong four rejection, and exact correct-four acceptance.
6. Interrupt arrest and escort at every yield; verify no duplicate NPCs, no
   skipped 14/16, and safe placement/re-entry.
7. Traverse both Castle floors before, during, and after the quest using shared
   map links; confirm unrelated players are unaffected.
8. Interrupt every 16/18/20/22 interrogation checkpoint; verify actor
   visibility, cell placement, dialogue resume, music, and exactly-once 24.
9. Complete with normal/full inventory and duplicate completion delivery;
   verify 1 QP, no item/coins/XP, type 2, first travel 1, journal complete, and
   no leftover scene actor.
10. Exercise Regulus 1→2, arrival 2→3, landing 3→4, cancellation, relog,
    duplicate travel, post-flight travel, and fairy-ring entry.
11. Attempt every implemented dependent below and at 24; ensure no geographic,
    teleport, cheat, or already-positioned bypass.
12. Run `::complete quest_childrenofthesun` twice and verify the first creates
    the durable completion/first-flight contract and the second is a no-op.

### Real-client Gate D evidence

- Record the two start paths, arrival scene, all five hide points, both tail
  failures, house scene, every guard leaf transform, wrong-set rejection,
  arrest/escort, Castle navigation, staged interrogation, reward scroll,
  Regulus flight, landing, fairy ring, and one downstream gate.
- Capture packets/var changes at all uncertain primary and first-flight writes.
- Compare journal text at every primary state with the pinned route.
- Record build, pack, automated, headless, and real-client commands/results in
  this dossier before changing status to `verified-modern`.

## 11. Audit evidence and disposition

Evidence run on 2026-08-17:

- read the complete 324-line quest root and all local state writes;
- inspected the native quest dbrow, primary/side varbits, NPC multinpc ranges,
  guard leaf operations, bandit door, and Castle map links;
- searched every production `.spawn` and confirmed no `vmq1_*` placement;
- inspected the spawn generator's dropped-name report instead of accepting
  stale numeric ids;
- searched all production `%vmq1`, first-travel, visited, Regulus, journal,
  cheat, downstream-quest, and Castle-loc readers/writers;
- inspected the pinned Wiki article, guide, transcript, NPC/access pages, and
  local Quest Helper source/capture notes; and
- ran `python3 tools/questhelper_extract.py childrenofthesun --check`
  successfully.

No gameplay/config code was changed and no compile, pack, automated gameplay,
headless route, or real-client test is claimed. Children of the Sun remains
`audit-pending`; restoring a start NPC alone, or making `::cotsrun` reach 24,
would not satisfy Gates A–D.
