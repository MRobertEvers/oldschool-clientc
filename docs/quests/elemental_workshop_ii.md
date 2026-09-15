# Elemental Workshop II modernization audit

Status: `audit-pending` — the cache-native quest row, permanent primary and
machinery state, entrance clue chain, elemental-metal supply, repair scripts,
priming state machine, extractor, workbench completion, dynamic journal, cheat
adapter, and reward call exist. The quest is nevertheless impossible through
normal play: its unlocked hatch has no cross-map destination, the return and
basement stairs do not implement their two-plane jumps, the jig cart has no
production spawn, and the crane-claw handler targets a crane variant that is
not placed. Beyond those blockers, the pipe puzzle is auto-solved, crate
contents are not player-randomised, cog mistakes are disallowed, the moving
machinery is text-only, repair prerequisites do not authorize the machines,
the cooling sequence is incomplete, Smithing boost/XP rules are wrong, and the
mind shield's completion equip gate is absent.

Audited: 2026-08-17

Governing plan: [Quest modernization plan](../QUEST_MODERNIZATION_PLAN.md). This
record applies that plan's Gates A–D to the direct quest root, Elemental
Workshop I's shared entrance/furnace/workbench, revision-239 map placements and
transforms, generic maplinks, the quest-list journal, equipment policy, the
Kandarin Diary consumer, loss/recovery, completion, and post-quest mind
equipment. It is an implementation specification, not completion evidence.

## 1. Authoritative references

The following current OSRS Wiki revisions are pinned so future implementation
and review use a stable route, dialogue, machine, item, recipe, reward, and
downstream contract.

