# Finishing Crafting

Plan to take `skill_crafting/` from its LostCity-era vertical slice to the
complete [Crafting](https://oldschool.runescape.wiki/w/Crafting) skill. The
wiki is the authority for content (products, levels, XP, materials, gates); the
cache (`OSRS-Content/osrs239-content/configs/all.obj`, `all.loc`, `all.seq`,
`pack/category.pack`) is the authority for names and for what is *expressible*
at this revision.

Queue items this closes: **#43–#48** in
[`SKILLS_CONTENT_PORT_QUEUE.md`](SKILLS_CONTENT_PORT_QUEUE.md). It also emits
**seven new Finish-queue rows** (§2) for branches no existing row names —
spinning remainder, silver casting remainder, leather/d'hide shields,
snakeskin, amethyst, the fabric-armour families, and dye targets beyond capes.

Companion plans: [`FLETCHING_COMPLETION_PLAN.md`](FLETCHING_COMPLETION_PLAN.md)
(shares the interface-270 make-menu and owns bolt tips / amethyst ammo),
[`MINING_COMPLETION_PLAN.md`](MINING_COMPLETION_PLAN.md) (owns the gem/clay/
silver/gold rocks that feed this skill),
[`RUNECRAFT_COMPLETION_PLAN.md`](RUNECRAFT_COMPLETION_PLAN.md) (consumes the
tiara this plan learns to cast).

---

## Status (2026-08-16)

**S2–S15 landed** (§2's slice table has the per-slice detail). Queue rows
**#43, #45–48** and the new rows **#154–159** are `done` in
[`SKILLS_CONTENT_PORT_QUEUE.md`](SKILLS_CONTENT_PORT_QUEUE.md). Fletching's
amethyst / gem-bolt-tip / toxic-blowpipe slices (§1.17, referenced from this
plan's chisel-switch coordination note) had already landed from a separate
session before this pass started; this pass's only touch there was the two
missing onyx/zenyte gem-cutting rows those cases assumed existed (#160).

**Not built:**

- **S1 (`~skill_multi` on interface 270)** — every menu this pass added
  instead extends the existing `~p_choice*` pagination convention (see
  `leather.rs2`'s `craft_leather_menu` for the shape). This keeps every new
  recipe reachable today; S1 remains the follow-up that would collapse the
  now-numerous two-page menus into one real make-all interface. Queue #44
  stays `pending` for this reason.
- **S16 (the C selftest suite, §4)** — verification this pass was
  `make -C src mock230-scripts` (0 errors) after every slice, not runtime
  behavioural tests. §4's eleven C1–C11 cases are still open work.
- Deferred by data gaps, not scope: lens mould → telescope disc and the
  light-orb wire assembly (glass, §1.11 — no "filled" light-orb obj or
  `bullseye_lantern_nolens` source in this cache); the guild's mould/tool
  respawns and teleport perk (§1.20 — no verified in-world coordinate, the
  same gap `RUNECRAFT_COMPLETION_PLAN.md` §4 already flags); birdhouses
  (§1.18, routed to Hunter).

`docs/TOOL_TRIGGER_ORGANISATION.md` was written alongside this pass and
documents the `[opheldu]` dispatch rules this plan's recipes were wired
against (one type-bound hub per tool, cross-file `@label`/`~proc` calls into
it, category bindings only where genuinely useful).

---

## Why this pass is more than six queue rows

**1. Coverage is roughly half the product surface.** `skill_crafting/` is
**1,411 lines** across 12 scripts and implements **75 of the 158** wiki
recipes this lane owns (§0.4's totals, less the 9 birdhouses routed to Hunter). The gaps are not exotica: an entire *branch* (weaving) has no
script, spinning implements 2 of its 10 rows, glassblowing 3 of its 10, gold
jewellery 18 of its 35, and silver jewellery 0 of 12. §0.4 has the per-branch
count.

**2. Queue row #47's premise is wrong.** It reads "*pottery urns / modern
pottery — pottery.rs2 pot/pie dish/bowl only; wiki urns/cups absent*". **OSRS
has no urns** — urns are an RS3 Divination-era item and appear nowhere on
[Pottery](https://oldschool.runescape.wiki/w/Crafting#Pottery). The real
pottery gaps are the [empty cup](https://oldschool.runescape.wiki/w/Empty_cup)
(level 3, four per soft clay) and the
[empty plant pot](https://oldschool.runescape.wiki/w/Empty_plant_pot) (19) —
and a **data bug**: the tree's pot lid is level 3 / 2.5 XP where the wiki says
level **25** / 20 XP shaping + 20 XP firing (§1.3).

**3. The `map_members` blocker that stops Fletching does not apply here.**
`SS_OP_MAP_MEMBERS` now pushes `srv->members_world`
(`src/net/mock/mock230_scripts.c:8060`), which
`mock230_flag_default_on("MOCK230_MEMBERS_WORLD")` defaults **on**
(`mock230_main.c:186`). Every `if (map_members = ^false)` gate in
`glass.rs2` / `leather.rs2` / `studded.rs2` / `battlestaves.rs2` / `snelm.rs2`
is live and passes. Crafting's members branches work today; only Fletching's
own S0 was ever blocked.

**4. Every item this plan needs is already named in the cache.** All 60+
gamevals were resolved against `configs/all.obj.compack` before this document
was written — including the modern fibres this cache carries but nothing
references (`hemp` 31457, `cotton_boll` 31460, `linen_yarn` 31463,
`bolt_of_canvas` 31475, `bolt_of_cotton` 31478) and the full zenyte/onyx
jewellery ladder. Nothing in §1 is blocked on a missing name. §0.3 lists the
loc side.

**5. The real make-menu is still alive client-side and unused server-side.**
Interface **270 `skillmulti`** is unpacked and named
(`OSRS-Content/osrs239-content/interfaces/skillmulti.if`, 35 components:
`1/5/10/other/x/all` + 18 item slots `a`–`r`), clientscripts **2046–2063** are
present, and both varcs exist (`200=skillmulti_quantity`,
`201=skillmulti_suggestedquantity`, plus `1167=skillmulti_quantity_string`).
`grep skillmulti` across `src/` and the whole content tree still returns
**zero** functional hits. Crafting has **eight** `~p_choice*` menus standing in
for it (`pottery.rs2` ×2, `glass.rs2`, `leather.rs2` ×2, `jewellery.rs2` ×3),
one of which — `craft_leather_menu` — is a four-deep chain of "More
options..." pages to fit 7 products into 3-row dialogs. This is the one build
worth doing before adding recipes, and it is **shared with Fletching S2**.

---

## 0. Where Crafting stands today — measured

### 0.1 What is wired

`OSRS-Content/osrs239-content/server/scripts/skill_crafting/`

| file | L | what it does |
|---|---|---|
| `scripts/dye_cape.rs2` | 222 | 3 dye mixes + 7×6 cape dye matrix; merge points for Mourning's End I bellows, goblin mail, blonde wig |
| `scripts/leather/leather.rs2` | 214 | `[opheldu,needle]` + per-hide reverse binds; soft/hard/d'hide menus; `%thread_used` 5-craft reel |
| `scripts/jewellery/jewellery.rs2` | 181 | `craft_gold_menu` (mould-aware), `craft_silver`; gold ring/necklace/amulet × gold→dragonstone |
| `scripts/pottery/pottery.rs2` | 141 | soften clay, potter's wheel, pottery oven (shape + fire, `stat_random(180,800)` crack roll) |
| `scripts/jewellery/stringing.rs2` | 120 | `[opheldu,ball_of_wool]` + 8 reverse binds; 6 amulets + holy/unholy symbol |
| `scripts/glass/glass.rs2` | 118 | sand pit fill, `smelt_glass`, `craft_glass_menu` (beer glass / vial / orb) |
| `scripts/snelm/snelm.rs2` | 106 | 9 shells → 9 snelms, plus `~snelm_reduction` for Mort Myre snails |
| `scripts/gem/uncut_gem.rs2` | 75 | `[opheldu,chisel]` — 8 gems + bolt tips + snelms + 5 quest branches |
| `scripts/studded/studded.rs2` | 68 | studded body / chaps |
| `scripts/battlestaves/battlestaves.rs2` | 63 | 4 orb → battlestaff |
| `scripts/spinning/spinning.rs2` | 58 | wool → ball of wool, flax → bow string |
| `scripts/crafting_guild/crafting_guild.rs2` | 45 | guild door (40 + brown apron), Master Crafter greeting |
| `configs/leather/leather.db{table,row}` | — | `craft_leather_table`, 20 rows |
| `configs/gem/gem.db{table,row}` | — | `gem_cutting_table`, 8 rows |
| `configs/thread_used.varp` | — | temp reel counter |

XP is stored in **tenths** and the existing rows are wiki-exact — spot-checked
gem cutting (`opal 150` = 15, `dragonstone 1375` = 137.5), leather
(`leather_gloves 138` = 13.8), d'hide (`black body 2580` = 258), glass
(`vial 350` = 35), studded (`body 400` = 40). Keep that convention.

**Outside `skill_crafting/` but part of the skill:**

| where | what |
|---|---|
| `areas/alkharid/scripts/tanner.rs2` | tanning — cowhide→leather/hard leather, 5 dragonhides→leather, Al Kharid + Canifis (Sbott) pricing. Complete against [Tanning](https://oldschool.runescape.wiki/w/Tanning) except snakeskin (§1.6) |
| `skill_smithing/scripts/smelting/smelting.rs2:38-42` | the furnace `[oplocu]` — `bucket_sand`/`soda_ash` → `@smelt_glass`, `gold_bar` → `@craft_gold_menu`, `silver_bar` → `@craft_silver`. **This is the seam**; new furnace recipes are added here, not in a second `[oplocu]` |
| `skill_fletching/scripts/bolts.rs2` | `@make_bolt_tips` — called from crafting's `[opheldu,chisel]` |
| `general_use/scripts/pickables.rs2:156` | `[oploc2,flax]` — flax never depletes at this revision |
| `areas/draynor/scripts/aggie.rs2` | red / yellow / blue dye maker |
| 30 quest scripts | crafting XP rewards + quest-local crafts (§1.23) |

### 0.2 Stat wiring is complete — no work required

| layer | location |
|---|---|
| Stat id | `pack/stat.pack:15` → `12=crafting` |
| Skill guide | `interface_skill_guide/configs/skill_guide.constant:86` → `^skill_guide_crafting = 11` |
| XP drops | `interface_chrome/configs/xpdrops.varp:77,197` |
| XP curve / level-up | generic `g_xp_table`, `levelup/scripts/levelup.rs2` |
| Cape item | `skillcape_crafting` 9780 / `skillcape_crafting_hood` 9782 — **item exists, no shop, no perk** (§1.20) |

### 0.3 The loc / NPC surface

Bound today:

| kind | binding | notes |
|---|---|---|
| loc category 377 | `_potters_wheel` | `[oplocu]` + shape menu |
| loc category 230 | `_pottery_oven` | `[oplocu]` + `[oploc1]` fire menu |
| loc name | `spinningwheel` (14889) | `[oplocu]` + `[oploc2]` |
| loc name | `sandpit` (14890) | `[oplocu]` bucket fill |
| loc name | `craftingguilddoor` (14910) | 40 Crafting + brown apron |
| loc category 215 | `smithing_furnace` | owned by smelting, dispatches here |
| npc | `master_crafter` (5810) | greeting only |
| npc | `tanner` (5809) / `werewolftanner` (6526) | tanning |

Named in the cache and **not** bound — every one of these is a real crafting
station the player can walk to and click:

| loc | id | needed by |
|---|---|---|
| `loom` | 8717 | §1.2 weaving — the generic loom, verb `Weave` |
| `regicide_loom` | 787 | §1.2 — already has an `[oplocu]` in `quest_regicide/scripts/regicide_bombcraft.rs2:80`; the weave op must **merge** into that file, not duplicate the binding |
| `fossil_loom` | 31432, `fossil_spinning_wheel` 31431 | §1.2 / §1.1 — Fossil Island repairables |
| `spinningwheel_2` | 56964 | §1.1 — second wheel model |
| `viking_spinningwheel` | 4309, `elf_village_spinning_wheel` 8748, `iznot_spinning_wheel` 21304, `contact_spinning_wheel` 20365, `kr_spinningwheel` 25824, `spinningwheel_quetzacali` 55330 | §1.1 — regional wheels, all currently inert |
| `sandpit_2` | 50733, `viking_sandpit` 4373, `dorgesh_sandpit` 22726, `prif_sandpit` 36563 | §1.11 — [sand pit](https://oldschool.runescape.wiki/w/Sand_pit) locations |
| npc `master_crafter_2` 5811, `master_crafter_3` 5812 | | §1.20 — one of the three sells the cape |
| npc `auburn_tanner` 14662, `ellis_tanner` 3231 | | §1.6 — snakeskin tanners |

Anims, all present: `human_potterywheel`, `human_furnace`,
`human_spinningwheel_90`/`_60` (13138/13139), `human_glassblowing` (884),
`human_leather_crafting` (1249), the nine `human_*cutting` gem seqs,
`human_snailshellcutting`, `human_fillbucket_sandpit`, and for weaving
**`regicide_useloom` (1238)** / **`farming_useloom` (2270)** — 2009scape's
`WeavePulse` uses 2270, which is the one to take.

### 0.4 Branch coverage, measured against the wiki

| branch | wiki rows | in tree | gap |
|---|---:|---:|---|
| [Spinning](https://oldschool.runescape.wiki/w/Crafting#Spinning) | 10 | 2 | 8 |
| [Weaving](https://oldschool.runescape.wiki/w/Crafting#Weaving) | 7 | 0 | 7 — **no script** |
| [Pottery](https://oldschool.runescape.wiki/w/Crafting#Pottery) | 6 | 4 | 2 + 1 wrong row |
| [Leather](https://oldschool.runescape.wiki/w/Crafting#Leather) | 12 | 10 | 2 |
| [Dragonhides](https://oldschool.runescape.wiki/w/Crafting#Dragonhides) | 16 | 12 | 4 (shields) |
| [Snakeskin](https://oldschool.runescape.wiki/w/Crafting#Snakeskin) | 7 | 0 | 7 |
| [Yak hide](https://oldschool.runescape.wiki/w/Crafting#Yak_hide) | 2 | 2 | 0 (via `fris_craft_yak_armour`) |
| [Snelms](https://oldschool.runescape.wiki/w/Crafting#Snelms) | 9 | 9 | 0 |
| [Crab armour](https://oldschool.runescape.wiki/w/Crafting#Crab_armour) | 2 | 0 | 2 |
| [Xerician](https://oldschool.runescape.wiki/w/Crafting#Xerician_robes) | 3 | 0 | 3 |
| [Splitbark](https://oldschool.runescape.wiki/w/Crafting#Splitbark_armour) | 5 | 0 | 5 |
| [Mixed hide](https://oldschool.runescape.wiki/w/Crafting#Mixed_hide_armour) | 4 | 0 | 4 |
| [Hueycoatl hide](https://oldschool.runescape.wiki/w/Crafting#Hueycoatl_hide_armour) | 4 | 0 | 4 |
| [Glass](https://oldschool.runescape.wiki/w/Crafting#Glass) | 10 | 3 | 7 |
| [Gems](https://oldschool.runescape.wiki/w/Crafting#Gems) | 10 | 8 | 2 |
| [Silver jewellery](https://oldschool.runescape.wiki/w/Crafting#Silver_jewellery) | 12 | 0 | 12 |
| [Gold jewellery](https://oldschool.runescape.wiki/w/Crafting#Gold_jewellery) | 35 | 18 | 17 |
| [Silver casting](https://oldschool.runescape.wiki/w/Crafting#Silver) | 5 | 3 | 2 |
| [Battlestaves](https://oldschool.runescape.wiki/w/Crafting#Battlestaves_and_silver_bolts) | 4 | 4 | 0 |
| [Amethyst](https://oldschool.runescape.wiki/w/Crafting#Amethyst) | 4 | 0 | 4 |
| [Birdhouses](https://oldschool.runescape.wiki/w/Crafting#Birdhouses) | 9 | 0 | 9 → **Hunter lane** |
| Dyes / capes | 9 + clothing | 9 | clothing targets |

---

## 1. The complete wiki inventory

Every gameval below was resolved against `configs/all.obj.compack` /
`all.loc.compack` / `all.npc.compack`. XP is shown as the wiki value; the tree
stores it ×10.

### 1.1 Spinning — [wheel](https://oldschool.runescape.wiki/w/Spinning_wheel)

`[oplocu]` / `[oploc2]` on the wheel. Anim `human_spinningwheel_90`.

| Lvl | Product | gameval | Input | gameval | XP | Status |
|---:|---|---|---|---|---:|---|
| 1 | [Ball of wool](https://oldschool.runescape.wiki/w/Ball_of_wool) | `ball_of_wool` | Wool | `wool` | 2.5 | ✅ |
| 1 | [Golden wool](https://oldschool.runescape.wiki/w/Golden_wool) | `viking_golden_wool` | Golden fleece | `viking_golden_fleece` 3693 | 2.5 | ➕ (Fremennik Trials) |
| 10 | [Bow string](https://oldschool.runescape.wiki/w/Bow_string) | `bow_string` | Flax | `flax` | 15 | ✅ |
| 10 | [Crossbow string](https://oldschool.runescape.wiki/w/Crossbow_string) | `xbows_crossbow_string` 9438 | Sinew | `xbows_sinew` 9436 | 15 | ➕ |
| 10 | Crossbow string | `xbows_crossbow_string` | Tree roots | `oak_roots` 6043 … `yew_roots` 6049 | 15 | ➕ |
| 12 | [Linen yarn](https://oldschool.runescape.wiki/w/Linen_yarn) | `linen_yarn` 31463 | Flax | `flax` | 16 | ➕ |
| 19 | [Magic string](https://oldschool.runescape.wiki/w/Magic_string) | `magic_string` 6038 | Magic roots | `magic_roots` 6051 | 30 | ➕ |
| 30 | [Rope](https://oldschool.runescape.wiki/w/Rope) | `rope` 954 | Yak hair | `yak_hair` 10814 | 25 | ➕ |
| 39 | [Hemp yarn](https://oldschool.runescape.wiki/w/Hemp_yarn) | `hemp_yarn` 31466 | Hemp | `hemp` 31457 | 60 | ➕ |
| 73 | [Cotton yarn](https://oldschool.runescape.wiki/w/Cotton_yarn) | `cotton_yarn` 31469 | Cotton boll | `cotton_boll` 31460 | 105 | ➕ |

Reference shape: Kronos `skills/crafting/SpinningWheel.java` (enum
`input, output, level, xp`) — the closest match to a dbtable here. LostCity
`skill_crafting/scripts/spinning/spinning.rs2` has the wheel plumbing only.

> The hemp / cotton / linen fibres carry *farming* inputs
> (`hemp_seed` 31543, `cotton_seed` 31545, `jute_seed` 5306) that the farming
> lane does not grow yet — see §3. The spin recipes still land; they simply
> have no renewable source until then.

### 1.2 Weaving — [loom](https://oldschool.runescape.wiki/w/Loom) *(queue #43)*

`[oploc1,loom]` verb **Weave**. Anim `farming_useloom` (2270). 4 ticks for the
first item, 3 per additional; drift net is 3 flat.

| Lvl | Product | gameval | Input | gameval | XP |
|---:|---|---|---|---|---:|
| 10 | [Strip of cloth](https://oldschool.runescape.wiki/w/Strip_of_cloth) | `regicide_cloth` 3224 | 4 × Ball of wool | `ball_of_wool` | 12 |
| 12 | [Bolt of linen](https://oldschool.runescape.wiki/w/Bolt_of_linen) | `bolt_of_linen` 31472 | 2 × Linen yarn | `linen_yarn` | 20 |
| 21 | [Empty sack](https://oldschool.runescape.wiki/w/Empty_sack) | `sack_empty` 5418 | 4 × Jute fibre | `jute_fibre` 5931 | 38 |
| 26 | [Drift net](https://oldschool.runescape.wiki/w/Drift_net) | `fossil_drift_net` 21652 | 2 × Jute fibre | `jute_fibre` | 55 |
| 36 | [Basket](https://oldschool.runescape.wiki/w/Basket) | `basket_empty` 5376 | 6 × Willow branch | `willow_branch` 5933 | 56 |
| 39 | [Bolt of canvas](https://oldschool.runescape.wiki/w/Bolt_of_canvas) | `bolt_of_canvas` 31475 | 2 × Hemp yarn | `hemp_yarn` | 75 |
| 73 | [Bolt of cotton](https://oldschool.runescape.wiki/w/Bolt_of_cotton) | `bolt_of_cotton` 31478 | 2 × Cotton yarn | `cotton_yarn` | 132 |

Reference shape: 2009scape
`content/global/skill/crafting/WeaveOptionPlugin.java` — three-option skill
dialog (sack / basket / cloth) + `WeavePulse` on anim 2270. LostCity has no
loom at rev 254.

**Trap:** `regicide_loom` (787) already owns an `[oplocu]` in
`quest_regicide/scripts/regicide_bombcraft.rs2:80`. A second binding on the
same name **replaces** rather than extends (see
`inverted-script-fallback` / the `[opheldu,leather]` bongos comment in
`leather.rs2`). Add the `Weave` op as an `[oploc1]` there, or route it to a
shared `~weave_menu` proc owned by `skill_crafting/scripts/weaving/`.

### 1.3 Pottery — [potter's wheel + oven](https://oldschool.runescape.wiki/w/Crafting#Pottery) *(queue #47)*

Soft clay = clay + `bowl_water`/`bucket_water`/`jug_water` ✅.

| Lvl | Product | unfired gameval | fired gameval | shape XP | fire XP | Status |
|---:|---|---|---|---:|---:|---|
| 1 | Pot | `pot_unfired` | `pot_empty` | 6.3 | 6.3 | ✅ |
| 3 | [Empty cup](https://oldschool.runescape.wiki/w/Empty_cup) | `cup_unfired` 28193 | `cup_empty` 1980 | 8.5 | 8.5 | ➕ **4 unfired cups per soft clay** |
| 7 | Pie dish | `piedish_unfired` | `piedish` | 15 | 10 | ✅ |
| 8 | Bowl | `bowl_unfired` | `bowl_empty` | 18 | 15 | ✅ |
| 19 | [Empty plant pot](https://oldschool.runescape.wiki/w/Empty_plant_pot) | `plantpot_unfired` 5352 | `plantpot_empty` 5350 | 20 | 17.5 | ➕ |
| 25 | [Pot lid](https://oldschool.runescape.wiki/w/Pot_lid) | `potlid_unfired` | `potlid` | 20 | 20 | ⚠️ **wrong in tree: level 3, 2.5/2.5 XP** (`pottery.rs2:60,88`) |

Pot lid also requires partial completion of
[One Small Favour](https://oldschool.runescape.wiki/w/One_Small_Favour); the
tree currently offers it unconditionally, which is the deliberate choice
recorded in `pottery.rs2`'s header — keep the availability, fix the numbers.

Six products no longer fit `~p_choice4`; this is the first branch that
**needs** §2 S1.

### 1.4 Leather + studded — [needle and thread](https://oldschool.runescape.wiki/w/Crafting#Leather)

One reel of `thread` = 5 crafts (`%thread_used`, already correct).

| Lvl | Product | gameval | Materials | XP | Status |
|---:|---|---|---|---:|---|
| 1 | Leather gloves | `leather_gloves` | leather | 13.8 | ✅ |
| 7 | Leather boots | `leather_boots` | leather | 16.3 | ✅ |
| 9 | Leather cowl | `leather_cowl` | leather | 18.5 | ✅ |
| 11 | Leather vambraces | `leather_vambraces` | leather | 22 | ✅ |
| 14 | Leather body | `leather_armour` | leather | 25 | ✅ |
| 18 | Leather chaps | `leather_chaps` | leather | 27 | ✅ |
| 28 | Hardleather body | `hardleather_body` | hard leather | 35 | ✅ |
| 32 | [Spiky vambraces](https://oldschool.runescape.wiki/w/Spiky_vambraces) | `spiked_vambraces` 10077 (+ 10079/10081/10083/10085 for green/blue/red/black) | any vambraces + `huntingbeast_claws` 10113 | 5.5 | ➕ |
| 38 | Coif | `coif` | leather | 37 | ✅ |
| 41 | [Hard leather shield](https://oldschool.runescape.wiki/w/Hard_leather_shield) | `leather_shield` 22269 | 2 × hard leather + `oak_shield` 22251 + 15 × `nails_bronze` 4819 + `hammer` 2347 | 70 | ➕ |
| 41 | Studded body | `studded_body` | leather body + `studs` | 40 | ✅ |
| 44 | Studded chaps | `studded_chaps` | leather chaps + `studs` | 42 | ✅ |

Reference shape for spiky vambraces: 2009scape
`content/global/skill/crafting/leather/SpikyVambraces.kt` (level 32, 6.0 XP —
the wiki table says 5.5, the wiki dragonhide prose says 6; **use the table**,
5.5, and note the conflict in the script header).

### 1.5 Dragonhide — [d'hide](https://oldschool.runescape.wiki/w/Crafting#Dragonhides)

Vambraces / chaps / bodies are ✅ for all four colours (and Royal, via the
rev-727 `rs2012_obj_24374` import). The four **shields** are the gap; each is
`2 × leather + wooden shield + 15 nails + hammer`, same shape as the hard
leather shield:

| Lvl | Shield | gameval | Wood shield | Nails | XP |
|---:|---|---|---|---|---:|
| 62 | Green d'hide shield | `green_dhide_shield` 22275 | `maple_shield` 22257 | `nails` 1539 ×15 | 124 |
| 69 | Blue d'hide shield | `blue_dhide_shield` 22278 | `yew_shield` 22260 | `nails_mithril` 4822 ×15 | 140 |
| 76 | Red d'hide shield | `red_dhide_shield` 22281 | `magic_shield` 22263 | `nails_adamant` 4823 ×15 | 156 |
| 83 | Black d'hide shield | `black_dhide_shield` 22284 | `redwood_shield` 22266 | `nails_rune` 4824 ×15 | 172 |

The wooden shields themselves are **Fletching** —
[`FLETCHING_COMPLETION_PLAN.md`](FLETCHING_COMPLETION_PLAN.md) §2.14 owns
them. Land the d'hide shields after that slice, or gate on the shield objs
existing in the shops.

### 1.6 Snakeskin — [snakeskin](https://oldschool.runescape.wiki/w/Crafting#Snakeskin)

Hides tan for 15 gp. Sources: bush snakes (Mos Le'Harmless), Hoop Snakes
(Tar Swamp), Karamja (Tai Bwo Wannai Cleanup), swamp snakes (Temple
Trekking).

| Lvl | Product | gameval | Materials | XP |
|---:|---|---|---|---:|
| 35 | [Broodoo shield](https://oldschool.runescape.wiki/w/Broodoo_shield) | `broodoo_poisonshield` 6235 / `broodoo_diseaseshield` 6257 / `broodoo_combatshield` 6279 | 2 snakeskin + tribal mask + 8 steel nails | 100 |
| 45 | Snakeskin boots | `snakeskin_boots` 6328 | 6 × snakeskin | 30 |
| 47 | Snakeskin vambraces | `snakeskin_vambraces` 6330 | 8 × snakeskin | 35 |
| 48 | Snakeskin bandana | `snakeskin_bandana` 6326 | 5 × snakeskin | 45 |
| 51 | Snakeskin chaps | `snakeskin_chaps` 6324 | 12 × snakeskin | 50 |
| 53 | Snakeskin body | `snakeskin_body` 6322 | 15 × snakeskin | 55 |
| 56 | Snakeskin shield | `snakeskin_shield` 22272 | 2 snakeskin + `willow_shield` 22254 + 15 iron nails | 100 |

Inputs: `village_snake_hide` 6287 / `templetrek_swamp_snake_hide` 7801 →
`village_snake_skin` 6289 via a tanner. Reference shape: 2009scape
`crafting/SnakeSkinPlugin.java` + `armour/SnakeSkin.java`.

**Tanning gap:** `tanner.rs2`'s `tan_leather_choices` offers soft / hard /
dragonhide only. Snakeskin tanning is a fourth row at 15 gp — add it to the
same menu (it is one `~tan_leather` call), and register
[Sseerra](https://oldschool.runescape.wiki/w/Sseerra) / `auburn_tanner` if
those NPCs are wanted as additional tanners.

### 1.7 Yak hide — ✅ complete

`[opheldu,needle]` → `~fris_craft_yak_armour`
(`quest_thefremennikisles/scripts/fris_shared.rs2`). Wiki: legs 43 / 32 XP
(1 cured hide), top 46 / 32 XP (2 cured hides), cure 5 gp. `yak_hide` 10818 →
`yak_hide_cured` 10820 → `yak_hide_armour_greaves` 10824 /
`yak_hide_armour_body` 10822. **Verify the two levels and both XP values
against the wiki when S13 lands** — the quest script was written for the quest,
not for the skill table.

### 1.8 Snelms — ✅ complete

All nine [snelms](https://oldschool.runescape.wiki/w/Snelm) at level 15 /
32.5 XP, chisel on shell. Colour mapping (cache name → wiki name):
`swamp`=myre, `red+black`=blood'n'tar, `yellow`=ochre, `blue`=bruise blue,
`round_orange`=broken bark.

### 1.9 Crab armour — [Recipe for Disaster](https://oldschool.runescape.wiki/w/Crafting#Crab_armour)

| Lvl | Product | Materials | XP |
|---:|---|---|---:|
| 15 | Crab helmet | `hundred_pirate_crab_shell_head` 7538 + chisel | 32.5 |
| 15 | Crab claw | fresh crab claw 7536 + chisel | 32.5 |

Two more cases on the existing `[opheldu,chisel]` switch. Gated on the
[Freeing Pirate Pete](https://oldschool.runescape.wiki/w/Freeing_Pirate_Pete)
subquest, which `quest_recipefordisaster/scripts/recipefordisaster_pirate.rs2`
already implements.

### 1.10 Fabric / fur armour families

All four use needle + thread; none exists in tree.

**[Xerician robes](https://oldschool.runescape.wiki/w/Xerician_robes)** —
`xeric_fabric` 13383 (Lizardmen / Stone chests):
hat 14 / 3 fabric / 66 XP (`xeric_hat` 13385) · robe 17 / 4 / 88
(`xeric_robe` 13389) · top 22 / 5 / 110 (`xeric_top` 13387).

**[Splitbark armour](https://oldschool.runescape.wiki/w/Splitbark_armour)** —
`hollow_bark` 3239 + `fine_cloth` 3470 (Shades of Mort'ton):
gauntlets 60 / 1+1 / 62 (`splitbark_gauntlets` 3391) · boots 60 / 1+1 / 62
(`splitbark_greaves` 3393) · helm 61 / 2+2 / 124 (`splitbark_helm` 3385) ·
legs 62 / 3+3 / 186 (`splitbark_legs` 3389) · body 62 / 4+4 / 248
(`splitbark_body` 3387).
`minigame_mortton` and `areas/wizard_tower/scripts/armourmaking_wizard.rs2`
already reference these objs.

**[Mixed hide armour](https://oldschool.runescape.wiki/w/Mixed_hide_armour)** —
`hg_mixedhide_base` 29292 + Hunter furs:
cape 68 / +jaguar fur (`varlamore_jaguar_fur` 29218) / 62 · boots 69 /
+sunlight antelope fur / 75 · legs 71 / +3 fox fur / 210 · top 72 / +2
sunlight antelope fur / 150.

**[Hueycoatl hide armour](https://oldschool.runescape.wiki/w/Hueycoatl_hide_armour)** —
`huey_hide` 30085:
vambraces 76 / ×1 / 95 · coif 76 / ×2 / 190 · chaps 77 / ×2 / 190 ·
body 78 / ×3 / 285.

### 1.11 Glass — [glassblowing](https://oldschool.runescape.wiki/w/Crafting#Glass) *(queue #45)*

`bucket_sand` 1783 + `soda_ash` 1781 at a furnace → `molten_glass` 1775
(20 XP) ✅. `glassblowingpipe` 1785 on molten glass → product.

| Lvl | Product | gameval | XP | Status |
|---:|---|---|---:|---|
| 1 | Beer glass | `beer_glass` | 17.5 | ✅ |
| 4 | [Empty candle lantern](https://oldschool.runescape.wiki/w/Empty_candle_lantern) | `candle_lantern_empty` 4527 | 19 | ➕ |
| 12 | [Empty oil lamp](https://oldschool.runescape.wiki/w/Empty_oil_lamp) | `oil_lamp_empty` 4525 | 25 | ➕ |
| 26 | [Oil lantern](https://oldschool.runescape.wiki/w/Oil_lantern) | `oil_lantern_unlit` 4537 | 50 | ➕ **assembly**: empty oil lamp + `oil_lantern_frame` 4540 |
| 33 | Vial | `vial_empty` | 35 | ✅ |
| 42 | [Empty fishbowl](https://oldschool.runescape.wiki/w/Fishbowl) | `fishbowl_empty` 6667 | 42.5 | ➕ |
| 46 | Unpowered orb | `stafforb` 567 | 52.5 | ✅ |
| 49 | [Lantern lens](https://oldschool.runescape.wiki/w/Lantern_lens) | `bullseye_lantern_lens` 4542 | 55 | ➕ |
| 87 | [Empty light orb](https://oldschool.runescape.wiki/w/Empty_light_orb) | `dorgesh_lightbulb_nofilament` 10980 | 70 | ➕ |
| 87 | [Light orb](https://oldschool.runescape.wiki/w/Light_orb) | *(filled)* | 104 | ➕ **assembly**: empty light orb + `dorgesh_wire` 10981 |

Plus the two deferrals named in `glass.rs2`'s header, both now unblocked:
**lens mould → telescope disc** (`lens_mould` 602, Observatory Quest) and
**bullseye lantern** (`bullseye_lantern_nolens` 4544 + lantern lens →
`bullseye_lantern_empty` 4546 — a light-source assembly, not a glassblow).

Reference shape: Kronos `skills/crafting/Glass.java` (enum
`level, xp, item, article`) — one-for-one with the table above; 2009scape
`crafting/glass/GlassProduct.kt` + `lightsources/LanternCrafting.kt` for the
assemblies.

Sand pits to bind: §0.3. Soda ash comes from burning seaweed on a range/fire —
check the cooking lane owns that before assuming a source exists.

### 1.12 Gem cutting — [gems](https://oldschool.runescape.wiki/w/Crafting#Gems) *(queue #46)*

Eight rows ✅ with correct crush rates. Two missing, both plain `gem.dbrow`
additions (no success-rate columns — sapphire-and-above never fail):

| Lvl | Product | gameval | Input | XP |
|---:|---|---|---|---:|
| 67 | [Onyx](https://oldschool.runescape.wiki/w/Onyx) | `onyx` 6573 | `uncut_onyx` 6571 | 167.5 |
| 89 | [Zenyte](https://oldschool.runescape.wiki/w/Zenyte) | `zenyte` 19493 | `uncut_zenyte` 19496 | **see note** |

> **Conflict to resolve at implementation time:** the wiki gem table lists
> zenyte cutting at **50 XP**; Kronos `skills/crafting/Gem.java:23` uses
> **200.0**. The wiki is the authority per the queue methodology — take 50,
> and record the conflict in the dbrow comment so the next reader does not
> "fix" it. Zenyte's uncut form comes from combining an onyx with three
> zenyte shards at the [Zalcano](https://oldschool.runescape.wiki/w/Zenyte)/
> demonic gorilla chain, which is not this lane's problem.

Also out of era but worth a line in the header: the
[Jeweller's chisel](https://oldschool.runescape.wiki/w/Jeweller%27s_chisel)
(id 34024, July 2026) adds +20% semi-precious success and a 10% double-cut. It
is **not** in this cache (`grep jeweller configs/all.obj.compack` → nothing).
Do not add it.

### 1.13 Silver jewellery — [silver](https://oldschool.runescape.wiki/w/Crafting#Silver_jewellery) *(queue #46)*

Silver bar + semi-precious gem + mould at a furnace. **Entirely absent.**
`craft_silver` in `jewellery.rs2` only offers symbol / emblem / sickle, and
never looks at a gem.

| Lvl | Product | gameval | Gem | XP |
|---:|---|---|---|---:|
| 1 | Opal ring | `opal_ring` 21081 | opal | 10 |
| 13 | Jade ring | `jade_ring` 21084 | jade | 32 |
| 16 | Topaz ring | `topaz_ring` 21087 | red topaz | 35 |
| 16 | Opal necklace | `opal_necklace` 21090 | opal | 35 |
| 22 | Opal bracelet | `opal_bracelet` 21117 | opal | 45 |
| 25 | Jade necklace | `jade_necklace` 21093 | jade | 54 |
| 27 | Opal amulet (u) | `unstrung_opal_amulet` 21099 | opal | 55 |
| 29 | Jade bracelet | `jade_bracelet` 21120 | jade | 60 |
| 32 | Topaz necklace | `topaz_necklace` 21096 | red topaz | 70 |
| 34 | Jade amulet (u) | `unstrung_jade_amulet` 21102 | jade | 70 |
| 38 | Topaz bracelet | `topaz_bracelet` 21123 | red topaz | 75 |
| 45 | Topaz amulet (u) | `unstrung_topaz_amulet` 21105 | red topaz | 80 |

Reference shape: Kronos `skills/crafting/SilverCasting.java` (rows 26–40 are
this table verbatim). The three opal/jade/topaz amulets also need
`stringing.rs2` cases (§1.14 note).

### 1.14 Gold jewellery — [gold](https://oldschool.runescape.wiki/w/Crafting#Gold_jewellery) *(queue #46)*

Rings / necklaces / amulets for gold→dragonstone are ✅ and wiki-exact. What is
missing: **the whole bracelet column**, the gold tiara, the onyx and zenyte
tiers, and the two slayer rings.

| Lvl | Product | gameval | Gem | XP |
|---:|---|---|---|---:|
| 7 | Gold bracelet | `jewl_gold_bracelet` 11069 | — | 25 |
| 23 | Sapphire bracelet | `jewl_sapphire_bracelet` 11072 | sapphire | 60 |
| 30 | Emerald bracelet | `jewl_emerald_bracelet` 11076 | emerald | 65 |
| 42 | Ruby bracelet | `jewl_ruby_bracelet` 11085 | ruby | 80 |
| 42 | [Gold tiara](https://oldschool.runescape.wiki/w/Gold_tiara) | `tiara_gold` 26788 | — (tiara mould) | 35 |
| 58 | Diamond bracelet | `jewl_diamond_bracelet` 11092 | diamond | 95 |
| 67 | Onyx ring | `onyx_ring` 6575 | onyx | 115 |
| 74 | Dragonstone bracelet | `jewl_dragonstone_bracelet` 11115 | dragonstone | 110 |
| 75 | [Slayer ring](https://oldschool.runescape.wiki/w/Slayer_ring) | `slayer_ring_3` 11871 | `slayer_gem` 4155 | 15 |
| 75 | Slayer ring (eternal) | *(eternal gem)* | eternal gem | 15 |
| 82 | Onyx necklace | `onyx_necklace` 6577 | onyx | 120 |
| 84 | Onyx bracelet | `jewl_onyx_bracelet` 11130 | onyx | 125 |
| 89 | Zenyte ring | `zenyte_ring` 19538 | zenyte | 150 |
| 90 | Onyx amulet (u) | `unstrung_onyx_amulet` 6579 | onyx | 165 |
| 92 | Zenyte necklace | `zenyte_necklace` 19535 | zenyte | 165 |
| 95 | Zenyte bracelet | `zenyte_bracelet` 19532 | zenyte | 180 |
| 98 | Zenyte amulet (u) | `unstrung_zenyte_amulet` 19501 | zenyte | 200 |

Requires `jewl_bracelet_mould` 11065 in the mould detection in
`craft_gold_menu` (a fourth kind) and `tiara_mould` 5523 for the gold tiara.
Reference shape: Kronos `skills/crafting/Mould.java` — the four blocks
(ring / necklace / amulet / bracelet) are this table exactly, including both
slayer rings.

**Stringing** (`stringing.rs2`, 40 XP flat) needs the six new unstrung
amulets: opal, jade, topaz, onyx, zenyte — and the level-1 unlock list on the
wiki confirms all of them string at level 1 for 40 XP, same as the existing
six.

### 1.15 Silver casting — [silver](https://oldschool.runescape.wiki/w/Crafting#Silver)

| Lvl | Product | gameval | Mould | XP | Status |
|---:|---|---|---|---:|---|
| 16 | Unstrung symbol | `nostringstar` 1714 | `holy_symbol_mould` 1599 | 50 | ✅ |
| 17 | Unstrung emblem | `nostringsnake` 1720 | `unholy_symbol_mould` 1594 | 50 | ✅ |
| 18 | Silver sickle | `silver_sickle` 2961 | `sickle_mould` 2976 | 50 | ✅ |
| 21 | [Silver bolts (unf)](https://oldschool.runescape.wiki/w/Silver_bolts_(unf)) | `xbows_crossbow_bolts_silver_unfeathered` 9382 | `xbows_silver_bolt_mould` 9434 | 50 | ➕ **×10 per bar** |
| 23 | [Tiara](https://oldschool.runescape.wiki/w/Tiara) | `tiara` 5525 | `tiara_mould` 5523 | 52.5 | ➕ |

The tiara is the notable one: `skill_runecraft/scripts/runecraft.rs2:39`
already **consumes** a tiara to attune it at an altar, but nothing in the tree
can **make** one. Runecraft's tiara path is unreachable except by drop/shop
today.

### 1.16 Battlestaves — ✅ complete

Water 54 / 100 · Earth 58 / 112.5 · Fire 62 / 125 · Air 66 / 137.5. All four
match the wiki.

### 1.17 Amethyst — [amethyst](https://oldschool.runescape.wiki/w/Crafting#Amethyst)

Chisel on `amethyst` 21347. Yields differ per product.

| Lvl | Product | gameval | XP | Yield |
|---:|---|---|---:|---:|
| 83 | Amethyst bolt tips | `xbows_bolt_tips_amethyst` 21338 | 60 | 15 |
| 85 | Amethyst arrowtips | `amethyst_arrowheads` 21350 | 60 | 15 |
| 87 | Amethyst javelin heads | `amethyst_javelin_head` 21352 | 60 | 5 |
| 89 | Amethyst dart tip | `amethyst_dart_tip` 25853 | 60 | 8 |

Yields from Kronos `skills/crafting/Amethyst.java:13-15` (which predates the
dart tip — take 8 from the wiki's
[Amethyst dart tip](https://oldschool.runescape.wiki/w/Amethyst_dart_tip)).

**Coordination:** `[opheldu,chisel]` lives in
`skill_crafting/scripts/gem/uncut_gem.rs2`. Fletching's plan lists amethyst as
its §2.9 / S9. A second `[opheldu,chisel]` anywhere is a **hard compile
error** — `ssc_compile.c:2845` rejects duplicate script names outright ("declare
it once and branch into a `[label,...]` from the other file"), so this cannot
silently shadow. The cost is coupling, not corruption: Fletching cannot land
amethyst without editing a Crafting file.

The way out is the merge pattern already used for `@make_bolt_tips`,
`@craft_snelm` and the five quest branches: a `case amethyst :` in the chisel
switch calling a fletching-owned `@make_amethyst_products` label. **Not** a
`[opheldu,_amethyst]` category binding in the fletching tree — that is
unreachable while `chisel` is type-bound, because the runtime tries clicked
type → dragged type → categories, and the dragged-type rung catches `chisel`
in either click order (`mock230_scripts.c:2536-2562`; the reference is
identical). A category binding on a family whose partner is a tool is dead
code and a tripwire for other pairs. The structural fix — moving the chisel
router to `general_use/scripts/tools/` so neither skill edits the other — is
[`TOOL_TRIGGER_ORGANISATION.md`](TOOL_TRIGGER_ORGANISATION.md) S3.

### 1.18 Birdhouses — [birdhouse trapping](https://oldschool.runescape.wiki/w/Birdhouse_trapping)

Nine tiers, logs + `poh_clockwork_mechanism` 8792 + chisel + hammer, levels
5/15/25/35/45/50/60/75/90 for 15/20/25/30/35/40/45/50/55 XP. Objs are all
present (`birdhouse_normal` 21512, `birdhouse_oak` 21515, …).

**This is the Hunter lane's** — the crafting step is inseparable from the trap
placement, seed loading and Fossil Island loop. Route to
`KRONOS_CONTENT_PORT_QUEUE.md` rather than implementing half of it here.

### 1.19 Dyes — [dye](https://oldschool.runescape.wiki/w/Dye)

Cape matrix and the three mixes are ✅. Missing targets, all one-line switch
cases on the existing `[opheldu,<dye>]` blocks:

| Dye | gameval | Target → product |
|---|---|---|
| Blue | `bluedye` | wizard hat → blue wizard hat · wizard robe → blue wizard robe · black robe → blue robe |
| Black (`golem_ink` 4622, from [black mushrooms](https://oldschool.runescape.wiki/w/Black_dye)) | `golem_ink` | wizard hat → black · robe top/bottom → black · desert shirt/robe → black |
| Pink (`handsand_pink_dye` 6955) | | lantern lens → rose-tinted lens (While Guthix Sleeps); pink cape |

Dye **sources** are ✅ (`aggie.rs2` red/yellow/blue for woad leaves/onions/
redberries; Betty sells pink dye post-quest per
[Pink dye](https://oldschool.runescape.wiki/w/Pink_dye)).

### 1.20 Crafting Guild, cape, shops *(queue #48)*

[Crafting Guild](https://oldschool.runescape.wiki/w/Crafting_Guild), 40
Crafting, south-west of Falador.

| Feature | Status |
|---|---|
| Door: 40 Crafting + `brown_apron` | ✅ `crafting_guild.rs2` |
| Door: `golden_apron` 20208 / `skillcape_crafting` / max cape as alternates | ➕ — the wiki lists all four |
| Master Crafter greeting | ✅ (one of three; `master_crafter_2` 5811 / `_3` 5812 unbound) |
| [Crafting cape](https://oldschool.runescape.wiki/w/Crafting_cape) shop — 99,000 gp at 99, gives hood | ➕ |
| Cape worn perk: **+1 Crafting boost** | ➕ — mirror `skill_herblore/scripts/skillcape_herblore.rs2`'s `~skillcape_*_boost` proc pattern |
| Cape perk: unlimited teleport to the guild (2015 poll) | ➕ — `tele_destinations.rs2` already knows the guild coord |
| Ground floor: 4 potter's wheels, pottery oven, sink | ✅ via categories |
| Ground floor respawns: jug, chisel, hammer, amulet + bracelet moulds | ➕ (loc/obj spawns) |
| Upper floor: tanner + 2 spinning wheels | ➕ — the tanner exists (`tanner` npc), the guild's own placement does not |
| Upper floor respawns: ring / tiara / necklace / holy moulds, shears | ➕ |
| Bank chest + deposit box, gated on hard Falador Diary **or** 99 Crafting | ➕ (diary lane) |
| Guild mine (7 gold, 6 silver, 6 clay rocks) | Mining lane |

Crafting shops that sell the tools —
[Dommik's Crafting Store](https://oldschool.runescape.wiki/w/Dommik%27s_Crafting_Store)
(Al Kharid) and the Rimmington store — already have `.inv` files
(`shop/al_kharid/configs/dommiks_crafting_store.inv`); confirm they stock
needle, thread, and all six moulds.

### 1.21 Tools and moulds — the complete list

| Item | gameval | Used for | In shops? |
|---|---|---|---|
| Needle | `needle` 1733 | all leather / hide / fabric | check |
| Thread | `thread` 1734 | ditto, 5 crafts per reel | check |
| Chisel | `chisel` 1755 | gems, snelms, amethyst, crab, limestone, birdhouses | ✅ |
| Glassblowing pipe | `glassblowingpipe` 1785 | glass | respawns on Entrana |
| Shears | `shears` | wool | ✅ |
| Ring mould | `ring_mould` 1592 | rings | ✅ |
| Necklace mould | `necklace_mould` 1597 | necklaces | ✅ |
| Amulet mould | `amulet_mould` 1595 | amulets | ✅ |
| Bracelet mould | `jewl_bracelet_mould` 11065 | bracelets | ➕ **unused today** |
| Tiara mould | `tiara_mould` 5523 | tiara, gold tiara | ➕ **unused today** |
| Holy mould | `holy_symbol_mould` 1599 | unstrung symbol | ✅ |
| Unholy mould | `unholy_symbol_mould` 1594 | unstrung emblem | ✅ |
| Sickle mould | `sickle_mould` 2976 | silver sickle | ✅ |
| Bolt mould | `xbows_silver_bolt_mould` 9434 | silver bolts (unf) | ➕ **unused today** |
| Lens mould | `lens_mould` 602 | telescope disc (Observatory Quest) | ➕ deferred in `glass.rs2` |
| Hammer | `hammer` | shields, birdhouses | ✅ |

### 1.22 Adjacent crafting-gated recipes that are *not* skill_crafting's

Listed so the audit is complete and so nobody re-scopes them here. Every one
is on the wiki
[level-up table](https://oldschool.runescape.wiki/w/Crafting/Level_up_table).

| Lvl | Thing | Owner |
|---:|---|---|
| 3 | Polished buttons (`anma_p_buttons` 10496) | `quest_animalmagnetism` ✅ |
| 10 | Extract sinew from damaged monkey tails | Cooking / Barbarian training |
| 11–97 | Pyre ships (13 log tiers) | [Barbarian Training](https://oldschool.runescape.wiki/w/Barbarian_Firemaking) — Firemaking lane |
| 12 | Limestone brick (`limestonebrick` 3420, chisel on `limestone` 3211, 6 XP) | Construction lane; 2009scape `crafting/limestone/` has the shape |
| 12–81 | Gem pouches / satchels / totes / sacks | Mining lane |
| 30 | Repair Piscarilius fishing cranes | Kourend |
| 35–65 | Meat / fur / clothes pouches | Hunter lane |
| 37 | Strung rabbit foot | Hunter |
| 38–43 | Heraldic helms / kiteshields | Construction (Sir Renitee) |
| 52 | Serpentine helm | ✅ `zulrah_item_charges.rs2` |
| 55 | Slayer helmet | ✅ `skill_slayer/scripts/slayer_helm.rs2` |
| 56 | Colossal pouch | Runecraft |
| 59–74 | Toxic staff of the dead, trident of the swamp, purging staff | Combat/Magic lanes (`trident.rs2` ✅) |
| 62 | Bryophyta's staff | Boss lane |
| 70–82 | Crystal equipment, celestial signet, Blade of Saeldor, Bow of Faerdhinen | Song of the Elves — `minigame_gauntlet/scripts/crystal_equipment.rs2` has the pattern |
| 75 | Divine rune pouch | Runecraft |
| 80 | Ancient rings | Magic |
| 84–86 | Necklace of rupture, Amulet of rancour | Boss lane |
| 90 | Masori armour, Armadylean plates | Boss lane |
| 60 | Golem crafting (Wyrmscraig, July 2026) | **out of era** — not in this cache |

### 1.23 Quests

30 quest scripts already grant Crafting XP (`grep -rn "stat_advance(crafting"`).
[Quests requiring Crafting](https://oldschool.runescape.wiki/w/Crafting#Quests_requiring_Crafting)
runs from The Giant Dwarf (12) to Song of the Elves; the level gates are quest-lane
concerns. The only quest interaction this plan owns is **One Small Favour →
pot lid** (§1.3).

---

## 2. The slices

Ordered by dependency. Each is one Finish-queue tick.

| # | Slice | Queue row | Depends on |
|---|---|---|---|
| **S1** | `~skill_multi` on interface 270 + Make-All batching | **#44** | — |
| **S2** | Pottery: cup, plant pot, pot lid data fix | **#47** | S1 (6 products) |
| **S3** | Spinning remainder: 8 rows + regional wheel bindings | *new row* | — |
| **S4** | Weaving: loom binding + 7 rows | **#43** | S1, S3 (yarns) |
| **S5** | Glass: 5 blows + 3 assemblies + sand pit bindings | **#45** | S1 |
| **S6** | Gems: onyx + zenyte dbrows | **#46a** | — |
| **S7** | Silver jewellery: 12 rows + gem-aware `craft_silver` | **#46b** | S1 |
| **S8** | Gold jewellery: bracelets, gold tiara, onyx, zenyte, slayer rings + 6 stringing rows | **#46c** | S1 |
| **S9** | Silver casting: tiara + silver bolts (unf) | *new row* | S7 |
| **S10** | Leather: spiky vambraces, hard leather shield, 4 d'hide shields | *new row* | Fletching S13 (wooden shields) |
| **S11** | Snakeskin: 7 products + tanner row | *new row* | S1 |
| **S12** | Amethyst: 4 tips (as cases in crafting's chisel switch) | *new row* | coordinate with Fletching S9 |
| **S13** | Fabric/fur armour: Xerician, Splitbark, Mixed hide, Hueycoatl, crab armour | *new row* | S1 |
| **S14** | Dye targets beyond capes | *new row* | — |
| **S15** | Crafting Guild build-out + cape shop + cape perks | **#48** | — |
| **S16** | Selftest suite | — | rolling, land with each slice |

### S1 · `~skill_multi` on interface 270 — the one build worth doing first

Eight `~p_choice*` menus in this skill are placeholders for it, and one of
them (`craft_leather_menu`) is a four-page chain. Every subsequent slice adds
products that will not fit three rows.

The mechanism is proven — `interface_chat/scripts/chat.rs2:190-210`'s
`~p_choice_open` is exactly this shape one interface over:

```
[proc,skill_multi_open](int $count, int $max, string $title, ...18 objs...)
if_opensub(skillmulti, ...);
runclientscript*(2046)(mode, max, obj1..obj18, initial_qty, title);
if_addresumebutton(skillmulti:bottom);
p_pausebutton;
// answer arrives as last_slot (which of a..r) + varc 200 skillmulti_quantity
```

`runclientscript*` (vararg, `SS_OP_RUNCLIENTSCRIPTVARARG` 11003) and
`runclientscript_ss` (11002) both exist and are already used by `chat.rs2`. No
new opcode is required. Deliverables:

1. `interface_skill_multi/scripts/skill_multi.rs2` — `~skill_multi(...)`
   returning `(obj, count)`, plus the `[if_button]` handlers for `1/5/10/x/all`
   and the `other` numeric-entry path (`p_countdialog`).
2. A **batch** helper: `~craft_repeat(product, count)` that loops with the
   per-item delay and stops on out-of-materials / inventory-full — the
   `weakqueue` the LostCity scripts used and every port dropped.
3. Convert `pottery.rs2`, `glass.rs2`, `leather.rs2`, `jewellery.rs2` from
   `~p_choice*` to `~skill_multi`. Keep the `~p_choice*` calls in the tanner
   and the guild dialogue — those are conversations, not make-menus.

This is the same slice as **Fletching S2**. Whichever lane lands it first owns
the file; the other consumes it. Do not build it twice.

### S2 · Pottery *(queue #47)*

Add `cup_unfired`/`cup_empty` (level 3, 8.5/8.5, **`inv_add(inv, cup_unfired, 4)`
per soft clay**) and `plantpot_unfired`/`plantpot_empty` (19, 20/17.5). Fix the
pot lid to level 25 / 20 / 20. Move the six-way level/XP switch in
`craft_pottery_menu` / `fire_pottery_menu` into a `pottery.dbtable` — the
switch is already at the limit of readability and S1 turns it into a loop.
Correct the queue row's "urns" premise in its Notes.

### S3 · Spinning remainder

Extend `[oplocu,spinningwheel]` to the ten-row table. Move the
`if/else` chain to a `spinning.dbtable` (`input, product, level, xp`) — the
Kronos enum maps to it directly. Bind the seven unbound wheel locs (§0.3);
each is `[oplocu]`+`[oploc2]` aliasing the same labels, which is why a table
matters. The Seers' Village wheel spins 33% faster with the medium Kandarin
diary — note it, defer to the diary lane.

### S4 · Weaving *(queue #43)*

New `skill_crafting/scripts/weaving/weaving.rs2` + `weaving.dbtable`.
`[oploc1,loom]` opens `~skill_multi` with the 7 products; anim 2270; 4-tick
first / 3-tick subsequent, drift net 3 flat. **Merge** into
`regicide_bombcraft.rs2` for `regicide_loom` rather than re-binding it.

### S5 · Glass *(queue #45)*

Five new blows into `glass.dbtable`; three assemblies as `[opheldu]` pairs
(oil lamp + frame, light orb + wire, lantern lens + bullseye lantern frame).
Bind the four unbound sand pits. Clear the two `glass.rs2` deferrals
(lens mould / telescope disc; lantern glass).

### S6–S9 · Jewellery *(queue #46)*

`gem.dbrow` +2. Then the big one: replace `~jewellery_gold_data` /
`~jewellery_silver_data` — 8 nested switches across 60 lines — with a
`jewellery.dbtable` keyed `(mould, gem)` carrying `product, level, xp,
members`, and let `craft_gold_menu` / `craft_silver` drive it off which moulds
the player holds. That is 47 rows of data replacing ~120 lines of switch, and
it is the only way the bracelet column, gold tiara, onyx, zenyte and the two
slayer rings fit without a fifth `p_choice` page. Then `stringing.rs2` +6
rows, and the silver casting pair (tiara, `×10` silver bolts).

### S10–S14

Straightforward table extensions once S1 exists. S12's only subtlety is the
shared `[opheldu,chisel]` (§1.17). S10 waits on Fletching's wooden shields.

### S15 · Guild and cape *(queue #48)*

Door alternates (golden apron / crafting cape / max cape), the cape shop on a
Master Crafter, the `~skillcape_crafting_boost` proc (mirror
`skillcape_herblore.rs2` exactly — do **not** edit the shared
`~skillcape_boost` if-chain, that is what the herblore header warns about),
the guild teleport, and the mould/tool respawns.

---

## 3. Cross-lane routing

| Gap | Route to |
|---|---|
| Birdhouses (9) | `KRONOS_CONTENT_PORT_QUEUE.md` — Hunter |
| Hemp / cotton / jute / flax patches (weaving + spinning inputs) | `FARMING_COMPLETION_PLAN.md` |
| Wooden shields for the 5 shield recipes | `FLETCHING_COMPLETION_PLAN.md` §2.14 |
| Amethyst ammo *after* the tips | `FLETCHING_COMPLETION_PLAN.md` §2.9 |
| Soda ash from burning seaweed | Cooking lane — verify a range/fire `[opheldu,seaweed]` exists |
| Uncut gems, gold/silver/clay rocks, amethyst | `MINING_COMPLETION_PLAN.md` (✅ landed) |
| Superglass Make (lunar) | `MAGIC_CONTENT_PORT_PLAN.md` |
| Bert's 84 buckets of sand / daily | `quest_handinthesand` (partly in tree) |
| Limestone brick | Construction lane |
| Crystal / Masori / boss-drop crafting | boss + Song of the Elves lanes |
| Guild bank chest diary gate | diary lane |
| Golem crafting, Jeweller's chisel | **out of era — do not port** |

---

## 4. Selftests

Follow the fishing/mining pattern: one suite per slice, run headless via
`mock230_dev` with a scratch `MOCK230_SAVES`
(`headless-runs-are-not-independent`).

| id | asserts |
|---|---|
| C1 | soft clay → 6 pottery shapes → 6 fired products; levels + XP exact; pot lid is 25/20/20 |
| C2 | 10 spin rows produce the right product and XP; a wheel that is not `spinningwheel` works |
| C3 | 7 weave rows; sack consumes 4 jute, basket 6 willow branches, drift net 2 jute |
| C4 | glass: sand+ash→molten (20 XP), 8 blows, 3 assemblies |
| C5 | gems: 10 cuts; opal/jade/topaz crush path yields `crushed_gemstone` + quarter XP; onyx/zenyte never crush |
| C6 | jewellery: all 47 mould×gem combinations gate on the right mould, gem and level |
| C7 | stringing: 14 unstrung → strung, 40 XP each |
| C8 | leather: thread reel = exactly 5 crafts; d'hide bodies eat 3 hides; shields eat shield + 15 nails |
| C9 | snakeskin: tanner charges 15 gp; 6 products consume the documented skin counts |
| C10 | amethyst: 4 tips with yields 15/15/5/8 — **and gem cutting still works** (the chisel-switch regression guard) |
| C11 | guild: door refuses at 39, accepts at 40 with each of the four garments; cape boost +1; shop sells at 99 only |

Per `verify-blocker-and-failing-test`: for at least C10, mutate the chisel
switch to prove the assertion can fail before calling it green.

---

## 5. Explicitly out of this lane

Golem crafting and the Jeweller's chisel (2026, absent from this cache);
birdhouses (Hunter); pyre ships (Firemaking); all crystal / Masori / boss-drop
crafting; the Falador-diary bank chest; Superglass Make; Sailing.

---

## 6. Verification

Every slice ends with:

```
python3 tools/mock230_pack.py --check-only          # 0 errors
make -C src mock230-scripts                          # sscompile green
make -C src test-crafting                            # the C-suite above
```

plus the queue discipline: mark the Finish row `done`, append a Log line, and
**never** `.rs2.skip` a sibling lane to green the compile
(`PORTING_GUIDE.md` §7).
