# Defender of Varrock modernization audit

Status: `audit-pending` — the native dbrow, primary state carrier, rich cache
asset set, dynamic journal registration, shared completion call, XP values, and
most headline story checkpoints exist. The playable implementation is not a
completable quest, however. State 28 has no writer after the second balcony,
the chaos golem that supplies the barronite core is never spawned, and state
54 has no writer before Captain Rovin's completion gate. Requirements are not
checked, the Elias follower and quest instances are absent, mist bottles and
both major cutscenes are reduced to messages, the invasion is skipped, quest
items have no recovery contract, and the Varrock Museum reward is missing.

Audited: 2026-08-17

Governing plan: [Quest modernization plan](../QUEST_MODERNIZATION_PLAN.md). This
record applies Gates A–D to requirements, the native state ladder, Elias's
follower lifecycle, trail evidence, the two base sections, armoured-zombie and
chaos-golem combat, mist bottles, Arrav, Camdozaal, the Dream Theatre, invaded
Varrock, library evidence, census candidates, the finale, recovery, completion,
post-quest access, Museum rewards, journal, and debug adapters. It is an
implementation specification, not completion evidence.

## 1. Authoritative references

The article and quick guide define requirements, action order, combat, rewards,
and unlocks. The transcript defines dialogue choices, hand-offs, re-talks, and
scene transitions. Revisions were resolved through the OSRS Wiki API on
2026-08-17.

