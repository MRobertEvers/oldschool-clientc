# Finishing Farming

Plan to take `skill_farming/` from its current vertical slice to the complete
[Farming](https://oldschool.runescape.wiki/w/Farming) skill. Wiki is the
authority for content (seeds, levels, XP, payments, locations); the cache
(`OSRS-Content/osrs239-content/configs/all.obj`, `all.loc`, `all.varbit`) is the
authority for names and for what is *expressible* at this revision.

Queue items this closes: **#131–#137** in
[`SKILLS_CONTENT_PORT_QUEUE.md`](SKILLS_CONTENT_PORT_QUEUE.md). #138 (Tithe /
Guild remainder) stays blocked on KRONOS and is scoped at the bottom.

---

## 0. Where farming stands today — measured

`OSRS-Content/osrs239-content/server/scripts/skill_farming/` is **4,925 lines**
across 12 scripts and 19 config files. It is a working vertical slice, not a
stub: raking, planting, growth via `softtimer` + `date_minutes` catch-up,
harvesting with a lives roll, compost bins, and `farming_view` painting all
work end to end.

What it covers, counted from the `.dbrow` files:

| category | rows in tree | rows in OSRS | file |
|---|---:|---:|---|
| allotment | **3** (potato, onion, cabbage) | 8 | `crops.dbrow` |
| flower | **1** (marigold) | 6 | `crops.dbrow` |
| herb | **4** (guam → harralander) | 16 | `herbs.dbrow` |
| hops | **1** (barley) | 10 | `hops.dbrow` |
| bush | **1** (redberry) | 6 | `bushes.dbrow` |
| tree | **1** (oak) | 5 | `trees.dbrow` |
| fruit tree | **1** (apple) | 8 | `fruit_trees.dbrow` |
| hardwood / special / anima / coral | **0** | ~23 | — |
| **total plantables** | **12** | **~78** | |

