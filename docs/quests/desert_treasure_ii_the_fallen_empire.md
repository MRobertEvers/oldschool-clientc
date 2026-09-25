# Desert Treasure II – The Fallen Empire modernization audit

Status: `audit-pending` — this is a 419-line soft-skip scaffold, not an
implementation of the quest. Its own header defers requirements, the Digsite
war-room puzzle, all four medallion investigations and boss instances, the
cell escape, the recurring Mysterious Figure, the wights, and full dialogue.
The script contains 26 explicit “Soft-skip” messages. It references only the
native `%dt2` primary field; revision 239 exposes another 151 named fields for
the vault, war room, four routes, medallion ordering, prison, finale, rewards,
teleports, sanity, puzzles, and boss mechanics, none of which the server reads
or writes.

Ordinary play is deterministically blocked at the first transition. The
placed Ancient Vault door changes state 0 to 4 but neither checks requirements
nor moves the player inside, and `dt2_asgarnia_smith_vis` is not spawned.
Later fallback clicks also do not travel through the Digsite winch or crevice.
The completion routine can permanently withhold the ring and lamps when the
inventory lacks enough free slots, grants the charged ring form with zero
charges instead of upgrading the Ring of visibility, and has no usable lamp
handler.

Audited: 2026-08-17

Governing plan: [Quest modernization plan](../QUEST_MODERNIZATION_PLAN.md). This
record applies Gates A–D to requirements, discovery, all native state,
Familiar Faces, the Ancient Guardian and war room, the four order-independent
investigations, every route puzzle and encounter, the recurring pursuer,
prison/finale, rewards, post-quest bosses and services, shared NPCs, journal,
migration, and debug tooling. It is an implementation specification, not
completion evidence.

## 1. Authoritative references

The quest article and quick guide define the current route, requirements,
encounters, recovery, rewards, and unlocks. The transcript owns dialogue,
identities, cutscenes, re-talks, and finale ordering. Revisions were resolved
through the OSRS Wiki API on 2026-08-17.