| Reference | Pinned revision | Use in this audit |
| --- | --- | --- |
| [Defender of Varrock](https://oldschool.runescape.wiki/w/Defender_of_Varrock?oldid=15266905) | 15266905, 2026-07-18 | Identity, requirements, walkthrough, combat, rewards, and unlocks |
| [Defender of Varrock/Quick guide](https://oldschool.runescape.wiki/w/Defender_of_Varrock/Quick_guide?oldid=15079953) | 15079953, 2025-12-06 | Exact critical path, item use, candidate search, and completion |
| [Transcript:Defender of Varrock](https://oldschool.runescape.wiki/w/Transcript%3ADefender_of_Varrock?oldid=15290060) | 15290060, 2026-08-07 | Acceptance, follower, scenes, candidate, recovery, and finale dialogue |
| [Elias White](https://oldschool.runescape.wiki/w/Elias_White?oldid=15128545) | 15128545, 2026-02-16 | Start, follower, base investigation, palace report, and completion |
| [Captain Rovin](https://oldschool.runescape.wiki/w/Captain_Rovin?oldid=15278799) | 15278799, 2026-07-28 | Palace scenes, Shield hand-off/recovery, and completion |
| [Ramarno](https://oldschool.runescape.wiki/w/Ramarno?oldid=15239734) | 15239734, 2026-06-25 | Barronite instructions, sacred forge, and Dream Theatre |
| [Arrav](https://oldschool.runescape.wiki/w/Arrav?oldid=15225392) | 15225392, 2026-06-04 | Base meetings, warning, dream, and finale |
| [Armoured zombie (Zemouregal's Base)](https://oldschool.runescape.wiki/w/Armoured_zombie_%28Zemouregal%27s_Base%29?oldid=15199982) | 15199982, 2026-04-28 | Current level, combat variants, loot, and quest/post-quest behavior |
| [Armoured zombie (Defender of Varrock)](https://oldschool.runescape.wiki/w/Armoured_zombie_%28Defender_of_Varrock%29?oldid=15200622) | 15200622, 2026-04-28 | Invaded-Varrock variants and encounter context |
| [Shield of arrav](https://oldschool.runescape.wiki/w/Shield_of_arrav_%28item%29?oldid=15192251) | 15192251, 2026-04-22 | Temporary hand-offs, destruction, and Rovin recovery |
| [List of elders](https://oldschool.runescape.wiki/w/List_of_elders?oldid=15192252) | 15192252, 2026-04-22 | Library evidence and Read operation |
| [Varrock Census](https://oldschool.runescape.wiki/w/Varrock_Census?oldid=15278762) | 15278762, 2026-07-28 | Census search and Fitzharmon-family cross-reference |
| [Zemouregal](https://oldschool.runescape.wiki/w/Zemouregal?oldid=15196627) | 15196627, 2026-04-25 | Balcony, dream, invasion, and finale scenes |
| [Zemouregal's Base](https://oldschool.runescape.wiki/w/Zemouregal%27s_Base?oldid=15302205) | 15302205, 2026-08-15 | Quest instance, post-quest dungeon, cannon restriction, and access |
| [Zombie axe](https://oldschool.runescape.wiki/w/Zombie_axe?oldid=15249310) | 15249310, 2026-07-03 | Broken zombie axe drop and post-quest reward loop |
| [Ruins of Camdozaal](https://oldschool.runescape.wiki/w/Ruins_of_Camdozaal?oldid=15241730) | 15241730, 2026-06-28 | Existing mining, golem, forge, and entrance systems |
| [Historian Minas](https://oldschool.runescape.wiki/w/Historian_Minas?oldid=15006333) | 15006333, 2025-10-16 | Post-quest Kudos and lamp claim |
| [Antique lamp (Varrock Museum)](https://oldschool.runescape.wiki/w/Antique_lamp_%28Varrock_Museum%29?oldid=15190500) | 15190500, 2026-04-22 | 5,000-XP selectable-skill reward contract |
| [While Guthix Sleeps](https://oldschool.runescape.wiki/w/While_Guthix_Sleeps?oldid=15303774) | 15303774, 2026-08-17 | Downstream quest requirement |
| [The Curse of Arrav](https://oldschool.runescape.wiki/w/The_Curse_of_Arrav?oldid=15271605) | 15271605, 2026-07-22 | Downstream Elias/base continuity |

Transition aid only: the local Quest Helper implementation at commit
[`5ea99d5`](https://github.com/Zoinkwiz/quest-helper/tree/5ea99d5ea9ba3fb096ebe7b5ed02d80883e9819d/src/main/java/com/questhelper/helpers/quests/defenderofvarrock)
maps the native 0–54 route, follower state, both instance zones, items, NPC
forms, locs, and side varbits. `python3 tools/questhelper_extract.py
defenderofvarrock --check` exits 0. It is a routing oracle, not proof that
server triggers, transactions, combat, instances, recovery, or rewards work.

## 2. Canonical contract

Defender of Varrock is the sixth quest in the Mahjarrat series. It is a
members, experienced, medium quest released 21 February 2024. It starts with
Elias White at the Jolly Boar Inn. Starting requires completed Shield of Arrav,
Temple of Ikov, Below Ice Mountain, Family Crest, Garden of Tranquillity, What
Lies Below, Demon Slayer, and Romeo & Juliet, plus base 55 Smithing and 52
Hunter. Neither skill is boostable. Combat level 65 is recommended. A pickaxe
or an existing barronite deposit is useful; all quest-specific items must be
obtainable and recoverable during the quest.

A canonical run must:

1. validate all eight quest and both base-stat requirements before Elias offers
   acceptance, then attach Elias as a player-owned follower;
2. inspect the six ordered cursed-mist clues with Elias present, obtain the
   grubby key at the fourth clue, and use it to open the trapdoor;
3. enter a per-player quest instance of Zemouregal's Base, observe the first
   balcony scene, and leave Elias at the overlook;
4. collect three empty mist bottles, kill at least three armoured zombies, fill
   the bottles from live red-mist sources, use all three on the first gate, and
   speak to Arrav;
5. repeat the bottle loop beyond Arrav, bringing the total to at least six
   armoured-zombie kills, open the second gate, and observe the second balcony
   scene before reporting to Elias at Varrock Palace;
6. speak to Captain Rovin, travel to Ramarno in Camdozaal, obtain a barronite
   deposit, kill chaos golems until obtaining a barronite core, and have
   Ramarno imbue it;
7. use the imbued barronite at the sacred forge, play the Dream Theatre scene
   with Arrav and Zemouregal, return to Rovin, and begin the invasion;
8. traverse the instanced Varrock invasion, consult Reldo, collect and read the
   list of elders, search the census, and cross-reference the two sources;
9. question descendants until one of the five eligible candidates reveals the
   randomized Fitzharmon lead—King Roald is optional and cannot be the answer;
10. temporarily present the Shield of Arrav to Dimintheis, play its reaction
    and the palace/garden finale, and return to Elias and Rovin; and
11. complete exactly once, normalize quest presentation, and expose the
    post-quest base and Museum claim without leaking temporary quest items.

Completion awards 2 quest points, 15,000 Smithing XP, 15,000 Hunter XP, and
access to Zemouregal's Base. Historian Minas additionally awards 5 Kudos and a
5,000-XP antique lamp usable on a skill of at least level 30. The base remains
an active combat/reward space after the quest, including armoured zombies and
their broken-zombie-axe drop path.

## 3. Native identity, requirements, and persistence

| Field | Native value / expected behavior |
| --- | --- |
| Quest metadata ID / packed dbrow index | 188 / 3466 |
| Dbrow | `quest_defenderofvarrock` |
| Wiki quest number | 160 |
| Type / difficulty / length | Members; experienced; medium |
| Series / release | Mahjarrat #6; 21 February 2024 |
| Start | `elias_white_jolly_boar` (NPC 6958), (3283, 3501, plane 0) |
| Quest requirements | Eight packed quest dbrows listed below |
| Stat requirements | Smithing 55 and Hunter 52; start-required and unboostable |
| Combat recommendation | 65 |
| Primary carrier | Native `dov_primary` bits 0–8 as `%dov` |
| End state | 56 |
| Rewards | 2 QP; 150,000 tenths Smithing XP; 150,000 tenths Hunter XP |

The dbrow's `requirement_quests` values are packed dbrow indices, not quest
metadata IDs. Resolving them as dbrow indices yields the correct prerequisites:

| Packed dbrow | Requirement |
| ---: | --- |
| 132 | Shield of Arrav |
| 146 | Temple of Ikov |
| 6 | Below Ice Mountain |
| 48 | Family Crest |
| 58 | Garden of Tranquillity |
| 159 | What Lies Below |
| 25 | Demon Slayer |
| 121 | Romeo & Juliet |

The constant-file header incorrectly interprets these numbers as metadata IDs,
declares the dbrow corrupt, and says Garden of Tranquillity is unported. That
comment is stale and must be removed during implementation. The dbrow itself
is correct and should drive a shared requirement summary and start gate.
Garden of Tranquillity now has a completion route. Temple of Ikov remains a
dependency concern because its local implementation is partial; Defender of
Varrock must not silently waive it. Modernize or verify that prerequisite
separately, then require its legitimate completion state here.

### 3.1 Primary ladder

| `%dov` | Canonical checkpoint | Current result |
| ---: | --- | --- |
| 0 | Not started / Elias offer | Offer is unconditional; no quest or stat checks |
| 2 | Accepted; Elias follows | State is written, but no follower is attached |
| 4, 6, 8, 10, 12 | Ordered trail clues | Six bits exist, but clues are not order-, state-, or follower-gated |
| 14 | Enter quest base instance | Fixed shared-map teleport; no key/follower/instance contract |
| 16 | First balcony scene | One message; no actors, camera, music, or Elias transition |
| 18 | Gather first bottle set | No bottles are granted or managed |
| 20 | First zombie/mist loop | One global zombie death stands in for three kills and three fills |
| 22 | First gate / Arrav | Gate consumes nothing; Arrav is reduced to a gate message |
| 24 | Gather second bottle set | No second bottle loop exists |
| 26 | Second balcony | Handler does not advance from 26 |
| 28 | Report to palace Elias | No code writes 28; deterministic progression blocker |
| 30 | Rovin sends player to Ramarno | Abbreviated shared-NPC dialogue |
| 32 | Obtain core and have it imbued | Chaos-golem spawn proc is never called |
| 34 | Sacred-forge Dream Theatre | Item is deleted and a message claims a new Shield is forged |
| 36 | Dream Theatre complete / invasion transition | Named state is omitted; no scene or write |
| 38 | Rovin during invasion | Rovin grants Shield directly; normal Varrock substitutes for invasion |
| 40 | Reldo directs library search | Abbreviated shared-NPC dialogue |
| 42 | Obtain and read list of elders | Add can fail; no held-item Read handler exists |
| 44 | Search and read census | Ledger writes the state without enforcing list reading or pages |
| 46 | Cross-reference and identify lead | Named state omitted; random reveal logic absent |
| 48 | Candidate phase resolved | Requires all six candidates, including optional King Roald |
| 50 | Present lead / prepare Dimintheis | Named state omitted |
| 52 | Shield reacts; finale | Dimintheis writes 52, leaves Shield, and runs no scene |
| 54 | Return to Rovin/Elias | No code writes 54; deterministic completion blocker |
| 56 | Complete | Rovin only completes from 54, so normal play cannot reach this |

Do not renumber or compress the native ladder. States 36, 46, and 50 have
canonical meanings even though the current constant aliases omit them. Each
transition should commit only after the associated scene, resource transfer,
or encounter succeeds, and should have a reconnect/re-talk continuation.

### 3.2 Side-state carriers

| Field | Native location | Intended contract | Current use |
| --- | --- | --- | --- |
| `%elias_white_vis` | `dov_primary` bits 9–12 | Start/base/palace/follower transforms | Read by cache transforms but never written locally |
| `%dov_hunting_trail_1..6` | bits 13–18 | One ordered flag per clue | Written, but not strictly sequenced |
| `%dov_censuspage` | bits 19–21 | Census/candidate selection progress | Declared but unused |
| `%dov_read_census` | bit 22 | Successful census reading | Written eagerly at the ledger |
| six `%dov_shield_*` bits | bits 23–28 | Candidate conversations | All six are required instead of resolving one eligible lead |
| `%vm_dov` | `vm_displays` | Museum DOV reward claim | Has no script owner |

Preserve these native meanings. Do not invent parallel quest variables merely
to bypass transform, random-selection, or Museum integration work.

## 4. Implementation and ownership surface

The quest root contains six files and 808 lines: one 159-line constant file
and five scripts. It uses symbolic config names and modern shared journal,
choice, and completion mechanisms; it does not open legacy quest interfaces.
Those modern adapters do not compensate for the collapsed gameplay between
them.

| Surface | Current responsibility | Audit result |
| --- | --- | --- |
| `configs/quest_defenderofvarrock.constant` | State aliases, requirements, XP, coordinates, extensive provenance comments | Useful constants, but stale prerequisite analysis and omitted native states |
| `scripts/dov_elias.rs2` | Start, trail, trapdoor/base, Elias, zombies, gates, completion, cheat | Main route; globally spawned actors, missing follower/items/instances, unreachable state 28 |
| `scripts/dov_camdozaal.rs2` | Ramarno, rock, chaos golem, forge | Guaranteed mining result; golem spawn unreachable; Dream Theatre skipped |
| `scripts/dov_invasion.rs2` | Shared candidate NPC branches and Dimintheis handoff | Normal-world substitute; incorrect all-six gate; state 54 absent |
| `scripts/dov_rovin.rs2` | Captain Rovin stages | Gives Shield without invasion and only finishes at unreachable 54 |
| `scripts/dov_journal.rs2` | Dynamic quest journal | Registered, but mirrors compressed and misleading route |
| native dbrow/varbits | Requirements, state, transforms, reward metadata | Strong source of truth; requirement and presentation fields underused |
| shared Varrock NPC scripts | Roald, Horvik, Curator Haig, Sir Prysin, Romeo, Rovin, Reldo, Dimintheis | Cross-quest owners need additive, state-bounded branches and regression tests |
| Below Ice Mountain/Camdozaal | Entrance and ordinary ruins systems | Entrance indirectly gates BIM, but DOV interactions shadow normal mechanics |
| The Curse of Arrav | Later Elias/base and base-door behavior | Downstream co-owner; modernization must preserve its completed-DOV route |
| While Guthix Sleeps | Reads `%dov >= 56` | Downstream requirement; debug writes are not completion evidence |
| Varrock Museum | Historian Minas and display claim state | DOV display bit exists, but reward service is absent |

The cache already contains native Elias start/base/palace/attacked/follower
forms, quest and invasion actors, Armoured Zombie variants, chaos golems,
Ramarno, Arrav, Zemouregal, Sharathteerk, Reldo/Rovin/Aeonisig/Prysin forms,
empty and full mist bottles, grubby key, barronite items, Shield, list, and
broken zombie axe. It also contains the relevant quest locs and the Undead
Army, Dream Theatre, and Zombie Invasion music rows. Prefer those native
assets and transforms after verifying placements and operation slots. Do not
continue hand-spawning global substitutes where a cache-authored lifecycle
exists.

## 5. Start, Elias follower, and hunting trail

Elias offers the quest unconditionally. Implement a dbrow-backed summary and
start check that requires all eight completed quests and base Smithing 55 and
Hunter 52. Boosted levels must not pass. Preserve acceptance, refusal, and
re-talk branches, and do not write state 2 until acceptance completes.

Acceptance must attach `dov_elias_white_follower` to the accepting player,
write the matching native visibility state, hide only that player's start
Elias as appropriate, and recover coherently on logout, reconnect, death,
teleport, region exit, and abandonment of the route. The current login hook
instead ensures direct Elias actors at the inn, base, and palace for every
player. These are global, always-visible substitutes and can leak or duplicate
presentation across players.

The six clue locs should expose their operation only at their exact expected
stage. Each handler must validate the current state, its own prior flag, every
earlier flag, proximity, Elias follower presence, and the actual loc form.
Then it should play the investigation and advance exactly once. Current code
only checks that the quest has started, so an out-of-order or stale loc packet
can set arbitrary clue bits and make the computed counter jump.

The fourth clue must grant the grubby key atomically. If inventory is full,
Elias canonically holds it and supplies it through dialogue; destruction has
the same recovery route. The current implementation never grants or consumes
the key. The trapdoor must require the completed trail, key, and Elias
follower during the quest, offer the ready/not-ready dialogue branches, and
consume or transition ownership only at successful entry.

## 6. Zemouregal's Base, mist, and Arrav

Before completion, the base is a player-scoped quest instance. After
completion, it is a shared repeatable dungeon. The current fixed-coordinate
teleport conflates those two modes. Build a modern instance with owner-scoped
actors, loc state, kill counters, reconnect/re-entry policy, and explicit
surface exit. The only current exit binding is generic `climb_up`, which moves
one plane at the same dungeon coordinate and does not prove a return to the
surface.

The first balcony is a resumable scene involving Elias, Zemouregal,
Sharathteerk, and Arrav. It must own camera, movement, dialogue, music, and
cleanup and then leave Elias in his intended base position. A one-line message
is not an acceptable transition because follower/presentation state changes
there.

The two base sections each need an explicit inventory and encounter loop:

| Step | Canonical behavior | Current behavior |
| --- | --- | --- |
| Empty bottles | Pick up three `dov_mist_bottle_empty` | No bottle acquisition |
| Zombie kills | Kill at least three per section | One global melee zombie per gate |
| Mist capture | Use each empty bottle on live red mist | Red mist only prints a message |
| Full bottles | Produce three `dov_mist_bottle_full` | Zombie death claims all three filled |
| Gate | Consume all three full bottles and open/transform | No items consumed and no gate transform |
| Arrav | Full conversation between the two sections | Reduced to a gate message |
| Second balcony | Scene and instance exit/report transition | One message and no write to state 28 |

Use owner-scoped encounter controllers. The current shared `npc_find` pattern
means one player's zombie can block or replace another player's fight, while
only the killer receives progress. Validate melee and ranged variants, target
ownership, retreat, death, logout, simultaneous players, and respawn behavior.
Inventory adds/removals must be atomic, with deliberate ground or owner
fallbacks when capacity changes during an action.

The current cache armoured zombies have combat level 82 following a 2025 stat
change. The main quest article still describes level 85, while the dedicated
monster page and local cache agree on 82. Treat 82 as the present implementation
target and preserve this discrepancy in test notes rather than silently
changing combat to the older article value.

The post-quest dungeon needs its larger persistent population, normal loot,
and 1/800 broken-zombie-axe drop route during and after the quest where
canonical. It must prohibit cannon placement. The broken axe exists in cache,
but no applicable drop owner was found in this audit.

## 7. Camdozaal, barronite, and the Dream Theatre

Rovin and Elias should move the player through the palace report scene before
Rovin sends them to Ramarno. Current dialogue compresses that scene and leaves
Elias/Rovin presentation global. Use native palace forms and player state;
avoid duplicating shared actors in the ordinary world.

The quest must coexist with the Ruins of Camdozaal rather than overriding its
systems. The current DOV handler on `camdozaalrock1` gives a barronite deposit
on every click without a pickaxe, mining animation, success roll, inventory
capacity, or normal-rock depletion. Integrate the quest requirement with the
existing mining controller, or bind only a verified quest-specific loc/form.
Preserve ordinary mining for non-quest players and concurrent users.

Canonical chaos golems are level 70 and have a 2/15 core chance. The current
code defines `~dov_spawn_chaos_golem`, but no caller invokes it and no active
`camdozaal_golem_chaos` world spawn exists; only inactive rubble forms are
placed. Therefore the barronite core cannot be obtained. Implement the native
rubble awakening/spawn loop, combat ownership, drop chance, and retry behavior.
Never advance if the core add fails; provide owner-scoped ground fallback or
recovery.

Ramarno should consume the core only after successfully producing the imbued
barronite. Using that item on the sacred forge does not forge a new Shield of
Arrav. It opens the Dream Theatre: Arrav's memory plays, then Zemouregal
interrupts and the invasion begins. The current handler deletes the item,
prints the incorrect Shield message, and writes 34. Implement the scene through
state 36, with Dream Theatre music, actor/camera cleanup, disconnect recovery,
and a committed transition into the invaded-Varrock instance.

## 8. Invaded Varrock, library evidence, and Shield recovery

The invasion is not decorative flavor. It is a duplicate/instanced Varrock
under siege, with quest-specific NPC and loc forms, movement through the
palace, and Zombie Invasion/Undead Army presentation. The current
implementation skips it and continues through ordinary Varrock actors. Build
the instance and prove entry, exit restrictions, reconnect, death, teleport,
and cleanup. Shared world players must never see another player's invasion
actors or altered locs.

Rovin currently grants the Shield directly at state 34 and writes 38. The add
is not capacity-checked; a full inventory can advance the state without the
Shield and permanently block the candidate phase. Grant it only after the
Dream Theatre/invasion transition and only in an atomic transaction. Captain
Rovin must replace a lost or destroyed Shield at every applicable pre-finale
state, without duplicating it across inventory, equipment, bank, temporary
ownership, or ground.

Reldo's library chain must preserve the evidence sequence. Fallen scrolls
currently attempt to add the list of elders and write state 42 even if the add
fails. There is no `[opheld1,dov_name_list]` Read owner. Add and state change
must be atomic; implement Read, Destroy, and Reldo recovery. The census search
must require that the list was read, update `%dov_censuspage` as intended,
write `%dov_read_census` only after the successful relevant entry, and expose
partial journal guidance. The current ledger accepts any state at or after 42
and writes state 44 immediately.

## 9. Candidate search, Dimintheis, and finale

The canonical candidate search does not require all six people. The player
cross-references the list and census and talks among five eligible descendants
until a randomized one reveals the adopted-lineage/Fitzharmon lead. King Roald
is optional and can never be the correct lead. Current code requires the bits
for Roald, Aeonisig, Sir Prysin, Horvik, Romeo, and Curator Haig; every branch
says no, and only the all-six aggregate writes 48. `%dov_censuspage` is never
used.

Implement a stable per-player selection in the native carrier, validate the
eligible set against the canonical revision, reveal the clue only through the
selected conversation, and preserve optional dialogue for Roald and already
asked candidates. Shared NPC scripts should add a tightly state-bounded DOV
branch without changing their Family Crest, Romeo & Juliet, Dragon Slayer II,
or ordinary dialogue behavior.

The Shield is temporarily passed to candidates in the story; dialogue should
model ownership without losing or duplicating it. Current candidate branches
merely require it in inventory. Dimintheis must receive/equip it, trigger its
reaction, and start the finale. The finale has Dimintheis destroy the invading
zombies through the palace/garden while Zemouregal and Sharathteerk retreat
with Arrav. Current Dimintheis prints a message, leaves the Shield in inventory,
and writes 52.

After the scene, commit state 54 and normalize invasion/actor/item state before
the return dialogue becomes available. No current code writes 54, while Rovin
only calls completion at exactly that state. This is independent of the earlier
state-28 and chaos-golem blockers and must have its own regression test.

## 10. Completion, rewards, and unlocks

The shared completion adapter correctly derives 2 quest points from the dbrow
and the quest script awards 15,000 Smithing and 15,000 Hunter XP. It currently
awards XP and invokes the shared reward flow before writing `%dov = 56`.
Because completion can yield through modal UI or queue execution, a duplicate
resume can replay rewards. Use the repository's modern exactly-once completion
contract: validate state 54, commit/guard completion atomically before the
first yield, award each reward once, and make every subsequent Rovin/Elias talk
post-quest dialogue only.

| Reward or unlock | Current status | Required proof |
| --- | --- | --- |
| 2 quest points | Advertised through shared dbrow reward | Exactly-once completed-count/QP test |
| 15,000 Smithing XP | Awarded by completion proc | Exact delta once; no replay |
| 15,000 Hunter XP | Awarded by completion proc | Exact delta once; no replay |
| Zemouregal's Base access | Text only / premature trapdoor access | Quest-instance before 56; shared dungeon after 56; correct exit |
| Shield cleanup | Not performed | No leaked temporary Shield after finale |
| 5 Kudos | Missing | Historian Minas claim once through `%vm_dov` |
| 5,000-XP Museum lamp | Missing | One lamp, skill >=30 selection, exact XP, recovery policy |
| Broken zombie axe loop | Missing | Correct eligible NPCs, rate, ownership, and post-quest persistence |

Historian Minas has no DOV dialogue/service owner in the searched server
surface, and `%vm_dov` is unused. Implement this in the shared Museum reward
system, not as an extra completion grant: the canonical reward is separately
claimed. Test claim before completion, first claim, full inventory, lamp use on
an ineligible skill, valid selection, destroy/loss policy, repeated talk, and
display state.

## 11. Item and transaction ledger

| Resource | Producer | Consumer / recovery contract |
| --- | --- | --- |
| Grubby key | Fourth clue or Elias fallback | Opens quest trapdoor; Elias recovers when lost at relevant states |
| Empty mist bottles | Base pickup | Filled individually at live red mist; repeat pickup while missing |
| Full mist bottles | Empty bottle + active mist | Three consumed atomically at each gate |
| Barronite deposit | Normal Camdozaal mining | Ramarno/core flow as canonically required; do not shadow general mining |
| Barronite core | Chaos-golem drop | Ramarno imbues; owner-ground fallback and repeat kills |
| Imbued barronite | Ramarno | Sacred forge consumes only when Dream Theatre begins successfully |
| Shield of Arrav | Rovin after invasion begins | Candidate/Dimintheis temporary hand-offs; Rovin replacement; finale cleanup |
| List of elders | Fallen scrolls | Read/Destroy operations; Reldo recovery until evidence phase ends |
| Broken zombie axe | Eligible armoured-zombie drop | Persistent loot; normal ownership and pickup behavior |
| Museum lamp | Historian Minas claim | One valid >=30 skill choice; no duplicate claim/use |

For every transfer, test zero slots, one slot, stack merge, already-owned copies,
banked/equipped copies where relevant, simultaneous interaction, interruption,
logout between deletion and addition, death, and repeated packets. A state may
never advance merely because `inv_add` or `inv_del` was attempted.

## 12. Journal, debug, and recovery

The dynamic journal dispatcher is the correct modern surface, but its content
does not describe the actual native contract. The not-started page omits all
eight prerequisites and both skill requirements. Trail entries do not expose
the ordered clue/key/follower status. Base entries collapse bottles, kills,
mist, gates, and Arrav. The forge entry incorrectly says a new Shield is
forged. States 36, 46, and 50, the invasion instance, random lead, Shield
recovery, and finale are absent. At state 52 it tells the player to speak to
Rovin even though Rovin only completes at 54, making the blocker visible in the
UI.

Rewrite the journal from the same authoritative state predicates used by the
handlers. Include actionable recovery hints for Elias/key, bottles, core,
Shield, list, and instance re-entry. Completed text should enumerate the
Museum claim and base unlock accurately without claiming an unimplemented
feature.

The generic cheat writes only `%dov = 56`. That may be useful for narrow
dependency testing but is not a quest normalization adapter. Define separately:

- a production-safe completion normalization path for transforms, temporary
  items, follower, instance, invasion, post-quest base access, and Museum
  eligibility without claiming the separate Museum reward; and
- an exhaustive development reset that clears every DOV carrier, follower,
  instance, temporary item, actor, and Museum bit intentionally.

Do not use the While Guthix Sleeps debug path that force-writes `%dov = 56` as
evidence that Defender of Varrock can complete or that its rewards/unlocks are
coherent.

## 13. Modernization work order

1. Correct the stale constant provenance comment, expose missing states 36,
   46, and 50, and capture a real-client/cache trace for every native operation,
   transform, placement, coordinate, music row, and instance boundary.
2. Implement dbrow-backed prerequisite/stat checks and Elias's owner-scoped
   follower/visibility/recovery lifecycle.
3. Rebuild the ordered clue/key/trapdoor route and separate the quest instance
   from the post-quest shared-base entrance and exit.
4. Implement both bottle/mist/gate loops, bounded armoured-zombie encounters,
   Arrav, both balcony scenes, and the missing transition to state 28.
5. Integrate Camdozaal mining safely, activate chaos golems and core drops,
   implement Ramarno transactions, and build the Dream Theatre through state
   36.
6. Build the invaded-Varrock instance, atomic Shield grant/recovery, Reldo list
   operations, census pages, and stable randomized candidate lead through
   states 46, 48, and 50.
7. Implement Dimintheis's hand-off and finale, write state 54 after successful
   cleanup, and make completion exactly once.
8. Implement post-quest base combat/loot/cannon rules and the Historian Minas
   Kudos/lamp claim, then rewrite journal and normalization/debug adapters.
9. Run the verification matrix below and update the master inventory status
   only after Gates A–D have fresh evidence.

## 14. Verification matrix

| Area | Required tests |
| --- | --- |
| Requirements | Each missing prerequisite; each stat at 54/55 and 51/52; boosted-only rejection; all-valid acceptance |
| Start/follower | Yes/no/re-talk; two simultaneous players; logout, death, teleport, region exit, reconnect; no global Elias leakage |
| Trail/key | All six in order; out-of-order/stale packets; repeated click; full inventory at clue four; loss/recovery; trapdoor without key/follower |
| Base instance | Two simultaneous owners; first/second entry; reconnect; explicit exit; post-completion shared mode; Curse of Arrav coexistence |
| Bottles/combat | Exactly three bottles each loop; empty/full conversions; six required kills; melee/ranged forms; death/retreat/logout; no cross-player credit |
| Scenes | Both balconies, Dream Theatre, invasion entry, Dimintheis finale; skip/reconnect/cancel; actor/camera/music cleanup |
| Camdozaal | Pickaxe/deposit behavior; normal mining coexistence; chaos golem retries and 2/15 core; capacity/ground fallback; forge interruption |
| Evidence | List add/read/destroy/recovery; census gating/pages; randomized eligible lead; Roald optional; all candidate re-talks |
| Shield | Full inventory at grant; lost/destroyed/banked/equipped detection; candidate hand-offs; Dimintheis consumption; post-finale cleanup |
| Completion | State 52 cannot finish; successful scene writes 54; duplicate talk/queue/reconnect awards QP/XP once; state 56 normalized |
| Museum | Pre-completion refusal; 5 Kudos once; lamp full-inventory/recovery; skill <30 refusal; valid 5,000 XP once; `%vm_dov` persistence |
| Post-quest base | Entrance/exit, population, cannon rejection, normal loot, broken axe rate/ownership, multiplayer persistence |
| Journal/debug | Every primary state and partial side-state combination; actionable recovery text; complete normalization; exhaustive reset |
| Downstream | While Guthix Sleeps requirement; The Curse of Arrav Elias/base/door flows; shared Rovin/Reldo/candidate NPC regressions |

Run repository compile/lint and relevant unit/integration suites after each
slice. Gate D additionally requires a fresh-client end-to-end run from state 0
to 56, a reconnect at every scene/instance/resource boundary, a two-player
isolation run, exact reward deltas, and verified post-quest Museum/base access.

## 15. Gate verdict

| Gate | Verdict | Evidence needed to pass |
| --- | --- | --- |
| A — cache and canonical discovery | **Partial** | Wiki/QH/native schema are pinned; still capture real-client operation/transform/placement/instance traces |
| B — state ownership and invariants | **Fail** | Prerequisite gates, follower/visibility ownership, missing 28/36/46/50/54 transitions, transaction order, instance ownership, and recovery must be implemented |
| C — implementation | **Fail** | Three deterministic blockers plus missing bottle loops, scenes, invasion, random candidate logic, finale, Museum reward, and post-quest dungeon remain |
| D — verification | **Fail** | No legitimate start-to-completion route exists; full matrix and fresh-client evidence are required |

Defender of Varrock must remain `audit-pending`. Existing dbrow metadata,
native carriers, cache assets, journal dispatch, and reward adapters are useful
modern foundations, but no quest may be described as implemented or modernized
while its normal route cannot reach states 28, 32, 54, or completion.
