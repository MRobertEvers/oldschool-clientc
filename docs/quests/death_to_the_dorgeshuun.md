# Death to the Dorgeshuun modernization audit

Status: `audit-pending` — the cache-native quest row, primary state, side
varbits, start carrier, journal, shared completion call, reward XP, H.A.M.
store-room minigame, Dorgesh-Kaan travel carriers, and basic combat hooks are
present. The playable route is not faithful or multiplayer-safe: the start
incorrectly requires both boostable skills, login mutates global quest actors,
the Lumbridge tour and H.A.M. stealth section are heavily reduced, the jail,
corpse item, cave traversal, crate item, mill instance, Sigmund prayer fight,
and finale are absent, and the permanent watermill passage does not move the
player. This is not `verified-modern`.

Audited: 2026-08-17

Governing plan: [Quest modernization plan](../QUEST_MODERNIZATION_PLAN.md).
This record applies that plan's Gates A–D to the quest root and every shared
start, follower, H.A.M., Tears of Guthix, combat, travel, reward, journal, and
debug surface. It is an implementation specification, not completion evidence.

## 1. Authoritative references

These stable revisions define the required route, dialogue, recovery,
encounter, reward, and post-quest behavior.

| Reference | Pinned revision | Use in this audit |
| --- | --- | --- |
| [Death to the Dorgeshuun](https://oldschool.runescape.wiki/w/Death_to_the_Dorgeshuun?oldid=15292353) | 15292353, 2026-08-10 | Identity, requirements, complete route, combat, rewards, unlocks, item-loss notes, and instance/death behavior |
| [Death to the Dorgeshuun/Quick guide](https://oldschool.runescape.wiki/w/Death_to_the_Dorgeshuun/Quick_guide?oldid=14740904) | 14740904, 2024-09-06 | Ordered actions, two H.A.M. sets, tour contacts, stealth solution, cave routes, crate conditions, and mill sequence |
| [Transcript:Death to the Dorgeshuun](https://oldschool.runescape.wiki/w/Transcript%3ADeath_to_the_Dorgeshuun?oldid=15295844) | 15295844, 2026-08-13 | Offers/refusals, re-talks, disguise dialogue, tour branches, jail/Jimmy, cutscenes, recovery, full inventory, and finale |
| [The Lost Tribe](https://oldschool.runescape.wiki/w/The_Lost_Tribe?oldid=15292326) | 15292326, 2026-08-10 | Direct prerequisite and the pre-existing cellar/goblin-mine route |
| [H.A.M. robes](https://oldschool.runescape.wiki/w/H.A.M._robes?oldid=15167651) | 15167651, 2026-04-07 | Seven-piece set identity and disguise acquisition |
| [H.A.M. Hideout](https://oldschool.runescape.wiki/w/H.A.M._Hideout?oldid=15302200) | 15302200, 2026-08-15 | Surface hideout, trapdoor, jail, and shared membership mechanics |
| [H.A.M. Store room](https://oldschool.runescape.wiki/w/H.A.M._Store_room?oldid=14427930) | 14427930, 2023-06-20 | Permanent store-room unlock, guards, keys, doors, and chests |
| [Zanik](https://oldschool.runescape.wiki/w/Zanik?oldid=15276928) | 15276928, 2026-07-27 | Follower forms and story/quest context |
| [Zanik (item)](https://oldschool.runescape.wiki/w/Zanik_(item)?oldid=15185878) | 15185878, 2026-04-22 | Carried unconscious-Zanik quest item and loss recovery |
| [Crate with Zanik](https://oldschool.runescape.wiki/w/Crate_with_Zanik?oldid=5509390) | 5509390, 2018-01-05 | Wielded crate carrier used to enter the mill |
| [Juna](https://oldschool.runescape.wiki/w/Juna?oldid=15298969) | 15298969, 2026-08-14 | Revival and Tears of Guthix dialogue carrier |
| [Tears of Guthix](https://oldschool.runescape.wiki/w/Tears_of_Guthix?oldid=15292296) | 15292296, 2026-08-10 | Shared cave, Juna, moving tear-stream, and Games necklace context |
| [Johanhus Ulsbrecht](https://oldschool.runescape.wiki/w/Johanhus_Ulsbrecht?oldid=15297476) | 15297476, 2026-08-13 | H.A.M. infiltration dialogue |
| [Jimmy the Chisel](https://oldschool.runescape.wiki/w/Jimmy_the_Chisel?oldid=15082549) | 15082549, 2025-12-09 | Required jail escape dialogue |
| [Sigmund](https://oldschool.runescape.wiki/w/Sigmund?oldid=15285834) | 15285834, 2026-08-02 | Level-50 boss, protection-prayer behavior, Zanik's ranged intervention, and escape |
| [Water mill cellar](https://oldschool.runescape.wiki/w/Water_mill_cellar?oldid=15212077) | 15212077, 2026-05-17 | Mill instance entrance, southern passage, and permanent route |
| [Dorgesh-Kaan](https://oldschool.runescape.wiki/w/Dorgesh-Kaan?oldid=15263913) | 15263913, 2026-07-15 | Permanent city access reward |
| [Bone dagger](https://oldschool.runescape.wiki/w/Bone_dagger?oldid=15214812) | 15214812, 2026-05-21 | Backstab special-attack unlock |
| [Dorgeshuun crossbow](https://oldschool.runescape.wiki/w/Dorgeshuun_crossbow?oldid=15210109) | 15210109, 2026-05-13 | Snipe special-attack unlock |
| [Another Slice of H.A.M.](https://oldschool.runescape.wiki/w/Another_Slice_of_H.A.M.?oldid=15292360) | 15292360, 2026-08-10 | Downstream prerequisite and Dorgeshuun-series continuity |

No separate Wiki journal subpage exists. The transcript and the live journal
requirements in the article therefore define journal fidelity.

Transition aid only: the local Quest Helper checkout's
[`DeathToTheDorgeshuun.java`](https://github.com/Zoinkwiz/quest-helper/blob/5ea99d5ea9ba3fb096ebe7b5ed02d80883e9819d/src/main/java/com/questhelper/helpers/quests/deathtothedorgeshuun/DeathToTheDorgeshuun.java)
at commit `5ea99d5ea9ba3fb096ebe7b5ed02d80883e9819d` confirms the primary
0–12 active checkpoints, native tour/guard/corpse conditions, involved zones,
items, NPCs, and locs. It is a transition/test aid and does not override the
Wiki, transcript, or osrs239 cache.

`python3 tools/questhelper_extract.py deathtothedorgeshuun --check` succeeds for
the current checkout.

## 2. Native quest identity and contract

The cache-native `quest_deathtothedorgeshuun` row and pinned references define
this player contract:

| Field | Native value / required behavior |
| --- | --- |
| Metadata quest ID | 113; packed dbrow index 24. Neither is the Wiki's release-order number |
| Type | Members' quest |
| Difficulty / length | Intermediate / medium |
| Series | Dorgeshuun, second quest |
| Release date | **21 June 2006**; the local dbrow incorrectly says 31 May 2006 |
| Start | Talk to Mistag in the Dorgeshuun Mines after The Lost Tribe |
| Prerequisite | The Lost Tribe, including its own prerequisites |
| Required levels | 23 Agility and 23 Thieving, both boostable and **not required to begin** |
| Required items | Any valid light source and two complete seven-piece H.A.M. sets; a pickaxe only for the mining route to Juna; a tinderbox only when using an open flame |
| Required combat | Defeat level-50 Sigmund with Melee or Magic after Zanik forces Protect from Missiles |
| Recommended combat | 30; combat equipment, food, Games necklace, and teleports are recommendations |
| Primary state | `%dttd_main`, native varbit on `dttd_base`, bits 0–10 |
| End state | 13 |
| Quest points | 1 |
| XP rewards | 2,000 Thieving and 2,000 Ranged |
| Unlocks | Dorgesh-Kaan, H.A.M. store room, watermill tunnel, Nardok's shop access through the city, and Bone dagger/Dorgeshuun crossbow special attacks |
| Required for | Another Slice of H.A.M. and later Dorgeshuun-series content |

The dbrow correctly stores `requirement_check_skills_on_start=0` and
`requirements_boostable=1`. The current Mistag script contradicts both by
checking current Agility and Thieving before it presents the offer. Starting
without the levels and boosting when the relevant obstacle is reached must be
supported.

The dbrow's `requirement_quests` value is packed row 87, which resolves to
`quest_losttribe`. The constant header mistakes metadata quest ID 87 for packed
dbrow 87 and consequently calls this Mourning's End Part I. That comment is
false; the data itself is correct and should remain native.

## 3. Implementation surface

Paths below are relative to `OSRS-Content/osrs239-content/`.

### Quest-owned files

| Path | Current responsibility | Audit result |
| --- | --- | --- |
| `server/scripts/quests/quest_deathtothedorgeshuun/configs/deathtothedorgeshuun.constant` | State names, requirements, rewards, counts, and coordinates | Mostly symbolic/native, but contains the false prerequisite comment, invalid value 2 for a one-bit Zanik carrier, and hard-coded encounter coordinates |
| `server/scripts/quests/quest_deathtothedorgeshuun/configs/dttd_extra.varp` | Authored persistent count for twenty quest tears | Necessary only because the native bit tracks collection mode; collection validation and migration/reset rules are missing |
| `server/scripts/quests/quest_deathtothedorgeshuun/scripts/dttd_start.rs2` | Mistag offer, Zanik recruitment/login, six-bit tour, follower transition | Wrong start gate, global followers, wrong two-set/pet contract, ordered/reduced tour, missing recovery/cutscenes, and one-bit truncation |
| `server/scripts/quests/quest_deathtothedorgeshuun/scripts/dttd_haminfiltrate.rs2` | Johanhus, hidden trapdoor, five guards, meeting door, actor login | Replaces disguise/dialogue/stealth/jail mechanics with sequential messages and globally deletes/spawns actors on every login |
| `server/scripts/quests/quest_deathtothedorgeshuun/scripts/dttd_savezanik.rs2` | Body, mine route, Juna, twenty-tear counter, revival/story | Gives no corpse item, performs no cave movement, treats tinderbox as light, accepts any wall click, and omits the authored revival sequence |
| `server/scripts/quests/quest_deathtothedorgeshuun/scripts/dttd_mill.rs2` | Crate, mill entry, three guards, Sigmund, drill, completion | Gives no crate item, uses a shared static room and global combat NPCs, omits boss prayer/escape and finale, and has message-only exits |
| `server/scripts/quests/quest_deathtothedorgeshuun/scripts/dttd_shared.rs2` | Requirements, robe helpers, completion | Skill gate is used at the wrong time; robe removal is not a reusable transaction; completion lacks idempotence and calls a global actor mutator |
| `server/scripts/quests/quest_deathtothedorgeshuun/scripts/dttd_journal.rs2` | Dynamic journal for 0–13 | Registered correctly but merges materially different stages and ignores side-state/recovery details |
| `server/scripts/quests/quest_deathtothedorgeshuun/scripts/dttd_debug.rs2` | Reset, start setup, and direct headless walk | Reset leaks quest items/actors/unlocks; headless path writes through every critical mechanic and is not verification |

These nine files total 926 lines including comments and config. The route has
real triggers and a shared completion call, so this is not an empty scaffold.
Its density should not be confused with fidelity: many canonical actions are
represented only by a state write and a message.

### Mandatory shared and cross-directory surfaces

| Path / subsystem | Relationship | Audit result / required ownership |
| --- | --- | --- |
| `server/scripts/quests/quest_losttribe/scripts/losttribe_finish.rs2` | Mistag start and post-quest Cellar/Watermill operations | Correctly splices the DTTD offer and exposes op4 at Lost Tribe state 13; travel must be tested together with the open tunnel endpoints |
| `server/scripts/areas/world/configs/m50_150.spawn` | Native `dttd_zanik_cellar` carrier | The carrier already exists; `~dttd_zanik_login` redundantly tries to add it and must not own a global world actor |
| `server/scripts/player/login.rs2` | Calls `~dttd_zanik_login` and `~dttd_ham_guards_login` for every player | Critical multiplayer defect: one player's state changes global actors seen by all players |
| Lumbridge Duke/citizen/priest/shop scripts and tour walktrigger | Six implemented tour flags | Cook, Hans, Bob, Lumbridge Guide, prayer tutor, optional contacts, and several authored reactions/cutscenes are missing |
| Generic H.A.M. surface trapdoor and `climb_shared.rs2` | Hideout and stage access | Surface entry is inherited from `%ham_thief`; DTTD does not enforce the full disguise or authored questioning route |
| `server/scripts/areas/world/configs/m40_81.spawn` | Five post-quest store-room guards | Placed actors already exist, but login code deletes/re-adds them according to one player |
| `server/scripts/minigames/minigame_ham_storerooms/` | Post-quest guard patrols, pickpocket, keys, doors, and chests | Substantial and state-gated implementation exists; it needs stable world ownership and integration tests |
| `server/scripts/quests/quest_tearsofguthix/scripts/tearsofguthix.rs2` | Shared Juna trigger | Correctly dispatches DTTD states 6 and 7, but the DTTD branch does not require the corpse item |
| `server/scripts/quests/quest_tearsofguthix/scripts/tearsofguthix_lantern.rs2` | Shared weeping-wall trigger | Any repeated click counts as a quest tear; blue/moving stream, reachability, queue, and re-click rules are not validated |
| `server/scripts/skill_thieving/scripts/ham_pickpocket.rs2` | Improved H.A.M. robe acquisition | The state-1 one-in-five robe roll is implemented and should be retained/tested |
| `server/scripts/skill_combat/scripts/player/specs/pvm_dttd_bone_dagger.rs2` | Backstab special | Mechanic exists with a documented solo-PvM simplification, but no quest-completion gate is present |
| `server/scripts/skill_combat/scripts/player/specs/pvm_dttd_bone_crossbow.rs2` | Snipe special | Mechanic exists, but no quest-completion gate is present |
| `server/scripts/ladders_stairs/` and DTTD multilocs | Trapdoors/ladders/tunnel transforms | Several climb handlers exist; neither open mills-side nor open H.A.M.-side permanent tunnel endpoint has a movement handler |
| `server/scripts/interface_questjournal/scripts/quest_journal.rs2` | Dynamic dbrow journal dispatch | Correctly calls `~dttd_journal`; retain this modern path |
| `server/scripts/quests/scripts/quest_cheat.rs2` | Generic `::complete` adapter | Sets primary state only; it does not normalize Lost Tribe state 13, actor ownership, items, or unlock services |
| `server/scripts/quests/quest_anothersliceofham/scripts/slice_urtag.rs2` | Downstream prerequisite | Correctly checks `%dttd_main >= 13`; retain after state migration |

### Cache-native content not properly connected

The osrs239 cache supplies dedicated Zanik cellar/follower/H.A.M./corpse/
revival/crate/showdown forms; the unconscious-Zanik and crate objects; five
quest guards and five post-quest guards; multiple Sigmund forms; the drilling
machine; hidden/open trapdoors and tunnel transforms; carrying base animations;
and state/region carriers. Modernization should connect these symbolic assets
through player-owned follower, cutscene, item, instance, combat, and maplink
services. Global `npc_find`/`npc_add` and text-only travel are not acceptable
substitutes.

## 4. Native state model

### Primary progression

The cache and Quest Helper agree on active states 0–12 and completion 13. The
local constants omit a name for 12, even though Quest Helper uses it for the
final return/finish phase.

| State | Canonical checkpoint | Current implementation / gap |
| ---: | --- | --- |
| 0 | Talk to Mistag and accept | Offer exists, but wrongly blocks until both current stats are 23 |
| 1 | Find Zanik; obtain two H.A.M. sets | Zanik exists; player must wear one set and carry exactly one inventory set, and pets are not checked |
| 2 | Tour Lumbridge with Zanik | Six-bit ordered subset; several mandatory/optional contacts and cutscenes are absent |
| 3 | Enter H.A.M. hideout in full disguise and investigate | Johanhus alone performs the member questions, speaker scene, trapdoor discovery, and state advance |
| 4 | Pick/discover the hidden stage trapdoor | Basic picklock message exists; no skill action/failure/disguise continuity |
| 5 | Solve five-guard stealth puzzle, hear meeting, get arrested | Five ordered Talk-to actions replace patrol/line-of-sight puzzle; arrest/jail/Jimmy/escape are omitted |
| 6 | Recover and carry unconscious Zanik to Juna | Body loc toggles a bit but grants no item; movement to Juna is mostly messages |
| 7 | Return twenty valid Tears and revive Zanik | Counter exists; collection source is not validated and revival is condensed |
| 8 | Recover follower, return to H.A.M. disguise, place Zanik in crate | State-8 follower is not restored; crate has no conditions and grants no wielded object |
| 9 | Enter player instance; defeat three guards and Sigmund | Shared static room/global NPCs; no prayer mechanic, Zanik shot, escape, or death lifecycle |
| 10 | Smash drilling machine | One click and state write; authored action/presentation absent |
| 11 | Leave south and play conclusion | Exit immediately completes through dialogue; no movement or finale |
| 12 | Final handoff/finish checkpoint | Unnamed and unused locally |
| 13 | Complete and retain unlocks | XP/scroll and Lost Tribe state write exist; unlock coverage and idempotence are incomplete |

### Native side carriers

`dttd_base` stores `%dttd_main` in bits 0–10, then fifteen named side fields
through bit 26: six tour flags, cellar visibility, shop/edge/H.A.M. discovery,
two-bit trapdoor state, corpse, collecting-tears, and Sigmund-present flags.
`dttd_temp` stores ten named guard/encounter fields in bits 1–11. Two anonymous
region fields exist on `dttd_region`. The authored permanent
`%dttd_tear_count` adds the missing 0–20 counter.

Important representation defect: `%dttd_zanik_in_cellar` is one bit, but the
constants declare hidden 0, visible 1, and resolved 2. Writing 2 truncates to
0. That happens to hide the carrier, but an impossible declared state must not
be used as control flow. Give the bit its real Boolean meaning and test both
transform values.

`%dttd_zanik_corpse` is also one bit. Its transform can hide or show the body,
but it cannot replace durable ownership of `dttd_dead_zanik`; the player's
inventory, follower/carry state, death, and recovery must agree with the bit.

The `dttd_temp` name suggests an ephemeral cache carrier, yet the current
implementation relies on it for five-guard and mill progress across travel and
relogin. Determine the engine persistence semantics explicitly. If it is
session/region temporary, restore the canonical puzzle reset behavior rather
than accidentally persisting or losing a half-solved encounter.

## 5. Required route and modernization findings

### Stage 1 — offer, levels, robes, and follower

Required behavior:

- allow any player who completed The Lost Tribe to accept before 23 Agility or
  23 Thieving;
- check the relevant boostable level at the action that needs it;
- preserve accept, decline, help, and every state-specific Mistag re-talk;
- activate the increased chance of pickpocketing H.A.M. robe pieces after the
  quest starts;
- require ownership of two complete H.A.M. sets, transfer one complete set to
  Zanik, and equip the player's remaining complete set for infiltration;
- reject or safely handle a pet before attaching Zanik; and
- make Zanik a player-owned follower with logout, teleport, boundary, death,
  dismissal, and recovery behavior.

The robe bonus is already implemented in `ham_pickpocket.rs2` while state is 1.
The recruitment transaction is not canonical: it requires the player to wear
all seven pieces and hold one spare of each in inventory. Canonical ownership
allows two complete sets before the handoff; the exact worn/inventory layout
should only be enforced when entering the hideout. Centralize a two-set
transaction, verify all seven deletions before committing, and define recovery
if the handoff is interrupted.

`~dttd_zanik_login` is not a follower service. It searches and mutates global
NPCs and can delete another player's follower. The cellar carrier already has
a world spawn. Replace login-wide actor mutation with an owned follower record
and region-aware recovery.

### Stage 2 — Lumbridge tour

Canonical tour coverage includes the Cook, Duke Horacio, Hans, a man or woman,
Bob, Father Aereck (and the prayer tutor interaction), Lumbridge Guide,
approaching the surface goblins, the general store, and authored reactions to
being outside. The current progression gates only Duke, sun, generic citizen,
Father Aereck, goblins, and shop.

The current six steps are also forced into a mostly linear order:
Duke → sun → citizen → priest → goblins → shop. The canonical tour permits
more flexible discovery and has contact-specific dialogue. Bind the relevant
world NPC subjects to one shared tour dispatcher, preserve their normal
dialogue for players without the follower, make each observation idempotent,
and let the journal enumerate what remains. Boundary warnings must not delete
or expose a global follower.

### Stage 3 — H.A.M. infiltration and hidden trapdoor

Required behavior:

- require a full worn H.A.M. disguise while bringing Zanik through the
  hideout;
- use the surface trapdoor/pick-lock flow without weakening shared Lost Tribe
  and H.A.M. membership behavior;
- support regular H.A.M. member/guard questions and Johanhus dialogue;
- hear the Deacon from the correct stage position;
- reveal and search/pick the rubble-hidden trapdoor; and
- preserve re-talk and recovery if Zanik is lost or the player leaves.

The current surface trapdoor remains open based on `%ham_thief=1` from shared
content and has no DTTD disguise gate. Johanhus compresses the questions,
speaker observation, and trapdoor discovery into one conversation, sets both
native observation bits, and advances the primary state. Implement each
world action against its native bit and location; do not invent a parallel
state machine.

### Stage 4 — five-guard stealth puzzle, arrest, and body

Canonical behavior uses cracks/visibility, moving guards, Zanik waiting and
“Now” signals, lures, and failure. Being caught sends the player to the H.A.M.
jail and resets the applicable puzzle state. After listening at the door, the
player is arrested, talks to Jimmy the Chisel, picks the cell lock, climbs out,
and finds Zanik outside. Leaving jail early has a Johanhus recovery path.

The current five guards stand at fixed tiles. Talking to them in numeric order
sets warned/dead bits through mesboxes; they have no patrol, line-of-sight,
catch, failure, jail, or reset logic. Listening at the door directly teleports
the player outside and shows the body. Jimmy is absent from the quest path.

`~dttd_ham_guards_login` is a critical multiplayer bug. On every player login
it globally deletes quest guards or the five map-spawned post-quest guards and
adds the set matching that one player's state. A pre-quest login can erase the
actors a completed player is using. Put the stealth puzzle in player-owned
instance/visibility state and leave persistent post-quest world guards under
the spawn/minigame system.

Clicking the body only clears a transform bit. It must atomically grant
`dttd_dead_zanik`, enforce capacity/carry rules, and support the documented
respawn at the hideout when the item is lost or the player dies.

### Stage 5 — route to Juna and twenty Tears

Both canonical routes must work:

1. use a Games necklace to Tears of Guthix when that teleport is available; or
2. bring Zanik, a valid light source, and a pickaxe, mine the rocks south of the
   goblin mines, squeeze through, traverse the swamp-cave hazards/stepping
   stone, and descend to Juna.

The current mining trigger checks for a pickaxe but tests `tinderbox` as the
light source. It then sets the Lost Tribe hole bit without moving the player.
The cave-down trigger is also message-only. Use the shared light-source policy,
consume no tinderbox for enclosed light sources, perform real maplinks/agility
checks, and retain the corpse item across both valid routes.

Juna currently accepts state 6 plus the dug-hole bit, not the unconscious
Zanik item. This rejects a Games-necklace arrival and accepts an intangible
body after the loc bit was cleared. Dispatch on actual carried quest-item or
owned carry state.

The authored tear counter is reasonable, but `tog_weepingwall` increments it
on every click. Require a currently blue, reachable moving stream; serialize
the pickup queue; reject stale/repeated clicks; restore the bowl/counter after
logout as canonical; and prevent ordinary Tears of Guthix reward logic from
interfering. The revival/story should use the native Zanik/Juna forms and
cutscene lifecycle, including safe abort and replay.

### Stage 6 — crate, mill instance, and Sigmund

Canonical pre-entry contract:

- after revival, restore Zanik as a follower, including recovery from the
  Lumbridge cellar after teleporting away;
- wear a full H.A.M. set;
- have both hands free;
- search the empty southern crate at the required location;
- receive and wield `dttd_zanik_crate`, with the proper carry animations; and
- recover the crate/Zanik safely if it is lost, unequipped, or the player
  leaves the route.

The current state-8 login path spawns no follower. Clicking the crate checks
none of those conditions, talks through `dttd_zanik_marked` even if no Zanik is
present, grants no object, and writes state 9.

The mill is canonically a player instance. The player defeats three level-22
guards, then level-50 Sigmund. Sigmund changes protection prayer to counter the
player. Zanik shoots him, forcing Protect from Missiles, so the player must use
Melee or Magic. At one hitpoint Sigmund escapes with a ring-of-life effect; he
does not die normally. Death places the grave outside the instance and ground
items left inside are lost.

The current `~dttd_spawn_mill_encounter` puts three identical guards and
Sigmund on one coordinate in a shared static map. `npc_find` makes this a
single encounter for all players. The NPCs have real retaliation/death hooks,
but one player's kills update only their own counters while removing the
shared NPCs. Sigmund has no prayer switching, Zanik shot, one-hitpoint escape,
or actor cleanup and dies through `~npc_default_death`.

Build an instance lifecycle with owned actors, explicit entry/re-entry state,
logout/region/death cleanup, per-actor positions, and a recoverable crate.
Retain the combat framework, but model the boss mechanics rather than a plain
NPC death. Reject/handle pets before instancing even if live OSRS historically
had a pet edge case; do not reproduce a known trap that can strand progress.

### Stage 7 — drill, conclusion, and completion

After Sigmund escapes, the player smashes the drilling machine and exits the
south passage. The exit begins the conclusion with Zanik and the Dorgeshuun
leaders and only then commits rewards. If the player lacks a light source, the
quest supplies a lit torch; if the inventory cannot receive it, the player is
returned to the watermill cellar until the condition is resolved.

The local drill is one message. The blocked exit calls completion without
moving the player or running a cutscene. The already-open mills-side endpoint
also only says the player squeezes through. No reciprocal handler for the open
H.A.M.-side endpoint was found, so the advertised permanent tunnel is broken.

`~dttd_quest_complete` writes state 13, calls the global guard mutator, writes
`%lost_tribe_quest=13`, awards both XP drops, and opens the shared reward
scroll. The XP amounts and one quest point are correct. The procedure has no
already-complete guard, so duplicate delivery can award XP again. Make one
atomic/idempotent commit own state 13, Lost Tribe's expanded post-quest state,
XP, quest points, completed-count, unlock flags, actor cleanup, and presentation.

## 6. Reward and post-quest audit

| Reward / effect | Current state | Required verification or fix |
| --- | --- | --- |
| 1 quest point | Shared completion derives it from the dbrow | Verify first completion and duplicate no-op |
| 2,000 Thieving XP | Awarded as 20,000 tenths | Make completion idempotent and test exact delta |
| 2,000 Ranged XP | Awarded as 20,000 tenths | Make completion idempotent and test exact delta |
| H.A.M. robe pickpocket chance | Implemented at state 1 | Verify activation timing and normal table coexistence |
| H.A.M. store room | Chests, keys, doors, pickpocket, and patrols exist behind state 13 | Remove login ownership conflict; test all entrances, guards, keys, loot, logout, and multiple players |
| Watermill tunnel | Closed loc transforms at completion, but open endpoints do not transport | Add both directions and test pre/post quest gating |
| Dorgesh-Kaan access | Completion promotes `%lost_tribe_quest` from 11 to 13 and Mistag/Kazgar gain Cellar/Watermill operations | Verify both operations, the physical route, relog, and existing Lost Tribe dialogue/items |
| Bone dagger special | PvM label exists, no DTTD gate | Gate activation according to the canonical unlock while preserving ordinary weapon use/trading policy |
| Dorgeshuun crossbow special | PvM label exists, no DTTD gate | Gate activation according to the canonical unlock while preserving ammo/weapon behavior |
| Nardok's shop | City NPC/shop assets appear geographically gated | Verify the city is actually reachable only after completion and shop operations work |
| Another Slice of H.A.M. | Explicit state-13 prerequisite exists | Retain and add a dependency regression test |

The two special-attack routines currently work for any player able to obtain
the tradeable weapons. The reward scroll advertises them as unlocked, so the
absence of a quest-state gate is a real reward-policy defect, not merely a UI
wording issue.

## 7. Journal, recovery, debug, and lifecycle

The journal uses the modern dbrow dispatcher and shared `~quest_journal`, which
is the right engine path. Its content is not state-accurate:

- states 2 and 3 share text that still tells a completed tourist to tour;
- the six tour flags are not enumerated;
- disguise ownership and Zanik follower recovery are omitted;
- Johanhus/Deacon/civilian/trapdoor discovery bits are not reflected;
- individual guard/failure/jail progress is absent;
- body found versus carried-item state is absent;
- tear collection does not show the persisted 0–20 count;
- revived follower, H.A.M. re-disguise, free hands, crate ownership, mill
  instance/re-entry, and state 12 are absent; and
- the completion entry cannot prove the permanent tunnel or reward services.

Modernize the journal after the state/ownership model is stable. Every entry
must tell a reconnecting player where the recoverable NPC/item/action is, not
just summarize the previous scene.

`~dttd_debug_reset` clears the named DTTD bits, tear count, Lost Tribe hole,
and H.A.M. concussion counter. It does not delete unconscious-Zanik or crate
items, detach owned followers, destroy a mill instance, clean global combat
actors, restore the consumed robe set, or safely demote `%lost_tribe_quest`
from 13 to the Lost Tribe completed state. A reset must be an explicit test
fixture with exact ownership cleanup; it must not corrupt unrelated H.A.M. or
Lost Tribe progress.

`::dttdrun` directly writes through the tour, disguise, stealth, jail, corpse,
cave, tears, crate, combat, and finale. It proves only that state variables can
be assigned. Replace it with transition assertions and a real-client path; do
not cite its “OK” line as Gate D evidence.

The generic `::complete` arm writes primary state 13 only. It currently leaves
Lost Tribe at 11, so Mistag/Kazgar's three-operation form and access path may
remain locked. The adapter must call an idempotent quest completion-state
normalizer without granting XP or replaying the scroll, and its second call
must be a no-op.

## 8. Modernization work order

1. Correct the dbrow release date and stale prerequisite comment; name state
   12; document the real Boolean meanings and persistence of every carrier.
2. Move the 23 Agility/Thieving checks from Mistag acceptance to the exact
   boostable obstacles; preserve The Lost Tribe as the only start gate.
3. Implement an owned Zanik follower/carry lifecycle and remove both global
   login actor mutators. Leave persistent cellar/post-quest actors to world
   spawns and minigame AI.
4. Make the two-set robe handoff atomic, add pet handling, implement all tour
   contacts/reactions, and support arbitrary canonical tour order/recovery.
5. Enforce the worn disguise in the H.A.M. hideout and split civilian,
   Johanhus, Deacon, hidden-trapdoor, and picklock actions across native bits.
6. Build the five-guard patrol/visibility/lure puzzle in player-owned state,
   including catch/reset, jail, Jimmy, lockpick/ladder, early-exit recovery,
   and multiplayer tests.
7. Grant/track/recover `dttd_dead_zanik`; implement both real Juna routes with
   shared light and maplink/agility machinery; validate blue tear streams and
   make revival replay-safe.
8. Restore the revived follower, full disguise/free-hands/location checks, and
   `dttd_zanik_crate` wield/carry/recovery transaction.
9. Create the mill instance, three-guard encounter, Sigmund prayer switching,
   Zanik ranged shot, one-hitpoint escape, death/grave/re-entry, and cleanup.
10. Implement drill/finale presentation, torch/full-inventory branch, physical
    southern exit, and permanent two-way watermill tunnel.
11. Make completion and the cheat adapter atomic/idempotent; verify all reward
    services, special-attack gates, Dorgesh-Kaan travel, Nardok, H.A.M. store
    room, and Another Slice prerequisite.
12. Rewrite the journal and debug fixture, add automated transition/recovery/
    multiplayer tests, then complete the full Gate D build/pack/client suite.

This order deliberately fixes ownership before adding more scenes. Building a
larger quest on top of global login-spawned followers and guards would make
later instance, recovery, and multiplayer evidence unreliable.

## 9. Required verification matrix

### Static and pack checks

- `python3 tools/questhelper_extract.py deathtothedorgeshuun --check`;
- quest manifest, journal dispatcher, start trigger, completion call, and cheat
  adapter all agree on `quest_deathtothedorgeshuun` and end state 13;
- no raw entity/interface IDs, undisclosed soft-skip, legacy modal open, or
  duplicate subject trigger remains;
- no DTTD login proc calls global `npc_add`, `npc_del`, or adopts a world NPC
  as a player follower;
- all native one-bit fields are assigned only 0/1;
- `make -C src mock230-scripts`; and
- `mock230_pack --check-only` against the intended cache.

### Automated route and invariant tests

- The Lost Tribe incomplete refuses; complete with sub-23 skills can accept.
- Boosted 23 Agility/Thieving passes the relevant action; expired boost fails
  without corrupting progress.
- Zero, one, incomplete, worn, banked, and two full H.A.M. set combinations;
  full inventory; interrupted handoff; and pet present.
- Every Lumbridge contact in multiple valid orders, repeat clicks, boundary
  crossing, teleport, logout, death, and follower recovery.
- Hideout entry without/with disguise; every interrogation/discovery bit;
  trapdoor failure/retry; guard sight/catch/reset; all lures; jail/Jimmy escape;
  early departure and re-entry.
- Corpse full-inventory refusal, pickup, drop/destroy/death recovery, duplicate
  prevention, and both Juna routes.
- Invalid/static/dry stream clicks do not count; twenty valid blue tears count
  exactly once; logout/reconnect preserves canonical progress; revival cannot
  duplicate or strand Zanik.
- Crate requires follower, location, disguise, free hands, and capacity;
  inventory/equipment loss and re-entry recover correctly.
- Two simultaneous players receive separate mill instances and actors; guard
  deaths cannot advance or remove another player's fight.
- Sigmund responds to styles, Zanik forces Protect from Missiles, Ranged alone
  cannot finish the intended phase, and the one-hitpoint escape is not death.
- Mill logout/death/re-entry/grave/ground-item cleanup follows policy.
- Drill and exit are one-shot; torch/full-inventory branch is recoverable;
  completion grants exact XP/QP once.
- Both permanent tunnel directions, Mistag/Kazgar options, Dorgesh-Kaan/Nardok,
  H.A.M. store room, both specials, and Another Slice gate survive relog.
- `::complete quest_deathtothedorgeshuun` twice normalizes all permanent state
  without duplicate XP, quest points, or presentation.

### Real-client evidence

Capture the real Mistag offer, two-set handoff, representative tour branches,
H.A.M. disguise/questioning, stealth success and jail failure, unconscious
Zanik item, both Juna routes, valid blue tear collection, revival, crate wield
animations, mill instance/prayer fight, Sigmund escape, drill/finale, reward
scroll, both tunnel directions, and two-player isolation. Record packets or
screenshots for each modal/cutscene and for relog recovery at states 2, 5, 6,
8, 9, 11, and 13.

## 10. Gate verdict

| Gate | Verdict | Blocking evidence |
| --- | --- | --- |
| A — complete discovery | Complete for this audit | Nine quest-owned files, native row/carriers, shared start/login/Juna/H.A.M./combat/travel/journal/cheat/dependency surfaces identified |
| B — modern engine | **Fail** | Global login actor mutation, unowned followers/guards, shared mill encounter, text-only travel, and impossible one-bit state value |
| C — gameplay/narrative | **Fail** | Wrong start gate; reduced tour/infiltration; no stealth failure/jail/corpse item/cave traversal/crate item/instance/prayer fight/finale; incomplete reward unlocks |
| D — verification | **Not run for modernization** | Quest Helper extraction passes, but no implementation changes, build/pack suite, transition tests, multiplayer test, or real-client evidence exist |

Death to the Dorgeshuun must remain `audit-pending`. A debug run reaching state
13 and a reward scroll are not sufficient: every critical route, recovery
state, multiplayer boundary, and permanent unlock above must work before this
row can become `verified-modern`.
