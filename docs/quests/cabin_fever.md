# Cabin Fever modernization audit

Status: `audit-pending` — the quest row, four native state carriers, battle
maps, ship geometry, multilocs, quest items, broad phase order, journal,
completion service, cheat arm, and correct XP quantities exist. The canonical
route is not faithfully playable: the visible fuse-lighting target has no
handler, the primary state ladder disagrees with the cache/Quest Helper at
several report phases, the cannon is marked loaded as soon as it is repaired,
shots are guaranteed counters rather than aimed attacks, completion occurs on
the battle map without the Mos Le'Harmless arrival, and every permanent travel
operation is unhandled.

Audited: 2026-08-17

Governing plan: [Quest modernization plan](../QUEST_MODERNIZATION_PLAN.md). This
record applies that plan's Gates A–D to requirements, acceptance, dock and
battle-map transport, rope swings, sabotage, repairs, lockers, plunder,
cannon operation, death/logout/leave cleanup, completion, rewards, Mos
Le'Harmless access, Book o' piracy recovery, charter pricing, cave horrors,
Trouble Brewing, The Great Brain Robbery, the Morytania Diary, the journal,
and the cheat adapter. It is an implementation specification, not completion
evidence.

## 1. Authoritative references

These pinned OSRS Wiki revisions define the currently documented quest route,
dialogue, battle mechanics, rewards, and permanent unlocks.

