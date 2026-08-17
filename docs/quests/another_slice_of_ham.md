# Another Slice of H.A.M. modernization audit

Status: `audit-pending` — the native 0–11 state machine, ten quest-owned
files, journal, prerequisite gates, excavation shell, two combat shells,
reward XP, and the Land of the Goblins prerequisite exist. The legitimate
route is nevertheless blocked at state 3 because the Zanik visibility bit is
interpreted backwards, and Oldak is spawned as a non-interactive cutscene NPC.
The railway return doorway is also unhandled. Both combat chapters use shared
world NPCs in static maps rather than player-owned instances, the stealth
chapter replaces the authored command puzzle, and neither permanent unlock is
implemented.

Audited: 2026-08-16

Governing plan: [Quest modernization plan](../QUEST_MODERNIZATION_PLAN.md). This
record applies that plan's Gates A–D to the quest root, Dorgesh-Kaan and Goblin
Village spawns, shared maplinks and ladders, the Ancient mace special attack,
Land of the Goblins, the train system, Oldak's spheres, music, and the
Lumbridge & Draynor Hard Diary. It is an implementation specification, not
completion evidence.

## 1. Authoritative references

These stable revisions define the required route, dialogue, encounter,
replacement, reward, and unlock behavior.

| Reference | Pinned revision | Use in this audit |
| --- | --- | --- |
| [Another Slice of H.A.M.](https://oldschool.runescape.wiki/w/Another_Slice_of_H.A.M.?oldid=15292360) | 15292360, 2026-08-10 | Requirements, route, instancing/death rules, enemies, rewards, unlocks, and downstream requirements |
| [Another Slice of H.A.M./Quick guide](https://oldschool.runescape.wiki/w/Another_Slice_of_H.A.M./Quick_guide?oldid=14458352) | 14458352, 2023-08-27 | Ordered interactions, item preparation, tower tactics, sergeant commands, and final fight |
| [Transcript:Another Slice of H.A.M.](https://oldschool.runescape.wiki/w/Transcript%3AAnother_Slice_of_H.A.M.?oldid=15263379) | 15263379, 2026-07-14 | Start alternatives, choices, re-talks, cutscenes, recovery, guard instructions, Sigmund, and finale |
| [Ancient mace](https://oldschool.runescape.wiki/w/Ancient_mace?oldid=15184174) | 15184174, 2026-04-22 | Wield gate, loss/replacement prices, combat stats, and Favour of the War God behavior |
| [Goblin village sphere](https://oldschool.runescape.wiki/w/Goblin_village_sphere?oldid=15184264) | 15184264, 2026-04-22 | Post-quest construction cost, teleport behavior, consumption, and Wilderness limit |
| [Dorgesh-Kaan–Keldagrim train system](https://oldschool.runescape.wiki/w/Dorgesh-Kaan%E2%80%93Keldagrim_train_system?oldid=15211839) | 15211839, 2026-05-17 | Quest-completion gate, free train travel, stations, and diary relationship |
| [Dorgesh-kaan sphere](https://oldschool.runescape.wiki/w/Dorgesh-kaan_sphere?oldid=15186988) | 15186988, 2026-04-22 | Existing Oldak sphere service that must coexist with the newly unlocked destination |
| [Sigmund](https://oldschool.runescape.wiki/w/Sigmund?oldid=15285834) | 15285834, 2026-08-02 | Level-64 encounter identity and prayer-switching context |

The article identifies the quest as required for Land of the Goblins and its
train as a Lumbridge & Draynor Hard Diary task. The completion rewards are one
quest point, 3,000 Mining XP, 3,000 Prayer XP, the Ancient mace, free train
travel, and the ability to ask Oldak for Goblin Village spheres.

Transition aid only: the local Quest Helper checkout's
[`AnotherSliceOfHam.java`](https://github.com/Zoinkwiz/quest-helper/blob/5ea99d5ea9ba3fb096ebe7b5ed02d80883e9819d/src/main/java/com/questhelper/helpers/quests/anothersliceofham/AnotherSliceOfHam.java)
at commit `5ea99d5ea9ba3fb096ebe7b5ed02d80883e9819d` confirms the 0–10
active checkpoints, zones, follower condition, artefact identities, exact
sergeant route, and unlock list. It guides transition tests but does not
override the Wiki, transcript, or osrs239 cache.

`python3 tools/questhelper_extract.py anothersliceofham --check` resolves all
named items, NPCs, locs, varbits, coordinates, and `quest_anothersliceofham`.

## 2. Native quest identity and player contract

The cache-native `quest_anothersliceofham` row and pinned sources define this
contract:

| Field | Native value / expected behavior |
| --- | --- |
| Cache quest ID | 133; OSRS release-order number 124 |
| Type | Members' quest |
| Difficulty / length | Intermediate / short |
| Series | Dorgeshuun, third quest |
| Release date | 24 April 2007 |
| Start | Talk to Ur-tag **or Ambassador Alvijar** in Dorgesh-Kaan |
| Prerequisites | Death to the Dorgeshuun, The Giant Dwarf, and The Dig Site complete |
| Required levels | 15 Attack and 25 Prayer; both non-boostable |
| Required items | A light source; rope and tinderbox according to the route/entrance state; trowel and specimen brush are supplied |
| Required combat | Level-30 H.A.M. Archer and Mage using Ranged/Magic, then level-64 Sigmund with any combat style and the Ancient mace special |
| Recommended combat | 35 |
| Primary state | `%slice_quest`, cache varbit on `slice_base`, bits 0–10 |
| Native side state | Six two-bit artefact fields plus `slice_zanik_at_dig`, `slice_hiding`, `slice_added_middle_corridor_guard`, `slice_reached_snipers`, and `slice_received_mace` |
| End state | 11 |
| Quest points | 1 |
| XP rewards | 3,000 Mining and 3,000 Prayer |
| Item reward | Ancient mace |
| Unlocks | Free Dorgesh-Kaan–Keldagrim train; Oldak can make Goblin Village spheres |
| Required for | Land of the Goblins; train use for the Lumbridge & Draynor Hard Diary |

The dbrow contains three wrong `requirement_quests` links. Decoding IDs 24,
63, and 29 against local quest rows yields Scorpion Catcher, Shades of Mort'ton,
and Clock Tower. Both authoritative sources and Quest Helper instead agree on
Death to the Dorgeshuun, The Giant Dwarf, and The Dig Site. The current Ur-tag
script correctly enforces the real three prerequisites directly; modernization
must correct the metadata/linkage rather than regress to the unrelated rows.

The dbrow's release date is correct. The constant header's statement that the
quest was released in October 2012 and is beyond a January 2009 source horizon
is false. It was released in April 2007, so its provenance assumptions must be
re-audited and that stale rationale removed.

## 3. Implementation surface

Paths below are relative to `OSRS-Content/osrs239-content/`.

### Quest-owned files

| Path | Present responsibility | Audit result |
| --- | --- | --- |
| `server/scripts/quests/quest_anothersliceofham/configs/quest_anothersliceofham.constant` | State names, requirements, rewards, coordinates, guard constants, and long provenance commentary | Primary values are native, but release/provenance is false and the Zanik transform is documented backwards |
| `server/scripts/quests/quest_anothersliceofham/configs/quest_anothersliceofham.varp` | Three authored permanent encounter flags | Mage, Archer, and Sigmund death are player state layered onto globally shared NPCs; reset and migration policy is absent |
| `server/scripts/quests/quest_anothersliceofham/scripts/slice_urtag.rs2` | Prerequisites, level gate, start offer, and Ur-tag re-talk | Correct base-level gate and prerequisites; missing the dbrow/Wiki-supported Ambassador start path |
| `server/scripts/quests/quest_anothersliceofham/scripts/slice_tegdak.rs2` | Tool handout, six digs, cleaning, hand-in, and mace discovery | Inventory transactions are unsafe, use-on interaction is absent, artefact mapping is simplified, and the Ancient mace is granted at the wrong story point |
| `server/scripts/quests/quest_anothersliceofham/scripts/slice_zanik.rs2` | Dig-site Zanik, follower, Scribe, Oldak, and teleport | Zanik's bit is inverted, Oldak is non-clickable, and follower ownership is global; state 3 cannot proceed legitimately |
| `server/scripts/quests/quest_anothersliceofham/scripts/slice_generals.rs2` | General dialogue, tower transition, Ancient mace removal/reward, and sergeant handoff | Meeting and kidnapping cutscenes are absent; item ownership ignores worn state/capacity; route teleports rather than instances |
| `server/scripts/quests/quest_anothersliceofham/scripts/slice_hammage.rs2` | Tower ladder, ranged/magic restrictions, two enemy deaths, and return | Static-map global NPC shell; no approach hazard, cover, item spawns, instance reset, death policy, or kidnapping scene |
| `server/scripts/quests/quest_anothersliceofham/scripts/slice_sergeants.rs2` | Swamp briefing, rope state, base entry, guards, sight, and stealth | Checks tinderbox instead of light, simplifies the authored command puzzle, and shares all actors between players |
| `server/scripts/quests/quest_anothersliceofham/scripts/slice_sigmund.rs2` | Final room, prayer forms, mace strip, Sigmund death, Zanik, and completion | Static global fight; special always strips prayer without a hit; opening/rescue/train finale is absent |
| `server/scripts/quests/quest_anothersliceofham/scripts/slice_journal.rs2` | Journal for states 0–11 | Broad state coverage exists but omits recoverable artefact/follower/encounter substates and repeats simplified behavior |

These ten files total 1,191 lines including comments and config. This is a
substantial implementation, not an empty scaffold, but its two state-3
blockers and absent state-3 return doorway mean a normal player cannot reach
the later authored chapters.

A repository-root file, `another_slice_of_ham.rs2`, contains only the comment
“Another Slice of Ham quest script”. It has no runtime responsibility and
should be removed or moved into an explicit archival note so it cannot be
mistaken for an engine entry point.

### Mandatory shared and cross-directory surfaces

| Path / subsystem | Relationship | Audit result / required ownership |
| --- | --- | --- |
| `server/scripts/interface_questjournal/scripts/quest_journal.rs2` | Dynamic quest-list dispatcher | Correctly calls `~slice_journal`; retain and expand the journal |
| `server/scripts/quests/scripts/quest_cheat.rs2` | `::complete` adapter | Writes only state 11; useful for registry checks, not reward/unlock evidence |
| `server/scripts/areas/world/configs/m42_83.spawn` | Ur-tag and both Alvijar carriers | Correct carriers exist; live `slice_dwarf_alvijar_there` has Talk-to but no quest trigger |
| `server/scripts/areas/world/configs/m39_86.spawn` | Tegdak and Zanik dig carrier | Correct world spawns expose the cache transform defect |
| `server/scripts/ladders_stairs/configs/maplinks.{loc,dbrow}` | Dorgesh-Kaan railway entrance | `slice_goblin_station_entrance` is correctly categorized and mapped into the railway |
| `slice_underground_wall_exit_goblin` | Railway return doorway | Cache loc has `Enter`; no name trigger, category, or maplink row was found, so leaving the dig is unimplemented |
| Oldak world/service dispatch | State-4 teleport and sphere construction | No ordinary Oldak spawn was found; login adds a cutscene-only type with no operations, while live `dorgesh_oldak_there` has Talk-to and Buy-sphere |
| `server/scripts/quests/quest_landofthegoblins/` | Shared Oldak and downstream prerequisite | Correctly refuses Land of the Goblins before state 11, but its own Oldak trigger will need a shared subject dispatcher |
| `server/scripts/skill_combat/scripts/player/specs/pvm_ancient_goblin_mace.rs2` | Favour of the War God | Quest branch bypasses accuracy/damage; generic branch omits prayer drain, prayer restoration/overboost, and protection bypass |
| Goblin Village and H.A.M. static maps | Both quest combat chapters | Required actors are hand-added globally; no player-owned instance lifecycle exists |
| Train NPCs, station locs, and ladders | Completion unlock and transport | Cache assets exist, but no ticket/travel implementation or completion gate was found |
| Oldak sphere objects | Completion unlock and teleport | `slice_teleport_artifact` exists in cache; no Buy-sphere service or Break handler was found |
| `server/scripts/interface_diaries/` | Lumbridge & Draynor Hard Diary | Generic diary infrastructure exists; no train-use task hook was found |
| Music dbrows and jingle | Quest presentation | H.A.M. and Seek, H.A.M. Attack, Slice of Silent Movie, Slice of Station, and Grand Opening exist in cache; none is used by the quest |

### Cache-native content already available

The cache includes materially more authored content than the scripts use:

- Ur-tag, Ambassador Alvijar, Tegdak, Zanik, Scribe, Oldak, generals,
  sergeants, H.A.M. guards, tower enemies, Sigmund forms, crowds, ticket NPCs,
  and cutscene-specific actors;
- the six artefact carriers and dirty/clean objects, specimen table, Ancient
  mace, Goblin Village sphere, tower cover/ladder pieces, tied Zanik, train
  station scenery, and doors;
- native transform tables for every primary state and side bit;
- dedicated quest animations, spotanims, tracks, and the Grand Opening
  jingle; and
- isolated map regions intended for the tower and final encounters.

Modernization should connect these symbolic assets through normal spawn,
instance, follower, cutscene, transport, item, music, and reward services.
Global `npc_find`/`npc_add` should not remain the multiplayer ownership model.

## 4. Native state model and current reachability

The cache and Quest Helper agree on the primary state sequence:

| State | Required phase | Current implementation / defect |
| ---: | --- | --- |
| 0 | Begin with Ur-tag or Ambassador Alvijar | Ur-tag works; Alvijar's live Talk-to has no binding |
| 1 | Speak to Tegdak at the railway dig | Present; tool grant lacks two-slot capacity handling |
| 2 | Excavate, clean, and hand in all six artefacts | Shell exists; unsafe add-before-state writes can lose an artefact permanently; interactions and item mapping need fidelity work |
| 3 | Zanik automatically follows; leave railway; speak to Scribe | **Blocked:** bit 0 hides the dig carrier, local code requires clicking it at bit 0, and login only restores a follower at bit 1; return doorway is also unhandled |
| 4 | Speak to Oldak with Zanik | **Blocked:** login spawns `lotg_oldak_cutscene`, which has no Talk-to operation, then binds op1 to that unreachable type |
| 5 | Meet the generals; prophecy and H.A.M. attack scene | Replaced by dialogue, item removal, and direct teleport |
| 6 | Reach the tower and defeat Archer/Mage in an instance | Simplified static global fight; no approach encounter or kidnapping cutscene |
| 7 | Report to generals; receive Ancient mace and two warriors | Basic dialogue and state write; ownership/capacity and recovery are incomplete |
| 8 | Meet sergeants at Lumbridge Swamp | Present but requires a tinderbox rather than a valid light source and accepts a banked mace |
| 9 | Command sergeants through guards; defeat Sigmund | Authored puzzle is replaced by a short patrol/crate trigger; final fight is global and special attack is incorrect |
| 10 | Untie Zanik and run station-opening finale | Untie directly queues completion; the entire rescue/train cutscene is absent |
| 11 | Complete | State, XP, and scroll exist; item/unlock/music/diary services do not |

### Deterministic Zanik blocker

Cache carrier `slice_zanik_archaeology` has:

```text
multivarbit=slice_zanik_at_dig
multinpc1=-1
multinpc2=slice_zanik_multi_there
```

For a one-bit varbit, value 0 selects `multinpc1` and therefore hides Zanik;
value 1 shows the talkable form. The constant and script comments claim the
opposite. Tegdak advances to 3 without establishing the follower or writing
the visible value. The op1 recruit branch can consequently never be clicked,
and the login helper restores a follower only for the incorrectly interpreted
value 1. Quest Helper's condition explicitly treats bit 0 or a live follower
as “Zanik following”, and the quick guide says she follows automatically after
the Tegdak conversation.

The fix is not merely flipping one comparison. Define and test the native
meaning of both bit values, make the state-2 commit atomically attach the
player-owned follower, support recovery from the railway when she is lost,
and ensure the carrier/follower/cutscene forms never duplicate one another.

### Deterministic Oldak blocker

`~slice_oldak_login` runs for every player without a quest-state or region
gate and adds `lotg_oldak_cutscene` at Oldak's coordinate. That cache NPC has
no op1. `[opnpc1,lotg_oldak_cutscene]` is therefore unreachable through the
client menu. The actual `dorgesh_oldak_there` type has `op1=Talk-to` and
`op3=Buy-sphere`, but no persistent world spawn was found. Establish one
canonical Oldak carrier and a shared dispatcher for Another Slice, Land of the
Goblins, and sphere services; do not keep a per-login global cutscene actor.

### Railway travel

The initial `slice_goblin_station_entrance` is a valid shared
`maplink_transition` with a maplink row from Dorgesh-Kaan to the dig. It is
not missing. The return loc `slice_underground_wall_exit_goblin`, which Quest
Helper uses at state 3, has `op1=Enter` in cache but no corresponding category,
maplink row, or script binding. Add a reusable reverse maplink record and test
both directions at every relevant state.

## 5. Current versus required playable route

### Stage 1 — start and excavation

Required behavior:

- expose the same quest offer through both Ur-tag and Ambassador Alvijar;
- enforce the three real prerequisite quests and base 15 Attack/25 Prayer;
- preserve accept, refuse, help, and re-talk transcript branches;
- give the trowel and specimen brush only when two slots are available, with
  free recovery from Tegdak;
- let each hotspot produce its correct dirty artefact only after a successful
  inventory transaction;
- clean an artefact by using it on the specimen table with the brush, using
  the correct animation and one-for-one dirty-to-clean exchange;
- allow any lost/destroyed dirty or clean artefact to be recovered without a
  permanent dug-bit dead end; and
- hand in each clean piece idempotently before the final Tegdak scene.

The current start gate's explicit prerequisite and `stat_base` checks are good.
The missing Alvijar trigger is independently proven by the quest dbrow, quick
guide, live cache NPC, and world carrier.

Tegdak calls `inv_add` for the two tools without a capacity check. More
seriously, `~slice_dig` adds an artefact and then marks its two-bit field as
dug without verifying the add. With a full inventory, a hotspot becomes an
empty hole and cannot be used again. Destroy/drop/bank recovery handlers were
not found. Every add/state transition must be an atomic item transaction or a
durable pending claim.

The local code deliberately assumes hotspot N yields artefact N. Quest
Helper's ordered item evidence maps the six physical digs to dirty object
identities `1, 5, 3, 2, 4, 6`, while the cache also contains nontrivial
hotspot model swaps. Verify this mapping against the pinned transcript or a
live capture and encode it as data; do not retain an acknowledged convenience
mapping merely because all pieces eventually hand in.

Clicking the table currently cleans every dirty piece in inventory at once.
No `[oplocu,slice_table_01]` binding exists, despite the authoritative route
requiring artefacts to be used on the table. Implement both item-on-loc packet
directions through the engine's established use-on dispatch and reject the
wrong item without consuming anything.

Tegdak currently creates `ancient_goblin_mace` immediately after the sixth
hand-in. The excavated sixth clean object is the story's mace artefact; the
usable Ancient mace is formally handed over by the generals after the tower
attack. Keep those identities and ownership checkpoints distinct.

### Stage 2 — Zanik, Scribe, Oldak, and Goblin Village

Required behavior:

- attach Zanik automatically when Tegdak finishes the artefact scene;
- use a player-owned follower with logout, teleport, death, dismissal, blocked
  path, region transition, and recovery behavior;
- allow the railway exit, then the Scribe conversation with Zanik present;
- use one canonical clickable Oldak and arbitrate all active quest/service
  subjects;
- run Oldak's sphere animation/scene and teleport both player and follower;
- run the generals' meeting, Zanik's public speech, prophecy, crowd reaction,
  and H.A.M. attack scenes; and
- preserve safe interruption/re-entry checkpoints rather than advancing on
  the opening line.

Current follower setup uses global `npc_find` near the player followed by
`npc_setfollower`. One player can find and take another player's Zanik, and
login cleanup can delete a different player's actor. All follower lookup and
cleanup must be keyed by owner, not type and radius.

The state-5 generals scene instead removes the Ancient mace, advances state,
and teleports to a remote map. It contains recognizable original prose but no
native crowd, meeting, prophecy, attack, or Zanik sequence. It also searches
only inventory and bank: a worn mace can survive removal and later be
duplicated. Item checks must use a unified ownership query and deliberate
equipment policy.

### Stage 3 — Goblin Village tower encounter

The pinned article describes this fight as an instance. Death places the grave
outside it; ordinary ground items left inside are lost. On approach, the
Archer and Mage fire from the tower, buildings/crates provide cover, and a
bronze crossbow plus 50 bronze bolts spawn near the ladder. Both level-30
enemies must be attacked with Ranged or Magic, and resetting the instance
resets the enemies.

The current implementation teleports to map `m38_84`, adds two global NPCs,
and stores death in three authored permanent varps. It has no instance owner,
approach projectiles, cover logic, crossbow/bolt spawn, cutscene, reset,
death/grave contract, or ground-item policy. If another player kills the
shared NPC, their `npc_findhero` and the victim's persistent flags determine
whose state advances. On both flags, the player is immediately teleported to
the generals; the canonical kidnapping of Zanik is omitted.

Replace this with the engine's player-owned instance lifecycle. Instance
state should own actors, temporary item spawns, cover/projectiles, reset, and
cleanup. Persistent quest state should record only the committed story
checkpoint after the encounter and kidnapping cutscene completes.

### Stage 4 — Ancient mace and sergeant briefing

After the tower scene, the generals must give the Ancient mace and direct the
player to the two sergeants. During the quest, a lost mace is recoverable from
the generals for 1,000 coins; after completion it is recoverable from Tegdak
for 1,000 coins. A player cannot wield a traded mace before reaching the
native receipt checkpoint.

Current state 7 adds the mace when inventory and bank do not contain one, with
no capacity check and no worn-item check. Tegdak has no post-quest recovery,
the generals have no priced recovery branch, and `slice_received_mace` does
not enforce the cache/Wiki wield gate. Implement one ownership/recovery
service and make receipt, replacement payment, inventory delivery, and the
flag atomic.

The sergeant gate accepts a mace in the bank, even though the player must use
it in the final room. It then checks for a tinderbox, not a valid light
source. This rejects a player carrying a lit lantern without a tinderbox and
admits a player carrying only a tinderbox into a dark cave. Use the shared
light-source classification and require the Ancient mace to be accessible.

When a rope is present, the entrance sets the native roped bit but never
deletes the rope. Treat the already-roped state as persistent, otherwise
consume exactly one rope only after the attachment succeeds. Confirm shared
ownership of `swamp_caves_roped_entrance` before changing it because the cave
entrance is not quest-exclusive.

### Stage 5 — H.A.M. base stealth and commands

The authored sequence is a command puzzle, not a generic sight cone:

1. enter the caves and climb the nearby ladder;
2. tell one sergeant, “One of you wait here”;
3. enter the room and wait until two guards pass;
4. tell the accompanying sergeant to wait;
5. run to the boxes to trigger the third guard, then return unseen;
6. ask the waiting sergeant to follow;
7. walk to the end so the last guard attacks the sergeant; and
8. use the cleared ladder to reach Sigmund.

The local implementation reduces this to hide behind one crate, wait for any
of three invented patrols to reach a corner, and step on one lure tile. That
single step writes both `slice_added_middle_corridor_guard` and
`slice_reached_snipers`. It never assigns separate wait/follow commands or
models the multi-stage guard movements.

Guard and sergeant actors are again global. Setup tests only whether guard 1
exists before adding all three, so a partially missing set is never repaired.
Cleanup deletes every matching actor within a radius, including actors another
player may own. Persistent bits survive reconnect, while NPC-local phase vars
and shared actors do not, producing impossible mixed states.

Implement the canonical command state machine in an owner instance. Give each
sergeant and guard a stable encounter role, derive resume state from native
bits plus an explicit instance checkpoint, and test logout/death/teleport at
each command boundary. The journal should report the next command rather than
the simplified crate shortcut.

### Stage 6 — Sigmund, Zanik, and station opening

The final encounter is also an instance with grave placement outside and loss
of ordinary ground items left inside. It begins with the hostage/train scene.
Sigmund switches protection prayer to the player's attack style. After prayer
has appeared, a **successful** Ancient mace special disables it, while also
performing the weapon's normal hit and prayer siphon. The player then defeats
him, unties Zanik, and completes the rescue and railway-opening sequence,
including the Grand Opening jingle and free train unlock.

Current `slice_sigmund_protected` changes type based on `%damagetype`, reports
a block, and retaliates, but does not establish a robust combat-state contract.
The quest branch in `pvm_ancient_goblin_mace_sa` calls
`~slice_sigmund_mace_special` before its generic accuracy/damage calculation
and returns. It therefore strips prayer unconditionally, even on what should
be a miss, and deals no special damage or prayer transfer. Outside the quest,
the generic implementation is only a plain crush hit.

The authoritative special costs 100% energy, rolls the selected attack style
against crush defence, hits through Protect from Melee, drains opponent Prayer
equal to damage, and restores/overboosts the user's Prayer by the same amount.
Refactor this as one reusable special-hit transaction. Sigmund's quest hook
should react to its successful result after his prayer phase begins; it should
not replace the weapon mechanic.

The current final room hand-adds one global Sigmund and stores defeat in a
player varp. A player can fight or delete another player's target. Tied Zanik
then queues state 11 and rewards immediately after short dialogue. There is no
opening cutscene, hostage damage beat, rescue travel, crowd, station ceremony,
train movement, dedicated music, or Grand Opening jingle.

### Stage 7 — completion and permanent services

Completion currently writes 11, deletes nearby stealth actors, awards both XP
values, and opens a reward scroll. It does not grant or validate the Ancient
mace at completion because the item was given at state 7. It also uses `coins`
as the completion icon rather than the quest reward.

Make completion idempotent and interruption-safe. The committed result must
include exactly one quest point through the shared completion machinery,
exactly 3,000 Mining/Prayer XP, durable Ancient mace receipt/recovery state,
train access, Goblin Village sphere service, music/jingle unlocks, and the
correct reward presentation. `::complete` may establish registry state but
must not be treated as proof that transactional rewards ran.

Oldak's post-quest Buy-sphere service must accept two law runes plus one molten
glass, including the documented noted-glass case, and create one stackable
Goblin Village sphere atomically. Breaking it consumes one, teleports to a
valid randomized Goblin Village tile, observes the level-20 Wilderness limit,
and coexists with Dorgesh-kaan and later sphere destinations.

Train travel must be free after state 11, support both stations and their
ticket NPC/doors/ladder route, and provide a clear pre-completion refusal. The
Lumbridge & Draynor Hard Diary task belongs at the successful qualifying train
journey commit point, not quest completion or merely clicking station scenery.

Land of the Goblins already checks `%slice_quest >= ^slice_complete`; retain
that dependency and regression-test its shared Oldak conversations after the
dispatcher is rebuilt.

## 6. Narrative, state, and lifecycle oversight matrix

| Area | Current oversight | Required invariant |
| --- | --- | --- |
| Provenance | Claims October 2012 release | April 2007 source history; re-audit available-era machinery |
| Start NPC | Ur-tag only | Ur-tag and Ambassador Alvijar expose one shared offer |
| Dbrow prerequisites | Three unrelated linked rows | Correct Death to Dorgeshuun, Giant Dwarf, Dig Site metadata |
| Tool handout | Two unchecked adds | Capacity-safe, idempotent grant and free replacement |
| Dig transaction | Marks dug after unchecked add | Never mark success unless the correct item was delivered |
| Artefact mapping | Convenience 1:1 map | Verified physical hotspot-to-item table |
| Cleaning | Click table cleans all | Item-on-table, one-for-one, correct brush/animation |
| Lost artefact | Dug hole cannot replace | Recover every dirty/clean/hand-in boundary |
| Mace identity | Usable Ancient mace at Tegdak | Artefact mace distinct from generals' weapon reward |
| Zanik bit | Meaning reversed | Cache-authoritative carrier/follower semantics |
| Zanik transition | Manual click on hidden NPC | Automatic owner-bound follower after Tegdak |
| Railway exit | Loc has no handler | Bidirectional shared maplink |
| Oldak | Global non-op cutscene spawn | One clickable carrier and shared subject dispatcher |
| Follower ownership | Type/radius lookup | Player-owned actor across travel/reconnect |
| General scene | Dialogue plus teleport | Prophecy, crowds, attack, kidnapping cutscenes |
| Tower | Static shared NPCs | Owner instance with cover, projectiles, supplies, reset/death policy |
| Mace removal | Ignores worn state | Unified inv/bank/worn ownership and explicit transfer |
| Mace recovery | Missing | Generals during quest, Tegdak after quest, 1,000 coins |
| Wield gate | Only skill config | Require native receipt checkpoint as well as 15/25 |
| Light gate | Tests tinderbox | Shared valid-light-source classification |
| Banked mace | Accepted for infiltration | Require usable/equipped-or-inventory weapon |
| Rope | Bit set without consumption | Consume one only on first successful attachment |
| Stealth | One crate/lure shortcut | Full sergeant wait/follow command state machine |
| Encounter actors | Global find/add/delete | Instance/owner-scoped lifecycle and cleanup |
| Sigmund prayer | Style type swap only | Defined post-hit prayer switching and protected behavior |
| Mace special | Always strips, no special hit | Successful hit, bypass, drain, restore/overboost, then quest hook |
| Finale | Untie then reward scroll | Complete hostage rescue and train-opening sequence |
| Music | No quest tracks/jingle | Correct contextual tracks and Grand Opening |
| Completion icon | Coins | Ancient mace/current native reward presentation |
| Sphere unlock | Absent | Atomic Oldak construction and Break teleport |
| Train unlock | Absent | Free two-way journey gated by completion |
| Diary | No task hook | Idempotent completion on qualifying train use |
| Journal | Broad plateaus only | Item, follower, command, fight, and recovery-aware guidance |
| Debug completion | State only | Never count as reward, service, instance, or cutscene evidence |

## 7. Modernization implementation plan

### Wave 1 — repair native state and shared world reachability

1. Correct the release/provenance comments and the dbrow prerequisite links.
2. Centralize the start offer and bind both Ur-tag and live Alvijar forms.
3. Define cache-authoritative names for Zanik bit values and attach her
   automatically at the state-2 commit.
4. Replace global follower lookup with the shared owner-bound follower service,
   including recovery and cleanup policy.
5. Add the railway exit as the reverse shared maplink and verify both doors.
6. Establish one persistent Oldak carrier and one dispatcher shared with Land
   of the Goblins and sphere services.
7. Expand journal text for artefact counts, follower loss, and reachable
   recovery actions.

### Wave 2 — rebuild excavation and item lifecycle

1. Make the tool grant capacity-safe and idempotent.
2. Verify and encode the six hotspot-to-object mappings in data.
3. Make dig delivery and bit mutation atomic; add full-inventory failures.
4. Implement item-on-specimen-table cleaning with the native use-on packet,
   animation, one-item conversion, and wrong-item behavior.
5. Add destroy/drop/bank/reclaim policy for every dirty and clean artefact.
6. Make Tegdak hand-ins idempotent and keep the clean mace artefact distinct
   from the Ancient mace reward.

### Wave 3 — implement story cutscenes and the tower instance

1. Build resumable Scribe, Oldak sphere, generals, prophecy, crowd, attack, and
   kidnapping sequences from native actors and assets.
2. Use checkpoints only after protected scene commits; clean up safely on
   logout, death, region change, or modal interruption.
3. Create a player-owned Goblin Village encounter instance with approach
   projectiles, cover, ladder, bronze crossbow/50 bolts, and both enemies.
4. Implement instance reset, exit, ground-item loss, outside grave placement,
   and multiplayer isolation.
5. Return to the generals after the kidnapping scene, then deliver the Ancient
   mace and sergeant mission atomically.
6. Implement 1,000-coin quest-time replacement and the wield checkpoint.

### Wave 4 — rebuild sergeant stealth and the final instance

1. Gate on a real light source and accessible Ancient mace; correct rope
   consumption and already-attached behavior.
2. Create a player-owned base instance with stable roles for two sergeants and
   every guard.
3. Implement wait/follow dialogue commands, timed guard passes, lure/run-back,
   walking noise rule, final engagement, detection, and reset exactly.
4. Persist only authoritative checkpoints and reconstruct safe encounter state
   after reconnect without actor leakage.
5. Create the final Sigmund instance, hostage opening, prayer-switching combat,
   defeat/flee behavior, ground-item/death policy, and Zanik rescue.
6. Implement the train return/opening ceremony, crowd, music, and jingle before
   completion commits.

### Wave 5 — repair Ancient mace, completion, and unlocks

1. Refactor Favour of the War God into a reusable successful-hit transaction:
   100% energy, protection bypass, style-versus-crush roll, damage, Prayer
   drain, and player restore/overboost.
2. Have Sigmund's prayer disable react only to a successful special after his
   protection phase begins.
3. Make completion state, XP, quest point, reward presentation, item receipt,
   music, and unlock flags idempotent and interruption-safe.
4. Implement 1,000-coin post-quest recovery through Tegdak.
5. Implement Oldak's two-law-plus-molten-glass Goblin Village sphere service
   and the sphere's randomized Break teleport/Wilderness restriction.
6. Implement the free Dorgesh-Kaan–Keldagrim train in both directions with
   correct pre-completion dialogue and instance/travel animation policy.
7. Complete the hard diary task only on the qualifying successful train trip.
8. Regression-test Land of the Goblins and every Oldak subject after shared
   dispatcher changes.

Do not add quest-specific C code for followers, instances, use-on dispatch,
cutscenes, light sources, combat results, or travel. If a general capability is
missing, add the smallest reusable engine/service primitive, prove it
independently, and keep Another Slice policy in RuneScript/config data.

## 8. Verification contract

### Static and pack verification

- `python3 tools/questhelper_extract.py anothersliceofham --check`;
- quest audit: no duplicate shared-NPC triggers, global login spawns, unowned
  encounter NPCs, unreachable op-bound NPC type, stale 2012 provenance,
  unchecked inventory add/state pair, or undisclosed route simplification;
- assert every `%slice_quest` write belongs to 0–11 and every side-bit meaning
  matches its cache transform;
- assert Ur-tag, both Alvijar carriers, Tegdak, Zanik, Scribe, Oldak, generals,
  sergeants, train NPCs, and required locs have exactly one intended ownership
  route;
- assert both railway doorway maplinks resolve and round-trip;
- assert journal, dbrow end state, completion registry, and cheat adapter agree;
- `make -C src mock230-scripts`; and
- `mock230_pack --check-only` against the intended cache.

### Automated route matrix

At minimum, test:

1. each prerequisite missing individually, Attack 14/15, Prayer 24/25,
   boosts below threshold, drains above threshold, and both start NPCs;
2. accept/refuse/help/re-talk and simultaneous Ur-tag/Alvijar clicks;
3. zero/one/two free slots at Tegdak, lost tools, duplicates, banked tools, and
   repeated state-1 conversation;
4. all six hotspots with correct object result, missing trowel, full inventory,
   repeated packets, relog, drop/destroy/bank, and every recovery path;
5. all dirty/clean use-on directions, wrong item, missing brush, one-versus-six
   held artefacts, hand-in order, repeated dialogue, and exact bit transitions;
6. automatic Zanik attachment, carrier visibility at both bit values, loss,
   dismissal, teleport, logout, death, blocked following, and recovery;
7. railway entrance/exit in both directions at states 0–11;
8. Scribe and Oldak with/without the correct owned follower, all concurrent
   Land of the Goblins/service subjects, and cutscene interruption;
9. generals scene and tower instance creation, approach cover/projectiles,
   crossbow/bolts, melee rejection, Ranged/Magic kills in either order,
   instance reset, death/grave, ground-item loss, logout, and two simultaneous
   players;
10. kidnapping scene, state-7 mace grant with full inventory, worn/banked/
    traded mace, pre-receipt wield refusal, loss, insufficient/exact 1,000
    coins, and duplicate prevention;
11. every valid and invalid light source, tinderbox-only, banked mace, rope
    missing/present/already attached, and exact one-rope consumption;
12. every sergeant wait/follow command, early run/walk, each guard pass,
    detection/reset, lure timing, final engagement, relog at every checkpoint,
    death/teleport, and multiplayer isolation;
13. each Sigmund protection style, ordinary blocked attacks, special before
    prayer, special miss/hit, energy boundary, Prayer drain/restore/overboost,
    defeat, death/grave, reset, and simultaneous players;
14. tied-Zanik behavior before/after defeat, rescue/finale interruption, music,
    Grand Opening, XP/quest point exactly once, reward icon, and repeated click;
15. generals/Tegdak mace recovery before and after completion with all payment
    and inventory cases;
16. Oldak sphere creation with unnoted/noted glass, law-rune counts, full
    inventory, repeated packets, Break destinations, consumption, blocked
    tiles, and level-20 Wilderness boundary;
17. train refusal before 11, free travel after 11 in both directions, logout/
    interruption, concurrent passengers, and all station interactions; and
18. Land of the Goblins refusal/acceptance around state 11 and the hard diary
    increment only on its qualifying train journey.

### Ancient mace combat tests

Use deterministic rolls rather than flaky sampling:

- 999 versus 1,000 special energy;
- miss and hit boundaries for every selected attack style against crush
  defence;
- Protect from Melee on players and NPCs;
- target Prayer below, equal to, and above damage;
- player Prayer below max, at max, and overboosted, including the resulting
  upper bound;
- immune/special targets under the current authoritative policy;
- Sigmund before prayer, each prayer form, miss, successful strip, and
  subsequent vulnerable attacks; and
- no double hit, double drain, double XP, or energy refund on duplicate
  packets.

### Live-client evidence

Capture a real-client run from either legitimate start NPC through both
post-quest services without state/debug commands. Evidence must include:

- all six excavation visuals, one-item cleaning, hand-ins, loss recovery, and
  correct Zanik carrier/follower transition;
- railway round-trip, follower travel, Scribe, Oldak, generals, crowds, and
  every cutscene interruption/reconnect boundary;
- two isolated concurrent tower instances, approach cover/projectiles,
  supplied Ranged gear, reset, grave, and kidnapping;
- Ancient mace receipt, wield gate, replacement, and its special's hit/Prayer
  behavior;
- the complete sergeant command puzzle with detection/reset and reconnect;
- two isolated final instances, Sigmund prayers/special, rescue, train opening,
  quest tracks, and Grand Opening jingle;
- exact rewards and repeated completion interaction; and
- both train directions, hard diary update, Goblin Village sphere creation and
  Break teleport, plus Land of the Goblins' shared Oldak subject.

Only after static checks, automated matrices, pack validation, and live-client
evidence pass may this record change from `audit-pending` to `modernized`.
