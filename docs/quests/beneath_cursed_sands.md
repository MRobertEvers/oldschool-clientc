# Beneath Cursed Sands modernization audit

Status: `audit-pending` — the cache-native quest row, state carriers, public
scenery transforms, item/NPC/sequence assets, journal arm, cheat arm, and
modern completion call exist. The legitimate route is blocked immediately
after reading Jamila's message because no production spawn places any of
Maisa's four public-world carriers. If those NPCs are injected, the script
reduces every defining interaction, puzzle, cutscene, and boss to a click that
writes a later state. Completion is also repeatable: every post-quest click on
Maisa or the High Priest awards another 50,000 Agility XP, two quest points,
and another completed-quest count.

Audited: 2026-08-17

Governing plan: [Quest modernization plan](../QUEST_MODERNIZATION_PLAN.md). This
record applies that plan's Gates A–D to Jamila, every Maisa placement, the
Necropolis and Ruins of Ullek, the Scabarite tomb, both boss encounters, Zahur
and the Nardah shop, the Sophanem High Priest's three-quest dispatcher, Osman,
Selim, the reward items, fairy ring and pharaoh's sceptre unlocks, music,
Tombs of Amascut access, the journal, the cheat adapter, and the completion
lifecycle. It is an implementation specification, not completion evidence.

## 1. Authoritative references

These stable OSRS Wiki revisions define the route, dialogue, combat, rewards,
and permanent unlocks.