| Reference | Pinned revision | Use in this audit |
| --- | --- | --- |
| [Cabin Fever](https://oldschool.runescape.wiki/w/Cabin_Fever?oldid=15295175) | 15295175, 2026-08-12 | Identity, requirements, complete route, battle behavior, rewards, and dependencies |
| [Cabin Fever/Quick guide](https://oldschool.runescape.wiki/w/Cabin_Fever/Quick_guide?oldid=14478674) | 14478674, 2023-10-13 | Exact action order, inventory needs, plunder route, cannon sequence, and completion |
| [Transcript:Cabin Fever](https://oldschool.runescape.wiki/w/Transcript%3ACabin_Fever?oldid=15263353) | 15263353, 2026-07-14 | Accept/refuse/re-talk branches, checkpoint dialogue, failure feedback, arrival scene, Mama, and post-quest coin claim |
| [Bill Teach](https://oldschool.runescape.wiki/w/Bill_Teach?oldid=15297848) | 15297848, 2026-08-13 | Start, ship travel, reward follow-up, and Book o' piracy relationship |
| [The Adventurous](https://oldschool.runescape.wiki/w/The_Adventurous?oldid=14341649) | 14341649, 2022-11-07 | Quest ship identity and travel contract |
| [Cannon (Cabin Fever)](https://oldschool.runescape.wiki/w/Cannon_%28Cabin_Fever%29?oldid=15241817) | 15241817, 2026-06-28 | Repair, load/fire/clean cycle, explosion, targeting, and Ranged-scaled hit chance |
| [Fuse (Cabin Fever)](https://oldschool.runescape.wiki/w/Fuse_%28Cabin_Fever%29?oldid=15184796) | 15184796, 2026-04-22 | Sabotage and cannon fuse ownership |
| [Plunder](https://oldschool.runescape.wiki/w/Plunder?oldid=15184802) | 15184802, 2026-04-22 | Enemy-hold loot and storage ownership |
| [Repair plank](https://oldschool.runescape.wiki/w/Repair_plank?oldid=15184804) | 15184804, 2026-04-22 | Hull-repair item contract |
| [Tacks](https://oldschool.runescape.wiki/w/Tacks?oldid=15184805) | 15184805, 2026-04-22 | Hull-repair quantities and stack behavior |
| [Book o' piracy](https://oldschool.runescape.wiki/w/Book_o%27_piracy?oldid=15295134) | 15295134, 2026-08-12 | Completion grant, use, loss, and replacement |
| [Mos Le'Harmless](https://oldschool.runescape.wiki/w/Mos_Le%27Harmless?oldid=15295177) | 15295177, 2026-08-12 | Island access and post-quest services |
| [Cave horror](https://oldschool.runescape.wiki/w/Cave_horror?oldid=15303930) | 15303930, 2026-08-17 | Kill/task unlock and cave access |
| [Charter ship](https://oldschool.runescape.wiki/w/Charter_ship?oldid=15264112) | 15264112, 2026-07-15 | Cabin Fever price reduction |
| [Trouble Brewing](https://oldschool.runescape.wiki/w/Trouble_Brewing?oldid=15276989) | 15276989, 2026-07-28 | Minigame access dependency |
| [The Great Brain Robbery](https://oldschool.runescape.wiki/w/The_Great_Brain_Robbery?oldid=15301585) | 15301585, 2026-08-14 | Direct downstream quest dependency |
| [Morytania Diary](https://oldschool.runescape.wiki/w/Morytania_Diary?oldid=15280663) | 15280663, 2026-07-29 | Medium-diary Trouble Brewing task dependency |

The sources were resolved through the OSRS Wiki API on 2026-08-17. They
identify Cabin Fever as quest #98, an experienced, short, members quest and
the second Pirate-series quest, released 7 February 2006. Starting requires
Pirate's Treasure, Rum Deal, Priest in Peril, and unboosted 42 Agility, 45
Crafting, 50 Smithing, and 40 Ranged. The guide recommends at least 11 open
inventory slots and food for the level-57 pirates; it does not make empty
slots a start requirement.

The reward contract is 2 quest points, 7,000 XP each in Crafting, Smithing,
and Agility, a Book o' piracy, access to Mos Le'Harmless and cave horrors,
halved charter-ship prices, and Trouble Brewing access. The 10,000 coins are
claimed by speaking to Bill after the quest rather than inserted into the
completion transaction.

Transition aid only: the local Quest Helper checkout's
[`CabinFever.java`](https://github.com/Zoinkwiz/quest-helper/blob/5ea99d5ea9ba3fb096ebe7b5ed02d80883e9819d/src/main/java/com/questhelper/helpers/quests/cabinfever/CabinFever.java)
at commit `5ea99d5ea9ba3fb096ebe7b5ed02d80883e9819d` confirms the native
0/10/20/30/40/50/60/70/80/90/100/110/120/130 ladder, all battle-map
coordinates, native loc/item names, four-rope sabotage route, three repair
transactions, plunder/storage loop, intact/loaded/dirty cannon requirements,
three prerequisites, four skills, rewards, and unlocks. It is a transition
test aid and does not override the Wiki, transcript, or cache.

`python3 tools/questhelper_extract.py cabinfever --check` exits 0. It resolves
the quest row, 16 item names, both active quest NPCs, 24 locs, 15 varbits, and
all listed coordinates. Symbol resolution does not establish correct packet
binding, transition semantics, encounter isolation, or world reachability.

The 2023 Sailing-integration design post is not an implementation authority
for this cache. The pinned current quest and cannon pages still describe the
bespoke Cabin Fever encounter. Do not substitute proposed Sailing facilities
unless the target live revision, cache assets, and authoritative route all
change together.

## 2. Native quest identity and player contract

| Field | Native value / expected behavior |
| --- | --- |
| Cache quest ID | 104 |
| Type | Members quest; Pirate series #2 |
| Difficulty / length | Cache 2 / 1; Wiki experienced / short |
| Release date | 7 February 2006 |
| Start | Bill Teach in The Green Ghost, Port Phasmatys |
| Start requirements | Pirate's Treasure, Rum Deal, Priest in Peril; base 42 Agility, 45 Crafting, 50 Smithing, 40 Ranged |
| Recommended capacity | 11 open slots and food; warn but do not encode as eligibility |
| Primary state | `%fever_quest`, transmitted permanent varp |
| Battle state | Native varbits on transmitted permanent `%fever_cannon_var`, `%fever_extra_var`, and `%fever_storage_var` carriers |
| Quest points | 2 |
| XP | 7,000 Crafting, 7,000 Smithing, 7,000 Agility |
| Completion item | Book o' piracy, with replacement path |
| Follow-up item | 10,000 coins from post-quest Bill, once |
| Unlocks | Mos Le'Harmless, cave horrors/tasks, halved charter prices, Trouble Brewing, The Great Brain Robbery dependency, Morytania Diary route |
| End state | 140 |

The quest dbrow has the correct identity, release date, series, start NPC,
skills, 2 QP, XP fields, and end state. Its `requirement_quests` values resolve
to `quest_contact` (124) and `quest_soulsbane` (108), neither of which is a
Cabin Fever prerequisite. Runtime must ignore those corrupt rows and check the
three real prerequisites explicitly. Rum Deal currently checks Priest in
Peril itself, but relying on that transitive history is unsafe for imports,
cheats, repair tools, and tests.

The quest header's rationale for omitting Priest in Peril is stale. Priest in
Peril now owns its 10–60 essence hand-in and completion path in
`mausoleum_drezel.rs2`, while Rum Deal hard-gates on state 60. Modernization
must remove the stale claim and enforce the canonical requirement directly.

### Primary state inventory

| State | Canonical phase | Current use / mismatch |
| ---: | --- | --- |
| 0 | Talk to Bill; requirements and accept/refuse | Requirements omit Priest in Peril; dialogue auto-accepts after requirements without a choice |
| 10 | Board at Port Phasmatys and talk to dock-side Bill | Present; Talk-to immediately sails without readiness choice |
| 20 | Set-sail/cutscene transition | Never written; collapsed into direct 10→30 teleport |
| 30 | Sabotage enemy powder barrel | Present, but opening battle/cannon damage setup is absent |
| 40 | Return and report successful sabotage | Current Talk-to writes 40 and immediately treats it as the repair phase, so Quest Helper continues pointing to Bill |
| 50 | Repair Bill's three hull holes | Never written; current repair phase runs at 40 |
| 60 | Return and report completed repairs | Never written; current code jumps from report at 40 directly to 70 after repairs |
| 70 | Collect and store ten plunder | Present |
| 80 | Return and report stored plunder | Never written; current report jumps 70→90 |
| 90 | Fetch barrel and repair cannon | Present until item use, but repair leaves primary state at 90 |
| 100 | Return and report repaired cannon | Never written; current report jumps 90→110, so helper cannot see its talk phase |
| 110 | Fire canisters at enemy pirates | Present initially |
| 111/112/113 | Not canonical primary states | Invented shot counters; they bypass the native pirate/accuracy model and are absent from Quest Helper |
| 120 | Return after three pirate kills | Never written; current state 113 Talk-to jumps to 130 |
| 130 | Fire cannonballs until three successful hull hits; success completes automatically | Present as guaranteed counter, then requires an extra Bill conversation to complete |
| 140 | Arrival, reward, island access, complete | Written on the battle map before grants; no arrival or permanent transport |

The skipped report states are not cosmetic. Quest Helper selects different
world/navigation actions at 40, 60, 80, 100, and 120, and the native cannon
and NPC transforms rely on phase-correct values. The modernization must use
the full ladder rather than preserving the local collapsed constants.

### Battle-side state inventory

| Carrier / varbit | Native meaning | Current ownership problem |
| --- | --- | --- |
| `%fever_cannon` (bits 1–4) | Cannon loc state: intact 0, broken 1, missing barrel 2, loaded/armed 3 | Repair writes 3 directly, conflating repaired with fully fused/ready; it never returns to intact 0 |
| `%fever_cannon_powder` (bit 5) | Powder loaded | Written, but not transactionally coupled to loc state |
| `%fever_cannon_tamp` (bit 6) | Powder tamped | Written; ramrod item is not consumed, correctly |
| `%fever_cannon_ammo` (bits 7–8) | 0 empty, 1 ball, 2 canister | Written, with phase-only wrong-ammo refusal |
| `%fever_cannon_fuse` (bit 9) | Fuse inserted | Written, but native armed loc state is not transitioned when fuse is inserted |
| `%fever_cannon_clean` (bit 10) | Fired cannon requires cleaning | Never written or read |
| `%fever_cannon_gonna_blow` (bit 11) | Invalid/danger state used by reset flow | Never written or read |
| `%fever_cannon_accuracy` (bits 12–26) | Cannon aim/accuracy presentation | Never written or read |
| `%fever_enemy_cannon` (bit 27) | Enemy cannon visual state: 0 intact, 1 destroyed | Written to out-of-range value 2, so it cannot select the destroyed leaf correctly |
| `%fever_holes_in_the_hull` (bits 28–31) | Successful enemy-hull hits 0–3 | Every shot increments; no Ranged roll or miss |
| `%fever_swing`, `%fever_swing_2` | Rope-swing presentation/progress | Never written |
| `%fever_hole_1/2/3` | 0 leak, 1 planked, 2 waterproofed | Correct broad per-hole sequence |
| `%fever_holes_patched/proofed` | Aggregate repair counters | Incremented independently; never reconciled against the per-hole source of truth |
| `%fever_gunpowder_barrel` | 0 intact, 2 fused, 1 exploded | Correct values, wrong lighting target/sequence owner |
| `%fever_fuse_1/2` | Visible fuse pieces | Both set on attachment; no distinct lighting handler |
| `%fever_plunder_points` | Stored plunder count | Written and clamped to 10; surplus is deleted |
| `%fever_crate/chest/barrel` | Per-container exhausted state | Made permanent for the whole quest; no canonical timed/world reset |
| `%fever_given_book` | Book grant ownership | Set only if completion inventory has space; no later recovery owner |
| `%fever_gold` | Post-quest coin ownership candidate | Never used; coins are granted at completion instead |

All four local carrier overlays are `scope=perm`. Battle-attempt state should
not survive forever after success, abandon, death, or repair tooling. Preserve
the cache's permanent layouts where required by client transforms, but give
the modern encounter one explicit initialization, cleanup, migration, and
post-completion policy.

## 3. Implementation surface

Paths below are relative to `OSRS-Content/osrs239-content/`.

### Quest-owned files

| Path | Present responsibility | Audit result |
| --- | --- | --- |
| `server/scripts/quests/quest_cabinfever/configs/cabinfever.constant` | State constants, rewards, coordinates, extensive provenance comments | Correct XP quantities and several thresholds; primary ladder is collapsed, cannon state 3 is mislabeled repaired/ready, Priest in Peril note is stale, and comments claim combat behavior that code does not implement |
| `configs/cabinfever.varp` | Four native carrier overlays | Native/transmitted/permanent, but no lifecycle reset contract |
| `configs/cabinfever.npc` | Adds Talk-to to three pre/in-quest Bill types | Does not cover either post-quest Mos Le'Harmless Bill leaf or non-Talk-to operations |
| `scripts/cabinfever_shared.rs2` | Requirement gate and completion | Missing Priest in Peril; non-atomic completion; wrong coin timing; lossy book grant; no arrival/cleanup |
| `scripts/cabinfever_bill.rs2` | Start, sail, all battle checkpoints | Broad order only; missing choices/cutscenes/native report states; final talk completes from wrong place |
| `scripts/cabinfever_transport.rs2` | Dock gangplank and rope swing | Exit gangplank is a message-only no-op; rope is not consumed; no failure/energy/presentation/cleanup/post-quest travel |
| `scripts/cabinfever_sabotage.rs2` | Fuse attachment and explosion | Tinderbox is handled on powder-barrel wrapper instead of the separate visible fuse target |
| `scripts/cabinfever_repair.rs2` | Three two-stage hull repairs | Core quantities work; no animations; aggregate/source reconciliation and duplicate-event safety are absent |
| `scripts/cabinfever_lockers.rs2` | Rope, repair-kit, barrel, ammunition supply | Wrong rope quantity; unsafe capacity tests; partial holdings duplicate full kits; cannon supplies can overfill by eleven slots |
| `scripts/cabinfever_loot.rs2` | Three loot sources and storage | Fixed one-pass 4/3/3 shortcut; no respawn; deletes surplus; no leave/death loss policy |
| `scripts/cabinfever_cannon.rs2` | Barrel replacement, loading, firing | Conflates repaired/armed state; no Fire/Inspect/Empty-Out handlers, cleaning, explosion, target, hit roll, pirate damage, projectile, or automatic completion |
| `scripts/cabinfever_journal.rs2` | Journal | Omits prerequisite, repeats nonexistent sail-cutting, cannot express native report states or side-state recovery |

The quest root totals 944 lines across twelve files. Its comments disclose
several shortcuts, but also describe an `ai_queue3` death-hook approach that
does not exist anywhere in the Cabin Fever files. A full production search
finds no Cabin Fever pirate death hook. The actual cannon simply increments
the primary quest value once per canister shot.

### Mandatory shared and cross-directory surfaces

| Path / subsystem | Relationship | Audit result / required ownership |
| --- | --- | --- |
| `configs/all.dbrow` | `quest_cabinfever` | Identity/rewards/end state are sound; prerequisite quest rows are corrupt |
| `configs/all.varbit` | 26 native battle/side fields | Layout is available; clean, danger, accuracy, swing, gold, and parts of fuse/cannon presentation are unused |
| `configs/all.loc` | Gangplanks, sails, lockers, holes, loot, fuse, barrel, cannon, island gangplanks | Native menus and transforms already encode the intended actions; several exposed ops have no handler |
| `configs/all.npc` | Port, battle, island, and ship Bill types | Battle Bill exposes `Return-Home`; island ship Bill exposes `Travel`; neither operation is handled |
| `configs/all.obj` | Rope, tinderbox, fuse, repair items, plunder, cannon supplies, book | Items resolve; most quest items have no centralized loss/leave cleanup or recovery contract |
| `areas/world/configs/m58_54.spawn` | Dock-side ship Bill | Wrapper is placed and usable from state 10; completion transforms it to an unhandled post-quest leaf |
| `areas/world/configs/m28_75.spawn` | Static battle-map Bill, enemy pirates, and ambient NPCs | Public map is always populated; no per-player/private encounter owner or enemy reset exists |
| `areas/world/configs/m57_46.spawn` | Island Bill and island ship Bill | Both are always placed, but neither has a production Talk-to/Travel handler |
| `interface_questjournal/scripts/quest_journal.rs2` | Journal dispatcher | Correctly calls `~cabinfever_journal` |
| `quests/scripts/quest_cheat.rs2` | Generic completion adapter | Writes only 140; does not normalize side state, move the player, grant/recover the book, arm coin claim, or unlock transport owners |
| `quest_rumdeal` and `quest_priestperil` | Prerequisite implementation | Both now have real completion gates; Cabin Fever's omission rationale is obsolete |
| `quest_thegreatbrainrobbery` | Direct downstream quest | Its current start deliberately soft-skips Cabin Fever, so this prerequisite remains unenforced |
| Mos Le'Harmless world/shops/cave | Headline access reward | World NPCs, shops, bank, and cave horrors exist, but no Cabin Fever transport gate delivers or returns the player |
| Charter transport | Price unlock | No production Cabin Fever reader was found; reward is presentation-only |
| Trouble Brewing | Minigame/diary unlock | Destination symbol exists, but no full minigame/access owner reading Cabin Fever was found |

### Cache-native assets already available

- the full primary varp and 26 named battle/side varbits;
- intact, broken, missing-barrel, loaded, enemy-cannon, hull-hole, loot,
  locker, fuse, gangplank, sail, ladder, and climbing-net loc families;
- all quest items, including separate floor rope/tinderbox supplies and the
  Book o' piracy;
- Port Phasmatys, battle-map, and Mos Le'Harmless Bill NPC variants, including
  Return-Home and Travel menu ops;
- both ships' three planes, enemy pirates, island, inn, cave, and permanent
  island world maps; and
- the current journal and quest-completion services.

Modernization should connect these assets using a protected encounter
session, phase-correct native var writes, atomic item transactions, loc-op
dispatch, combat/projectile services, cutscene/movement cleanup, and shared
transport/unlock predicates. It should not invent another progress counter or
retain message-only navigation as an accepted implementation.

## 4. Native reachability and first blockers

The start and dock route can reach the battle map. The first canonical hard
block is sabotage:

1. Adding `fever_fuse` to `fever_multi_gunpowder_barrel` sets the barrel to 2
   and makes both fuse visuals visible.
2. The cache and Quest Helper then direct the player to use a tinderbox on
   `fever_multi_fuse_2` at 1824,4831,1.
3. Production has no `oplocu,fever_multi_fuse_2` handler. Its only tinderbox
   case is attached to `fever_multi_gunpowder_barrel`.
4. The canonical visible action therefore cannot explode the barrel. A player
   may progress only if the client/server happens to route a noncanonical
   tinderbox-on-barrel action to the wrapper.

Independent route defects remain even if that hidden action works:

| State | Current reachability / defect |
| ---: | --- |
| 0 | Real Bill starts the quest after two quest and four skill checks; no Priest in Peril check, refusal, second confirmation, or capacity warning |
| 10 | Dock Bill immediately teleports and writes 30; no ready choice, opening attack cutscene, cannon initialization, side-state initialization, or session allocation |
| 30 | Two ropes act as infinite ropes; canonical fuse-light action is unhandled; leaving/re-entering has no attempt cleanup |
| 40 | Current repair phase runs under the canonical report state; Quest Helper remains on Bill; repair-locker capacity and duplication can prevent a coherent kit |
| 70 | One pass produces exactly ten plunder; container states never respawn; storage deletes all held surplus and leaving does not remove plunder |
| 90 | Barrel can repair the cannon, but writes cannon state 3 (loaded) while leaving primary state 90; native/helper report state 100 is absent |
| 110 | Item sequence can be entered, but native Fire is unhandled; using tinderbox as an item directly on the cannon guarantees one fictional kill and writes 111/112/113 |
| 113 | Noncanonical state has no Quest Helper step; Bill manually advances to 130 |
| 130 | Every tinderbox-on-cannon cycle guarantees a hull hit; after three, the player must speak to Bill instead of success completing the encounter |
| 140 | Player remains on the public battle map; no return-home op, Mos Le'Harmless arrival, Mama scene, island transport, deferred reward recovery, or cleanup |

The battle Bill already exposes `op3=Return-Home`, but there is no
`opnpc3,fever_quest_ship_teach` owner. The dock gangplank exit similarly only
prints text and does not move the player. These are not presentation defects:
they strand players unless an unrelated teleport is available.

## 5. Current versus required playable route

| Chapter | Current route | Modernized route contract |
| --- | --- | --- |
| Start | Compressed pitch, automatic Yes, state 10 | Explicit eligibility messages; accept/refuse and second confirmation; no write before acceptance; warn about inventory/combat |
| Board/set sail | Talk-to teleports to a static map and writes 30 | Gangplank in/out works; readiness choice; allocate/reset encounter; play opening battle; land on correct state 20/30 and cannon visuals |
| Obtain supplies | Two reusable ropes, one fuse; no floor tinderbox route | Locker/floor sources grant/recover exactly what the phase needs with capacity checks and no duplication |
| Rope swing | Distance-based teleport; rope retained; always succeeds | Consume one rope per crossing, use the authored climb/swing path, apply failure/swim/run-energy behavior, and recover safely on interruption |
| Sabotage | Fuse on barrel, then noncanonical tinderbox on barrel; instant message | Fuse on barrel, tinderbox on visible fuse, lighting success/failure as authoritative, explosion cutscene, enemy cannon state, atomic phase transition |
| Report sabotage | Writes 40 then immediately expects repairs | Explosion reaches report state 40; Bill conversation commits 50 |
| Repair hull | Generic Repair twice per hole with correct item totals | Retain native op sequence and 2 planks/10 tacks/1 paste each; animate; transact atomically; source aggregate counters from per-hole state |
| Report repair | Jumps directly 40→70 | Repairs reach report state 60; Bill conversation commits 70 |
| Plunder | Fixed 4/3/3, permanently exhausted containers | Canonical 3/2/1 first pass, respawn/world reset behavior, up-to-ten storage, surplus handling, and leave/death cleanup |
| Report plunder | Jumps 70→90 and marks cannon broken | Store/report uses state 80 then 90; opening battle owns initial damaged visuals; report does not invent unrelated state |
| Repair cannon | Barrel use changes broken 1 directly to armed 3 | Replace barrel through missing-barrel animation and settle at intact 0; state 100 owns Bill report |
| Canister phase | Tinderbox item-use guarantees three progress increments | Load powder→tamp→canister→fuse; native Fire op; require pirate in field of fire; hit/kill real target; clean after every shot; state 120 only after three kills |
| Cannon failure | Wrong ammo refused; no dirty state or explosion | Empty-Out supports recovery; dirty reload explodes, deals canonical damage, removes barrel, and returns to repairable state without corrupting counters |
| Cannonball phase | Every shot increments hull counter | Native Fire op with Ranged-scaled hit roll, miss feedback/projectile, exactly three successful holes, clean cycle, retry supplies |
| Completion | Extra Bill talk on battle map writes 140, grants coins/XP/book opportunistically | Third successful hull hit triggers finale, island inn/Mama arrival, book and XP/QP transaction, encounter cleanup, and coherent post-quest location |
| Post-quest | Port/island Bill and gangplanks unhandled | Bidirectional Travel/Return-Home/gangplank ownership, once-only 10k claim, book replacement, and durable unlock gates |

## 6. Item, capacity, and cleanup transactions

### Lockers and recoverability

The repair locker tests `inv_freespace(inv) < 4`, then unconditionally adds one
hammer, six non-stackable repair planks, one tack stack, and one swamp-paste
stack. From an empty requirement set that needs nine slots, not four. If the
player still owns some but not all requirements, the script adds the full
quantities again rather than the deficits. This can duplicate supplies and
still fail partway through the grant.

The gun locker has no free-space check during either firing phase. From no
supplies it attempts one ramrod, five non-stackable canisters or cannonballs,
and five non-stackable fuses: eleven slots. It also grants five whenever the
held count is below three rather than granting the deficit. Every locker grant
must be an all-or-nothing deficit transaction, with precise feedback and the
same result under duplicate packets.

The sabotage route tells the player to obtain four ropes before leaving, but
the locker gives only two. The four ropes cover the two sabotage crossings and
the later two plunder crossings, with an enemy-deck rope source for recovery.
Current progress works only because swings consume nothing. Modernization must
make rope consumption, phase quantities, failure loss, and recovery agree.

### Plunder

The first canonical pass yields six plunder: three from the chest, two from
the crate, and one from the barrel. Sources replenish on their own schedule,
and Quest Helper documents world hopping as the immediate reset route. The
current one-pass 3/4/3 distribution erases the wait/reset mechanic and makes
all three native exhausted bits permanent for the rest of the quest.

Storage must accept only the amount still needed. Current `oploc2` deletes all
held plunder, adds the full held count, then clamps the state to ten; any
surplus vanishes. The modern transaction should leave surplus until Bill's
authoritative report branch handles it. Teleporting, returning home, death,
logout/session teardown, or otherwise leaving the battle must enforce the
canonical temporary-plunder policy without deleting unrelated items.

### Completion rewards

`~cabinfever_quest_complete` writes 140 before any reward grant. It then adds
XP, attempts 10,000 coins, and gives the book only if one slot is free. A full
inventory can therefore complete with neither durable book entitlement nor a
recoverable pending branch; a full inventory without an existing coin stack
can also lose the immediate coin grant. Repeated completion packets are not
guarded inside the procedure and could repeat XP/coins before caller state
dispatch protects them.

Modern completion must:

1. validate state 130, three successful hull hits, and encounter ownership;
2. reserve or defer the Book o' piracy safely;
3. award each XP reward and 2 QP exactly once through the completion service;
4. set book entitlement and move the player through the island arrival;
5. arm, but not pay, the once-only 10,000-coin Bill conversation;
6. remove or normalize temporary battle items/state according to the live
   contract; and
7. become a no-op under duplicate success, reconnect, and repeat Talk-to.

`%fever_gold` and `%fever_given_book` are natural native ownership signals,
but their exact live semantics should be captured before assigning values.
Do not overload inventory presence as the sole entitlement record.

## 7. Cannon contract

The cannon is the largest old-machinery substitution in this port. Its native
loc family already exposes the intended state machine:

| Loc/state | Required operations |
| --- | --- |
| Intact cannon, `%fever_cannon=0` | Inspect and Empty-Out; accepts powder/ramrod/ammo/fuse in order |
| Broken cannon, `%fever_cannon=1` | Repair/strip damaged barrel |
| Missing-barrel cannon, `%fever_cannon=2` | Repair with replacement barrel |
| Armed cannon, `%fever_cannon=3` | Fire! |
| Dirty flag, `%fever_cannon_clean=1` | Ramrod clean before next powder cycle |
| Danger flag, `%fever_cannon_gonna_blow=1` | Empty-Out/recovery or explosion behavior as captured |

Current code binds only `oplocu,fever_multi_cannon`. There are no owners for
the cannon's visible Inspect, Empty-Out, Repair, or Fire operations. Adding a
fuse sets only `%fever_cannon_fuse`; it does not transition the cannon to
armed state 3. Conversely, replacing the broken barrel sets state 3 before
powder, tamping, ammunition, or a fuse exists.

The modern cannon should be one validated state machine, not separate phase
switches that happen to inspect the same bits. Every item use must validate
the immediately preceding state and apply one atomic mutation. Fire should:

- choose the loaded ammunition branch;
- validate aim and a live pirate target for canisters;
- validate the phase and perform the Ranged-scaled hull roll for cannonballs;
- play the correct animation, sound, projectile, impact, and miss feedback;
- kill/damage the real encounter entity or increment a successful hull hit,
  never increment primary progress merely because Fire was clicked;
- clear armed load fields and set the dirty flag;
- require ramrod cleaning before another powder load; and
- explode and return to barrel-repair state if the dirty-cycle rule is
  violated, without resetting earned pirate/hull credit.

Three canister hits should move 110→120 once, after which Bill moves 120→130.
Three successful cannonball hits should invoke the finale and 130→140 without
an extra report conversation. Misses consume the shot and preserve the phase.

## 8. Encounter, world, and unlock oversights

### Encounter lifecycle

The battle uses the always-populated public `m28_75` map. Per-player varbits
can individualize loc models, but enemy NPCs, targeting, damage, and reset are
not owned by a player session. The modern engine should create a protected
quest encounter or otherwise prove equivalent isolation. Its lifecycle must
cover allocation, re-entry, Return-Home, logout, disconnect, death, teleport,
completion, server restart, and abandoned/stale permanent state.

Initialization must derive all battle fields from the canonical primary
phase, not from whatever permanent bits a previous attempt or cheat left.
Cleanup must preserve durable quest progress while removing transient
entities/items and producing a safe Port Phasmatys or island destination.

### Permanent travel

At state 140, the Port Phasmatys wrapper resolves to
`fever_harmless_port_ship_teach`, whose cache menu is Talk-to/Travel. Mos
Le'Harmless separately spawns `fever_harmless_teach` and another
`fever_harmless_port_ship_teach`. None is covered by the quest NPC overlay or
any production trigger. The island gangplanks
`fever_gangplank_harmless`/`fever_gangplank_exit_harmless` also have no
handlers.

Implement one shared, location-aware Bill transport service with explicit
Cabin Fever completion checks, correct endpoints, aboard disembarkation, and
ordinary dialogue. Do not make the permanent headline reward depend on the
battle instance still existing.

### Downstream unlocks

A full production search finds no reader of `%fever_quest` outside the quest,
journal, and cheat scripts. Consequently:

- charter prices are not halved by Cabin Fever;
- Trouble Brewing has no confirmed access gate or complete minigame owner;
- cave horrors are spawned and combat gear behavior exists, but access to
  their island/cave is not delivered by quest transport and task assignment
  has no confirmed Cabin Fever predicate;
- The Great Brain Robbery's current implementation deliberately soft-skips
  Cabin Fever rather than enforcing it; and
- the medium Morytania Diary route cannot rely on a verified Trouble Brewing
  completion contract.

Modernization must centralize `cabinfever_complete` as the shared predicate
used by travel, charter pricing, Slayer/task access, minigame access,
downstream quest requirements, and diary checks. Each consumer needs a normal
completion and cheat/import completion test.

### Shared dialogue and journal

The Port Bill start trigger owns all Talk-to before completion and gives no
subject/refusal path. Post-completion Port and island Bill need deterministic
priority among ordinary travel, the 10,000-coin claim, book replacement, and
Pirate-series follow-ups. Test each menu operation independently; do not hide
Travel behind Talk-to text.

The journal omits Priest in Peril and says the player cuts the enemy sail,
although no sail-cut objective exists in the canonical route or production
mechanics. It also cannot represent fused-but-not-lit, partially repaired,
partial storage, dirty/broken cannon, missed shots, state 120 report, pending
coin claim, or book recovery. Rewrite it from primary and authoritative
side-state predicates after the state model is corrected.

## 9. Findings by priority

### P0 — canonical path, completion, and permanent access

1. Restore the full 0/10/20/.../130/140 primary state contract.
2. Bind tinderbox use to the visible `fever_multi_fuse_2` target and implement
   the sabotage/explosion transition and scene.
3. Replace the cannon shortcut with the native intact/broken/missing/armed,
   load/fire/clean/explode/repair state machine and real target/hit behavior.
4. Make dock exit, battle Return-Home, battle re-entry, and encounter cleanup
   functional and safe.
5. Complete automatically on the third successful hull hit, run the island
   inn/Mama arrival, and transact XP/QP/book exactly once.
6. Implement post-quest Bill and gangplank travel in both directions so Mos
   Le'Harmless access is durable.
7. Move the 10,000 coins to Bill's once-only post-quest branch and provide
   Book o' piracy recovery.
8. Make `::complete` normalize a coherent completed/unlocked state without
   granting repeat XP/coins or leaving a battle session.

### P1 — requirements, recoverability, and full mechanics

1. Enforce Priest in Peril explicitly and restore accept/refuse/re-talk and
   readiness dialogue.
2. Make all locker grants deficit-based, capacity-safe, atomic, and
   duplicate-packet safe.
3. Implement rope consumption, swing failure/swim, run-energy behavior,
   animations, and floor/locker recovery.
4. Implement canonical plunder amounts, source respawns/world reset, storage
   cap/surplus, and leave/death policy.
5. Animate and transactionally own each hull repair; reconcile aggregates
   from the three per-hole fields.
6. Implement Inspect, Empty-Out, Repair, and Fire loc ops plus wrong-order,
   miss, dirty, explosion, barrel-replacement, and supply-recovery branches.
7. Restore checkpoint, Mama, reward follow-up, travel, replacement, and shared
   Pirate-series dialogue.
8. Gate charter, Slayer/cave, Trouble Brewing, Great Brain Robbery, and diary
   consumers on one completion predicate.

### P2 — presentation, maintainability, and regression hardening

1. Restore the opening attack, sabotage, cannon-repair, projectile/impact,
   sinking, voyage, inn-arrival, camera, animation, music, and sound scenes.
2. Split cannon/encounter lifecycle from NPC dialogue while retaining one
   writer per transition and one transaction owner per item family.
3. Replace stale and contradictory provenance comments with measured state
   documentation after live capture.
4. Add quest speedrunning or Sailing-era integration only when target cache
   and authoritative live behavior support it.
5. Remove all shortcut claims only after the corresponding failure, cleanup,
   and re-entry tests pass.

## 10. Modernization work packages

### Package 0 — fixtures, migration, and state ownership

- Add executable fixtures for primary states 0, 10, 20, 30, 40, 50, 60, 70,
  80, 90, 100, 110, 120, 130, and 140, plus every meaningful side-state.
- Define authoritative predicates for eligibility, encounter membership,
  current objective, repair completion, stored plunder, cannon load/dirty/
  damage state, reward entitlement, coin claim, and permanent access.
- Add migration/repair rules for local saves in invented 111/112/113 states,
  state-40 repairs, state-90 repaired cannon=3, completed players without a
  book/coin entitlement, and stale battle items/bits.
- Record current live transforms and packet subjects before finalizing any
  uncertain fuse/enemy-cannon/gold semantics.

### Package 1 — start, transport, and encounter shell

- Modernize Bill's eligibility, acceptance, refusal, re-talk, capacity warning,
  dock boarding, disembarkation, and readiness dialogue.
- Allocate and initialize a protected battle encounter; implement opening
  attack/cutscene and exact state 20→30 ownership.
- Implement battle re-entry and Return-Home, with logout/death/teleport/
  disconnect/server-restart cleanup.
- Implement canonical rope sources, consumption, swing, failure, swim, run
  energy, landing, interruption, and both directions.

### Package 2 — sabotage, repairs, and plunder

- Bind fuse attachment and the separate visible-fuse lighting action; implement
  lighting failure if current behavior has it, then explosion/cutscene/report.
- Implement atomic per-hole repair actions and safe deficit-based locker
  recovery at states 50/60.
- Implement the 3/2/1 loot pass, independent respawns/world reset, ten-item
  storage, surplus/report behavior, and temporary-item cleanup.
- Restore the native 40→50→60→70→80→90 report/action progression and journal.

### Package 3 — cannon combat

- Implement broken→missing-barrel→intact repair and state 90→100→110.
- Implement ordered item loading, Inspect, Empty-Out, armed Fire, dirty
  cleaning, explosion/damage, and barrel recovery as one state machine.
- Add canister field-of-fire selection, real pirate targets, hit/death credit,
  projectiles, and exactly-once 110→120.
- Add cannonball Ranged roll, miss/impact feedback, three successful native
  hull holes, and automatic finale trigger.
- Test unlimited retry supplies without duplication or inventory corruption.

### Package 4 — completion and permanent contract

- Implement sinking/voyage/Other Inn/Mama arrival and an atomic 130→140
  completion with correct XP, QP, book, journal, and cleanup.
- Implement once-only Bill coin claim, Book o' piracy replacement, and all
  Port/island/aboard travel and gangplank operations.
- Connect charter price reduction, cave-horror/task access, Trouble Brewing,
  Great Brain Robbery, and Morytania Diary consumers.
- Upgrade the cheat/import adapter to the same durable access/reward ownership
  contract and test it twice.

Package 4 is part of Cabin Fever acceptance. An end-state write and reward
scroll that leave the player on the battle map cannot satisfy “access to Mos
Le'Harmless.”

## 11. Verification plan

### Static and build checks

- `rg` must find exactly one owner for every primary transition and every
  cannon/item transaction, including all visible fuse/cannon/Bill loc/NPC ops.
- Assert the full native primary ladder; reject production writers of
  111/112/113.
- Assert explicit Pirate's Treasure, Rum Deal, Priest in Peril, and four base
  skill checks at the start boundary.
- Resolve every NPC, loc, obj, varbit, animation, sound, map, and dbrow symbol
  against the intended osrs239 cache.
- Run `python3 tools/questhelper_extract.py cabinfever --check`.
- Run `make -C src mock230-scripts` and the intended-cache pack/check-only
  target after implementation.

### Automated transition and transaction tests

1. Exercise missing/every prerequisite and below/exact skill boundaries;
   decline/re-talk/repeated acceptance never writes early.
2. Board/disembark, decline/accept readiness, allocate once, replay duplicate
   packets, and verify exact 10→20→30 initialization.
3. Interrupt rope swings at every tick; test exact rope consumption, no rope,
   full inventory recovery, failure/swim, zero energy, death, logout, and both
   directions.
4. Attach a fuse, use tinderbox on the visible fuse, test wrong target/order,
   lighting failure/success, duplicate packets, explosion scene, and 30→40.
5. Report 40→50; repair holes in all orders; test each missing item, partial
   locker holdings, four/eight/nine free slots, duplicates, relog, and
   aggregate reconciliation; report 60→70.
6. Loot all source orders, wait each reset, change world, partly store, carry
   surplus, leave/re-enter, die/relog, and report exact ten through 70→80→90.
7. Repair cannon through 1→2→0; test missing barrel, full inventory, duplicate
   use, visible loc state, and report 100→110.
8. Exercise every legal/illegal load order, Inspect, Empty-Out, no target,
   aligned target, miss/hit, clean, dirty reload explosion, damage, destroyed
   barrel, recovery, and three real pirate kills into 120.
9. Report 120→130; exercise low/exact/high Ranged hit rolls with deterministic
   RNG fixtures, misses, clean/explosion loops, and exactly three successful
   hull hits.
10. Trigger finale with full/non-full inventory and duplicate success packets;
    verify one XP/QP/book grant, correct arrival, no battle residue, and state
    140.
11. Claim coins with full inventory, existing/no coin stack, repeat packets,
    relog, and after book loss/replacement.
12. Exercise Port→island→Port Travel, every gangplank, and Return-Home after
    normal completion, cheat/import completion, relog, and restart.
13. Verify halved charter price, cave access/task eligibility, Trouble Brewing,
    Great Brain Robbery, and Morytania Diary with state 139 versus 140.
14. Run `::complete quest_cabinfever` twice from clean and dirty side-state
    fixtures; first creates coherent access, second is a no-op.

### Live-client acceptance

Run one fresh-account-equivalent route from The Green Ghost through the Other
Inn and back to Port Phasmatys. Capture:

- every eligibility/refusal/acceptance/readiness branch;
- boarding, opening attack, battle re-entry, Return-Home, and disembarkation;
- locker/floor supplies and four real rope crossings, including one failure;
- visible fuse attachment, lighting, and explosion;
- all three two-stage repairs and partial-item recovery;
- plunder source amounts, respawn, storage, surplus, and leave policy;
- cannon barrel repair and every load/Fire/clean/explosion/Empty-Out state;
- canister alignment hit/miss and cannonball Ranged hit/miss;
- automatic finale, sinking/voyage/inn/Mama presentation, reward scroll,
  journal, item cleanup, book, and location;
- post-quest 10,000-coin claim, book replacement, and bidirectional travel; and
- representative charter, cave/task, Trouble Brewing, Great Brain Robbery,
  and diary unlock checks.

Repeat the route with deliberately full/partial inventories and with logout,
death, teleport, and reconnect at every phase boundary. No test may depend on
an unrelated teleport to escape a ship or receive the access reward.

## 12. Audit evidence and disposition

Evidence collected for this record:

- complete 944-line quest-root inventory and cross-directory production
  searches;
- quest dbrow inspection and corrupt prerequisite pack-ID resolution
  (`124=quest_contact`, `108=quest_soulsbane`);
- exact native varbit bit-range and multiloc/menu inspection;
- Port, battle-map, and island NPC spawn/menu/handler reconciliation;
- item stackability and locker capacity calculation;
- cannon visible-state, load, clean, danger, accuracy, and hit-counter audit;
- downstream transport, charter, Slayer, minigame, quest, diary, journal, and
  cheat-reader searches;
- OSRS Wiki API revision pinning and targeted article/reference review;
- local Quest Helper source review at the pinned commit; and
- successful `python3 tools/questhelper_extract.py cabinfever --check` symbol
  and coordinate resolution.

No gameplay code was changed and no compile, pack, automated transition, or
live-client run is claimed. Cabin Fever remains `audit-pending` until all P0
items, the relevant P1 recovery/mechanics items, and Gates A–D pass with
recorded evidence.
