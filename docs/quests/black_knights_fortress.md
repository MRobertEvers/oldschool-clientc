# Black Knights' Fortress modernization audit

Status: `audit-pending` — the cache-native quest row, complete 0–4 primary
state ladder, 12-quest-point start gate, Sir Amik dialogue, disguise entrance,
fortress map, navigation, grill conversation, cabbage discrimination, journal,
reward call, cheat arm, and downstream prerequisite checks exist. The normal
route is nevertheless hard-blocked after the grill: the banquet door searches
for an unplaced base `fortressguard`, while the map contains only four numbered
variants. The port also omits the dossier and its recovery/read lifecycle, all
modern alternative disguises, the native sabotage cutscene and cauldron
transform, post-sabotage grill dialogue, safe coin delivery, and quest
speedrunning support.

Audited: 2026-08-17

Governing plan: [Quest modernization plan](../QUEST_MODERNIZATION_PLAN.md). This
record applies that plan's Gates A–D to Sir Amik Varze, the dossier, the quest
point gate, every fortress guard variant, all supported disguises, fortress
doors and navigation, the listening grill, Witch/Greldo/Captain/Cat scene,
cabbage validation, cauldron transform, reward, journal, speedrun metadata,
cheat adapter, and the shared Recruitment Drive, Wanted!, King's Ransom,
Dragon Slayer II, and While Guthix Sleeps surfaces. It is an implementation
specification, not completion evidence.

## 1. Authoritative references

These stable OSRS Wiki revisions define the current quest, dialogue, item
lifecycle, shared location behavior, presentation, rewards, and downstream
contracts.