| Reference | Pinned revision | Use in this audit |
| --- | --- | --- |
| [Beneath Cursed Sands](https://oldschool.runescape.wiki/w/Beneath_Cursed_Sands?oldid=15297310) | 15297310, 2026-08-13 | Identity, requirements, complete route, bosses, rewards, unlock timing, music, and downstream dependency |
| [Beneath Cursed Sands/Quick guide](https://oldschool.runescape.wiki/w/Beneath_Cursed_Sands/Quick_guide?oldid=15206000) | 15206000, 2026-05-05 | Ordered actions, valid items, furnace/chest/emblem, lever run, randomized riddle, chemistry solution, and re-entry |
| [Transcript:Beneath Cursed Sands](https://oldschool.runescape.wiki/w/Transcript%3ABeneath_Cursed_Sands?oldid=15263401) | 15263401, 2026-07-14 | Acceptance/refusal, interrupted re-talks, item recovery, full inventory, shared NPC choices, finale, and owed rewards |
| [Head Menaphite Guard](https://oldschool.runescape.wiki/w/Head_Menaphite_Guard?oldid=15200294) | 15200294, 2026-04-28 | Level-174 encounter and punishment for overhead protection prayers |
| [Champion of Scabaras](https://oldschool.runescape.wiki/w/Champion_of_Scabaras?oldid=15242426) | 15242426, 2026-06-29 | Level-379 boss, shadow burst, timed rift, poisonous swarm/flames, death, and door persistence |
| [Menaphite Akh](https://oldschool.runescape.wiki/w/Menaphite_Akh?oldid=15293750) | 15293750, 2026-08-12 | Level-351 boss, lightning, shadow styles, translocation/channel, immunities, escape, death, and same-tick kill policy |
| [Ruins of Ullek](https://oldschool.runescape.wiki/w/Ruins_of_Ullek?oldid=15211859) | 15211859, 2026-05-17 | Public ruins, tomb entrance, required traversal, and dungeon relationship |
| [Necropolis](https://oldschool.runescape.wiki/w/Necropolis?oldid=15276372) | 15276372, 2026-07-27 | Investigation world state, obelisk, mine, fairy-ring island, and Jaltevas Pyramid |
| [Keris partisan](https://oldschool.runescape.wiki/w/Keris_partisan?oldid=15254688) | 15254688, 2026-07-05 | Reward/reclaim, wield requirement, scabarite/kalphite damage and puncture effects, and absence of a base special attack |
| [Circlet of water](https://oldschool.runescape.wiki/w/Circlet_of_water?oldid=15290451) | 15290451, 2026-08-08 | Uncharged reward, charging, heat protection, maximum charges, and paid/free reclaim rules |
| [Pharaoh's sceptre](https://oldschool.runescape.wiki/w/Pharaoh%27s_sceptre?oldid=15242598) | 15242598, 2026-06-30 | Jaltevas commune gate, teleport destination, charges, and menu behavior |
| [Into the Tombs](https://oldschool.runescape.wiki/w/Into_the_Tombs?oldid=15218542) | 15218542, 2026-05-28 | Direct dependency and the actual post-quest route into Tombs of Amascut |
| [Lily of the Elid](https://oldschool.runescape.wiki/w/Lily_of_the_elid?oldid=15217097) | 15217097, 2026-05-25 | Roger/meat crossing and quest-item lifecycle |

The sources identify Beneath Cursed Sands as quest #153, a medium, master,
members quest released 27 April 2022 and the fourth Kharidian/Desert-series
quest. It requires Contact!, 62 Agility, 55 Crafting, and 55 Firemaking. The
three skills are not boostable and are required before starting. Recommended
combat is 85.

Transition aid only: the local Quest Helper checkout's
[`BeneathCursedSands.java`](https://github.com/Zoinkwiz/quest-helper/blob/5ea99d5ea9ba3fb096ebe7b5ed02d80883e9819d/src/main/java/com/questhelper/helpers/quests/beneathcursedsands/BeneathCursedSands.java)
and
[`TombRiddle.java`](https://github.com/Zoinkwiz/quest-helper/blob/5ea99d5ea9ba3fb096ebe7b5ed02d80883e9819d/src/main/java/com/questhelper/helpers/quests/beneathcursedsands/TombRiddle.java)
at commit `5ea99d5ea9ba3fb096ebe7b5ed02d80883e9819d` confirm every even
primary state from 0 through 106, completion at 108, coordinates, items,
randomized urn state, burner state, and encounter boundaries. They guide
transition tests but do not override the Wiki, transcript, or osrs239 cache.

`python3 tools/questhelper_extract.py beneathcursedsands --check` resolves every
named item, NPC, loc, coordinate, varbit, and `quest_beneathcursedsands`.

## 2. Native quest identity and player contract

The cache-native `quest_beneathcursedsands` row and pinned sources define this
contract:

| Field | Native value / expected behavior |
| --- | --- |
| Cache quest ID | 169 |
| Type | Members quest |
| Difficulty / length | Master / medium |
| Series | Kharidian/Desert, #4 |
| Release date | 27 April 2022 |
| Start | Jamila in Sophanem, 3311,2779,0 |
| Prerequisite | Contact!, including its transitive prerequisites |
| Required levels | 62 Agility, 55 Crafting, 55 Firemaking; all unboostable and checked to start |
| Required items | Coal, iron bar, tinderbox, spade, and one supported raw or cooked meat; tinderbox/spade/meat can be obtained during the quest |
| Recommended | Combat 85, desert-heat protection, antipoison, food, melee and ranged gear, stamina, Nardah travel, dramen staff, and optionally a pharaoh's sceptre |
| Required combat | Head Menaphite Guard (174), two Scarab Mages (119), Champion of Scabaras (379), and Menaphite Akh (351) |
| Primary state | `%bcs`, bits 0–8 of `bcs_primary`, active states 0/2/.../106 and complete 108 |
| Side state | Investigation, excavation, pillar/mould/tomb/emblem, Lily/Roger, completion presentation, Selim, and reward-owed fields on `bcs_primary`; urns on `bcs_urns`; burners on `bcs_burner` |
| Quest points | 2 |
| Experience | 50,000 Agility XP (stored natively as 500,000 tenths) |
| Item rewards | One Keris partisan and one **uncharged** circlet of water, subject to owed-item recovery |
| Unlocks | AKP fairy ring on first Necropolis entry; optional Jaltevas sceptre teleport on Commune; Tombs of Amascut/Into the Tombs access; five music tracks |
| End state | 108 |
| Required for | Into the Tombs miniquest |

The dbrow correctly records Contact!, all three skill requirements, quest
points, recommended combat, XP, start NPC/coordinate, and end state. The
current start dialogue enforces none of the requirements.

### Native side-state inventory

| Varbit | Native storage | Required ownership |
| --- | --- | --- |
| `%bcs_mehhar_returned` | `bcs_primary` bit 9 | Mehhar has returned/named; dialogue and key phase |
| `%bcs_excavation_status` | bits 10–11 | Necropolis population/excavation transforms |
| `%bcs_investigated_entry` / `%bcs_investigated_citizens` | bits 12/13 | Both investigation facts, not substitutes for primary checkpoints |
| `%bcs_found_pillar` | bit 14 | Correct buried-chest dig clue |
| `%bcs_found_mould` | bits 15–17 | Tablet/chest/mould/emblem lifecycle |
| `%bcs_found_tomb` | bit 18 | Inner-tomb discovery and transforms |
| `%bcs_emblem_rotation` | bits 19–24 | Interface 750 rotation; downward solution is 15 |
| `%bcs_lily_type` | bits 25–26 | Crocodile/Roger/lily sequence |
| `%bcs_questcomplete_type` | bits 27–28 | Finale/reward presentation variant |
| `%bcs_met_selim` | bit 29 | Selim purchase/reclaim dialogue |
| `%bcs_owed_partisan` / `%bcs_owed_circlet` | bits 30/31 | Full-inventory reward recovery from tent/Selim and High Priest |
| `%bcs_urn_1..4` | four 3-bit fields on `bcs_urns` | Per-player randomized riddle placements |
| `%bcs_burner_1..3` | three 10-bit fields on `bcs_burner` | Chemistry interface 751 and valve coupling |
| `%pharaohs_sceptre_necropolis` | bit 6 on `pharaohs_sceptre` | Optional Commune unlock, independent of completion |
| `%fairyrings_log_akp` | bit 1 on `fairyrings_multi_2` | First Necropolis arrival unlock, independent of completion |
| `%buff_water_circlet_charges` | bits 8–26 on `buff_bar_varp_1` | Worn circlet charge display/effect |

The overlay declares only `bcs_primary`. The native `bcs_urns`, `bcs_burner`,
pharaoh's-sceptre, fairy-ring, and circlet carriers still resolve from the
global cache, but production scripts do not implement their policies.

## 3. Implementation surface

Paths below are relative to `OSRS-Content/osrs239-content/`.

### Quest-owned files

| Path | Present responsibility | Audit result |
| --- | --- | --- |
| `server/scripts/quests/quest_beneathcursedsands/configs/beneathcursedsands.constant` | Selected primary breakpoints, XP, and two coordinates | Omits 23 active checkpoints and every side/unlock constant needed for a faithful route |
| `server/scripts/quests/quest_beneathcursedsands/configs/beneathcursedsands.varp` | Permanent `bcs_primary` overlay | Does not document the other native carriers or their owners |
| `server/scripts/quests/quest_beneathcursedsands/scripts/beneathcursedsands.rs2` | Start, Maisa, Necropolis, tomb, Nardah, finale, completion, journal, and debug | A 445-line monolith whose header explicitly defers every defining fight, puzzle, interface, cutscene, and hard gate |

The root totals 492 lines across three files. `::bcsrun` directly writes a
sparse route, invents the cure, omits the actual unlocks, and invokes the same
unsafe completion procedure. It proves only that selected symbols link.

### Mandatory shared and cross-directory surfaces

| Path / subsystem | Relationship | Audit result / required ownership |
| --- | --- | --- |
| `server/scripts/interface_questjournal/scripts/quest_journal.rs2` | Dynamic journal dispatcher | Correctly calls `~beneathcursedsands_journal`; the journal collapses 54 active checkpoints to five vague lines |
| `server/scripts/quests/scripts/quest_cheat.rs2` | `::complete` adapter | Writes only `%bcs=108`; does not make excavation, reward debt, AKP, music, or other permanent state coherent |
| `server/scripts/areas/world/configs/m51_43.spawn` | Jamila carrier | `contact_market_craft_multi` is correctly placed and transforms to Jamila through Contact! state |
| `server/scripts/areas/world/configs/m52_43.spawn` | Campsite item props | Places only the spectacles and bottle ground items; no Maisa or quest population |
| All world `.spawn` files and all `npc_add` scripts | Maisa, Necropolis population, tomb actors, bosses, Osman, and Selim | No production placement exists for any BCS NPC carrier; the route hard-blocks at state 6 |
| `server/scripts/ladders_stairs/configs/ladders.loc` | Tomb stairs categories | Declares climb categories only; no exact destinations, gates, re-entry, or instance ownership |
| Cache-baked BCS locs/maps | Furnace, pillars, tomb, levers, urns, doors, lily, chemistry table, fires, and final entrance | Rich native loc/transform topology exists, but most operations are unbound or name-bound to instant state writes |
| `server/scripts/quests/quest_contact/scripts/contact_priest.rs2` | Sophanem High Priest during Contact! | BCS currently delegates here after its own window; preserve this subject |
| `server/scripts/quests/quest_icthlarin/scripts/icthlarin_pyramid.rs2` | High Priest during Icthlarin's Little Helper | `@ics_hipriest_talk` is the final fallthrough; preserve it and establish one documented priority dispatcher |
| `server/scripts/shop/nardah/scripts/nardah_general_store.rs2` | Shared Nardah General Store | BCS op1 shadows the real store during its meat window and creates free cooked meat; op3 remains the canonical shop path |
| Zahur's `elid_herbalist` operations and herb/potion services | Shared Clean/Decant/Make services | BCS takes op1 and returns a generic line outside its range; preserve ordinary menu subjects and the direct op3/op4/op5 services |
| Combat, death, poison, projectile, and owner-scoped instance services | Four combat stages and two boss arenas | No production combat starts, kill callback, hazard lifecycle, gravestone/re-entry, or simultaneous-player isolation exists |
| Interface 750 and 751 machinery | Emblem rotation and chemistry | Cache interfaces and native varbits exist; the script never mounts or arms either interface and directly writes solved values |
| Fairy-ring system | AKP destination/log unlock | Native `%fairyrings_log_akp` exists; no production writer or verified destination gate is present |
| Pharaoh's-sceptre/Pyramid Plunder system | Commune and Jaltevas teleport | Object menus and native unlock bit exist, but no sceptre operation, charge, Commune, or teleport script exists |
| `server/scripts/interface_music/` | Music lock/read policy | DB rows use `musicmulti_22` bits 22–26; no quest script writes any of the five bits |
| `itt_primary`, `%itt`, and `%toa_entrance_open` cache state | Into the Tombs and raid entrance | The miniquest dbrow/varbits and entrance transform exist, but no Into the Tombs implementation or real entrance travel exists |
| `server/scripts/skill_combat/` and Keris partisan configs | Reward weapon | Stats/animations exist, but the scabarite/kalphite 33% and 1/51 triple-damage effects are absent; base partisan incorrectly has a special bar |
| Desert-heat and charged-item services | Circlet reward | No Charge/Check/Uncharge handler, rune accounting, worn heat protection, charge display, or reclaim implementation exists |

### Cache-native assets already available

The cache contains substantially more than the script uses:

- camp, Necropolis, Nardah, Sophanem, cutscene, armed, and combat forms of
  Maisa, plus state-driven carriers for each public placement;
- all excavation citizens and guards, Head Guard combat/dying/injured forms,
  Osman, Selim, Mehhar, High Priest of Scabaras, Scarab Mages/swarms, Champion,
  rift, Akh/shadow, Menaphites, Kemesis, and final-cutscene actors;
- furnace stages, well, pillar/emblem rotation, tomb doors/stairs, both
  levers, projectile walls, four urn transforms, key urn, altar, stepping
  stones, lily, chemistry table, camp fires, and owed-partisan tent transform;
- message, tablet, chest, mould, emblem, four riddle emblems, rusty key, lily,
  cure crate, Keris partisan, and both circlet forms;
- dedicated BCS sequences, spot animations, projectiles, synths, interfaces
  749–751, and static regions around map squares 52/53,144; and
- native primary/side varbits, five music rows, AKP/Jaltevas unlock bits,
  reward-debt bits, and Into the Tombs state.

Modernization should connect these assets using the existing shared dialogue,
protected cutscene, owner-scoped instance, combat, projectile, poison,
interface, music, charged-item, maplink, and completion services. It should not
replace them with messages, global bosses, direct solved-state writes, or new
parallel progress fields.

## 4. Native state model and current reachability

Quest Helper and cache transforms expose every even state below. States in one
row still need distinct transcript/cutscene resume behavior; grouping here is
for readability, not permission to collapse them.

| State(s) | Required phase | Current implementation / defect |
| ---: | --- | --- |
| 0 | Jamila's shop menu and explicit accept/refuse | Starts without Contact! or skill checks; writes 2 even with a full inventory and no message |
| 2 | Receive/recover the slipped message | Re-talk recovery exists, but full-inventory semantics are incomplete |
| 4 | Read message checkpoint | Never written; reading at 2 jumps directly to 6 |
| 6/8/10/12 | Meet Maisa, accept investigation, protected travel cutscene, arrive at Necropolis | No Maisa carrier is placed. Injected Maisa jumps 6→14 and marks the entry investigated |
| 14 | Inspect blocked Jaltevas entrance | `toa_entrance` has no investigation branch |
| 16 | Question a valid citizen/guard | Citizen click at 14 skips directly to 18, so 16 is never written |
| 18 | Head Guard cutscene/fight | No spawned actor, cutscene, combat, prayer punishment, escape, death, or kill callback; injected click wins |
| 20/22/24 | Interrogation, return to camp, finish Maisa dialogue, explore cliffs | One Maisa click writes 26, skipping all three boundaries |
| 26 | Inspect furnace | One click skips fuel and writes 32 |
| 28 | Use coal | Never written; coal is never consumed |
| 30 | Use tinderbox and confirm lighting | Never written; tinderbox is never required |
| 32/34 | Search/read tablet, identify dig spot, dig chest, enter PIN 1118513, retrieve mould, smith emblem | One click on the wrong loc (`bcs_emblem_plaque`) sets mould state 2 and skips every item/action |
| 36 | Insert and rotate emblem downward in interface 750 | Same click directly sets rotation 15 and state 38 |
| 38/40 | Enter lower ruins, defeat two Scarab Mages and swarm | No travel/spawn/combat; injected mage click writes 44 |
| 42 | Descend second stairs | Never written or routed |
| 44 | Pull south then north lever within 60 seconds while avoiding darts | Either lever writes 46; no timer, identity, projectile, stun, damage, death anomaly, or retry |
| 46 | Randomized four-emblem tomb riddle | Any plaque/urn click writes all urns to 1 and state 48; no emblems, random solution, interface, swapping, or failure effects |
| 48/50/52 | Enter inner tomb and speak to Mehhar | Any broad tomb door writes 54; travel and primary dialogue checkpoints are absent |
| 54 | Search key urn after the incantation | Advances to 56 even when inventory is full and no key was granted; no lost-key recovery |
| 56/58/60 | Unlock persistent door, begin/fight Champion | No instance or combat; clicking Champion, swarm, or rift instantly writes 62 |
| 62/64/66 | Free and question High Priest of Scabaras | One click jumps 62→68, skipping incantation and two dialogue boundaries |
| 68/70 | Return to Maisa and Zahur in Nardah | No Nardah Maisa carrier; broad dialogue writes 72 |
| 72/74 | Buy/obtain supported meat and cross toward lily | Seller click gives free cooked meat without five coins or shop; Zahur can invent meat |
| 76/78 | Feed Roger and return with Lily of the Elid | Stepping stones, crocodile, confirmation, lily, capacity, and `%bcs_lily_type` are entirely absent |
| 80 | Give lily to Zahur/start equipment work | Broad Zahur handler writes 82 without consuming a lily |
| 82 | Solve coupled chemistry valves in interface 751 | Zahur or table directly writes burner values 9/45/45 and advances |
| 84/86 | Zahur finishes and hands/replaces cure crate | Never written; state advances to 88 even if full inventory prevented the crate, making replacement unreachable |
| 88 | Deliver cure crate to Sophanem High Priest | Deletes it if present but advances even when absent |
| 90 | Discuss cured city and prepare Necropolis return | Maisa click writes 92; no actual preparation dialogue/placement policy |
| 92/94/96 | Necropolis incantation, excavation transformation, camp/Osman cutscene, Akh spawn | No cutscene/population/state sequence; injected Akh can be clicked from 92 |
| 98 | Fight Menaphite Akh | No instance/combat/hazards; Akh or shadow click writes 100 |
| 100 | Speak to Osman after victory | No spawned Osman; injected click writes 102 |
| 102/104/106 | Return to Sophanem and complete bridge/Kemesis cutscene | Maisa at 102 calls completion immediately; High Priest can too; 104/106 are never written |
| 108 | Permanent completion/post-quest dialogue and owed rewards | Every Maisa or High Priest click calls completion again and duplicates XP/QP/completed count |

Of the 54 active pre-completion states, constants name 31 and production
writes only a sparse subset. Twenty-three Quest Helper states have no constant,
and broad range handlers erase the resumable cutscene/dialogue boundaries the
native carriers reserve.

### Deterministic first blocker

Jamila is reachable and reading the message writes `%bcs=6`. At that point the
player must talk to `bcs_maisa_camp` transformed as `bcs_maisa_vis` at
3378,2792,0. Repository-wide `.spawn` and `npc_add` searches find no placement
for that carrier, any other Maisa carrier, or any BCS story/combat NPC. Static
map squares 52/53,144 likewise contain no production NPC population. The
legitimate route therefore stops at state 6. Name-bound triggers later in the
monolith are not evidence of reachability.

### Repeatable completion exploit

`~bcs_quest_complete` unconditionally writes 108, sets presentation type 1,
adds 50,000 Agility XP, opportunistically adds items, and calls
`~quest_complete_rewards`. Maisa checks `if (%bcs >= 102)` first and calls that
procedure; the High Priest does the same. State 108 still satisfies both
conditions. Each post-quest conversation can therefore repeat the XP, two
quest points, completed-count increment, and completion scroll. This is a
release-blocking integrity defect independent of the route blocker.

## 5. Current versus required playable route

| Chapter | Current route | Modernized route contract |
| --- | --- | --- |
| Jamila | Short Yes/No; no gates; message/state can diverge | Preserve ordinary shop dialogue/trade, enforce Contact! and all three unboostable base levels before mutation, slip message only with capacity, and implement refusal/recovery |
| Maisa to Necropolis | Missing NPC; injected click jumps 6→14 | Place native carriers, protect each dialogue/cutscene boundary 6/8/10/12, teleport exactly, unlock AKP and Dunes of Eternity on arrival, and resume interrupted scenes |
| Investigation | Any citizen advances; entrance does nothing | Require blocked-entry inspection at 14, then one valid citizen/guard at 16; preserve population transforms and cutscene ownership |
| Head Guard | Injected click wins | Real staged fight with Maisa/guard actors, melee behavior, overhead-prayer one-third-HP punishment, escape/re-talk, death, and one kill-credit commit |
| Ullek approach | Maisa click jumps to furnace | Finish states 20/22/24, preserve tinderbox/spade acquisition, traverse actual cliff stairs, and unlock Ruins of Isolation on arrival |
| Furnace/chest/emblem | Furnace and plaque clicks solve everything | Consume coal, confirm tinderbox use, recover/read tablet, mark exact pillar, dig with spade, enter 1118513, recover chest/mould, consume iron bar, make/insert emblem, and mount/re-arm interface 750 |
| Mages/lever run | NPC/lever clicks advance | Travel to the lower ruins; spawn two real mages plus poison-capable swarm; require both deaths; implement two distinct levers, 60-second timer, darts, stun/damage, death/retry, and exact door transform |
| Tomb riddle | Any plaque/urn click sets four identical values | Generate one persistent per-player permutation, grant/recover four emblems, show riddle interface 749, support place/remove/swap with capacity checks, validate all urns, and apply poison/damage/stat drain without resetting on failure |
| Mehhar/key | Door skips dialogue; key state advances when no key is given | Travel through 48/50/52, name/return Mehhar, remember incantation, require dialogue before key, keep state recoverable on full inventory/loss, and permanently unlock Champion door on key use |
| Champion | Clicking any boss mechanic wins | Owner-scoped encounter with magic/melee, distance behavior, shadow burst, timed 20-HP rift/explosion, poison swarm/flames, kill/death/escape cleanup, surface gravestone, and persistent re-entry |
| Scabaras priest | One line skips three states | Incantation and full 62/64/66 transcript, correct actor transform, then release to Nardah |
| Nardah/lily | Missing Maisa; free/invented cooked meat; no lily | Preserve Zahur and store services, charge shop price, accept every Wiki-valid meat and reject fish/yak/etc., confirm feeding Roger, cross stones, pick one lily with capacity, and consume it at Zahur |
| Chemistry/cure | Directly writes solved burner values; loses crate on full inventory | Mount interface 751 with three coupled valves and persistent values; only advance on actual target; protect 82/84/86; grant or later replace exactly one crate |
| Cure hand-in | Advances without crate | Require and atomically consume the crate, then preserve Contact!/Icthlarin dialogue arbitration outside BCS's exact window |
| Necropolis/Akh | No cutscene or fight; injected click wins | Full incantation/excavation/camp/Osman/fire sequence across 92/94/96; owner-scoped Akh fight at 98 with lightning, shadow style switch, true-Akh channel interrupt, translocation, immunity, escape/restart, gravestone, and same-tick kill rule |
| Finale | Osman then Maisa skips 104/106 | Complete Osman dialogue, Sophanem re-talk, bridge/Kemesis cutscene, exact The Pharaoh music timing, interruption-safe 102/104/106 progression, then one idempotent completion transaction |
| Rewards | Gives charged circlet if space; no debt/reclaim; completion repeats | Award uncharged circlet and partisan or set exact owed bits; tent/Selim free partisan and later 60,000-coin purchase policy; High Priest free owed circlet then 10,000-coin replacement; one XP/QP/count transaction |
| Permanent unlocks | Reward text claims AKP/Jaltevas/ToA but no system changes | Unlock AKP on first arrival, Jaltevas only on optional Commune, open the post-quest tomb/Into the Tombs path, and write all five music bits at their authored events |

## 6. Oversight register

### P0 — release blockers and integrity

1. **No playable route after state 6.** Every required Maisa/Necropolis/tomb/
   finale NPC placement is absent.
2. **Infinite completion rewards.** Post-quest Maisa and High Priest dialogue
   repeatedly grants 50,000 Agility XP, two QP, and completed count.
3. **Every combat and puzzle is fake.** Injected entity clicks are instant wins;
   the furnace, lever run, riddle, chemistry, Champion, and Akh have no gameplay.
4. **Required items are not required.** Coal, iron bar, tinderbox, spade, meat,
   lily, key, and cure crate can all be skipped, invented, or bypassed.
5. **Reward loss is permanent or wrong.** Full inventory loses initial items;
   owed bits and recovery actors are unused; the script grants the charged
   circlet despite the transcript and item definition requiring uncharged.

### P1 — state, shared-system, and unlock correctness

1. Start prerequisites are absent despite complete native dbrow metadata.
2. Twenty-three active states are unnamed and most named states are skipped,
   so dialogue/cutscene interruption cannot resume safely.
3. Native AKP, Jaltevas, music, excavation, Lily, Selim, and owed-reward state
   is never written; completion text falsely advertises those features.
4. The only `toa_entrance` handler prints a description. `%toa_entrance_open`,
   `%itt`, and the entire Into the Tombs/raid route have no implementation.
5. Zahur op1 hides her normal conversation and the Nardah seller op1 bypasses
   the generated shop/coin path during the quest.
6. The High Priest is shared by three quests. Current range fallthrough is a
   useful start but lacks tests for every overlapping state and owed-circlet
   post-quest priority.
7. Tomb stairs have generic categories without quest destinations. Broad door
   triggers also bind several visually different doors to one state jump.
8. `bcs_urns` and `bcs_burner` are native permanent carriers but are neither
   declared locally nor given reset/randomization ownership.

### P2 — reward utility and presentation

1. The base Keris partisan should deal 33% bonus damage to kalphites and
   scabarites and has a 1/51 triple-damage proc; neither effect exists. It has
   `specwep=1`/75% energy even though only jeweled variants have specials.
2. Circlet Charge/Check/Uncharge operations are unbound. It should consume five
   water runes per charge, hold up to 500,000, add 12 seconds between desert
   heat drinks while worn, consume one charge per prevented drink, and return
   runes on uncharge.
3. Pharaoh's sceptre has four cache menu subactions but no operational charge,
   last-teleport, unlock filtering, or Jaltevas teleport implementation.
4. The five native music rows are `music_necropolis`, `music_ullek`,
   `music_mehhars_tomb`, `music_amascuts_thralls`, and `music_kemesis`, using
   `musicmulti_22` bits 22–26 respectively. None has a writer.
5. The journal ignores item recovery, puzzle partial state, boss readiness,
   current location, owed rewards, and 49 of the 54 active checkpoints.
6. Debug reset/run procedures clear only selected items/bits and can produce
   incoherent permanent unlock and reward-debt state.

## 7. Modernization work packages

Execute these in dependency order. Keep quest policy in RuneScript/config;
add C only for a missing reusable VM/protocol capability.

### Package 1 — state contract, placements, and shared dispatch

1. Add constants/comments for every even 0–108 primary state and every native
   side/unlock field; document exactly which operation owns each write.
2. Place all public carrier NPCs at cache/Quest Helper coordinates: four Maisa
   locations, Necropolis population, Mehhar/priest/tomb actors, Nardah phase,
   Osman, Sophanem finale actors, and Selim. Use native multi-NPC transforms;
   do not spawn permanent base variants from dialogue.
3. Establish one High Priest dispatcher whose tested priority is BCS active
   hand-in/finale/owed circlet, Contact!, Icthlarin's Little Helper, then
   ordinary post-quest subjects.
4. Merge BCS subjects into Zahur and Nardah store behavior without hiding
   op3/op4/op5 services or bypassing `~openshop`/coins.
5. Replace broad range jumps with exact, monotonic, interruption-safe commits.
   State changes occur after the protected action succeeds.

### Package 2 — start, investigation, and Head Guard

1. Read prerequisite quests/levels from the native contract or shared quest
   requirement helper and reject before any item/state mutation.
2. Implement Jamila's general/special-item menu, ring ruse, explicit Yes/No,
   inventory-full response, message Read/Destroy, and loss recovery.
3. Implement Maisa states 6/8/10/12 as protected dialogue/travel boundaries.
   On actual Necropolis arrival, idempotently set AKP and Dunes of Eternity.
4. Require the entrance then a citizen/guard, stage the confrontation in an
   owner-protected cutscene, and run the Head Guard through ordinary combat and
   death credit. Add his overhead-prayer punishment without affecting other
   prayer/combat encounters.
5. Preserve flee/re-talk/death paths and the post-fight return-to-camp scene.

### Package 3 — Ullek items, interfaces, and dungeon

1. Bind campsite tinderbox/spade acquisition and exact cliff stairs/maplinks;
   unlock Ruins of Isolation on first Ullek arrival.
2. Implement furnace 26→28→30, well/tablet/full-inventory recovery, correct
   pillar dig, chest/PIN, mould recovery, emblem smithing, and pillar insertion.
3. Mount interface 750 in its cache-authored modern parent, run its onload
   clientscript, arm/re-arm rotate/confirm ops, and validate rotation 15 on the
   server rather than trusting a client close packet.
4. Create owner-scoped tomb encounter state. Spawn two mages and swarm, require
   kill credit, bind exact stairs, then implement separate lever identities,
   60-second timer, arrow projectiles, stun/damage, door transform, and retry.
5. Generate/persist one random urn solution. Mount interface 749, grant and
   recover emblems, support all transfers atomically, validate the four native
   fields, and apply failure effects without rerolling/resetting.
6. Implement inner-tomb travel, The Forgotten Tomb unlock, Mehhar dialogue,
   incantation memory, and a recoverable rusty-key lifecycle.

### Package 4 — Champion and Scabaras aftermath

1. Consume the rusty key once to make the upper door permanently traversable,
   independent of later inventory/death state.
2. Allocate an owner-scoped Champion arena and implement ordinary attacks,
   distance rules, specials, spawned rift/swarm ownership, poisonous flame
   hazards, timers, cleanup, and one death callback.
3. Unlock Thrall of the Devourer when combat actually begins. Preserve escape,
   teleport, logout, player death, surface gravestone, re-entry, simultaneous
   players, and stale-instance cleanup.
4. Stage states 62/64/66 and the incantation/High Priest transform only after a
   credited victory.

### Package 5 — Nardah cure

1. Stage Maisa/Zahur dialogue while preserving ordinary Zahur services.
2. Define/test the full supported-meat predicate from the pinned guide; keep
   the generated five-coin shop route and reject invalid fish/yak/etc.
3. Bind stepping stones and Roger. Confirm and atomically consume one meat,
   write `%bcs_lily_type`, allow the crossing, and grant at most one lily with
   correct full-inventory/loss recovery.
4. Consume the lily only when Zahur accepts it. Mount interface 751, initialize
   native burner fields, implement coupled plus/minus valves, re-arm operations,
   persist reconnect state, and server-validate the marked target.
5. Protect states 84/86 and grant exactly one cure crate. Full inventory and
   later loss must leave a reachable replacement subject.
6. Require and atomically delete the cure crate before state 90, then return to
   the shared High Priest dispatcher.

### Package 6 — Akh, finale, and idempotent completion

1. Implement the Necropolis incantation/excavation and camp/Osman/Akh cutscene
   across 92/94/96 with protected actors, loc transforms, fire hazards, skip,
   logout, reconnect, and cleanup.
2. Allocate an owner-scoped Akh arena. Implement melee/stab weakness, ranged/
   magic resistance, running/freeze immunity, seven-action lightning cadence,
   two-tick dodge, shadow ranged→magic switch at half health, translocation,
   true-Akh channel interrupt, Vengeance/recoil immunity, and permitted thralls.
3. Preserve Osman escape/restart, death gravestone, same-tick kill-and-death
   success, and exactly one state-100 commit.
4. Implement Osman and the Sophanem/bridge/Kemesis finale at 100/102/104/106,
   unlocking The Pharaoh at its authored cutscene moment.
5. Replace `~bcs_quest_complete` with one guarded transaction: final-cutscene
   evidence, state 108, presentation type, 50,000 Agility XP, two QP/count via
   the shared completion call, and exactly-once scroll/jingle behavior.
6. Award uncharged `water_circlet` and `keris_partisan` only when absent across
   inventory/equipment/bank policy; otherwise set the exact owed bit. Never
   call completion from post-quest dialogue.

### Package 7 — permanent rewards and downstream access

1. Bind the owed-partisan tent transform and Selim. Claim the initially owed
   partisan free; implement later 60,000-coin additional purchases and
   `%bcs_met_selim` dialogue without confusing purchase with quest debt.
2. Give an owed circlet free from the High Priest. After the free entitlement
   is satisfied, implement the pinned 10,000-coin lost-item replacement rule,
   including the Into the Tombs compatibility condition in the transcript.
3. Implement circlet charged-item operations and desert-heat integration as a
   reusable charged equipment effect, not a quest-only heat loop.
4. Implement the base partisan's reusable attribute damage/proc rules and
   remove its false special bar while preserving jeweled variants' specials.
5. Implement/gate AKP in the fairy-ring service and the optional obelisk
   Commune/Jaltevas path in the pharaoh's-sceptre service. Completion must not
   retroactively claim a Commune the player never performed.
6. Bind the completed Jaltevas entrance to Into the Tombs start/access. The
   native miniquest/raid implementation is a separate implementation unit; do
   not falsely mark BCS verified while its advertised entrance is only text.
7. Correct the cheat adapter to make a coherent post-quest test save without
   granting production rewards or replaying completion. Set only unlocks that
   canonical completion necessarily implies; leave optional Commune false.

## 8. Verification contract

### Static and pack verification

- `python3 tools/questhelper_extract.py beneathcursedsands --check`;
- assert all even primary states 0–108 have explicit handler/journal/resume
  policy and no broad write skips a reserved boundary;
- assert Contact! and exact unboostable 62/55/55 checks occur before every
  start mutation;
- assert every public BCS carrier has one production placement and each
  temporary actor has one owner/cleanup lifecycle;
- assert `%bcs_excavation_status`, investigation, pillar/mould/tomb/emblem,
  Lily, completion type, Selim, owed rewards, all four urns, and all three
  burners each have a production owner and tests;
- assert AKP is written only on real Necropolis arrival and Jaltevas only on
  successful optional Commune;
- assert `musicmulti_22` bits 22–26 are written at the five authored events and
  unrelated bits remain unchanged;
- assert interfaces 749/750/751 use named modern mounts, onload setup,
  server-side validation, and op re-arming; no legacy modal open remains;
- assert the four combats use attack/death credit rather than op1 state writes;
- assert every required item is validated/consumed atomically and every unique
  quest item has capacity/loss/replacement policy;
- assert Jamila, Zahur/store, High Priest/Contact!/Icthlarin, fairy ring,
  sceptre, heat, combat, music, and ToA triggers each have one deterministic
  owner/dispatcher;
- assert completion can be reached only from state 106, writes 108 once, and
  no state-108 dialogue calls `stat_advance` or `~quest_complete_rewards`;
- assert reward item forms, debt bits, XP, QP, journal, dbrow, reward text,
  world transforms, and cheat arm agree;
- assert no active `Soft-skip`, injected-click win, free shop item, direct
  puzzle solution write, missing production spawn, or falsely advertised
  unbound operation remains undisclosed;
- `make -C src mock230-scripts`; and
- `mock230_pack --check-only` against the intended cache.

### Automated route matrix

At minimum, test:

1. start with Contact! incomplete/complete; skills one below, exact, boosted,
   drained, and above; refusal, full inventory, repeated click, and no mutation
   on failure;
2. Jamila ordinary menu/trade, special-item branch, message Read/Destroy,
   inventory-full handoff, loss/recovery, and every state 0/2/4 re-talk;
3. Maisa 6/8/10/12 with accept/refuse, cutscene close/skip/logout/reconnect,
   exact arrival, one AKP/music unlock, and simultaneous players;
4. entrance-before-citizen ordering, every valid/invalid citizen or guard,
   population transforms, duplicate packets, and state 14/16 recovery;
5. Head Guard with ordinary combat styles, overhead on/off, exact one-third-HP
   punishment boundaries, flee/re-talk, player death, boss death, duplicate
   death queues, and post-fight cutscene interruption;
6. Maisa 20/22/24, campsite tinderbox/spade with full inventory and duplicates,
   both cliff directions, Ullek arrival, and music unlock once;
7. furnace with missing/present coal and tinderbox, reversed use order,
   confirmation refusal, duplicate use packets, and exact state/item commits;
8. well/tablet Read/Destroy/loss/full inventory, wrong/correct dig tiles, no
   spade, chest PIN wrong/correct/cancel/reopen, mould loss/recovery, iron-bar
   use, and inventory edge cases;
9. emblem interface open/remount/logout, every rotation, forged-item
   requirement, confirm/cancel, client-forged values, and only rotation 15
   opening the route;
10. mage encounter allocation, two players, all kill orders, swarm poison,
    flee/death/logout/re-entry, duplicate kill credit, and no NPC-click win;
11. lever identity/order, 59/60/61-second timing, dart lanes, projectile stun
    and 0–10 damage, death/retry anomaly, logout/reset, and exact door state;
12. every randomized riddle permutation, persistence across re-entry/relog,
    emblem capacity/duplicate/loss, each urn place/remove/swap, wrong lever
    damage/poison/stat drain without reset, and correct completion;
13. tomb travel and Mehhar states 48/50/52, all dialogue subjects, key before/
    after incantation, full inventory/loss, permanent door unlock, and music;
14. Champion with melee/magic, distance, burst ranges, rift timer/20 HP/
    explosion, swarm/fire/poison, kill races, escape/teleport/logout/death,
    gravestone/re-entry, two owners, and one state-62 transition;
15. High Priest of Scabaras 62/64/66 with interruption and correct transform;
16. Maisa/Zahur 68/70, all accepted and rejected meat IDs, shop zero/exact
    coins/full inventory, normal shop and Zahur services, and no free item;
17. stepping stones before/after assignment, confirm/refuse, meat consumption,
    Roger state, lily one/full/lost, crossing both directions, and Zahur hand-in;
18. chemistry interface at every valve boundary, coupled changes, known
    solution, invalid client values, close/remount/relog, two players, target
    validation, full inventory at cure creation, and crate replacement;
19. High Priest with no/one/multiple crate, atomic consumption, every Contact!
    and Icthlarin state combination, ordinary post-quest subject, owed circlet,
    and no trigger shadowing;
20. Necropolis/camp cutscene at 90/92/94/96 with skip/logout/reconnect, actor/
    loc cleanup, excavation transforms, and simultaneous players;
21. Akh normal attacks, freeze/ranged/magic resistance, lightning cadence and
    two-tick dodge, shadow half-health style switch, translocation, correct/
    wrong channel target, thralls, Vengeance/recoil immunity, escape/restart,
    death, same-tick mutual death, and duplicate callbacks;
22. Osman and finale 100/102/104/106 with both Sophanem starters, every
    protected boundary, The Pharaoh unlock, full inventory, reconnect, and one
    completion transaction;
23. completion with each reward present in inventory/worn/bank or absent, 0/1/2
    free slots, duplicate queue/click/relog, exact XP/QP/count, uncharged
    circlet, owed bits, one scroll, and state 108;
24. post-quest Maisa/High Priest repeated conversations proving zero XP/QP/
    count changes; tent/Selim free debt claim and later 60,000 purchase; High
    Priest free debt claim and later 10,000 replacement;
25. circlet 0/1/500,000 charges, insufficient/exact/excess water runes, Check/
    Charge/Uncharge in inventory/worn/bank, heat ticks, depletion, relog, and
    rune refund; base partisan scabarite/kalphite/non-target damage and 1/51
    proc plus no base special bar;
26. AKP locked/pre-arrival/arrival/postquest, fairy-ring round trip, Commune
    without/with charged or uncharged sceptre, Jaltevas menu filtering/charge/
    last-teleport/landing, and no unlock from `::complete` alone;
27. each music track locked immediately before and unlocked at its exact event,
    manual playback, playlist/relog, duplicate unlock, and unrelated bits; and
28. `::complete quest_beneathcursedsands` twice, coherent state/transforms and
    no production rewards, then ToA entrance/Into the Tombs prerequisite and
    Contact!/Icthlarin/Zahur/store regressions.

### Live-client evidence

Capture a real-client run from a clean eligible account through all post-quest
reclaims without state/debug commands. Evidence must include:

- all start requirement refusals, Jamila's ring/message flow, message recovery,
  every Maisa placement, protected travel, AKP and Dunes unlock timing;
- entrance investigation, complete Head Guard scene/fight/prayer punishment,
  flee/re-entry, and population transforms;
- campsite items, Ullek travel/music, furnace fuel/light, tablet/dig/PIN/mould,
  emblem crafting and the live rotation interface;
- real mages/swarm, timed projectile corridor, at least two randomized riddle
  solutions including a failure, Mehhar/key recovery, and tomb music;
- two simultaneous owner-isolated Champion encounters, all three specials,
  death/gravestone/re-entry, music, and High Priest aftermath;
- normal Nardah shop/Zahur services beside quest subjects, accepted/rejected
  meat, Roger/lily, live chemistry interface, full-inventory crate recovery,
  and required cure hand-in;
- the full Necropolis/Osman/Akh sequence, every Akh special, escape/restart,
  death and same-tick kill handling, then the complete bridge/Kemesis finale;
- exact one-time reward transaction, uncharged circlet, both full-inventory
  debt paths, repeated post-quest conversations with no reward duplication,
  and paid later replacements/purchases;
- operational circlet heat protection, base-partisan target effects, fairy-ring
  AKP, optional sceptre Commune/Jaltevas, all five music tracks, and the real
  Into the Tombs entrance; and
- relog/reconnect captures at representative dialogue, cutscene, puzzle,
  instance, cure, finale, and owed-reward boundaries.

Only after static checks, automated matrices, pack validation, and live-client
evidence pass may this record change from `audit-pending` to
`verified-modern`.
