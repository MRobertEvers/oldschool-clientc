# Between a Rock... modernization audit

Status: `audit-pending` — the cache-native quest row, complete 0–110 primary
state ladder, permanent carrier, public NPC placements, travel, page gathering,
golden cannonball, schematic items, gold helmet, realm map, Avatar variants,
journal, cheat arm, and modern completion call exist. A nominal route can reach
completion, but it does so by omitting Dwarf Cannon, collapsing the schematic
puzzle, reversing the cannon-shot story, and replacing the Arzinian encounter
with a globally shared generic NPC. The post-quest mine, ore-banking service,
ring-of-wealth teleport, and both music unlocks are not operational. A separate
global scorpion drop hook also leaks a quest page to unrelated players.

Audited: 2026-08-17

Governing plan: [Quest modernization plan](../QUEST_MODERNIZATION_PLAN.md). This
record applies that plan's Gates A–D to the south-west Keldagrim route,
Dondakan, both ferrymen, the boatmen, Librarian, Dwarven Engineer, Rolad,
Khorvak, Dwarven Mine page sources, furnace and anvil integrations, native
schematic interface, cannon cutscenes, Dondakan's mine, all nine Avatar forms,
completion, post-quest mining/banking, ring-of-wealth travel, music, journal,
cheat adapter, and shared-quest dispatch. It is an implementation specification,
not completion evidence.

## 1. Authoritative references

These stable OSRS Wiki revisions define the route, dialogue, items, encounter,
rewards, and permanent services.

