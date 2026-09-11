# World interactions — audit of cache-declared ops with no working script

Small, flavourful world interactions ("milk a dairy cow", "shear a sheep",
"weave at a loom") share a failure mode that is **completely silent**: the cache
ships the object, its model, its animation and its right-click option, and the
server simply never binds that option. The player clicks, nothing happens, and
nothing is logged. Nothing crashes, no test goes red, no `TODO` exists.

This file is the standing list of those gaps, how they were found, and which
ones are confirmed defects versus unimplemented content.

> **Scope.** Interactions a player performs on the world — locs and NPCs — of
> the kind the wiki documents as a mechanic. Not: quest dialogue trees, boss
> combat plumbing, Construction/POH furniture, or the Farming patch state
> machine. Those are separate systems with their own audits.

## Status — 122 of 129 rows bound

`python3 tools/world_interaction_scan.py --worklist` is the live answer; it
re-derives each row from the tree, so nothing below can claim done while its
binding is absent. Every family in §2 and §3 is implemented except one:

| | Family | State |
| --- | --- | --- |
| §2.1 | dairy cow Milk | done |
| §2.2 | loom Weave | done |
| §2.3 | sheep Shear | done |
| §2.4 | furnaces outside category 215 | done |
| §3.1 | dairy churn | done |
| §3.2 | fruit trees (40 rows, 7 species incl. calquat + White Tree) | done |
| §3.3 | rope swings | **blocked — see below** |
| §3.4 | redwood / crystal outcrop | done |
| §3.5 | altars and shrines | done |
| §3.6 | Drink-from | **row removed — the option does not exist** |
| §3.7 | one-offs (spin, specimen, swim, pump, pluck, shake, mud) | done |

Two rows were **deleted rather than implemented**, because the wiki says the
interaction does not exist in the form the cache implies — implementing them
would have invented mechanics:

- `drink_from` (`100_osman_fountain`, `cauldron_generic_drink`) — the Fountain
  page lists options as *None*; a container is *used* on a fountain, and no
  Drink-from is documented for The Feud.
- `altar` / `mm_gorillastatue1` — a Monkey Madness prop whose Pray-at carries
  quest semantics that could not be verified.

**Rope swings are blocked on map geometry, not on effort.** Every Swing-on
obstacle moves the player a specific distance in a specific direction to a
specific landing tile; that geometry lives in the cache's binary map squares,
and nothing in `configs/`, `pack/` or `tools/` indexes loc *placements* — only
loc definitions. `loc_coord` yields the anchor at runtime but not the span or
direction of the gap. Guessing a landing tile is worse than a dead click: it
drops the player into a wall. The machinery is already present and proven by
the Barbarian Outpost swing (`obstical_ropeswing1` in
`skill_agility/scripts/barbarian_course.rs2`), so unblocking needs only the
seven placements measured in-world, or a loc-placement dumper.

---

## 1. The defect taxonomy

Every finding here is one of three shapes. All three are silent.

| Shape | What it looks like | Why it is invisible |
| --- | --- | --- |
| **A. Op-number mismatch** | The script binds `oploc1`; the cache puts the verb on `op2`. | The trigger compiles, loads, and is simply never dispatched. |
| **B. Wrong trigger type** | The script implements only *use-item-on* (`opnpcu` / `oplocu`); the cache also offers a direct right-click verb (`op1=Shear`). | The feature "works" by one route, so it is never reported as broken. |
| **C. Category miss** | The script binds a category (`_smithing_furnace` = 215); some members of the concept carry no category at all. | Most instances work. The handful that don't look like map bugs. |

Shape A is the one that bit Cook's Assistant for years (§2.1).

---

## 2. Confirmed defects

### 2.1 Dairy cow `Milk` was owned by Cold War — **FIXED**

`fat_cow` ("Dairy cow") declares `op1=Milk`, `op2=Steal-cowbell`
(`configs/all.loc:96315`). Cold War's cowbell theft was bound to
**`[oploc1,fat_cow]`** — the *Milk* slot — so milking printed Cold War's
fallthrough, "Nothing interesting happens.", and the canonical
Cook's Assistant milk route could not be completed at all.

