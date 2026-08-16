# Finishing Mining

Plan to take `skill_mining/` from its current vertical slice to the complete
[Mining](https://oldschool.runescape.wiki/w/Mining) skill. Wiki is the authority
for behaviour (levels, XP, rates, gear effects, locations); the cache
(`OSRS-Content/osrs239-content/configs/all.loc`, `all.obj`, `all.npc`) is the
authority for names and for what is *expressible* at this revision;
`LostCity_Server/content/scripts/skill_mining/` is the reference for the script
shape wherever it has one.

Queue rows this closes: **#49, #50, #51, #52, #53, #55** in
[`SKILLS_CONTENT_PORT_QUEUE.md`](SKILLS_CONTENT_PORT_QUEUE.md). #54 (Blast /
Volcanic / Stars) stays in the KRONOS lane and is scoped in §9.

---

## 0. Where mining stands today — measured

`OSRS-Content/osrs239-content/server/scripts/skill_mining/` is **496 lines**
across 2 scripts and 4 config files:

| file | lines | what it holds |
|---|---:|---|
| `scripts/mining.rs2` | 236 | swing loop, prospect, pickaxe checker, rate/anim switches |
| `scripts/dwarf_mining_guild.rs2` | 29 | guild dwarf Talk |
| `configs/mine.dbrow` | 122 | **11 rock families** |
| `configs/rocks.loc` | 97 | **24 loc overlays** (22 rocks + `rocks1`/`rocks2`) |
| `configs/mine.dbtable` | 10 | `mining_table` schema |
| `configs/mining.constant` | 2 | `^mining_ore_normal` / `^mining_ore_fast` |

It is a working vertical slice: swing → `%action_delay` roll → `stat_random`
success → `loc_change` deplete → `inv_add` + `stat_advance`, with Prospect and a
pickaxe ladder. Sounds (`mine_quick`, `mine`, `prospect`) resolve.

### 0.1 Coverage against the cache

The osrs239 cache carries **234 locs with `op1=Mine`**. The tree binds **22** of
them. That ratio is the whole plan in one number.

