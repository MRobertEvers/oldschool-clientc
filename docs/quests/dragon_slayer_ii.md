# Dragon Slayer II modernization audit

Status: `audit-pending` — the quest has a recognisable 0–215 primary ladder,
four investigation branches, Robert, Vorkath, recruitment, dragon waves,
Galvek, a dynamic journal, and shared completion call. It is not a playable
revision-239 implementation. The start has no requirements, the map and crypt
puzzles are replaced with shortcuts, all four investigation carriers use
values incompatible with their native varbits, several required transitions
are unreachable or unconditional, the fleet battle is narrated, dragon combat
is a shared-coordinate melee approximation, Galvek's phase hazards are text,
and most rewards and permanent unlocks are absent or ungated.

Audited: 2026-08-17

Governing plan: [Quest modernization plan](../QUEST_MODERNIZATION_PLAN.md). This
record applies Gates A–D to requirements, native persistence, every preliminary
investigation, the four dragon-key routes, Lithkren and the Ancient Cavern,
shared monarch dialogue, fleet combat, Galvek, death/recovery, completion,
rewards, post-quest services, migration, debug tooling, and verification. It is
an implementation specification, not evidence that the quest is complete.

## 1. Authoritative references

The current article and quick guide define requirements, route, combat,
recovery, rewards, and unlocks. The transcript defines offer/refusal, re-talk,
item-replacement, alternate-order, funeral, and post-quest dialogue. Revisions
were resolved through the OSRS Wiki API on 2026-08-17.