| Reference | Pinned revision | Use in this audit |
| --- | --- | --- |
| [Black Knights' Fortress](https://oldschool.runescape.wiki/w/Black_Knights%27_Fortress?oldid=15290630) | 15290630, 2026-08-08 | Identity, requirements, route, disguises, cutscene, reward, and downstream quests |
| [Black Knights' Fortress/Quick guide](https://oldschool.runescape.wiki/w/Black_Knights%27_Fortress/Quick_guide?oldid=14710393) | 14710393, 2024-07-31 | Exact navigation, dialogue choices, item use, and return route |
| [Transcript:Black Knights' Fortress](https://oldschool.runescape.wiki/w/Transcript%3ABlack_Knights%27_Fortress?oldid=15289971) | 15289971, 2026-08-07 | Start/refusal, capacity/recovery, guard subjects, grill scenes, sabotage cutscene, and finale |
| [Black Knights' Fortress (location)](https://oldschool.runescape.wiki/w/Black_Knights%27_Fortress_(location)?oldid=15285133) | 15285133, 2026-08-01 | All accepted disguise families, NPC behavior, and later-quest use of the shared fortress |
| [Dossier (Black Knights' Fortress)](https://oldschool.runescape.wiki/w/Dossier_(Black_Knights%27_Fortress)?oldid=15185327) | 15185327, 2026-04-22 | Grant, replacement, Read/Destroy, interruptible self-destruction, banking, and post-quest retention |
| [Transcript:Dossier (Black Knights' Fortress)](https://oldschool.runescape.wiki/w/Transcript%3ADossier_(Black_Knights%27_Fortress)?oldid=14902387) | 14902387, 2025-05-16 | Exact three-line self-destruct text |
| [Fortress Guard](https://oldschool.runescape.wiki/w/Fortress_Guard?oldid=15289953) | 15289953, 2026-08-07 | Four visible variants, entry hint, meeting warning, and combat identity |
| [Cabbage](https://oldschool.runescape.wiki/w/Cabbage?oldid=15255126) | 15255126, 2026-07-06 | Ordinary versus Draynor Manor cabbage identity and sources |
| [Sir Amik Varze](https://oldschool.runescape.wiki/w/Sir_Amik_Varze?oldid=15276997) | 15276997, 2026-07-28 | Shared quest subjects and Temple Knight handoff |
| [Ruined Potion](https://oldschool.runescape.wiki/w/Ruined_Potion_(Black_Knights%27_Fortress)?oldid=15303876) | 15303876, 2026-08-17 | Authored sabotage jingle and event timing |
| [Recruitment Drive](https://oldschool.runescape.wiki/w/Recruitment_Drive?oldid=15292308) | 15292308, 2026-08-10 | Immediate downstream prerequisite and Sir Amik recommendation subject |
| [King's Ransom](https://oldschool.runescape.wiki/w/King%27s_Ransom?oldid=15292350) | 15292350, 2026-08-10 | Downstream prerequisite and later fortress/disguise reuse |

The sources identify Black Knights' Fortress as quest #12, an intermediate,
very short, free-to-play quest released 6 April 2001. It requires 12 quest
points at start and no mandatory kill; the player must evade or survive level
33 Black Knights. The local cache's recommended combat level is 15.

Transition aid only: the local Quest Helper checkout's
[`BlackKnightFortress.java`](https://github.com/Zoinkwiz/quest-helper/blob/5ea99d5ea9ba3fb096ebe7b5ed02d80883e9819d/src/main/java/com/questhelper/helpers/quests/blackknightfortress/BlackKnightFortress.java)
at commit `5ea99d5ea9ba3fb096ebe7b5ed02d80883e9819d` confirms active states
0/1/2/3, all fortress zones, every ladder/door/grill/hole coordinate, recovery
from later-quest basement and turret areas, required items, and rewards. It is a
transition-test aid and does not override the Wiki, transcript, or cache.

`python3 tools/questhelper_extract.py blackknightfortress --check` resolves all
five items, the Sir Amik NPC, twelve loc names, `cutscene_status`, and every
coordinate. Its only failure is an audit-tool naming inference: it looks for
`quest_blackknightfortress`, while the real, manually verified cache row is the
plural `quest_blackknightsfortress`. Modernization should add that explicit
alias to the extractor rather than misreport a missing dbrow.

## 2. Native quest identity and player contract

| Field | Native value / expected behavior |
| --- | --- |
| Cache quest ID | 12 |
| Type | Free-to-play quest |
| Difficulty / length | Intermediate / very short |
| Series | None; its completion opens the Temple Knight chain |
| Release date | 6 April 2001 |
| Start | Sir Amik Varze on floor 2 of the White Knights' Castle, approximately 2960,3336,2 |
| Start requirement | 12 quest points, checked before acceptance |
| Required items | One ordinary cabbage, plus a valid worn fortress disguise |
| Combat | No required kill; level 33 Black Knights are aggressive to lower-combat players and meeting-room knights always attack intruders |
| Primary state | `%spy`, clean transmitted permanent varp 130 |
| Side state | `%spy_cauldron_multi` and `%spy_armour_hint`, bits 0–2 of native carrier `spy_cauldron` |
| Quest points | 3 |
| Item reward | 2,500 coins |
| Unlocks | Eligibility for Recruitment Drive and King's Ransom; shared fortress remains visitable |
| End state | 4 |
| Speedrun | Native row, best-time varp and trophy varbit exist; no server subsystem currently consumes them |

The dbrow's identity, type, difficulty, length, release date, quest-point
requirement, recommendation, reward QP, end state, and speedrun link are
coherent. Unlike several earlier audits, its primary metadata does not point at
the wrong prerequisite.

### Native state inventory

| State | Constant | Required phase |
| ---: | --- | --- |
| 0 | `blackknight_not_started` | General Sir Amik dialogue; enforce 12 QP; explicit Yes/No; grant dossier before commit |
| 1 | `blackknight_started` | Infiltrate in disguise, navigate to grill, learn the cabbage weakness |
| 2 | `blackknight_listened` | Enter the meeting room, reach the hole, sabotage with ordinary cabbage |
| 3 | `blackknight_sabotaged` | Return to Sir Amik and claim reward |
| 4 | `blackknight_complete` | Downstream quests and ordinary/post-quest subjects |

| Side varbit | Bits | Required ownership |
| --- | ---: | --- |
| `%spy_cauldron_multi` | 0–1 | Per-player intact/sabotaged `bkf_cauldron_multi` transform; value 1 after successful cutscene |
| `%spy_armour_hint` | 2 | First/repeat guard uniform hint; exact live transition should be captured before final verification |

The cache carrier `spy_cauldron` is not declared by any server overlay and
neither varbit is referenced by production. Consequently the cauldron remains
visually intact at states 3 and 4, and the cheat adapter cannot establish
coherent permanent presentation.

## 3. Implementation surface

Paths below are relative to `OSRS-Content/osrs239-content/`.

### Quest-owned files

| Path | Present responsibility | Audit result |
| --- | --- | --- |
| `server/scripts/quests/quest_blackknight/configs/quest_blackknight.constant` | States, 3 QP, 12-QP requirement | Correct values; no side-state/cutscene constants |
| `configs/quest_blackknight.varp` | `%spy` overlay | Correct clean transmitted permanent declaration; native `spy_cauldron` carrier is absent |
| `scripts/quest_blackknight.rs2` | Doors, disguise gate, grill, cabbage, aggro, completion | Main ladder exists, but banquet guard lookup cannot resolve, only one disguise works, presentation is message-only, and completion coin grant is unsafe |
| `scripts/fortress_guard.rs2` | Guard Talk-to | Binds only unplaced `fortressguard`; all four visible numbered variants fall through |
| `scripts/blackknight_journal.rs2` | Dynamic journal | Tracks 0–4, but falsely says level 33 knights must be killed and omits dossier/recovery/alternative disguises |

The quest root totals 271 lines across five files. The main header explicitly
describes a LostCity port, substitutes teleport-through for real door
open/close behavior, and drops sound, session logging, and progress/completion
presentation. Those are recorded deferrals, not fidelity evidence.

### Mandatory shared and cross-directory surfaces

| Path / subsystem | Relationship | Audit result / required ownership |
| --- | --- | --- |
| `configs/all.dbrow` | `quest_blackknightsfortress` and `speedrun_blackknightsfortress` | Quest row is coherent; speedrun row, loadout/unlocks, trophy times, best varp, and trophy varbit have no server runtime |
| `configs/all.obj` | `bk_dossier`, wieldable dossier, armor/cabbage/coins | Dossier and temporary held form exist but no quest script produces or reads them |
| `configs/all.seq` / `all.spotanim` / synth dbrow | Dossier and cabbage scenes | Reading/explosion, listening pose, throw/projectile/tumble/bubbles, seven sounds, and ruined-potion jingle are all unused |
| `configs/all.loc` | Fortress doors, grill, hole, ladders, cauldron multi | Route assets exist; cauldron transform keys off unwritten side state |
| `areas/world/configs/m47_54.spawn` | Guards, Witch, Captain, Greldo, Cat, aggressive knights | All scene actors exist, but only `fortressguard_01`–`_04` are placed; no base `fortressguard` exists |
| `areas/falador/scripts/sir_amik_varze.rs2` | Start, recovery/report/finale, Recruitment Drive, Wanted!, DS2 | Primary dispatcher exists; it skips dossier and current start confirmation, abbreviates reports, and needs a full overlap matrix |
| `ladders_stairs/scripts/*` and maplink data | Fortress vertical route | Cache categories and same-tile plane links cover the QH path; verify every rung live and retain later-quest recovery routes |
| `doors/scripts/*` and quest-local walk-through proc | Door geometry | Quest uses direct `p_teleport` fallback without open locs; modernize atomically and test both directions/collision/repeated packets |
| `player/scripts/drop.rs2` | Dossier `Destroy` op5 fallback | Generic op5 drops the untradeable dossier to the ground; quest must override with authored destroy confirmation/semantics |
| `interface_questjournal/scripts/quest_journal.rs2` | Journal dispatcher | Correctly calls `~blackknight_journal` |
| `quests/scripts/quest_cheat.rs2` | Completion adapter | Writes only `%spy=4`; does not set cauldron state or clean test-owned state/items |
| `quest_recruitmentdrive` | Direct sequel and Sir Amik subject | Correctly depends on completion through the shared Sir Amik route; debug procs alone force prerequisites and are not production bypasses |
| `quest_kingsransom` | Downstream requirement and fortress basement | Requirement reads `%spy`; later route assumes the standard disguise and shares entrance/secret-wall behavior |
| `quest_wanted`, `quest_dragonslayer2`, `quest_recipefordisaster` | Later Sir Amik subjects | Dispatcher priority must preserve each eligible branch and separate world/cutscene variants |
| `quest_whileguthixsleeps` | Later fortress and elite/Dark Squall disguise use | Shared entrance contract must accept current later-game outfit families without weakening full-set validation |
| Quest completion/reward services | QP, count, reward scroll | Generic call is present; quest-local state/item commit must be once-only and capacity-safe |

### Cache-native assets already available

- quest state varp, cauldron/hint carrier varbits, quest/speedrun rows, best-time
  varp, trophy varbit, reward and item configs;
- normal and temporary wieldable dossier forms, reading animation, dossier
  explosion player animation and spot animation;
- exact sturdy/meeting/secret doors, grill, hole, ladders/stairs, intact and
  sabotaged cauldron locs, and a per-player multivar wrapper;
- Witch, Black Knight Captain, Greldo, Black Cat, four fortress guard variants,
  and aggressive Black Knights at the required public coordinates;
- cabbage throw animation, thrown/travelling/bubbling spot animations, seven
  named sabotage synths, and the Ruined Potion jingle row; and
- all standard, black/trimmed, elite black, and Dark Squall equipment items
  needed by the current shared-location disguise contract.

Modernization should connect these through current item transactions,
interruptible item reading, protected cutscenes, per-player transforms, shared
disguise predicates, authoritative door guards, combat aggression, completion,
and speedrun services. It should not replace them with chat messages or
quest-local duplicate assets.

## 4. Native reachability and first blocker

The standard bronze-med-helm plus iron-chainbody disguise opens
`bkfortressdoor1`. The secret wall, six ladder transitions, and `witchgrill`
then permit a normal state-1 player to hear the conversation and reach state 2.
The route becomes deterministically blocked at the dining-room door:

1. `bkfortressdoor2` calls `npc_find(..., fortressguard, ...)` before showing
   the required warning and crossing the door.
2. The only placed guards are `fortressguard_01`, `_02`, `_03`, and `_04`.
   They are distinct NPC configs, not transforms of the base type.
3. The branch has no fallback when the exact lookup fails, so it neither opens
   the door nor explains why.
4. The route to `dk_meeting_ladder` and the cabbage hole therefore cannot be
   reached legitimately at state 2.

The same name mismatch makes `[opnpc1,fortressguard]` unreachable from every
visible guard. It also makes the no-disguise entrance silently fail instead of
showing its explanation, although a correctly worn standard disguise bypasses
that lookup and still enters.

| State | Current reachability / defect |
| ---: | --- |
| 0 | Correctly rejects fewer than 12 QP, but starts immediately after the danger choice with no explicit Yes/No, no free-slot check, and no dossier |
| 1 | Standard disguise/secret wall/ladders/grill are reachable; black, trimmed, elite, and Dark Squall disguises are rejected; guard Talk-to and uniform hint are unbound |
| 2 | Hard-blocked at the meeting door because exact `fortressguard` is absent; with a teleport/bypass, ordinary cabbage can still write 3 |
| 3 | Sir Amik reports success and writes 4 before queueing reward; cauldron side state was never changed and coin grant can fail at full inventory |
| 4 | Recruitment Drive and King's Ransom gates read completion; retained dossier, sabotaged cauldron, later disguises, and speedrun state remain incoherent |

## 5. Current versus required playable route

| Chapter | Current route | Modernized route contract |
| --- | --- | --- |
| Start/QP | 12 QP is checked, then either danger branch starts immediately | Preserve both character responses, show explicit `Start quest? Yes/No`, require one outcome slot for dossier, and commit 0→1 only after dossier grant succeeds |
| Dossier | Entirely absent | Grant one `bk_dossier`; recover from Sir Amik when genuinely lost; Read uses temporary held form, countdown, animations, and deletes only if uninterrupted; Destroy must not ground-drop; retained unread dossier survives completion |
| Fortress entry | Only exact bronze med helm + iron chainbody; missing guard gives silence | Centralize full-set predicate for standard, black/trimmed/gold-trimmed, elite black, and Dark Squall outfits; enforce it authoritatively at door; use any valid placed guard only for presentation |
| Guard subjects | Base NPC handler is unplaced | Bind all four visible variants through one proc; preserve no-disguise hint, disguised response, first/repeat hint bit, meeting warning, and attack behavior |
| Vertical route | Generic same-tile plane transitions and direct door teleports | Validate every QH coordinate and both directions; retain protected movement/animation/collision, secret-wall behavior, basement/turret recovery, and no route skips |
| Grill | Linear chat writes 2; no listening pose; later states say nothing useful | Reserve the three actors, use listening animation/camera as authored, commit once after the full scene, and provide post-sabotage ruined-potion conversation at 3/4 |
| Meeting room | Exact base-guard lookup blocks entry | Resolve a placed guard variant or run authoritative door dialogue independent of NPC liveness; Yes stays outside, defiant choice crosses and triggers meeting-room aggression |
| Cabbage | Correct ordinary/magic ID test; messages only | Reject every non-cabbage and pre-listen attempt without consumption, retain Draynor cabbage, consume exactly one ordinary cabbage, then run protected sabotage cutscene |
| Sabotage presentation | Four messages, no actors/camera/projectile/sound, primary state only | Animate throw/travel/cauldron reaction and Witch/Captain/Cat dialogue, play Ruined Potion, set cauldron multi and state 3 together only after the committed event |
| Finale | State increments, unchecked coin add, queue repeats state 4 | Recheck state 3, deliver or ground 2,500 coins according to live behavior, commit QP/count/state exactly once, retain eligible dossier, and preserve downstream Sir Amik subjects |
| Speedrun | Cache rows only | Integrate with shared quest-speedrunning reset/loadout/timer/trophy service; reset `%spy`, cauldron/hint, dossier, location, and temporary encounter state coherently |

## 6. Item, disguise, and state transactions

### Dossier lifecycle

1. Starting at 0 requires 12 QP, explicit Yes, and one free inventory slot. A
   failed grant leaves `%spy=0`.
2. Sir Amik replaces a dossier only when the player genuinely owns none in
   inventory/bank/other supported storage. The transcript's full-inventory
   fallback must be reproduced rather than duplicating it.
3. Read temporarily presents the wieldable form and exact self-destruct text.
   Movement/dialogue interruption before destruction leaves the dossier.
4. Uninterrupted completion plays the explosion/cooling animation and removes
   one dossier. `Destroy` uses authored non-drop semantics.
5. Completion does not blanket-delete an unread dossier; the pinned item page
   explicitly says it can be retained after the quest.

### Valid worn disguises

The entrance predicate must require one complete family, never mix partial
pieces across families:

- bronze med helm plus iron chainbody;
- black full helm, black platebody, and black platelegs or plateskirt, including
  recognized trimmed/gold-trimmed variants;
- the complete elite black armour set; or
- the complete Dark Squall robe set.

Other worn slots are unrestricted. The quest article confirms the first two;
the location reference defines the later shared-fortress sets. The outfit is
needed only to enter, so normal quest players may remove it once inside.

### Sabotage transaction

The item-on-hole handler must validate state 2 and exact ordinary cabbage,
reserve the player/cutscene, consume one cabbage, run the protected scene, set
`%spy_cauldron_multi=1`, then commit `%spy=3`. Cancellation before the committed
throw must retain the cabbage/state; interruption after commit must recover to
the sabotaged visual/state rather than replay or leave an intact cauldron.

Completion must similarly validate state 3, establish a capacity-safe 2,500
coin outcome, then commit the quest once. Repeated packets, reconnect, and a
queued callback must not duplicate coins, quest points, quest count, or
speedrun result.

## 7. Oversight register

### P0 — release blockers and integrity

1. **The route is hard-blocked at state 2.** The meeting door searches for
   unplaced `fortressguard`; only numbered variants exist, and failure has no
   door/dialogue fallback.
2. **Visible guard interactions are unimplemented.** All four placed variants
   offer Talk-to, but the only trigger binds the absent base NPC.
3. **Completion reward can be lost.** `%spy` reaches 4 before an unchecked
   `inv_add(coins,2500)`; a full inventory with no coin stack can finish without
   receiving the authored reward.
4. **Sabotage state is split and only half is written.** `%spy` advances, but
   the native per-player cauldron transform remains intact permanently.
5. **Dossier op5 would leak an untradeable quest item.** The generic op5 handler
   ground-drops it unless the quest overrides `Destroy` before adding the item.

### P1 — current fidelity and shared systems

1. The current dossier grant, capacity gate, replacement, reading animation,
   interruptible self-destruction, and retained post-quest item do not exist.
2. The current explicit start confirmation is absent.
3. Black/trimmed/gold-trimmed, elite black, and Dark Squall disguises are
   rejected by an exact two-item check.
4. Guard behavior depends on an exact nearby NPC lookup rather than the
   authoritative door; a dead/moved/missing presentation actor creates silence.
5. Listening and sabotage scenes omit pose, camera, actor staging, projectile,
   cauldron bubbles/explosion, dialogue, sound effects, and jingle despite all
   major assets existing.
6. The post-sabotage grill conversation is replaced with “I can't hear much
   right now.”
7. `%spy_armour_hint` is unused and its first/repeat behavior is flattened.
8. Meeting-room aggression uses public nearby NPC mode changes without an
   explicit multi-player regression contract.
9. Quest speedrunning metadata exists, but no server timer/loadout/reset/trophy
   integration exists anywhere in the script tree.
10. Sir Amik's Black Knights' Fortress dialogue is abbreviated around Temple
    Knights, the intermediate report, dossier recovery, and the final report.

### P2 — presentation and maintainability

1. The journal incorrectly says the player must kill level 33 knights; the
   current quest requires no kills.
2. The journal omits the dossier, full disguise alternatives, exact recovery,
   and retained-item behavior.
3. `~update_blackknight_progress` performs blind arithmetic rather than named,
   guarded transitions.
4. Direct door `p_teleport` is a documented stand-in with no visual door state
   and needs collision/direction/repeated-packet evidence.
5. Quest cheat sets only state 4 and leaves side state/presentation incoherent.
6. The Quest Helper extractor's singular/plural dbrow inference creates a false
   unresolved result for an existing row.

## 8. Modernization packages

### Package 0 — native state, metadata, and test adapter

- declare/transmit permanent `spy_cauldron` ownership and document both side
  varbits without changing cache IDs;
- replace blind increments with explicit guarded transitions;
- add the `quest_blackknightsfortress` extractor alias;
- make the cheat/state adapter establish coherent primary, cauldron, hint,
  dossier, and location state; and
- inventory the speedrun row/loadout/unlocks as a dependency on the shared
  speedrun service.

Exit evidence: config compile, all state-writer scan, extractor with no false
unresolved row, and adapter tests for states 0–4 plus side-state reset.

### Package 1 — Sir Amik and dossier service

- restore current dialogue, explicit acceptance, and no-mutation refusal;
- reserve capacity and atomically grant the dossier before state 1;
- implement bank-aware replacement and the exact no-room fallback;
- implement interruptible Read with native temporary form/animations/spotanim;
- override Destroy so it never becomes a public ground item; and
- arbitrate Black Knights' Fortress, Recruitment Drive, Wanted!, Dragon Slayer
  II, and ordinary Sir Amik subjects deterministically.

Exit evidence: 11/12 QP, Yes/No, 0/1 free slot, banked/lost/destroyed dossier,
movement interrupt at every countdown step, retained post-quest dossier, and
shared-quest priority tests.

### Package 2 — fortress access, guards, and navigation

- centralize the complete worn-disguise predicate and use it for every shared
  entrance consumer;
- bind `fortressguard_01`–`_04` to one dialogue procedure and make doors
  authoritative if a presentation actor is unavailable;
- preserve no-disguise hint, disguised response, meeting warning/refusal,
  defiant entry, and aggression;
- modernize door movement and validate every QH ladder/maplink coordinate; and
- preserve King's Ransom/WGS basement, turret, secret wall, and recovery paths.

Exit evidence: every complete/partial/mixed disguise, guard alive/dead/moved,
both door directions, all six outbound and return ladder links, collision,
repeated packet, and two-player meeting entry.

### Package 3 — investigation and sabotage cutscenes

- use the native listening pose, actors, camera, and full transcript before
  committing state 2;
- implement the current state-3/4 ruined-potion grill branch;
- validate all item-on-hole cases and exact cabbage identity;
- run the protected throw/projectile/cauldron/dialogue/audio sequence; and
- atomically commit cauldron transform plus state 3, with reconnect-safe cleanup.

Exit evidence: state0/1/2/3/4 grill matrix; non-item, Draynor, and ordinary
cabbage; no cabbage loss before commit; cutscene cancel/logout/death/reconnect;
per-player cauldron visuals; competing players; jingle and actor staging.

### Package 4 — finale, downstream contracts, and speedrun

- restore full intermediate/final Sir Amik reports;
- deliver/drop coins safely and complete exactly once;
- verify Recruitment Drive and King's Ransom reject state 3 and accept state 4;
- regression-test all later fortress disguises and Sir Amik subjects; and
- integrate shared speedrun start/reset/timer/trophy/best-time behavior without
  changing the normal route.

Exit evidence: inventory capacity/repeated completion tests; exact 3 QP and
2,500 coins; downstream state3/4 matrix; later-quest fortress access; and native
speedrun loadout/reset/trophy verification.

## 9. Verification matrix

### Static gates

- compile configs and RuneScript with no duplicate NPC/door/item triggers;
- rerun the extractor and manually cross-check every QH coordinate/zone;
- prove Sir Amik and all scene actors are placed, and prove no code relies on
  the unplaced base guard;
- scan every `%spy`, `%spy_cauldron_multi`, and `%spy_armour_hint` writer;
- scan every dossier/cabbage/coin producer, consumer, drop, death, and bank path;
- prove each accepted disguise maps to exact worn item families; and
- prove both downstream dbrows/scripts read the same end-state predicate.

### Automated route gates

1. Clean free-to-play route 0→4 with exactly 12 QP and the standard disguise.
2. Start refusal, 11 QP, full inventory, dossier grant failure, and re-talk.
3. Dossier bank/replacement/read/destroy/interruption/post-quest retention.
4. Every valid full disguise and every partial/mixed/ornament variant.
5. Guard Talk-to and both doors with each numbered variant alive, absent,
   moving, in combat, and used concurrently by two players.
6. Every outbound/return ladder, secret wall, sturdy door, meeting door,
   basement, turret, logout, and reconnect position.
7. Grill at states 0–4, including repeated click and actor contention.
8. Hole with arbitrary item, no cabbage, Draynor cabbage, ordinary cabbage,
   wrong state, repeated packet, and cutscene interruption.
9. Per-player intact/sabotaged cauldron for two players at different states.
10. Finale with full/no-coin-stack inventory, existing coin stack, reconnect,
    and double invocation; reward/QP/count change once.
11. Recruitment Drive and King's Ransom gates at 3/4; DS2/Wanted!/WGS and all
    later fortress outfit overlap cases.
12. Speedrun reset/loadout/timer/trophy/best-time plus abort/restart/normal-save
    separation.

### Live evidence required before `verified`

- authoritative guard variant used at each door and behavior when it is dead or
  already in combat;
- exact `%spy_armour_hint` transition and first/repeat text;
- dossier replacement at full inventory, interruption frame, and Destroy
  confirmation behavior;
- exact accepted trimmed/gold-trimmed, elite black, and Dark Squall item IDs;
- full listening and cabbage cutscene camera/tick/audio sequence;
- state/side-state commit point and reconnect result during sabotage;
- 2,500-coin full-inventory outcome; and
- native quest-speedrunning loadout, timer units, trophy thresholds, and reset
  semantics for this quest.

## 10. Completion criteria

Black Knights' Fortress may move from `audit-pending` to `verified` only when
Packages 0–4 are implemented; a clean 12-QP free-to-play account can complete
0→4 without teleports or cheats; all visible guards and shared disguises work;
the dossier, cutscenes, cauldron, cabbage, coins, and retained-item state are
transactional and recoverable; completion is once-only; Recruitment Drive and
King's Ransom gate correctly; later Sir Amik/fortress consumers regress cleanly;
and the cache-advertised speedrun contract is either operational through the
shared service or explicitly tracked as an unresolved release dependency. A
state-4 write or reward-scroll call alone is not evidence that the quest is
modern or playable.