| ore family | in `mine.dbrow` | wiki |
|---|:--:|---|
| clay, copper, tin, iron, silver, coal, gold, mithril, adamantite, runite | ✅ | [Ore](https://oldschool.runescape.wiki/w/Ore) |
| blurite | ✅ | [Blurite ore](https://oldschool.runescape.wiki/w/Blurite_ore) |
| rune essence / pure essence | ❌ | [Rune essence](https://oldschool.runescape.wiki/w/Rune_essence) |
| gem rocks | ❌ | [Gem rock](https://oldschool.runescape.wiki/w/Gem_rock) |
| limestone | ❌ | [Limestone](https://oldschool.runescape.wiki/w/Limestone) |
| sandstone | ❌ (quest-only, §4.2) | [Sandstone](https://oldschool.runescape.wiki/w/Sandstone) |
| granite | ❌ (quest-only, §4.2) | [Granite](https://oldschool.runescape.wiki/w/Granite) |
| amethyst | ❌ | [Amethyst](https://oldschool.runescape.wiki/w/Amethyst) |
| soft clay rocks | ❌ | [Soft clay](https://oldschool.runescape.wiki/w/Soft_clay) |
| volcanic ash | ❌ | [Volcanic ash](https://oldschool.runescape.wiki/w/Volcanic_ash) |

### 0.2 The queue notes are stale — this is already live

Five things `SKILLS_CONTENT_PORT_QUEUE.md` rows #52–#54 call absent have landed
since that audit tick. Do **not** re-implement them:

| thing | where | wiki |
|---|---|---|
| Dragon + crystal pickaxe in `~pickaxe_checker` (61 / 71) | `mining.rs2:167` | [Pickaxe](https://oldschool.runescape.wiki/w/Pickaxe) |
| "Rock Knocker" +3 Mining spec, all four pickaxes | `skill_combat/scripts/player/specwep.rs2:180` | [Dragon pickaxe](https://oldschool.runescape.wiki/w/Dragon_pickaxe#Special_attack) |
| Infernal pickaxe smelt-on-mine + charge drain | `general/scripts/enchanted_jewellry/infernal_pickaxe.rs2:78` | [Infernal pickaxe](https://oldschool.runescape.wiki/w/Infernal_pickaxe) |
| Celestial ring / signet invisible boost | `general/scripts/enchanted_jewellry/celestial_ring.rs2:100` | [Celestial ring](https://oldschool.runescape.wiki/w/Celestial_ring) |
| Crystal pickaxe charge drain | `minigames/minigame_gauntlet/scripts/crystal_equipment.rs2:86` | [Crystal pickaxe](https://oldschool.runescape.wiki/w/Crystal_pickaxe) |
| Motherlode Mine core (veins → paydirt → hopper → sack) | `minigames/minigame_motherlode/` | [Motherlode Mine](https://oldschool.runescape.wiki/w/Motherlode_Mine) |
| Mining Guild 60 gate (ladder + door) | `areas/falador/scripts/mining_guild.rs2` | [Mining Guild](https://oldschool.runescape.wiki/w/Mining_Guild) |

Update those queue rows as part of slice 0 rather than leaving them to mislead
the next audit.

---

## 1. Structural fixes, before any content lands

Five things in the current 236 lines will multiply by every rock family added.
Fix them first; each is small on its own and expensive after.

### 1.1 `loc_type = rocks1 | loc_type = rocks2` must become a loc param

`mining.rs2:49` and `:62` hardcode the two depleted locs. The cache has a
*different* depleted loc per family — `amethystrock_empty`, `limestone_rock_noore`,
`swamp_rock_noore`, `punishrocks_no_ore`, `camdozaalrock1_empty`,
`varlamore_mining_rock_empty`, `rocks3` — so this test is wrong the moment
anything past the standard ten lands.

**Fix:** mint the reference's own param. LostCity uses
`mining_rock_empty` (`skill_mining/configs/mine.param`, `type=int default=0`),
tested as `if (loc_param(mining_rock_empty) = ^true)`. Allocate it in
`pack/param.alloc` (currently 114 entries; `next_loc_stage` = 2641 is the
existing precedent) and set it on every depleted overlay.

### 1.2 `~mining_use_pickaxe` whitelists six pickaxes — a live bug

`mining.rs2:38` accepts only bronze→rune. Use-a-dragon-pickaxe-on-a-rock
falls through to `~displaymessage(^dm_default)` even though `~pickaxe_checker`
one screen down understands dragon and crystal. Same list, two places.

**Fix:** replace the `|` chain with `oc_category(last_useitem) = weapon_pickaxe`.
This costs **no new config** — `weapon_pickaxe` is already category 67 in
`pack/category.pack:138`, and the osrs239 cache already assigns it to all 28
pickaxes, the full ladder included: bronze, iron, steel, black, mithril,
adamant, rune, `trail_gilded_pickaxe`, dragon, `dragon_pickaxe_pretty`,
`3a_pickaxe`, infernal, crystal, `gauntlet_pickaxe*`, `zalcano_pickaxe`, the
three `nzone_*` and the seven `trailblazer_*`.

Two members of that category should **not** mine on the same terms:
`infernal_pickaxe_empty` (mines, but no smelt proc) and
`crystal_pickaxe_inactive` (an uncharged shell). Branch on them explicitly
rather than letting the category answer for them.

### 1.3 `~pickaxe_checker` is missing four pickaxes

Live ladder: crystal 71, dragon 61, rune 41, adamant 31, mithril 21, steel 6,
iron, bronze. Missing, all present in the cache by name:

| obj (cache name) | Mining | Attack | wiki |
|---|:--:|:--:|---|
| `infernal_pickaxe` | 61 | 60 | [Infernal pickaxe](https://oldschool.runescape.wiki/w/Infernal_pickaxe) |
| `3a_pickaxe` | 61 | 65 | [3rd age pickaxe](https://oldschool.runescape.wiki/w/3rd_age_pickaxe) |
| `black_pickaxe` | 11 | 10 | [Black pickaxe](https://oldschool.runescape.wiki/w/Black_pickaxe) |
| `trail_gilded_pickaxe` | 41 | 40 | [Gilded pickaxe](https://oldschool.runescape.wiki/w/Gilded_pickaxe) |

`infernal_pickaxe` is the sharp one: `get_ore_normal`/`get_ore_fast` already call
`~infernal_pickaxe_proc`, so today the smelt-on-mine effect can only fire for a
player *also* carrying a pickaxe the checker recognises. Add
`dragon_pickaxe_pretty` and `gauntlet_pickaxe_hm` at the same time.

### 1.4 Pickaxe rate/anim: switches or params — decide once

`mining.rs2` comments claim "obj overlays only accept `levelrequire`". That is
**not true** — `fields/obj.ini:3` documents ordinary `param=<name>,<value>` rows
via `mock230_objinfo_param_overlay`, and `equipment.obj` uses them. The two
30-line switch procs (`~mining_pickaxe_rate`, `~mining_pickaxe_anim`) can become
LostCity's `param=mining_rate` / `param=mining_animation`
(`skill_mining/configs/pickaxes.param`), read with `oc_param($pickaxe, …)`.

Either is defensible at 8 pickaxes; at 12 with variants the params win, and they
delete the third copy of the ladder living in `motherlode_mine.rs2`. Do this
before §5, not after, and fix the stale comment either way.

### 1.5 `~scale_by_playercount` needs an engine op that isn't wired

LostCity scales every respawn by population
(`general/scripts/player_count.rs2`):

```
[proc,scale_by_playercount](int $base)(int)
def_int $playercount = min(playercount, 2000);
return (scale(sub(4000, $playercount), 4000, $base));
```

`SS_OP_SCALE` (4618) is implemented. `SS_OP_PLAYERCOUNT` (**1018**) is **not** —
`src/net/mock/mock230_opcode_coverage.gen.h` has 1016 `MAP_PLAYERCOUNT` but no
1018. Three options, in preference order:

1. Write the proc against `map_playercount` (zone-local, already implemented).
   Wrong semantics at scale, right answer at this server's population.
2. Implement `SS_OP_PLAYERCOUNT` in `mock230_scripts.c` and regenerate coverage
   (`python3 net/mock/gen_opcode_coverage.py`; `make -C src test-mock230-coverage`).
3. Ship `~scale_by_playercount` as identity and leave a comment.

Pick (1) or (2) — but the proc must exist and be called, because every new
`rock_respawnrate` row below assumes it.

### 1.6 Gem-necklace category has no cache carrier

LostCity gates the gem-find boost on `oc_category($neck) = category_557`. In
osrs239, `[amulet_of_glory]` (`all.obj:27302`) carries **no `category=` line at
all** — the check is unwritable as-is.

**Landed as direct enumeration, not a category mint.** Minting `gem_necklace`
in `pack/category.pack` and assigning it via a server `.obj` overlay was the
first attempt, matching how `mining_rock_normal`/`maplink_transition` mint
categories on **locs**. It does not carry over to objs: `mock230_content.c`'s
`obj_config_key` refuses a `category=<name>` overlay line on a cache-defined
obj today ("obj key `category` is the cache's to state, ignored") — loc's
`category=` overlay and obj's are two different code paths, and only the
loc one accepts an overlay. A same-named function
(`mock230_objinfo_category_overlay`) exists but was mid-flight in a
concurrent session's uncommitted changes to that exact file when this was
implemented — not something to build on top of un-landed.

`~mining_gem_necklace_worn` in `mining.rs2` enumerates the nine glory-family
objs directly instead (`amulet_of_glory`, `_1`..`_6`, `_inf`,
`trail_amulet_of_glory`). Nine names is small enough that this isn't a real
cost, and it unblocks slice 3 without depending on unmerged engine work. If
the category overlay capability lands later, this collapses to one
`oc_category` line — revisit then, not now.

---

## 2. Slice #49 — Rune essence and pure essence

Wiki: [Rune essence](https://oldschool.runescape.wiki/w/Rune_essence) ·
[Pure essence](https://oldschool.runescape.wiki/w/Pure_essence) ·
[Rune Essence Mine](https://oldschool.runescape.wiki/w/Rune_Essence_mine)

The teleport in is already live (`skill_runecraft/scripts/essence_mine.rs2`,
Aubury/Sedridor). Only the rock is missing — you can stand in the mine and not
mine it.

**Locs** (all present in `all.loc`):

| loc | name | note |
|---|---|---|
| `blankrunestone` | Rune Essence | the mine's rock; **never depletes** |
| `big_essence_rock` | Rune essence | large variant |
| `lunar_runestone_top` | Rune essence | Lunar Isle mine |

**Objs:** `blankrune` (Rune essence), `blankrune_high` (Pure essence).

**Rows** — one `mining_table` row, no `rock_respawnrate`, success 256/256:

| field | value |
|---|---|
| `rock_level` | 1 |
| `rock_exp` | 50 (5.0 XP) |
| `rock_successchance` | 256, 256 |
| output | `blankrune` / `blankrune_high` at Mining 30 |

**Behaviour** (LostCity `get_ore_essence`): no `loc_change`, no gem roll, no
success roll — add, advance, `p_oploc` and keep swinging. Pure essence is a
level check on the same rock, not a second rock: **30 Mining and a members
world** yields pure essence, otherwise rune essence. That level branch is the
one thing LostCity's `get_ore_essence` does *not* do (its `rock_output` is flat
`blankrune`), so read it off the wiki, not the reference.

**Files:** `mine.dbrow` (+1 row), `rocks.loc` (+3 blocks, `mining_rock_empty=0`),
`mining.rs2` (+`[oploc1,blankrunestone]` family → new `@get_ore_essence`).

**Also closes:** the `pure essence mining` half of queue #37.

---

## 3. Slice #50 — Gem rocks, the random gem table, and the necklace boost

Wiki: [Gem rock](https://oldschool.runescape.wiki/w/Gem_rock) ·
[Mining § Gems](https://oldschool.runescape.wiki/w/Mining) ·
[Shilo Village](https://oldschool.runescape.wiki/w/Shilo_Village)

Three separate mechanics that the queue row bundles:

### 3.1 Gem rocks proper

**Locs:** `gemrock`, `gemrock1` (category 163), `village_gem_rock1/2/3` (Shilo,
category 163). No depleted variant exists under a `gem` name — check whether the
cache reuses `rocks1`/`rocks2` here before minting one.

**Row:** level 40, 65 XP, respawn 200 ticks, `rock_successchance,28,70`
(LostCity `mine.dbrow:[gem_rock]`). `rock_output` is empty — output comes from a
drop table, not the row.

**Drop table** (LostCity `gem_rock_table.dbrow`, total 128):

| gem | weight | wiki |
|---|:--:|---|
| `uncut_opal` | 60 | [Uncut opal](https://oldschool.runescape.wiki/w/Uncut_opal) |
| `uncut_jade` | 30 | [Uncut jade](https://oldschool.runescape.wiki/w/Uncut_jade) |
| `uncut_red_topaz` | 15 | [Uncut red topaz](https://oldschool.runescape.wiki/w/Uncut_red_topaz) |
| `uncut_sapphire` | 9 | |
| `uncut_emerald` | 5 | |
| `uncut_ruby` | 5 | |
| `uncut_diamond` | 4 | |

**Blocker:** `~roll_on_drop_table` **does not exist in this tree**.
`drop_tables/configs/drop_table.dbtable` is present and its header comment says
so explicitly — the schema was ported and left rowless because LostCity's only
caller was mining. This slice is the caller that makes it real. Write
`~roll_on_drop_table(dbtable)` in `drop_tables/scripts/`, add `gem_rock_table`
as the first `.dbrow` in that directory, and the 71 threshold-walk scripts
already there stay as they are.

### 3.2 The 1/256 gem while mining any rock

Every non-essence, non-gem rock rolls for a gem *before* the ore roll. LostCity:
`random(256)`, improved to `random(86)` with a gem necklace worn on a members
world, then `~mining_gem_table` — diamond <2, ruby <10, emerald <26, sapphire
<58 of 128, else nothing (so ~55% of the 1/256 procs give nothing).

Confirm both the 256/86 constants and the "members only" gate against
[Mining](https://oldschool.runescape.wiki/w/Mining) before shipping — LostCity's
numbers are rev-254-era and this is one of the mechanics Jagex has revisited.

Depends on §1.6 for the necklace category.

### 3.3 The gem-rock success-rate boost

Gem rocks additionally triple `low`/`high` with a gem necklace worn
(LostCity `get_ore_gem_rock`). Same category gate.

**Files:** `mine.dbrow`, `rocks.loc`, `mining.rs2` (+`get_ore_gem_rock` label,
+gem roll in both `get_ore_*` labels), `drop_tables/scripts/drop_table.rs2`
(new), `drop_tables/configs/gem_rock_table.dbrow` (new),
`pack/category.pack` + a `.obj` overlay for `gem_necklace`.

Sound: `found_gem` — verify it resolves in `pack/4_soundeffects.pack` the way
`mine`/`mine_quick`/`prospect` do (see `docs/SKILLING_SOUNDS.md` §4.1).

---

## 4. Slice #51 — Members ore rocks

### 4.1 Limestone — straightforward

Wiki: [Limestone](https://oldschool.runescape.wiki/w/Limestone)

**Locs:** `limestone_rock1/2/3` (category 2429), depleted `limestone_rock_noore`
("Pile of rock"). LostCity has all three rows already
(`mine.dbrow:[limestone_rock1..3]`, named `loc_4027`–`loc_4029` there — remap to
our names): level 10, 265 XP-tenths, `95,310`, respawn 10/20/40 ticks by variant.

`swamp_rock1/2/3` + `swamp_rock_noore` ("Pile of Rock") are the second limestone
site — check which quarry each belongs to before assuming they share a row.

### 4.2 Sandstone and granite — collides with an existing quest bind

Wiki: [Sandstone](https://oldschool.runescape.wiki/w/Sandstone) ·
[Granite](https://oldschool.runescape.wiki/w/Granite) ·
[Desert Quarry](https://oldschool.runescape.wiki/w/Desert_Quarry)

**This is the trap in this slice.** `enakh_sandstone_rocks` and
`enakh_granite_rocks` (categories 2430 / 2431) are *already bound* by
`quests/quest_enakhraslament/scripts/enakhraslament_quarry.rs2:114` and `:129`,
which hand a fixed `enakh_sandstone_medium` / granite block with no level gate,
no XP, and no roll. Adding a `mining_table` row for those locs does not override
that — the quest's `[oploc1,…]` wins and the row is dead.

Resolve deliberately: either the quest binds move to a `~mining_swing` call with
a quest-specific output branch, or the real quarry gets its own locs. Decide
before writing rows.

Output is weight-tiered, and the tier is **rolled per swing**, not chosen:

| obj | Mining | XP | | obj | Mining | XP |
|---|:--:|:--:|---|---|:--:|:--:|
| Sandstone (1kg) | 35 | 30 | | Granite (500g) | 45 | 50 |
| Sandstone (2kg) | 35 | 40 | | Granite (2kg) | 45 | 60 |
| Sandstone (5kg) | 35 | 50 | | Granite (5kg) | 45 | 75 |
| Sandstone (10kg) | 35 | 60 | | | | |

All seven objs exist by name (`all.obj:110800`–`110934`). The weight roll is not
expressible as a single `rock_output` column — it needs either a per-tier drop
table (reusing §3.1's `~roll_on_drop_table`) or a `~sandstone_roll` proc. Both
are fast rocks (`^mining_ore_fast`), like iron.

`Sandstone (20kg)` / `(32kg)` exist in the cache but are not quarry drops —
leave them.

### 4.3 Amethyst

Wiki: [Amethyst](https://oldschool.runescape.wiki/w/Amethyst) ·
[Mining Guild](https://oldschool.runescape.wiki/w/Mining_Guild)

**Locs:** `amethystrock1`, `amethystrock2` (category 967), depleted
`amethystrock_empty` ("Empty wall", category 1134). Note these are *wall* locs
(`shape1=0,…`), not the usual ground scenery — check `loc_change` behaves for a
wall shape before assuming the standard deplete wire works.

Level 92, 240 XP, output `amethyst` (`all.obj:276412`). Located in the Mining
Guild's lower level — gated behind §7.

### 4.4 Soft clay rocks

Wiki: [Soft clay](https://oldschool.runescape.wiki/w/Soft_clay)

**Locs:** `softclayrock1`, `softclayrock2`, `prif_mine_softclayrock1`. Output
`softclay` (`all.obj:28207`) directly — same level/XP as clay. Trivial row once
§1.1 lands; do it with limestone.

### 4.5 Volcanic ash

Wiki: [Volcanic ash](https://oldschool.runescape.wiki/w/Volcanic_ash)

`fossil_ashpile` / `fossil_ashpile_empty`. Level 22, 10 XP, output count scales
with level (1 at 22, up to 3). Fossil Island — check the island is reachable in
this tree before spending the slice; if not, park it with §9.

---

## 5. Slice #52 — Pickaxe ladder

Wiki: [Pickaxe](https://oldschool.runescape.wiki/w/Pickaxe)

Most of this is §1.2–§1.4. What remains after those:

| item | what | wiki |
|---|---|---|
| Pickaxe repair | Nurmof / Yarsul re-attach a broken head | [Nurmof](https://oldschool.runescape.wiki/w/Nurmof) |
| `to_be_fixed_by_nurmof` | the deferred obj from CONTENT 8x | |
| Shops | `shop/mining_guild/yarsuls_prodigious_pickaxes` and `shop/keldagrim/pickaxe_is_mine` exist as inv configs — verify each stocks the right ladder | [Yarsul](https://oldschool.runescape.wiki/w/Yarsul) |

Pickaxe *head breaking* is LostCity's random-event/macro system
(`param=broken`, `param=pickaxe_head`, the `macro_*` rock family). OSRS has no
such event — implement repair for the quest-granted broken pickaxes only, and
do not port the macro rocks.

---

## 6. Slice #53 — Mining gear perks

Wiki: [Mining § Equipment](https://oldschool.runescape.wiki/w/Mining)

Every obj below is present in `all.obj`. All six hook the same two call sites in
`mining.rs2` (`get_ore_normal` / `get_ore_fast`), beside the existing
`~celestial_ring_proc` / `~infernal_pickaxe_proc` calls — which is the argument
for collapsing those two labels into one before adding six more procs.

**Cache names are not the display names here** — every one of these had to be
resolved by walking `name=` back to its block, and four of them sit under a
`motherlode_*` or `mguild_*` prefix that a grep for "prospector" or "mining
gloves" will never find:

| perk | effect | cache obj |
|---|---|---|
| [Prospector kit](https://oldschool.runescape.wiki/w/Prospector_kit) | +0.4 / 0.8 / 0.6 / 0.2 % XP, +1% set bonus = 2.5% | `motherlode_reward_hat` / `_top` / `_legs` / `_boots` |
| [Varrock armour](https://oldschool.runescape.wiki/w/Varrock_armour) | chance at a second ore, tier-gated by ore | `varrock_armour_easy/medium/hard/elite` |
| [Mining gloves](https://oldschool.runescape.wiki/w/Mining_gloves) | rock does not deplete (iron→gold) | `mguild_gloves` |
| [Superior mining gloves](https://oldschool.runescape.wiki/w/Superior_mining_gloves) | same, runite/amethyst | `mguild_gloves_superior` |
| [Expert mining gloves](https://oldschool.runescape.wiki/w/Expert_mining_gloves) | both tiers | `mguild_gloves_expert` |
| [Bracelet of clay](https://oldschool.runescape.wiki/w/Bracelet_of_clay) | clay mines as soft clay, 28 charges | `jewl_bracelet_of_clay` — reuse `~charges_item_*` |
| [Mining cape](https://oldschool.runescape.wiki/w/Mining_cape) | +1 Boost, teleport to Mining Guild | `skillcape_mining` — add the branch to `skillcape_boost.rs2` |
| [Gem bag](https://oldschool.runescape.wiki/w/Gem_bag) / [Coal bag](https://oldschool.runescape.wiki/w/Coal_bag) | storage, fill on mine | `gem_bag`, `coal_bag` |

There is a **second** prospector set in the cache —
`fossil_motherlode_reward_hat/_top/_legs/_boots`, same display names. Resolve
which family the Motherlode shop issues before wiring §7's Percy, or the set
bonus will check four objs the player does not have.

The XP-bonus perks (prospector) and the drop-doubling perks (Varrock armour) do
**not** compose the same way; keep them as two distinct procs, not one
"~mining_bonus".

`skillcape_mining` already has rows in `skill_combat/configs/equipment.obj` and
`levelrequire.dbrow` — only the `skillcape_boost.rs2` branch is missing, which is
four lines matching the eleven capes already there.

---

## 7. Mining Guild interior and the modern reward loop

Wiki: [Mining Guild](https://oldschool.runescape.wiki/w/Mining_Guild) ·
[Motherlode Mine](https://oldschool.runescape.wiki/w/Motherlode_Mine)

The 60-level gate is live; the guild's *contents* are not.

| thing | cache name | wiki |
|---|---|---|
| Amethyst area (§4.3) + the 92 gate | `amethystrock1/2` | [Amethyst](https://oldschool.runescape.wiki/w/Amethyst) |
| Unidentified minerals drop while mining in the guild | `mguild_minerals` | [Unidentified minerals](https://oldschool.runescape.wiki/w/Unidentified_minerals) |
| Mineral Exchange — trades minerals for the three glove tiers | `mguild_gloves*` | [Belona](https://oldschool.runescape.wiki/w/Belona) |
| Prospector Percy — nuggets → prospector kit, coal/gem bag, soft-clay pack | `motherlode_nugget` | [Prospector Percy](https://oldschool.runescape.wiki/w/Prospector_Percy%27s_Nugget_Shop) |
| Rock golem pet roll on every successful mine | `skillpetmining*` | [Rock golem](https://oldschool.runescape.wiki/w/Rock_golem) |

`motherlode_nugget` (the golden nugget) and the sack already exist in
`minigame_motherlode/`, so Percy is a shop wiring job, not a minigame one.

The pet is the awkward one: the cache carries **eighteen** `skillpetmining_*`
objs — `_tin`, `_copper`, `_iron`, `_blurite`, `_silver`, `_coal`, `_gold`,
`_mithril`, `_granite`, `_adamantite`, `_runite`, `_amethyst`, `_lovakite`,
`_elemental`, `_daeyalt`, `_lead`, `_rubium`, `_nickel`, plus the bare
`skillpetmining` — all named "Rock golem". The golem's appearance is **the rock
you were mining when it dropped**, so the roll needs a rock→pet map, not a
single obj. That map is a natural `mining_table` column
(`rock_pet,namedobj`), which is why this belongs after §1 and not before it.

---

## 8. Slice #55 — Miscellania mining intercept

Wiki: [Managing Miscellania](https://oldschool.runescape.wiki/w/Managing_Miscellania)

`areas/area_miscellania/scripts/miner_magnus.rs2` exists with Talk only; its
header says the intercept is deferred. `%misc_approval` is live and already
incremented by `weed_herbs.rs2`, so the varp and the pattern are both in place.

Port LostCity `~magnus_intercept_ore` (`miner_magnus.rs2:26`): inside the
Miscellania zone, a successful mine gives **XP but no ore**, bumps
`%misc_approval` (capped 127), and makes `misc_miner` say "Thanks!". `inzone`
(`SS_OP_INZONE` 1004) is implemented. `misc_dummy_coalrock1` in the cache is
the loc this hangs off.

Call it from `get_ore_normal`/`get_ore_fast` **before** `inv_add`, exactly where
LostCity puts it. The paired fishing intercept is queue #67 — same shape, same
file layout; land them together if convenient.

---

## 9. Explicitly out of this plan

These stay in the KRONOS lane (queue #54 and beyond). They are listed so the
next audit does not re-emit them here. The cache has locs for all of them.

| activity | locs | wiki |
|---|---|---|
| Blast mine | `digblastbrick`, `lovakengj_blast_mining_hud.if` | [Blast mine](https://oldschool.runescape.wiki/w/Blast_mine) |
| Volcanic Mine | `fossil_volcanic_mine.if` | [Volcanic Mine](https://oldschool.runescape.wiki/w/Volcanic_Mine) |
| Shooting Stars | `star_size_one_star` … `star_size_nine_star` | [Shooting Stars](https://oldschool.runescape.wiki/w/Shooting_Stars) |
| Camdozaal / barronite | `camdozaalrock1/2` + `_empty` | [Barronite deposit](https://oldschool.runescape.wiki/w/Barronite_deposit) |
| Daeyalt essence | `area_sanguine_mine_minerocks_01..03`, `daeyalt_stone_top_active` | [Daeyalt essence](https://oldschool.runescape.wiki/w/Daeyalt_essence) |
| Weiss salt / basalt | `my2arm_saltrock_*` | [Salt](https://oldschool.runescape.wiki/w/Salt) |
| Lovakite | `lovakite_rock1/2`, `crimson_lovakite*` | [Lovakite ore](https://oldschool.runescape.wiki/w/Lovakite_ore) |
| Volcanic sulphur | `sulphur_rock_01..03` | [Volcanic sulphur](https://oldschool.runescape.wiki/w/Volcanic_sulphur) |
| Varlamore calcified rocks | `varlamore_mining_rock*` | [Calcified deposit](https://oldschool.runescape.wiki/w/Calcified_deposit) |
| Mine of Trials (lead/nickel/rubium) | `leadrock1`, `nickelrock1`, `rubiumrock1` | [Mine of Trials](https://oldschool.runescape.wiki/w/Mine_of_Trials) |
| Zalcano | `zalcano_rock_active/partial` | [Zalcano](https://oldschool.runescape.wiki/w/Zalcano) |
| Guardians of the Rift | `gotr_essence_tier_*` | [Guardians of the Rift](https://oldschool.runescape.wiki/w/Guardians_of_the_Rift) |
| LotR / Prifddinas / GIM mine variants | `lotr_mine_wall_*`, `prif_mine_*`, `gim_ironrock` | area-owned |

Also out of scope: LostCity's `macro_*` rock family and `afk_event` random
events (no OSRS equivalent), and the tutorial rocks
(`tut2_copperrock`/`tut2_tinrock`/`newbiecopperrock`/`newbietinrock`) which
belong to whichever lane owns Tutorial Island.

---

## 10. Ordering

Dependencies are real here — §1 gates everything, §3.1 gates §4.2, §1.6 gates
§3.2/§3.3.

| # | slice | depends on | closes |
|---|---|---|---|
| 0 | Correct the stale queue rows (§0.2) | — | bookkeeping |
| 1 | `mining_rock_empty` param (§1.1) | — | gates all rows |
| 2 | Pickaxe fixes: whitelist, ladder, params (§1.2–1.4) | — | #52 (most) |
| 3 | `~scale_by_playercount` + `playercount` op (§1.5) | — | respawn correctness |
| 4 | Rune / pure essence (§2) | 1 | #49, #37 half |
| 5 | `gem_necklace` category (§1.6) | — | gates 6 |
| 6 | `~roll_on_drop_table` + gem rocks + 1/256 table (§3) | 1, 5 | #50 |
| 7 | Limestone + soft clay (§4.1, §4.4) | 1, 3 | #51 part |
| 8 | Sandstone + granite, quest-bind resolution (§4.2) | 1, 6 | #51 part |
| 9 | Mining gear perks (§6) | 2 | #53 |
| 10 | Mining Guild interior + amethyst (§4.3, §7) | 1, 9 | #51 rest |
| 11 | Miscellania intercept (§8) | — | #55 |
| 12 | Selftest coverage (§11) | all | — |

---

## 11. Verification

There is **no mining coverage in `selftest*.rs2` today** — `grep -n mining
selftest*.rs2` returns nothing. Every slice above lands untested unless this
changes, so treat it as part of the work, not a follow-up.

For each slice, mutate the implementation to prove the assertion can actually
fail before trusting it — a `.dbrow` typo makes most of these pass vacuously.

Minimum suite, in a new `skill_mining/scripts/mining_selftest.rs2`:

1. **Row integrity** — every `mining_table` row's `rock` locs resolve, and every
   loc carrying `mining_rock_normal`/`mining_rock_fast` has a row. The current
   silent failure mode is `db_findnext = null` → `~displaymessage(^dm_default)`,
   which reads as a UI bug, not a data bug.
2. **Deplete/respawn** — `loc_change` to the family's own empty loc, and the
   empty loc answers `loc_param(mining_rock_empty) = ^true`.
3. **Pickaxe ladder** — `~pickaxe_checker` returns the best usable pick for a
   given level, and `oc_category(last_useitem)` accepts every one of the twelve.
4. **Gem table** — `~roll_on_drop_table(gem_rock_table)` over N rolls stays
   inside the 128-total weights.
5. **Essence** — never depletes, and the level-30 branch switches output.
6. **Perks** — prospector XP multiplier, Varrock armour double, gloves
   no-deplete, each proven by a mutation.

Run headless against a throwaway `MOCK230_SAVES` directory — headless runs share
state, and a stale profile's Mining level will decide the result of every level
gate above.

Pack check after every slice: `mock230_pack` must stay at **0 errors**.