| Reference | Pinned revision | Use in this audit |
| --- | --- | --- |
| [Dragon Slayer II](https://oldschool.runescape.wiki/w/Dragon_Slayer_II?oldid=15303675) | 15303675, 2026-08-17 | Identity, requirements, complete route, bosses, rewards, unlocks, and history |
| [Dragon Slayer II/Quick guide](https://oldschool.runescape.wiki/w/Dragon_Slayer_II/Quick_guide?oldid=15270142) | 15270142, 2026-07-20 | Exact critical path, materials, checkpoints, recovery, and encounter order |
| [Transcript:Dragon Slayer II](https://oldschool.runescape.wiki/w/Transcript%3ADragon_Slayer_II?oldid=15292952) | 15292952, 2026-08-11 | Offers, shared NPC routing, cutscenes, failure/re-talk paths, funeral, and post-quest dialogue |
| [Alec Kincade](https://oldschool.runescape.wiki/w/Alec_Kincade?oldid=15303665) | 15303665, 2026-08-17 | Start, requirements, completion, and post-quest dialogue |
| [Dallas Jones](https://oldschool.runescape.wiki/w/Dallas_Jones?oldid=15177778) | 15177778, 2026-04-16 | Musa Point, Crandor, Fossil Island, map, Lithkren, and death checkpoints |
| [Jardric](https://oldschool.runescape.wiki/w/Jardric?oldid=15262148) | 15262148, 2026-07-10 | Rowboat construction and Lithkren travel |
| [Bob](https://oldschool.runescape.wiki/w/Bob?oldid=15196223) | 15196223, 2026-04-25 | Dream route, key investigations, recruitment, and sacrifice |
| [Sphinx](https://oldschool.runescape.wiki/w/Sphinx?oldid=15254285) | 15254285, 2026-07-02 | Bob identification and permanent catspeak grant |
| [Oneiromancer](https://oldschool.runescape.wiki/w/Oneiromancer?oldid=15205243) | 15205243, 2026-05-03 | Dream potion, lost-item replacement, and shared quest dialogue |
| [Robert the Strong](https://oldschool.runescape.wiki/w/Robert_the_Strong?oldid=15199387) | 15199387, 2026-04-28 | Pillar fight, prayer deactivation, and memories |
| [Dragon key](https://oldschool.runescape.wiki/w/Dragon_key?oldid=15210415) | 15210415, 2026-05-08 | Four-piece assembly, Ancient Cavern use, and recovery |
| [Dragon key piece](https://oldschool.runescape.wiki/w/Dragon_key_piece?oldid=15188642) | 15188642, 2026-04-23 | Four piece identities and sources |
| [Karamjan Temple](https://oldschool.runescape.wiki/w/Karamjan_Temple?oldid=15229046) | 15229046, 2026-06-08 | Traps, maze, guardians, key plinth, and exit |
| [Locator orb](https://oldschool.runescape.wiki/w/Locator_orb?oldid=15239918) | 15239918, 2026-06-21 | Morytania directions, damage floor, dig site, retention, and replacement |
| [Vorkath](https://oldschool.runescape.wiki/w/Vorkath?oldid=15283387) | 15283387, 2026-07-31 | Quest/post-quest forms, attacks, specials, death, and unlock |
| [Vorkath/Strategies](https://oldschool.runescape.wiki/w/Vorkath/Strategies?oldid=15299164) | 15299164, 2026-08-15 | Protection, equipment, special cycle, and counterplay |
| [Ungael](https://oldschool.runescape.wiki/w/Ungael?oldid=15023397) | 15023397, 2025-07-29 | Torfinn travel, quest instance, Vorkath, and laboratory route |
| [Ungael laboratory](https://oldschool.runescape.wiki/w/Ungael_laboratory?oldid=15076734) | 15076734, 2025-10-19 | Notes, spiders, lever, timed door, and chest |
| [Old notes (Dragon Slayer II)](https://oldschool.runescape.wiki/w/Old_notes_%28Dragon_Slayer_II%29?oldid=15226536) | 15226536, 2026-06-06 | Laboratory story item and replacement |
| [Shayzien Crypts](https://oldschool.runescape.wiki/w/Shayzien_Crypts?oldid=15233580) | 15233580, 2026-06-14 | Three floors, undead, randomized riddle, busts, and reset |
| [Tomb (Dragon Slayer II)](https://oldschool.runescape.wiki/w/Tomb_%28Dragon_Slayer_II%29?oldid=15233573) | 15233573, 2026-06-14 | Amelia's investigation and tomb interaction |
| [Aivas' diary](https://oldschool.runescape.wiki/w/Aivas%27_diary?oldid=15282275) | 15282275, 2026-07-30 | Lithkren discovery item and recovery |
| [Ancient Cavern](https://oldschool.runescape.wiki/w/Ancient_Cavern?oldid=15267032) | 15267032, 2026-07-18 | Entry prerequisite, mithril door, forge, and Lithkren access |
| [Ancient key (Dragon Slayer II)](https://oldschool.runescape.wiki/w/Ancient_key_%28Dragon_Slayer_II%29?oldid=15188641) | 15188641, 2026-04-23 | Laboratory chest, mithril door, and recovery |
| [Lithkren](https://oldschool.runescape.wiki/w/Lithkren?oldid=15267164) | 15267164, 2026-07-18 | Island travel, vault, laboratory, and post-quest access |
| [Galvek](https://oldschool.runescape.wiki/w/Galvek?oldid=15272224) | 15272224, 2026-07-22 | Stats, four phases, ordinary attacks, specials, death, and credit |
| [Dragonfire/Galvek](https://oldschool.runescape.wiki/w/Dragonfire/Galvek?oldid=14924513) | 14924513, 2025-06-23 | Exact dragonfire-protection matrix |
| [Myths' Guild](https://oldschool.runescape.wiki/w/Myths%27_Guild?oldid=15239262) | 15239262, 2026-06-20 | Completion gate, floors, shops, dungeon, altar, and services |
| [Dog (Myths' Guild)](https://oldschool.runescape.wiki/w/Dog_%28Myths%27_Guild%29?oldid=15166131) | 15166131, 2026-04-06 | Permanent-catspeak demonstration |
| [Primula](https://oldschool.runescape.wiki/w/Primula?oldid=15166124) | 15166124, 2026-04-06 | Super-antifire teaching and potion unlock |
| [Ellen](https://oldschool.runescape.wiki/w/Ellen?oldid=15166133) | 15166133, 2026-04-06 | Four selectable 25,000-XP combat rewards |
| [Fountain of Uhld](https://oldschool.runescape.wiki/w/Fountain_of_Uhld?oldid=15166123) | 15166123, 2026-04-06 | Recharging dragonstone jewellery |
| [Pool of Dreams](https://oldschool.runescape.wiki/w/Pool_of_Dreams?oldid=15166117) | 15166117, 2026-04-06 | Robert and Galvek replay access |
| [Super antifire potion](https://oldschool.runescape.wiki/w/Super_antifire_potion?oldid=15293127) | 15293127, 2026-08-11 | Unlock, recipe, XP, and extended form |
| [Ava's assembler](https://oldschool.runescape.wiki/w/Ava%27s_assembler?oldid=15270658) | 15270658, 2026-07-20 | Vorkath-head upgrade contract |
| [Mythical Cape Store](https://oldschool.runescape.wiki/w/Mythical_Cape_Store?oldid=15166105) | 15166105, 2026-04-06 | Cape acquisition and completion gate |
| [Mythical cape](https://oldschool.runescape.wiki/w/Mythical_cape?oldid=15242067) | 15242067, 2026-06-23 | Lithkren teleport and mounted-cape behavior |
| [Wrath Altar](https://oldschool.runescape.wiki/w/Wrath_Altar?oldid=15239062) | 15239062, 2026-06-20 | Completion-gated Runecraft access |
| [Ferocious gloves](https://oldschool.runescape.wiki/w/Ferocious_gloves?oldid=15301367) | 15301367, 2026-08-16 | Hydra-leather crafting unlock |
| [Dragon platebody](https://oldschool.runescape.wiki/w/Dragon_platebody?oldid=15183891) | 15183891, 2026-04-22 | Reforging unlock |
| [Dragon kiteshield](https://oldschool.runescape.wiki/w/Dragon_kiteshield?oldid=15183892) | 15183892, 2026-04-22 | Reforging unlock |
| [Rune dragon](https://oldschool.runescape.wiki/w/Rune_dragon?oldid=15199900) | 15199900, 2026-04-28 | Post-quest vault monster, attacks, and loot |
| [Adamant dragon](https://oldschool.runescape.wiki/w/Adamant_dragon?oldid=15199943) | 15199943, 2026-04-28 | Post-quest vault monster, attacks, and loot |

The article records the February 2023 dialogue/cutscene rewrite in which Bob's
death became an intentional sacrifice, the July 2024 *While Guthix Sleeps*
epilogue, and the November 2022 XP increase. A faithful port must implement the
current route, not preserve dialogue or rewards from the 2018 release build.

Transition aid only: Quest Helper at commit
[`5ea99d5`](https://github.com/Zoinkwiz/quest-helper/tree/5ea99d5ea9ba3fb096ebe7b5ed02d80883e9819d/src/main/java/com/questhelper/helpers/quests/dragonslayerii)
observes primary states 0 through 215, all four native investigation ladders,
24 map-piece bits, map/crypt puzzle state, item alternatives, route coordinates,
and combat checkpoints. `python3 tools/questhelper_extract.py dragonslayerii
--check` resolves the expected `quest_dragonslayer2` dbrow and every referenced
gameval. Quest Helper is a state/test oracle, not server behavior evidence.

## 2. Canonical contract

Dragon Slayer II is a members-only, grandmaster, very long quest released 4
January 2018. It starts with Alec Kincade outside the Myths' Guild. Starting
requires 200 quest points; 75 Magic, 70 Smithing, 68 Mining, 62 Crafting, 60
Agility, 60 Thieving, 50 Construction, and 50 Hitpoints; completion of Legends'
Quest, Dream Mentor, A Tail of Two Cats, Animal Magnetism, Ghosts Ahoy, Bone
Voyage, and Client of Kourend; and enough Barbarian Training progress to enter
the Ancient Cavern. Combat level 100 is recommended.

A canonical run must:

1. validate all hard prerequisites before Alec offers the quest, preserve an
   explicit accept/refuse branch, and route the player through Dallas at Musa
   Point, Crandor, the laboratory, the Spawn, the mural, and Fossil Island;
2. let the player recover and hand in any subset of 24 Fossil Island map
   pieces, then operate the real rotate/drag map puzzle without requiring all
   pieces in inventory at once;
3. build a reusable rowboat with eight oak planks, ten swamp paste, at least 12
   nails of any kind, a hammer, and a saw, including bent-nail behavior and
   two-way/re-entry travel;
4. search Lithkren, recover Aivas' diary, identify Bob, obtain permanent
   catspeak from the Sphinx, make/recover the dream potion, defeat Robert using
   the pillars, and play the memory sequence;
5. run the Karamjan Temple, Morytania locator, Ungael laboratory, and Shayzien
   Crypts investigations in any order, with their real traps, puzzles, combat,
   item recovery, and native checkpoints;
6. use the Ancient key to enter the mithril door, cast Fire Wave or Fire Surge
   on all three dragon heads, forge the four key pieces, open Lithkren's vault,
   and play the Zorgoth/Galvek/Dallas/Jardric sequence;
7. recruit Sir Amik Varze, King Lathas or Thoros, and King Brundt through their
   shared NPC dialogue, then play the Varrock dining and private Bob scenes;
8. launch with Torfinn, survive the four-minute ship-integrity defence, traverse
   the wreckage, fight each canonical allied dragon wave with checkpointed
   recovery, and play Bob's sacrifice;
9. defeat a private 1,200-HP Galvek through four exact phases, support the
   canonical 100,000-coin death-reclaim contract, and count simultaneous death;
   and
10. play Zorgoth's death and the Burthorpe funeral, complete once with Alec,
    award the exact XP/QP, and make every permanent unlock available through
    its real owner.

Completion awards 5 quest points, 80,000 Smithing XP, 60,000 Mining XP, 50,000
Agility XP, and 50,000 Thieving XP. The four additional 25,000-XP combat choices
are a post-quest service from Ellen, not four generic lamps in Alec's completion
inventory grant. Permanent catspeak is granted by the Sphinx during the quest.
Completion opens the Myths' Guild and its services; adamant/rune dragons;
repeat Vorkath and Ava's assembler; super antifire potions; the Wrath Altar;
mythical capes and their mounted teleport; dragon armour reforging; ferocious
gloves; the Lithkren digsite-pendant destination; the Pool of Dreams; and the
documented house and diary consumers.

## 3. Native identity and persistence

| Field | Native value / expected behavior |
| --- | --- |
| Quest metadata ID | 148 |
| Dbrow | `quest_dragonslayer2` |
| Type / difficulty / length | Members; grandmaster; very long |
| Release | 4 January 2018 |
| Start | `alec_kincade` (NPC 7950), coordinate `0_38_44_26_53` |
| Primary | `%ds2`, bits 0–8 of `dragonslayer2_main` |
| Investigation fields | `%ds2_zeah` bits 9–14; `%ds2_karam` 15–19; `%ds2_mory` 20–25; `%ds2_frem` 26–31 |
| Secondary | `dragonslayer2_secondary`: heads, warning, recruits, 24 map pieces, super-antifire unlock |
| Tertiary | `dragonslayer2_tertiary`: locator coordinate, pendant, training, visibility, notes, dialogue, failure counters |
| Crypt plinths | Native fields in `dragonslayer2_plinths` |
| Puzzle scratch | Cache-authored temporary/interface carriers including `xbows_int`, `xbows_obj1`, and `skill_int1` |
| Native end | 215 |

The quest root redeclares `[dragonslayer2_main]` without permitting whole writes
and correctly uses a separate temporary `%ds2_wave_step`. The declaration is
not the main persistence defect. The implementation assigns old condensed
numbers to four native bitfields whose cache and helper consumers expect
different values.

### 3.1 Primary ladder

The local constants largely use real top-level milestones, but collapse entire
subranges and write several states back-to-back. Missing values are meaningful
canonical re-talk, scene, or recovery points; they must not be skipped merely
because the endpoints exist.

| `%ds2` | Canonical checkpoint | Current local result |
| ---: | --- | --- |
| 0 | Alec; not started | No requirement gate; immediate start |
| 5 | Dallas at Musa Point | Present but all Dallas variants share routing |
| 10 | Dallas on Crandor | Present, abbreviated |
| 15–16 | Enter/investigate mine wall | Inspect exists; Enter does not move player |
| 17–21 | Laboratory/mural sequence | Collapsed |
| 22–24 | Spawn encounter | Can be bypassed by clicking mural twice |
| 25 | Reinspect mural | Collapsed |
| 30 | Return to Dallas | Present |
| 35 | Dallas on Fossil Island | Present |
| 40 | Collect/hand in map pieces | Requires all 24 in inventory |
| 45 | Operate map puzzle | Puzzle omitted |
| 50 | Solved map / Dallas | Written immediately after 45 |
| 55 | Jardric | Present |
| 60–64 | Construct rowboat | Single all-at-once recipe |
| 65 | Board/travel | First outbound use only |
| 70–75 | Lithkren descent/search | Collapsed/ungated shared routes |
| 80–84 | Diary investigation | Collapsed |
| 85 | Dallas after diary | Present |
| 90 | Bob | Present across every Bob variant |
| 95–96 | Sphinx | One-line checkpoint; permanent catspeak absent |
| 100 | Oneiromancer | Present, incomplete recovery |
| 105 | Enter dream | Copied Dream Mentor arena |
| 110–111 | Bob/Robert dream | 110 omitted; Robert approximation at 111 |
| 115–120 | Bob/key-investigation branches | Collapsed to one hub |
| 125–129 | Ancient Cavern/forge route | Collapsed |
| 130 | Key forged | Present |
| 135–145 | Lithkren door/release | Immediately cascades to 150 |
| 150 | Return after release | Present without scenes |
| 155 | King Roald | Present |
| 160–161 | Recruits/dining | Shared recruits exist; dining narrated |
| 165 | Private Bob scene | State written immediately after dining text |
| 170 | Sail to Ungael | Starts from wrong owner |
| 175 | Ship defence | Narrated; immediately writes 180 |
| 180–184 | Wreck traversal | Omitted |
| 185–196 | Dragon waves | Sequential shared spawns with temp substep |
| 200–201 | Galvek | Approximate shared fight |
| 205–210 | Aftermath/funeral/Alec | Funeral omitted; 210 unused |
| 215 | Complete | Shared completion is called, but reward/unlock transaction is wrong |

### 3.2 Native side ladders

| Route | Native checkpoints | Local constants / writes | Consequence |
| --- | --- | --- | --- |
| Karamja | Key held/completed at `%ds2_karam >= 20` | 0→1→2→3, done 4 | Native transforms, helper, re-talk, and recovery never see completion |
| Morytania | Reldo 10, census 15, hand-in 20, follow-up 25, Sarah 35, Ava 40, orb delivered 46, Ava follow-up 50, key 55 | inert 1, active 2, done 3 | Entire investigation is replaced and every native consumer is incompatible |
| Kourend | Amelia 20, tomb inspected 25, opened 30, key 35 | clues 1, done 2 | Crypt visibility and key ownership cannot reconcile |
| Fremennik | Brundt 10, Vorkath 30, key 35, mithril door 40 | Vorkath dead 1, done 2 | Torfinn/lab/door route and native transforms never see their states |

`dragonslayer2_secondary` also owns the three dragon-head bits, Ungael warning,
the Lathas/Brundt/Amik recruitment bits, all 24 map-piece hand-in bits, and
`super_antifire_unlocked`. The local root uses the three recruitment fields but
does not use the warning, map, or potion fields. `dragonslayer2_tertiary` owns
the real Morytania coordinate, Lithkren-pendant redirect, combat-training,
visibility, diary, notes, optional books, Bob dialogue, and fleet-failure data;
the local quest ignores nearly all of it.

### 3.3 Save migration

Migration must run before modern handlers interpret any investigation field.
Add an explicit version marker and migrate only saves identified as belonging
to this local condensed implementation. Snapshot the primary, four side fields,
all relevant secondary/tertiary bits, puzzle/plinth state, and quest-item
ownership across inventory, bank, reclaim, and any private ground owner.

The high-confidence terminal conversions are Karamja local 4→native 20,
Morytania 3→55, Kourend 2→35, and Fremennik 2→35. Earlier local values are not
universally equivalent:

- Karamja 1–3 record a non-canonical guardian counter; use key ownership and
  route telemetry, otherwise restart the route safely rather than invent 20.
- Morytania 1 likely means the inert-orb checkpoint around native 40–45, 2 is
  closest to Ava's post-activation checkpoint 50, and 3 means key 55. Confirm
  the 1 boundary with a live legacy save before shipping.
- Kourend 1 is closest to tomb-inspected state 25 and 2 to key state 35, but a
  save affected by the full-inventory bug may have state 1 without any busts;
  recovery must reconstruct the playable checkpoint rather than mark solved.
- Fremennik 1 is closest to Vorkath-dead state 30 and 2 to key state 35. Do not
  infer mithril-door state 40 until the Ancient key was actually used.

Preserve native primary milestones rather than remapping them blindly. Use
owned or safely consumed map pieces to populate the 24 hand-in bits only when
the primary and item ledger corroborate collection. Primary 125 or later may
corroborate four key pieces, but it does not prove optional notes/books.
Primary 215 can establish the completion-dependent unlock baseline, including
`super_antifire_unlocked`, only after the one-time reward ledger is reconciled.

The migration must be monotonic and idempotent, preserve unrelated bits, never
duplicate items or XP, log ambiguous saves for review, and pass a matrix over
every old primary/side value, item location, inventory capacity, relog, and
repeated migration. Remove every condensed constant/write after rollout.

## 4. Implementation and ownership surface

The quest root has 1,701 lines: one 1,556-line RuneScript file and two config
files. The seven principal shared owners add 1,322 lines, for 3,023 directly
audited lines before generic maplinks, combat, death, shops, Construction,
Runecraft, Herblore, catspeak, diaries, and downstream quests.

| Surface | Current responsibility | Audit result |
| --- | --- | --- |
| `dragonslayer2.constant` | Primary aliases, condensed side states, approximate coordinates/hits | Primary endpoints useful; side values incompatible; comments explicitly disclose shortcuts |
| `dragonslayer2.varp` | Main carrier declaration and temp wave counter | Temp counter cannot recover a persistent wave chapter |
| `dragonslayer2.rs2` | Nearly the entire quest | Monolithic, broad triggers, unsafe ownership, abbreviated route, and missing real mechanics |
| `king_roald.rs2` | Start recruitment | Native recruit bit, but abbreviated shared routing |
| `sir_amik_varze.rs2` | Asgarnia recruit | Native bit; dialogue precedence needs cross-quest testing |
| `king_lathas.rs2` / Thoros | Kandarin recruit | Native bit; complex shared state and ruler substitution |
| `viking_brundt.rs2` | Fremennik recruit | Native bit; shared quest routing |
| `anma.rs2` | Ava/Morytania splice | Condensed orb branch replaces the real investigation |
| `minigame_vorkath` | Ungael travel/post-quest boss | Quest and repeat boss are stubs; loot/reclaim deferred |
| `minigame_lithkren` | Vault travel/content | Ungated routes; adamant/rune combat and counters deferred |
| Dream Mentor potion code | Shared dream-potion recipe | Reused without complete DS2 ownership/recovery lifecycle |
| shops, Construction, Runecraft, ranged, catspeak | Permanent consumers | Isolated assets/functions exist; most DS2 gates or owner services do not |

The cache surface is substantially richer than the script: at least 99
DS2/Galvek NPC entries, 617 locations, 58 objects, 92 sequences, 12 spotanims,
and more than a hundred named state fields are present. Ship decks, damage and
repair assets, fleet/cutscene actors, map and crypt puzzle state, Galvek phase
variants, tomb/Lithkren geometry, and numerous scene variants should be bound by
symbolic gameval. Before adding an engine primitive, prove this existing native
surface and the established instance/queue/interface APIs cannot express the
required behavior.

## 5. Start, Dallas, Crandor, and the Spawn

`[opnpc1,alec_kincade]` immediately begins and writes state 5. It checks none
of the eight skills, 200 QP, seven quests, membership, or Ancient Cavern access,
and offers no accept/refuse branch. Post-quest Alec has only generic welcome
text. Modern Alec must use native dbrow requirements where possible, report
every failed hard gate accurately, distinguish recommended combat, and commit
the start transition only after explicit acceptance.

All Dallas variants share one broad handler and state switch. A player who can
reach a Dallas in the wrong place can advance a different chapter. Split or
validate each variant by exact primary range, map/instance, and expected actor.
Implement transcript re-talks and scene ownership rather than relying on the
current short exposition.

The mine-wall Enter option only displays a message; it does not move the
player, making the normal route a hard blocker. The mural advances 17→22 on
first inspection but does not create a player-owned Spawn. A second inspection
advances 22→25 without requiring the Spawn to have appeared or died. The
Spawn's death queue also lacks expected-state and owner validation. Bind the
real tunnel/laboratory geometry, scenes, transform sequence, private Spawn,
kill credit, death/re-entry, and mural checkpoint. No click should progress
while the expected encounter is alive or absent for the wrong reason.

## 6. Fossil Island map puzzle

Five local gathering handlers grant representative bundles of the 24 pieces.
Their ownership checks are inventory-only: banking or losing one representative
allows the entire bundle to be granted again. The script requires all 24
pieces simultaneously, deletes them, never records the 24 native hand-in bits,
and immediately narrates states 40→45→50. The actual map interface and its
native drag/rotation state are unused.

Modernization must:

1. define a single ownership ledger for every piece across inventory, bank,
   Dallas custody, private ground/reclaim, and active grant transaction;
2. make each source idempotent and preserve its canonical gather interaction;
3. allow pieces to be handed to Dallas in any order and write the exact native
   bit only after accepted custody;
4. recover held or handed-in pieces according to the transcript without
   duplicating either representation;
5. open the cache-authored map interface, initialise its native positions and
   rotations, arm/re-arm every operation, validate server-side completion, and
   preserve or intentionally reset scratch state across close/reopen/logout;
6. consume/reconcile only the pieces Dallas owns when the solved transition is
   atomically committed; and
7. test all 2^24 logical ownership patterns through generated/property tests,
   with focused UI tests for rotations, dragging, close/reopen, invalid layouts,
   repeated submit, full inventory, and reconnect.

The generic Destroy handler currently drops these Destroy-labelled pieces into
the public world. Quest-specific Destroy/replacement behavior must replace it.

## 7. Jardric, the rowboat, and first Lithkren visit

The rowboat recipe incorrectly binds `nails` to steel nails, while the current
route accepts any nail type. It always consumes exactly 12 and has no bending.
Material removal is a sequence of unchecked deletes rather than a reserved
transaction, and construction scenes/stages 60–64 are collapsed. Implement the
real recipe, tool checks, nail-selection policy, bent-nail loop, animations,
capacity/busy handling, and commit-on-success semantics.

The built boat's first use at state 65 teleports out and writes 70. At state 70
or later it only says “Your rowboat,” so ordinary return/re-entry is impossible.
Make both directions and every valid recovery checkpoint explicit. Shared
Lithkren stairs, vault, and barrier handlers are currently unconditional stubs;
gate them by primary/door/completion state, map context, and instance ownership.

Aivas' diary is granted based on inventory only, so banking it allows duplicates.
Add study text, owned-anywhere detection, canonical loss/replacement, Dallas
hand-off/re-talk, and safe full-inventory behavior. The post-quest world must
remain reachable after the quest without reopening quest-only scenes.

## 8. Bob, the Sphinx, the dream, and Robert

The Bob trigger is installed on many Bob variants and can progress the quest
without proving the correct location or form. It does not enforce the enchanted
catspeak amulet route or real locator interaction. The Sphinx is reduced to one
line and a state increment; it never grants permanent catspeak. Consequently,
other local content still explicitly requires a worn amulet and even comments
that the DS2 exception is missing.

The Oneiromancer and potion path inspects only inventory forms and reuses Dream
Mentor logic without a complete DS2 item lifecycle. Modern routing must coexist
with Lunar Diplomacy and Dream Mentor, recognize banked/ground/reclaim states,
avoid duplicate potion ingredients/forms, and return the player to the correct
DS2 checkpoint after interruption.

Robert is fought in a copied Dream Mentor arena. The special decides that the
player is “hiding” when their distance from Robert is at least three; it does
not use pillar collision or line of sight, still deals reduced damage, and does
not deactivate prayers. Implement the real dream map and pillars, telegraph,
LOS-safe resolution, Prayer deactivation, styles/stats, kill ownership,
double-KO behavior, leave/death/re-entry, and reconnect. Then play the Not Bob
and current memory sequence before returning to the key hub. `npc_findhero`
must not be the authority for quest credit.

Permanent catspeak is a durable mid-quest unlock, not a completion reward.
Expose a shared `can_understand_cats` predicate used by Bob, the Myths' Guild
dog, Ratcatchers, Wintertodt helpers, and future cat dialogue; its truth sources
are a valid catspeak item or the native DS2 unlock state.

## 9. Four-key hub and global item ownership

The local Bob hub teleports directly to Karamja and Shayzien, bypassing travel
and story, and checks key ownership only in inventory. It needs a route-aware
menu driven by the native side ladders, current item ledger, and transcript
re-talks. All four investigations must remain completable in any order.

Create shared predicates and atomic operations for:

- each dragon key piece;
- the complete dragon key;
- the Ancient key;
- locator orb forms;
- four crypt busts;
- Aivas' diary and old notes; and
- every map piece.

“Owned” must include inventory, bank, legitimate NPC custody, private reclaim,
and an in-flight protected grant. A quest state may advance after an item grant
only when the grant succeeds or ownership already exists. Destruction, death,
banking, logout, and repeated dialogue must converge on exactly one recoverable
copy. Generic public ground drops are not valid recovery for Destroy items.

## 10. Karamjan Temple investigation

Temple entrance and stairs are not primary-stage gated. Agility and Thieving
traps use an approximate threshold of 30 rather than the required level/formula
and their handlers do not perform traversal. A failed Thieving trap calls
`npc_add` with `ds2_maze_skeleton`, which is a location, not an NPC. Guardian
combat is approximate, uses shared-world additions/deletions, and increments
the side field on any found hero's kill. The key is incorrectly locked behind
killing all three guardians; canonically it can be taken from the centre
without doing so.

Bind the real maze and trap endpoints, formulas, animations, damage, failure
placement, and NPC definitions. Instances must be private, cleanup-safe, and
reconstructable after logout. Guardians should exist and fight canonically but
must not become a fabricated key requirement. The plinth must gate on the
native route, grant/recover one Karamjan piece atomically, and commit native
state 20 only with ownership/custody evidence.

## 11. Morytania locator investigation

The current branch omits Reldo, the red book/census, Sarah, the ghost trail, and
all native values 10–55. It creates an inert/active orb and uses a random 1–10
“warmth” counter; repeated lucky uses eventually add the key directly. There
is no persistent one-of-24 coordinate, compass-direction response, spade, dig,
or position validation. The key add is unchecked and the route is marked done
even when a full inventory prevents delivery, producing an unrecoverable save.

Implement the complete shared-NPC chain and write each native checkpoint only
after its dialogue/item transaction commits. Select and persist the native
`ds2_mory_coord` once per route; calculate the locator direction/distance from
that coordinate; apply canonical damage without letting the orb directly kill
the player; and require a spade/use at the correct tile. Delivery must free or
reserve a slot, add/recover exactly one Morytania key piece, and then write 55.
Test all 24 coordinates, boundaries, direction wording, teleport/relogin,
inventory/full-hitpoint edge cases, and every shared Reldo/Sarah/Ava precedence
combination.

## 12. Fremennik, Ungael, and Vorkath investigation

The local quest skips King Brundt's permission, Torfinn, Ungael exploration,
the laboratory, notes, spiders, lever, timed door, and chest. It teleports to a
global non-instance Vorkath arena. Quest Vorkath is primarily melee with one
spawn every six attacks; it lacks the standard ranged/magic/dragonfire/venom,
prayer-disabling, fireball, acid, freeze, and Crumble Undead interactions.

On Vorkath's death the script tries to add both the dragon key piece and Ancient
key only when two spaces exist, but advances and teleports regardless. A full
inventory permanently loses both. Replace this with:

1. Brundt and Torfinn dialogue at native 10 and private Ungael travel;
2. a quest Vorkath instance with exact 460-HP stats, attack selection,
   protection matrix, specials, spawn/Crumble behavior, venom/immunities,
   wake/sleep, kill credit, leave, death, logout, and re-entry;
3. native Vorkath-dead state 30 only after the owned boss dies;
4. laboratory exploration, optional old notes, spiders, lever, 15% run/timed
   door behavior, failure reset, and private loc transforms;
5. an atomic chest grant of both keys and native piece state 35, with recovery
   if either item is already owned or space is insufficient; and
6. Ancient-key use on the mithril door to native state 40.

Post-quest Vorkath is a separate stronger repeatable encounter with loot,
personal-best/kills, death storage, and 100,000-coin reclaim. The shared
minigame file explicitly defers important parts of that contract; do not treat
the quest boss rewrite as completing the repeatable boss.

## 13. Kourend and Shayzien Crypts investigation

The current route skips Veos/archive context, the historians, and Amelia, then
teleports directly into a crypt. Tomb entry is unconditional. First inspection
writes the clue state even if fewer than four inventory slots exist and grants
no busts; later interactions have no regrant branch, so the player becomes
stuck. The puzzle has one fixed selection rather than a per-player randomized
riddle and individual bust placement. Wrong answers do not reset, teleport, or
randomize.

Implement the required Amelia route and native values 20, 25, 30, and 35. Bind
the full three-floor private crypt, canonical undead encounters, inspections,
four-bust ownership ledger, native plinth transforms, and an instance-seeded
solution. Each bust must be placed/removed independently; submitting a wrong
layout must clear/reset as canonically defined, return the player, and choose a
new solution. Correct submission grants/reconciles exactly one Kourend piece
before writing 35. Test every solution/permutation, partial placement, lost
bust, full inventory, death, logout, duplicate submit, and two concurrent
players with different layouts.

## 14. Ancient Cavern forge and Lithkren vault

The mithril door only shows a message. The three dragon heads use Talk-to and
delete four fire and four air runes rather than accepting an actual Fire Wave
or Fire Surge cast. This ignores blood/wrath runes, elemental staffs, rune
pouches, selected spell, spell accuracy, and normal magic semantics. The forge
then jumps 125→130→135→150, omitting door use, states 140–145, and every
Zorgoth/Galvek/Dallas/Jardric scene.

Modernization must share the magic subsystem's normal cast validation and
rune-consumption transaction, including staffs and rune pouch, while targeting
the exact native head. Record each of the three native head bits only after a
successful qualifying cast. The forge should accept one of each owned piece,
reserve the result, consume exactly those pieces, create/recover one dragon key,
and commit state 130 atomically.

Use the key on the real Lithkren door, preserve it according to the canonical
item contract, and implement all 135–150 scenes with private actors and safe
resume points. Dallas's death and Jardric's escape must be world-state driven,
not a text summary. Gate the post-quest vault and Lithkren destinations
separately from quest-only scenes.

## 15. A World United and shared NPC routing

King Roald, Sir Amik Varze, King Lathas/Thoros, and King Brundt have DS2 branches
and write the native recruitment bits. This is the strongest existing part of
the chapter, but each is a high-contention shared NPC. Verify routing against
Garden of Tranquillity, Defender of Varrock, Shield of Arrav, Black Knights'
Fortress, Regicide, Making History, Song of the Elves, and every Fremennik quest
that uses Brundt. Test every Cartesian boundary rather than only a clean DS2
save.

The local Roald branch narrates the dining scene and writes 161, then narrates
the private Bob scene and writes 165. Bind the real Varrock actors, seating,
dialogue, current cutscene, abort/re-entry checkpoints, and Bob follow-up.
`ds2_ungael_warning` is currently unused; route it through the canonical warning
without making repeated dialogue destructive.

## 16. Fleet defence, wreck traversal, and dragon waves

The assault starts from the Ungael boat instead of Torfinn. State 170→175→180
is advanced by narration, so the four-minute integrity minigame is absent. The
cache already contains ship supplies, fires, leaks, mast and damage states,
actors, and deck geometry. Build a private encounter that models all four
repair/supply actions, integrity/time, attacks, failure, death, logout, restart,
and successful handoff. Keep authoritative timers and ownership server-side.

The wreck traversal and its Agility failures are omitted. Bind the shipwreck
routes, allies, red/iron/brutal-green encounter, and the native fleet-failure
counter. Then implement the combat chapters:

| Chapter | Canonical encounter | Required checkpoint behavior |
| --- | --- | --- |
| Part II | Allied fights against red, iron, and brutal green | Traverse/fight on wreckage; recover at chapter boundary |
| Part III | Two green and two blue dragons | Correct simultaneous aggression and guard/allied behavior |
| Part IV-A | Black, steel, and brutal red | Play current Bob sacrifice after valid completion |
| Part IV-B | Mithril, adamant, and rune | Exact metal-dragon specials and Galvek fireballs; preserve checkpoint |

The current `%ds2_wave_step` is temporary while the primary remains at a broad
chapter value. Logout/restart therefore forgets which of 13 sequential dragons
was active and can replay the wrong creature against a later checkpoint. All
dragons spawn at shared player coordinates and are melee stand-ins; simultaneous
waves become sequential. Any death queue uses `npc_findhero` and can advance a
nearby player without validating quest state, expected wave, or owner.

Use a player-owned instance and a persistent canonical chapter checkpoint plus
reconstructable ephemeral encounter state. Each dragon needs its actual
style/dragonfire/special/weakness behavior, position, allies, target policy,
kill-credit token, and cleanup. A late queue from an old instance must be unable
to mutate a new run. Implement the 100,000-coin item reclaim and verify every
checkpoint after death, disconnect, teleport, simultaneous kills, and server
restart.

## 17. Galvek

The local Galvek uses the four cache-native 1,200-HP variants and 900/600/300
phase thresholds, but little else is canonical. It is globally spawned on the
Vorkath exterior. Ordinary attacks are approximate magic/ranged hits with
hand-picked max hits; protection prayers appear to reduce them to zero. Melee,
the full dragonfire matrix, purple prayer-deactivation projectile, weaknesses,
immunities, and exclusions are absent.

The fireball special measures distance from Galvek rather than displacement
from the targeted tile. At normal arena range the player will commonly receive
a reduced hit without moving; a true dodge still takes damage. Canonically a
direct hit can deal up to 115, moving one tile halves it, and moving at least
two tiles avoids it. The four phase mechanics are narration only:

| HP | Phase | Missing canonical mechanic |
| ---: | --- | --- |
| 1,200–901 | Fire | Fire traps and teleport positioning |
| 900–601 | Wind | Hurricane, stat/run-energy drain, and ranged style |
| 600–301 | Water | Tsunami gap plus follow-up fireball |
| 300–0 | Earth | Entombing earth attack and breakout/death resolution |

Create a private Galvek instance with exact spawn, arena clipping, phase
transforms, standard/special attack scheduler, current protection and antifire
matrix, purple projectile, target-tile fireball, trap/hurricane/tsunami/earth
objects or projectiles, animation timing, immunities, damage credit, and safe
cleanup. Use symbolic cache assets and shared combat APIs. Hard-coded hits are
acceptable only if they are canonical constants with pinned provenance, not
admitted guesses.

There are no mid-Galvek checkpoints. Leaving, death, or disconnect must reset
the boss while preserving only the chapter entry state. The 100,000-coin item
reclaim must be atomic and cannot delete a later death's items. A simultaneous
player/Galvek death counts as victory; stale damage and queue callbacks must
not award another player or complete twice. On valid credit, play Zorgoth's
death and advance to the funeral/after-action sequence, not directly to Alec.

## 18. Completion, rewards, and unlock ownership

At state 205 Alec calls `~ds2_quest_complete`. That procedure writes 215 before
all grants are secured. It gives four `thosf_reward_lamp` items, a generic
antique-lamp object shared by other quests for which no use handler exists, and
only if four slots are already free. These lamps are neither usable nor the
canonical reward. The locator orb is also inventory-only and silently omitted
when full. The shared completion procedure correctly derives 5 QP, but the
overall transaction is not retry-safe.

Completion should atomically ledger and award the four fixed XP rewards, mark
the native end state, QP/completed-count/UI/jingle, and activate completion
unlocks exactly once. It should not grant Ellen's four combat choices. Ellen
must persist four unclaimed 25,000-XP choices and let the player select the
canonical combat skills one at a time, with busy/logout/repeat protection.

| Unlock / owner | Current local evidence | Required modernization |
| --- | --- | --- |
| Myths' Guild entrances | Bridge/dungeon/cave configs exist; no DS2 gate handlers found | Gate exterior entry at 215; keep exits/recovery safe |
| Primula / super antifire | Potion rows exist; no owner dialogue gate; `super_antifire_unlocked` never set | Teach once, set native bit, gate normal/extended brewing |
| Ellen XP | NPC exists; no service | Four durable, selectable 25k combat rewards |
| Dog / permanent catspeak | No dialogue; other content checks amulets only | Use shared mid-quest permanent-catspeak predicate |
| Fountain of Uhld | Asset/service not established | Implement canonical jewellery recharge |
| Pool of Dreams | No handler found | Replay Robert and Galvek safely without quest rewards |
| Guild shops | Armoury/weaponry/herbalist open unconditionally if reachable | Rely on correctly gated guild access and guard alternate reachability |
| Wrath Altar | `wrath_altar` loc exists; no access handler/gate found | Completion-gated route and normal Runecraft integration |
| Mythical Cape Store | Acquisition owner absent | Completion-gated purchase and stock behavior |
| Mythical cape | Mounted-cape teleport exists | Implement acquisition, wearable teleport, build/unbuild item safety, Lithkren destination |
| Ava's assembler | Ammo-save behavior exists; Ava upgrade absent | Vorkath-head upgrade, item/cost/recovery transaction |
| Ferocious gloves | Item exists; no Hydra-leather craft found | Completion-gated machinery interaction and Crafting transaction |
| Dragon platebody/kiteshield | Items exist; reforge service absent | Completion-gated forge, pieces/cost/Smithing semantics |
| Adamant/rune dragons | Lithkren mural says combat/counters deferred | Full monsters, private/shared area policy, loot, Slayer/kill counters |
| Repeat Vorkath | Stub and loot/reclaim deferred | Full repeat encounter, loot, head, records, reclaim |
| Digsite pendant redirect | Native tertiary bit unused | Unlock/set destination through canonical interaction |
| POH Vorkath topiary | Generic code requires `%total_vorkath_kills` | Ensure quest/repeat kill ownership increments the correct counter once |
| POH rune-dragon guardian | Assets/consumer not proven complete | Audit build requirement, completion gate, behavior, save persistence |
| Locator orb | Completion grant can silently fail | Owned-anywhere recovery and deliberate retention contract |
| While Guthix Sleeps epilogue | Absent | Add current conditional post-quest scene without changing DS2 rewards |

Each unlock needs its own owner and regression test. Setting `%ds2=215` is not a
substitute for service code, and an interior shop cannot be considered gated
until every entrance, teleport, shortcut, and debug-accessible route has been
audited.

## 19. Item, death, and recovery matrix

Map pieces, Aivas' diary, locator orb, four busts, four dragon-key pieces, the
complete dragon key, and Ancient key all advertise Destroy, but no quest-specific
Destroy handler exists. The generic `[opheld5,_]` route publicly drops the item.
Replace it with transcript-accurate destruction and owner-based replacement.

| Item/state | Normal owner/recovery | Required failure behavior |
| --- | --- | --- |
| 24 map pieces | Source or Dallas custody | No bundle duplication; partial hand-in survives relog |
| Aivas' diary | Lithkren search/Dallas route | Recover if lost; bank does not duplicate |
| Dream potion/forms | Oneiromancer/shared recipe | Resume correct form; never collide with Dream Mentor state |
| Four busts | Crypt investigation/instance | Reissue missing set members; preserve/reset plinths consistently |
| Locator orb | Ava/retained post-quest | Cannot directly kill; bank-aware replacement; no silent full-inventory loss |
| Four key pieces | Four investigations | One each across all stores; investigation state and ownership reconcile |
| Dragon key | Ancient Cavern forge | Atomic consume/create; canonical recovery before/after door |
| Ancient key | Ungael chest | Recover independently of Fremennik piece; door use writes native 40 |
| Old notes | Ungael lab | Optional ownership must not block required chest route |
| Quest death storage | Torfinn/encounter owner | Exact 100k reclaim, no cross-death overwrite or public leakage |
| Locator orb at completion | Retained utility item | Completion cannot become unrewardable because inventory is full |

Every remove/add pair should use a result-checked transaction or reserve the
destination before consumption. State transition, item custody, XP ledger, and
scene checkpoint must commit in an order that makes retries harmless.

## 20. Journal, debug, and observability

The dynamic journal uses the modern shared journal entry point but offers only
five broad chapter hints. It omits requirements, side-route state, map-piece
custody, puzzle status, lost-item recovery, rowboat/Lithkren access, key order,
boss checkpoints, death reclaim, Ellen rewards, and exact current objective.
Render from native state and the ownership ledger; avoid inventory-only prose
that lies when an item is banked or held by Dallas.

`::ds2run` overwrites the primary through the entire route and awards completion
without exercising requirements, side fields, items, instances, puzzles,
combat, scenes, or unlocks. The generic quest cheat also sets 215 without
establishing the native permanent state. Keep developer conveniences separate:

- a completion-state constructor may populate the canonical native end-state
  and permanent unlock baseline for test setup;
- an end-to-end runner must invoke real triggers and assert every transition;
- boss/scene fixtures must create private, owned test instances; and
- cheats must never be cited as player-path verification.

Add structured telemetry for migration ambiguity, rejected cross-owner kill
credit, failed item/reward grants, instance reconstruction, reclaim transactions,
and impossible state/item combinations. Do not log ordinary player secrets or
make telemetry itself a progression dependency.

## 21. Modernization delivery sequence

Implement in dependency order so later chapters build on proven shared
contracts:

1. **Native-state and ownership foundation.** Add the versioned migration,
   remove condensed side constants, establish item/custody/reward ledgers, and
   write invariant/property tests before changing visible quest flow.
2. **Start through map.** Gate Alec, split Dallas contexts, implement Crandor
   tunnel/lab/Spawn, bind all 24 map sources/hand-ins, and mount the real map
   interface.
3. **Rowboat through Robert.** Implement transactional construction and travel,
   Lithkren diary/recovery, permanent catspeak, dream item flow, private Robert,
   and the current memory scene.
4. **Four investigations.** Build each as an independently replayable native
   sub-state machine. Land shared NPC changes with their cross-quest matrices.
5. **Forge and reveal.** Integrate normal spell casting, atomic key forging,
   native door/vault transforms, and all reveal/death/escape scenes.
6. **World United.** Finish shared recruit dialogue, real dining/private scenes,
   warnings, and scene resume points.
7. **Fleet and waves.** Implement the ship-integrity instance, wreck traversal,
   allied combat, exact dragon waves, checkpoints, and death storage.
8. **Galvek and aftermath.** Implement the four-phase private boss, simultaneous
   death, Zorgoth, funeral, Alec turn-in, and current epilogue.
9. **Rewards and unlocks.** Make completion atomic, implement Ellen and every
   downstream owner, and audit all alternate access paths.
10. **Hardening.** Run static, pack, transition, concurrency, reconnect, death,
    UI, real-client, and idempotence gates; remove obsolete shortcuts only after
    migrated saves pass.

Do not split by arbitrary line count. The stable boundaries are map puzzle,
item ledger, dream instance, each investigation, spell/forge, shared monarch
routing, fleet instance, dragon combat, Galvek, and post-quest services.

## 22. Verification matrix

### Static and pack checks

- `python3 tools/questhelper_extract.py dragonslayerii --check` remains clean.
- Quest manifest, journal, start, completion, cheat, shared-variable owners, and
  unlock consumers all reference `quest_dragonslayer2` consistently.
- No condensed writes 1–4 remain in the native investigation fields.
- No required handler is a message-only soft-skip; every cache symbol resolves.
- No quest Destroy item falls through to generic public drop.
- `make -C src mock230-scripts` and `mock230_pack --check-only` pass against the
  intended cache.

### State and migration tests

- Every primary checkpoint 0–215 and every native side checkpoint renders the
  correct world, actor, journal, re-talk, and recovery route.
- Every legacy side value migrates once; ambiguous cases are playable and
  logged; unrelated bits remain byte-for-byte unchanged.
- All 24 map bits, four key-route orders, 24 locator coordinates, crypt
  solutions, recruit-order permutations, and item-location combinations pass.
- Repeating any dialogue, loc operation, queue callback, migration, or
  completion is idempotent.

### Transaction and recovery tests

- Zero through required free slots for every grant, reward, forge, chest, bust,
  and construction action.
- Inventory, bank, legitimate NPC custody, private ground/reclaim, destroyed,
  and in-flight ownership for every quest item.
- Logout/reconnect, teleport, death, double click, modal close, instance expiry,
  and server restart at every scene, puzzle, construction, and combat boundary.
- Simultaneous players cannot see, damage, solve, delete, or receive credit for
  one another's Spawn, crypt, Vorkath, waves, Galvek, drops, or loc transforms.

### Combat tests

- Robert pillar LOS, prayer deactivation, death, re-entry, and double KO.
- Quest Vorkath's full attack/special cycle, protections, spawn/Crumble, acid,
  freeze, kill ownership, and laboratory handoff.
- Every dragon type's attack styles, dragonfire, specials, weaknesses, allied
  targeting, simultaneous groups, chapter checkpoints, and Galvek support fire.
- Galvek's exact four thresholds, every standard/special attack, target-tile
  fireball distances 0/1/2+, traps, hurricane drain, tsunami gaps, entombment,
  protection matrix, phase transition, death reset, and simultaneous kill.
- Torfinn reclaim at 0, 99,999, 100,000, and greater coins; repeat reclaim and
  later-death isolation.

### Reward and unlock tests

- Completion grants exactly 5 QP and 80k/60k/50k/50k XP once, with a full
  inventory and after reconnect at every transaction boundary.
- Ellen grants four and only four selectable 25k combat rewards across arbitrary
  order, logout, repeated talk, and maximum-XP edge cases.
- Permanent catspeak works immediately after the Sphinx, with and without an
  amulet, across all shared consumers.
- Every guild entrance/exit, shop, Primula, Fountain, Pool, Wrath Altar, cape,
  pendant, assembler, forge/crafting unlock, dragon, Vorkath, POH consumer, and
  epilogue is unavailable before its canonical state and usable afterward.
- `::complete quest_dragonslayer2` twice produces the same permanent state and
  never repeats XP, QP, items, lamps, or unlock side effects.

### Real-client evidence

Capture Alec requirement/offer panels, Crandor scenes, map puzzle operations,
rowboat stages, Lithkren transforms, permanent-catspeak dialogue, Robert's
special, each investigation's puzzle and recovery, spell-on-head casts, forge
and reveal scenes, all shared monarch conversations, dining/Bob scenes, ship
integrity UI, wreck/waves, every Galvek phase, death reclaim, funeral,
completion scroll, Ellen choices, and each permanent unlock. For every mounted
interface, capture initial variables, armed operations, close/reopen, resize,
and remount behavior.

## 23. Gate disposition

| Gate | Current disposition | Exit evidence required |
| --- | --- | --- |
| A — discover | **Audit complete; implementation fails** | Keep surface/manifest generated and resolve any newly discovered external owner |
| B — modern engine | **Fail** | Native side fields, real interfaces/instances/transforms, owned queues/combat, no shortcut-only route |
| C — gameplay/narrative | **Fail** | Complete current route, atomic recovery/rewards, exact bosses/scenes, all post-quest services |
| D — verify | **Not run for a modern implementation** | Static/pack/E2E/concurrency/reconnect/client evidence and idempotent completion |

Dragon Slayer II must remain `audit-pending` until all critical chapters are
playable through normal world interactions. The presence of a 215 state, a
headless debug walk, narrated boss phases, or isolated reward objects is not a
modern implementation.