Patch registry (`patches.dbrow`) has **43 rows**; OSRS has roughly **110 patch
instances**. Of the 43 registered, growth is actually wired for 5 herb, 2
allotment, 1 flower, 4 tree, 4 fruit, 4 hops, 4 bush = **24**. Spirit tree,
belladonna, mushroom, cactus and calquat rows exist in the registry but have no
plant/grow scripts (queue #134).

`interface_farming/` (the Tool Leprechaun store, interfaces 125/126) is **done
and verified** — see [`farming_server_reqs.md`](farming_server_reqs.md) and
[`farming_tools.md`](farming_tools.md).

### 0.1 XP scale already matches the wiki

`crops.dbrow` stores XP ×10 (`plant_xp,80` for potato = 8.0 XP), and every
value spot-checked against the wiki agrees — potato 8/9, guam 11/12.5, oak
14/467.3. The 2009scape-derived values in the tree are **the same numbers OSRS
uses today**, so the wiki can be used directly to fill in the remaining rows
without an era reconciliation pass. Growth cadence agrees too: allotment 4×10 =
40 min, herb 4×20 = 80 min, flower 4×5 = 20 min, tree 4×40 = 2h40, fruit tree
6×160 = 16h.

---

## 1. The blocker: per-patch state is hand-unrolled

This is the thing to fix before any content lands, because every new patch
multiplies it.

Per-patch runtime state (`_lives` / `_next` / `_seed`, plus `_check` for
trees and bushes) is **one hand-declared `perm` varp per field per patch**.
`farming_runtime.varp` is already **84 declarations for 28 patch slots**. Adding
the remaining ~70 patches at 3–5 fields each lands somewhere near **400
hand-written varp blocks**.

The permanent visual state is worse: `farming_patch.rs2` carries **two parallel
50-branch `if ($patch = …)` switches** — `~farming_patch_read_varbit` and
`~farming_patch_write_varbit` — because, in that file's own words:

> Permanent state varbit is NOT a db column type (`mock230_db` has no varbit
> pack kind); scripts switch on the wrapper loc to read/write `%varbit_N`.

And `farming_transmit.rs2` carries a *third* copy of the same mapping to push
permanent → `farming_transmit_*` for multiloc remorph, plus one `[mapzone,…]`
trigger per map square.

So each new patch today costs: 3–5 varp blocks, 2 switch arms, 1 transmit arm,
1 mapzone arm, and a `~farming_catchup_*` call. Five edit sites, none of them
checked against each other. Herbs, hops, bushes, trees and fruit trees already
have the good shape (`~farming_catchup_bush(loc $patch)` — generic, takes the
patch); allotments and flowers are the outliers, still Falador-only with
`~farming_advance_falador_allot_nw`-style procs.

### 1.1 Two engine changes unlock the collapse

Both are small and both are general — neither is farming-specific.

**(a) `varbit` as a db column kind.** `db_kind_for_type()` in
`src/net/mock/mock230_db.c` maps 16 type words to pack kinds; `varbit` is not
among them, though `MOCK230_PACK_VARBIT` exists and
`mock230_content_symbol(MOCK230_PACK_VARBIT, name)` already resolves varbits by
name at runtime (`mock230_world.c:16858`). The compiler already has
`SSC_SYM_VARBIT` (`ssc_symbols.c:527`). Adding one row to `k_map` lets
`patches.dbrow` carry the varbit directly:

```
column=state_varbit,varbit
```

**(b) A dynamic varbit get/set op pair.** `SS_OP_PUSH_VARBIT` (25) and
`SS_OP_POP_VARBIT` (27) take the varbit id as a *compile-time operand*, so
script cannot read a varbit whose id came from a db row. A
`varbit_get($varbit)` / `varbit_set($varbit, int)` pair closes that. With (a)
and (b) together, both 50-branch switches become:

```
[proc,farming_patch_get_state](loc $patch)(int $state)
def_dbrow $row = ~farming_patch_row($patch);
return(varbit_get(db_getfield($row, farming_patches:state_varbit, 0)));
```

**(c) Optional but strongly recommended: an indexed per-patch record store.**
Replacing the ~400 varps with a small engine-side per-player table
(`patch_slot × {seed, planted_at, stage, lives, flags}`) saved with the player.
Farming state is genuinely server-only — the client needs only the packed
visual varbit and the `farming_view` panel — so it does not have to live in
varps at all. This is the difference between ~400 config blocks and one table.
If (c) is judged too large, (a)+(b) plus a naming convention still work; (c) is
what makes patch count stop mattering.

### 1.2 Consequence for sequencing

**Phase 1 below is not optional and not deferrable.** Landing content rows
first means re-editing every one of them when the state model changes.

---

## 2. The content catalogue

Everything the skill needs, with the wiki reference for each. Cache names are
`all.obj` / `all.loc` symbols verified present in `cache.osrs239` unless
marked otherwise. ✅ = in tree today.

### 2.1 Patch types

| type | wiki | patches in OSRS | registered | wired |
|---|---|---:|---:|---:|
| Allotment | [Allotment patch](https://oldschool.runescape.wiki/w/Allotment_patch) | 19 | 9 | 2 |
| Flower | [Flower patch](https://oldschool.runescape.wiki/w/Flower_patch) | 9 | 4 | 1 |
| Herb | [Herb patch](https://oldschool.runescape.wiki/w/Herb_patch) | 10 | 5 | 5 |
| Hops | [Hops patch](https://oldschool.runescape.wiki/w/Hops_patch) | 5 | 4 | 4 |
| Bush | [Bush patch](https://oldschool.runescape.wiki/w/Bush_patch) | 5 | 4 | 4 |
| Tree | [Tree patch](https://oldschool.runescape.wiki/w/Tree_patch) | 7 | 5 | 4 |
| Fruit tree | [Fruit tree patch](https://oldschool.runescape.wiki/w/Fruit_tree_patch) | 7 | 5 | 4 |
| Spirit tree | [Spirit tree patch](https://oldschool.runescape.wiki/w/Spirit_tree) | 5 | 3 | 0 |
| Hardwood | [Hardwood tree patch](https://oldschool.runescape.wiki/w/Hardwood_tree_patch) | 2–3 | 0 | 0 |
| Cactus | [Cactus patch](https://oldschool.runescape.wiki/w/Cactus_patch) | 2 | 1 | 0 |
| Calquat | [Calquat tree patch](https://oldschool.runescape.wiki/w/Calquat_tree_patch) | 1–3 | 1 | 0 |
| Mushroom | [Mushroom patch](https://oldschool.runescape.wiki/w/Mushroom_patch) | 1 | 1 | 0 |
| Belladonna | [Belladonna patch](https://oldschool.runescape.wiki/w/Belladonna_patch) | 1–2 | 1 | 0 |
| Grape (Vinery) | [Vinery](https://oldschool.runescape.wiki/w/Vinery) | 12 | 0 | 0 |
| Seaweed | [Seaweed patch](https://oldschool.runescape.wiki/w/Seaweed_patch) | 2 | 0 | 0 |
| Celastrus | [Celastrus patch](https://oldschool.runescape.wiki/w/Celastrus_patch) | 1 | 0 | 0 |
| Redwood | [Redwood tree patch](https://oldschool.runescape.wiki/w/Redwood_tree_patch) | 1 | 0 | 0 |
| Crystal tree | [Crystal tree patch](https://oldschool.runescape.wiki/w/Crystal_tree) | 1 | 0 | 0 |
| Anima | [Anima patch](https://oldschool.runescape.wiki/w/Anima_patch) | 1 | 0 | 0 |
| Hespori | [Hespori patch](https://oldschool.runescape.wiki/w/Hespori) | 1 | 0 | 0 |
| Coral | [Coral nursery](https://oldschool.runescape.wiki/w/Coral_nursery) | 2 | 0 | 0 |

Master reference: [Farming patch](https://oldschool.runescape.wiki/w/Farming_patch).

> **Era gate.** Coral nurseries and the Anglers' Retreat hardwood patch are
> Sailing-era. The *items* are in this cache (`coral_elkhorn_frag`,
> `coral_pillar_frag`, `coral_umbral_frag`), but confirm the map squares exist
> in `all.loc` before registering the patch — see [`era-feature-table`
> conventions](SKILLS_CONTENT_PORT_QUEUE.md). Same check for Kastori /
> Auburnvale / Aldarin / Ortus Farm (Varlamore) and Nemus Retreat.

### 2.2 Allotment seeds — [Allotment patch/Seeds](https://oldschool.runescape.wiki/w/Allotment_patch/Seeds)

3 seeds per planting. Protection is a flower in the adjacent flower patch, or a
gardener payment.

| seed | cache name | lvl | plant XP | harvest XP | produce | growth | flower | payment |
|---|---|---:|---:|---:|---|---|---|---|
| ✅ [Potato](https://oldschool.runescape.wiki/w/Potato_seed) | `potato_seed` | 1 | 8 | 9 | Potato | 40m | Marigold | Compost ×2 |
| ✅ [Onion](https://oldschool.runescape.wiki/w/Onion_seed) | `onion_seed` | 5 | 9.5 | 10.5 | Onion | 40m | Marigold | Potatoes(10) ×1 |
| ✅ [Cabbage](https://oldschool.runescape.wiki/w/Cabbage_seed) | `cabbage_seed` | 7 | 10 | 11.5 | Cabbage | 40m | Rosemary | Onions(10) ×1 |
| [Tomato](https://oldschool.runescape.wiki/w/Tomato_seed) | `tomato_seed` | 12 | 12.5 | 14 | Tomato | 40m | Marigold | Cabbages(10) ×2 |
| [Sweetcorn](https://oldschool.runescape.wiki/w/Sweetcorn_seed) | `sweetcorn_seed` | 20 | 17 | 19 | Sweetcorn | 60m | Scarecrow | Jute fibre ×10 |
| [Strawberry](https://oldschool.runescape.wiki/w/Strawberry_seed) | `strawberry_seed` | 31 | 26 | 29 | Strawberry | 60m | White lily | Apples(5) ×1 |
| [Watermelon](https://oldschool.runescape.wiki/w/Watermelon_seed) | `watermelon_seed` | 47 | 48.5 | 54.5 | Watermelon | 80m | Nasturtium | Curry leaf ×10 |
| [Snape grass](https://oldschool.runescape.wiki/w/Snape_grass_seed) | `snape_grass_seed` | 61 | 82 | 82 | Snape grass | 70m | White lily | Jangerberries ×5 |

[White lily](https://oldschool.runescape.wiki/w/White_lily) protects **all**
adjacent allotments and is the general case worth building first.

### 2.3 Flower seeds — [Flower patch](https://oldschool.runescape.wiki/w/Flower_patch)

1 seed per planting.

| seed | cache name | lvl | plant XP | harvest XP | produce | protects |
|---|---|---:|---:|---:|---|---|
| ✅ [Marigold](https://oldschool.runescape.wiki/w/Marigold_seed) | `marigold_seed` | 2 | 8.5 | 47 | Marigolds | potato, onion, tomato |
| [Rosemary](https://oldschool.runescape.wiki/w/Rosemary_seed) | `rosemary_seed` | 11 | 12 | 66.5 | Rosemary | cabbage |
| [Nasturtium](https://oldschool.runescape.wiki/w/Nasturtium_seed) | `nasturtium_seed` | 24 | 19.5 | 111 | Nasturtiums | watermelon |
| [Woad](https://oldschool.runescape.wiki/w/Woad_seed) | `woad_seed` | 25 | 20.5 | 115.5 | Woad leaf ×3 | — |
| [Limpwurt](https://oldschool.runescape.wiki/w/Limpwurt_seed) | `limpwurt_seed` | 26 | 21.5 | 120 | Limpwurt root ×3–13 | — |
| [White lily](https://oldschool.runescape.wiki/w/White_lily_seed) | `white_lily_seed` | 58 | 42 | 250 | White lily | all allotments |

Plus [Scarecrow](https://oldschool.runescape.wiki/w/Scarecrow) — not a seed:
hay sack + bronze spear + watermelon, 23 Farming, 25 XP, occupies the flower
patch, protects sweetcorn. Cache: `scarecrow_torso`, `scarecrow_torso_spear`,
`scarecrow_complete`.

### 2.4 Herb seeds — [Herb patch](https://oldschool.runescape.wiki/w/Herb_patch)

All 80 min (4 × 20). All produce grimy herbs and share the harvest-lives model.

| seed | cache name | lvl | plant XP | harvest XP | grimy herb |
|---|---|---:|---:|---:|---|
| ✅ [Guam](https://oldschool.runescape.wiki/w/Guam_seed) | `guam_seed` | 9 | 11 | 12.5 | Grimy guam leaf |
| ✅ [Marrentill](https://oldschool.runescape.wiki/w/Marrentill_seed) | `marrentill_seed` | 14 | 13.5 | 15 | Grimy marrentill |
| ✅ [Tarromin](https://oldschool.runescape.wiki/w/Tarromin_seed) | `tarromin_seed` | 19 | 16 | 18 | Grimy tarromin |
| ✅ [Harralander](https://oldschool.runescape.wiki/w/Harralander_seed) | `harralander_seed` | 26 | 21.5 | 24 | Grimy harralander |
| [Gout tuber](https://oldschool.runescape.wiki/w/Gout_tuber) | `village_rare_tuber` † | 29 | 105 | 45 | Goutweed (`eadgar_goutweed_herb`) |
| [Ranarr](https://oldschool.runescape.wiki/w/Ranarr_seed) | `ranarr_seed` | 32 | 27 | 30.5 | Grimy ranarr weed |
| [Toadflax](https://oldschool.runescape.wiki/w/Toadflax_seed) | `toadflax_seed` | 38 | 34 | 38.5 | Grimy toadflax |
| [Irit](https://oldschool.runescape.wiki/w/Irit_seed) | `irit_seed` | 44 | 43 | 48.5 | Grimy irit leaf |
| [Avantoe](https://oldschool.runescape.wiki/w/Avantoe_seed) | `avantoe_seed` | 50 | 54.5 | 61.5 | Grimy avantoe |
| [Kwuarm](https://oldschool.runescape.wiki/w/Kwuarm_seed) | `kwuarm_seed` | 56 | 69 | 78 | Grimy kwuarm |
| [Snapdragon](https://oldschool.runescape.wiki/w/Snapdragon_seed) | `snapdragon_seed` | 62 | 87.5 | 98.5 | Grimy snapdragon |
| [Huasca](https://oldschool.runescape.wiki/w/Huasca_seed) | `huasca_seed` | 65 | 86.5 | 110 | Grimy huasca |
| [Cadantine](https://oldschool.runescape.wiki/w/Cadantine_seed) | `cadantine_seed` | 67 | 106.5 | 120 | Grimy cadantine |
| [Lantadyme](https://oldschool.runescape.wiki/w/Lantadyme_seed) | `lantadyme_seed` | 73 | 134.5 | 151.5 | Grimy lantadyme |
| [Dwarf weed](https://oldschool.runescape.wiki/w/Dwarf_weed_seed) | `dwarf_weed_seed` | 79 | 170.5 | 192 | Grimy dwarf weed |
| [Torstol](https://oldschool.runescape.wiki/w/Torstol_seed) | `torstol_seed` | 85 | 199.5 | 224.5 | Grimy torstol |

† Gout tuber has no `gout_*` symbol in this cache; `village_rare_tuber` (Tai
Bwo Wannai Cleanup) is the match, `myarm_hardytubers` is the *Hardy* gout tuber
from [My Arm's Big Adventure](https://oldschool.runescape.wiki/w/My_Arm%27s_Big_Adventure).
Confirm both before wiring — one seed, two sources.

`farming.constant` currently holds `^farming_herbsave_*` bases for only the
four live herbs. The remaining 12 need their
[chance-to-save](https://oldschool.runescape.wiki/w/Harvest_lives) constants —
see §2.13.

### 2.5 Hops seeds — [Hops patch](https://oldschool.runescape.wiki/w/Hops_patch)

Stage length is 10 min; stage *count* differs per seed, which the existing
`stages` column already handles.

| seed | cache name | lvl | seeds | plant XP | harvest XP | produce | growth | payment |
|---|---|---:|---:|---:|---:|---|---|---|
| ✅ [Barley](https://oldschool.runescape.wiki/w/Barley_seed) | `barley_seed` | 3 | 4 | 8.5 | 9.5 | Barley | 40m | Compost ×3 |
| [Hammerstone](https://oldschool.runescape.wiki/w/Hammerstone_seed) | `hammerstone_hop_seed` | 4 | 4 | 9 | 10 | Hammerstone hops | 40m | Marigold ×1 |
| [Asgarnian](https://oldschool.runescape.wiki/w/Asgarnian_seed) | `asgarnian_hop_seed` | 8 | 4 | 10.9 | 12 | Asgarnian hops | 50m | Onions(10) ×1 |
| [Jute](https://oldschool.runescape.wiki/w/Jute_seed) | `jute_seed` | 13 | 3 | 13 | 14.5 | Jute fibre | 50m | Barley malt ×6 |
| [Yanillian](https://oldschool.runescape.wiki/w/Yanillian_seed) | `yanillian_hop_seed` | 16 | 4 | 14.5 | 16 | Yanillian hops | 60m | Tomatoes(5) ×1 |
| [Flax](https://oldschool.runescape.wiki/w/Flax_seed) | `flax_seed` | 18 | 3 | 16 | 17.5 | Flax | 60m | Grain ×6 |
| [Krandorian](https://oldschool.runescape.wiki/w/Krandorian_seed) | `krandorian_hop_seed` | 21 | 4 | 17.5 | 19.5 | Krandorian hops | 70m | Cabbages(10) ×3 |
| [Wildblood](https://oldschool.runescape.wiki/w/Wildblood_seed) | `wildblood_hop_seed` | 28 | 4 | 23 | 26 | Wildblood hops | 80m | Nasturtiums ×1 |
| [Hemp](https://oldschool.runescape.wiki/w/Hemp_seed) | `hemp_seed` | 37 | 3 | 33 | 37 | Hemp | 80m | Flax ×6 |
| [Cotton](https://oldschool.runescape.wiki/w/Cotton_seed) | `cotton_seed` | 71 | 72 | 82 | Cotton boll | 100m | Hemp ×6 |

### 2.6 Bush seeds — [Bush patch](https://oldschool.runescape.wiki/w/Bush_patch)

Bushes have a **check-health** step (large one-off XP) then repeated berry
picking with regrowth.

| seed | cache name | lvl | plant | check | pick | produce | stages | payment |
|---|---|---:|---:|---:|---:|---|---:|---|
| ✅ [Redberry](https://oldschool.runescape.wiki/w/Redberry_seed) | `redberry_bush_seed` | 10 | 11.5 | 64 | 4.5 | Redberries | 5 | Cabbages(10) ×4 |
| [Cadavaberry](https://oldschool.runescape.wiki/w/Cadavaberry_seed) | `cadavaberry_bush_seed` | 22 | 18 | 102.5 | 7 | Cadava berries | 6 | Tomatoes(5) ×3 |
| [Dwellberry](https://oldschool.runescape.wiki/w/Dwellberry_seed) | `dwellberry_bush_seed` | 36 | 31.5 | 177.5 | 12 | Dwellberries | 7 | Strawberries(5) ×3 |
| [Jangerberry](https://oldschool.runescape.wiki/w/Jangerberry_seed) | `jangerberry_bush_seed` | 48 | 50.5 | 284.5 | 19 | Jangerberries | 8 | Watermelons ×6 |
| [Whiteberry](https://oldschool.runescape.wiki/w/Whiteberry_seed) | `whiteberry_bush_seed` | 59 | 78 | 437.5 | 29 | White berries | 8 | Bittercap mushrooms ×8 |
| [Poison ivy](https://oldschool.runescape.wiki/w/Poison_ivy_seed) | `poisonivy_bush_seed` | 70 | 120 | 675 | 45 | Poison ivy berries | 8 | disease-immune |

### 2.7 Tree saplings — [Tree patch](https://oldschool.runescape.wiki/w/Tree_patch)

Two-step: seed + plant pot + trowel + water → sapling, then plant. Check-health
gives the payload XP.

| sapling | seed cache name | sapling cache name | lvl | plant | check | logs | growth | payment |
|---|---|---|---:|---:|---:|---|---|---|
| ✅ [Oak](https://oldschool.runescape.wiki/w/Oak_sapling) | `acorn` | `plantpot_oak_sapling` | 15 | 14 | 467.3 | Oak logs | 2h40 | Tomatoes(5) ×1 |
| [Willow](https://oldschool.runescape.wiki/w/Willow_sapling) | `willow_seed` | `plantpot_willow_sapling` | 30 | 25 | 1,456.5 | Willow logs | 4h | Apples(5) ×1 |
| [Maple](https://oldschool.runescape.wiki/w/Maple_sapling) | `maple_seed` | `plantpot_maple_sapling` | 45 | 45 | 3,403.4 | Maple logs | 5h20 | Oranges(5) ×1 |
| [Yew](https://oldschool.runescape.wiki/w/Yew_sapling) | `yew_seed` | `plantpot_yew_sapling` | 60 | 81 | 7,069.9 | Yew logs | 6h40 | Cactus spine ×10 |
| [Magic](https://oldschool.runescape.wiki/w/Magic_sapling) | `magic_tree_seed` | `plantpot_magic_tree_sapling` | 75 | 145.5 | 13,768.3 | Magic logs | 8h | Coconut ×25 |

### 2.8 Fruit tree saplings — [Fruit tree patch](https://oldschool.runescape.wiki/w/Fruit_tree_patch)

All 16h (6 × 160 min), max 6 fruit, 40 min regrowth per fruit.

| sapling | seed cache name | lvl | fruit | payment |
|---|---|---:|---|---|
| ✅ [Apple](https://oldschool.runescape.wiki/w/Apple_sapling) | `apple_tree_seed` | 27 | Cooking apple | Sweetcorn ×9 |
| [Banana](https://oldschool.runescape.wiki/w/Banana_sapling) | `banana_tree_seed` | 33 | Banana | Apples(5) ×4 |
| [Orange](https://oldschool.runescape.wiki/w/Orange_sapling) | `orange_tree_seed` | 39 | Orange | Strawberries(5) ×3 |
| [Curry](https://oldschool.runescape.wiki/w/Curry_sapling) | `curry_tree_seed` | 42 | Curry leaf | Bananas(5) ×5 |
| [Pineapple](https://oldschool.runescape.wiki/w/Pineapple_sapling) | `pineapple_seed` | 51 | Pineapple | Watermelons(5) ×2 |
| [Papaya](https://oldschool.runescape.wiki/w/Papaya_sapling) | `papaya_tree_seed` | 57 | Papaya fruit | Pineapples ×10 |
| [Palm](https://oldschool.runescape.wiki/w/Palm_sapling) | `palm_tree_seed` | 68 | Coconut | Papaya fruit ×15 |
| [Dragonfruit](https://oldschool.runescape.wiki/w/Dragonfruit_sapling) | `dragonfruit_tree_seed` | 81 | Dragonfruit | Coconut ×15 |

### 2.9 Hardwood trees — [Hardwood tree patch](https://oldschool.runescape.wiki/w/Hardwood_tree_patch)

640 min per stage. Fossil Island, gated behind
[Bone Voyage](https://oldschool.runescape.wiki/w/Bone_Voyage).

| sapling | seed cache name | lvl | logs | payment |
|---|---|---:|---|---|
| [Teak](https://oldschool.runescape.wiki/w/Teak_sapling) | `teak_seed` | 35 | Teak logs | Limpwurt root ×15 |
| [Mahogany](https://oldschool.runescape.wiki/w/Mahogany_sapling) | `mahogany_seed` | 55 | Mahogany logs | Yanillian hops ×25 |
| [Camphor](https://oldschool.runescape.wiki/w/Camphor_sapling) | `camphor_seed` | 66 | Camphor logs | White berries ×25 |
| [Ironwood](https://oldschool.runescape.wiki/w/Ironwood_sapling) | `ironwood_seed` | 80 | Ironwood logs | Curry leaf ×25 |
| [Rosewood](https://oldschool.runescape.wiki/w/Rosewood_sapling) | `rosewood_seed` | 92 | Rosewood logs | Dragonfruit ×15 |

### 2.10 Special patches

| crop | seed cache name | lvl | plant | harvest | produce | growth | location | payment |
|---|---|---:|---:|---:|---|---|---|---|
| [Giant seaweed](https://oldschool.runescape.wiki/w/Seaweed_spore) | `seaweed_seed` | 23 | 19 | 21 | Giant seaweed | 4×10m | Underwater | Numulite ×200 |
| [Grape](https://oldschool.runescape.wiki/w/Grape_seed) | `grape_seed` | 36 | 31.5 | 40 | Grapes | 7×5m | [Vinery](https://oldschool.runescape.wiki/w/Vinery) ×12 | free |
| [Mushroom](https://oldschool.runescape.wiki/w/Mushroom_spore) | `mushroom_spore` | 53 | 61.5 | — | Mushroom | 6×40m | W of Canifis | none |
| [Cactus](https://oldschool.runescape.wiki/w/Cactus_seed) | `cactus_seed` | 55 | 66.5 | 25 | Cactus spine | 7×80m | Al Kharid, Guild | Cadava berries ×6 |
| [Belladonna](https://oldschool.runescape.wiki/w/Belladonna_seed) | `belladonna_seed` | 63 | 91 | — | Nightshade | 4×80m | Draynor Manor | none |
| [Potato cactus](https://oldschool.runescape.wiki/w/Potato_cactus_seed) | `potato_cactus_seed` | 64 | — | — | Potato cactus | — | cactus patch | — |
| [Hespori](https://oldschool.runescape.wiki/w/Hespori_seed) | `hespori_seed` | 65 | 62 | 12,600 | boss | 3×640m | Guild | immune |
| [Calquat](https://oldschool.runescape.wiki/w/Calquat_tree_seed) | `calquat_tree_seed` | 72 | 129.5 | 48.5 | Calquat fruit | 8×160m | Tai Bwo Wannai | Poison ivy berries ×8 |
| [Crystal tree](https://oldschool.runescape.wiki/w/Crystal_acorn) | `crystal_tree_seed` | 74 | 126 | — | Crystal shard | 6×80m | Prifddinas | immune |
| [Spirit tree](https://oldschool.runescape.wiki/w/Spirit_seed) | `spirit_tree_seed` | 83 | 199.5 | — | teleport node | 12×320m | 5 patches | Monkey nuts ×5 + Monkey bar ×1 + Ground tooth ×1 |
| [Celastrus](https://oldschool.runescape.wiki/w/Celastrus_seed) | `celastrus_tree_seed` | 85 | 204 | 23.5 | Celastrus bark | 5×160m | Guild | Potato cactus ×8 |
| [Redwood](https://oldschool.runescape.wiki/w/Redwood_tree_seed) | `redwood_tree_seed` | 90 | 230 | — | Redwood logs | 10×640m | Guild | Dragonfruit ×6 |

**Anima** — [Anima patch](https://oldschool.runescape.wiki/w/Anima_patch), all
level 76, 100 plant XP, 8×640m, Farming Guild, immune. Global effects while
alive:

| seed | cache name | effect |
|---|---|---|
| [Kronos](https://oldschool.runescape.wiki/w/Kronos_seed) | `kronos_seed` | faster growth |
| [Iasor](https://oldschool.runescape.wiki/w/Iasor_seed) | `iasor_seed` | ×0.2 disease chance (min 1/128) |
| [Attas](https://oldschool.runescape.wiki/w/Attas_seed) | `attas_seed` | ×1.05 chance to save a life |

**Coral** — [Coral nursery](https://oldschool.runescape.wiki/w/Coral_nursery),
4×40 min. `coral_elkhorn_frag` (28), `coral_pillar_frag` (52),
`coral_umbral_frag` (77). Verify map presence before registering.

### 2.11 Tools and consumables

| item | cache name | wiki | in tree |
|---|---|---|---|
| Rake | `rake` | [Rake](https://oldschool.runescape.wiki/w/Rake) | ✅ |
| Seed dibber | `dibber` | [Seed dibber](https://oldschool.runescape.wiki/w/Seed_dibber) | ✅ |
| Spade | `spade` | [Spade](https://oldschool.runescape.wiki/w/Spade) | ✅ |
| Secateurs | `secateurs` | [Secateurs](https://oldschool.runescape.wiki/w/Secateurs) | ✅ store only |
| Magic secateurs | `fairy_enchanted_secateurs` | [Magic secateurs](https://oldschool.runescape.wiki/w/Magic_secateurs) | ✗ (#135) |
| Gardening trowel | `gardening_trowel` | [Gardening trowel](https://oldschool.runescape.wiki/w/Gardening_trowel) | ✅ store only |
| Watering can (0–8) | `watering_can_0..8` | [Watering can](https://oldschool.runescape.wiki/w/Watering_can) | ✅ store only |
| Gricoller's can | Tithe reward | [Gricoller's can](https://oldschool.runescape.wiki/w/Gricoller%27s_can) | ✗ |
| Bucket | `bucket_empty` | [Bucket](https://oldschool.runescape.wiki/w/Bucket) | ✅ |
| Plant cure | `plant_cure` | [Plant cure](https://oldschool.runescape.wiki/w/Plant_cure) | ✗ (#132) |
| Compost | `compost` | [Compost](https://oldschool.runescape.wiki/w/Compost) | ✅ |
| Supercompost | `supercompost` | [Supercompost](https://oldschool.runescape.wiki/w/Supercompost) | ✅ |
| Ultracompost | `ultracompost` | [Ultracompost](https://oldschool.runescape.wiki/w/Ultracompost) | ✗ (#133) |
| Volcanic ash | `volcanic_ash` | [Volcanic ash](https://oldschool.runescape.wiki/w/Volcanic_ash) | ✗ |
| Compost potion | `compost_potion*` | [Compost potion](https://oldschool.runescape.wiki/w/Compost_potion) | ✗ |
| Bottomless compost bucket | `bottomless_compost_bucket` | [Bottomless compost bucket](https://oldschool.runescape.wiki/w/Bottomless_compost_bucket) | ✅ store slot only |
| Seed box | `seed_box` | [Seed box](https://oldschool.runescape.wiki/w/Seed_box) | ✗ |
| Herb sack | `herb_sack` | [Herb sack](https://oldschool.runescape.wiki/w/Herb_sack) | ✗ |
| Plant pot / filled | `plantpot_*` | [Plant pot](https://oldschool.runescape.wiki/w/Plant_pot) | partial |
| Seed vault | — | [Seed vault](https://oldschool.runescape.wiki/w/Seed_vault) | ✗ |

**Compost** — [Compost bin](https://oldschool.runescape.wiki/w/Compost_bin).
15 items (30 in the Guild's big bin), 35–70 min to rot. Compost = 18 XP/bucket,
−50% disease, +1 life. Supercompost = −80%, +2 lives. Ultracompost (25 volcanic
ash on a supercompost bin, 50 for the big bin) = −90%, +3 lives. All-tomato bin
→ [Rotten tomato](https://oldschool.runescape.wiki/w/Rotten_tomato). Bins:
Falador, Port Phasmatys, Ardougne, Catherby, Hosidius, Prifddinas, Civitas illa
Fortis, plus the Guild's big bin (45 Farming). Classic bins 1–4 are live;
#133 covers 5–7 + Dump + potion conversion. **The bin produces compost that
nothing consumes** — applying a bucket to a patch does not exist (§2.13.1).

### 2.12 NPCs

**Tool leprechauns** — [Tool leprechaun](https://oldschool.runescape.wiki/w/Tool_leprechaun).
One per patch cluster; stores tools, exchanges harvested produce for banknotes
(queue #136, not implemented). Cache: `farming_tools_leprechaun` ✅.

**Gardeners** (paid to protect; cannot protect herbs or flowers) —
[Farming](https://oldschool.runescape.wiki/w/Farming). None implemented; all of
#132.

| NPC | patch cluster |
|---|---|
| [Elstan](https://oldschool.runescape.wiki/w/Elstan) | Falador allotment/flower/herb |
| [Dantaera](https://oldschool.runescape.wiki/w/Dantaera) | Catherby |
| [Kragen](https://oldschool.runescape.wiki/w/Kragen) | Ardougne |
| [Lyra](https://oldschool.runescape.wiki/w/Lyra) | Port Phasmatys |
| [Marisi](https://oldschool.runescape.wiki/w/Marisi) | Hosidius |
| [Vasquen](https://oldschool.runescape.wiki/w/Vasquen) | Lumbridge hops |
| [Rhonen](https://oldschool.runescape.wiki/w/Rhonen) | McGrubor's hops |
| [Selena](https://oldschool.runescape.wiki/w/Selena) | Yanille hops |
| [Francis](https://oldschool.runescape.wiki/w/Francis) | Entrana hops |
| [Dreven](https://oldschool.runescape.wiki/w/Dreven) | Champions' Guild bush |
| [Taria](https://oldschool.runescape.wiki/w/Taria) | Rimmington bush |
| [Torrell](https://oldschool.runescape.wiki/w/Torrell) | Ardougne bush |
| [Rhazien](https://oldschool.runescape.wiki/w/Rhazien) | Etceteria bush |
| [Fayeth](https://oldschool.runescape.wiki/w/Fayeth) | Lumbridge tree |
| [Treznor](https://oldschool.runescape.wiki/w/Treznor) | Varrock tree |
| [Heskel](https://oldschool.runescape.wiki/w/Heskel) | Falador tree |
| [Alain](https://oldschool.runescape.wiki/w/Alain) | Taverley tree |
| [Prissy Scilla](https://oldschool.runescape.wiki/w/Prissy_Scilla) | Gnome Stronghold tree |
| [Bolongo](https://oldschool.runescape.wiki/w/Bolongo) | Gnome Stronghold fruit |
| [Ellena](https://oldschool.runescape.wiki/w/Ellena) | Catherby fruit |
| [Gileth](https://oldschool.runescape.wiki/w/Gileth) | Tree Gnome Village fruit |
| [Garth](https://oldschool.runescape.wiki/w/Garth) | Brimhaven fruit |
| [Liliwen](https://oldschool.runescape.wiki/w/Liliwen) | Lletya fruit |
| [Imiago](https://oldschool.runescape.wiki/w/Imiago) | Tai Bwo Wannai calquat |
| [My Arm](https://oldschool.runescape.wiki/w/My_Arm) | Troll Stronghold herb |
| [Boulder](https://oldschool.runescape.wiki/w/Boulder) | Weiss herb |

**Farming Guild** — [Farming Guild](https://oldschool.runescape.wiki/w/Farming_Guild),
45 / 65 / 85 Farming tiers.

| NPC | role |
|---|---|
| [Guildmaster Jane](https://oldschool.runescape.wiki/w/Guildmaster_Jane) | [Farming contracts](https://oldschool.runescape.wiki/w/Farming_contract) |
| [Alan](https://oldschool.runescape.wiki/w/Alan) | beginner tier gardener |
| [Rosie](https://oldschool.runescape.wiki/w/Rosie) | tree patch |
| [Nikkie](https://oldschool.runescape.wiki/w/Nikkie) | fruit tree patch |
| [Latlink Fastbell](https://oldschool.runescape.wiki/w/Latlink_Fastbell) | spirit tree patch |
| [Taylor](https://oldschool.runescape.wiki/w/Taylor) | celastrus patch |
| [Alexandra](https://oldschool.runescape.wiki/w/Alexandra) | redwood patch |

**Other** — [Martin the Master Gardener](https://oldschool.runescape.wiki/w/Martin_the_Master_Gardener)
(Draynor, sells the [Farming cape](https://oldschool.runescape.wiki/w/Farming_cape)),
[Farmer Gricoller](https://oldschool.runescape.wiki/w/Farmer_Gricoller) and
[Bologa](https://oldschool.runescape.wiki/w/Bologa) (Tithe Farm),
[Hespori](https://oldschool.runescape.wiki/w/Hespori) (demi-boss).

### 2.13 Interactions

Measured from the trigger declarations in `skill_farming/scripts/*.rs2`, not
inferred.

> **IMPLEMENTED 2026-08-15 — 5 of 31 → 26 of 31.** Before this pass only two
> items dispatched on a patch (`rake`, the seed/sapling), `skill_farming/`
> declared **zero `[opheldu,…]` triggers**, and **compost was never applied to
> a patch at all** — the bins produced buckets nothing consumed while
> `farming_plant.rs2` hardcoded three harvest lives, making the +1/+2/+3 in
> §2.14 documented and unreachable. New files: `farming_items.rs2` (item →
> patch), `farming_craft.rs2` (item → item, item → scenery),
> `farming_state.rs2` + `farming_state.varp` (per-patch compost / watered /
> diseased / dead / protected / scarecrow word), `saplings.dbtable` +
> `saplings.dbrow` (23 seed → seedling → watered → sapling chains, every one of
> the 92 obj names checked against `all.obj`). Compost now seeds harvest lives
> as `3 + tier` and is cleared when the patch returns to weeds.
>
> **The five that remain**, and why:
> 1. **Gricoller's can** — no `gricoller*` symbol exists in this cache; the
>    Tithe reward item has to be identified first.
> 2. **Cure Plant (Lunar)** — a spell-on-loc trigger, not an item interaction.
> 3. **Herb sack** (`slayer_herb_sack`) and 4. **seed box** (`seed_box`) — both
>    need a real container, which is the same `container_for` work
>    `farming_server_reqs.md` left untouched.
> 5. The **disease roll** itself is still Phase 3. Plant cure and secateurs are
>    implemented and read `^farming_flag_diseased`, but nothing sets that flag
>    yet, so both currently answer "This patch is not diseased." The item side
>    is done; the simulation that makes it fire is not.
>
> One correction to the table below from building it: **filling a plant pot is
> item-on-*patch*, not item-on-item** — the wiki is explicit that you "use the
> empty pot on any farming patch, while carrying a gardening trowel". It has
> moved to §2.13.1.

#### 2.13.1 Item → patch (`[oplocu]`)

| item | target state | effect | status |
|---|---|---|---|
| Rake | weedy patch, any type | clear one weed stage | ✅ all 7 wired types |
| Seed | weeded allotment/flower/herb/hops/bush patch | plant | ✅ 9 seeds |
| Sapling (`plantpot_*`) | weeded tree/fruit patch | plant | ✅ oak, apple |
| Compost | any planted patch | +1 life, −50 % disease | ✅ |
| Supercompost | any planted patch | +2 lives, −80 % | ✅ |
| Ultracompost | any planted patch | +3 lives, −90 % | ✅ |
| Bottomless compost bucket | any planted patch | apply stored charge | ✅ |
| Watering can 1–8 | growing allotment/flower/hops | water; −disease this cycle | ✅ |
| Gricoller's can | same | same, 1,000 doses | ✗ no cache item |
| Plant cure | diseased allot/flower/herb/hops/cactus/belladonna/mushroom/calquat | cure | ✅ (awaits the disease roll) |
| Secateurs / magic secateurs | diseased bush or tree | prune, 75 % success | ✅ (awaits the disease roll) |
| Spade | dead plant, spent crop, stump | clear to weeded | ✅ |
| Scarecrow | empty flower patch | place; protects sweetcorn | ✅ server-side flag; patch varbit value unmeasured |
| *Cure Plant* (Lunar) | diseased patch | cure, no item | ✗ |

Seed dibber is correctly modelled as a held *requirement*
(`~farming_has_dibber`), not a use-on-patch — that matches OSRS.

#### 2.13.2 Item → compost bin (`[oplocu]`)

| item | target state | effect | status |
|---|---|---|---|
| Compostable item | open bin, 0–14 filled | add one, 15 closes it | ✅ 54 compostables |
| Bucket (empty) | finished bin | withdraw one bucket | ✅ |
| Compost potion | compost-filled bin | → supercompost | ✅ |
| Volcanic ash ×25 (×50 big bin) | supercompost-filled bin | → ultracompost | ✅ |
| Bottomless compost bucket | finished bin | bulk fill | ✅ |

#### 2.13.3 Item → item (`[opheldu]`) — **zero implemented**

| item A | item B | result | status |
|---|---|---|---|
| Gardening trowel | Plant pot (empty) | Plant pot (soil) | ✅ — moved to §2.13.1, it is item-on-patch |
| Tree/fruit/special seed | Filled plant pot | seed in pot | ✅ 23 chains |
| Watering can | Seed in pot | watered → sapling over time | ✅ + sprout softtimer |
| Compost potion | Bucket of compost | Supercompost | ✅ |
| Volcanic ash | Supercompost bucket | Ultracompost | ✅ ×2 ash, 1.1 XP |
| Empty sack | Hay | Hay sack | ✅ sack on `haystack` loc |
| Hay sack + bronze spear + watermelon | — | Scarecrow | ✅ two steps, 23 Farming |
| Grimy herb | Herb sack | store | ✗ needs a container |
| Seed | Seed box | store | ✗ needs a container |
| Bologa's blessing | Grapes | Zamorak's grapes | ✅ |

#### 2.13.4 Item → scenery (non-patch)

| item | target | effect | status |
|---|---|---|---|
| Watering can (0–7) | fountain / sink / well / water source | refill to 8 | ✅ |
| Bucket | water source | Bucket of water | ✅ |

#### 2.13.5 Loc ops (no item held)

| op | status |
|---|---|
| Inspect patch | ✅ |
| Rake (op on the weeds loc) | ✅ |
| Check health | ✅ tree / fruit / bush |
| Harvest crop, lives roll | ✅ |
| Pick fruit / berries, with regrowth | ✅ |
| Chop grown tree → stump | ✅ |
| Clear stump / dead plant | ✅ partial (stump only) |
| Spirit tree teleport menu | ✗ **#134** |

#### 2.13.6 NPC ops

| op | status |
|---|---|
| Tool leprechaun: store / withdraw (interfaces 125/126) | ✅ |
| Tool leprechaun: exchange harvest for banknotes | ✗ **#136** |
| Gardener: talk → pay → protect | ✗ **#132** |
| Guildmaster Jane: accept / complete contract | ✗ |
| Farmer Gricoller / Bologa: Tithe shop | ✗ **#138** |
| Martin: buy Farming cape | ✗ **#137** |
| Fight Hespori | ✗ **#138** |

#### 2.13.7 Interface

| op | status |
|---|---|
| Open `farming_view` (Geomancy / tab) | partial — panel paints, opener deferred **#135** |

**Tally of item interactions: 26 of 31 exist** (was 5) — 13 of 15 on patches,
5 of 5 on bins, 6 of 8 item-on-item, 2 of 2 on water sources.

### 2.14 Boosts and rewards

| thing | wiki | effect |
|---|---|---|
| Magic secateurs | [link](https://oldschool.runescape.wiki/w/Magic_secateurs) | ×1.10 chance to save (herbs, allotments) |
| Farming cape | [link](https://oldschool.runescape.wiki/w/Farming_cape) | ×1.05 herb save; unlimited Guild teleport; +1 Farming boost |
| Attas | [link](https://oldschool.runescape.wiki/w/Attas_seed) | ×1.05 chance to save |
| Farmer's outfit | [link](https://oldschool.runescape.wiki/w/Farmer%27s_outfit) | 0.4/0.8/0.6/0.2 % + 0.5 % set = **2.5 %** XP |
| Achievement diaries | [link](https://oldschool.runescape.wiki/w/Achievement_Diary) | disease-free patches; +10…+25 flat save bonus |
| Compost tiers | [link](https://oldschool.runescape.wiki/w/Compost) | +1/+2/+3 lives, −50/80/90 % disease — **unreachable today**, see §2.13 |
| Seed packs | [link](https://oldschool.runescape.wiki/w/Seed_pack) | tiers 1–5, from contracts and Tithe |
| Tangleroot | [link](https://oldschool.runescape.wiki/w/Tangleroot) | pet, 1/5,375–1/4,525 from Hespori |

**Harvest-lives formula** —
[Harvest lives](https://oldschool.runescape.wiki/w/Harvest_lives):

```
chance_to_save = 1 + floor( CTSlow*(99-F)/98 + CTShigh*(F-1)/98 + 0.5 ) / 256
expected_yield = lives / (1 - chance_to_save)
```

Modifiers apply in order: items (secateurs, cape) → diary flat bonus → Attas
multiplier. The tree's current
`rand(256) > min(base+level, 80)` in `farming.constant` is the **2009scape
approximation**, not this formula — replacing it is a Phase 3 task, and every
crop needs its CTS-low/CTS-high pair rather than a single base.

---

## 3. Plan

### Phase 1 — make patch count stop mattering (engine + refactor)

No new content. Everything after this is cheap; nothing before it is.

1. **Add `varbit` to `db_kind_for_type()`** in `src/net/mock/mock230_db.c`
   (`MOCK230_PACK_VARBIT` and `mock230_content_symbol` already exist). One row
   in `k_map`. Follow the file's own rule: an unrecognised type word is a load
   error, so this must be an added *kind*, not a fallback.
2. **Add `varbit_get` / `varbit_set` ops** taking the varbit id from the stack,
   in `src/serverscript/` (`ss_opcode.h`, `ss_meta.c`, `ssvm_provider.c`) and
   the mock230 provider. `SSC_SYM_VARBIT` already exists compiler-side.
3. **Per-patch record store** (recommended). A fixed-size per-player table in
   mock230 — `{seed, planted_minute, stage, lives, flags}` × patch slot —
   saved and loaded by `mock230_save_player` / `mock230_load_player`, exposed
   as `patchstate_get(loc, field)` / `patchstate_set(loc, field, value)`.
   Retires `farming_runtime.varp` entirely. Per CLAUDE.md: allocation failure
   is an `assert`, and an unregistered patch loc reaching these ops is a
   contract violation, not a `return 0`.
4. **Widen `patches.dbrow`** to carry `state_varbit`, `transmit_varbit`,
   `mapzone`, `gardener`, `compost_bin`, `stage_mins`, `disease_free`. The
   registry becomes the single source of truth.
5. **Collapse the three mappings.** Delete both 50-branch switches in
   `farming_patch.rs2` and the per-patch arms in `farming_transmit.rs2`;
   replace with db lookups. Replace the 5 `[mapzone,…]` triggers with one
   generic sync driven by the registry.
6. **Generalise allotments and flowers** to the `~farming_catchup_bush(loc
   $patch)` shape that herbs, hops, bushes, trees and fruit trees already use.
   Deletes ~120 lines of Falador-specific procs from `farming_growth.rs2`.
7. **Selftest**: register two patches that differ only by db row, plant in
   both, assert independent state. Mutation to prove it: swap the two rows'
   `state_varbit` and watch one patch's growth appear in the other.

*Exit criterion: registering a new patch is one dbrow line and nothing else.*

### Phase 2 — content fill (#131, #134)

Data, not code, once Phase 1 lands.

8. **Remaining 66 plantables** as `.dbrow` rows from §2.2–2.10: 5 allotment,
   5 flower, 12 herb, 9 hops, 5 bush, 4 tree, 7 fruit tree, 5 hardwood, 12
   special, 3 anima, 3 coral. Each needs its `fullygrown` / `check_loc` /
   `stump_loc` locs resolved in `all.loc` — that is the real per-row work, not
   the numbers.
9. **Remaining ~67 patch instances** as `patches.dbrow` rows. Verify each
   wrapper loc and its varbit against `all.varbit.compack` — the file's
   existing comments show this was done by measurement, and guessing here
   produces patches that grow invisibly.
10. **New patch-type behaviours**: hardwood (like trees, 640 min stages),
    cactus/calquat (harvest with regrow), mushroom/belladonna (no protection),
    grape ×12 vinery slots, seaweed, celastrus (bark + lives), redwood,
    crystal tree, anima (global effect flags), spirit tree (teleport
    destination + `[oploc]` menu).

### Phase 3 — mechanics (#132, #133, #135)

11. **Disease** — the largest single gap. Per-cycle roll on stage advance,
    skipping stage 0 and the final stage; compost multiplier; Iasor ×0.2 with
    a 1/128 floor; watered-this-cycle reduction; flower/scarecrow protection
    (extend the existing `~farming_falador_flower_protects`); gardener
    protection; per-patch `disease_free` flag for diary/quest unlocks. Dead
    plants need spade removal.
12. **Cures** — plant cure on crops, secateurs prune at 75 % on trees/bushes,
    Lunar *Cure Plant*.
13. **Watering** — watering can charges (`watering_can_0..8`), Gricoller's can,
    per-cycle disease reduction for allotment/flower/hops. This also makes the
    already-transcribed charge table in `farming_tools.constant` load-bearing.
14. **Gardener payment** — dialogue on each of the 26 NPCs in §2.12, exact-item
    payment including noted form, per-patch protected flag.
15. **Compost application first, then completion.** Applying a compost bucket
    to a patch does not exist at all: the bin makes compost and nothing
    consumes it, while `farming_plant.rs2:83` hardcodes 3 lives. Wire the three
    tiers into a per-patch compost field that seeds lives (3/4/5/6) and feeds
    the disease multiplier, *then* add ultracompost via volcanic ash, compost
    potion conversion, Dump-all, bottomless bucket, big compost bin and bins
    5–7. This is a prerequisite for item 11, not a sibling of it.
16. **Correct yield model** — replace the `rand(256)` approximation with the
    CTS formula, add per-crop CTS pairs, wire magic secateurs, Farming cape,
    Attas and diary bonuses.
17. **Farmer's outfit XP bonus** (2.5 % set) into the Farming XP path.

### Phase 4 — surrounding content (#136, #137, and the openable parts of #138)

18. **Tool leprechaun note exchange** (#136) — the store is done; this is the
    produce→banknote op.
19. **`farming_view` opener** (#135) — the panel paints; the Geomancy/tab entry
    point is what is missing.
20. **Farming cape + hood** (#137) — Martin, Guild teleport, herb save bonus.
21. **Farming contracts** — Guildmaster Jane, three tiers, seed packs.
22. **Seed box / herb sack / seed vault** storage items.

### Phase 5 — blocked / out of scope for this lane

- **Tithe Farm** and **Hespori** stay assigned to KRONOS per #138. The seeds,
  fruits and reward items are all present in this cache
  (`hosidius_tithe_seed_a/b/c`, `tithe_reward_*`, `hespori_seed`), so the
  content side is unblocked whenever the owner picks it up.
- **Farming Guild map/tiers** beyond the contract NPC.

---

## 4. Verification

Follow the pattern `farming_tools` already set — a named section in
`mock230 --selftest`, with a mutation that makes each assertion fail:

| assertion | mutation that must break it |
|---|---|
| two registry patches keep independent state | swap their `state_varbit` |
| growth catch-up survives logout | shift `planted_minute` past the grown state |
| compost adds exactly +1/+2/+3 lives | drop one tier's row |
| an uncomposted patch starts at exactly 3 lives | hardcode 3 in the composted path too — the current bug, and it must fail |
| diseased crop stops advancing | force the disease roll to always miss |
| gardener payment consumes the exact items | change one payment quantity |
| flower protection gates on *grown*, not planted | plant the flower, don't grow it |
| yield matches the CTS formula at L1 and L99 | swap CTS-low and CTS-high |

Two traps this tree has already paid for and that apply directly here:

- [`pristine-baseline-skips`] — a worktree without the cache **skips** suites,
  and a skip reads as a pass. Confirm the farming section actually ran.
- [`headless-runs-are-not-independent`] — use a scratch `MOCK230_SAVES`; farming
  is all `scope=perm` state, so a leftover save from the previous run will make
  a broken growth clock look fine.

---

## 5. Open questions

1. **Phase 1 item 3** — engine-side record store, or stay with generated varps?
   The store is more work up front and removes ~400 config blocks plus a whole
   class of save/load bug. Recommendation: build it.
2. **Era gate** — do the Varlamore/Sailing map squares (Kastori, Auburnvale,
   Aldarin, Ortus Farm, Nemus Retreat, Coral Nurseries, Anglers' Retreat) exist
   in `all.loc` at this revision? The items do. This decides whether §2.1's
   count is ~110 or ~90.
3. **Disease rates** — the wiki gives per-cycle chances per crop
   ([Disease](https://oldschool.runescape.wiki/w/Disease_(Farming))); the tree's
   2009scape reference gives different ones. Modern OSRS values are the right
   target since the XP table already matches modern OSRS.