| Reference | Pinned revision | Use in this audit |
| --- | --- | --- |
| [Elemental Workshop II](https://oldschool.runescape.wiki/w/Elemental_Workshop_II?oldid=15271178) | 15271178, 2026-07-21 | Quest identity, requirements, complete route, repair puzzles, machine sequence, rewards, and downstream diary relationship |
| [Elemental Workshop II/Quick guide](https://oldschool.runescape.wiki/w/Elemental_Workshop_II/Quick_guide?oldid=14955157) | 14955157, 2025-07-31 | Ordered interactions, materials, floor transitions, and concise recovery route |
| [Transcript:Elemental Workshop II](https://oldschool.runescape.wiki/w/Transcript%3AElemental_Workshop_II?oldid=15263361) | 15263361, 2026-07-14 | Start confirmation, item messages, schematic choice, wrong-machine feedback, and completion text |
| [Beaten book](https://oldschool.runescape.wiki/w/Beaten_book?oldid=15282320) | 15282320, 2026-07-30 | Quest-start reading, scroll/book ownership, and helmet instructions |
| [Elemental metal](https://oldschool.runescape.wiki/w/Elemental_metal?oldid=15275847) | 15275847, 2026-07-26 | One ore plus four coal, 20 boostable Smithing, six-tick smelt, and 8 Smithing XP |
| [Crane claw](https://oldschool.runescape.wiki/w/Crane_claw?oldid=15184180) | 15184180, 2026-04-22 | 20 boostable Smithing, four-tick workbench action, and 20 Smithing XP |
| [Primed bar](https://oldschool.runescape.wiki/w/Primed_bar?oldid=15183739) | 15183739, 2026-04-22 | Full crane/press/tank/fan state sequence, wrong-state recovery, and moving jig contract |
| [Primed mind bar](https://oldschool.runescape.wiki/w/Primed_mind_bar?oldid=15188345) | 15188345, 2026-04-22 | Extractor transaction and repeated-bar behavior |
| [Mind helmet](https://oldschool.runescape.wiki/w/Mind_helmet?oldid=15183129) | 15183129, 2026-04-22 | Partial-quest creation, beaten-book selection, 30 boostable Smithing, 30 XP, and Kandarin medium task |
| [Mind shield](https://oldschool.runescape.wiki/w/Mind_shield?oldid=15205410) | 15205410, 2026-05-03 | Slashed-book selection, 30 boostable Smithing, 30 XP, post-quest equip gate, and wyvern protection |
| [Battered key](https://oldschool.runescape.wiki/w/Battered_key?oldid=15261991) and [slashed book](https://oldschool.runescape.wiki/w/Slashed_book?oldid=15261994) | 15261991 / 15261994, 2026-07-12 | Shared Elemental Workshop I entrance and replacement ownership |
| [Key](https://oldschool.runescape.wiki/w/Key_%28Elemental_Workshop_II%29?oldid=15186084), [crane schematic](https://oldschool.runescape.wiki/w/Crane_schematic?oldid=15185548), and [lever schematic](https://oldschool.runescape.wiki/w/Lever_schematic?oldid=15185549) | 15186084 / 15185548 / 15185549, 2026-04-22 | Hatch-key lifecycle and readable repair/puzzle clues |

The sources identify Elemental Workshop II as quest number 112, released 2
October 2006. It is a short, intermediate, members' quest. Completion of
Elemental Workshop I is mandatory; 20 Magic and 30 Smithing are boostable, but
the Wiki still marks their exact quest-start enforcement as unknown. The quest
awards one quest point, 7,500 Smithing XP, 7,500 Crafting XP, and the ability to
make and use mind equipment. Making a mind helmet is a medium Kandarin Diary
task.

Transition aid only: the local Quest Helper checkout's
[`ElementalWorkshopII.java`](https://github.com/Zoinkwiz/quest-helper/blob/5ea99d5ea9ba3fb096ebe7b5ed02d80883e9819d/src/main/java/com/questhelper/helpers/quests/elementalworkshopii/ElementalWorkshopII.java)
and
[`ConnectPipes.java`](https://github.com/Zoinkwiz/quest-helper/blob/5ea99d5ea9ba3fb096ebe7b5ed02d80883e9819d/src/main/java/com/questhelper/helpers/quests/elementalworkshopii/ConnectPipes.java)
at commit `5ea99d5ea9ba3fb096ebe7b5ed02d80883e9819d` confirm primary
values 0–10, all crane/cart/tank/fan conditions, solved pipe values, object
coordinates, recipe inventory, and reward values. It guides state tests but
does not override the Wiki, transcript, or cache.

`python3 tools/questhelper_extract.py elementalworkshopii --check` resolves
every referenced item, NPC, loc, and varbit. Its only unresolved symbol is the
helper's guessed `quest_elementalworkshopii` dbrow name; the cache's real row is
`quest_elementalworkshop2`. This is an extractor alias mismatch, not a missing
quest row, and symbol resolution alone does not prove that actors are spawned
or transitions have destinations.

## 2. Native quest identity and contract

| Field | Native value / expected behavior |
| --- | --- |
| Cache quest row | `quest_elementalworkshop2`; dbrow pack index 39, quest metadata ID 119 |
| OSRS release-order number | 112 |
| Type / difficulty / length | Members / intermediate / short |
| Release date | 2 October 2006 |
| Start | Search the Exam Centre bookcase, receive the book/scroll, and explicitly accept the starting interaction |
| Required quest | Elemental Workshop I |
| Skills | 20 Magic and 30 Smithing, boostable; skill checks are not hard dbrow start checks |
| Required supplies | Hammer, battered key, two elemental metals or two ores plus eight coal; pickaxe if mining |
| Combat | Two level-35 earth elementals only when sourcing both ores during the quest |
| Primary state | `%elemental_quest_2_main`, bits 0–4 of permanent varp `elemental_quest_2_bits` |
| Machinery state | Native fields on permanent varps `elemental_quest_2_bits` and `_bits_2`; `_temp` is also cache-native |
| End state | 11 |
| Quest points | 1 |
| XP reward | 7,500 Smithing and 7,500 Crafting (`75000` raw tenths each) |
| Crafted result | One mind helmet; no separate reward-item grant |
| Unlocks | Continued primed-bar/mind-equipment creation; mind shield becomes wearable after completion |
| Downstream | Creating a mind helmet is a medium Kandarin Diary task |

The dbrow's `requirement_quests=38` is correct. Dbrow pack index 38 maps to
`quest_elementalworkshop1`; it is not quest metadata ID 38 (Waterfall Quest).
The direct constant's claim that this column is corrupt is therefore false and
must not drive a replacement schema. The runtime prerequisite check against
`%elemental_workshop_finished` is also legitimate, but modernization should
recognize it as the adapter for the already-correct dbrow dependency.

All primary and mechanical carriers are native permanent varps. No quest-owned
parallel varp and no C engine state are needed. The cache exposes:

- primary progress, key visibility, and hatch state;
- jig position and material phase;
- crane payload and arm position;
- three four-bit junction-pipe endpoints plus their aggregate field;
- repaired water pipe, inlet/outlet valves, tank door/grabber, and water level;
- three independently sized cog positions, aggregate air state, and fan state;
- extractor payload;
- crate state; and
- three two-bit temporary junction-pipe fields.

Modernization should preserve these fields and define named predicates over
them. It should not flatten the apparatus into the primary state or infer
authorization from one arbitrary subfield.

## 3. Implementation surface

The direct root contains 1,022 lines across eight files.

| Quest-owned path | Present responsibility | Audit result |
| --- | --- | --- |
| `configs/elementalworkshopii.constant` | Primary/substate constants and a long port rationale | Native values and XP are useful; several conclusions are demonstrably stale or false, including the corrupt prerequisite claim, allegedly unimplemented EW1 machinery, absent crane variants, fully played priming claim, and deferred mind-shield loop |
| `scripts/elem2_intro.rs2` | Bookcase, beaten book, scroll, boiler key, and hatch unlock | Real recovery checks exist; start ordering/dialogue and key visibility are noncanonical, and unlocking supplies no travel destination |
| `scripts/elem2_gather.rs2` | Earth-rock encounter and shared furnace binding | Correctly reuses EW1 ore drops and furnace, but comments misdescribe that dependency and owner/rate behavior needs live verification |
| `scripts/elem2_repair.rs2` | Schematics, claw, crane, pipe UI substitute, broken pipe, cogs, and crates | Substantial scaffold; wrong crane subject is a hard blocker, puzzle/crates/cogs are simplified, and arbitrary repair order can strand primary progress |
| `scripts/elem2_priming.rs2` | Crane/cart/press/tank/fan finite-state transitions | Uses native state, but the cart does not exist, scenery never moves, repair predicates are ignored, the water cycle is incomplete, and several recovery states differ from the cache/helper contract |
| `scripts/elem2_helm.rs2` | Extractor, mind bar, helmet/shield workbench, and completion | Native gun transforms work; machine authorization, boost/XP/timing, pre-completion shield selection, and completion invariant are wrong |
| `scripts/elem2_shared.rs2` | Started, workshop-repaired, and completion helpers | Journal predicate is fine; repaired test is incomplete and completion is not guarded/idempotent |
| `scripts/elem2_journal.rs2` | Dynamic journal for primary milestones | Correctly dispatched and better than an unwritten journal, but it ignores independent repair and in-flight machinery state |

Mandatory shared and cache surfaces:

| Path / subsystem | Relationship | Audit result / required ownership |
| --- | --- | --- |
| `quests/quest_elemental_workshop/` | Battered/slashed books, odd-wall access, earth-elemental death drop, waterwheel, bellows, furnace, and elemental shield | This implementation really does set waterwheel, bellows, and furnace flags. EW2's comments claiming the middle machinery is unwritten are stale. The shared furnace correctly consumes one ore and four coal and awards 8 XP, but its current-Smithing check and persistent-machine assumptions need cross-quest tests |
| `ladders_stairs/configs/maplink.dbrow` and generic climb scripts | Surface hatch, hatch return, basement descent/return, catwalk | No EW2 row exists. Generic ±1-plane movement cannot express a cross-map surface transition or the deep map's level 2 ↔ 0 stairs |
| `maps/m30_80.jl2` | Static deep-workshop geometry | Places the crane as `elem2_crane_lava_down_broken`, places repair/machine scenery, and places stairs; it cannot spawn the NPC jig cart |
| `areas/world/configs/*.spawn` | Production actors | No `elem2_cart_npc` spawn exists anywhere, so both use-bar and take-primed-bar triggers are unreachable |
| `configs/all.loc` / `all.npc` | Native visual variants | The cache includes all sixteen crane variants, six jig-cart NPC leaves, water-door variants, tank level, cog permutations, fan states, gun states, and repair transforms. Current code uses only a fraction |
| `interface_questjournal/scripts/quest_journal.rs2` | Quest-list dispatch | Correctly calls `~elem2_journal` for `quest_elementalworkshop2` |
| `quests/scripts/quest_cheat.rs2` | `::complete` adapter | Idempotently writes primary state 11 only; this is registry setup, not XP/QP/item/machinery proof |
| `quests/scripts/questpoints.rs2` | QP, completed count, jingle, and reward scroll | Modern shared presentation is used; it cannot enforce the quest's preceding item/state transaction |
| `player/scripts/equip.rs2` and `skill_combat/scripts/levelrequire.rs2` | Mind-shield wear policy | No mind-shield completion branch exists, so a bought shield is wearable before Elemental Workshop II despite the current item contract |
| `interface_diaries/` | Kandarin medium diary | Generic area/tier counters exist, but repository search finds no mind-helmet creation task hook |

## 4. Primary state and current reachability

| State | Canonical phase | Current implementation / defect |
| ---: | --- | --- |
| 0 | Search bookcase, receive book/scroll, and accept/refuse the starting interaction | Search immediately writes state 1 and gives only the book. The interaction has no transcript/QH combat warning or Yes/No acceptance; later Read manufactures the scroll |
| 1 | Quest accepted; read/retain beaten book and scroll | Reading is a one-message synopsis. The state/item ordering differs from the transcript but remains recoverable |
| 2 | Read scroll | Works and advances to 3; scroll is recoverable only while exactly at 2, which is sufficient after it has served its purpose |
| 3 | Enter workshop and search northern machinery | EW1 completion is checked only at the Exam Centre start; the shared staircase itself is not gated on battered key/book. Boiler gives/replaces the small key but never writes native `_hide_key`, so its visual leaf never changes |
| 4 | Use small key on central hatch | Key is consumed and hatch transform opens. No maplink or exact handler moves the player to the deep map, so ordinary play stops here |
| 5 | Descend and take both schematics | Deep map is unreachable normally. If teleported, crate gives both documents at once rather than the two-choice interaction; neither schematic has its native Read action implemented |
| 6 | Make one crane claw from elemental metal | Workbench silently routes a bar to the claw when its internal predicate matches. It requires base 30 Smithing instead of effective 20 and awards no 20 Smithing XP |
| 7 | Lower crane and fit claw | The map places `elem2_crane_lava_down_broken`, but item-use is bound only to unplaced `elem2_crane_track_up_empty`; state 8 is therefore unreachable without mutation |
| 8 | Repair junction, pipe, and cog train | One box click auto-solves the junction. Any crate can sequentially issue all four parts. Cogs can only be placed correctly. Progress recheck is absent after small/medium cog placement, so some valid repair orders never reach 9 |
| 9 | Run one elemental metal through all four stations and extract mind energy | No cart spawn exists. Even if supplied, machinery does not require repairs, remains physically static, and cooling omits required valve/water-level transitions |
| 10 | Smith mind helmet with beaten book | Workbench can complete from any pre-11 primary state if a mind bar/book are present. It requires base 30, awards no 30 Smithing XP, and does not model the four-tick action |
| 11 | Complete; repeat mind-equipment production | XP/QP/scroll values are right. Mind shield crafting exists despite comments saying it is deferred, but its equip gate, creation XP, and robust machine route do not |

### Hard travel and actor blockers

These are independent failures, not one symptom:

1. The surface hatch at approximately `0_42_154_32_35` must land in the
   separate `m30_80` workshop around `2_30_80_34_35`. The resolved open hatch
   only inherits generic `climb_down`, which tries plane -1 from surface plane
   0 and returns the blocked message.
2. `elem2_stairs1` must return from deep-map plane 2 to the surface map. Its
   generic climb instead moves to plane 3 at the same deep coordinate.
3. `elem2_stairs_door_open_no_hatch` and `elem2_stairs2` connect deep-map plane
   2 to plane 0. Generic climb moves only one plane, marooning the player on
   unused plane 1 in both directions.
4. Only `elem_gantry_stairs`/`_top` are naturally compatible with generic
   movement because their catwalk transition is a same-coordinate 2 ↔ 3
   change.
5. `elem2_cart_npc` has six cache leaves but no production spawn or script
   creation. The quest cannot accept an elemental metal or expose Take-from.
6. The static crane is ID/name `elem2_crane_lava_down_broken`; the claw script
   binds a different unplaced leaf. Lever-written varbits do not transform the
   static crane because the crane is not a multiloc.

An end-state write or direct teleport can hide these defects; neither is an
end-to-end quest test.

## 5. Detailed machinery and lifecycle audit

### Start, clue, access, and recovery

The transcript places the warning and acceptance after the book is found/read
within the starting interaction, while Quest Helper attaches its `Yes.` step to
the bookcase object interaction. Both agree that search supplies the book and
bookmark scroll and that Yes/No controls whether state 1 is committed. Current
search starts immediately, gives only the book, later Read manufactures the
scroll, and no refusal exists. This violates the observable start contract and
makes an accidental Search irreversible.

The bookcase has useful inventory/bank-aware beaten-book recovery, and the
boiler has inventory/bank-aware small-key recovery. Full-inventory paths avoid
advancement. Modernization should retain those properties while restoring the
canonical ordering and readable content. It must also gate the shared odd-wall
and spiral-stair access according to the battered-key/book policy already owned
by Elemental Workshop I rather than relying on deeper content to reject a
player after entry.

The small-key interaction writes `_hatch` but not `_hide_key`; consequently the
boiler's cache-authored found/empty transform is dead. The replacement leaf has
a handler, but ordinary state never exposes it. Use both fields for their
native visual meanings and keep replacement based on hatch/item ownership.

### Elemental ore and metal

The earth-rock handler verifies current Mining 20 and a pickaxe, then creates an
owner-bound earth elemental whose death drops owner-private ore. The room has
many rock NPC spawns; the one-tile duplicate search permits a player to wake
multiple elementals at distant rocks. Verify live OSRS concurrency and rock
respawn before choosing a cap, but keep ownership and guaranteed ore.

`~elem1_furnace` is real and is correctly shared. Contrary to EW2's comments,
EW1 implements water gates, waterwheel, bellows repair/lever, lava bowl, and
furnace lighting. A legitimately completed EW1 player should retain usable
machine state, while `::complete` alone does not prepare those substates. Test
both cases explicitly. The furnace's one ore + four coal, animation, delay,
effective-current Smithing 20 check, and 8 XP match the pinned elemental-metal
recipe and should be retained.

### Schematics and workbench dispatch

The canonical schematic crate presents a choice and lets the player take both
documents. Current code grants both in one click. More importantly, the crane
and lever schematics advertise Read but no `opheld1` handler exists. The crane
document explains the claw; the lever document is the clue for the junction
connections. A modern puzzle must restore both readings before expecting a
player to solve it.

The shared workbench has one item-use dispatcher, which is structurally the
right ownership pattern. Its policy is not:

- an elemental metal is forced into a claw whenever the fire state is broken
  and a crane schematic is in inventory, with no smithing menu;
- crane-claw creation checks base 30, while the recipe is boostable 20;
- the claw action is instant and awards no 20 Smithing XP;
- only inventory, not bank, is considered for an existing claw, allowing
  replacement/duplication behavior to diverge;
- after crane repair, elemental metal falls through to EW1's shield recipe;
- mind items check base 30 rather than effective boostable 30, act instantly,
  and award no 30 Smithing XP; and
- the pre-completion branch refuses to make a shield without a beaten book,
  whereas the canonical selection permits the slashed-book mind shield but
  does not complete the quest.

Model the actual workbench selection/menu and let book ownership plus player
choice determine valid output. Completion must be attached only to successful
mind-helmet creation at the proper quest phase.

### Crane repair and visual state

All sixteen track/lava × up/down × broken/empty/cold/hot crane locs are present
in the cache. The assertion that they do not exist confuses “no ready-made
multiloc wrapper” with “no assets.” The map begins with lava/down/broken. A
modern owner must reconstruct the visible loc from `%fire_state` and
`%fire_pos`, update it after each protected transition, and bind claw use to all
legitimate broken variants or to a shared crane category. The action must
consume one claw only after validating position, capacity/state, and successful
visual transition.

Text saying the crane moved is not sufficient: its location and payload are
the puzzle's state, click target, and feedback. Per-player varbits combined
with a shared static loc are unsafe for concurrent users unless the machinery
is instanced or the engine supplies player-local dynamic scenery.

### Junction box, water pipe, cogs, and crates

Opening the junction box currently sets `(5, 6, 13)` immediately. The cache
ships interface `ElemMagicpressPipes`, and Quest Helper identifies the six
endpoints and correct connections. A modern implementation needs the actual
connect/disconnect interaction, persistent partial state, validation, success
feedback, reopen/relog reconstruction, and no consumption. The repaired
predicate must validate the complete unordered set or a dedicated native fixed
bit—not only pipe 1 equal to 5.

The eight searchable crates all call one procedure that returns the first
missing item in fixed small/medium/large/pipe order. Repeatedly searching one
crate yields all four. Canonical locations are random per player among eligible
crates and a lost part is recovered from the same assigned crate. Implement a
stable player-specific assignment (native state, persistent quest data, or a
documented deterministic account hash) and retain inventory/bank plus installed
part recovery. `_box_state` alone has only eight values and does not encode all
four-of-eight assignments; do not pretend its current monotonic counter does.

Cache transforms allow any size cog on any of the three shafts and expose Take
on installed cogs. The correct arrangement is small upper-left, medium
lower-left, large right. Current handlers accept only those three correct
uses, so the puzzle cannot be configured incorrectly, diagnosed by the fan, or
corrected by taking cogs back. It also calls `~elem2_check_workshop_repaired`
only after placing the large cog. If large is placed before the remaining
cogs, with the pipe repairs already done, later small/medium placement never
rechecks and primary state remains 8. Put validation behind every relevant
transition and derive fan direction from actual arrangement.

### Priming state machine

The use of native fields is a valuable start, but the claim that the apparatus
is “fully played” is not supported by runtime ownership:

- the jig cart is absent, and its cache multinpc varies contents only; it does
  not encode track position;
- no script moves/spawns the cart among crane, press, tank, and tunnel;
- no script swaps the sixteen crane variants;
- the press runs even when the junction is unsolved;
- the tank cools even when the broken supply pipe is unrepaired;
- the fan runs even when cogs are missing or arranged incorrectly;
- most actions are immediate `mes()` plus varbit writes, with no protected
  movement, loc animation, sound timing, or busy-state exclusion; and
- the dry-cart Take-from handler checks content but not that the cart has
  returned to the starting station.

The water cycle is especially incomplete. `_water_level` is never written.
The current east-valve action jumps directly from hot to cool and opens the
outlet; the next west-valve action closes the inlet; no cool-state branch
closes the outlet before retrieval. This omits the fill/drain lifecycle and
disagrees with both the pinned fabrication route and Quest Helper's
water-level/valve conditions. Build one explicit transition table covering
door, grabber, both valves, water level, bar temperature, and repaired pipe.
Invalid actions should have truthful feedback and remain recoverable.

Primary state 9 is not itself used to authorize any apparatus action. Once the
hard blockers are repaired, a player could repair only the claw, then use the
press/tank/fan despite the skipped junction, pipe, and cogs. The extractor also
accepts a primed bar at any quest state, and the workbench completion path
accepts a mind bar at any state below 11. Every mutation needs a central phase
and substate predicate; the journal's recommended ordering is not an
authorization boundary.

### Extractor, repeat production, and equipment

The extractor gun's three-leaf native transform is correctly used. Loading
consumes the primed bar, the hat requires 20 current Magic, subtracts 20, and
Take-from checks one free slot before restoring the item. Those are sound
transactional pieces. Missing elements are phase authorization, protected
chair animation/camera/sound, busy-state handling, and repeated-bar/relog tests.

The workbench really does implement post-quest mind helmet and mind shield
creation, contradicting the constant's “deferred” note. It prioritizes the
slashed book after completion, which matches the shield rule, but lacks recipe
XP/timing and cannot be exercised reliably because the upstream factory is
broken. `elemental_mind_shield` is tradeable and the shared equip gate does not
check quest completion, so a player may buy and wield the unlock early. Add
the completion gate in the shared equipment owner; do not hide it inside the
crafting script.

### Journal, completion, and downstream credit

The quest-list dispatcher and detailed primary journal are both present. The
journal should be retained and upgraded to inspect each repair, cart phase,
cart position, crane payload/position, tank controls, extractor payload, and
owned quest item. Broad advice at states 8 and 9 is inadequate when those
phases have dozens of resumable substates.

Successful helmet creation currently adds the helmet, writes state 11, awards
both 7,500 XP rewards, then calls the shared QP/scroll helper. There is no
completion guard in `~elem2_finish`, so a malformed direct call can duplicate
rewards, and state is committed before the rest of the award sequence. Build
one idempotent transaction requiring state 10 plus the successful recipe,
granting recipe XP separately from quest XP, and committing completion exactly
once. The reward-scroll helmet is an icon for the item just crafted, not an
additional item grant.

Making the helmet must call the medium Kandarin Diary task owner only after a
successful creation. Quest completion alone is not the task event, and the
generic diary count is not a substitute. Repeated helmet creation must not
award the same task twice.

## 6. Oversight matrix

| Priority | Oversight | Consequence | Required correction |
| --- | --- | --- | --- |
| Critical | Surface hatch has no cross-map transition | Normal play stops immediately after using the small key | Add exact bidirectional surface ↔ deep-workshop destinations and tests |
| Critical | Deep return and basement stairs use generic ±1 movement | Players reach wrong deep-map planes and cannot traverse the quest floors | Add exact level-2 ↔ level-0 mappings; leave only true 2 ↔ 3 gantry stairs generic |
| Critical | Jig-cart NPC has no production spawn | Elemental metal cannot enter the factory and primed bar cannot be taken | Create an owner/instance-scoped cart with position and content reconstruction |
| Critical | Crane repair binds an unplaced empty-track variant while map starts lava/down/broken | Crane cannot be repaired; primary state 8 is unreachable | Bind all legitimate broken states and render the correct variant after each transition |
| Critical | Repair predicates do not authorize press/tank/fan/extractor/workbench completion | Once blockers are patched, players can skip defining repairs and finish from malformed earlier states | Centralize phase plus exact substate guards at every mutation |
| High | Junction interface auto-solves in one click | Lever-schematic reasoning and the whole pipe puzzle are absent | Implement the six-endpoint interface with partial/relog state and exact validation |
| High | Any one crate issues all four parts in fixed order | Player-random search/recovery contract is absent | Persist or deterministically derive stable per-player crate assignments |
| High | Cog puzzle only accepts correct arrangement | Wrong arrangement, Take correction, and reverse/sucking fan behavior are absent | Support every native cog/shaft combination and validate fan direction |
| High | Repaired check tests only pipe 1 and is not called after every cog | Malformed pipe state can pass; valid action order can strand state 8 | Validate the whole repair invariant after every contributing transition |
| High | Machinery visuals never move | Text and state disagree with click targets, collision, payload, and other players | Use protected player-local/instanced dynamic loc/NPC ownership and native variants |
| High | Water level is unused and outlet cannot complete its canonical cycle | Cooling/draining sequence is shortened and state recovery is inaccurate | Implement complete door/grabber/inlet/outlet/level/temperature table |
| High | Start has no acceptance/refusal and starts on Search | Accidental interaction irreversibly starts the quest; transcript branches absent | Give book/scroll on Search and start only after Yes on Read |
| High | Claw requires base 30 instead of boostable 20; mind items require base 30 | Valid boosted players are rejected and claw recipe uses the wrong level | Use effective 20 for claw and effective 30 for mind equipment |
| High | Claw and mind recipes award no 20/30 Smithing XP | Repeat production and quest totals are wrong | Add per-item recipe XP separately from 7,500 quest reward |
| High | Mind shield has no completion equip gate | Trade bypasses the advertised quest unlock | Add shared equip authorization for `elemental_mind_shield` |
| High | Completion helper has no phase/idempotence guard | Malformed state or repeated call can duplicate XP/QP/count | Require state 10 and one guarded completion transaction |
| Medium | Boiler never writes `_hide_key` | Cache-authored searched machinery state and alternate recovery leaf are dead | Drive key visibility independently from hatch ownership |
| Medium | Schematics cannot be read and are granted together | Puzzle clues/lore and canonical choice are missing | Implement both Read handlers and the two-option crate interaction |
| Medium | Battered-key workshop entrance is a bare shared staircase | Possession/access policy is not enforced at the boundary | Restore EW1-owned odd-wall/stair requirement and recovery contract |
| Medium | Dry bar can be taken without cart-at-start validation | A state-machine step can be skipped once a cart exists | Require jig position lava/start plus fan stopped and dry content |
| Medium | Earth-rock radius permits many simultaneous owner elementals | Combat/material pacing may be bypassed and room can flood with owned actors | Confirm live rule, then enforce an owner/rock lifecycle |
| Medium | Journal ignores mechanical substates | Relogged players receive a paragraph, not their exact next valid action | Render repair and machine state dynamically |
| Medium | Comments assert false cache/runtime facts | Reviewers may preserve defects as intentional design | Remove/update only alongside verified code and tests |

## 7. Modernization architecture and migration

### State ownership

Keep the native permanent varbits authoritative. Introduce named predicates for
`started`, `hatch_unlocked`, `crane_operational`, `junction_solved`,
`water_supply_repaired`, `cogs_correct`, `workshop_repaired`, `cart_safe`,
`primed_bar_ready`, `mind_bar_ready`, and `complete`. Each trigger validates
before consuming an item and writes only its owned fields after the protected
world transition succeeds.

The deep machinery should be one player-local instance or a proven equivalent
owner-scoped scene. The state is personal, and the visual/collision state must
be personal too. On entry/relogin, a renderer derives cart actor and coordinate,
crane loc variant, press state, water door/level, cog locs, fan state, pipe
repair, and extractor gun from varbits. Do not use long global `loc_change`
timers against personal bits.

### Compatibility and reconciliation

No blanket reset is acceptable. Existing values may represent legitimate
progress, current shortcuts, debug setup, or interrupted item transactions.
Migration should:

1. preserve primary 0–11 and all valid native subfields;
2. if primary is 9 or greater, reconcile the entire repair invariant to a
   canonical solved junction, repaired pipe, and correct cogs because old code
   could set 9 after testing only pipe 1;
3. preserve a loaded jig/extractor item and reconstruct it rather than minting
   another inventory copy;
4. map every valid crane/cart/tank combination to a visible scene and route
   invalid combinations to a conservative recovery action, not silent loss;
5. keep state-11 accounts complete without retroactively granting QP/XP or
   diary credit, because provenance cannot be inferred safely;
6. remove orphaned nontradeable repair parts only through explicit recovery or
   post-completion cleanup policy, not a login sweep; and
7. treat `::complete` as state setup only and provide a separate developer
   fixture that prepares coherent machinery when a test needs it.

For crate assignment, add the smallest persistent representation that can
reproduce all four item locations, or derive it deterministically from stable
account identity and a versioned quest salt. Changing assignments after an
item is lost violates recovery.

### No engine-first exception

The present evidence does not justify quest-specific C. Exact teleports,
maplinks, native transforms, dynamic loc/NPC operations, queues, modern
interfaces, inventory transactions, equipment gates, and journal/reward
helpers already exist. If concurrent player-local moving scenery cannot be
represented safely in shared space, use the established instance machinery.
Escalate only after a minimal reproduction proves a missing generic primitive.

## 8. Implementation plan

### Wave 1 — restore reachability and authoritative predicates

1. Add exact surface hatch, hatch return, basement descent, and basement return
   mappings/handlers; test all four directions and leave gantry stairs intact.
2. Add central phase/repair/machine predicates and remove comment-derived
   authorization assumptions.
3. Build deep-workshop entry/relogin scene reconstruction.
4. Spawn the jig cart with player/instance ownership at the coordinate derived
   from `_jig_pos` and the leaf derived from `_jig_state`.
5. Render and bind every crane broken/empty/cold/hot × track/lava × up/down
   state, beginning with the actual map's lava/down/broken loc.

### Wave 2 — start, documents, supply, and workbench

1. Restore Search → book/scroll plus warning/Yes/No start semantics, then the
   post-acceptance Read progression, full-inventory retry, and loss recovery.
2. Drive `_hide_key`, small-key recovery, hatch visual, and battered-key access
   at their correct owners.
3. Restore beaten book, scroll, crane schematic, and lever schematic readable
   content from pinned sources/transcript.
4. Cross-test the real EW1 waterwheel/bellows/furnace path; correct stale
   comments and effective-level checks without duplicating that subsystem.
5. Implement the workbench menu, 20-level/20-XP claw recipe, and 30-level/30-XP
   mind recipes with atomic four-tick transactions.

### Wave 3 — rebuild workshop repairs

1. Implement `ElemMagicpressPipes` endpoint selection, disconnect/reconnect,
   partial state, close/reopen/relog, and exact solved validation.
2. Assign four parts to stable player-specific eligible crates and implement
   same-crate replacement after drop/destruction.
3. Support all nine cog/shaft combinations, taking installed cogs while safe,
   and correct/reverse fan behavior.
4. Make broken-pipe repair visual and transactional.
5. Re-evaluate the full repair invariant after every junction, pipe, cog, or
   crane transition and advance primary state exactly once.

### Wave 4 — rebuild priming as a protected machine

1. Encode one table over jig state/position, crane state/position, repair
   predicates, and busy state; make loc/NPC motion and collision authoritative.
2. Animate/tick the crane pickup, rotate, lava dip, return, and release using
   the cache variants and effects.
3. Require solved junction and animate the press before hot → flat-hot.
4. Implement the full tank table, including repaired supply, door/grabber,
   inlet/outlet, `_water_level`, cooling, draining, and invalid feedback.
5. Derive fan direction from cog placement, require correct flow for drying,
   and animate start/rumble/stop.
6. Require the dry cart to return to start before Take-from; preserve every
   in-flight state across logout, death, and region leave.

### Wave 5 — extractor, completion, equipment, and diary

1. Authorize extractor use only after repair/priming requirements while
   retaining repeated bars and exact 20-current-Magic drain.
2. Add the extractor chair's protected animation/effects and resume/cleanup.
3. Allow canonical pre-completion mind-shield crafting without completing;
   complete only on a state-10 mind helmet with beaten book.
4. Make quest reward delivery idempotent: recipe item/XP, 7,500 + 7,500 quest
   XP, one QP/count, jingle, scroll, and state exactly once.
5. Add the shared mind-shield equip completion check and wyvern-protection
   integration test.
6. Award the medium Kandarin task on successful mind-helmet creation once.
7. Upgrade journal/recovery messages for every repair and machine substate.

### Wave 6 — cleanup and evidence

1. Remove `soft-skipped`, `no native loc`, `deferred`, and corrupt-dbrow claims
   only when code and tests prove their replacements.
2. Add negative, relog, malformed-state, and two-client tests before changing
   the master status.
3. Run a clean script build/pack and complete the quest on an ordinary account
   without teleports, state commands, or pre-generated nontradeable items.
4. Record live-client screenshots/video/state logs at each checkpoint and both
   post-quest equipment recipes.

## 9. Verification matrix

### Static and build gates

- `python3 tools/questhelper_extract.py elementalworkshopii --check` continues
  to resolve all gamevals, with only the documented dbrow alias handled.
- The quest has one completion call, one journal arm, one cheat arm, exact
  cross-map transitions, and one production cart owner.
- No unresolved symbols, raw cache IDs, duplicate trigger subjects, legacy
  modal shortcuts, or undisclosed `soft`/`deferred` markers remain.
- `make -C src torirsserver-scripts` and the revision-239 pack check succeed.
- Static map tests assert the initial lava/down/broken crane and every required
  surface/deep landing.

### Start, access, and materials

- Searching with 0/1/2 free slots follows the canonical retry contract; the
  interaction does not commit state 1 before Yes; No remains 0 and Yes starts
  once with both generated items accounted for.
- EW1 incomplete refuses; complete with battered key enters; lost book, scroll,
  battered key, and small key recover without duplication across inventory and
  bank.
- Every surface/deep/basement/catwalk transition lands exactly and returns;
  repeated hatch clicks and two players do not cross-contaminate instances.
- Mining below 20, boosted/effective 20, no pickaxe, multiple rocks, owner kill,
  other-player kill, ore drop, and respawn are covered.
- One ore + four coal produces one metal in six ticks and exactly 8 Smithing XP;
  insufficient coal/full inventory/interruption consumes nothing incorrectly.
- Legitimately completed EW1 machinery and cheat-completed EW1 state have
  explicit, truthful behavior.

### Repair puzzles

- Both schematics can be selected/read/recovered and do not mutate unrelated
  progress.
- Effective Smithing 19 fails and boosted 20 makes exactly one claw for 20 XP;
  base/current 30 distinctions do not change the claw recipe.
- Every initial crane orientation accepts repair only when reachable/lowered;
  the claw is consumed once and visible variant matches varbits.
- Junction wrong/partial/right connections persist through close/reopen/relog;
  only the exact solved topology authorizes the press.
- Each test player receives four stable distinct eligible crates; empty crates,
  banked parts, loss, same-crate recovery, full inventory, and another player
  are covered.
- All cog permutations render; incorrect layouts reverse/fail the fan; Take is
  safe while stopped; correct layout plus pipe/junction/crane reaches state 9
  in every repair order.

### Priming and extractor

- Cart/crane initial state is correct on first entry and relog.
- Canonical crane sequence picks up one bar, heats it, returns it, and never
  duplicates a bar between cart/crane/inventory.
- Wrong lever orders are recoverable; press before junction, water before pipe,
  and fan before correct cogs do not progress.
- Cart physically visits all four stations, cannot be interacted with at a
  stale coordinate, and cannot be advanced through collision/busy state.
- Every tank combination of door, grabber, two valves, level, temperature, and
  pipe condition has an expected transition/message; canonical fill/cool/drain
  sequence succeeds.
- Fan on/off, wrong direction, logout during rumble, and dry-cart return work;
  Take-from requires dry + start + stopped and returns exactly one primed bar.
- Extractor load/operate/take survives logout at each step; current Magic 19
  fails, boosted/restored 20 succeeds, and each bar drains exactly 20.
- Two players operating every station concurrently see and mutate only their
  own scene/items.

### Recipes, completion, and unlocks

- Effective Smithing 29 fails and boosted 30 succeeds for helmet/shield; each
  recipe takes four ticks and gives exactly 30 Smithing XP.
- Before completion: beaten book makes helmet and completes only at state 10;
  slashed-book shield creation follows canonical selection and does not
  complete; missing instructions refuse safely.
- Completion gives one crafted helmet, 30 recipe XP, 7,500 Smithing quest XP,
  7,500 Crafting XP, one QP, one completed count, one jingle/scroll, and state
  11 exactly once.
- Repeated workbench use, reward reopening, logout at every yield, and direct
  completion-proc attempts cannot duplicate rewards.
- After completion both recipes remain repeatable with correct books and XP.
- A pre-completion bought mind shield refuses equip; post-completion succeeds;
  wyvern breath recognizes it.
- Successful mind-helmet creation awards the exact Kandarin medium task once;
  quest cheat/state alone does not.
- `::complete quest_elementalworkshop2` remains an idempotent state adapter and
  is never accepted as end-to-end reward or machinery evidence.

### Live-client evidence

Capture the start warning/accept/refuse, every readable, book/key recovery,
odd-wall access, hatch and all floor transitions, both ore fights, EW1 furnace,
schematic selection, every crane pose, partial/solved junction interface,
player-random crates, wrong/correct cog layouts, broken/repaired pipe, moving
cart at every station, complete tank fill/drain sequence, fan direction,
primed-bar pickup, extractor drain, pre-completion shield branch, quest helmet,
completion scroll, post-quest repeat recipes, mind-shield equip refusal/success,
and diary task. Repeat the machinery capture with two clients concurrently.

## 10. Likely change surface and verdict

Modernization is expected to touch:

- all direct quest scripts and constants under `quest_elementalworkshopii`;
- exact maplink/transition configuration for surface, second level, and
  basement stairs;
- a player/instance-scoped deep-workshop cart/crane/machine scene owner;
- Elemental Workshop I only where shared access/furnace tests expose a real
  contract defect;
- the shared equipment quest gate for the mind shield;
- Kandarin diary task ownership;
- dynamic journal details, migration/reconciliation, and test fixtures; and
- stale queue/comment documentation after runtime proof.

Elemental Workshop II must not be labelled `verified-modern`. It contains a
thoughtful native-varbit scaffold and correct reward totals, but four separate
reachability/actor mismatches make the current quest unfinishable without
debug intervention. Once those are fixed, the implementation still substitutes
messages and direct state writes for the quest's defining physical factory and
omits crucial authorization, recipe, equipment, recovery, and concurrency
rules. The highest-value sequence is: restore all floor mappings; instantiate
the cart and native crane scene; centralize repair authorization; rebuild the
junction/crates/cogs; then implement the full moving prime/extractor pipeline
and idempotent recipe/reward/unlock transaction. Only a normal-account,
two-client, relog-heavy real-client run can advance the status.
