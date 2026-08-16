# Finishing Smithing

Plan to take `skill_smithing/` from its F2P vertical slice to the complete
[Smithing](https://oldschool.runescape.wiki/w/Smithing) skill. The wiki is the
authority for content (products, levels, XP, bars-per-item, tools, gates); the
cache (`OSRS-Content/osrs239-content/configs/all.obj`, `all.loc`,
`pack/category.pack`) is the authority for names and for what is *expressible*
at this revision.

Queue rows this closes: **#56–#62** in
[`SKILLS_CONTENT_PORT_QUEUE.md`](SKILLS_CONTENT_PORT_QUEUE.md).

## Implementation status (2026-08-16)

**Done: S1, S2, S3, S4, S5, S6, S8, S9, S11, S12, S14, plus `::smithingrun`
wired into `mock230 --selftest`.** Verified: clean `make -C src
mock230-scripts` + `make -C src mock230`, and `./src/build_opt/mock230
--selftest` passes `::smithingrun` with no new failures against the
pre-existing baseline (the varp collision, `NPC_FINDOWNED`, `::maxstats`, the
running-tick-cost check, and the castle-door pair — none smithing-related).

- **S1**: `[oploc1,_anvil]` now opens the same menu the Use-item path always
  had; both share `~smithing_anvil_gate_ok` (Doric's Quest + `camdozaal_anvil`
  exclusion).
- **S2**: `smithing.dbtable` gained a `kind` column; the six ~140-line
  per-metal `switch_int` procs are gone, replaced by
  `~smithing_product_for`'s `db_find`-then-loop filter (the same idiom
  `maplink_try` already used for a second-key lookup). Every product added
  after this point was a dbrow, not a script edit.
