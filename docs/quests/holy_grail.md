# Holy Grail modernization audit

Status: `audit-pending` — the broad LostCity-era narrative path, native state,
combat stats, rewards, journal registration, restored-realm map, BJR fairy-ring
gate, King Arthur portrait gate, and King's Ransom prerequisite are present.
The quest is not currently completable through ordinary gameplay: neither of
the two required Draynor Manor magic whistles exists in world data or script,
and the Grail bell's inventory `Ring` option is wired to the wrong trigger
class. The magic gold feather also has no `Blow-on` handler. Public temporary
actors/items, unguarded realm objects, unchecked item grants, state-first
completion, and a stub Nightmare Zone leave major ownership, recovery,
transaction, and reward gaps after those blockers.

Audited: 2026-08-17

Governing plan: [Quest modernization plan](../QUEST_MODERNIZATION_PLAN.md). This
record applies that plan's Gates A-D to acceptance, shared Camelot/Entrana/
Galahad dispatch, napkin and whistle recovery, both Fisher Realm versions,
Titan combat, bell access, Fisher King dialogue, Percival discovery, grail
pickup, completion settlement, journals/admin adapters, and every direct
consumer found. It is an implementation specification, not evidence that the
quest has been modernized.

## 1. Authoritative references

The current OSRS Wiki revisions below are pinned so implementation and review
use stable requirements, dialogue, location, item-lifecycle, reward, and
integration contracts.