| Reference | Pinned revision | Use in this audit |
| --- | --- | --- |
| [Desert Treasure II – The Fallen Empire](https://oldschool.runescape.wiki/w/Desert_Treasure_II_-_The_Fallen_Empire?oldid=15303590) | 15303590, 2026-08-16 | Identity, requirements, full route, encounters, rewards, music, and unlocks |
| [Desert Treasure II – The Fallen Empire/Quick guide](https://oldschool.runescape.wiki/w/Desert_Treasure_II_-_The_Fallen_Empire/Quick_guide?oldid=15293294) | 15293294, 2026-08-12 | Critical path, route items, puzzle ordering, combat summaries, and recovery |
| [Transcript:Desert Treasure II – The Fallen Empire](https://oldschool.runescape.wiki/w/Transcript%3ADesert_Treasure_II_-_The_Fallen_Empire?oldid=15282790) | 15282790, 2026-07-30 | Offers, refusal trees, identities, scenes, shared-NPC topics, wights, and completion |
| [Ancient Vault](https://oldschool.runescape.wiki/w/Ancient_Vault?oldid=15023279) | 15023279, 2025-11-12 | Start door, statues, interior, post-quest services, and ring destination |
| [Asgarnia Smith](https://oldschool.runescape.wiki/w/Asgarnia_Smith?oldid=15123075) | 15123075, 2026-02-07 | Vault partnership, disguise continuity, and DT1 sharing |
| [Terry Balando](https://oldschool.runescape.wiki/w/Terry_Balando?oldid=14922467) | 14922467, 2025-06-19 | Digsite hand-off and DT1/Dig Site shared routing |
| [Dr Banikan](https://oldschool.runescape.wiki/w/Dr_Banikan?oldid=15090401) | 15090401, 2025-12-19 | Digsite installation, Guardian, golem, and reveal |
| [Ancient Guardian](https://oldschool.runescape.wiki/w/Ancient_Guardian?oldid=15200249) | 15200249, 2026-04-28 | Shield, falling-rock special, instance, and death behavior |
| [The Stranglewood](https://oldschool.runescape.wiki/w/The_Stranglewood?oldid=15211913) | 15211913, 2026-05-17 | Infection, strangled, survival, shortcuts, and route geography |
| [Strangled](https://oldschool.runescape.wiki/w/Strangled?oldid=15272589) | 15272589, 2026-07-22 | Rooting, infection, route variants, and combat |
| [Kasonde](https://oldschool.runescape.wiki/w/Kasonde?oldid=15265760) | 15265760, 2026-07-17 | Survival defence, serum, betrayal, combat, and medallion trail |
| [Vardorvis](https://oldschool.runescape.wiki/w/Vardorvis?oldid=15275741) | 15275741, 2026-07-26 | Quest fight, axes, prayer projectile, roots, spikes, and replay distinction |
| [The Scar](https://oldschool.runescape.wiki/w/The_Scar?oldid=15211905) | 15211905, 2026-05-17 | Three passages, choking, puzzles, ships, and Leviathan route |
| [Catalytic Guardian](https://oldschool.runescape.wiki/w/Catalytic_Guardian?oldid=15197021) | 15197021, 2026-04-25 | Temple of the Eye entry and shared minigame ownership |
| [Wizard Persten](https://oldschool.runescape.wiki/w/Wizard_Persten?oldid=15196950) | 15196950, 2026-04-25 | Scar guidance, ship scenes, disguise continuity, and medallion |
| [The Leviathan](https://oldschool.runescape.wiki/w/The_Leviathan?oldid=15292448) | 15292448, 2026-08-11 | Prayer sequence, Shadow stun, back damage, specials, and replay distinction |
| [Ghorrock Dungeon](https://oldschool.runescape.wiki/w/Ghorrock_Dungeon?oldid=15271658) | 15271658, 2026-07-22 | Prison route, key puzzles, Jhallan, asylum, and boss access |
| [Jhallan](https://oldschool.runescape.wiki/w/Jhallan?oldid=15277198) | 15277198, 2026-07-28 | Pursuit/survival encounter and Secrets of the North sharing |
| [Duke Sucellus](https://oldschool.runescape.wiki/w/Duke_Sucellus?oldid=15275314) | 15275314, 2026-07-25 | Poison preparation, gaze, spikes, acid, Magic, and replay distinction |
| [Lassar Undercity](https://oldschool.runescape.wiki/w/Lassar_Undercity?oldid=15221113) | 15221113, 2026-05-29 | Teleporters, devices, Shadow Realm, keys, and route geography |
| [Blackstone fragment](https://oldschool.runescape.wiki/w/Blackstone_fragment?oldid=15191546) | 15191546, 2026-04-22 | Realm entry/exit, recall, and device ownership |
| [Sanity](https://oldschool.runescape.wiki/w/Sanity?oldid=15276224) | 15276224, 2026-07-26 | Meter drain, restoration, damage, and Whisperer fight |
| [Ketla](https://oldschool.runescape.wiki/w/Ketla?oldid=14995912) | 14995912, 2025-09-28 | Device crafting, Silent Choir route, and disguise continuity |
| [The Whisperer](https://oldschool.runescape.wiki/w/The_Whisperer?oldid=15275320) | 15275320, 2026-07-25 | Volleys, tentacles, sanity, specials, enrage, and replay distinction |
| [Mysterious Figure](https://oldschool.runescape.wiki/w/Mysterious_Figure?oldid=15200396) | 15200396, 2026-04-28 | Recurring medallion pursuit, final fight, ownership, death, and re-entry |
| [The Forsaken Assassin](https://oldschool.runescape.wiki/w/The_Forsaken_Assassin?oldid=15212399) | 15212399, 2026-05-17 | White/pink smoke, poison bottles, and wight sequence |
| [Ketla the Unworthy](https://oldschool.runescape.wiki/w/Ketla_the_Unworthy?oldid=15212400) | 15212400, 2026-05-17 | Clones, charged shots, healing, and wight sequence |
| [Kasonde the Craven](https://oldschool.runescape.wiki/w/Kasonde_the_Craven?oldid=15200397) | 15200397, 2026-04-28 | Corruption vials, shockwaves, styles, and wight sequence |
| [Persten the Deceitful](https://oldschool.runescape.wiki/w/Persten_the_Deceitful?oldid=15212397) | 15212397, 2026-05-17 | Lightning, portals, leeches, and wight sequence |
| [Azzanadra](https://oldschool.runescape.wiki/w/Azzanadra?oldid=15301694) | 15301694, 2026-08-15 | Mahjarrat scene, Elder Horn aftermath, ring upgrade, and completion |
| [Ring of visibility](https://oldschool.runescape.wiki/w/Ring_of_visibility?oldid=15183687) | 15183687, 2026-04-22 | Required input to Lassar and the final ring upgrade |
| [Ring of shadows](https://oldschool.runescape.wiki/w/Ring_of_shadows?oldid=15278103) | 15278103, 2026-07-28 | Upgrade, charges, destinations, tablets, Lassar restriction, and replacement |
| [Ancient lamp](https://oldschool.runescape.wiki/w/Ancient_lamp?oldid=15289051) | 15289051, 2026-08-06 | Three 100,000-XP choices, level gate, Destroy, and reclaim |
| [Ancient rings](https://oldschool.runescape.wiki/w/Ancient_rings?oldid=15085424) | 15085424, 2025-12-13 | Boss-kill wear gate, icon/vestige/ingot crafting, and skill rules |
| [The Forgotten Four](https://oldschool.runescape.wiki/w/The_Forgotten_Four?oldid=15272664) | 15272664, 2026-07-22 | Quest, repeat, awakened versions, drops, and progression |
| [Awakener's orb](https://oldschool.runescape.wiki/w/Awakener%27s_orb?oldid=15239080) | 15239080, 2026-06-25 | Awakened encounter entry contract |
| [Scar essence mine](https://oldschool.runescape.wiki/w/Scar_essence_mine?oldid=15283007) | 15283007, 2026-07-30 | Partial-quest access, Mining gate, extractor, coffer, and runic extracts |
| [Demonic Brutus](https://oldschool.runescape.wiki/w/Demonic_Brutus?oldid=15214877) | 15214877, 2026-05-22 | 2026 abyssal-potato post-quest unlock and encounter |
| [His Faithful Servants](https://oldschool.runescape.wiki/w/His_Faithful_Servants?oldid=15167680) | 15167680, 2026-04-07 | Miniquest prerequisite |

Transition aid only: the local Quest Helper implementation at commit
[`5ea99d5`](https://github.com/Zoinkwiz/quest-helper/tree/5ea99d5ea9ba3fb096ebe7b5ed02d80883e9819d/src/main/java/com/questhelper/helpers/quests/deserttreasureii)
contains nine Java files and 5,076 lines covering the primary 0–114 route,
four side-state machines, war-room and route puzzles, recovery branches, and
hundreds of cache coordinates/items/NPCs/locs. `python3
tools/questhelper_extract.py deserttreasureii --check` exits 0 and resolves
the quest dbrow and every extracted gameval. It is a routing/state oracle, not
proof that server travel, transactions, instances, combat, rewards, or shared
triggers work.

## 2. Canonical contract

Desert Treasure II is a members, grandmaster quest in the Mahjarrat series,
released 26 July 2023. It starts at the Ancient Vault north-east of Nardah and
is intentionally one of the longest and most combat-heavy quests in the game.
All six skills are unboostable and required to start: 75 Firemaking, 75 Magic,
70 Thieving, 62 Herblore, 60 Runecraft, and 60 Construction. The player must
also be on Ancient Magicks.

Direct prerequisites are Desert Treasure I, Secrets of the North, Enakhra's
Lament, Temple of the Eye, The Garden of Death, Below Ice Mountain, and His
Faithful Servants. Required equipment includes the Ring of visibility, a
pickaxe, tinderbox, pestle and mortar, face mask or Slayer helmet, and enough
runes for all four Burst spells or stronger Ancient equivalents. The metadata
recommends combat level 100.

A canonical run must:

1. validate every start requirement and Ancient spellbook at the placed vault
   door, enter the vault, complete Asgarnia's offer, inspect one plaque and all
   four statues, and follow Terry Balando to Dr Banikan;
2. mine into a player-owned Ancient Guardian instance, defeat its regenerating
   shield and falling-rock cycle, then use four Ancient elements, charged
   cells, and an eight-cell golem puzzle to identify the generals;
3. investigate Stranglewood, the Scar, Ghorrock, and Lassar in any order,
   preserving each long native side state, route items, shortcuts, instances,
   deaths, re-entry, and medallion recovery;
4. survive recurring, player-owned Mysterious Figure attacks while a medallion
   is carried, place each medallion on its corresponding statue, and preserve
   acquisition/placement order;
5. when returning the final medallion, survive the prison inventory escrow,
   escape the cell, recover equipment, defeat the final Mysterious Figure with
   multiple combat styles, and retrieve the medallion;
6. play the complete Mahjarrat/Sliske/Elder Horn scene, then defeat all four
   wights consecutively in a private encounter with restart semantics; and
7. complete exactly once, transform the Ring of visibility into the correct
   uncharged Ring of shadows form, deliver three recoverable lamps, and expose
   every current partial- and post-quest unlock.

Completion awards 5 quest points and three ancient lamps. Each lamp gives
100,000 XP in Attack, Strength, Defence, Hitpoints, Ranged, Magic, or Prayer
when that skill is at least 60. It also unlocks the ring and ancient-ring wear
contract, repeat and awakened Forgotten Four fights, their item ecosystem,
ring teleports/tablets, and Demonic Brutus. The Scar essence mine is actually
a partial-progress unlock after disturbing all three Scar passages and has a
separate level-64 Mining/Hagus permission contract; it must not be delayed to
the final completion click merely because the reward list mentions it.

## 3. Native identity, requirements, and persistence

| Field | Native value / expected behavior |
| --- | --- |
| Quest metadata ID / packed dbrow index | 185 / 2343 |
| Dbrow | `quest_deserttreasure2` |
| Type / difficulty / length | Members; grandmaster; very long |
| Series / release | Mahjarrat #14; 26 July 2023 |
| Start | `dt2_desert_vault_door` (loc 46743), near (3509, 2971, plane 0) |
| Prerequisite dbrows | DT1 27; Secrets of the North 2338; Enakhra's Lament 40; Temple of the Eye 167; Garden of Death 180; Below Ice Mountain 6; His Faithful Servants 3250 |
| Skill metadata | Magic 75; Firemaking 75; Thieving 70; Herblore 62; Runecraft 60; Construction 60 |
| Recommended combat | 100 |
| Primary field / end state | `%dt2`, `dt2_primary` bits 0–8 / 118 |
| Reward | 5 QP; three 100,000-XP lamps; unlocks |

### 3.1 Requirement implementation

There is no requirement procedure at all. Clicking the door at state 0 writes
state 4 for any player, regardless of skill levels, prerequisite quests,
members status, Ancient spellbook, Ring of visibility, or Burst-spell access.
The generic quest cheat can also write 118 without satisfying anything.

Implement one metadata-aligned start predicate using `stat_base` for all six
unboostable skills and native end states for all seven direct prerequisites.
Do not encode the transitive prerequisite tree separately: each prerequisite
quest owns its own closure. Validate Ancient Magicks at entry and provide the
canonical offer/refusal/requirements UI. Once the quest has genuinely started,
do not retroactively strand a player if a temporary spellbook/equipment
condition changes; enforce those conditions only at their relevant operations.

### 3.2 Native primary ladder

| `%dt2` | Canonical checkpoint | Current behavior |
| ---: | --- | --- |
| 0, 2 | Attempt/start Ancient Vault entry | State 0 becomes 4 with no checks or travel; state 2 has no authored constant |
| 4 | Talk to Asgarnia inside | Asgarnia is absent |
| 6 | Inspect plaque and four statues | Any one of five locs advances to 8; no side fields |
| 8 | Talk to Asgarnia again | One short line advances to 10 |
| 10 | Talk to Terry Balando | Shared handler advances directly to 12 |
| 12, 14, 16 | Descend and talk to Dr Banikan | Winch changes no position and keeps/sets 12; Banikan absent |
| 18 | Mine through the crevice | Crate/crevice click sets 20; no pickaxe grant, mining, or travel |
| 20, 22 | Defeat Ancient Guardian | Clicking a missing actor sets 24 |
| 24, 26 | Talk to Banikan after the fight | Missing actor; current constant uses only 24 |
| 28, 30 | Inspect altar and golem | Repeated click on any of three locs advances |
| 32 | Solve charged-cell golem puzzle | Repeated click sets 34 |
| 34 | Operate golem | Repeated click sets 36 |
| 36 | Search four generals | Repeated click sets 38 |
| 38, 40 | Final operation and Elissa direction | Repeated click sets 42; no Elissa hand-off |
| 42–84 | Four investigations in any order | All collapsed to state 42 plus inventory-only medallions |
| 86 | Return with final medallion | Any finale NPC click sets 88 |
| 88, 90 | Escape cell/recover items | Any finale NPC click sets 92; no prison loc handlers |
| 92 | Defeat Mysterious Figure | Any finale NPC click sets 94 |
| 94, 96 | Recover/place final medallion | Any plaque/statue click sets 98 without an item |
| 98 | Defeat four wights | Any finale NPC click sets 100 |
| 100–114 | Horn scene, consecutive wights, aftermath | At 100 any finale NPC immediately completes |
| 118 | Complete | Main value is correct; reward state is not |

The local constants expose only 20 values and use broad greater-than ranges.
They preserve the correct native end value and write only the primary varbit,
but omit the intermediate resume/cutscene states on which client transforms,
Quest Helper, recovery, and live dialogue rely. Record an exact live trace of
every even state 0–118 and any transient odd value before authoring handlers.

### 3.3 Native carrier inventory

Revision 239 defines 152 named fields across the audited DT2 carrier family.
Only `%dt2` appears anywhere in server RuneScript. The quest declares only
`dt2_primary`; every other carrier reports undeclared/non-transmitting to the
loc/var audit and cannot yet support native per-player presentation.

| Carrier | Native ownership | Required treatment |
| --- | --- | --- |
| `dt2_primary` | Main 0–118; plaque/statues; Banikan; four golem searches; four overlapping altar/totem bits; altar/chat; lamp; hideout gate | Preserve named bit writes; treat `%dt2_war_room_altar` as the four-bit aggregate alias, not independent storage |
| `dt2_secondary` | Seven-bit Stranglewood, Lassar, Ghorrock, and Scar ladders plus dialogue flags | Declare/transmit; route each investigation independently |
| `dt2_tertiary` | Four medallion-order fields; Stranglewood ingredients; ring reward; four ring teleports; shared dialogue; realm visibility | Declare/transmit; coordinate DT1 visibility and post-quest ring owners |
| `dt2_lassar_general` | Six schematics, seven teleporters, and empowered remnants | Drive every undercity transform and recovery source |
| `stranglewood` | Infection and three-minute survival telemetry | Separate durable checkpoints from temporary encounter meters |
| `dt2_noprotect*` | Horn room, prison, pursuer, Lassar temp state, eight golem cells, icon lesson, music, scoreboards | Reconstruct/clear temporary state safely across region/logout boundaries |
| `dt2_ghorrock_transmit` | Two shortcuts and asylum gate | Set only from canonical puzzle/door milestones |
| five Scar-maze carriers | Maze order, ritual nerves, memory sequence, fibres, growth, lights, breath, and three challenge completions | Generate once, persist/reset exactly per passage and leave rules |
| `whisperer*`, `leviathan`, `vardorvis` | Sanity, realm access, sector, and six QTE fields | Encounter-owned state; reset after death/exit without erasing quest checkpoints |
| `dt2_rewards` | Ten cache prayer-unlock fields | Do not grant them: current OSRS does not award the shelved Ruinous Powers |

Representative static evidence is decisive. `loc_var_audit.py` reports zero
reads and writes for each four-route field. `%dt2_stranglewood` drives 10 locs
across 25 placements, `%dt2_lassar` 12/23, `%dt2_ghorrock` 7/15, and
`%dt2_scar` 15/27; all four are `op_bound_gap`. The war-room altar is also an
`op_bound_gap`. Several route bases are hidden at value 0, so missing writes
make the intended entry or continuation literally invisible.

### 3.4 Legacy save migration

Migration is still required even though current code uses the native primary
field: the soft-skip implementation created primary checkpoints with all
native side fields at zero and may have granted route/reward items without
their entitlement bits. Run a versioned, idempotent conversion before any
modern handler interprets those combinations.

| Legacy evidence | Safe modernization policy |
| --- | --- |
| 0 | Keep not started; no requirement claim |
| 4 | Mark legacy-started and provide actual vault entry/Asgarnia continuation; do not trap behind newly checked start requirements |
| 6–10 | Preserve the nearest dialogue checkpoint, but require missing plaque/statue side interactions rather than inventing them |
| 12–24 | Resume at a safe Digsite travel/Guardian checkpoint; never fabricate a Guardian kill from an unreachable click |
| 28–40 | Preserve completed native side bits only when durable authored evidence exists; otherwise offer replay of missing cells/searches |
| 42 with medallion ownership | Record a compatibility entitlement for each held/banked/player-ground medallion; do not pretend the corresponding route combat is proven |
| 86 or later | Current code proves all four medallions were simultaneously in inventory once, but not their order or placement; mark route recovery eligible and restart at the safest final-medallion return boundary |
| 88–100 | Do not invent prison escape, Mysterious Figure, Horn scene, or wight kills; map to a safe owned finale checkpoint based on exact current location/items |
| 118 | Preserve completion; reconcile missing/duplicate ring and lamp entitlement separately and do not replay QP |

Current saves cannot reveal medallion order, whether a full inventory prevented
one or all rewards, whether lamps were used/destroyed, or whether a ring was
lost after delivery. Introduce an explicit migration ledger rather than
guessing from absence. Reconcile inventory, worn, bank, player-owned ground,
pending transfer, current location, completion count, QP/XP history where
available, and native reward fields. Test every authored state, every subset
and storage location of four medallions, 0–3 lamps, both ring forms/charges,
and repeated login before retiring compatibility logic.

## 4. Implementation and ownership surface

The quest root contains one 384-line script and two tiny config files, 419
lines total. That scale is itself evidence: the local Quest Helper route model
alone is 5,076 lines before server-only travel, transactions, NPC AI,
instances, combat, cutscenes, drops, and post-quest systems are counted.

| Surface | Current responsibility | Audit result |
| --- | --- | --- |
| `quest_deserttreasureii/configs/deserttreasureii.constant` | Twenty coarse primary constants and one exterior coordinate | Correct end state; incomplete state contract and no route constants |
| `quest_deserttreasureii/configs/deserttreasureii.varp` | Declares only `dt2_primary` | Other native carriers and transforms are unusable |
| `quest_deserttreasureii/scripts/deserttreasureii.rs2` | All route/finale/reward soft skips, journal, debug | No canonical end-to-end path or encounter |
| `quest_itexam/scripts/archaeological_expert.rs2` | Terry shared branch | One-line skip; precedence affects DT1 and The Dig Site |
| `quest_defenderofvarrock/scripts/dov_camdozaal.rs2` | Ramarno shared branch | DT2 branch directly grants Whisperer medallion and can shadow Defender dialogue |
| `quest_secretsofthenorth/scripts/secretsofthenorth.rs2` | Jhallan shared branch | DT2 branch directly grants Sucellus medallion; requirements omission can deadlock SOTN sharing |
| world spawn files | Boss and route actor discovery | Only unconditional Vardorvis, Leviathan, and `whisperer_quest` examples exist; core quest actors are absent |
| generic doors/ladders/maplinks | Some static route travel | Cache geometry exists, but state-driven entries and instances do not |
| `general/.../ring_of_shadows.rs2` | Charge/uncharge and one teleport | Wrong default destination, four permanent refusals, no tablet/native unlocks, incomplete restrictions/recovery |
| boss combat/drop systems | Quest/replay/awakened encounters and loot | No DT2 implementation; only scattered generated animation/stats and Soulreaper special code |
| crafting/equipment | Ancient rings and Soulreaper assembly/use | Acquisition/crafting/wear gates absent; one existing weapon special is not the unlock |
| Scar essence mine / Demonic Brutus | Partial/post-quest activities | Cache assets exist; server owners are absent |

The cache and maps already provide extensive native areas, actors, objects,
items, interfaces, varbits, animations, sounds, music rows, projectiles, and
transforms. Quest Helper resolves all extracted gamevals. Modernization should
use those symbolic assets and build real owned state around them; broad trigger
stacks that turn any actor or loc into “skip this chapter” must be deleted.

`tools/check_quest_combat_contract.py` currently exits 0 for its global
145-unit ledger, but the DT2 manifest entry remains `audit-pending` with empty
source, gameval, handler, loot, test, and known-gap arrays. The global pass
proves none of the Ancient Guardian, route, boss, pursuer, or wight contracts.

## 5. Discovery, vault start, Asgarnia, and Terry

The native start door is placed. Its state-0 operation prints that the player
enters, writes main 4, and returns; it never opens, teleports, instances, or
moves the player. At completion it only prints “The vault door,” so ordinary
post-quest access is also absent. Implement both exterior-to-interior and
interior-to-exterior travel, state/requirement gates, confirmation,
interruption, and reconnect destinations.

`dt2_asgarnia_smith_vis` has no world spawn and no `npc_add`. Even a player
whose state is advanced remains outside with no actor to continue. Restore the
symbolic base/transform family in the correct private or per-player vault
scene; do not hard-spawn a visible child. Preserve DT1's Asgarnia identity and
the eventual Sliske-disguise reveal without allowing one quest's transform to
hide the other's required actor.

Initial inspection is not “click any scenery once.” The plaque and four
statues have five distinct native bits and dialogue. Require each exactly once,
allow reinspection, then route the Asgarnia follow-up. Current code sets no
bits and advances on the first object, so cache presentation and journal state
remain untouched.

Terry's shared handler must retain DT2, DT1 translation, and Dig Site topics
for every valid state combination. Current DT2 precedence is sensible only
after the prerequisite is enforced; its single line contains no canonical
conversation. Add full transcript, interruption/re-talk, and state 10
transition. The east Digsite winch currently writes/retains 12 without moving
the player, and Banikan is absent, forming a second deterministic travel block.

## 6. Ancient Guardian and military installation

Implement the winch descent, Banikan dialogue, bronze-pickaxe crate, mining
animation/level-independent crevice work, installation entry, and safe return.
The current crate does not grant a pickaxe; the crevice does not mine or move;
both merely advance state. All hand-offs and state writes need capacity and
interruption boundaries.

The Ancient Guardian must be a player-owned instance. It has 200 Hitpoints and
a shield that regenerates unless pressured with melee; melee bypasses the
shield. Every fifth attack initiates falling rocks whose shadows telegraph safe
tiles, with 15–25 damage on failure. Protect from Melee blocks ordinary melee.
Items dropped in the instance disappear after the fight. Current code exposes
two NPC op names but no spawn, stats owner, AI, shield, rocks, death queue,
instance, or loot contract; clicking either missing actor would simply set 24.

After Banikan, the player must inspect the altar/golem, search crates for
uncharged cells, cast Smoke, Shadow, Blood, and Ice Burst-or-better on the
matching totems, charge cells at the altar, insert them, and solve the native
eight-position warmind puzzle. The four individual totem bits alias the
four-bit altar field; write them consistently and reject standard/nonmatching
spells without consuming the wrong resources.

Current code lets repeated clicks on the warmind, altar, or crate advance 28
to 32 to 34 to 36 to 38 to 42. It creates no cells, casts no spells, opens no
interface, changes no transforms, and does not require searches for Vardorvis,
Perseriya, Sucellus, or the Whisperer. Implement random/encoded cell positions,
interface validation, cancellation, reconnect, exact search fields, final
operation, and Elissa dialogue before opening the four routes.

## 7. Four medallions and the recurring Mysterious Figure

The four investigations may be completed in any order. Native seven-bit route
fields hold each detailed path, while four three-bit tertiary fields preserve
medallion order. The current server stays at main 42, writes none of them, and
only calls `~dt2_try_finish_four` when all four item IDs are simultaneously in
inventory. It therefore has no placement, route, recovery, or ordering truth.

Each current route binds many unrelated NPC forms to one block. Clicking any
listed Vardorvis/Kasonde/Elissa actor grants Vardorvis's medallion; Ramarno or
any Lassar actor grants the Whisperer's; Jhallan, Duke, or assassin forms grant
Sucellus's; Leviathan, Persten, or even one Scar black demon grants
Perseriya's. The add is inventory-only. Banking a medallion allows another to
be created; a full inventory prints “recovered” without granting anything.
Replace these blocks with route-specific dialogue, encounter death, chest or
debris recovery, and a central ownership ledger.

After at least one medallion is placed, the level-271 Mysterious Figure can
repeatedly ambush a player carrying another. She is player-owned, can follow
teleports/fairy rings and enter a POH, uses all three styles, protection
prayers, Tele Block and freezes, persists across logout, and cannot be attacked
by another player. If she kills the target, prison/item-recovery behavior
applies. Banking/placing the medallion removes the trigger. None of this exists.

Medallion placement must use the correct item on the correct statue, consume
exactly one, set the route/order/visual state atomically, and support recovery
before and after placement. Current `oploc1` conflates initial inspection and
final placement; at main 94 any one plaque/statue advances to 98 without an
item. Test every route order, duplicate/banked/player-ground items, pursuer
timing, full inventory, logout, death, and two simultaneous players.

## 8. Stranglewood: Vardorvis and Kasonde

`%dt2_stranglewood` is a native 0–127 ladder with at least these corroborated
milestones: Elissa 4, Barus 6, potion note 8, potion drunk 10, arrival scene
16, Kasonde 18, first survival complete 22, ingredients task 24, temple defence
34, serum 36/38, shortcut 40, Vardorvis 42, key hand-off 46, Kasonde hostile
48, Kasonde defeated 50, medallion clue 52, medallion obtained 54, route done
56. The server never reads or writes the field.

Implement Elissa's Lovakengj clue, Barus at the Burning Man, Kasonde's desk,
strange potion/note, drink and boat transaction, arrival scene, map access,
stamina totems, and route shortcuts. The native boat base is hidden at value 0,
so the unwritten side field prevents intended entry even though its map loc is
placed.

Strangled creatures need their quest variants, roots, continual damage,
infection, cure-state interaction, aggression, safe reset, and player
ownership where the survival activity requires it. The three-minute defence
must create six randomized/puzzle chests, two barricades, four explosive
satchels, wave directions, construction/detonation actions, Kasonde HP, timer,
success/failure reset, rewards, and cleanup. No part exists.

Implement korbal herb and argian berry acquisition, unfinished serum
combination, Kasonde's second defence, serum completion/drink, and the exact
permanent protection from rooting/infection. Transactions must support loss,
reclaim, wrong order, full inventory, and reconnect without regenerating
ingredients or skipping the defence.

Vardorvis's quest instance needs slash weakness, melee, axes and bleed,
floor spikes, the ranged prayer-disabling head projectile, root/QTE blood
splats, phase pacing, owner-only damage, null quest loot, temple-key drop, and
shortcut persistence. The unconditional world `vardorvis` spawn and generated
animation rows are not a fight implementation and must not be usable as a
pre-quest boss or medallion dispenser.

Kasonde then attacks with melee/ranged positioning, poison/venom liquid,
orange prayer-disabling splats, line-of-sight charge, healing, and arena
pillars. On his owned defeat, preserve the injured dialogue, tatty page,
hideout chest, medallion acquisition/recovery, and statue placement. Quest
Kasonde and later wight Kasonde must be separate encounters and mechanics.

## 9. The Scar: three passages and the Leviathan

`%dt2_scar` corroborates demons defeated 8, boat attempted 10, Persten 14,
passage/ship checkpoints through 16–36, Leviathan-ready 38, boss defeated 42,
Persten aftermath 44/46, medallion found 48, and route done 50. Five dedicated
maze carriers store order, challenge completion, random sequences, nerves,
fibres, lights, breath, and growth state. None is declared or written.

Implement shared Catalytic Guardian entry without breaking Guardians of the
Rift, the initial demon/abyssal roster, stepping stones, boat refusal, Persten
dialogue, and randomized three-passage order. The player needs a face mask or
Slayer helmet for later passages; choking/damage and the rule that leaving the
Scar resets unfinished passage puzzles require exact region cleanup. Player
ground items must never be moved into an inaccessible replacement instance.

All nine puzzle rooms are absent:

1. one passage contains Axon Terminal navigation, underwater broken nerve
   combinations/oxygen, and a protected summoning circle;
2. one contains underwater stem memory, antibody defence of a brain pillar,
   and lure/fibre repair of unlit veins; and
3. one contains cerebral-orb tether pathing, compound-rune Catalyst inputs,
   and lure plus randomized growth-order repair.

Each passage ends with tinderbox, gunpowder, slimy key, readable tablet, and
ship-burning steps. Implement generated order once, per-room reset rules,
oxygen/choking, NPC ownership, puzzle interfaces, failure damage, item cleanup,
ship state, Persten re-talk, and safe banking between completed passages.

The quest Leviathan requires colour/style prayer switching at increasing
speed, Shadow-spell stun, rear-arc bonus damage, lightning/debris specials,
arena sectors, enrage/phase behavior, exact quest stats, owner-only lifecycle,
and no post-quest unique loot. The unconditional `leviathan` spawn and empty
animation shell do not supply these mechanics. After the kill, implement
Persten's scene, debris search, Perseriya medallion ownership/recovery, and the
correct statue.

The Scar essence mine is a partial-route system, not simply a completion
reward. After all three passages are disturbed, level-64 Mining and Hagus
permission unlock amalgamation mining, a large persistent tainted-essence
store, Ventriculus conversion, coffer charges, pure essence, and the current
runic-extract table. No owner exists in server scripts. Implement/test it in a
separate shared Runecraft slice at the canonical Scar checkpoint.

## 10. Ghorrock: prison, Jhallan, and Duke Sucellus

`%dt2_ghorrock` corroborates soldier dialogue 10, letter 12, knife 14,
lockpick 18, cell escape 20, assassin departure 22, first door 24, gear
recovered 50, assassin follow-up 52, Jhallan 54/56, Duke access 58, Duke death
64, assassin aftermath 66, and route done 70. Separate native fields own
shortcuts and asylum gate. All remain zero.

Entering Ghorrock temporarily removes the player's inventory and worn items
into a recoverable quest escrow. Implement the cell wall voice, readable
prisoner's letter, bed/bucket knife and lockpick, staged hand-offs, lock puzzle,
escape, Assassin dialogue, equipment chest, and every exit/death/logout/relog
boundary. Never model this with destructive deletion or a shared ground pile;
prove exact restoration of item IDs, quantities, charges, degradables, and
equipment slots.

Implement the prison's four key chains and gates: sapphire numeric chest,
emerald word chest, ruby directional combination, and diamond word chest;
administration/refugee routes; lockpicking; blocked crevice; shortcuts; and
recovery after an interrupted combination interface. Passwords must be
validated server-side and the native door transforms must match each owner.

Jhallan is a survival/pursuit encounter, not a direct medallion click. He uses
Magic, teleports/freezes the player, and creates one-hitpoint illusions while
the player lights firecrackers and survives until the Assassin intervenes.
Preserve his Secrets of the North dialogue owner. Because start requirements
are currently absent, a player can start DT2 before completing SOTN and then
have the DT2 Jhallan branch preempt the prerequisite itself; fixing requirements
and topic routing must be one proving slice.

Duke preparation requires safely passing extremity gazes and floor attacks,
collecting and grinding arder/musca mushrooms, mining 12 salt per mixture,
filling vats, and producing/applying two poisons. Ingredient quantities vary
with the canonical Herblore threshold; trace exact current rules and item
recovery. The room supplies a pickaxe and pestle and mortar when needed.

Duke's owned quest fight needs adjacent spikes, heavy melee, reduced-through-
Prayer Magic at range, persistent acid clouds, six-attack gaze with pillar
line-of-sight, demonbane interaction, phase counters, and null quest loot.
After death, handle his key, asylum gate, Assassin scene, Sucellus medallion
chest/recovery, and statue. No Duke quest or post-quest actor is spawned by the
server today.

## 11. Lassar: Shadow Realm and the Whisperer

`%dt2_lassar` corroborates Prescott 4, rope 6, cathedral blackout 10, Ketla
dialogue 16, perfected schematic 26/28, Silent Choir 30, pub remnant 32, icon
36, vision escape 38, cathedral 42, Whisperer death 44, and route done 48.
Seventeen Lassar-general fields own schematics, seven teleporters, and
remnants; sanity and realm carriers own encounter presentation. None is used.

The current Ramarno splice in Defender of Varrock directly grants the
Whisperer medallion at any main state 42–85 and returns before Defender
dialogue. Replace it with an explicit topic router and canonical Ramarno/
Prescott conversation, very-long-rope grant, sinkhole attachment, entry, and
re-entry. Preserve Below Ice Mountain/Defender states and never make a DT2
debug state suppress another quest.

Implement all seven teleporters, infinite run/stamina pools including Ring of
endurance charge interaction, Ketla's blackout, five shadow keys, and the
full device lifecycle: shadow blocker, basic/superior/perfected torches,
revitalising idol, anima portal, schematics, workbench, placement, recall,
loss/reclaim, and realm-specific transforms. Current cache locs are present,
but the side ladder's zero writes leave them at the wrong variants.

Shadow Realm entry must use the blackstone fragment and a player-specific
realm. Sanity drains one per tick, Lost Souls remove additional sanity,
devices suppress/restore it, leaving restores it, and zero sanity starts rapid
damage. Death places the grave at the Camdozaal rope entrance. Implement
realm-only keys/locks/tentacles/braziers/remnants, player-owned actors, device
range, safe logout, no cross-player loc state, and return to the exact real-
world position.

Implement the Silent Choir trail, two icon segments, combined strange icon,
drain/cistern, hallucination scene, totem destruction, real perfected torch,
cathedral opening, and Ketla departure. Dialogue and scenes must reflect the
later Sliske disguise reveal without leaking it prematurely.

The Whisperer's quest instance needs Magic weakness, sanity UI, delayed
Ranged/Magic volley prayer switching, homing tentacles, Blackstone realm
specials, Lost Soul phrase groups, seed colour puzzle, pillar screeches,
binding pursuit countered by Ice spells, and the final 100-HP enraged phase.
Implement simultaneous-projectile timing, tentacle movement, realm toggles,
death-tick medallion rules, entrails search/recovery, and null quest loot.
The static `whisperer_quest` spawn has generated basic stats but none of this
AI, ownership, or progression.

The completed Ring of shadows cannot be equipped in Lassar. Entry must unequip
it into a free inventory slot or refuse when full, and ring teleport must also
check a free slot. Current generic equip/entry code enforces none of this.

## 12. Prison, final Mysterious Figure, Horn, and wights

Returning the final medallion triggers an owned abduction and a second item
escrow. Implement the hairclip search, staged lock brute force, prison gate,
east-room equipment chest, missing-medallion evidence, west altar, and final
Mysterious Figure appearance. None of the bed, chest, gate, altar, barrier, or
portal operations has a quest handler.

The final Mysterious Figure has 450 Hitpoints, all three styles, switching
protection prayers, Magic stat drain, freeze/shove, and a delayed high-damage
purple orb that becomes dodgeable before impact. She is immune to poison,
venom, recoil, and teleport escape; the barrier is the safe exit and the quarry
portal is the re-entry. Require at least two damage styles through mechanics,
not an inventory heuristic. Her owned death must expose exactly the missing
medallion and preserve re-entry/death recovery.

Current finale handling stacks Azzanadra, the pursuer, and four wight NPCs on
one state switch. At 86 any one actor means “returned,” at 88 it means “escaped,”
at 92 “pursuer defeated,” at 98 “wights defeated,” and at 100 it completes.
Those actors are absent, so it is both unreachable and semantically unsafe.

After final placement, implement the complete Azzanadra, Hazeel, Enakhra,
Akthanakos, Asgarnia/Sliske, Elder Horn, and Barrows-brother scene using native
actors, camera, movement, animations, sounds, and music. Sliske's disguises as
Asgarnia, Persten, Kasonde, Ketla, and the Ghorrock Assassin must resolve at
the correct point. Native main states 100–114 need exact cutscene/encounter
boundaries and reconnect continuation.

The four wights are one consecutive private encounter. Leaving or dying
restarts from the Forsaken Assassin, except for the canonical simultaneous
Persten-death completion case. They ignore recoil and life leech. Implement:

1. Forsaken Assassin immunity until lured into white smoke, healing pink smoke,
   melee/ranged autos, and telegraphed poison bottles;
2. Ketla ranged attacks, four/five one-HP clones, charged shots intercepted by
   clones, and healing from successful charged damage;
3. Kasonde melee/ranged positioning, persistent corruption vials, 12-second
   Prayer drain, and one-to-three large shockwaves with a small safe sector;
4. Persten Magic, escalating lightning, one/two 30-HP portals, and rapid,
   accurate typeless wighted leeches.

Commit each form only from its owner death, preserve resources between forms,
clean every clone/portal/projectile/ground hazard on reset, and prove no player
can join, damage, block, or receive another's completion. Then play the Horn
escape and Azzanadra aftermath before awarding anything.

## 13. Completion, lamps, ring, and post-quest ecosystem

`~dt2_quest_complete` writes main 118 first, conditionally adds a ring if one
inventory slot is free, conditionally adds all three lamps only if three slots
are free, and then calls the shared 5-QP reward scroll. A player with zero to
two free slots completes permanently without one or both rewards. Checks cover
inventory only, so banked/worn rings can be duplicated. There is no explicit
once-only QP/reward token.

Canonical completion upgrades one Ring of visibility into an uncharged Ring
of shadows. Current code neither consumes/transforms the visibility ring nor
sets `%dt2_ring_reward_given`; it adds `ring_of_shadows`, the charged-form item,
with no stored charges. Make the one-for-one transformation slot-neutral where
possible, locate the input under canonical inventory/worn rules, set native
entitlement only after success, and route exceptional full/missing-input cases
to a recoverable post-completion claim without duplicating either ring.

The three ancient lamps are nonbankable, Destroy-able, and reclaimable from
the Mysterious Bandit. No Rub or Destroy handler exists, so current lamps cannot
award XP. Implement a selection interface limited to the seven canonical
skills at base level 60+, a 100,000-XP once-only consumption transaction, the
native lamp claim field, and exact recovery of 0–3 unspent entitlement. Never
infer that an absent legacy lamp was used without evidence.

The Mysterious Bandit must sell additional Rings of shadows for 75,000 coins
and reclaim unspent lamps. Ring ownership is intentionally not unique after
purchase. Payment/addition, charge state, PvP conversion, death, Destroy, and
bank/worn inventory need focused transactions.

The existing ring script has a useful matched-rune charge cap and uncharge
foundation, but major contracts are missing or wrong:

- the default teleport uses the exterior start coordinate, not the canonical
  Ancient Vault interior destination;
- Ghorrock, Scar, Lassar, and Stranglewood always refuse, with no tablet-use
  handlers or native teleport-unlock fields;
- it does not enforce completion/replacement, Lassar equip/unequip/full-slot
  restrictions, destination danger warnings, or correct bank configuration;
- its source falsely says the boss lairs do not exist in the tree, while the
  cache/maps and several world spawns do; and
- specific uncharge overflow/drop ordering remains knowingly deferred.

Implement frozen, scarred, sirenic, and strangled tablet unlocks atomically,
correct coordinates and arrival safety, one charge per successful teleport,
cancel/failure preservation, and all five subactions from inventory/worn/bank
as canonical. Add regression for 0/1/1000 charges and partial rune sets.

Post-quest Forgotten Four are distinct, harder repeatable instances with
normal unique tables; awakened versions require an Awakener's orb and enhanced
mechanics. Quest variants must never roll Virtus, chromium ingots, vestiges,
quartz, tablets, Soulreaper pieces, pets, or awakened rewards. No boss owner,
instance, death/drop table, kill count, tablet, orb, awakened, or scoreboard
system exists. Unconditional Vardorvis/Leviathan/Whisperer spawns must be
removed or gated.

Ancient ring wear requires the corresponding quest boss kill. Crafting needs
Peer's topic, Fremennik icon plus vestige and 500 blood runes at boostable 90
Magic/80 Crafting, then the upgraded icon, ring mould, three chromium ingots,
and a furnace. No acquisition/crafting/wear-gate owner exists. Soulreaper's
special attack script alone does not prove its four-piece assembly or boss
drops.

Current OSRS also exposes Demonic Brutus after DT2 through an abyssal potato
from the Scar. The cache contains `demonic_potato`, `cowboss_hardmode`, and
Brutus slippers, but no server implementation. Treat this 2026 unlock as a
separate owned encounter slice and do not omit it because the initial 2023
completion string predates it.

## 14. Shared systems and downstream integration

Terry, Elissa, Ramarno, Jhallan, the Catalytic Guardian, Asgarnia, Azzanadra,
Ring of visibility/Shadow Realm, generic maplinks, POH, fairy rings, death/
graves, Ancient spells, poison/venom/corruption, item charges, and shared
combat damage all cross quest ownership boundaries. Each needs an explicit
topic or service router rather than state-range interception.

Most dangerously, requirements are absent while DT2 branches are checked
before prerequisite content. A player can enter a logically impossible DT2
state and have Ramarno or Jhallan routes suppress Defender of Varrock or
Secrets of the North. Characterize all before/active/complete/debug
permutations and make prerequisite completion plus actor identity part of the
dispatch contract.

The cache still contains `dt2_rewards` fields for the proposed Ruinous Powers,
but the released/current quest does not award those prayers. Do not interpret
cache presence as canonical content. Conversely, Demonic Brutus is a current
reward even though it was added in 2026. Wiki revision/date pinning and an
explicit current-vs-cache policy are required for every post-release feature.

## 15. Journal, debug, and recovery adapters

The journal is dynamically registered and therefore uses the modern shared
renderer. Its content is only four broad sentences: before 42 investigate the
vault/war room, before 86 recover four medallions, before 118 place them/finish,
then complete. It cannot distinguish any primary checkpoint, side route,
puzzle, boss, medallion, prison, recovery, or reward entitlement and provides
no requirements.

Rewrite it from primary and native side fields. It should report exact missing
start requirements before acceptance; five vault inspections; Digsite/
Guardian/golem cells and searches; independent route checkpoint/recovery;
medallion ownership/placement; pursuer risk; prison equipment location;
Mysterious Figure/wight re-entry; and outstanding lamps/ring. Keep puzzle
solutions out unless the live journal exposes them, and do not leak Sliske's
identity before the reveal.

`::deserttreasureii` resets only main 0 and teleports to the exterior. `::dt2run`
deletes medallions/ring/lamps from inventory only, ignores bank/worn/ground and
all 151 side fields, direct-adds four medallions without capacity checks, jumps
0→4→42→86→100, and calls real completion. Repeated runs can duplicate QP,
items, or ground overflow while leaving stale native transforms and boss state.
The generic quest cheat merely writes 118, granting no QP, ring, lamps, kill
counts, teleports, services, or cleanup.

Replace these with explicit reset, setup, checkpoint, encounter, and complete
adapters. Reset only DT2-owned state/items/actors/instances after an exact
scope confirmation; support every primary and route checkpoint; construct
coherent native side state; distinguish reward/no-reward completion; and make
debug state impossible to confuse with live proof.

## 16. Modernization work order

1. Characterize every current primary value, all 152 cache fields, placed
   transforms, world actors, shared trigger precedence, item ownership, and
   current reward outcomes before changing persistence.
2. Capture live revision-239/current traces for all primary even states,
   route ladders, medallion order enums, temp resets, puzzle generation,
   cutscene commits, boss stats/mechanics, death, and reclaim behavior.
3. Declare the required carriers and implement the versioned legacy migration,
   compatibility entitlement ledger, exhaustive table tests, and rollback
   guard before enabling native side reads.
4. Repair requirements and real vault/winch/crevice travel; restore symbolic
   Asgarnia/Banikan discovery; implement plaque/statues, Terry, and shared topic
   routers.
5. Build the owned Ancient Guardian and complete cell/totem/altar/golem/search
   war-room sequence from native state.
6. Implement the central medallion/order/placement/recovery ledger and
   player-owned recurring Mysterious Figure before any investigation grants an
   item.
7. Deliver Stranglewood as separate slices: entry/infection, survival,
   ingredients/serum, Vardorvis, Kasonde, medallion/recovery.
8. Deliver the Scar as separate slices: entry/monsters, each randomized
   passage and ship, Leviathan, Persten/medallion, then the partial Scar mine.
9. Deliver Ghorrock as separate slices: item escrow/cell, key prison,
   shortcuts/Jhallan, Duke preparation/fight, medallion/recovery.
10. Deliver Lassar as separate slices: entry/teleporters, device tiers and
    realm/sanity, Silent Choir/icon, Whisperer, medallion/recovery.
11. Build the final prison escrow, Mysterious Figure, medallion placement,
    Mahjarrat/Horn scene, four-wight gauntlet, aftermath, and reconnect-safe
    state 100–114 continuation.
12. Make completion exactly once; implement lamps, ring transformation,
    Mysterious Bandit, charges/tablets/teleports, and journal/debug recovery.
13. Implement post-quest Forgotten Four normal/awakened instances, drops,
    kill counts, ancient rings, Soulreaper assembly, scoreboards, and current
    Demonic Brutus in independently reviewable owners.
14. Run the complete verification matrix from ordinary world entry with two
    players, every four-route permutation, failure/death/logout boundaries,
    and all shared/downstream regressions.

Do not land this as one giant quest patch. Persistence, discovery, war room,
medallion/pursuer, each route, finale, reward services, each replay boss, and
each post-quest economy should be independently reviewable and reversible.

## 17. Verification matrix

| Area | Required evidence |
| --- | --- |
| Discovery/start | Placed door at ordinary world location; all seven prerequisites; six exact base levels; Ancient spellbook; offer/refusal; actual vault entry/exit; symbolic Asgarnia |
| Native state | Every primary 0–118 checkpoint; all route/side carriers declared; alias bits; client transforms; no broad soft-skip ranges; live trace |
| Migration | Every legacy value and item subset/location; side-zero compatibility; repeated login; reward ambiguity ledger; no fabricated kills/order/rewards |
| Vault/Digsite | Five separate inspections; shared Terry; winch/Banikan/pickaxe/mining travel; interruption/reconnect; ordinary Digsite unaffected |
| Guardian/war room | Owned 200-HP fight, shield/melee/rocks, null drops, death/re-entry; four spells/totems, cells, eight-position puzzle, four searches |
| Medallion core | Any 24 route orders; route completion vs item possession; correct four statues; ownership/recovery; recurring pursuer across world/POH/fairy ring/logout; two-player isolation |
| Stranglewood | Barus/note/potion/boat; infection/root; stamina; chest puzzle; three-minute defence; herb/berries/serum; shortcuts; Vardorvis; Kasonde; medallion |
| Scar | Guardian sharing; demon roster; mask/choking; randomized passage order/reset; all nine puzzles; oxygen/items/ships; Leviathan; Persten/debris; medallion |
| Scar mine | Correct partial checkpoint, 64 Mining/Hagus, chunks/store, Ventriculus, pure/extract conversion, coffer prices/capacity, reconnect/economy tests |
| Ghorrock | Exact item/equipment escrow across charges/death/relog; cell steps; four keys/codes; gates/shortcuts; Jhallan survival; Duke ingredients/vats/fight; medallion |
| Lassar | Shared Ramarno; rope/entry; seven teleporters; five keys; every device/schematic/recall; sanity/realm/death; Choir/icon/vision; Whisperer; medallion |
| Final prison | Abduction, second item escrow, hairclip/lock/chest/altar, no teleport, barrier/portal, final Mysterious Figure styles/prayers/orb/death/re-entry |
| Horn/wights | Full scenes/music/actors; exact 100–114 commits; all four owned mechanics; restart on leave/death; simultaneous Persten death; cleanup; two-player isolation |
| Completion | Native 118 once; exactly 5 QP; slot-neutral visibility-ring upgrade to uncharged Shadows; exactly three lamp entitlements; duplicate queue/reconnect proof |
| Lamps/bandit | Seven skill choices at base 60+, 100k once, 0–3 Destroy/reclaim, 75k multi-ring purchase, inventory/bank/worn/full-slot/payment cases |
| Ring | 0–1000 matched charges; refund/overflow; five correct destinations; four tablet unlocks; one charge only on success; Lassar unequip/full-slot; death/PvP |
| Post-quest bosses | Quest/normal/awakened isolation; all mechanics; no quest loot; exact normal/awakened tables; kill counts/tablets/quartz/vestiges/Soulreaper/Virtus/pets/orbs/scoreboards |
| Other unlocks | Ancient ring wear/crafting, Peer/furnace/skill boosts, Soulreaper assembly, Demonic Brutus/potato, no Ruinous Powers grant |
| Shared systems | DT1/DT2 Asgarnia and visibility; Terry/Elissa/Ramarno/Jhallan/Catalytic Guardian; POH/fairy ring/graves; spells/poison/corruption/charges; unrelated content unchanged |
| Journal/debug | Every native checkpoint/recovery/entitlement; no spoilers; coherent reset/setup/advance/complete; no teleport or debug-run accepted as live proof |
| Tooling/live | Quest Helper extraction, loc/var audit, complete DT2 combat manifest, compile/lint, focused route/transaction/instance tests, full quest suite, two-player end-to-end smoke |

## 18. Gate verdict

| Gate | Verdict | Reason |
| --- | --- | --- |
| Gate A — discovery and state reachability | Fail | Door neither validates nor enters; Asgarnia/Banikan/core actors absent; winch/crevice do not travel; four route fields leave placed entries/transforms unusable |
| Gate B — resource and transaction safety | Fail | No item escrow/recovery ledger; medallions duplicate through bank; placement consumes nothing; full inventory permanently loses ring/lamps; lamps unusable |
| Gate C — encounter and multiplayer safety | Fail | Every required instance, route activity, boss mechanic, pursuer, and wight lifecycle is absent; unconditional/shared actors can cross players or bypass content |
| Gate D — completion and integration | Fail | Finale is unreachable/soft-skipped; no idempotence proof; wrong ring form/destination; lamps, tablets, mine, replay bosses, drops, crafting, Brutus, journal, and shared routing are incomplete |

Desert Treasure II remains `audit-pending`. Do not mark it modernized until a
fresh eligible player enters from the ordinary vault door, completes every
native war-room and four-route contract in any order, survives all owned
encounters and recovery boundaries, finishes the real prison/Horn/wight
sequence, receives exactly one complete reward package, and exercises every
current partial- and post-quest unlock without debug state or cross-player
leakage.