- **S3–S6**: 56 new dbrows — dart tips, arrowtips, throwing knives, javelin
  heads, unfinished bolts, crossbow limbs (bronze→rune + blurite), claws
  (bronze→rune), mith grapple tip, nails (bronze/iron/mithril/adamant/rune —
  steel already existed, and its `product_amount` bug, 2 instead of 15, got
  fixed in the same pass), bronze wire, iron spit, oil lantern frame, steel
  studs, bullseye lantern (unf), blurite bar smelting + its two anvil
  products. Items gated on an unported quest (Tourist Trap, Death Plateau,
  The Knight's Sword) ship ungated with the debt recorded in a comment,
  matching this tree's existing convention for that situation.
- **S8**: cannonball XP fixed (375 → 256, i.e. 25.6 — the wiki's steel
  cannonball rate, not the steel bar rate it was silently paying), the
  missing Dwarf Cannon quest gate added (`%mcannon < ^mcannon_complete`), and
  the single-set click turned into a loop like every other smithing/smelting
  action in this tree.
- **S9**: `~has_smithing_hammer` (hammer in inventory or Imcando hammer worn
  *or* carried) replaces every `inv_total(inv, hammer) < 1` check across
  `smithing.rs2`, `smelting.rs2`, `cannonballs.rs2`, and `dragon_sq.rs2`.
  `~smithing_has_gold_xp_bonus` folds the Smithing cape into the existing
  goldsmith-gauntlet gold-XP bonus (applies once, not twice, if both worn).
  Varrock armour's 10%-double-smelt perk is implemented tree-wide rather than
  Edgeville-only — this tree has no verified in-cache coordinate for that one
  furnace placement, and fabricating a bounding box was judged worse than the
  documented simplification (see the comment in `smithing_gear.rs2`).
- **S11**: gold bowl (Legends' Quest gate, `%legendsquest >= ^legends_complete`)
  as a normal anvil product — no furnace involved, correcting this plan's own
  earlier guess that it needed `furnace_legendsquest`.
- **S12**: dragonfire shield assembly is new (`smithing_assembly.rs2`, level
  90, 200 XP). The godsword blade turned out to **already exist** —
  `areas/area_godwars/scripts/godwars_godsword.rs2` had the full shard-combine
  chain, just missing the wiki's level-80 gate and 100 total XP; those were
  added there instead of building a second, conflicting anvil-side
  implementation.
- **S14**: `dwarf_keldagrim_furnace`/`_furnace2` categorised as
  `smithing_furnace`. Left alone on purpose: `viking_furnace2` (matches
  `furnace2`'s own no-name/no-op exclusion), `wint_anvil`/`gh_anvil`
  (decorative, no `op1=Smith`), and `brut_anvil` (has `op1=Smith` but must
  stay uncategorised until it gets its own spear/hasta-only handler — see
  S10 below).

**Not done, on purpose:**

| slice | why |
|---|---|
| S7 (elemental metal) | scoped to `quest_elemental_workshop/`, not `skill_smithing/` — the furnace must not gain `category=smithing_furnace` (§7 S7) |
| S10 (barbarian spears/hastae) | blocked on a Barbarian Training miniquest that doesn't exist in this tree; `brut_anvil` stays uncategorised rather than wrongly offering the generic bronze..rune ladder |
| S13 (interface 312) | the `~p_choice5` chain now covers all 31 product kinds (extended through `smithing_type_menu_extra`, `smithing_blurite_menu`, `smithing_gold_menu`) — usable today, if not the real client UI; landing IF 312 is a separable, larger UI project |

---

Primary wiki sources, all read for this plan:

| page | what it settles |
|---|---|
| [Smithing](https://oldschool.runescape.wiki/w/Smithing) | bar chart, gear table, XP-per-bar rule, activity list |
| [Smithing/Level up table](https://oldschool.runescape.wiki/w/Smithing/Level_up_table) | every unlock 1–99, with its quest gate |
| [Template:Smithing/Bronze bar](https://oldschool.runescape.wiki/w/Template:Smithing/Bronze_bar) · [Iron](https://oldschool.runescape.wiki/w/Template:Smithing/Iron_bar) · [Steel](https://oldschool.runescape.wiki/w/Template:Smithing/Steel_bar) · [Mithril](https://oldschool.runescape.wiki/w/Template:Smithing/Mithril_bar) · [Adamantite](https://oldschool.runescape.wiki/w/Template:Smithing/Adamantite_bar) · [Runite](https://oldschool.runescape.wiki/w/Template:Smithing/Runite_bar) · [Gold](https://oldschool.runescape.wiki/w/Template:Smithing/Gold_bar) | the per-bar product tables reproduced in §2 |
| [Anvil](https://oldschool.runescape.wiki/w/Anvil) · [Furnace](https://oldschool.runescape.wiki/w/Furnace) | the loc inventory in §4 |
| [Cannonball](https://oldschool.runescape.wiki/w/Cannonball) · [Ammo mould](https://oldschool.runescape.wiki/w/Ammo_mould) | cannonball tiers, XP, mould behaviour |
| [Blast Furnace](https://oldschool.runescape.wiki/w/Blast_Furnace) · [Giants' Foundry](https://oldschool.runescape.wiki/w/Giants%27_Foundry) | the two minigames |

---

## Why this pass is more than seven queue rows

**1. Clicking an anvil does nothing.** The cache defines `[anvil]` with
`op1=Smith` (`configs/all.loc:17661`), and `dorics_anvil`, `viking_anvil`,
`dwarf_keldagrim_anvil`, `lovakengj_anvil`, `darkm_anvil` and nine more carry
`category=772`. The whole tree defines exactly **one** anvil trigger —
`[oplocu,_anvil]` at
[`smithing.rs2:16`](../OSRS-Content/osrs239-content/server/scripts/skill_smithing/scripts/smithing/smithing.rs2#L16).
There is no `[oploc1,_anvil]`. The only way to smith today is to *use a bar on*
the anvil; the advertised "Smith" option is an inert row. The wiki is explicit
that both work: *"An anvil can be used to smith items by click on an anvil or
using a bar on it"* ([Anvil](https://oldschool.runescape.wiki/w/Anvil)).

**2. The real make-menu is alive client-side and unused server-side.** Interface
**312 `smithing`** is unpacked and fully named — `interfaces/smithing.if`, 39
components: `make_1/5/10/x/all/some` plus product slots `dagger`, `sword`,
`scimitar`, `longsword`, `2h`, `axe`, `mace`, `warhammer`, `battleaxe`,
**`claws`**, `chainbody`, `platelegs`, `plateskirt`, `platebody`, `nails`,
`medhelm`, `fullhelm`, `squareshield`, `kiteshield`, **`darttips`**,
**`arrowheads`**, **`knives`**, **`bolts`**, **`limbs`**, and six `other_*`
slots. `grep -rn "smithing:" server/` returns only `db_getfield` hits — the
interface has **zero** server references. The client already has slots for the
exact products queue #57 calls "deferred".

**3. Queue #59's premise is wrong.** "Members bars (blurite/elemental/lovakite)
— blurite ore mineable but no bar" is false on all three counts:

| bar | cache id | category |
|---|---|---|
| `blurite_bar` | 9467 | already `151` = `smithing_bar` |
| `lovakite_bar` | 13354 | already `151` |
| `elemental_workshop_bar` | 2893 | uncategorised (correct — it is workbench-only) |

Because both are already in `smithing_bar`, `[oplocu,_anvil]` *accepts* them
today and then drops into `switch_obj`'s `case default` → "Nothing interesting
happens." The blocker was a name search, not the data.

**4. Queue #57's "not expressible" items are almost all expressible.** The
barbarian hastae hide under a LostCity-style prefix — `brut_bronze_spear`
(11367) … `brut_rune_spear` (11377) *are* the hastae, not the spears (those are
`bronze_spear` 1237 … `rune_spear` 1247). Full name resolution in §5.

**5. Two live bugs in the code that exists.**

- `cannonballs.rs2:38` awards `stat_advance(smithing, 375)` = 37.5 XP. The wiki
  gives a steel cannonball set **25.6 XP**
  ([Template:Smithing/Steel bar](https://oldschool.runescape.wiki/w/Template:Smithing/Steel_bar)) —
  the code is paying the steel-*bar* rate. Should be `256`.
- `cannonballs.rs2` gates on members + level 35 + mould, but **not on
  [Dwarf Cannon](https://oldschool.runescape.wiki/w/Dwarf_Cannon)** — even
  though `%mcannonquest` / `^mcannon_complete` are fully ported
  (`quests/quest_mcannon/configs/quest_mcannon.constant:18`).

**6. One mis-categorised loc.** `camdozaal_anvil` carries `category=772` but is
named **"Barronite Crusher"** — an
[Imcando hammer](https://oldschool.runescape.wiki/w/Imcando_hammer) /
[barronite](https://oldschool.runescape.wiki/w/Barronite_deposit) machine, not
an anvil. Any future `[oploc1,_anvil]` fires the smithing menu on it. It needs
either an overlay dropping the category or an explicit early-out.

---

## 1. Where Smithing stands today — measured

### 1.1 What is wired

`server/scripts/skill_smithing/` is **534 lines** across 4 scripts plus 2 dbrow
files, 2 dbtables and a loc overlay.

| file | content | notes |
|---|---|---|
| `scripts/smithing/smithing.rs2` (294 L) | `[oplocu,_anvil]`, 6 hand-written per-metal `switch_int` product procs, `~p_choice5` menu chain | 17 product kinds + nails; 6 near-identical 17-case procs — the scaling wall, see §6.1 |
| `scripts/smelting/smelting.rs2` (154 L) | `[oplocu,_smithing_furnace]` + `[oploc2,_smithing_furnace]`, table-driven | iron 50 % fail + ring of forging + goldsmith gauntlets already live |
| `scripts/smelting/cannonballs.rs2` (38 L) | steel cannonballs | wrong XP + missing quest gate (§0.5) |
| `scripts/dragon_sq.rs2` (48 L) | dragon sq shield repair | complete; closes queue #58's headline item |
| `configs/smithing.dbtable` / `.dbrow` | 11 columns, **103 rows** | 6 metals × 17 kinds + steel nails |
| `configs/smelting.dbtable` / `.dbrow` | 11 columns, **8 rows** | bronze, iron, steel, silver, gold, mithril, adamantite, runite |
| `configs/smithing_sources.loc` | `newbiefurnace` → `category=smithing_furnace` | one overlay |

XP is stored in **tenths** and the existing rows are wiki-exact — spot-checked
`bronze_dagger` `125` = 12.5, `bronze_platebody` `625` = 62.5 (5 bars × 12.5),
`mithril_platebody` `2500` = 250, `rune_dagger` `750` = 75. That convention is
correct; keep it.

The XP-per-bar constants the whole table rests on
([Smithing § Smithing tables](https://oldschool.runescape.wiki/w/Smithing#Smithing_tables)):

| bar | XP per bar (tenths) |
|---|---|
| bronze | 12.5 (`125`) |
| blurite | 17.5 (`175`) |
| iron | 25 (`250`) |
| steel | 37.5 (`375`) |
| mithril | 50 (`500`) |
| adamantite | 62.5 (`625`) |
| runite | 75 (`750`) |

> *"Smithing experience is calculated by taking the experience granted from 1
> bar and multiplying it by the number of bars used."* Exceptions: spears and
> hastae (2× the bar rate) and cannonballs (§2.8).

### 1.2 Stat wiring is complete — no work required

| layer | location |
|---|---|
| Stat id | `pack/stat.pack` → `13=smithing` |
| Display name | `general/configs/stat.enum` |
| XP curve | generic `g_xp_table`, `src/net/mock/mock230_combat.c:527` |
| Level-up trigger | `levelup/scripts/levelup.rs2` → `[advancestat,smithing]` |
| Skill guide | `interface_skill_guide/configs/skill_guide.constant` |
| XP drops | `interface_chrome/configs/xpdrops.varp` |
| Cape item | `skillcape_smithing` (9795) / `_trimmed` / `_hood` |

### 1.3 Adjacent smithing content already done elsewhere in the tree

Do not re-port any of this.

| thing | where | wiki |
|---|---|---|
| Blast Furnace: enter/fee, pump/pedals/coke/stove/belt/dispenser, breakage | SCAPE2009 queue §13 / 13b / 13c — **done** | [Blast Furnace](https://oldschool.runescape.wiki/w/Blast_Furnace) |
| Superheat Item (incl. gold-bar gauntlet bonus) | `skill_magic/scripts/spells/superheat.rs2` | [Superheat Item](https://oldschool.runescape.wiki/w/Superheat_Item) |
| Ring of forging charge burn | `~lose_charge_ring_of_forging`, called from `smelt_ore_loop` | [Ring of forging](https://oldschool.runescape.wiki/w/Ring_of_forging) |
| Goldsmith gauntlets 22.5→56.2 gold XP | `smelting.rs2:146` (`scale(5,2,$xp)`) + `superheat.rs2:47` | [Goldsmith gauntlets](https://oldschool.runescape.wiki/w/Goldsmith_gauntlets) |
| Family Crest gauntlet award | `quests/quest_crest/scripts/crest_avan.rs2:137` | [Family Crest](https://oldschool.runescape.wiki/w/Family_Crest) |
| Gold helmet at the Keldagrim anvil | `quest_betweenarock/scripts/betweenarock_schematics.rs2:126` | [Between a Rock...](https://oldschool.runescape.wiki/w/Between_a_Rock...) |
| Dragon forge anvil | `quest_dragonslayer2/scripts/dragonslayer2.rs2:1300` | [Dragon forge](https://oldschool.runescape.wiki/w/Dragon_forge) |
| Dragon sq shield repair | `skill_smithing/scripts/dragon_sq.rs2` | [Dragon sq shield](https://oldschool.runescape.wiki/w/Dragon_sq_shield) |
| One Small Favour vane repair | `~osf_repair_vane_part`, dispatched from `[oplocu,_anvil]` | [One Small Favour](https://oldschool.runescape.wiki/w/One_Small_Favour) |
| Doric's anvil quest gate | `smithing.rs2:17` | [Doric's Quest](https://oldschool.runescape.wiki/w/Doric%27s_Quest) |
| Silver / gold jewellery furnace redirects | `~craft_silver`, `@craft_gold_menu` (`skill_crafting/jewellery`) | [Crafting](https://oldschool.runescape.wiki/w/Crafting#Silver_jewellery) |
| Glass (`bucket_sand` + `soda_ash`) | `@smelt_glass` (`skill_crafting/glass`) | — |
| Dwarf Cannon quest (11 stages) | `quests/quest_mcannon/` | [Dwarf Cannon](https://oldschool.runescape.wiki/w/Dwarf_Cannon) |
| Elemental Workshop I / II | `quest_elemental_workshop/`, `quest_elementalworkshopii/` | [Elemental Workshop I](https://oldschool.runescape.wiki/w/Elemental_Workshop_I) |

### 1.4 What has no coverage at all

Every members anvil product (13 product kinds × up to 6 metals), every members
bar, the anvil's own "Smith" click, the smithing interface, and all smithing
gear perks except the two gold-bar ones.

---

## 2. The complete wiki product inventory

This is the list the plan is measured against. **Status** is against this tree
today: `live` = works now, `gap` = in scope below, `n/a` = no cache object at
this revision, `lane` = deliberately another lane (§9).

### 2.1 Smelting — bars ([Smithing § Bar chart](https://oldschool.runescape.wiki/w/Smithing#Bar_chart))

| Lvl | Bar | Ore | XP | Facility | Cache name | Status |
|---|---|---|---|---|---|---|
| 1 | [Bronze bar](https://oldschool.runescape.wiki/w/Bronze_bar) | copper ×1 + tin ×1 | 6.2 | furnace | `bronze_bar` | live |
| 13 | [Blurite bar](https://oldschool.runescape.wiki/w/Blurite_bar) | blurite ×1 | 8 | furnace | `blurite_bar` (9467) | **gap** S6 |
| 15 | [Iron bar](https://oldschool.runescape.wiki/w/Iron_bar) | iron ×1 | 12.5 | furnace, 50 % fail | `iron_bar` | live |
| 20 | [Silver bar](https://oldschool.runescape.wiki/w/Silver_bar) | silver ×1 | 13.7 | furnace | `silver_bar` | live |
| 20 | [Elemental metal](https://oldschool.runescape.wiki/w/Elemental_metal) | elemental ×1 + coal ×4 | 7.5 | **Elemental Workshop furnace only** | `elemental_workshop_bar` (2893) | **gap** S7 |
| 25 | [Lead bar](https://oldschool.runescape.wiki/w/Lead_bar) | lead ×2 | 15.5 | furnace | `lead_bar` | lane (Sailing) |
| 30 | [Steel bar](https://oldschool.runescape.wiki/w/Steel_bar) | iron ×1 + coal ×2 | 17.5 | furnace | `steel_bar` | live |
| 40 | [Gold bar](https://oldschool.runescape.wiki/w/Gold_bar) | gold ×1 | 22.5 / 56.2 | furnace | `gold_bar` | live |
| 45 | [Lovakite bar](https://oldschool.runescape.wiki/w/Lovakite_bar) | lovakite ×1 + coal ×2 | 20 | **Lovakite furnace only** | `lovakite_bar` (13354) | lane (Kourend) |
| 50 | [Mithril bar](https://oldschool.runescape.wiki/w/Mithril_bar) | mithril ×1 + coal ×4 | 30 | furnace | `mithril_bar` | live |
| 70 | [Adamantite bar](https://oldschool.runescape.wiki/w/Adamantite_bar) | adamantite ×1 + coal ×6 | 37.5 | furnace | `adamantite_bar` | live |
| 74 | [Cupronickel bar](https://oldschool.runescape.wiki/w/Cupronickel_bar) | nickel ×1 + copper ×2 | 42 | furnace | `cupronickel_bar` | lane (Sailing) |
| 85 | [Runite bar](https://oldschool.runescape.wiki/w/Runite_bar) | runite ×1 + coal ×8 | 50 | furnace | `runite_bar` | live |
| — | ['Perfect' gold bar](https://oldschool.runescape.wiki/w/%27Perfect%27_gold_bar) | Family Crest reward | — | — | `perfect_gold_bar` | live (crafting redirect) |

Wiki note worth preserving verbatim: the in-game guide claims blurite is level 8,
but *"attempting to do so with less than 13 Smithing will prompt the player that
the level requirement is 13"*. Smelt = **13**; the anvil products are 8 and 13.

### 2.2 Anvil products — bronze (12.5 XP/bar)

| Lvl | Product | Bars | Qty | XP | M | Gate | Cache name | Status |
|---|---|---|---|---|---|---|---|---|
| 1 | [Bronze dagger](https://oldschool.runescape.wiki/w/Bronze_dagger) | 1 | 1 | 12.5 | | | `bronze_dagger` | live |
| 1 | [Bronze axe](https://oldschool.runescape.wiki/w/Bronze_axe) | 1 | 1 | 12.5 | | | `bronze_axe` | live |
| 2 | [Bronze mace](https://oldschool.runescape.wiki/w/Bronze_mace) | 1 | 1 | 12.5 | | | `bronze_mace` | live |
| 3 | [Bronze med helm](https://oldschool.runescape.wiki/w/Bronze_med_helm) | 1 | 1 | 12.5 | | | `bronze_med_helm` | live |
| 3 | [Bronze bolts (unf)](https://oldschool.runescape.wiki/w/Bronze_bolts_(unf)) | 1 | 10 | 12.5 | ✓ | | `xbows_crossbow_bolts_bronze_unfeathered` | **gap** S3 |
| 4 | [Bronze sword](https://oldschool.runescape.wiki/w/Bronze_sword) | 1 | 1 | 12.5 | | | `bronze_sword` | live |
| 4 | [Bronze dart tip](https://oldschool.runescape.wiki/w/Bronze_dart_tip) | 1 | 10 | 12.5 | ✓ | [The Tourist Trap](https://oldschool.runescape.wiki/w/The_Tourist_Trap) | `bronze_dart_tip` | **gap** S3 |
| 4 | [Bronze wire](https://oldschool.runescape.wiki/w/Bronze_wire) | 1 | 1 | 12.5 | ✓ | | `bronzecraftwire` (1794) | **gap** S4 |
| 4 | [Bronze nails](https://oldschool.runescape.wiki/w/Bronze_nails) | 1 | 15 | 12.5 | ✓ | | `nails_bronze` (4819) | **gap** S3 |
| 5 | [Bronze scimitar](https://oldschool.runescape.wiki/w/Bronze_scimitar) | 2 | 1 | 25 | | | `bronze_scimitar` | live |
| 5 | [Bronze spear](https://oldschool.runescape.wiki/w/Bronze_spear) | 1 + logs | 1 | 25 | ✓ | [Barbarian Smithing](https://oldschool.runescape.wiki/w/Barbarian_Training#Barbarian_Smithing) | `bronze_spear` (1237) | lane S10 |
| 5 | [Bronze hasta](https://oldschool.runescape.wiki/w/Bronze_hasta) | 1 + logs | 1 | 25 | ✓ | Barbarian Smithing | `brut_bronze_spear` (11367) | lane S10 |
| 5 | [Bronze arrowtips](https://oldschool.runescape.wiki/w/Bronze_arrowtips) | 1 | 15 | 12.5 | ✓ | | `bronze_arrowheads` (39) | **gap** S3 |
| 5 | [Bronze cannonball](https://oldschool.runescape.wiki/w/Bronze_cannonball) | 1 | 4 | 9 | ✓ | Dwarf Cannon | — | n/a (2025 Sailing) |
| 6 | [Bronze limbs](https://oldschool.runescape.wiki/w/Bronze_limbs) | 1 | 1 | 12.5 | ✓ | | `xbows_crossbow_limbs_bronze` (9420) | **gap** S3 |
| 6 | [Bronze longsword](https://oldschool.runescape.wiki/w/Bronze_longsword) | 2 | 1 | 25 | | | `bronze_longsword` | live |
| 6 | [Bronze javelin heads](https://oldschool.runescape.wiki/w/Bronze_javelin_heads) | 1 | 5 | 12.5 | ✓ | | `bronze_javelin_head` (19570) | **gap** S3 |
| 7 | [Bronze full helm](https://oldschool.runescape.wiki/w/Bronze_full_helm) | 2 | 1 | 25 | | | `bronze_full_helm` | live |
| 7 | [Bronze knife](https://oldschool.runescape.wiki/w/Bronze_knife) | 1 | 5 | 12.5 | ✓ | | `bronze_knife` (864) | **gap** S3 |
| 8 | [Bronze sq shield](https://oldschool.runescape.wiki/w/Bronze_sq_shield) | 2 | 1 | 25 | | | `bronze_sq_shield` | live |
| 9 | [Bronze warhammer](https://oldschool.runescape.wiki/w/Bronze_warhammer) | 3 | 1 | 37.5 | | | `bronze_warhammer` | live |
| 10 | [Bronze battleaxe](https://oldschool.runescape.wiki/w/Bronze_battleaxe) | 3 | 1 | 37.5 | | | `bronze_battleaxe` | live |
| 10 | Bronze keel parts | 5 | 1 | 62.5 | ✓ | Pandemonium | `sailing_boat_keel_part_bronze` | lane (Sailing) |
| 11 | [Bronze chainbody](https://oldschool.runescape.wiki/w/Bronze_chainbody) | 3 | 1 | 37.5 | | | `bronze_chainbody` | live |
| 12 | [Bronze kiteshield](https://oldschool.runescape.wiki/w/Bronze_kiteshield) | 3 | 1 | 37.5 | | | `bronze_kiteshield` | live |
| 13 | [Bronze claws](https://oldschool.runescape.wiki/w/Bronze_claws) | 2 | 1 | 25 | ✓ | [Death Plateau](https://oldschool.runescape.wiki/w/Death_Plateau) | `bronze_claws` (3095) | **gap** S5 |
| 14 | [Bronze 2h sword](https://oldschool.runescape.wiki/w/Bronze_2h_sword) | 3 | 1 | 37.5 | | | `bronze_2h_sword` | live |
| 16 | [Bronze platelegs](https://oldschool.runescape.wiki/w/Bronze_platelegs) | 3 | 1 | 37.5 | | | `bronze_platelegs` | live |
| 16 | [Bronze plateskirt](https://oldschool.runescape.wiki/w/Bronze_plateskirt) | 3 | 1 | 37.5 | | | `bronze_plateskirt` | live |
| 18 | [Bronze platebody](https://oldschool.runescape.wiki/w/Bronze_platebody) | 5 | 1 | 62.5 | | | `bronze_platebody` | live |

### 2.3 Anvil products — iron (25 XP/bar)

Same 17 F2P kinds at 15/16/17/18/19/20/21/22/23/24/25/26/27/29/31/31/33 — **all
live**. The members additions:

| Lvl | Product | Bars | Qty | XP | Gate | Cache name | Status |
|---|---|---|---|---|---|---|---|
| 17 | [Iron spit](https://oldschool.runescape.wiki/w/Iron_spit) | 1 | 1 | 25 | | `spit_iron` (7225) | **gap** S4 |
| 18 | [Iron bolts (unf)](https://oldschool.runescape.wiki/w/Iron_bolts_(unf)) | 1 | 10 | 25 | | `xbows_crossbow_bolts_iron_unfeathered` | **gap** S3 |
| 19 | [Iron dart tip](https://oldschool.runescape.wiki/w/Iron_dart_tip) | 1 | 10 | 25 | Tourist Trap | `iron_dart_tip` | **gap** S3 |
| 19 | [Iron nails](https://oldschool.runescape.wiki/w/Iron_nails) | 1 | 15 | 25 | | `nails_iron` (4820) | **gap** S3 |
| 20 | [Iron spear](https://oldschool.runescape.wiki/w/Iron_spear) / [hasta](https://oldschool.runescape.wiki/w/Iron_hasta) | 1 + oak logs | 1 | 50 | Barbarian Smithing | `iron_spear` / `brut_iron_spear` | lane S10 |
| 20 | [Iron arrowtips](https://oldschool.runescape.wiki/w/Iron_arrowtips) | 1 | 15 | 25 | | `iron_arrowheads` (40) | **gap** S3 |
| 20 | [Iron cannonball](https://oldschool.runescape.wiki/w/Iron_cannonball) | 1 | 4 | 17 | Dwarf Cannon | — | n/a |
| 21 | [Iron javelin heads](https://oldschool.runescape.wiki/w/Iron_javelin_heads) | 1 | 5 | 25 | | `iron_javelin_head` | **gap** S3 |
| 22 | [Iron knife](https://oldschool.runescape.wiki/w/Iron_knife) | 1 | 5 | 25 | | `iron_knife` (863) | **gap** S3 |
| 23 | [Iron limbs](https://oldschool.runescape.wiki/w/Iron_limbs) | 1 | 1 | 25 | | `xbows_crossbow_limbs_iron` | **gap** S3 |
| 26 | [Oil lantern frame](https://oldschool.runescape.wiki/w/Oil_lantern_frame) | 1 | 1 | 25 | | `oil_lantern_frame` (4540) | **gap** S4 |
| 28 | [Iron claws](https://oldschool.runescape.wiki/w/Iron_claws) | 2 | 1 | 50 | Death Plateau | `iron_claws` (3096) | **gap** S5 |
| 45 | [Iron sheet](https://oldschool.runescape.wiki/w/Iron_sheet) | 1 | 1 | 20 | [Swan Song](https://oldschool.runescape.wiki/w/Swan_Song), **metal press** | `iron_sheet` (7941) | lane (quest) |

### 2.4 Anvil products — steel (37.5 XP/bar)

F2P kinds at 30–48 all **live**, plus `nails` (steel, 15/bar, level 34) already
wired as product kind 18. Members additions:

| Lvl | Product | Bars | Qty | XP | Gate | Cache name | Status |
|---|---|---|---|---|---|---|---|
| 33 | [Steel bolts (unf)](https://oldschool.runescape.wiki/w/Steel_bolts_(unf)) | 1 | 10 | 37.5 | | `xbows_crossbow_bolts_steel_unfeathered` | **gap** S3 |
| 34 | [Steel dart tip](https://oldschool.runescape.wiki/w/Steel_dart_tip) | 1 | 10 | 37.5 | Tourist Trap | `steel_dart_tip` | **gap** S3 |
| 34 | Chain | 1 | 1 | 37.5 | Sailing | `chain` (32886) | lane |
| 35 | [Steel spear](https://oldschool.runescape.wiki/w/Steel_spear) / [hasta](https://oldschool.runescape.wiki/w/Steel_hasta) | 1 + willow | 1 | 75 | Barbarian Smithing | `steel_spear` / `brut_steel_spear` | lane S10 |
| 35 | [Steel arrowtips](https://oldschool.runescape.wiki/w/Steel_arrowtips) | 1 | 15 | 37.5 | | `steel_arrowheads` (41) | **gap** S3 |
| 35 | [Steel cannonball](https://oldschool.runescape.wiki/w/Steel_cannonball) | 1 | 4 | **25.6** | [Dwarf Cannon](https://oldschool.runescape.wiki/w/Dwarf_Cannon) | `mcannonball` (2) | **buggy** S8 |
| 36 | [Steel limbs](https://oldschool.runescape.wiki/w/Steel_limbs) | 1 | 1 | 37.5 | | `xbows_crossbow_limbs_steel` | **gap** S3 |
| 36 | [Steel javelin heads](https://oldschool.runescape.wiki/w/Steel_javelin_heads) | 1 | 5 | 37.5 | | `steel_javelin_head` | **gap** S3 |
| 36 | [Steel studs](https://oldschool.runescape.wiki/w/Steel_studs) | 1 | 1 | 37.5 | | `studs` (2370) | **gap** S4 |
| 37 | [Steel knife](https://oldschool.runescape.wiki/w/Steel_knife) | 1 | 5 | 37.5 | | `steel_knife` (865) | **gap** S3 |
| 43 | [Steel claws](https://oldschool.runescape.wiki/w/Steel_claws) | 2 | 1 | 75 | Death Plateau | `steel_claws` (3097) | **gap** S5 |
| 49 | [Bullseye lantern (unf)](https://oldschool.runescape.wiki/w/Bullseye_lantern_(unf)) | 1 | 1 | 37.5 | | `bullseye_lantern_nolens` (4544) | **gap** S4 |

### 2.5 Anvil products — mithril (50 XP/bar)

F2P kinds at 50–68 **live**. Members additions: bolts (unf) 53, nails 54, dart
tip 54, [cannonball](https://oldschool.runescape.wiki/w/Mithril_cannonball) 55
(n/a), spear/hasta 55 (lane), arrowtips 55, limbs 56, javelin heads 56, knife 57,
**[mith grapple tip](https://oldschool.runescape.wiki/w/Mith_grapple) 59**
(`xbows_grapple_tip_mithril`, 9416 — the Smithing half of a chain finished in
Fletching), claws 63.

### 2.6 Anvil products — adamant (62.5 XP/bar) and rune (75 XP/bar)

F2P kinds at 70–88 / 85–99 **live**. Members additions mirror mithril exactly:
bolts (unf) 73/88, nails 74/89, dart tip 74/89, cannonball 75/90 (n/a),
spear/hasta 75/90 (lane), arrowtips 75/90, limbs 76/91, javelin heads 76/91,
knife 77/92, claws 83/98. Rune adds
[Dragon nails](https://oldschool.runescape.wiki/w/Dragon_nails) at 92 (Dragon
forge, lane).

### 2.7 Gold anvil products ([Template:Smithing/Gold bar](https://oldschool.runescape.wiki/w/Template:Smithing/Gold_bar))

| Lvl | Product | Bars | XP | Gate | Status |
|---|---|---|---|---|---|
| 50 | [Gold helmet](https://oldschool.runescape.wiki/w/Gold_helmet) | 3 | 30 | [Between a Rock...](https://oldschool.runescape.wiki/w/Between_a_Rock...) | live (`betweenarock_schematics.rs2`) |
| 50 | [Gold bowl](https://oldschool.runescape.wiki/w/Gold_bowl) | 2 | 30 | [Legends' Quest](https://oldschool.runescape.wiki/w/Legends%27_Quest) | **gap** S11 (`goldbowl_empty`, 721) |

The current code sends both `gold_bar` and `silver_bar` to
`~chatplayer("Perhaps I should use this in a furnace instead.")` — correct for
the general case, wrong once the two quest products exist.

### 2.8 Blurite anvil products ([Blurite bar](https://oldschool.runescape.wiki/w/Blurite_bar))

| Lvl | Product | Bars | Qty | XP | Gate | Cache name |
|---|---|---|---|---|---|---|
| 8 | [Blurite bolts (unf)](https://oldschool.runescape.wiki/w/Blurite_bolts_(unf)) | 1 | 10 | 17.5 | [The Knight's Sword](https://oldschool.runescape.wiki/w/The_Knight%27s_Sword) | `xbows_crossbow_bolts_blurite_unfeathered` (9376) |
| 13 | [Blurite limbs](https://oldschool.runescape.wiki/w/Blurite_limbs) | 1 | 1 | 17.5 | The Knight's Sword | `xbows_crossbow_limbs_blurite` (9422) |

Doric's anvil is the diary target for blurite limbs
([Falador Easy Diary](https://oldschool.runescape.wiki/w/Falador_Diary#Easy)).

### 2.9 Cannonballs ([Cannonball](https://oldschool.runescape.wiki/w/Cannonball))

| Lvl | Ball | Bar | Qty | XP | Cache name | Status |
|---|---|---|---|---|---|---|
| 5 | Bronze | bronze | 4 | 9 | — | n/a |
| 20 | Iron | iron | 4 | 17 | — | n/a |
| **35** | **Steel** | steel | **4** | **25.6** | `mcannonball` (2) | **buggy** |
| 50 | Granite | granite dust | 4 | — | — | n/a |
| 55 | Mithril | mithril | 4 | 34 | — | n/a |
| 75 | Adamant | adamantite | 4 | 42.4 | — | n/a |
| 90 | Rune | runite | 4 | 50.5 | — | n/a |

Only steel is in-era; the rest arrived with Sailing.
[Ammo mould](https://oldschool.runescape.wiki/w/Ammo_mould) `ammo_mould` (4) is
live; [Double ammo mould](https://oldschool.runescape.wiki/w/Double_ammo_mould)
`double_ammo_mould` (27012) is a Giants' Foundry reward — lane.

### 2.10 Assembled / special products at a regular anvil

| Product | Lvl | Inputs | Cache name | Status |
|---|---|---|---|---|
| [Dragon sq shield](https://oldschool.runescape.wiki/w/Dragon_sq_shield) | 60 | shield halves + hammer | `dragon_sq_shield` | **live** |
| [Godsword blade](https://oldschool.runescape.wiki/w/Godsword_blade) | 80 | 3 godsword shards | `godwars_godsword_blade1+2+3` (11798) | **gap** S12 |
| [Dragonfire shield](https://oldschool.runescape.wiki/w/Dragonfire_shield) | 90 | draconic visage + anti-dragon shield | `dragonfire_shield_uncharged` (11284) | **gap** S12 |
| [Ancient wyvern shield](https://oldschool.runescape.wiki/w/Ancient_wyvern_shield) | 66 (+66 Magic) | wyvern visage + elemental shield | `wyvern_visage` (21637) | lane |
| [Dragonfire ward](https://oldschool.runescape.wiki/w/Dragonfire_ward) | 90 | skeletal visage + ancient wyvern shield | `skeletal_visage` (22006) | lane |
| [Dragon platebody](https://oldschool.runescape.wiki/w/Dragon_platebody) / [kiteshield](https://oldschool.runescape.wiki/w/Dragon_kiteshield) | 90 / 75 | metal lump/slice/shard at the **Dragon forge** | `dragon_platebody` (21892) | lane (DS2, partly wired) |
| Torva restoration | 90 | Bandosian components | `broken_torva_*` (26376–26380) | lane |
| Spirit sigil → blessed spirit shield | 85 (+90 Prayer) | sigil + shield | `arcane_sigil` etc. | lane |

### 2.11 Elemental Workshop products (workbench, not anvil)

| Product | Lvl | XP | Facility | Cache name |
|---|---|---|---|---|
| [Elemental shield](https://oldschool.runescape.wiki/w/Elemental_shield) | 20 | 20 | `elemental_workshop_workbench` + battered book | `elemental_shield` (2890) |
| [Elemental helmet](https://oldschool.runescape.wiki/w/Elemental_helmet) | 20 | 20 | workbench + beaten book | — (check `elem_*`) |
| [Mind helmet](https://oldschool.runescape.wiki/w/Mind_helmet) | 30 | 30 | workbench, primed mind bar | `elem_mind_helm` (9733) |
| [Mind shield](https://oldschool.runescape.wiki/w/Mind_shield) | 30 | 30 | workbench, primed mind bar | `elemental_mind_shield` (9731) |

`elem_primed_bar` (9727) and `elem_mind_bar` (9728) both exist. This belongs in
the `quest_elementalworkshopii/` lane, not `skill_smithing/` — noted here so the
Smithing audit is complete.

---

## 3. Loc inventory

### 3.1 Anvils ([Anvil](https://oldschool.runescape.wiki/w/Anvil))

Cache locs already carrying `category=772`:

`anvil` (2097) · `dorics_anvil` (2031) · `viking_anvil` (4306, Rellekka) ·
`dwarf_keldagrim_anvil` (6150) · `dorgesh_blacksmith_anvil` (22725) ·
`lovakengj_anvil` (28563) · `lovakengj_anvil_noop` · `ds2_guild_blacksmith_anvil`
(31623, Myths' Guild) · `darkm_anvil` (39242) · `lumbridge_anvil` (39620,
**Rusted anvil — bronze only**) · `sw_anvil` · `camdozaal_anvil`
(**mis-categorised**, §0.6) · `pvpa_fake_anvil` · `sangvesti_anvil` ·
`cam_torum_anvil`

Cache anvils **missing** the category — each is a dead "Smith" click today:

| loc | id | wiki | needed by |
|---|---|---|---|
| `experimental_anvil` | 2672 | [The Tourist Trap](https://oldschool.runescape.wiki/w/The_Tourist_Trap) | dart-tip prototype (quest lane) |
| `brut_anvil` | 25349 | [Barbarian anvil](https://oldschool.runescape.wiki/w/Barbarian_anvil) | spears/hastae (S10) |
| `ds2_ac_forge_anvil` / `_unlit` | 32215/6 | [Dragon forge](https://oldschool.runescape.wiki/w/Dragon_forge) | already has its own `[oplocu]` |
| `wint_anvil` | 29310 | Wintertodt | none |
| `gh_anvil` | 39724 | — | audit |
| `raids_tekton_anvil` | 29867 | CoX | none (Tekton prop) |

### 3.2 Furnaces ([Furnace](https://oldschool.runescape.wiki/w/Furnace))

Already `category=215`: `furnace` (2030) · `furnace3` · `bcs_furnace` (+`_lit`) ·
`furnace_upass` · `viking_furnace` · `tzhaar_forge` (Lava forge) · `fairy_furnace`
· `swan_furnace` · `varrock_diary_furnace` · `ahoy_new_furnace` · `iznot_clay_forge`
· `fai_falador_furnace` · `wilderness_resource_furnace` · `lovakengj_furnace_01` ·
`zqfurnace` · `brimstone_furnace` · `zalcano_furnace` · `prif_furnace` ·
`darkm_furnace` · `furnace_lassar01_*` · `cam_torum_furnace` /
`cam_torum_sacred_forge` · `barracuda_furnace` · `grimstone_furnace` ·
`deepfin_furnace`

Plus the tree's own overlay: `newbiefurnace` → `smithing_furnace`.

Missing the category — **dead Smelt clicks**:

| loc | id | note |
|---|---|---|
| `furnace2` | 2099 | no name and no ops in rank 0; correctly skipped, documented in `smithing_sources.loc` |
| `viking_furnace2` | 4305 | Rellekka second furnace |
| `dwarf_keldagrim_furnace` / `_furnace2` | 6189/6190 | Keldagrim |
| `elemental_workshop_furnace` | 3410 | **must stay uncategorised** — elemental metal only (§2.11) |
| `regicide_furnace` | 3994 | quest prop |
| `furnace_legendsquest` | 2966 | gold bowl (S11) |
| `plaguesheep_furnace` | 165 | quest prop |

### 3.3 Special facilities

| facility | wiki | cache | status |
|---|---|---|---|
| [Blast Furnace](https://oldschool.runescape.wiki/w/Blast_Furnace) | half coal, all ore at once | `blast_furnace_*` (9085–9103…) | done, SCAPE2009 §13/13b/13c |
| [Lovakite furnace](https://oldschool.runescape.wiki/w/Lovakite_furnace) | only place lovakite smelts | `lovakengj_furnace_01` | Kourend lane |
| [Barbarian anvil](https://oldschool.runescape.wiki/w/Barbarian_anvil) | spears + hastae only | `brut_anvil` | S10 |
| [Rusted anvil](https://oldschool.runescape.wiki/w/Rusted_anvil) (Lumbridge) | bronze only | `lumbridge_anvil` | S2 |
| [Dragon forge](https://oldschool.runescape.wiki/w/Dragon_forge) | dragon plate/kite, dragon nails | `ds2_ac_forge_anvil` | DS2 lane |
| Metal press (Piscatoris) | iron sheet | — | Swan Song lane |
| [Giants' Foundry](https://oldschool.runescape.wiki/w/Giants%27_Foundry) | 2022 | — | KRONOS lane |

---

## 4. Tools and gear ([Smithing § Smithing equipment and tools](https://oldschool.runescape.wiki/w/Smithing#Smithing_equipment_and_tools))

| Item | Effect | Cache name | Status |
|---|---|---|---|
| [Hammer](https://oldschool.runescape.wiki/w/Hammer) | required at every anvil | `hammer` (2347) | live |
| [Imcando hammer](https://oldschool.runescape.wiki/w/Imcando_hammer) | wieldable hammer | `imcando_hammer` (25644) | **gap** S9 — `inv_total(inv, hammer)` misses it entirely |
| [Goldsmith gauntlets](https://oldschool.runescape.wiki/w/Goldsmith_gauntlets) | gold smelt 22.5 → 56.2 | `gauntlets_of_goldsmithing` (776) | live |
| [Ring of forging](https://oldschool.runescape.wiki/w/Ring_of_forging) | 100 % iron, 140 charges | `ring_of_forging` (2568) | live |
| [Smithing cape](https://oldschool.runescape.wiki/w/Smithing_cape) | goldsmith effect + coal bag 27→36 | `skillcape_smithing` (9795) | **gap** S9 |
| [Smiths' Uniform](https://oldschool.runescape.wiki/w/Smiths%27_Uniform) | 20 %/piece chance of 5→4 tick anvil actions | `smithing_uniform_torso/legs/boots/gloves` (27023–27029) | lane (2022) |
| [Smiths gloves (i)](https://oldschool.runescape.wiki/w/Smiths_gloves_(i)) | uniform + ice gloves | `smithing_uniform_gloves_ice` (27031) | lane |
| [Ice gloves](https://oldschool.runescape.wiki/w/Ice_gloves) | BF / Foundry bar handling | `ice_gloves` (1580) | BF lane |
| [Varrock armour](https://oldschool.runescape.wiki/w/Varrock_armour) 1–4 | 10 % double bar **at the Edgeville furnace only**; tiers cap at steel/mith/adam/all | `varrock_armour_easy…elite` (13104–13107) | **gap** S9 |
| [Coal bag](https://oldschool.runescape.wiki/w/Coal_bag) | 27 coal (36 with cape), consumed after inventory coal | `coal_bag` (12019) | deferred — see [`MINING_COMPLETION_PLAN.md`](MINING_COMPLETION_PLAN.md) §6 |
| [Smithing catalyst](https://oldschool.runescape.wiki/w/Smithing_catalyst) | 2× XP, ½ coal | `smithing_catalyst` (27017) | lane (2022) |
| [Ammo mould](https://oldschool.runescape.wiki/w/Ammo_mould) | 4 balls/bar | `ammo_mould` (4) | live |
| [Double ammo mould](https://oldschool.runescape.wiki/w/Double_ammo_mould) | 8 balls / 2 bars | `double_ammo_mould` (27012) | lane |
| [Blacksmith's helm](https://oldschool.runescape.wiki/w/Blacksmith%27s_helm) | **no smithing effect** | `blacksmith_helm` (19988) | n/a by design |

---

## 5. Quest and miniquest interactions

| Quest / miniquest | Smithing role | ported? |
|---|---|---|
| [Doric's Quest](https://oldschool.runescape.wiki/w/Doric%27s_Quest) | unlocks `dorics_anvil` | ✓ gate live |
| [The Knight's Sword](https://oldschool.runescape.wiki/w/The_Knight%27s_Sword) | unlocks blurite smelting + blurite anvil products | ✗ **no `quest_knightsword/`** — see S6 |
| [Dwarf Cannon](https://oldschool.runescape.wiki/w/Dwarf_Cannon) | unlocks cannonballs, awards the ammo mould | ✓ quest live, gate missing (S8) |
| [The Tourist Trap](https://oldschool.runescape.wiki/w/The_Tourist_Trap) | unlocks dart tips; `experimental_anvil` | ✗ no quest dir |
| [Death Plateau](https://oldschool.runescape.wiki/w/Death_Plateau) | unlocks claws | ✗ no quest dir — S5 needs a gate decision |
| [Barbarian Training](https://oldschool.runescape.wiki/w/Barbarian_Training#Barbarian_Smithing) | unlocks spears + hastae at `brut_anvil` | ✗ no miniquest dir |
| [Family Crest](https://oldschool.runescape.wiki/w/Family_Crest) | goldsmith gauntlets | ✓ `quest_crest` |
| [Between a Rock...](https://oldschool.runescape.wiki/w/Between_a_Rock...) | gold helmet, golden cannonball | ✓ `quest_betweenarock` |
| [Legends' Quest](https://oldschool.runescape.wiki/w/Legends%27_Quest) | gold bowl | ✓ dir exists, product missing (S11) |
| [One Small Favour](https://oldschool.runescape.wiki/w/One_Small_Favour) | anvil vane repair | ✓ dispatched from `[oplocu,_anvil]` |
| [Elemental Workshop I](https://oldschool.runescape.wiki/w/Elemental_Workshop_I) / [II](https://oldschool.runescape.wiki/w/Elemental_Workshop_II) | elemental metal + workbench products | ✓ dirs exist, §2.11 products missing |
| [Zogre Flesh Eaters](https://oldschool.runescape.wiki/w/Zogre_Flesh_Eaters) | needs 4 Smithing (nails for brutal arrows) | ✓ `quest_zogreflesheaters` |
| [Dragon Slayer II](https://oldschool.runescape.wiki/w/Dragon_Slayer_II) | dragon forge, dragon kite/plate | ✓ partly (`[oplocu,ds2_ac_forge_anvil]`) |
| [Swan Song](https://oldschool.runescape.wiki/w/Swan_Song) | iron sheets at the metal press | ✓ dir exists, product missing |
| [Scrambled!](https://oldschool.runescape.wiki/w/Scrambled!) · [Cabin Fever](https://oldschool.runescape.wiki/w/Cabin_Fever) · [Devious Minds](https://oldschool.runescape.wiki/w/Devious_Minds) | consume nails / smithed parts | dirs exist |

---

## 6. The blocker: six parallel 17-case switches

### 6.1 What is wrong

`smithing.rs2` holds six procs — `~smithing_bronze_product` …
`~smithing_rune_product` — each a 17-case `switch_int` mapping an integer
"product kind" to a `namedobj`, plus `~smithing_product_for` dispatching on the
bar. 132 lines of the 294 are that mapping, and it already carries a live typo
(`adamnt_warhammer`, matching a cache typo). Adding the 13 members kinds means
**six more switches growing to 30 cases each** — ~250 more lines of the same
shape — and the special metals (blurite, gold) do not fit the shape at all.

`smithing.dbrow` **already carries the whole mapping**: `ifproduct` is indexed,
and `bar`/`bar_amount`/`levelrequired`/`experience`/`product_amount` are all
columns. The switches exist only because the menu speaks *kind* and the table is
keyed by *product*.

### 6.2 The fix — one column, then delete the switches

Add two columns to `smithing.dbtable`:

```
column=kind,int,INDEXED        // 1 dagger … 17 kiteshield, 18 nails, 19… members kinds
column=menu_order,int          // sort key within a bar, for the IF grid
```

Then `~smithing_product_for($bar, $kind)` becomes a two-key `db_find`, and all
six procs plus `~smithing_product_for` are deleted (−140 lines). Every new
product afterwards is a dbrow, not a script edit — which is what makes S3–S7
cheap instead of quadratic.

**Do this first.** S3 onwards assume it.

### 6.3 The second wall: the menu

`~p_choice5` chains cannot express 30 kinds. Five nested "More options…" prompts
is already the ceiling and it is ugly at 18. Interface **312** (§0.2) is the
answer and it is already in the cache with the right slot names. S13 lands it;
until then S3–S7 extend the `p_choice` chain and accept the ugliness, because a
working ugly menu beats no product.

---

## 7. The slices

### S1 — `[oploc1,_anvil]`: make the advertised click work
Closes the tree's most visible smithing bug. Add

```
[oploc1,_anvil]
@smithing_anvil_menu(null);      // null bar = "pick a bar you are carrying"
```

with the Doric gate hoisted out of `[oplocu,_anvil]` into a shared
`[proc,anvil_gate_ok]` so both entry points honour it. With no bar the menu must
offer the bars actually in the inventory, highest tier first (this is what the
real interface does). Also add the `camdozaal_anvil` early-out (§0.6) and decide
`lumbridge_anvil`'s bronze-only restriction here
([Rusted anvil](https://oldschool.runescape.wiki/w/Rusted_anvil)).

*Independent of everything else; land it first.*

### S2 — `smithing.dbtable` `kind` refactor  *(blocks S3–S7)*
§6.2. Byte-compare a 500-item smithing run before and after.

### S3 — Ammunition and parts — queue #57
The 8 kinds that share one shape (1 bar → N items, no extra input):

| kind | metals | qty | cache prefix |
|---|---|---|---|
| dart tips | bronze→rune | 10 | `*_dart_tip` |
| arrowtips | bronze→rune | 15 | `*_arrowheads` |
| throwing knives | bronze→rune | 5 | `*_knife` |
| javelin heads | bronze→rune | 5 | `*_javelin_head` |
| unfinished bolts | bronze→rune | 10 | `xbows_crossbow_bolts_*_unfeathered` |
| crossbow limbs | bronze→rune | 1 | `xbows_crossbow_limbs_*` |
| nails | bronze, iron, mithril, adamant, rune (steel already live) | 15 | `nails_*` |
| mith grapple tip | mithril only | 1 | `xbows_grapple_tip_mithril` |

~45 new dbrows, zero new script logic after S2. Dart tips carry a Tourist Trap
gate; with no `quest_touristtrap/` in the tree, gate on the varp if one exists,
otherwise ship ungated and record the debt in the row comment.

Cross-lane: this **unblocks** [`FLETCHING_COMPLETION_PLAN.md`](FLETCHING_COMPLETION_PLAN.md)'s
crossbow chain, which currently has no way to obtain limbs or unfinished bolts.

### S4 — Single-item members products — queue #57
`bronze_wire` (bronze 4), `spit_iron` (iron 17), `oil_lantern_frame` (iron 26),
`studs` (steel 36), `bullseye_lantern_nolens` (steel 49). Five dbrows. Each has
a downstream consumer already in the tree (Crafting studded armour, Firemaking
lanterns, Cooking spits) — check each consumer's `[opheldu]` still resolves.

### S5 — Claws — queue #57
`{bronze,iron,steel,mithril,adamant,rune}_claws`, 2 bars, at 13/28/43/63/83/98.
`black_claws` (3098) sits in the middle of the id run but is **not** a smithing
product — [black equipment](https://oldschool.runescape.wiki/w/Black_equipment)
cannot be smithed at all, at any tier. Gate:
[Death Plateau](https://oldschool.runescape.wiki/w/Death_Plateau)
is not ported; ship ungated with the debt recorded, matching how S3 handles
Tourist Trap.

### S6 — Blurite — queue #59
Smelt `blurite_ore` → `blurite_bar` at 13 (8 XP); anvil `blurite_bolts_unf` at 8
and `blurite_limbs` at 13, both 17.5 XP. One smelting row, two smithing rows, and
a `switch_obj` arm for the special-metal menu. The Knight's Sword is not ported —
same ungated-with-debt rule.

### S7 — Elemental metal — queue #59
`elemental_workshop_ore` + 4 coal → `elemental_workshop_bar`, level 20, 7.5 XP,
**only** at `elemental_workshop_furnace` (3410), which must *not* get
`category=smithing_furnace`. Implement as an `[oplocu,elemental_workshop_furnace]`
in `quest_elemental_workshop/`, not in `skill_smithing/`. One bar at a time —
the wiki is explicit: *"Each bar must be made individually."*

### S8 — Cannonball fixes — queue #56
1. XP `375` → `256` (25.6, [Steel cannonball](https://oldschool.runescape.wiki/w/Steel_cannonball)).
2. Add the `%mcannonquest < ^mcannon_complete` gate.
3. Make it loop (currently one set per click; the real thing continues until
   bars or mould run out).

### S9 — Gear perks — queue #61
- **Imcando hammer.** Replace every `inv_total(inv, hammer) < 1` with a
  `~has_smithing_hammer` proc covering `hammer` in inventory **or**
  `imcando_hammer` worn/carried. Four call sites in `smithing.rs2`, one in
  `dragon_sq.rs2`, plus Crafting/Construction/Mahogany Homes consumers.
- **Smithing cape.** `skillcape_smithing` / `_trimmed` gets the goldsmith-gauntlet
  gold-bar bonus in both `smelting.rs2` and `superheat.rs2`, and raises the coal
  bag ceiling once that lands.
- **Varrock armour.** 10 % double-bar at `fai_varrock_*`/Edgeville furnace only,
  tier-capped steel / mithril / adamantite / all. Note the
  [Ring of forging](https://oldschool.runescape.wiki/w/Ring_of_forging)
  interaction: a doubled iron smelt burns **2** charges.
- Skillcape `+1` boost: check `skillcape_boost.rs2` already covers Smithing (it
  does for Fishing).

### S10 — Barbarian smithing (spears + hastae) — queue #57
`brut_anvil` needs `category=anvil` *or* its own trigger; spears take 1 bar +
1 log of the matching tier and pay **2× the bar rate**; hastae are
`brut_*_spear`. Six spears + six hastae = 12 rows plus a two-input path the
current `~smithing_anvil` proc does not have (it deletes exactly one obj type).
Blocked on a
[Barbarian Training](https://oldschool.runescape.wiki/w/Barbarian_Training)
miniquest that does not exist — land the anvil and the recipes, gate on a new
`%barbarian_smithing` varp defaulting to unlocked, and record the debt.

### S11 — Gold bowl — Legends' Quest
`goldbowl_empty` (721), 2 gold bars, level 50, 30 XP, at `furnace_legendsquest`
(2966). Replace the blanket `gold_bar` → "use a furnace" line with a quest check.

### S12 — Assembled specials at a regular anvil — queue #58
[Godsword blade](https://oldschool.runescape.wiki/w/Godsword_blade) (80, three
shards) and
[Dragonfire shield](https://oldschool.runescape.wiki/w/Dragonfire_shield) (90,
visage + anti-dragon shield). Both are `[oplocu,_anvil]` arms in the same
"check `last_useitem`, call out, fall through" idiom `dragon_sq.rs2` and the One
Small Favour repair already use — so the pattern is proven. Both inputs resolve
under non-obvious cache names: draconic visage is **`dragonfire_visage`**
(11286) and the anti-dragon shield is **`antidragonbreathshield`** (1540); the
three godsword shards combine through `godwars_godsword_blade1+2`,
`1+3`, `2+3` to `godwars_godsword_blade1+2+3` (11798).

### S13 — Interface 312 — queue #60
Replace the `~p_choice5` chain with the real make-menu. 39 components, `make_1/5/
10/x/all/some`, 24 product slots. Follow the Fletching plan's `skillmulti`
findings for the entry-script signature convention; smithing's own entry is
`clientscript 2926` per `[makex]`'s `onload`. This is the slice that makes 30
product kinds usable.

### S14 — Anvil / furnace category sweep
Categorise the locs listed in §3.1 and §3.2 that should be smithable and are
not, with an overlay file per §3's table. Deliberately excluded:
`elemental_workshop_furnace`, `furnace2`, `raids_tekton_anvil`,
`camdozaal_anvil`.

---

## 8. Selftests — land alongside S2, not at the end

`server/scripts/selftest.rs2` is the framework; `charges_selftest.rs2` and
`gauntlet_selftest.rs2` are the per-system pattern to copy. There is no smithing
selftest today.

| test | asserts |
|---|---|
| `selftest_smithing_table` | every `smithing` row resolves obj + bar + level + XP; no duplicate `(bar, kind)` pair |
| `selftest_smithing_xp_rule` | for every non-exempt row, `experience == bars × per-bar constant` (§1.1) — the single check that would catch a mistyped XP the way the cannonball bug slipped through |
| `selftest_smithing_kind_lookup` | after S2, `~smithing_product_for(bar, kind)` returns the same obj for all 103 pre-existing rows as the deleted switches did |
| `selftest_smelting_table` | every `smelting` row resolves ore + bar + level + XP; iron is the only row with a fail chance |
| `selftest_smithing_hammer` | `~has_smithing_hammer` passes for inventory `hammer`, for worn `imcando_hammer`, and fails for neither |
| `selftest_anvil_categories` | every loc in `category=772` has a live `[oploc1]` **and** `[oplocu]` — the regression guard against the dead click S1 fixes, and against a second `camdozaal_anvil` |
| `selftest_furnace_categories` | same for `category=215` and `[oploc2]` |
| `selftest_cannonball` | 35 Smithing + mould + quest → 4 balls and 25.6 XP; missing quest → refusal |

The two category tests are the ones that would have caught S1 and §0.6.

---

## 9. Order

```
S1  oploc1 anvil        ─ independent, cheap, fixes the headline bug
S14 category sweep      ─ independent, cheap, fixes N dead clicks
S8  cannonball fixes    ─ independent, 3 lines
S2  kind refactor       ─ blocks S3–S7
 ├─ S3 ammunition + parts   ─┐
 ├─ S4 single-item members   ├─ each independent, any order
 ├─ S5 claws                 │
 ├─ S6 blurite               │
 └─ S7 elemental metal      ─┘   (S7 lands in the quest lane)
S9  gear perks          ─ independent of S2
S10 barbarian anvil     ─ after S2 (needs the two-input path)
S11 gold bowl           ─ independent
S12 assembled specials  ─ independent (oplocu arms)
S13 interface 312       ─ after S3–S7, so there is something to show
§8  selftests           ─ with S2, extended per slice
```

Cheapest-first, if the goal is fewest dead clicks per commit: **S1, S14, S8**
before the refactor.

---

## 10. Explicitly out of this lane

| what | why | where it goes |
|---|---|---|
| [Giants' Foundry](https://oldschool.runescape.wiki/w/Giants%27_Foundry) | 2022; commissions, mould presets, temperature, Foundry Reputation, double ammo mould, Smiths' Uniform, smithing catalyst | KRONOS |
| [Smiths' Uniform](https://oldschool.runescape.wiki/w/Smiths%27_Uniform) tick-speed perk | 2022, Foundry reward | KRONOS (with the Foundry) |
| Sailing bars + keel parts + chain + cupronickel + lead | post-2009 skill | KRONOS |
| Bronze/iron/mithril/adamant/rune/granite cannonballs | Sailing-era; no cache objects | KRONOS |
| [Shayzien armour](https://oldschool.runescape.wiki/w/Shayzien_armour) tiers 1–5, [Lovakite bar](https://oldschool.runescape.wiki/w/Lovakite_bar) | 2016 Kourend; objects exist (`shayzien_*`, `lovakite_bar`) but the Lovakite furnace and Shayzien are a whole-area lane | Kourend lane |
| Crystal singing / [Song of the Elves](https://oldschool.runescape.wiki/w/Song_of_the_Elves) smithing | 2019 | Prifddinas lane |
| [Barronite deposit](https://oldschool.runescape.wiki/w/Barronite_deposit) crushing, [Imcando hammer](https://oldschool.runescape.wiki/w/Imcando_hammer) acquisition | 2021 Camdozaal; partially present via `quest_defenderofvarrock` | Camdozaal lane (S9 only *consumes* the hammer) |
| [Coal bag](https://oldschool.runescape.wiki/w/Coal_bag) | already deferred by [`MINING_COMPLETION_PLAN.md`](MINING_COMPLETION_PLAN.md) §6 | Mining lane |
| Torva restoration, spirit sigils, ancient wyvern shield, dragonfire ward | post-2009 boss chains | boss lanes |
| Zalcano tephra refining | 2019 | Prifddinas lane |
| [Blast Furnace](https://oldschool.runescape.wiki/w/Blast_Furnace) remainder (queue #62) | enter/machine/breakage all landed | SCAPE2009 §13c — closed |
| Elemental Workshop workbench products (§2.11) | quest content, not skill content | `quest_elementalworkshopii/` |
| [Iron sheet](https://oldschool.runescape.wiki/w/Iron_sheet) metal press | Swan Song prop | `quest_swansong/` |

---

## 11. Verification

- `make -C src mock230-scripts` after every config change — the compack and the
  scripts must be rebuilt together.
- Headless: use a scratch `MOCK230_SAVES`; runs are **not** independent.
- **S1:** click each `category=772` anvil with a hammer and one bar of each tier;
  every one must open a menu. Then click `camdozaal_anvil` and confirm it does
  not.
- **S2:** byte-compare the message + XP output of a 500-item smithing run before
  and after the refactor. Zero diff, or the refactor is wrong.
- **S3–S7:** for each new product, smith one at the gate level and one below it;
  confirm the level message names the right item, the bar count deducted matches
  the table, the XP matches §1.1, and running out of bars mid-batch stops the
  loop with the right message rather than looping on an empty inventory.
- **S8:** 35 Smithing without Dwarf Cannon → refusal; with it → exactly 4
  `mcannonball` and 25.6 XP per bar.
- **S9:** smelt gold with gauntlets, with the cape, and with both — the bonus
  must apply once, not twice. Smelt iron at Edgeville in Varrock armour with a
  ring of forging and confirm a doubled smelt burns 2 charges.
- **§8 selftests** must be green before each slice is called done, and
  `selftest_smithing_xp_rule` must be shown to fail on a deliberately corrupted
  row — mutate the implementation, not the constant, and watch the assertion
  break. A test that cannot fail proves nothing.