| Reference | Pinned revision | Use in this audit |
| --- | --- | --- |
| [Holy Grail](https://oldschool.runescape.wiki/w/Holy_Grail?oldid=15273068) | 15273068, 2026-07-23 | Identity, requirements, full route, rewards, and direct prerequisite |
| [Holy Grail/Quick guide](https://oldschool.runescape.wiki/w/Holy_Grail/Quick_guide?oldid=15292972) | 15292972, 2026-08-11 | Ordered actions, exact route, equipment, and alternate return travel |
| [Transcript:Holy Grail](https://oldschool.runescape.wiki/w/Transcript%3AHoly_Grail?oldid=15263251) | 15263251, 2026-07-14 | Offer/warning, side dialogue, interaction messages, recovery, and completion boundary |
| [Black Knight Titan](https://oldschool.runescape.wiki/w/Black_Knight_Titan?oldid=15199191) | 15199191, 2026-04-28 | Stats, immobility, Excalibur death rule, respawn, XP, and NMZ unlock |
| [Magic whistle](https://oldschool.runescape.wiki/w/Magic_whistle?oldid=15183338) | 15183338, 2026-04-22 | Draynor visibility, two spawns, travel zone, realm return, and replacement |
| [Holy table napkin](https://oldschool.runescape.wiki/w/Holy_table_napkin?oldid=15183340) | 15183340, 2026-04-22 | Galahad source, whistle visibility, and safe-loss conditions |
| [Magic gold feather](https://oldschool.runescape.wiki/w/Magic_gold_feather?oldid=15183365) | 15183365, 2026-04-22 | Arthur source, direction behavior, and postquest disposal |
| [Holy grail (item)](https://oldschool.runescape.wiki/w/Holy_grail_%28item%29?oldid=15212877) | 15212877, 2026-05-19 | Worthiness gate, one-item limit, destroy/recovery route, and King's Ransom reuse |
| [Grail bell](https://oldschool.runescape.wiki/w/Grail_bell?oldid=15183499) | 15183499, 2026-04-22 | Fisherman reveal, one-item limit, ring behavior, and postquest ownership |
| [Sacks](https://oldschool.runescape.wiki/w/Sacks_%28Holy_Grail%29?oldid=15113056) | 15113056, 2026-01-26 | `Prod`/`Open`, Percival discovery, and persistent postquest prod behavior |
| [Fisher Realm](https://oldschool.runescape.wiki/w/Fisher_Realm?oldid=15264829) | 15264829, 2026-07-16 | Corrupted/restored world topology, access, occupants, and item spawns |
| [The Fisher King](https://oldschool.runescape.wiki/w/The_Fisher_King?oldid=14996031) | 14996031, 2025-09-28 | First-visit king and succession narrative |
| [Sir Percival](https://oldschool.runescape.wiki/w/Sir_Percival?oldid=15273067) | 15273067, 2026-07-23 | Sack discovery, whistle hand-off, and restored king identity |
| [Galahad](https://oldschool.runescape.wiki/w/Galahad?oldid=15018317) | 15018317, 2025-11-07 | Napkin/tea dialogue, replacements, and Kandarin diary interaction |
| [King Arthur](https://oldschool.runescape.wiki/w/King_Arthur?oldid=15292084) | 15292084, 2026-08-10 | Start, feather replacement, completion, and later Camelot dialogue |
| [Excalibur](https://oldschool.runescape.wiki/w/Excalibur?oldid=15182999) | 15182999, 2026-04-22 | Unboostable Attack 20 equip gate and 500-coin replacement |
| [King's Ransom](https://oldschool.runescape.wiki/w/King%27s_Ransom?oldid=15292350) | 15292350, 2026-08-10 | Only direct quest prerequisite and later holy-grail item reuse |
| [Nightmare Zone](https://oldschool.runescape.wiki/w/Nightmare_Zone?oldid=15271187) | 15271187, 2026-07-21 | Completion-gated Titan variant and its no-Excalibur rule |

These sources define a members, intermediate, short quest released 23 July
2002 as Camelot series entry 2. Merlin's Crystal must be complete. Attack 20
is unboostable but deliberately is not required to start; it is needed to
wield Excalibur. Combat level 50 is recommended, and the level-120 Black
Knight Titan must be defeated. Completion awards two quest points, 11,000
Prayer XP, 15,300 Defence XP, Fisher Realm access, the King Arthur POH
portrait, and the Black Knight Titan in Nightmare Zone. Holy Grail is directly
required only by King's Ransom.

Current mechanics that must survive modernization include:

- the low-combat warning followed by the standard `Start the Holy Grail
  quest?` Yes/No boundary;
- two whistles visible and pickable in the Draynor top-floor south room only
  while the napkin is in inventory, including room re-entry and drop-trick
  reacquisition;
- whistle travel only from the four Brimhaven tower pillars, with blowing it
  in either Fisher Realm returning to Brimhaven;
- damage with any weapon, but Excalibur held when the Titan reaches zero or it
  heals fully;
- fisherman-revealed bell pickup, failure away from the broken wall, and
  castle entry only at the valid ring area;
- both Fisher King topics, followed by Arthur's feather, sacks, Percival's
  one-whistle hand-off, and a restored second visit that skips the Titan;
- only one bell and one grail owned at a time, and no grail before worthiness;
  and
- postquest whistle recovery from Draynor or restored-realm spawns.

Transition aid only: Quest Helper's
[`HolyGrail.java`](https://github.com/Zoinkwiz/quest-helper/blob/5ea99d5ea9ba3fb096ebe7b5ed02d80883e9819d/src/main/java/com/questhelper/helpers/quests/holygrail/HolyGrail.java)
at commit `5ea99d5ea9ba3fb096ebe7b5ed02d80883e9819d` confirms primary
states 0, 2, 3, 4, 7, 8, and 9; the Draynor room, exact Brimhaven zone,
corrupted/restored realm areas, bell tile, castle floors, required items, and
reward values. The file last changed in
`3d7bba42fee847bd35717bbc7eac039efc129653` on 2025-09-28. Running
`python3 tools/questhelper_extract.py holygrail --check` resolves every named
item, NPC, loc, dbrow, and world point. Quest Helper uses remembered dialogue
text to infer that the fisherman revealed the bell because primary state does
not encode it. It cannot prove server writes, private visibility, actor/item
ownership, collision, queue safety, transactions, migration, or current-client
presentation.

## 2. Native quest identity and state contract

| Field | Native value / expected behavior |
| --- | --- |
| Cache row | `quest_holygrail`; quest metadata ID 31 |
| Implementation root | `server/scripts/quests/quest_grail` |
| Type / difficulty / length | Members quest / intermediate / short |
| Release / series | 23 July 2002 / Camelot #2 |
| Start | `king_arthur` in Camelot |
| Primary state | `%grail`, native permanent/transmitted varp 5 |
| Proven values | 0 not started; 2 started; 3 spoke to Merlin; 4 spoke to crone; 7 failed to defeat Titan with Excalibur; 8 finding Percival; 9 gave Percival a whistle; 10 complete |
| Unresolved values | 1, 5, and 6 are not named, read, or written in this repository; current-server capture must establish whether they are reserved, legacy, or reachable |
| End / quest points | State 10 / 2 QP |
| Skill policy | Attack 20, unboostable, checked by Excalibur equip rather than quest acceptance |
| Other requirements | Merlin's Crystal complete; ability to defeat level-120 Titan |
| Direct rewards | Prayer 11,000 XP; Defence 15,300 XP; Fisher Realm; Arthur portrait; NMZ Titan |
| Direct downstream quest | King's Ransom |

The native dbrow correctly records end state 10, two quest points, Merlin's
Crystal, Attack 20, deferred skill checking, recommended combat 50, both XP
awards in tenths, and the quest's identity metadata. The equipment config also
correctly assigns Excalibur `levelrequire,attack,20`. Those facts should remain
data-driven.

No native Holy Grail support varbits were found. Primary state alone does not
durably express several current mechanics:

| Support responsibility | Why state 0/2/3/4/7/8/9/10 is insufficient |
| --- | --- |
| Crone presentation | State 3 to 4 does not identify the summoned actor, owner, or interrupted conversation |
| Napkin issued/recovery | State remains 4/7 while the item can be held, banked, destroyed, or replaced |
| Draynor room visibility generation | Two per-player spawns and re-entry/drop-trick behavior cannot be represented by state 4 alone |
| Titan passage attempt | State 7 records only an Excalibur failure; no primary state records a successful kill before state 8 |
| Fisherman/bell reveal | The player remains state 4/7 and current Quest Helper must infer this from dialogue history |
| Bell ownership and castle entry | One-item ownership, valid reveal, and first-visit access are not primary-state milestones |
| Fisher King dialogue topics | State 8 proves the Percival request but not independently that the grail explanation was heard |
| Feather issue and Percival reveal | State 8 covers no feather issue, sacks reveal, private actor lifetime, or retry |
| Completion settlement | State 10 cannot prove grail consumption, either XP grant, QP, completion count, or unlock publication |

Use native cache carriers if current-server capture identifies them. Otherwise
add versioned server-side support records owned by the quest. Do not fabricate
client-visible primary values 1, 5, or 6 merely to fill numeric gaps.

### Required state capture and migration

Capture varp 5, any hidden support variables, all item domains, relevant map
state, actor/object visibility, and dialogue after each canonical action. Run
fresh, low-combat, full-inventory, death, logout, region-leave, duplicate-item,
and concurrent-player cases for every primary value 0-10.

| Existing shape | Risk | Migration rule |
| --- | --- | --- |
| State 0 with quest items | Old debug/spawn access or unrelated King's Ransom fixture | Do not infer progress; quarantine illegal quest-item ownership and preserve legitimate later-quest fixtures by provenance |
| State 1, 5, or 6 | Unknown native/legacy meaning | Capture before mapping; never silently coerce to the nearest authored constant |
| State 4/7 without napkin or whistle | Normal loss, but currently unrecoverable because Draynor spawns are absent | Restore canonical Galahad/Draynor recovery; do not advance state just for issuing items |
| State 4/7 with bell | Public bell leakage or interrupted first visit | Validate reveal/realm provenance; never use bell possession alone as proof of Titan or fisherman progress |
| State 8 without feather | Normal before Arthur or item loss | Arthur must issue/reissue safely across all item domains without changing state |
| State 8 with a shared Percival actor nearby | Current public dynamic actor may belong to another player | Never attach by proximity; discard/rebuild as an owner-scoped reveal |
| State 9 without a remaining whistle | Player gave away their only whistle; current Draynor recovery is absent | Restore canonical replacement and BJR access; do not roll state back |
| State 9 with more than one grail | Current unguarded static spawn can duplicate | Keep one legitimate quest item, remove extras with audit telemetry, and preserve King's Ransom provenance |
| State 10 missing XP/QP components | Current completion publishes state first | Repair only independently proven missing components; never replay both XP grants from state alone |
| State 10 with feather/napkin/bell | Temporary-item cleanup policy differs by item and current transcript | Capture exact current cleanup; do not globally delete unrelated item instances without provenance |

Admin fixtures must distinguish “set primary state for dialogue testing” from a
fully settled completion. Existing state-only `::complete` behavior cannot be
treated as a normal player history.

## 3. Implementation surface

The direct quest root contains seven scripts, two configs, and 744 lines, but
most route owners are distributed across shared area, Construction, diary,
minigame, combat, journal, and later-quest content.

| Path / subsystem | Present responsibility | Audit result |
| --- | --- | --- |
| `quest_grail.constant` / `.varp` | Named states and realm coordinates | Native varp is correct; values 1/5/6 and all support state are unresolved; travel zones are hand-authored and need capture |
| `quest_grail.rs2` | Completion, whistle travel, bell ring | Completion is state-first; whistle route is broad/raw; bell uses the wrong trigger class |
| `black_knight_titan.rs2` | Talk/fight, Excalibur death check, drops | Broad mechanic exists; distance fallback can bypass quest rule and no successful-kill support milestone exists |
| `grail_realm_npcs.rs2` | Fisherman, maiden, peasants, restored Percival | Dialogue is present; fisherman creates a public temporary bell with no player ownership/state |
| `fisher_king.rs2` | First king topics and state 8 | Dialogue is broad; no access/relevance gate or independent topic state |
| `sir_percival.rs2` | Sacks `Open`, dynamic Percival, whistle hand-off | `Prod` and feather navigation are missing; actor is public/proximity-based and remains after hand-off |
| `grail_crone.rs2` | Crone dialogue and state 4 | Direct Talk-to is unguarded; summoned actor/context and lifetime are unsafe |
| `grail_journal.rs2` | Journal rendering | Registered and detailed; uses item possession as progress and assumes collapsed milestones |
| Camelot `king_arthur.rs2` | Start, feather, finish, King's Ransom dispatch | Prerequisite works; modern offer/warning absent; item/reward operations are not transactional |
| Camelot `merlin.rs2` and eight knight scripts | Quest advice and side dialogue | Knight dialogue is broadly present; Merlin falsely enters Holy Grail dialogue even at state 0 |
| Entrana `high_priest_of_entrana.rs2` | Priest and dynamic crone | Other quest branches can shadow this quest; new crone is not rebound before dialogue |
| Seers `brother_galahad.rs2` | Napkin, replacement, tea | Unchecked grants, inventory-only duplicate checks, and missing returns cause false success/double dialogue |
| `m43_73.spawn` / `m41_73.spawn` | Corrupted/restored realm rosters | Both world copies exist; restored map has three whistles and one grail; no Draynor whistles exist anywhere |
| `poh_fairy_ring.rs2` | BJR availability and destination | Correctly gates BJR at state 9 and sends the player to restored m41_73 |
| `poh_crest.rs2` | Arthur portrait purchase | Correctly requires Merlin's Crystal and Holy Grail completion |
| `poh_quest_status_generated.rs2` | POH quest-list status adapter | Maps state 10 to complete and every nonzero value to started; unknown 1/5/6 values therefore appear in progress |
| `lady_of_the_lake.rs2` / equipment config | Excalibur replacement/equip | 500-coin replacement and Attack-20 config exist; replacement output is unchecked |
| `quest_kingsransom` | Later prerequisite and grail reuse | Explicit state-10 prerequisite exists; later quest uses the same item identity |
| `minigame_nightmarezone` | Completion unlock consumer | Entire minigame is a two-boss stub; Titan is not in wave selection or unlock logic |
| achievement diaries | Galahad tea and nearby Karamja tasks | Generic counters exist; no task-specific Galahad/Brimhaven hooks were found |
| quest journal/list and cheat | UI dispatch and admin completion | Journal row is registered; cheat writes state 10 only |
| automated tests | Route, state, items, queues, rewards | No Holy Grail-specific transition/concurrency/settlement tests were found |

The cache contains all principal item, NPC, loc, map, combat-stat, and quest
metadata symbols. The critical gaps are script ownership/lifecycle and missing
interactions, not missing cache identities.

## 4. Route reachability and fidelity

### Acceptance and Camelot advice

| Phase | Canonical transition | Current behavior / defect |
| --- | --- | --- |
| Eligibility | Merlin's Crystal complete; Attack is not checked at start | Arthur offers only when `%arthur` is complete, correctly enforcing the prerequisite |
| Offer | Warn below combat 50, then standard Start? Yes/No | Bespoke two-choice dialogue starts directly; warning and modern offer boundary are absent |
| Refusal | No state mutation | Existing refusal does not mutate, broadly correct |
| Acceptance | Write state 2 after Yes | Existing write is after the second Yes-like choice, but not through shared offer machinery |
| Merlin | Only relevant from state 2 onward; write state 3 | `merlin2` uses Holy Grail dialogue for every non-9/non-10 state, including state 0, where the player falsely says Arthur sent them |
| Knights | In-progress Grail side dialogue | Eight shared scripts broadly reproduce it; current exact gender-neutral wording and postquest priority need transcript review |

### Entrana, Galahad, and the Draynor whistle source

| Phase | Canonical transition | Current behavior / defect |
| --- | --- | --- |
| High Priest | Relevant state-3 route; crone interrupts as her own actor | Priest branch covers states 3-7 but follows Desert Treasure/Devious Minds branches; a newly added crone is not rebound as active NPC before `@grail_crone` |
| Crone | Own dialogue; write state 4 once | Direct `opnpc1` is available to any player and the public 500-tick actor is not owner-bound; interruption and another player's crone are unsafe |
| Galahad napkin | Checked add, no duplicate across canonical domains, replacement while needed | Initial state-4 check looks only in inventory; state-7 checks inventory/bank; every add is unchecked and dialogue claims success even at full inventory |
| Galahad flow | Exactly one coherent conversation and tea outcome | State-10 and state-7 branches omit `return`, so both can fall through into generic dialogue; several labels also call tea delivery before caller fallthrough calls it again |
| Draynor entry | Two whistles appear in the south room only with napkin in inventory | No Draynor whistle spawn, visibility controller, room-enter hook, or specialized pickup exists in the entire script tree |
| Whistle respawn | Timed despawn; re-entering room restores two; drop trick remains possible | Entire lifecycle is absent; the three world spawns found are all in restored m41_73 |

The missing Draynor source is a hard blocker. A player at state 4 cannot obtain
the first whistle and therefore cannot enter the corrupted Fisher Realm by any
ordinary route. BJR is correctly unavailable until state 9, so it cannot mask
this defect.

### Whistle travel and first Fisher Realm visit

| Phase | Canonical transition | Current behavior / defect |
| --- | --- | --- |
| Wrong location | Exact no-noise message and no movement | Current messages differ and state below 4 gets a separate response not shown in the transcript |
| Brimhaven zone | Only tiles between the four pillars (Quest Helper: 2740-2743, 3234-3237) | Radius 6 around 2740,3232 accepts a substantially broader hand-authored area |
| Crossing | Transition presentation, corrupted realm for state below 9 | Raw `p_teleport` and substitute mesbox; destination/collision path needs runtime capture |
| Realm exit | Whistle from the realm returns to Brimhaven | Bounding box over m41-m43 does this broadly; exact destination/presentation remain uncaptured |
| First/repeat map | Titan required on first visit/re-entry until Percival becomes king | State below 9 always selects corrupted m43_73; state 9+ selects restored m41_73, which is a reasonable two-map model pending topology proof |

The two static map copies avoid exposing restored scenery to state-4 players,
but their collision, spawn landing, doors, stairs, music, and cross-square
paths still require live smoke. Do not replace them with an instance merely
for architectural uniformity if current capture confirms the two-map design;
modernization is about correct ownership and lifecycle, not gratuitous map
rewrites.

### Black Knight Titan

| Contract | Current result / defect |
| --- | --- |
| Level/stats | Cache values match level 120, 142 HP, Attack 91, Strength 100, Defence 91, and extreme Ranged/Magic defence |
| Movement | World spawn and NPC design are static; verify collision/immobility live |
| Attack entry | Talk-to offers fight and calls retaliate; generic Attack is also configured |
| Final condition | Death queue checks Excalibur in worn inventory, correctly allowing another weapon before zero and delayed-hit weapon switching |
| Failure | State 4 becomes 7, Titan heals fully, and dialogue/message are broadly correct |
| Success | Player is moved beside the body and gets the success message; no durable successful-defeat milestone is written |
| Distance fallback | If killer is more than 11 tiles away when the queued handler runs, generic death executes before the Excalibur check; this is a potential bypass and must be removed/proven unreachable |
| XP | Current Wiki specifies only one XP per damage in the quest encounter; no explicit audit evidence proves this cache combat path applies that rule |
| Drops | Script manually reproduces a legacy drop table plus default death item; owner visibility and exact current table need capture |
| Concurrency | Public shared NPC can be fought/killed by multiple players; killer credit uses `npc_findhero`, but passage, respawn, loot, and simultaneous final hits need current-behavior proof |

The primary state intentionally jumps from 4/7 to 8 only after the Fisher
King request, so lack of a new primary value on Titan success is not itself a
bug. A support milestone may still be required for secure bell/castle access,
recovery, diagnostics, and migration.

### Fisherman, bell, castle, and Fisher King

| Phase | Canonical transition | Current behavior / defect |
| --- | --- | --- |
| Fisherman reveal | Relevant first-visit conversation reveals one bell at 2762,4694 | Any player can select the option; `obj_add` creates a public 500-tick bell and records no owner/reveal milestone |
| Bell pickup | One owned at a time, normal full-inventory handling | Generic pickup can take the public item; repeated fisherman conversations can create overlapping bells; no one-item guard exists |
| Ring inventory option | `Ting-a-ling-a-ling!`; fail away from bricks; maiden and teleport near bricks | Item config says `ifop1=Ring`, which requires `[opheld1,grail_bell]`; implementation registers `[opobj1,grail_bell]`, a ground-object op. The inventory Ring action has no handler and is a second hard blocker |
| Access authorization | Only a legitimately revealed bell in the correct realm/area should enter | Existing wrong-class handler has no state, realm, owner, or distance check and always teleports if somehow invoked |
| Fisher King access | First-visit NPC and relevant quest state only | Dialogue has no state gate; any player reaching the NPC can set state 8 from a lower state |
| Grail topic | Explains worthiness before the health/request branch | Full dialogue exists, but no independent topic bit exists; choosing health directly can write state 8 |
| Health/request | Set state 8 after identifying Percival | Existing state write broadly matches |

The transcript/guide says to complete both Fisher King topics. Capture whether
current OSRS enforces both with hidden support state or merely preserves them
as dialogue topology. Quest Helper advances using the health option alone, so
do not invent a mechanical lock without capture.

### Arthur's feather, sacks, and Percival

| Phase | Canonical transition | Current behavior / defect |
| --- | --- | --- |
| Feather issue | State 8, one free slot, replacement if lost | Arthur checks free space before global possession, producing the wrong full-inventory response for a banked feather; add is otherwise space-safe |
| Blow-on | Eight directions, within-five nearby, dungeon failure, and sacks-specific response | Item has `ifop1=Blow-on`, but no `[opheld1,magic_golden_feather]` exists, so every navigation response is missing |
| Sacks `Prod` | Muffled groan and wiggle, including postquest | No `oploc1` handler exists |
| Sacks `Open` | State 8 plus feather; reveal Percival with correct presentation | Broad gate exists, but actor spawns at the player's coordinate for 1,000 ticks and is public |
| Reveal ownership | Each player's reveal/dialogue must remain independent | `npc_find` treats any nearby Percival as already found; another player can suppress, share, or steal the interaction |
| No-whistle re-talk | Percival remains/reappears so the player can fetch one | Public actor lifetime substitutes for durable reveal state; logout/despawn/re-entry are unmodeled |
| Whistle hand-off | Delete exactly one after successful conversation, set state 9, Percival leaves | Delete then state write are not transactional; dynamic NPC is not deleted, so state-9 and postquest odd dialogue remain available until timeout |
| Restored succession | Second visit shows `king_percival`, healthy world, no Titan | Static m41_73 roster and state-9 travel/BJR gate provide this broadly |

The feather is guidance rather than a strict global routing requirement, but
the sacks correctly require it in inventory. Its absent handler is a major
narrative/interaction omission even though a guide-reading player could find
the sacks directly once the other blockers are repaired.

### Restored realm and holy grail pickup

| Phase | Canonical transition | Current behavior / defect |
| --- | --- | --- |
| Return | Remaining whistle or BJR; restored realm, no Titan | State-9 whistle and BJR both land in m41_73; broad behavior exists |
| Replacement whistle | Three restored-realm spawns plus Draynor recovery | Three static m41_73 spawns exist; Draynor recovery does not |
| Grail worthiness | Cannot take before the required task | Static restored-map spawn has no custom pickup guard; map selection normally hides it before state 9, but admin/cross-map access bypasses policy |
| One-item limit | Taking another gives the greedy message | Generic pickup has no total check, so a respawning static item can be duplicated |
| Full inventory | No loss or false pickup | Generic pickup is all-or-nothing and reports no space, broadly safe |
| Destroy/recovery | Return to the restored realm and take another | Static respawn provides replacement, but also provides unlimited duplicates and no provenance distinction for King's Ransom |
| Completion hand-in | Arthur consumes one legitimate grail at state 9 | Inventory count and state are checked, but no grail provenance is checked |

Implement specialized ground-pickup policy around the real static spawn rather
than removing the map source. It must preserve one-item ownership, worthiness,
Telekinetic Grab rejection, full inventory, destroy/recovery, and legitimate
later King's Ransom behavior.

## 5. Item, actor, and recovery contract

| Subject | Canonical acquisition/recovery | Modern owner required |
| --- | --- | --- |
| Excalibur | Existing sword or 500 coins to Lady of the Lake after Merlin's Crystal | Shared Camelot item owner; checked coin-for-item transaction and all-domain duplicate check |
| Holy table napkin | Galahad after crone; replacement while still needed | Galahad dialogue plus checked delivery; exact inventory/bank/ground policy captured |
| Magic whistle | Two player-visible Draynor room spawns with napkin; three restored-realm spawns; repeatable after loss | Player-scoped room visibility/generation and normal item pickup, preserving deliberate drop trick |
| Grail bell | Fisherman reveals one; only one owned; unavailable after completion | Player-scoped reveal/ground item or equivalent visibility record, correct held Ring handler, cleanup/recovery |
| Magic gold feather | Arthur at state 8, replace if absent | Arthur checked delivery plus exact navigation procedure; one global legitimate copy |
| Sir Percival | Revealed from sacks, reusable until hand-off, gone afterward | Player-scoped actor or per-player loc/NPC transform with durable reveal/recovery state |
| Holy grail | Static restored castle spawn at state 9; one at a time; repeat after destroy | Specialized pickup guard over map spawn with cross-quest provenance awareness |
| Crone | Appears during Priest conversation and speaks as herself | Conversation-scoped/owner-aware actor with correct active subject and cleanup |

Every grant must check capacity before claiming success. Every consume/output
pair must either be atomic or replay-safe. `~obj_gettotal` is useful for broad
duplicate prevention but cannot by itself prove quest provenance, distinguish
another quest's same item identity, or identify which settlement component
completed.

### Concurrency and lifecycle model

Current public dynamic entities make one player's dialogue mutate the world
for everyone:

- the High Priest's crone can be found/reused by another player;
- the fisherman creates a public bell that another player can pick up;
- sacks create a public Percival at the interacting player's coordinate;
- proximity to any Percival suppresses another player's reveal; and
- the static Titan and its manual drops have no documented multi-killer policy.

The modernization must support at least two players at every state in the same
region. Crone, bell, and Percival need explicit owner/visibility and cleanup
rules. The Titan may remain shared if current capture proves that is canonical,
but final-credit, heal/death, passage, loot, and respawn behavior must be
deterministic under simultaneous damage. Do not infer actor ownership from
nearest-player or proximity queries.

Test logout, death, region leave, map reload, server restart, NPC timeout,
ground-item timeout, repeated clicks, and a second player's interaction at each
temporary-entity boundary. Recovery should recreate only the missing view or
actor, not replay narrative state or duplicate items.

## 6. Completion settlement and downstream consumers

Arthur queues `grail_quest_complete`. The queue immediately writes state 10,
then deletes the grail, grants Prayer XP, grants Defence XP, and calls the
shared completion helper. There is no durable component record. A failure after
the state write makes Arthur route to postquest dialogue while one or more item,
XP, QP, count, or presentation operations are missing. Retrying all operations
would duplicate whatever already succeeded.

Replace this with a replay-safe settlement containing independent once-only
markers for:

1. verified state-9 and legitimate grail preconditions;
2. one grail consumption;
3. exactly 11,000 Prayer XP;
4. exactly 15,300 Defence XP;
5. two quest points and completed-count publication;
6. reward/unlock presentation; and
7. final state-10 publication.

Publish state 10 last, or require every state-10 read to reconcile incomplete
settlement first. Failure injection after every operation must resume only the
missing component. The reward string currently names both XP grants and Fisher
Realm access, but omits the Arthur portrait and NMZ Titan; capture the current
completion interface and present every supported unlock truthfully.

| Consumer | Canonical dependency | Current result / required work |
| --- | --- | --- |
| Whistle/Fisher Realm | State-appropriate first/restored access; postquest replacement | State routing exists, but missing Draynor items break both quest and postquest recovery |
| BJR fairy ring | Available after Percival receives whistle (state 9) | Explicit state-9 gate and restored destination are correct; verify shared fairy-ring prerequisites separately |
| Arthur portrait | Merlin's Crystal and Holy Grail complete, 1,000 coins | Explicit gate and purchase route exist; verify transaction/full inventory in Construction audit |
| POH quest-list status | Not started / started / complete | Generated adapter uses 0 / any nonzero / 10; verify unresolved states and partial settlement do not misreport |
| Black Knight Titan in NMZ | Holy Grail complete; NMZ variant does not require Excalibur | NMZ currently runs only Count Draynor and Elvarg in a two-wave stub; no Titan unlock/selection exists |
| King's Ransom | Holy Grail complete plus its other requirements | `kr_meets_requirements` explicitly checks state 10, correct once settlement is coherent |
| King's Ransom grail identity | Later replacement grail from Keep Le Faye | Shared `holy_grail` identity means cleanup/duplicate rules must respect later-quest provenance |
| Kandarin easy tea task | Have Galahad make tea | Generic diary counters exist; no Galahad task hook or per-task idempotence was found |
| Karamja easy incidental tasks | Nearby rope swing and gold mine | Not quest rewards/prerequisites; preserve shared area hooks when modernizing Brimhaven travel |
| Quest journal/list | Accurate state/recovery advice | Registered; possession-derived feather/grail branches and collapsed milestones need support-state parity |
| Quest cheat | Coherent admin fixture | Writes state 10 only and bypasses items, XP, QP settlement history, and unlock proof |

Nightmare Zone is a repository-wide partial subsystem, not a defect to solve
inside `quest_grail.rs2`. Holy Grail acceptance nevertheless requires a tracked
consumer test or an explicit external blocker; the quest dossier must not claim
the canonical unlock works while the minigame omits it.

## 7. Modernization sequence

### Phase 0 — current capture, ownership map, and save safety

1. Capture varp 5 and hidden support data after every canonical interaction,
   including unresolved values 1, 5, and 6.
2. Capture exact start/warning UI, Draynor visibility/respawn, Brimhaven tiles,
   realm landing/collision, Titan shared behavior, bell reveal/ring range,
   Fisher King topic persistence, actor lifetimes, and item cleanup.
3. Select the current engine's owner-aware NPC/ground-item/visibility machinery
   and replay-safe quest-settlement pattern.
4. Freeze fixtures for all primary states, illegal item combinations, public
   legacy actors/bells, duplicate grails, state 10 partial rewards, and
   state-only admin completion.
5. Define migration and cross-quest item provenance before changing any write.

Exit: all primary/support meanings, item domains, world geometry, entity
ownership, current messages, and partial-save repairs are evidence-backed.

### Phase 1 — acceptance, shared dialogue, and quest-item sources

1. Move Arthur acceptance to the modern quest offer with combat-50 warning and
   preserve Attack 20 as an unboostable equip-time rule.
2. Gate Merlin, Priest, crone, knights, and Galahad to exact relevant states and
   repair shared-dispatch priority without hiding other quests.
3. Make crone dialogue use the crone as subject with owner-safe lifetime.
4. Repair Galahad's returns, tea path, checked napkin delivery, duplicate/loss
   handling, and Kandarin task hook.
5. Implement the two player-visible Draynor whistles, exact room lifecycle,
   pickup/full-inventory behavior, drop trick, and postquest recovery.
6. Implement the feather's complete held-item direction procedure and sacks
   `Prod` behavior.

Exit: a fresh player can accept, reach state 4, obtain/recover the napkin and
two whistles, and use every configured item/loc option without public leakage
or false-success dialogue.

### Phase 2 — realm travel, Titan, bell, and first castle visit

1. Replace the radius whistle predicate with the captured four-pillar zone and
   exact wrong-location/realm-return presentation.
2. Validate both map copies, landing points, collision, doors, stairs, music,
   and interruption-safe transitions.
3. Close the Titan distance bypass; enforce held Excalibur at zero HP, one-XP-
   per-damage, exact heal/death, killer credit, drop ownership, and shared-NPC
   concurrency.
4. Introduce a durable owner-scoped fisherman/bell reveal and one-item policy.
5. Replace `[opobj1,grail_bell]` with the correct held-item handler, exact ring
   zone, failure messages, maiden dialogue, and safe castle transition.
6. Gate Fisher King access and persist only the current-server topic/milestone
   semantics before state 8.

Exit: state 4/7 reaches state 8 through the canonical combat and castle path,
with two simultaneous players unable to steal bells, skip dialogue, or affect
one another's access.

### Phase 3 — Percival, restored realm, grail, and settlement

1. Make sacks reveal a player-owned/recoverable Percival with exact dialogue,
   no-whistle retry, and immediate cleanup/transform after hand-off.
2. Set state 9 and consume one whistle through a replay-safe transition.
3. Preserve both whistle and BJR restored-realm routes and validate no-Titan
   topology, King Percival, healthy scenery, and replacement spawns.
4. Guard the static grail pickup by worthiness, one-item total, provenance,
   Telekinetic Grab, full inventory, and destroy/recovery policy.
5. Implement replay-safe completion with state 10 last and migrate partial
   settlements component by component.
6. Drive journal/admin fixtures from the same support/settlement model and
   verify portrait, King's Ransom, diary, and NMZ consumers.

Exit: a player can complete from the real start, recover from every loss or
interruption, receive each reward exactly once, and exercise every direct
unlock through a coherent completion history.

## 8. Required tests

### State, acceptance, shared dialogue, and migration

- Fresh state 0 shows the correct journal; Arthur offers only after Merlin's
  Crystal.
- Combat below 50 shows the warning but does not block acceptance; Attack below
  20 can accept but cannot wield Excalibur; boosts do not satisfy its equip
  requirement.
- Refusal changes no state; Yes writes state 2 only at the captured boundary.
- Merlin at state 0 never claims Arthur sent the player; state 2 writes 3 once;
  all re-talk/postquest branches are correct.
- High Priest routes Holy Grail alongside every relevant Desert Treasure,
  Devious Minds, and other shared-NPC state without accidental shadowing.
- Crone dialogue uses the correct actor and only the triggering player can see,
  continue, or clean it up.
- Every primary value 0-10 and unresolved 1/5/6 fixture renders deterministic
  dialogue/journal or enters explicit repair handling.
- Legacy public crone/bell/Percival and partial state-10 fixtures migrate
  without fabricated progress or duplicated items/rewards.

### Galahad, napkin, whistles, and feather

- Each Galahad choice produces exactly one dialogue/tea outcome; state-7 and
  state-10 paths do not fall through.
- Tea and napkin at zero free slots report failure and neither claim delivery
  nor advance any support state.
- Initial and replacement napkins obey captured inventory/bank/worn/ground
  duplicate rules and remain obtainable while required.
- The Draynor room shows exactly two whistles only with napkin in inventory;
  outside players, banked napkin, and unrelated rooms do not see them.
- Pickup at 0/1/2 free slots, despawn, room exit/re-entry, drop trick, logout,
  restart, death, and two players in the room all match current behavior.
- Postquest loss can recover a whistle from Draynor and each restored spawn.
- Whistle wrong-location text, exact Brimhaven tiles, first/restored route, and
  realm return match the transcript.
- Feather emits all eight directions, within-five, dungeon, and sacks messages;
  Arthur replaces a lost feather and never creates duplicates.
- Sacks `Prod` works before, during, and after the quest as captured.

### Titan, fisherman, bell, and Fisher King

- Titan stats, max hit, attack speed, immobility, respawn, and one-XP-per-damage
  match the pinned NPC page.
- Every supported combat style may reduce HP; zero without held Excalibur heals
  fully and only state 4 becomes 7.
- A delayed projectile followed by equipping Excalibur succeeds; removing it
  before zero fails.
- Moving beyond 11 tiles, teleporting, death, logout, simultaneous final hits,
  poison, recoil, and other indirect damage cannot bypass the Excalibur rule.
- Only the credited killer receives success/loot semantics; concurrent players
  observe captured passage and respawn behavior without duplicate death.
- Fisherman reveal creates exactly one owner-visible bell at the exact tile;
  another player cannot see/take/use it; repeated dialogue does not duplicate.
- Full inventory preserves the bell for recovery and tells the truth.
- Ring away from the broken wall gives both failure lines and no movement;
  valid tiles show maiden/presentation and move once.
- Ring rejects wrong state, wrong realm, stale/unrevealed bell, and duplicate
  concurrent requests.
- Fisher King topics and state-8 write match captured hidden/current semantics;
  inaccessible/admin cases cannot advance from state 0.

### Percival, restored realm, and grail

- Arthur's feather issue works at 0 and 1 free slots and reports an existing
  copy before complaining about space.
- `Open` rejects wrong state or missing feather; valid open reveals only the
  triggering player's Percival at the captured location.
- Two players opening sacks have independent actors/dialogues; nearby public
  NPCs do not suppress either.
- Without a whistle, re-talk remains recoverable after logout, region leave,
  actor timeout, and restart.
- With a whistle, exactly one is consumed, state 9 writes once, Percival leaves,
  and injected failure resumes rather than consuming another.
- State 9 whistle and BJR routes land in the restored realm; the Titan is not
  required; King Percival and healthy scenery are correct.
- Grail pickup before state 9 gives the worthiness message; after state 9 it
  gives one item; a second attempt gives the greedy message.
- Telekinetic Grab cannot take the grail; full inventory does not remove it;
  destroy then recovery works.
- A King's Ransom grail fixture is not deleted or misclassified by Holy Grail
  duplicate cleanup.

### Completion and consumers

- Arthur refuses completion without state 9 and one legitimate inventory
  grail; banked, ground, duplicate, or provenance-invalid items do not settle.
- Failure after grail consumption, each XP award, QP/count publication, reward
  screen, and final state resumes only missing operations.
- Completion grants exactly 11,000 Prayer XP, 15,300 Defence XP, two QP, one
  completion count, and one state-10 publication.
- State 10 is invisible to consumers until settlement is durable.
- Postquest journal/dialogue, whistle recovery, Fisher Realm, and BJR work.
- Arthur portrait purchase rejects incomplete players and succeeds after a
  coherent completion.
- King's Ransom requirement rejects incomplete/partial settlement and accepts
  coherent state 10.
- Galahad tea completes the Kandarin easy task once; incidental Karamja hooks
  remain functional.
- Nightmare Zone includes the normal/hard Titan only after completion and does
  not require Excalibur inside the dream.
- Admin commands can build a primary-state fixture or a fully settled fixture;
  repeated full completion is a no-op.

## 9. Acceptance evidence

Gate A requires a complete state/ownership table from fresh, migrated,
interrupted, concurrent, duplicate-item, and admin fixtures; current captures
for states 1/5/6 and hidden support data; and a source map for every shared NPC,
item, map, travel, diary, minigame, and later-quest consumer.

Gate B requires static proof that each cache item option has the correct
trigger class, no duplicate trigger exists, all symbolic names resolve, every
queue has the intended player/NPC subject, and all temporary entities have
explicit ownership/cleanup. `tools/questhelper_extract.py holygrail --check`,
`make -C src mock230-scripts`, and `mock230_pack --check-only` must pass.

Gate C requires automated full-route traces plus failure injection at crone
spawn, napkin/tea delivery, whistle generation/pickup/travel, Titan death,
bell reveal/pickup/ring, Fisher King write, feather issue, Percival reveal and
hand-off, grail pickup, and every completion component. Parallel-player tests
must prove there is no cross-player visibility, suppression, theft, credit, or
state mutation.

Gate D requires a real-client smoke from Arthur's offer through reward scroll
and postquest return, packet/screenshots for offer and transitions, pinned Wiki
review, exact XP/QP verification, and live exercise of BJR, portrait, King's
Ransom, diary, and NMZ consumers. Record the commands, captures, Wiki revisions,
remaining cosmetic deviations, and migration outcome in this dossier.

The dossier may move to `verified-modern` only after all four gates have checked
evidence. Setting `%grail = 10`, manually spawning a whistle, invoking the
misregistered bell label, or displaying the journal's completion text is not
completion evidence.

## 10. Prioritized findings

### P0 — canonical reachability and settlement

1. No Draynor Manor magic-whistle spawn or visibility script exists. State-4
   players cannot obtain the first whistle, and BJR is correctly locked until
   state 9, so the quest blocks before the first realm visit.
2. Grail bell `Ring` is an inventory option but the implementation uses
   `[opobj1,grail_bell]` instead of a held-item trigger. After pickup the Ring
   action has no handler, blocking castle entry even if a whistle is injected.
3. The bell is a public temporary ground item with no reveal/owner/state guard;
   the existing wrong-class handler has no location check and would teleport
   unconditionally if invoked.
4. Completion writes state 10 before grail consumption, both XP grants, and
   shared QP/completion settlement, with no replay-safe component record.
5. The static holy grail spawn has no worthiness or one-item guard, allowing
   duplicate pickup and bypass under cross-map/admin access.
6. Titan's queued distance fallback executes generic death before checking
   Excalibur, creating a potential final-condition bypass that must be closed
   or proven unreachable.

### P1 — ownership, recovery, and narrative fidelity

1. Crone, bell, and Sir Percival are public dynamic entities; one player's
   actors/items can be seen, suppressed, reused, or taken by another.
2. Magic gold feather `Blow-on` and sacks `Prod` have no handlers, removing all
   canonical direction and muffled-groan behavior.
3. The whistle accepts a radius much larger than the four-pillar zone and uses
   substitute messages/raw teleports.
4. Merlin enters Holy Grail dialogue at state 0, and Fisher King/crone have no
   relevance gate; shared High Priest branch priority/context are unsafe.
5. Galahad has unchecked napkin/tea adds, inconsistent duplicate-domain checks,
   and missing returns that can produce double dialogue/items or false success.
6. Percival spawns at the player coordinate, is located by public proximity,
   has no durable reveal state, and remains after the state-9 hand-off.
7. No durable milestones cover fisherman reveal, Fisher King topics, feather
   issue, actor ownership, item provenance, or completion settlement.
8. Current postquest whistle recovery is broken for players without BJR because
   the canonical Draynor source is absent.

### P2 — downstream completeness and diagnostics

1. Nightmare Zone is a two-wave stub containing only Count Draynor and Elvarg;
   the completion-gated Black Knight Titan reward is absent.
2. No Galahad tea achievement-diary hook was found despite generic diary
   counters; nearby incidental Karamja task hooks also need shared-area review.
3. Reward presentation omits the Arthur portrait and NMZ Titan unlock.
4. Journal progress derives feather/grail milestones from inventory possession
   and cannot describe bell reveal, actor retry, or partial settlement.
5. State-only quest cheat can masquerade as normal completion to BJR-adjacent,
   portrait, King's Ransom, journal, and future NMZ consumers.
6. Hand-authored realm/Brimhaven coordinates, collision, transitions, drops,
   item cleanup, and unresolved states 1/5/6 lack live-client evidence.
7. No automated state, item-lifecycle, combat, concurrency, migration,
   transaction, completion, or downstream Holy Grail tests were found.

## 11. Evidence still required before implementation

- Exact current-server values and meanings for primary states 1, 5, and 6 and
  every hidden support varbit/record used by the quest.
- Exact current Draynor two-spawn visibility, respawn/despawn, room re-entry,
  drop trick, owner visibility, full-inventory, logout, and postquest rules.
- Exact four-pillar whistle tiles, animations, spot effects, sounds, fade/
  transition packets, destinations, and realm-return presentation.
- Corrupted/restored realm landing coordinates, map-square collision, bridge
  direction, door/ladder states, music, and whether current OSRS uses the same
  two static copies or player transforms.
- Titan shared-versus-private behavior, one-XP-per-damage implementation,
  indirect/delayed damage, final-hit races, passage, drop ownership, and exact
  current drop table.
- Fisherman reveal persistence and bell ground-item ownership/duration,
  one-item behavior, wrong-place radius, and recovery after loss/logout.
- Whether both Fisher King topics are mechanically persisted or only required
  by dialogue/guide convention.
- Crone and Percival actor ownership, exact spawn/despawn presentation, and
  recovery following interruption or reconnect.
- Complete temporary-item cleanup at completion for napkin, whistle, bell,
  feather, and grail, including interactions with later King's Ransom.
- Current reward-interface layout and how the portrait/NMZ unlocks are shown.
- The intended modern engine primitives for player-visible ground items,
  conversation-scoped actors, guarded static pickups, and replay-safe quest
  settlement.
