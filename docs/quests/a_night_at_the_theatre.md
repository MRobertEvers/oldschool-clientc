# A Night at the Theatre modernization audit

Status: `audit-pending` — the shared completion call exists, but the normal
gameplay route cannot reach it and the Theatre is only a one-room stub.

Audited: 2026-08-16

Governing plan: [Quest modernization plan](../QUEST_MODERNIZATION_PLAN.md). This
record applies that plan's Gates A–D to one quest implementation unit and its
mandatory Theatre of Blood dependency. It is not completion evidence.

## 1. Authoritative references

These revisions are pinned so later implementation and review use the same
requirements, route, dialogue contract, raid behavior, and rewards.

| Reference | Pinned revision | Use in this audit |
| --- | --- | --- |
| [A Night at the Theatre](https://oldschool.runescape.wiki/w/A_Night_at_the_Theatre?oldid=15291381) | 15291381, 2026-08-09 | Requirements, narrative route, enemies, rewards, and change history |
| [A Night at the Theatre/Quick guide](https://oldschool.runescape.wiki/w/A_Night_at_the_Theatre/Quick_guide?oldid=15288044) | 15288044, 2026-08-05 | Ordered interactions, items, travel, fights, and prior-raid skip |
| [Transcript:A Night at the Theatre](https://oldschool.runescape.wiki/w/Transcript:A_Night_at_the_Theatre?oldid=15245915) | 15245915, 2026-07-01 | Start/refusal, re-talk, item loss, memories, raid commentary, and reward claim branches |
| [Theatre of Blood/Entry Mode](https://oldschool.runescape.wiki/w/Theatre_of_Blood/Entry_Mode?oldid=15301035) | 15301035, 2026-08-14 | Six-room Entry Mode, party, wipe/retry, supply, and boss mechanics |
| [Antique lamp](https://oldschool.runescape.wiki/w/Antique_lamp_(A_Night_at_the_Theatre)?oldid=15190220) | 15190220, 2026-04-22 | Four-lamp quantity, valid skills, destroy, and reclaim behavior |

The source comments name Quest Helper's `anightatthetheatre`, but no Quest
Helper checkout or extracted fixture is present in this workspace. It may be
added as a transition/test aid; the pinned Wiki remains authoritative.

## 2. Native quest identity and player contract

The native `quest_nightatthetheatre` dbrow supplies the core metadata:

| Field | Native value / expected behavior |
| --- | --- |
| Quest ID | 163 |
| Type | Members' quest |
| Difficulty / length | Master / medium |
| Release date | 3 June 2021 |
| Start | Mysterious Stranger inside Ver Sinhaza, beside the Theatre entrance |
| End state | `%tobquest = 86` |
| Quest points | 2 |
| Direct prerequisite | A Taste of Hope |
| Indirect prerequisites | Darkness of Hallowvale, In Aid of the Myreque, In Search of the Myreque, Nature Spirit, Priest in Peril, and The Restless Ghost |
| Required levels | None; 95 Combat is recommended, not required |
| Required non-raid items | Ivandis flail or blisterwood flail, a saw (crystal saw works), a ghostspeak amulet or Morytania legs 2+, and any axe except the blessed axe |
| Recommended support | Drakan's medallion, energy/stamina restoration, antivenom or antipoison, additional antipoison for Hespori, fairy-ring access, a druid pouch or Fire of Dehumidification, and high-end multi-style combat gear with poison/venom capability |
| Mandatory enemies | One level-105 vyrewatch and a level-302 quest Hespori |
| Avoidable enemies | Venomous level-96/146 araxytes; since the 2024 change they require 92 Slayer to harm, but the quest must not require killing them |
| Conditional raid | A complete Theatre of Blood run in any mode unless the player already had a qualifying completion before reaching that choice |
| Rewards | 2 quest points and four quest-specific lamps, each granting 20,000 XP in Attack, Strength, Defence, Ranged, Magic, or Hitpoints at level 50 or above |
| Downstream requirement | The Blood Moon Rises in the current Wiki revision |

The current completion string incorrectly lists “Access to the Theatre of
Blood” as a reward. The quest requires entering the Theatre before completion,
and the pinned Wiki reward list contains only the quest points and lamps.

## 3. Implementation surface

Paths below are relative to `OSRS-Content/osrs239-content/`.

### Quest-owned files

| Path | Present responsibility | Audit result |
| --- | --- | --- |
| `server/scripts/quests/quest_nightatthetheatre/configs/nightatthetheatre.constant` | Names 21 primary states, coordinates, and lamp count | Values align broadly with native state checkpoints; several states have no normal writer |
| `server/scripts/quests/quest_nightatthetheatre/configs/nightatthetheatre.varp` | Redeclares native `tobquest_main` | Native carrier is appropriate, but ownership of its many side bits is undocumented |
| `server/scripts/quests/quest_nightatthetheatre/scripts/nightatthetheatre.rs2` | Journal, quest interactions, completion, and debug walk | 322 lines, eight explicit soft-skips, no start choices, no complete normal route |

### Mandatory shared and cross-directory files

| Path | Relationship | Audit result / required ownership |
| --- | --- | --- |
| `server/scripts/minigames/minigame_tob/scripts/tob.rs2` | Mandatory raid path | Explicit solo Maiden-only stub; no party, notice board, later rooms, loot lifecycle, or quest-finish transition |
| `server/scripts/minigames/minigame_tob/configs/tob.constant` | Stub lobby/spawn/outside coordinates | Uses one static room instead of an owned party instance |
| `server/scripts/minigames/minigame_tob/configs/tob.varp` | Authored temporary `tob_active` and `tob_maiden_started` flags | Ignores the cache-native party/progress fields and cannot represent a six-room raid |
| `server/scripts/quests/quest_druidspirit/scripts/filliman.rs2` | Owns Filliman's real Talk-to trigger | Delegates to a one-line AKD-style soft-skip and does not enforce ghostspeak/Morytania legs for this branch |
| `server/scripts/quests/quest_druidspirit/scripts/quest_druidspirit.rs2` | Owns the Nature Grotto entrance | Teleports directly to the Hespori area and writes state 42; needs a real island/stepping-stone/instance lifecycle |
| `server/scripts/interface_questjournal/scripts/quest_journal.rs2` | Dispatches the native quest dbrow | Correct modern dispatch; journal content is too coarse |
| `server/scripts/quests/scripts/quest_cheat.rs2` | Idempotently sets `%tobquest` to 86 | Keep as an end-state adapter, not gameplay verification |
| `server/scripts/areas/world/configs/m49_69.spawn` | Statically spawns `tob_maiden_100` in the stub room | Conflicts conceptually with `npc_add` of another Maiden at entry and lacks per-party ownership |
| `server/scripts/ladders_stairs/configs/ladders.loc` | Supplies the quest crypt entrance config | Verify its map placement and do not alias unrelated Slepe ladders to Ranis's crypt behavior |

### Cache-native content already available

The osrs239 cache includes considerably more content than the scripts use:

- `%tobquest` on `tobquest_main`, plus `tob_progress`, `tobquest_done_tob`,
  `tobquest_lamps`, `tobquest_bonus_lamps`, `tobquest_stranger_op`,
  `tobquest_given_acid`, `tobquest_hespori_awareness`,
  `tobquest_in_instanced_cave`, and `tobquest_used_notice_board`;
- Theatre party-slot, wave-progress, chest, supply, damage, and loot varbits;
- quest-specific key, head, note, acid, eggs, bark, and `tobquest_lamp` objects;
- quest vyrewatch, memory-scene characters, Hespori/flower, raid commentary,
  Story/Entry boss, Nylocas, and Verzik NPC variants;
- native transforming crypt coffin, spider-cave egg sac/skeleton, and Hespori
  locs; and
- the surface notice board, raid entrance, scoreboard, raid rooms, barriers,
  doors, and reward-area assets.

Modernization should connect these symbolic cache assets to current engine
systems before authoring substitutes. The exact semantics and historic-migration
role of both lamp fields must be confirmed before writing them.

## 4. Persisted state model and reachability

`%tobquest` occupies bits 8–14 of permanent `tobquest_main`. The script's named
values follow the cache transforms, but its writes skip multiple stable states.

| Value | Symbol | Intended milestone | Current normal writer / defect |
| ---: | --- | --- | --- |
| 0 | `not_started` | Not started | Reset/default |
| 6 | `stranger` | Accepted and viewed the Stranger's setup/eggs | No normal writer; start jumps to 8 |
| 8 | `crypt` | Sent to Ranis's crypt | First Stranger click, without prerequisite or acceptance checks; also gives a key incorrectly |
| 12 | `coffin` | Vyrewatch killed, key obtained, gate opened | Crypt entrance itself writes 12, bypassing the kill and gate |
| 14 | `head` | Ranis's head obtained | Coffin Search with one free slot |
| 16 | `more` | Head delivered / first memory checkpoint | No normal writer; Stranger removes the head and jumps to 20 |
| 20 | `spider` | Sent to the spider cave | Stranger dialogue with no memory cutscene |
| 22 | `skeleton` | Reached nest and can inspect evidence | Entering the cave teleports directly to the endpoint |
| 26 | `daer` | Sticky note obtained/read; seek Daer Krand | Skeleton advances even when no note can be added and does not require reading it |
| 28 | `eggs_cave` | Sulphuric acid obtained; return to sac | Generic `sanctuary_dark_wizard` dialogue writes it but grants no acid |
| 30 | `eggs` | Acid used and strange eggs recovered | Egg-sac click grants eggs without acid/use-on validation |
| 32 | `cutscene1` | Eggs delivered; memory sequence in progress/complete | No normal writer; Stranger deletes all eggs and jumps to 38 |
| 38 | `grotto` | Sent to Filliman | Direct jump from 30 |
| 40 | `hespori` | Filliman identifies the island Hespori | One-line cross-file soft-skip |
| 42 | `hespori_fight` | Hespori encounter begun | Grotto door teleport, without crossing the island or starting an owned fight |
| 44 | `bark_chop` | Hespori defeated; bark can be cut | Talking to/clicking Hespori instantly “defeats” it |
| 46 | `bark` | Hespori bark obtained | Script expects `tobquest_hespori_dead`; native transform exposes dead at 45, while state 44 resolves to growing, so the normal trigger appears unreachable |
| 48 | `cutscene2` | Bark delivered / final Ranis memories checkpoint | No normal writer; Stranger jumps from 46 to 54 |
| 54 | `tob` | Carry the Stranger through a qualifying Theatre run | Bark hand-in with cutscene skipped |
| 80 | `finish` | Raid and final memory complete; claim reward | Debug runner only; the Maiden stub never writes quest progress |
| 86 | `complete` | Permanent quest completion | Stranger completion call, reachable normally only if state 80 is injected |

The result is not merely reduced fidelity: the normal route cannot reach state
46 through the visible Hespori transform and cannot reach state 80 through the
raid. `::nattrun` hides both failures by assigning every milestone directly.

## 5. Current versus required playable route

### Stage 1 — start and Ranis's crypt

Required behavior:

1. The Stranger distinguishes players with and without prior Theatre kill
   count, verifies A Taste of Hope, explains the request, and offers Yes/No.
2. Acceptance records state 6 and instructs the player to bring a valid flail
   and saw; the Stranger does not hand over the crypt key.
3. In the crypt, the player kills a level-105 vyrewatch with a valid vampyre
   weapon. The authoritative kill grants/drops the crypt key.
4. The gate consumes or validates the key and actually opens for that player.
5. Searching Ranis's coffin requires a saw and free inventory slot. The head is
   replaceable from the coffin if lost and gives the already-held/full-inventory
   transcript responses.
6. Giving exactly one head to the Stranger runs the first complete memory
   cutscene and resumes safely if the player logs out between its checkpoints.

Current behavior gives the key at acceptance, teleports into the crypt, writes
the post-gate state on entry, has no vyrewatch encounter or saw/flail check, and
skips the memory.

### Stage 2 — araxyte nest and sulphuric acid

Required behavior:

1. The player traverses the Morytania Spider Cave; araxytes venom but are
   avoidable, so 92 Slayer must never become an accidental requirement.
2. Opening the intact egg sac without acid fails appropriately. The player
   searches the skeleton, obtains and reads the sticky note, and learns of Daer
   Krand.
3. Daer Krand's correct NPC branch reports the dead assistant, grants one vial
   of sulphuric acid if space permits, and supports acid loss/replacement using
   the native `tobquest_given_acid` state.
4. The player explicitly uses the acid on the full egg sac. The acid is removed
   atomically and the strange eggs are granted only if space is available.
5. The emptied sac can replace lost eggs as described by the transcript/cache
   transform. The Stranger removes one set and runs both egg-memory cutscenes.

Current behavior teleports to the destination, can advance without receiving
the note, talks to the generic `sanctuary_dark_wizard` instead of implementing
Daer Krand, never creates acid, and turns a plain egg-sac click into success.

### Stage 3 — Filliman and the quest Hespori

Required behavior:

1. Filliman's quest branch accepts a ghostspeak amulet or Morytania legs 2+ and
   provides the island directions/re-talk from the transcript.
2. The player reaches and crosses the stepping stone, then attempts to chop the
   plant with an allowed axe to begin a private/owned level-302 Hespori fight.
3. The fight implements the quest version's attacks and flowers, including the
   differences from Farming Guild Hespori. Death creates a normal gravestone
   outside the instance, and logout/re-entry cannot duplicate or strand it.
4. The defeated loc transforms correctly; chopping it with a valid axe grants
   one replaceable bark only when inventory space exists.
5. Giving the bark to the Stranger runs both remaining Ranis-memory cutscenes,
   including their intermediate re-talk checkpoints.

Current behavior routes Filliman through a one-line soft-skip, teleports via the
grotto door, lets a Talk-to/Chop operation kill Hespori, and appears to transform
to a non-interactive growing loc at state 44.

### Stage 4 — Theatre of Blood

Required behavior:

1. If a qualifying Theatre completion predates this stage, the Stranger offers
   a clear Yes/No skip. Otherwise the player uses the notice board and may form
   a party of one to five.
2. Entry, Story/Entry, and other qualifying modes share one authoritative raid
   completion service. For the quest path, the Stranger accompanies/commentates
   after each wave and before all six bosses.
3. The raid runs Maiden, Bloat, Nylocas waves/Vasilias, Sotetseg, Xarpus, and
   all Verzik phases in order, with mode/team scaling, supplies, Dawnbringer,
   transitions, owned instances, and loot/reward-room exit.
4. Entry Mode supports unlimited boss retries, bandages after wipes and at the
   appropriate chests, health/prayer restoration, clean logout/leave behavior,
   and no obsolete 100,000-coin death fee.
5. Only the authoritative full-raid result sets `tobquest_done_tob`/state 80;
   individual room deaths, Maiden kills, resigns, and debug entry do not.

Current behavior has no notice-board handler, opens a solo static Maiden room,
adds a boss where one is also world-spawned, provides no real Maiden mechanics,
then teleports outside on her death. Bloat through Verzik are explicitly
deferred and no raid event advances the quest.

### Stage 5 — final memories and rewards

Required behavior:

1. The Stranger runs the doctor/true-Verzik memory and the conversation with
   Sugadinti, with resumable checkpoints matching transcript states 80–86.
2. With four free slots, the Stranger grants four `tobquest_lamp` items and
   invokes shared completion exactly once. With insufficient space, dialogue
   remains in the reward-claim state instead of losing lamps.
3. Each lamp grants exactly 20,000 XP in Attack, Strength, Defence, Ranged,
   Magic, or Hitpoints at level 50+, excludes Prayer and non-combat skills, and
   updates native claim state atomically.
4. Destroyed or deferred lamps are reclaimable from the Stranger. Historic
   original/rework bonus-lamp fields are handled deliberately if this server
   supports migrated player saves.
5. Post-quest dialogue and downstream prerequisite checks use permanent state
   86. “Access to the Theatre” is removed from the reward scroll.

Current completion sets 86 before inventory checks, loops over free slots, and
silently loses any of four rewards that do not fit. It grants generic
`thosf_reward_lamp`, while the correct `tobquest_lamp` has no Rub handler.

## 6. Gap and oversight register

| Priority | Area | Current defect | Required correction |
| --- | --- | --- | --- |
| P0 | End-to-end reachability | Normal play cannot obtain bark through the state-44 loc transform and no raid action writes state 80. | Correct transform/checkpoint ownership and connect full raid completion to the quest; prove a no-debug start-to-finish run. |
| P0 | Start contract | A first click starts immediately, does not check A Taste of Hope, has no accept/refuse or prior-KC branch, and skips state 6. | Use native requirement metadata and modern `~p_choice*` dialogue; preserve refusal and separate prior-raid dialogue. |
| P0 | Crypt | Start grants the key, entering writes the post-gate state, and no vyrewatch, valid-flail, saw, drop, or gate-opening mechanic exists. | Implement the cache vyrewatch encounter, authoritative key grant/drop, actual gate transition, saw requirement, and retry/death paths. |
| P0 | Acid/egg route | The note may be lost on full inventory, is never read, Daer grants no acid, and an ordinary sac click gives eggs. | Make each item transition atomic; implement note Read, correct Daer branch, acid grant/replacement, and item-on-loc consumption. |
| P0 | Theatre | Only Maiden exists; it is a minimally scripted shared/static encounter. Five rooms, real mechanics, party/instance ownership, supplies, retries, and quest completion are absent. | Modernize the Theatre once as a shared raid subsystem and integrate the quest through authoritative party/raid events. |
| P0 | Rewards | State 86 is written before capacity checks; partial lamps are permanently lost; the wrong generic item is granted and neither lamp has a valid XP handler. | Retain a durable claim state until all four `tobquest_lamp` rewards are delivered/reclaimed; implement exact 20,000-XP skill selection and claim accounting. |
| P1 | Quest items | Key, head, note, acid, eggs, and bark lack complete lost-item, already-held, duplicate, full-inventory, and replacement behavior. Egg hand-in deletes every copy. | Implement transcript branches and remove/grant exactly one item only after the whole transition can succeed. |
| P1 | Spider cave | Entry teleports to the endpoint, ignores native instance state, and omits pathing/venom context. | Use the cache map/entrances, safe ownership rules, and current araxyte behavior without requiring 92 Slayer. |
| P1 | Hespori | Filliman and pathing are skipped; the boss is defeated by interaction; axe, flowers, combat, death, gravestone, and cleanup are absent. | Implement an owned quest encounter and correct native loc transforms, then test death/logout/re-entry. |
| P1 | Cutscenes | All five Ranis memories, raid interludes, final doctor memory, and Sugadinti exchange are absent. States 16, 32, and 48 are skipped. | Build resumable modern cutscenes with cache NPCs, cameras, animations, and explicit stable resume points. |
| P1 | Prior raid completion | `tobquest_done_tob` is set only by the quest completion proc/debug reset; the script cannot recognize or offer the Wiki's prior-completion skip. | Define the authoritative all-mode raid completion/KC query, migrate the native bit meaning if needed, and test both Yes and No choices. |
| P1 | Raid ownership | Player-local temp flags coexist with globally spawned/added NPCs in a static room. Another player can interfere and duplicate Maiden spawns are possible. | Use party-owned dynamic maps/entities, member validation, instance cleanup, and reconnect rules. |
| P1 | Journal | Twenty-one primary states are collapsed into nine broad buckets and do not report missing key/note/acid/eggs/bark, memory checkpoints, or raid status. | Render exact objectives from primary and side state, including reward claims and prior-KC choice. |
| P1 | Dialogue | Quest root contains no player choices and almost all transcript re-talk, decline, alternate, raid commentary, item-loss, and post-quest branches are missing. | Implement reachable transcript branches with modern chat menus and state/item/KC predicates. |
| P1 | Native-state use | Acid, Hespori awareness, notice board, cave instance, raid progress, lamp, and bonus-lamp bits are unused while authored stub flags model a narrower flow. | Confirm native semantics and make those fields the canonical persistent/client-visible state where appropriate. |
| P1 | Reward metadata | Completion claims Theatre access as a reward although the player must already enter it and the Wiki does not list it. | Remove the false unlock and audit downstream The Blood Moon Rises gating separately. |
| P1 | Test validity | `::nattrun` assigns all milestones, including impossible 46 and 80, before invoking completion. | Make tests execute real operations and authoritative encounter/raid callbacks; retain direct state setters only as isolated adapters. |
| P2 | Presentation/lore | Memory cameras, NPC staging, exact animations, music, wave dialogue, and readable quest books/records are unaudited or absent. | Reconcile cache assets and transcript after all critical paths work; document only genuinely cosmetic deviations. |

## 7. Modern-engine assessment

Parts to retain:

- native permanent `%tobquest` and related `tobquest_main` fields;
- symbolic cache names rather than raw NPC, loc, object, interface, or map IDs;
- dbrow-based journal dispatch and shared `~quest_complete_rewards` lifecycle;
- current trigger ownership delegation to Druid Spirit's real Filliman and
  grotto scripts, avoiding duplicate RuneScript triggers; and
- modern `~p_choice2` use in the raid stub as a UI idiom.

No `if_openmain` or `if_openoverlay` remains in the quest or raid roots. The
problem is therefore not a visible IF1 quest panel. It is that modern shared
lifecycle pieces surround a gameplay scaffold, while the raid scaffold ignores
the cache's party, wave, supply, loot, and quest state.

The target architecture should be:

```text
quest state/dialogue
        |
        v
shared Theatre party + mode selection
        |
        v
party-owned dynamic raid (six ordered encounters)
        |
        v
authoritative full-raid result
        |
        +--> raid KC/loot lifecycle
        +--> A Night at the Theatre state 80 when eligible
```

Quest-specific C shortcuts or a second lightweight “quest Theatre” would leave
the wrong final state. Missing general party/dynamic-map capability, if proven,
belongs in the engine as reusable infrastructure; all quest and raid policy
remains RuneScript/config content.

## 8. Implementation sequence

### NATT-1 — formalize state, item, and raid contracts

- Add the quest and every external file above to the generated manifest.
- Confirm the exact native semantics of all `tobquest_main` fields and map every
  loc/NPC transform at states 0–86.
- Define one atomic quest-item helper pattern for grant, consume, loss,
  replacement, inventory-full, and duplicate actions.
- Define the shared raid result API for mode, party members, prior KC, quest
  eligibility, completion, wipe, leave, logout, and loot.

Acceptance: every state/side bit and item has one owner; no intermediate state
is skipped merely to avoid implementing its action.

### NATT-2 — implement start and crypt

- Add A Taste of Hope checks, prior-KC-aware start dialogue, acceptance/refusal,
  and state-accurate re-talks.
- Implement the vyrewatch/key/gate/coffin route with flail and saw validation.
- Add head loss/replacement, inventory-full, death, repeated-op, and relog tests.
- Implement the first memory and its resume checkpoint.

Acceptance: states 0–20 are reachable only through the pinned Wiki actions and
the crypt cannot be bypassed by entering or clicking repeatedly.

### NATT-3 — implement spider cave and item-use route

- Route the real cave and model venomous, avoidable araxytes.
- Implement sticky-note Read, Daer Krand dialogue/acid, acid-on-sac, and empty-
  sac replacement behavior with native side state.
- Implement both egg-memory cutscenes and intermediate re-talks.

Acceptance: states 22–38 survive relog, full inventory never advances without
the item, and a player without 92 Slayer can complete the route by avoidance.

### NATT-4 — implement Filliman and Hespori

- Complete the cross-quest Filliman branch with valid communication equipment.
- Implement the island/stepping-stone route, axe validation, private Hespori,
  flower mechanics, defeat transform, gravestone, and bark lifecycle.
- Implement the two bark-memory cutscenes.

Acceptance: states 40–54 are reachable through real travel/combat/actions; all
loc transforms are visible and operable at their named states.

### NATT-5 — modernize Theatre of Blood as shared content

- Implement the notice board, one-to-five-player party state, mode selection,
  dynamic map ownership, and reconnect/leave cleanup.
- Implement all six ordered encounters and room transitions, using Story/Entry
  cache variants and shared mechanics rather than quest-only fake bosses.
- Implement Entry Mode wipe/retry, bandages/supply chests, restoration,
  Dawnbringer, reward room, loot policy, and the Stranger's quest commentary.
- Publish one authoritative completion event that records raid completion and
  advances eligible quest members to 80.

Acceptance: solo and grouped Entry Mode runs complete all rooms; normal/hard
completion also satisfies the quest; partial runs never do; party members and
entities never leak across instances.

### NATT-6 — implement final memories and exact rewards

- Implement the final memory/Sugadinti sequence with restart-safe checkpoints.
- Replace `thosf_reward_lamp` with four `tobquest_lamp` claims and implement the
  six-skill level-50+ picker, destroy, and Stranger reclaim.
- Keep state below 86 until reward capacity/claim policy is satisfied; update
  lamp fields atomically and award two quest points once.
- Remove the false Theatre-access reward line and add correct post-quest text.

Acceptance: all four lamps yield exactly 80,000 total eligible combat XP, full
inventory loses nothing, destruction/reclaim is idempotent, and repeated final
dialogue/completion calls cannot duplicate points or lamps.

### NATT-7 — verify and remove scaffolding

- Replace `::nattrun` direct assignments with real-trigger test orchestration or
  retire it after equivalent automated coverage exists.
- Remove every active soft-skip/deferred marker from the quest's critical path
  and all static/shared raid shortcuts.
- Compile, pack, run state/party/raid tests, and capture a real-client quest
  smoke including cutscenes, interfaces, every room, and the reward scroll.

Acceptance: all Gates A–D pass and the manifest status can change from
`audit-pending` to `verified-modern`.

## 9. Verification matrix

| Scenario | Required assertions |
| --- | --- |
| Start | Missing A Taste of Hope blocks; refusal remains at 0; accept reaches 6; prior-KC and no-KC dialogue branches differ correctly |
| Crypt | Only an authoritative vyrewatch kill grants a key; valid flail/saw rules hold; locked gate blocks; head full-inventory/already-held/lost replacement paths work |
| Spider cave | Note is readable/replaceable; Daer grants one acid; acid must be used on the sac; araxytes venom but can be avoided without 92 Slayer |
| Eggs/memories | Lost eggs are recoverable from the empty sac; exactly one set is consumed; logout during either cutscene resumes safely |
| Filliman | Ghostspeak amulet and Morytania legs 2+ each work; neither equipped gives correct dialogue; Nature Spirit ownership remains intact |
| Hespori | Valid axe starts; flowers/boss mechanics work; death puts a gravestone outside; wipe/logout/re-entry cleans and restores a valid state; bark is replaceable |
| Prior raid | A qualifying pre-existing completion offers skip; Yes reaches the post-raid sequence; No runs the raid; no completion offers no skip |
| Party/instance | Sizes 1–5, leader/member readiness, concurrent parties, leave/kick/logout, reconnect, and cleanup cannot cross-contaminate state |
| Raid order | Maiden, Bloat, Nylocas/Vasilias, Sotetseg, Xarpus, and Verzik must all complete in one run; skipped or failed rooms never publish success |
| Entry retries | Unlimited retries, wipe bandages, supply chests, health/prayer restoration, and no obsolete death fee match the pinned guide |
| Raid result | Entry/normal/hard full completions qualify; Maiden-only/debug/resign do not; every eligible quest party member advances exactly once |
| Final sequence | Both final memories/dialogues resume after interruption and only reach reward claim when complete |
| Rewards | Zero through four free-slot cases lose nothing; four correct lamps are claimable; only six allowed skills at level 50+ receive 20,000 XP each; destroy/reclaim works |
| Completion | Two quest points are awarded once; journal/post-quest state is correct; The Blood Moon Rises sees state 86 |
| Cheat adapter | First `::complete quest_nightatthetheatre` reaches 86 and accounts for points; second invocation is a no-op |

Minimum repository checks after implementation:

```sh
tools/questhelper_extract.py anightatthetheatre --check
make -C src torirsserver-scripts
ToriRSServer_Pack --check-only
```

The Quest Helper command is conditional on adding the missing helper source or
fixture. Also record automated encounter/party suites and real-client packet/
screenshot captures; script compilation alone cannot prove a raid.

## 10. Definition of done

A Night at the Theatre may be marked `verified-modern` only when:

- the real Stranger start reaches state 86 without direct state assignments;
- requirements, choices, re-talks, every quest item, all memory cutscenes,
  crypt, spider cave, Hespori, and lost/full-inventory paths match the pinned
  article, guide, and transcript;
- a complete, party-owned six-room Theatre supports current Entry Mode behavior
  and shares authoritative results with normal/hard modes;
- prior completion and voluntary non-skip branches both work;
- rewards are exact, durable, reclaimable, and idempotent;
- native state and cache assets are used coherently, with no static NPC or
  authored-state shortcut substituting for party/instance ownership;
- no active critical soft-skip, deferred room, legacy panel open, raw-ID
  workaround, or quest-specific engine shortcut remains; and
- script compilation, cache packing, automated state/item/combat/party/raid
  coverage, real-client smoke evidence, and idempotent cheat evidence are
  recorded in this file.