| Reference | Pinned revision | Use in this audit |
| --- | --- | --- |
| [Between a Rock...](https://oldschool.runescape.wiki/w/Between_a_Rock...?oldid=15292292) | 15292292, 2026-08-10 | Identity, requirements, route, eight-minute rule, Avatar selection, rewards, music, and downstream use |
| [Between a Rock.../Quick guide](https://oldschool.runescape.wiki/w/Between_a_Rock.../Quick_guide?oldid=14555440) | 14555440, 2024-03-17 | Ordered actions, dialogue choices, page sources, Resource Area helmet, schematic route, and combat preparation |
| [Transcript:Between a Rock...](https://oldschool.runescape.wiki/w/Transcript%3ABetween_a_Rock...?oldid=15278849) | 15278849, 2026-07-28 | Start/refusal, re-talks, item loss/capacity, Khorvak/Rolad branches, cutscenes, timer warnings, finale, and post-quest dialogue |
| [Arzinian Being of Bordanzan](https://oldschool.runescape.wiki/w/Arzinian_Being_of_Bordanzan?oldid=15167148) | 15167148, 2026-04-07 | Six-ore damage gate, 6–14/15+ difficulty, regeneration, style counter, and intentional randomization |
| [Dondakan's mine](https://oldschool.runescape.wiki/w/Dondakan%27s_mine?oldid=15233018) | 15233018, 2026-06-13 | Post-quest entry/exit, 147 gold sources, ore-hauler fee, helmet requirement, and transport |
| [Gold helmet](https://oldschool.runescape.wiki/w/Gold_helmet?oldid=15183047) | 15183047, 2026-04-22 | 30 Defence equip gate, 50 Smithing/30 XP recipe, eight-minute quest-only weight, any-anvil replacement, and diary use |
| [Dwarven lore](https://oldschool.runescape.wiki/w/Dwarven_lore?oldid=15282362) | 15282362, 2026-07-30 | Full readable book, replacement, and base-schematic item lifecycle |
| [Rolad](https://oldschool.runescape.wiki/w/Rolad?oldid=14768114) | 14768114, 2024-10-13 | Page hand-in, book replacement, lie choice, and schematic conversation |
| [Dondakan the Dwarf](https://oldschool.runescape.wiki/w/Dondakan_the_Dwarf?oldid=15227292) | 15227292, 2026-06-06 | Quest and post-quest subjects, re-entry, helmet enforcement, and boots branch |
| [Khorvak](https://oldschool.runescape.wiki/w/Khorvak?oldid=216504) | 216504, 2014-05-01 | Stout/refusal routes and shared White Wolf Tunnel placement |
| [Dwarven Engineer](https://oldschool.runescape.wiki/w/Dwarven_Engineer?oldid=14988953) | 14988953, 2025-09-19 | Research handoff and Engineer schematic ownership |
| [Ring of wealth](https://oldschool.runescape.wiki/w/Ring_of_wealth?oldid=15234929) | 15234929, 2026-06-18 | Charged and imbued Dondakan destination, completion gate, and one-charge consumption |

The sources identify Between a Rock... as quest #74, an experienced, medium,
members quest released 21 March 2005. It requires Dwarf Cannon and Fishing
Contest. Its 30 Defence, 40 Mining, and 50 Smithing requirements are boostable
and are not all start gates: the relevant actions enforce them. The required
combat is level 14 scorpions and a level 75–125 Arzinian Avatar.

Transition aid only: the local Quest Helper checkout's
[`BetweenARock.java`](https://github.com/Zoinkwiz/quest-helper/blob/5ea99d5ea9ba3fb096ebe7b5ed02d80883e9819d/src/main/java/com/questhelper/helpers/quests/betweenarock/BetweenARock.java)
and
[`PuzzleStep.java`](https://github.com/Zoinkwiz/quest-helper/blob/5ea99d5ea9ba3fb096ebe7b5ed02d80883e9819d/src/main/java/com/questhelper/helpers/quests/betweenarock/PuzzleStep.java)
at commit `5ea99d5ea9ba3fb096ebe7b5ed02d80883e9819d` confirm all eleven
active states from 0 through 100, zones, entities, item conditions, and
schematic targets; the cache quest row and Dondakan transform confirm completion
at 110. The
puzzle's three overlay targets are `(240,170,1856)`, `(235,170,1860)`, and
`(235,175,1864)`, with four-pixel positional tolerance. These files guide
transition tests but do not override the Wiki, transcript, or osrs239 cache.

`python3 tools/questhelper_extract.py betweenarock --check` resolves every
Quest Helper item, NPC, loc, coordinate, varbit, and `quest_betweenarock`.

### Reference differences requiring an explicit decision

| Subject | Evidence | Modernization rule |
| --- | --- | --- |
| Ferryman fee | Current pinned Wiki: 2 coins, or free through the worn ring-of-charos (a) Charm route. Pinned Quest Helper: 5 coins. Current script: 5 coins. | Follow the current Wiki contract: two coins and the free Charm route. Record a live-client capture because the helper is stale here. |
| Mining page 3 | Article and Quest Helper name clay, copper, tin, and iron; the quick guide says any rock except adamantite or coal. | Retain the four-source predicate until live evidence proves the broader quick-guide wording; test every local rock ID. |
| Weakness threshold | Article/Being page require six ore to damage the Avatar; the transcript has no-gold and `5–14` wording. | Six is the damage threshold. Test 0–5 individually; never turn the discrepancy into an invented hard wall at the flame. |

## 2. Native quest identity and player contract

| Field | Native value / expected behavior |
| --- | --- |
| Cache quest ID | 76 |
| Type | Members quest |
| Difficulty / length | Experienced / medium |
| Series | None |
| Release date | 21 March 2005 |
| Start | Dondakan in the Keldagrim south-west mine, approximately 2821,10168,0 |
| Prerequisites | Dwarf Cannon and Fishing Contest |
| Required levels | 30 Defence, 40 Mining, 50 Smithing; all boostable and action-gated (`requirement_check_skills_on_start=0`) |
| Required items | Pickaxe, four ordinary gold bars, hammer, ammo mould or double ammo mould, and ferry/travel coins; gold ore may be shown instead of a bar |
| Recommended | Combat 50, food/armour/weapon, Falador or other route teleports, and 15 free inventory spaces for the easier Avatar |
| Primary state | `%dwarfrock_quest`, bits 0–7 of `dwarfrock_main`, states 0/10/.../100 and complete 110 |
| Side state | Fifteen native fields on bits 8–31 of `dwarfrock_main` for research, cannonball, schematics, brothers, realm visit/timer, ferrymen, boatman, and Engineer |
| Quest points | 2 |
| Experience | 5,000 Defence, 5,000 Mining, and 5,000 Smithing XP; native values are 50,000 tenths each |
| Item rewards | Rune pickaxe and retained gold helmet |
| Unlocks | Dondakan's/Arzinian mine; ore-hauler banking; charged ring-of-wealth Dondakan teleport; Claustrophobia and In Between music |
| End state | 110 |
| Partial requirement for | Medium Wilderness Diary and Medium Fremennik Diary |

The cache row's two `requirement_quests` values are corrupt. IDs 35 and 52
resolve locally to `quest_sheepherder` and `miniquest_magearena1`; the real
Dwarf Cannon and Fishing Contest rows are IDs 47 and 27. The script knowingly
works around only Fishing Contest and deliberately omits Dwarf Cannon because
that quest is presently incompletable. Modernization must correct the dbrow and
enforce both canonical prerequisites. Dwarf Cannon's missing start/finale is a
downstream blocker to verification, not permission to publish a false gate.

### Native side-state inventory

| Varbit | Bits | Required ownership |
| --- | ---: | --- |
| `%dwarfrock_lookingforinfo` | 8 | Dondakan's initial research request / Keldagrim inquiry dialogue |
| `%dwarfrock_gold_cannonball` | 9 | Gold material accepted and special furnace recipe enabled |
| `%dwarfrock_rolad_schematics_heardof` | 10 | Rolad schematic subject introduced |
| `%dwarfrock_rolad_schematics_lookingfor` | 11 | Rolad has sent the player back to the lore book |
| `%dwarfrock_dondakan_inside_heardof` | 12 | First realm report / re-entry dialogue distinction |
| `%dwarfrock_schematics_solved` | 13 | Server-validated completion of interface 113/114 |
| `%dwarfrock_brothers_introduced` | 14 | Miodvetnir/Derni/Dernu introductory subject |
| `%dwarfrock_brothers_toldvictory` | 15 | One-time post-victory brothers dialogue |
| `%dwarfrock_inside_visited` | 16 | First launch versus later quest re-entry |
| `%dwarfrock_inside_timeleft` | 17–26 | Quest-only eight-minute realm budget and warnings |
| `%dwarfrock_ferryman1_beenbefore` | 27 | Paid-side ferryman first/repeat conversation |
| `%dwarfrock_ferryman2_beenbefore` | 28 | Return ferryman first/repeat conversation |
| `%dwarfrock_gold_boatman_met` | 29 | Post-quest ore-hauler introduction, not the Keldagrim shortcut boatman |
| `%dwarfrock_fired_gold_cannonball` | 30 | Golden cannonball cutscene completed |
| `%dwarfrock_met_engineer` | 31 | Initial Engineer research conversation completed |

The local overlay correctly declares `dwarfrock_main` as a permanent,
transmitted carrier. Current production code writes only the cannonball,
solved, visited/timer, ferryman, shortcut-boatman, fired, and Engineer fields.
The research, Rolad, realm-report, and three-brothers fields are unused, and the
boatman bit is assigned to the wrong NPC subject.

## 3. Implementation surface

Paths below are relative to `OSRS-Content/osrs239-content/`.

### Quest-owned files

| Path | Present responsibility | Audit result |
| --- | --- | --- |
| `server/scripts/quests/quest_betweenarock/configs/betweenarock.constant` | Primary states, levels, XP, fee, ore thresholds, timer | States/XP are sound; fee is stale; comments incorrectly claim the Avatar colours are cosmetic and the timer is approximate |
| `server/scripts/quests/quest_betweenarock/configs/betweenarock.varp` | Permanent `dwarfrock_main` overlay | Correct carrier declaration |
| `scripts/betweenarock_dondakan.rs2` | Dondakan, cannonball, furnace integration | Auto-starts, reverses who enters the rock, omits cutscenes/recovery/ore input, and allows a perfect-gold furnace exploit |
| `scripts/betweenarock_pages.rs2` | Rolad, pages, lore book | Guarantees every find, auto-combines pages, loses items at capacity, omits book replacement/full reading, and does not consume the ruined book |
| `scripts/betweenarock_schematics.rs2` | Engineer, shared Khorvak, puzzle, helmet | Shadows Between a Rock behind Forgettable Tale, invents a coin purchase, click-solves the native puzzle, and limits the helmet to one anvil |
| `scripts/betweenarock_realm.rs2` | Launch, timer, flame, Avatar, kill credit | Public global boss, wrong difficulty selection, incorrect strongest-stat calculation, no regeneration, and incomplete exit cleanup |
| `scripts/betweenarock_travel.rs2` | Tunnels, ferrymen, shortcut boatman, cannon | Manual teleports, stale fee, missing op3 travel, no Charm route, and post-quest cannon goes to the wrong side of the rock |
| `scripts/betweenarock_shared.rs2` | Skill gates, prerequisite gate, completion | Intentionally omits Dwarf Cannon; completion is unguarded and rune-pickaxe capacity is unsafe |
| `scripts/betweenarock_journal.rs2` | Dynamic journal | Broadly tracks the ladder but omits Dwarf Cannon, side-state recovery, exact item state, and permanent services |

The quest root totals 913 lines across nine files. It is organized better than
many older quest roots, but several headers normalize deliberate shortcuts as
precedent. Those comments are not fidelity evidence and must be removed with
the shortcuts.

### Mandatory shared and cross-directory surfaces

| Path / subsystem | Relationship | Audit result / required ownership |
| --- | --- | --- |
| `server/scripts/areas/world/configs/m44_158.spawn`, `m44_159.spawn`, `m44_154.spawn`, and `m47_53.spawn` | Dondakan, ferrymen, brothers, ore hauler, Engineer, Khorvak, Rolad | All principal public carriers are placed; native Dondakan and ore-hauler transforms already key off state 110 |
| `server/scripts/skill_mining/scripts/mining.rs2` | Page 3 and realm gold mining | Page hook runs after successful ore grant; it needs probability/capacity policy and the realm needs warning/Avatar coupling |
| `server/scripts/skill_smithing/scripts/smelting/smelting.rs2` | Golden cannonball | Both ordinary and perfect-gold bars dispatch to the quest label; the label always deletes `gold_bar`, so perfect gold can produce a cannonball without consuming the used item |
| `server/scripts/drop_tables/scripts/wiki_scorpion.rs2` | Other scorpion variants | Unconditionally drops `dwarf_rock_page1` from jungle and Stronghold-of-Security scorpions for every player, outside the quest and Dwarven Mine |
| `server/scripts/interface_questjournal/scripts/quest_journal.rs2` | Journal dispatcher | Correctly calls `~betweenarock_journal` |
| `server/scripts/quests/scripts/quest_cheat.rs2` | `::complete` adapter | Writes only 110; does not establish music, post-quest service, or optional dialogue coherence |
| `quest_forgettabletale` and Khorvak | One NPC serves two quests | Current Between a Rock trigger gives every active Forgettable Tale branch unconditional priority, making the schematic unreachable in overlapping states |
| Generic smithing/anvil and Wilderness Diary | Gold helmet recipe | Quest code binds only `dwarf_keldagrim_anvil`; it cannot satisfy the Resource Area diary path or any-anvil post-quest replacement |
| Ring-of-wealth item family and Fountain of Rune | Reward teleport | Charged normal/imbued cache items advertise `Dondakan`, but no Rub/worn teleport service consumes a charge or gates the destination |
| `dwarfrock_multi_gold_boatman` / `dwarfrock_gold_boatman` | Post-quest ore hauling | Carrier appears at 110, but no dialogue or ore-banking operation exists; the script confuses its bit with `dwarf_city_boatman_mines_postquest` |
| Interface 113 `dwarf_rock_schematics` and 114 control | Schematic puzzle | Cache models, controls, select buttons, movement, rotation, solved text, and Quest Helper targets exist; production never mounts them |
| Realm map, timer, equipment/logout, and owner-scoped encounter services | Dondakan's mine and Avatar | Map exists, but quest uses shared static coordinates/global `npc_find`; helmet removal/logout, gold absorption, warning ticks, NPC ownership, and cleanup are not integrated |
| `music_claustrophobia` and `music_in_between` | Music rewards | Native rows use `musicmulti_10` bits 3 and 2. No quest or area script writes either bit |
| Completion and ground-item services | XP/QP/rune pickaxe | Shared completion call is present; the item grant needs an atomic inventory-or-ground outcome and an internal once-only guard |

### Cache-native assets already available

- state-driven Dondakan and post-quest ore-hauler carriers, both ferrymen, the
  three boatman brothers, Rolad, Engineer, Khorvak, and fake cutscene actors;
- all individual/combined pages, lore book, four schematic pieces, completed
  schematic, golden cannonball, gold helmet, rune pickaxe, and cannon moulds;
- the native schematic and control interfaces, three overlay/border model
  pairs, movement/rotation controls, selected-state varps, and solved message;
- both cannon loc forms, flame walls, 70 gold rocks, 77 gold veins, and the
  complete realm map;
- blue/no-level, green level-125, and yellow level-75 versions of the Magic,
  Ranging, and Strength Avatars, plus the chatbox-only Arzinian Being;
- dedicated rock/Avatar animations and nine sets of combat stats; and
- every primary/side varbit plus the two music rows and ring menu metadata.

Modernization should connect these assets through the current dialogue,
protected cutscene, interface, item-transaction, owner-scoped encounter,
combat-AI, equipment/region lifecycle, music, charged-jewellery, travel, and
completion services. It should not replace them with messages, global NPC
searches, direct solved bits, or quest-local copies of shared systems.

## 4. Native state model and current reachability

| State | Required phase | Current implementation / defect |
| ---: | --- | --- |
| 0 | General Dondakan subjects, two-step investigation, explicit accept/refuse | Only Fishing Contest is checked; one linear conversation auto-accepts and writes 10 |
| 10 | Ask around Keldagrim; Librarian optional fallback; Dwarven Engineer points to Rolad | Engineer directly writes 20. Librarian and brothers subjects do not exist |
| 20 | Meet Rolad; dialogue disengages/re-engages; accept three-page search | Abbreviated dialogue writes 30 and ignores native research side bits |
| 30 | Find three pages in any order, combine them, report partial counts, give them to Rolad | Finds are guaranteed; cart/mining can lose items at full capacity; pages auto-jump to `pagex3`; partial re-talks and manual `pagex2` assembly are absent |
| 40 | Read the complete Dwarven lore and recover it if lost | One message writes 50; no readable book pages or replacement route |
| 50 | Tell Dondakan what the book says, watch granite-boot demonstration, then show ordinary gold ore or bar | Requires both book and bar before dialogue, skips the story/cutscene, rejects ore, and writes 60 |
| 60 | Cast a golden cannonball, confirm firing it, watch it pass through, then accept/refuse being fired and receive Dondakan's schematic | Furnace instantly crafts; shot is a message; next dialogue says Dondakan himself will enter, then writes 70 |
| 70 | Dondakan explains human-cannon modifications and gives/replaces his schematic | Script falsely reports Dondakan visited the realm, grants without capacity fallback, and writes 80 |
| 80 | Obtain four pieces in flexible order, tear base from book, make helmet, solve native overlay puzzle, return equipped | Engineer incorrectly requires base first; Khorvak may be shadowed; book remains; interface is skipped; helmet works only at one anvil; grants lack recovery |
| 90 | Protected launch, absorb carried gold, mine under eight-minute quest timer, converse at flame, fight owned Avatar | Teleports immediately to public map, preserves carried gold, polls helmet every 50 ticks, blocks flame below six ore, randomly chooses difficulty, and shares one global boss |
| 100 | Return after credited Avatar defeat and complete once | Short dialogue calls unguarded completion; rune pickaxe has no ground fallback |
| 110 | Permanent mine entry, no timer, ore banking, ring travel, music, ordinary dialogue | Dondakan cannot re-enter the mine, cannon fires outside, ore hauler has no handler, ring travel/music are absent |

### Deterministic route and first blockers

All core public NPCs are present, so this is not a missing-spawn hard block.
On an account with Fishing Contest complete, the simplified route can normally
advance 0→110. The first canonical blocker is earlier: a legitimately eligible
account cannot complete the required Dwarf Cannon prerequisite in this tree,
and Between a Rock deliberately ignores it. Once that dependency is repaired,
the corrected dbrow/gate must reject the old impossible route rather than
silently preserving it.

Independent route corruptions can block particular accounts: an overlapping
Forgettable Tale state monopolizes Khorvak, inventory-full page/book/schematic
grants can mutate state without granting the item, and the public Avatar can be
claimed or killed by another player. After completion, the advertised mine is
deterministically inaccessible through canonical means: Dondakan offers no
shoot-again subject and the cannon sends the player outside, not into the mine.

## 5. Current versus required playable route

| Chapter | Current route | Modernized route contract |
| --- | --- | --- |
| Travel/start | Five-coin op1 ferry; no Charm/op3; auto-start | Two-coin atomic fare or worn-charos free Charm, bind Talk-to and Travel consistently, preserve ordinary route subjects, enforce both quests, and commit only after explicit Yes |
| Research | Engineer→Rolad in two short talks | Preserve Dondakan's second conversation, city inquiry/Librarian fallback, Engineer and optional brothers dialogue, and native side bits |
| Pages/book | First attempt always succeeds; auto-combine; one-screen book | Retry-capable sources, correct Dwarven Mine/rock predicates, inventory-or-ground grants, page1+page2/page3 combinations, partial Rolad dialogue, lie/truth choice, full readable lore, and replacement |
| Gold hypothesis | Dialogue requires book+bar; only bar accepted | Dondakan first receives the book findings, stages granite kick, rejects other/perfect metals, then accepts ordinary gold ore or bar independently |
| Cannonball/shot | Furnace instantly swaps one bar; message shot; story says Dondakan enters | Confirm special recipe, validate actual used item and ammo mould, consume ordinary bar only, animate casting, preserve normal jewellery menu, then protected cannon shot in which only the ball passes through |
| Schematic search | Fixed ordering and unsafe grants | Give/recover Dondakan piece; allow Engineer/Khorvak/Rolad pieces in authored order-flexible dialogue; consume the ruined book for base; arbitrate Khorvak with Forgettable Tale |
| Puzzle/helmet | One click consumes four pieces; one special anvil | Mount interfaces 113/114, persist one open-session puzzle, server-validate all three positions/rotations, then atomically assemble; add the 50 Smithing/30 XP recipe to shared any-anvil smithing with bank-aware duplicate policy and diary compatibility |
| First launch | Short talk and immediate teleport | Require worn helmet, assembled schematic, 30 Defence and readiness confirmation; consume/hand over schematics as authored; run protected cannon/flying/landing sequence; unlock Claustrophobia at the authored launch event |
| Realm timer | Six-ore flame wall; coarse silent polling | Absorb all carried gold ore/bars, arm exact 800-tick quest timer, emit warnings at 100/250/400/500/600/700/750/775, exit immediately on helmet removal, and clear quest gold on exit/time/death as required |
| Avatar | Random colour, melee=Attack+Strength, generic global NPC | Counter the highest individual Attack/Strength/Ranged/Magic stat with intentional small randomization; blue is invulnerable below six ore, green is 125 at 6–14, yellow is 75 at 15+; continuously couple regeneration to held ore in an owner-scoped encounter |
| Finale | Generic kill callback, outside teleport, short completion | Credit only the owning player, handle timer/death/logout races, return/drop helmet as authored, stage full Dondakan report, grant/drop rune pickaxe, and commit XP/QP/count exactly once |
| Post-quest | Claimed only in reward text | Dondakan/cannon re-entry with worn helmet and no timer; immediate helmet-removal/logout exit; unlock In Between on first post-quest entry; operational ore hauler and charged/imbued ring-of-wealth destination |

## 6. Oversight register

### P0 — release blockers and integrity

1. **Canonical eligibility is false.** The dbrow points at Sheep Herder and
   Mage Arena I, the script checks only Fishing Contest, and Dwarf Cannon is
   deliberately omitted. Fix Dwarf Cannon, then correct and enforce both rows.
2. **Perfect-gold cannonball exploit.** The global furnace switch routes
   `perfect_gold_bar` into a label that deletes `gold_bar` and grants the
   cannonball. A player can preserve the perfect bar and potentially create the
   quest item without an ordinary bar.
3. **Quest item leaks globally.** Jungle and Stronghold-of-Security scorpions
   unconditionally drop book page 1 to any killer, regardless of quest state,
   ownership, or location.
4. **The defining puzzle is not implemented.** `Assemble` waits one tick,
   deletes four pieces, sets solved, and never opens the two native interfaces.
5. **The defining boss is not implemented safely.** One static public NPC is
   selected with wrong stats/difficulty, has no gold-driven regeneration, and
   uses global `npc_find`/`npc_findhero`; simultaneous players can steal state.
6. **Advertised permanent access is broken.** At state 110 neither Dondakan nor
   the cannon enters Dondakan's mine. The cannon's destination is the outside
   rock, so the primary reward cannot be used.

### P1 — route, state, and shared-system correctness

1. Dondakan auto-accepts. Refusal, ordinary subjects, the two-conversation
   start, Librarian fallback, and full transcript recovery are absent.
2. Seven native narrative bits are unused; the shortcut boatman incorrectly
   owns the ore-hauler bit. The brothers have no quest dialogue.
3. Ferry op3 is unbound, the current fee is stale, and ring-of-charos (a)
   cannot Charm for a free trip.
4. Cart/mining page, lore book, Dondakan/Engineer/Khorvak schematics, and rune
   pickaxe grants are not consistently capacity-safe or recoverable.
5. Page sources are guaranteed rather than retry-based; pages skip their native
   two-page item; Rolad cannot describe 0/1/2-page progress or replace the lore.
6. Dondakan refuses valid gold ore, accepts only bar, has no perfect-gold
   rejection, and skips the granite-boots demonstration.
7. The golden cannonball is made without the confirmation/menu/animation and
   the cannon sequence incorrectly claims Dondakan travelled through the rock.
8. Khorvak fabricates a two-coin drink transaction, never accepts a stout used
   on him, and is unreachable under broad Forgettable Tale states.
9. Reading the schematic page copies it without consuming the ruined lore
   book. Rolad's schematic discovery/report dialogue is absent.
10. The gold helmet recipe is quest-local to one loc. It fails the Resource
    Area diary path, post-quest any-anvil recreation, bank duplicate check, and
    shared smithing menu integration.
11. Launch preserves pre-carried gold, enabling the 6/15-ore fight thresholds
    without mining. Helmet removal is checked only every 50 ticks, timer
    warnings are absent, and exit does not absorb realm gold.
12. The flame refuses all sub-six attempts even though OSRS manifests the blue
    invulnerable/regenerating form. Difficulty is random cosmetic colour despite
    cache stats proving blue/green/yellow are distinct.
13. `Attack + Strength` is compared with individual Ranged/Magic, so melee is
    almost always selected as strongest. Intentional form randomization is also
    missing.
14. Avatar Magic/Ranged variants have no verified style-specific projectile/
    attack AI. The generic category/animation alone is not evidence.
15. Completion has no internal state-100 once-only guard and no atomic
    inventory-or-ground rune pickaxe grant.

### P2 — rewards, presentation, and maintenance

1. The post-quest ore hauler is visible but has no handler. The required fee is
   `ceil(ore * 20%)`, or `ceil(ore * 10%)` with worn ring of charos (a), with an
   explicit confirmation and atomic bank transfer.
2. Charged and imbued ring-of-wealth cache items expose Dondakan subactions,
   but no Rub/worn teleport dispatcher, completion filter, charge consumption,
   or landing exists.
3. `musicmulti_10` bit 3 (Claustrophobia) and bit 2 (In Between) are never
   written. In Between belongs to first post-quest mine entry, not completion.
4. Post-quest Dondakan lacks shoot-again and full boots subjects. Post-quest
   entry must have no eight-minute limit while still requiring the helmet.
5. The journal omits Dwarf Cannon, precise partial-page/schematic recovery,
   active realm time, failed Avatar attempt, and permanent reward guidance.
6. The cheat adapter produces state 110 without documenting which mandatory
   completion effects are derived from state and which require coherent bits;
   it must never replay production XP/QP/rewards.
7. Comments throughout the root describe deliberate fidelity gaps as accepted
   engine precedent and incorrectly assert that no distinct 75/125 NPCs exist.

## 7. Modernization work packages

Execute these in dependency order. Keep quest policy in RuneScript/config; add
C only for a missing reusable VM/protocol capability.

### Package 0 — prerequisite dependency and native contract

1. Finish and verify Dwarf Cannon's missing start, state 0→1, 8→9, and 10→11
   transitions before enabling the canonical Between a Rock gate.
2. Correct `quest_betweenarock` prerequisite dbrows to Dwarf Cannon (47) and
   Fishing Contest (27); add a regression that resolves row names, not only
   numeric values.
3. Name/document every primary and side field and its sole writer. Remove the
   soft-skip and stale/cosmetic/approximation comments.
4. Make the quest requirement helper enforce the two completed quests at state
   0 while leaving the three boostable skill checks at their authored actions.

### Package 1 — travel, start, and research dialogue

1. Move both ferrymen and the shortcut boatman onto the shared travel service:
   exact endpoints, two-coin fare, free worn-charos Charm, atomic payment,
   first/repeat dialogue, and both Talk-to/Travel operations.
2. Rebuild Dondakan state 0 as a deterministic subject menu with Who are you?,
   cannon/rock investigation, noisy/goodbye branches, explicit Yes/No, and a
   state write only after acceptance succeeds.
3. Use `%dwarfrock_lookingforinfo`, `%dwarfrock_met_engineer`, and the three
   brothers bits to preserve the Keldagrim inquiry, Librarian fallback,
   Engineer handoff, Miodvetnir/Derni/Dernu introduction, and victory re-talk.
4. Keep all carrier spawns/multis native. Do not hand-spawn permanent dialogue
   NPCs or bind the transform wrapper instead of its visible leaf.

### Package 2 — pages, lore, and gold cannonball

1. Implement retry-capable page rolls only after state 30 and only at the
   validated Dwarven Mine sources. Remove the unconditional external scorpion
   drop. Every success must use inventory-or-owned-ground fallback.
2. Support all individual page combinations through `dwarf_rock_pagex2` and
   `pagex3`; preserve partial inventory and Rolad's 0/1/2/3-page responses.
3. Protect Rolad's hand-in, truth/lie choice, book grant, full readable text,
   lost-book replacement, and state 40→50 commit. Never delete pages before a
   successful replacement item outcome.
4. Separate Dondakan's lore report from item use. Accept ordinary gold ore or
   bar, reject perfect gold and unrelated metals, stage the granite kick, and
   write the cannonball permission bit once.
5. Add the golden cannonball to the shared furnace recipe/menu with a confirmed
   ordinary-bar input, either mould, correct animation/XP, and rollback-safe
   item transaction. Preserve the jewellery menu for cancel/other inputs.
6. Run the cannonball shot as a protected sequence with loc/NPC animation,
   confirmation/refusal, and exact dialogue. Commit fired state only after the
   ball has passed through and then give/recover Dondakan's schematic.

### Package 3 — schematic ownership and native puzzle

1. Establish one Khorvak dispatcher that offers explicit Forgettable Tale and
   Between a Rock subjects when both are active. Implement the free refusal
   route and the alternate item-on-NPC dwarven-stout hand-in; do not buy a
   fictional drink with coins.
2. Make Dondakan, Engineer, Khorvak, and Rolad/book entitlements individually
   recoverable with inventory-or-ground semantics. Use the two Rolad side bits
   for the authored discovery/re-read conversation and consume the ruined book
   when its base page is torn out.
3. Mount interface 113 with interface 114 controls. Initialize the three
   overlay models, selected varps, movement and rotation; re-arm button ops and
   keep unsolved session state isolated per player.
4. Validate all three target positions/animations on the server within the
   native four-pixel tolerance. Only then consume the four pieces, grant the
   completed schematic, set `%dwarfrock_schematics_solved`, show the native
   solved layer, and play Ready to Fire.
5. Integrate the gold helmet into shared any-anvil smithing: state eligibility,
   boosted 50 Smithing, hammer, three bars, 30 XP, confirm/menu, bank-aware
   duplicate rule, Resource Area diary event, and post-quest replacement.

### Package 4 — protected launch and realm lifecycle

1. At state 80 require assembled schematic, worn helmet, boosted 30 Defence,
   and explicit readiness. Hand the schematic to Dondakan only inside the
   protected transaction and preserve refusal/reconnect recovery.
2. Run the cannon-adjust/flying/landing cutscene with native actors/locs,
   interruption cleanup, exact destination, and one state-90 commit. Unlock
   Claustrophobia at its authored first launch event.
3. Before every quest launch, atomically absorb all gold ore and bars from the
   inventory. Start a precise 800-tick owner timer and write the native
   `inside_timeleft` representation without allowing relog to extend it.
4. Integrate worn-item removal, logout/world-hop, teleport, death, and region
   change. Helmet removal exits immediately; logout/world-hop exits safely;
   quest timeout removes the helmet as authored and all exit paths clean owned
   NPCs/timers and prevent imported/mined gold leakage.
5. Emit the transcript warnings at 100, 250, 400, 500, 600, 700, 750, 775,
   and 800 ticks. The eight-minute timer is disabled after completion, but the
   helmet remains mandatory for entry and removal still exits.

### Package 5 — Arzinian encounter

1. Let the player speak through the central flame even below six ore. Stage the
   Arzinian Being conversation and mining taunts; do not use an op1 click as a
   silent spawn button.
2. Determine the player's strongest individual combat stat. Counter Attack or
   Strength with Magic, Magic with Ranging, and Ranged with Strength, preserving
   the source-confirmed small intentional randomization and deterministic tests
   around ties/random branches.
3. Spawn blue/no-level below six ore, green level 125 at 6–14, and yellow level
   75 at 15+. Re-evaluate held ore during combat: below six makes damage
   ineffective and restores the Avatar, while 15+ remains the easier native
   form according to verified transition policy.
4. Allocate one owner-scoped encounter and implement the correct melee,
   projectile Ranged, and projectile Magic attacks, protection-prayer behavior,
   target/kill credit, and NPC dialogue. Never use global `npc_find` as lock or
   ownership.
5. Handle flee by helmet removal, timeout, teleport, logout, player death,
   simultaneous players, duplicate death queues, and same-tick timer/kill
   races. Exactly one credited kill writes state 100 and returns the player
   outside with the gold-helmet inventory/ground behavior from the transcript.

### Package 6 — completion and permanent services

1. Replace the short finale with the full Dondakan report and one guarded
   state-100 transaction: state 110, three 5,000-XP awards, two QP/count through
   the shared completion call, rune pickaxe inventory-or-ground, retained gold
   helmet, one scroll, and one jingle.
2. Rebuild post-quest Dondakan subjects. Shoot-again and the cannon Fire
   operation both require a worn helmet and enter the mine; boots and ordinary
   conversation remain available. There is no post-quest time limit.
3. Bind `dwarfrock_gold_boatman` as the ore hauler. Confirm the load, compute
   `ceil(n * 20 / 100)` or `ceil(n * 10 / 100)` with worn charos, atomically
   remove fee/deposit remainder, and cover small loads/full bank/relog races.
4. Add Dondakan to the reusable ring-of-wealth teleport dispatcher for every
   charged normal and imbued variant. Require state 110, consume exactly one
   charge only after successful teleport, respect Wilderness limits, and land
   at 2824,10168,0. Locked accounts must not see/use the destination.
5. Unlock In Between only on first real post-quest mine entry; preserve
   Claustrophobia/In Between and unrelated `musicmulti_10` bits independently.
6. Expand the journal and make `::complete` yield a coherent test-only state
   without production XP/QP/items. State-derived transforms may update
   naturally; optional first-use dialogue/music must not be falsely claimed.

## 8. Verification contract

### Static and pack verification

- `python3 tools/questhelper_extract.py betweenarock --check`;
- assert the corrected dbrow resolves exactly Dwarf Cannon and Fishing Contest,
  and both are completed before any state-0 mutation;
- assert all 0/10/.../110 primary states and all fifteen side fields have one
  documented owner, resume policy, and journal policy;
- assert every principal carrier has exactly one production placement and all
  cutscene/Avatar actors are owner-scoped with cleanup;
- assert `dwarf_rock_page1` has no unconditional NPC drop and all three page
  predicates require state 30 plus Dwarven Mine location;
- assert the furnace distinguishes `gold_bar` from `perfect_gold_bar`, cannot
  grant on failed deletion, and preserves normal crafting;
- assert every unique page/book/schematic/cannonball/helmet/reward transition
  is atomic and has inventory, ground, loss, and duplicate policy;
- assert interfaces 113/114 are mounted through the modern interface service,
  all controls are server-handled/re-armed, and solved cannot be forged by a
  close packet or direct bit write;
- assert the helmet recipe is shared by every valid anvil, emits the diary
  event in Resource Area, and checks inventory/equipment/bank duplicates;
- assert no launch preserves pre-carried gold and exact realm warning/exit
  hooks cover helmet removal, 800 ticks, logout, world-hop, teleport, death,
  and completion;
- assert Avatar colour/stats follow held-gold thresholds, highest-stat counter
  uses individual stats, every style has real combat AI, and no global NPC
  search controls ownership or progress;
- assert completion is reachable only from 100, writes 110 once, and repeated
  clicks/queues/relogs never repeat XP/QP/count/item/scroll;
- assert the ore hauler, mine entry/exit, charged/imbued ring teleport, and two
  music bits are operational rather than reward text;
- assert Khorvak, Librarian, brothers, ferrymen/boatmen, shared smithing,
  mining, scorpion drops, ring travel, music, and diary triggers each have one
  deterministic dispatcher with regression coverage;
- `make -C src mock230-scripts`; and
- `mock230_pack --check-only` against the intended cache.

### Automated route matrix

At minimum, test:

1. start with each prerequisite incomplete/complete and both incomplete; all
   three skills below/exact/boosted/drained; refusal, repeat click, and zero
   mutation on failure;
2. ferry from both sides with 0/1/2+ coins, worn/unworn valid charos variants,
   Talk-to/Travel/Charm, first/repeat dialogue, payment rollback, and exact
   endpoints;
3. every Dondakan state-0 subject/order, Librarian before/after Engineer,
   brothers introduction/victory, interruption/re-talk, and native bit writes;
4. page sources before/during/after state 30, every Dwarven Mine rock, every
   unrelated scorpion/cart/rock, repeated failures, full inventory ground
   fallback, pickup/loss, all six pair orders, and partial Rolad dialogue;
5. Rolad hand-in with 0/1 free slots, truth/lie, book Read/cancel/full content,
   drop/destroy/loss replacement before/after reading, and exact 40→50 commit;
6. Dondakan lore report, every ordinary/perfect/other ore/bar used before and
   after permission, granite cutscene skip/reconnect, and no item consumed by
   showing it;
7. furnace with ordinary/perfect/no bar, each mould, no mould, normal jewellery,
   confirmation/cancel, duplicate packets, animation/XP, full inventory, and no
   grant after failed deletion;
8. golden cannonball confirmation/refusal, protected shot interruption, exact
   consumption, fired bit, Dondakan story, schematic capacity/drop/loss/
   replacement, and no duplicate ball/schematic;
9. Khorvak with every Forgettable Tale overlap, subject selection, refusal,
   stout use/no stout/full inventory, plus Engineer and Rolad pieces in every
   order with loss/recovery;
10. schematic interface open/close/remount/relog, every selection/movement/
    rotation, boundary coordinates, four-pixel tolerance, invalid client
    packets/forged varps, two simultaneous players, and atomic solve;
11. helmet at Keldagrim/Resource Area/ordinary anvils before 80, at 80, and
    after 110; Smithing 49/50/boosted/drained, hammer/bars, cancel, inventory/
    equipment/bank duplicates, drop trick, 30 XP, diary, Wear at Defence 29/30;
12. first launch with missing/unequipped helmet, Defence 29/30/boosted/drained,
    missing/complete schematic, accept/refuse, inventory gold ore and bars,
    cutscene skip/logout/reconnect, exact landing, and Claustrophobia once;
13. timer warnings at every authored tick, relog non-extension, immediate
    helmet-removal exit, timeout, teleport, world-hop, death, inventory-full
    helmet outcome, gold absorption, and no stale timer/NPC;
14. flame with 0–5, 6–14, and 15+ ore; dropping/mining ore during combat;
    blue/green/yellow variants; every highest-stat/tie/random style; melee,
    Ranged, Magic, prayers, regeneration, and taunts;
15. two or more simultaneous realm owners, kill stealing, duplicate death
    callback, timeout-versus-kill race, player death, flee/re-entry, and exactly
    one 90→100 transition for the credited owner;
16. finale with 0/1+ free slots, rune pickaxe inventory/ground pickup, duplicate
    click/queue/relog, exact XP/QP/count, retained helmet, scroll/jingle, and
    state 110;
17. post-quest Dondakan and cannon with helmet worn/unworn, mine entry without
    timer, helmet removal/logout/world-hop exit, first/repeat In Between unlock,
    and repeated ordinary/boots dialogue with no rewards;
18. ore hauler for 0/1/5/10/25/26/28 ores with and without worn charos, rounding,
    confirmation/cancel, bank capacity, atomic failure, duplicate packet, and
    the correct `%dwarfrock_gold_boatman_met` owner;
19. every charged normal/imbued ring at 1–5 charges, uncharged variants,
    completion locked/unlocked, Rub and worn Dondakan subaction, one-charge
    decrement, exact landing, Wilderness limits, cancel/failure, and other ring
    destinations unchanged;
20. both music tracks locked immediately before and unlocked only at their
    authored launch/post-quest-entry events, plus relog/manual playback and
    unrelated `musicmulti_10` bits; and
21. `::complete quest_betweenarock` twice, coherent transforms with no
    production rewards, then diary, Khorvak/Forgettable Tale, Dwarf Cannon,
    mining, furnace, scorpion drop, ferry, ring, and music regressions.

### Live-client evidence

Capture a clean eligible account from the Troll Stronghold route through all
post-quest services without state/debug commands. Evidence must include:

- both canonical prerequisite refusals, explicit start acceptance/refusal,
  current two-coin/charos ferry behavior, Librarian/Engineer/Rolad route, and
  first/repeat travel subjects;
- retry-based page acquisition, partial/combined pages, full lore reading and
  replacement, Dondakan's granite demonstration, gold ore/bar acceptance,
  perfect-gold refusal, furnace menu, and complete cannonball cutscene;
- all four schematic owners, overlapping Khorvak quest subjects, full native
  overlay puzzle, Resource Area and ordinary-anvil helmet creation, diary event,
  and recovery/full-inventory paths;
- protected player-launch sequence, carried-gold absorption, every timer
  warning, immediate helmet exit, all three Avatar styles and gold tiers,
  regeneration, owner isolation with two clients, death/flee/re-entry, and
  cleanup;
- exact one-time finale rewards including ground rune pickaxe behavior and
  repeated post-quest dialogue proving no XP/QP/count duplication; and
- no-time-limit post-quest mine entry, helmet/logout exits, In Between,
  ore-hauler fees with/without charos, and charged normal/imbued ring-of-wealth
  Dondakan teleport with charge consumption.

Only after the Dwarf Cannon dependency, static checks, automated matrices, pack
validation, and live-client evidence pass may this record change from
`audit-pending` to `verified-modern`.