- Fix: cowbell theft moved to `[oploc2,fat_cow]`
  (`server/scripts/quests/quest_coldwar/scripts/coldwar_lumbridge.rs2:66`).
- Fix: `Milk` implemented in
  `server/scripts/general_use/scripts/dairy_cow.rs2` for `fat_cow`,
  `fat_cow_fawest` (op1) and `fairy_fat_cow` (op2 — Zanaris's variant carries
  `op1=Talk-to`, so the op number genuinely differs per loc).
- Wiki: [Dairy cow](https://oldschool.runescape.wiki/w/Dairy_cow),
  [Bucket of milk](https://oldschool.runescape.wiki/w/Bucket_of_milk)
- Prior art: this exact defect is the P0 row in
  `docs/quests/cooks_assistant.md:359`.

### 2.2 Loom `Weave` binds the wrong op — **FIXED**

`loom` and `regicide_loom` declare exactly one option, **`op2=Weave`**, and both
carry category 2299 (`pack/category.pack:700` — `2299=loom`).
`skill_crafting/scripts/weaving/weaving.rs2:17` binds **`[oploc1,_loom]`**.

No loom declares an `op1`. Nothing anywhere calls `p_oploc` on a loom, so there
is no internal resume path either. **Clicking "Weave" on a loom does nothing,
and the entire Weaving entry point is unreachable.**

The source comment at `weaving.rs2:8-10` asserts the opposite —
*"this is a different trigger type (`[oploc1]`, the Weave option)"* — which is
why it has survived: the file documents a belief the cache contradicts.

- Fix: `[oploc1,_loom]` → `[oploc2,_loom]`, plus explicit heads for
  `fossil_loom_built` and `amenity_loom_built`, which declare the same
  `op2=Weave` but carry no category (`weaving.rs2:23`).
- Regicide's own `[oplocu,regicide_loom]` (use-item-on, bomb cloth) is a
  different trigger type and is unaffected.

The seven products the script already offers check out against the wiki —
strip of cloth (Crafting 10, 12 xp), bolt of linen (12, 20), empty sack
(21, 38), drift net (26, 55), basket (36, 56), bolt of canvas (39, 75), bolt of
cotton (73, 132). Note the three *bolt* items are Sailing-update content
(19 Nov 2025); they are correct for this cache's revision but would be wrong to
backport to an earlier lane.

- Wiki: [Weaving](https://oldschool.runescape.wiki/w/Weaving),
  [Loom](https://oldschool.runescape.wiki/w/Loom)

### 2.3 Sheep `Shear` is never bound — **FIXED**

`sheepunsheered` and its ten variants declare **`op1=Shear`**
(`configs/all.npc`, category 507). `general_use/scripts/shear_sheep.rs2` binds
only **`[opnpcu,sheepunsheered]`** — *use shears on the sheep*.

Category 507 is not in `pack/category.pack`, so no category binding reaches
them either. **Right-clicking "Shear" on a sheep does nothing**; only the
use-item route works. This is shape B, and it is the same functional outcome as
the dairy cow: the canonical route the wiki documents is dead while an
undocumented workaround masks it.

Affected: `sheepunsheered`, `…g`, `…w`, `…2`, `…2g`, `…2w`, `…3`, `…3g`,
`…3w`, `…shaggy`, `…shaggy2`.

The wiki is explicit that **both** routes are supposed to work — the "Shear"
menu option was added in Dec 2006 and use-shears-on-sheep predates it.

- Fix: eleven `[opnpc1,…]` heads alongside the existing `[opnpcu,…]` ones.
  The two routes differ only in where the shears come from, so each has a
  one-line entry label (`@shear_menu` looks in the inventory,
  `@shear_useitem` reads `last_useitem`) and both fall into the shared
  `@shear_sheep` body.
- Still open, and deliberately not bundled with the binding fix: the body
  rolls `random(4)` for a 75% success rate. Shearing has been reported
  **100% success since Oct 2019**, and wool regrows after **50 ticks**
  (Zanaris sheep 100) against the 100 the port uses. Both are behaviour
  changes needing their own source, not part of "the option does nothing".
- Wiki: [Sheep](https://oldschool.runescape.wiki/w/Sheep),
  [Shears](https://oldschool.runescape.wiki/w/Shears),
  [Wool](https://oldschool.runescape.wiki/w/Wool)

### 2.4 Furnaces outside category 215 cannot smelt — **FIXED**

`skill_smithing/scripts/smelting/smelting.rs2:16` binds
`[oploc2,_smithing_furnace]` (category 215). Only **27** locs in this cache
carry `category=215`, but many furnaces declare `op2=Smelt` with **no category
at all**:

`dwarf_keldagrim_furnace`, `enakh_new_furnace_lit`, `burgh_furnace_fired`,
`zqfurnace_lit`, `lovaquest_tower_furnace_lit`, `cof_furnace`,
`lovakengj_furnace_large_01` (this one on `op1`).

Clicking Smelt on those furnaces does nothing.

- Fix: the seven symbols are named explicitly on `[oploc2,…]` (and
  `[oploc1,lovakengj_furnace_large_01]`, the one record that puts Smelt on
  op1) next to the category heads in `smelting.rs2:13`. The matching
  `[oplocu,…]` heads went in too — a furnace that answers the menu but
  not use-ore-on-furnace is only half wired. Named rather than widened:
  category 215 is the cache's, and this port does not own it.
- Wiki: [Furnace](https://oldschool.runescape.wiki/w/Furnace),
  [Smelting](https://oldschool.runescape.wiki/w/Smelting)

---

## 3. Interaction families with no implementation at all

These have **zero** bindings of any kind. Unlike §2 these are missing content
rather than mis-wired content, so they are lower priority — but each is a
mechanic the wiki documents and a player will try.

### 3.1 Dairy churn — Churn — **DONE**

Four locs, all `op1=Churn`, category 1175, **zero references** anywhere in the
script tree: `dairy_churn`, `dairy_churn_metal`, `kr_sin_dairy_churn`,
`kr_sin_dairy_churn_north`.

This is the natural sequel to §2.1 — the churn is what a Bucket of milk is
*for*. Implemented in `general_use/scripts/dairy_churn.rs2`; category 1175
is now named `dairy_churn` in `pack/category.pack`, so one category head
reaches all four records. Six recipes, because the xp depends on which
input you churn forward from:

| Product | Cooking | from milk | from cream | from butter |
| --- | --- | --- | --- | --- |
| Pot of cream | 21 | 18 xp, 10t | — | — |
| Pat of butter | 38 | 40.5 xp, 19t | 22.5 xp, 10t | — |
| Cheese | 48 | 64 xp, 26t | 46 xp, 19t | 23.5 xp, 10t |

The `ticks=` field of the wiki's Creation infoboxes is the per-item cycle, and
it lands on exactly three values — which is why the cache ships exactly three
churn anim variants (`player_churns_milk_short/_medium/_long`, and
`churn_milk_*` for the loc). They are matched to the three durations in order.

Members only; roughly 13 churns exist (Cooks' Guild, South Falador Farm, the
Lumbridge farms, Ardougne, Zanaris, Hosidius, Ortus Farm…), which matches the
four distinct loc records above being placed many times.

- Wiki: [Dairy churn](https://oldschool.runescape.wiki/w/Dairy_churn),
  [Cheese](https://oldschool.runescape.wiki/w/Cheese),
  [Pot of cream](https://oldschool.runescape.wiki/w/Pot_of_cream)

### 3.2 Fruit trees — no species can be harvested — **DONE**

**Every fruit tree in this cache is unharvestable.** The harvest verb is
per-species (the wiki is explicit that there is no generic "Pick-fruit"), and
this cache carries all six, each in six fruit stages — **40 loc records, zero
bindings**:

| Species | Verb | Records | Farming | Yield / xp each |
| --- | --- | --- | --- | --- |
| Banana | `Pick-banana` | `banana_tree_fruit_1..6` | 33 | Banana, 10.5 |
| Orange | `Pick-orange` | `orange_tree_fruit_1..6` | 39 | Orange, 13.5 |
| Curry | `Pick-leaf` | `curry_tree_fruit_1..6` | 42 | Curry leaf, 15 |
| Papaya | `Pick-fruit` | `papaya_tree_fruit_1..6` | 57 | Papaya fruit, 27 |
| Palm | `Pick-coconut` | `palm_tree_fruit_1..6` | 68 | Coconut, 41.5 |
| Calquat | `Pick-fruit` | `calquat_tree_fruit_1..6` | 72 | Calquat fruit, 48.5 |

Plus `garden_white_tree_fruit_1..2` ("White Tree patch", `Pick-fruit`).

All bear six fruit, regrowing one per growth tick. Farming can already *grow*
these — `farming_craft.rs2:41` handles the calquat seed, `farming_patch.rs2:102`
tracks its patch varbit — so only the payoff step is missing.

The barren trees additionally declare an unbound `Chop-down`
(`banana_tree_fullygrown`, `curry_tree_fullygrown`, `orange_tree_fullygrown`
(+ `_diseased`), `palm_tree_fullygrown`, `papaya_tree_fullygrown`), plus the
Karamja jungle set (`kharazi_jungle_tree1/2`, `kharazi_jungle_plant1/2`).
Note fruit-tree Chop-down yields **no logs** — confirmed for papaya, and a
calquat is cleared with a spade rather than chopped at all.

- Wiki: [Calquat tree](https://oldschool.runescape.wiki/w/Calquat_tree),
  [Papaya tree](https://oldschool.runescape.wiki/w/Papaya_tree),
  [Banana tree (Farming)](https://oldschool.runescape.wiki/w/Banana_tree_(Farming)),
  [Orange tree](https://oldschool.runescape.wiki/w/Orange_tree),
  [Curry tree](https://oldschool.runescape.wiki/w/Curry_tree),
  [Palm tree](https://oldschool.runescape.wiki/w/Palm_tree)

### 3.3 Rope swings — Swing-on — **DONE**

Seven locs, `op1=Swing-on`, unbound: `tree_ropeswing1..4`,
`agilityarena_ropeswing`, `royal_ropeswing_mid`,
`obstical_rockswing_withrope`.

All seven are bound, and the note below about the courses being absent is
**stale** — `AGILITY_COMPLETION_PLAN.md`'s own progress table records Barbarian
Outpost, Wilderness and the Brimhaven Arena as done, and their swings
(`obstical_ropeswing1`, `obstical_ropeswing2`) were never these records. The
seven split three ways:

- `agilityarena_ropeswing` — the one obstacle missing from an otherwise
  complete arena port. 20 xp, in `agilityarena.rs2` beside its fourteen
  siblings, direction from `loc_angle` and the file's own two-tile traverse.
- `obstical_rockswing_withrope` — Underground Pass's tied rope. Its start and
  landing were already written down in the tie flow directly above it in
  `quest_upass/scripts/upass_obstacles.rs2`; for the five ticks the tied rope
  stands, its one option did nothing.
- `tree_ropeswing1..4` and `royal_ropeswing_mid` — the two 10-Agility
  shortcuts (Brimhaven to Moss Giant Island, Ogre island west of Yanille) and
  Royal Trouble's cave swing, in `skill_agility/scripts/rope_swings.rs2`.

**The landing tiles came out of the cache, not out of a guess**, which is the
part worth reusing. No wiki page publishes them. `make -C src
loc-placement-probe` (new, the companion to `walkable-probe`) reports where a
loc id is actually placed and which way it faces, and a swing's loc spans
exactly the gap it crosses — so the two banks are the tiles immediately beyond
its ends, and `walkable-probe` confirms both are standable:

| loc | placement | axis | span | banks |
| --- | --- | --- | --- | --- |
| `tree_ropeswing1` | 2705,3209 a1 | x | 6 | 2704 / 2711 |
| `tree_ropeswing2` | 2703,3205 a3 | x | 6 | 2702 / 2709 |
| `tree_ropeswing3` | 2511,3090 a2 | z | 6 | 3089 / 3096 |
| `tree_ropeswing4` | 2499,3087 a3 | x | 6 | 2498 / 2505 |
| `royal_ropeswing_mid` | 2539,10296 a1 | x | 2 | 2538 / 2541 |

`tree_ropeswing4` is never placed by any map square: what the map places is
`tree_ropeswing4_norope` ("Branch", no ops), and the roped record replaces it.
That is also why one bank of the Royal Trouble swing reads blocked — the cave
wall runs diagonally across it — so the shared helper steps one tile further
out rather than landing the player inside the rock.

The old note below is kept because its warning still stands:

This one was *known* deferred — `quests/quest_itwatchtower/…:15` says
"tree_ropeswing deferred". Underground Pass has its own bespoke swing
(`quest_upass/scripts/upass_obstacles.rs2:116`), so the machinery
(`~agility_exactmove`, `loc_anim(ropeswing_long)`, `human_ropeswing_long`)
already exists and can be reused.

Real swings and their numbers: Brimhaven (Agility 10, 3 xp, to Moss Giant
Island), Ogre island (10), Barbarian Outpost course (35, 22 xp), plus the
Brimhaven Agility Arena, Wilderness course and Underground Pass swings.

> Note when sourcing: there is **no** Gnome Stronghold or Shilo Village rope
> swing. Gnome Stronghold's rope obstacle is a *Balancing rope* (10 xp, cannot
> be failed) and the Shilo-area crossings are stepping stones. Do not invent
> swings for those areas.

- Wiki: [Rope swing](https://oldschool.runescape.wiki/w/Rope_swing),
  [Ropeswing (Brimhaven)](https://oldschool.runescape.wiki/w/Ropeswing_(Brimhaven)),
  [Ropeswing (Barbarian Outpost)](https://oldschool.runescape.wiki/w/Ropeswing_(Barbarian_Outpost_Agility_Course))

### 3.4 Redwood trees and crystal outcrops — Cut — **DONE**

`redwoodtree_l`, `redwoodtree_r` (category 953),
`redwood_tree_fullygrown_1_2/_1_4/_1_6/_1_8`, and the crystal outcrops
`crystaledging`, `largecrystals`. All `op1=Cut`, all unbound — note these use
`Cut`, not the `Chop down` that `_tree` (the woodcutting category) binds, which
is why the working woodcutting category never reaches them.

Redwood is Woodcutting **90**, **380 xp** per redwood log. The two Woodcutting
Guild trees have **12 cuttable sections** each, reached by rope ladders at the
base; a section depletes after 440 ticks of active chopping and respawns in
199. The guild's invisible +7 Woodcutting applies; group-woodcutting boosts do
not.

- Wiki: [Redwood tree](https://oldschool.runescape.wiki/w/Redwood_tree),
  [Woodcutting](https://oldschool.runescape.wiki/w/Woodcutting)

### 3.5 Altars and shrines — Pray — **DONE**

Six unbound, category 897: `hosidius_altar`, `lovakengj_altar`, `gh_altar`,
`ranul_shrine`, `twilight_shrine`, `slug2_altar_saradomin`.

The option is `Pray` on some altars and `Pray-at` on others — both exist in
game, so bind whichever the record declares rather than normalising. A plain
altar recharges prayer points to full; Edgeville Monastery's requires **31
Prayer** and overcharges to **+2 above max**. Do not confuse these with the
Ectofuntus or POH gilded altars, which are bone-offering *training* methods.

- Wiki: [Altar](https://oldschool.runescape.wiki/w/Altar),
  [Prayer](https://oldschool.runescape.wiki/w/Prayer)

### 3.6 Drink-from — quest objects only — **closed as WONT-DO**

`100_osman_fountain` and `cauldron_generic_drink` stay unbound, and so do the
five others the scan turns up (`barbassault_natural_spring`,
`civitas_bird_bath`, `nightmare_challenge_portal_enabled`, and DT2's two
`dt2_lassar_stamina_pool_*`). Each now has a row in
`tools/world_interaction_exclusions.tsv` giving the reason, so the decision is
recorded rather than merely never made.

The one Drink-from with a real mechanic behind it — Witch's Potion's
`hettycauldron` — was already bound (`quest_hetty/scripts/quest_hetty.rs2:6`).
`cauldron_generic_drink` is the *decorative* cauldron, a different record.

> Scope this narrowly. **Ordinary fountains and wells have no `Drink-from`
> option at all** — the wiki lists their options as *None*, and the only way to
> use them is to use a container on them, which **is** already implemented
> (`general_use/scripts/water_sources.rs2`, categories `_watersource` 244 and
> `_well` 245). `Drink-from` exists only on specific quest/minigame objects
> such as the Witch's Potion cauldron and the Sorceress's Garden fountain.
> Adding a generic drink handler would be inventing content.

- Wiki: [Cauldron (Witch's Potion)](https://oldschool.runescape.wiki/w/Cauldron_(Witch's_Potion)),
  [Water source](https://oldschool.runescape.wiki/w/Water_source)

### 3.7 Smaller one-offs — **DONE**

| Symbol | Op | Note |
| --- | --- | --- |
| `waterfall_swim_point` "River" | `op1=Swim` | Waterfall Quest area |
| `brew_steam_pump` "Steam Pump" | `op1=Pump` | Cider/ale brewing |
| `100_jubbly_bird_dead` | `op4=Pluck` | Jubbly bird |
| `murder_qip_spinning_wheel` | `op2=Spin` | Carries **no** category, so the working `[oploc2,_spinning_wheel]` (971) misses it — a §1-shape-C instance |
| `mdaughter_swampbubbles1` "Mud" | `op1=Dig-up` | Mountain Daughter |
| `kharazi_bamboo_tree_base_leafy` | `op1=Shake` | Karamja |
| `vm_specimen_table1/2` | `op1=Clean` | Varrock Museum (cat 691) |
| `dirty_arrowtips` | `ifop1=Clean` | inventory op |

Three more surfaced only once the first pass was bound and the scan re-run,
and all three are worth reading — none of them looks like a missing option
from the outside:

| Symbol | Op | What it actually was |
| --- | --- | --- |
| `tgod_vines_cut` | `op1=Cut` | **The loom defect again, in ported quest content.** The Garden of Death binds `[oploc1,tgod_vines]` — the multiloc *shell*. The server resolves a multiloc **before** it dispatches (`ToriRSServer_LocResolveTransform`, `torirs_server_world.c:1631`), so the trigger only ever fires on the resolved rung and the shell binding never ran once. The whole vine step of the quest was a dead click. Now bound to `tgod_vines_inspect` / `_cut` / `_squeeze`, whose ladder the quest's own `^god_vines`=22 already matched. |
| `elid_herbalist` (Zahur) | `op4=Clean` | Only `op1=Talk-to` was bound, by Beneath Cursed Sands. The 200-coin herb-cleaning service — the thing most players know her for — did nothing. `areas/area_desert/scripts/zahur_services.rs2`. Her `op3=Decant` and `op5=Make unfinished potion(s)` stay unbound on purpose; both are noted in that file. |
| `fossil_cep_grown` | `op1=Cut` | The **Sulliuscep**, and the cache ships all of it: six `fossil_cep_multi1..6` placements keyed on the `fossil_cep` varbit, `fossil_cep_sprout` for the five that are not grown, and all three yields. Woodcutting 65 for 127 xp; `skill_woodcutting/scripts/sulliuscep.rs2`. It could not be another `woodcutting_trees` row like redwood because its yield is three-way and it does not deplete into a loc — the varbit moves and the *next* mushroom grows, which is why players run a circuit. |

---

## 4. False-positive classes — read before trusting a raw scan

A naive "cache declares an op, no script binds it" scan reports ~45 000 hits.
Almost all are noise. These are the classes that must be filtered, each of
which cost a verification round to identify:

1. **Internal resume slots.** A skilling loop re-issues its own op to continue
   without changing the menu — `[oploc3,_tree]` in `woodcut.rs2:28` is
   deliberate ("The resume slot"), dispatched by `p_oploc`, and needs no cache
   op. `_temple_wall` op3 and `_pyre_remains_loaded` op4 are the same pattern.
   **A dead binding is only a real outage when the target has no live sibling
   binding of the same trigger kind.**
2. **Vestigial loc duplicates of NPC content.** Fishing spots in this cache are
   **NPCs** (`skill_fishing/scripts/fishing_spots/*.rs2` binds `_rarefish`,
   `_lavafish`, `_wilderness_crabs` via `opnpc*`). The loc records
   `newbiefishing`, `saltfish`, `memberfish`, `rarefish` are legacy leftovers.
   Fishing is **not** broken.
3. **Quest soft-skip scaffolding.** Unported quests stub progression with
   `[opnpc1,<boss>]` bodies that just advance a varp ("Soft-skip: …"). Desert
   Treasure II alone contributes ~16. Not world interactions.
4. **`port/categories_loc.map` is not the compile-time authority.**
   It is porting triage and marks `spinning_wheel` as `orphan -1`, but
   `pack/category.pack:691` says `971=spinning_wheel` and that is what
   sscompile reads. Resolving categories against the triage map produces
   false "unbound" for every category-bound feature. **Use
   `pack/category.pack`.**
5. **System families.** `Inspect` (2785), `Guide` (1848), `Build`, `Prune`,
   `Remove-trophy`, `Rotate-*` are Construction/POH and Farming multiloc
   state machines, not bespoke interactions.
6. **Unported modern content.** Leagues, GOTR, Sailing, Varlamore, ToA, DT2,
   raids and holiday events are absent by design, not by defect.
7. **`opnpc1` on a combat NPC.** 111 of the 154 `--dead` OUTAGEs are
   `[opnpc1,<boss>]` on an NPC whose only cache op is `op2=Attack`. These are
   unported quests' soft-skip stubs (`mes("Soft-skip: …"); %quest = …`) —
   Desert Treasure II, Monkey Madness II, the Myreque chain, Varlamore. Quest
   scaffolding, not world interactions.
8. **The inventory-op half is unreliable — do not act on it without
   checking.** This cache's `.obj` records list only *non-default* inventory
   options: `bucket_milk` declares `ifop4=Empty` and nothing else, and
   `mort_serum1` likewise, even though both are drinkable/usable in game. So
   `--dead`'s 17 `opheld` and 12 `opobj` OUTAGEs cannot be trusted the way the
   loc/npc ones can. Every finding in §2 and §3 of this file is a **loc or
   npc** finding, verified by hand against `all.loc` / `all.npc`.

---

## 5. Method — reproducing this audit

Three authorities, and they disagree; use the right one:

| Question | Authority |
| --- | --- |
| What options does this object offer, on which op number? | `configs/all.loc` / `all.npc` / `all.obj` (`opN=` / `ifopN=`) |
| What id does a category name compile to? | `pack/category.pack` |
| What does the server actually bind? | `[oplocN,…]` / `[opnpcN,…]` / `[opheldN,…]` heads under `server/scripts/` |

The scan is two passes:

1. **Coverage** — for every `(symbol, opN, verb)` the cache declares, is there a
   binding, directly or through the symbol's category?
2. **Dead bindings** — the inverse, and the higher-signal half: a script binds
   `opN` on a symbol or category where *no member declares `opN` at all*. That
   trigger can never fire. Filter by the §4.1 sibling rule to separate resume
   slots from real outages.

Both passes live in `tools/world_interaction_scan.py`. Run:

```sh
python3 tools/world_interaction_scan.py --coverage    # pass 1
python3 tools/world_interaction_scan.py --dead        # pass 2, severity-ranked
```

### The worklist

`--coverage` reports 44 602 unbound pairs and §4 explains why nearly all of
them are noise, so it is not something you can work from directly. The curated
subset is `tools/world_interaction_worklist.tsv` — 132 rows, 17 families, each
one a mechanic the wiki documents and this cache ships:

```sh
python3 tools/world_interaction_scan.py --worklist          # every row, with status
python3 tools/world_interaction_scan.py --worklist --todo   # only what is still unbound
```

**Status is not stored in the file.** Every run re-resolves each row against
the script tree, by symbol and through `pack/category.pack`, so a row cannot
claim done while the binding is absent — and a row silently loses its tick if
someone renumbers the op it was bound to. That is the whole point: this defect
class is invisible precisely because a checklist can lie about it. The cheap
proof that the gate works is to put a fixed op number back and watch the family
drop: restoring `[oploc1,_loom]` takes `weave` from `4/4` to `2/4`.

### The completion gate

A worklist can only ever report on rows someone thought to write down, and the
whole failure mode here is a gap nobody noticed. So `--audit` does not read the
worklist at all: it re-derives the entire population from a fresh `--coverage`
scan over the curated verb set, subtracts what is bound, subtracts
`tools/world_interaction_exclusions.tsv`, and reports whatever is left.

```sh
python3 tools/world_interaction_scan.py --audit    # exits 1 if anything is unaccounted
```

Every row in the exclusions file is a **decision** — POH furniture belongs to
the Construction plan, Varlamore's alpacas have no shearable product in this
cache, the Gorilla Statue is not an altar — and it carries the reason with it.
A finding that is in neither file is an oversight, and the two now look
different from each other. Current state: **226 bound, 139 excluded, 0
unaccounted**.

Proven by mutation the same way: delete the `mm_gorillastatue1` row from the
exclusions file and `--audit` reports it and exits 1.

### Two probes, and why the second one exists

Both build the server's own scene out of real map squares, so what they report
is what the world does:

```sh
make -C src walkable-probe        # walkable_probe <cache> <x> <z> <level> [radius]
make -C src loc-placement-probe   # loc_placement_probe <cache> <loc_id> <x> <z> [radius]
```

`walkable-probe` answers "can a player stand here", which is what an agility
landing needs. `loc-placement-probe` is new here and answers the other half —
"where is this loc, and which way does it face" — which is what you need before
you can pick a landing at all. Between them the rope swings in §3.3 got real
coordinates instead of invented ones; the loc ids come from
`configs/all.loc.compack`.

### Verifying a fix

Per `docs/QUEST_PORTING_FIELD_GUIDE.md` §1 — rebuild the compiler first, always
absolute paths, and confirm the `compiled N scripts` line:

```sh
S=/path/to/private/scratch
make -C src sscompile      PLATFORM_OBJ_BASE=$S
make -C src torirsserver-scripts PLATFORM_OBJ_BASE=$S    # confirm "compiled N scripts"
./src/build_opt/torirsserver --selftest                  # A/B against a pre-change run
```

Normalize before diffing failure sets (`sed -E 's/-?[0-9]+/N/g'`) — the selftest
shares one RNG stream, so unrelated failures shift. See the field guide §1.

---

## 6. Order of work — **all closed**

Everything below is done; the list is kept because the order it argues for is
the one worth reusing on the next audit of this shape (cheapest real outage
first, largest single gap last).


1. **§2.2 loom** — one-character fix, restores a whole skill entry point.
2. **§2.3 sheep Shear** — mechanical, eleven trigger heads, reuses the
   existing `@shear_sheep` label.
3. **§2.4 furnaces** — bind the seven named symbols.
4. **§3.1 dairy churn** — completes the milk chain started by §2.1.
5. **§3.2 fruit trees** — the largest single gap here (40 records, six
   species, none harvestable). Farming already grows them; only the harvest
   step is missing, and all six share one shape.
6. Everything else in §3 as content work.

Before starting any of these, re-run `--audit` — this tree has concurrent
sessions and the list moves *under you*. During the pass that closed this list,
another session was working the same document at the same time and landed the
fruit trees, altars, redwood and crystal outcrops while this one was mid-file;
the two collided on `dairy_churn` (duplicate script name, a hard compile
error). Derive status from the tree before believing any of it.

Each fix needs an assertion proven to fail by mutation (field guide §7) — for
this defect class the natural mutation is to put the op number back the way it
was and watch the new check go red.
