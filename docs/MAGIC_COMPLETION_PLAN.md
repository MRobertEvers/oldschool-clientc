# Finishing Magic

Plan to take Magic from its **standard-spellbook-only** state to the complete
[Magic](https://oldschool.runescape.wiki/w/Magic) skill — every spell in all
four spellbooks, plus the items, locs and NPCs each one needs.

The wiki is the authority for content (levels, runes, XP, effects, unlock
gates). The cache (`OSRS-Content/osrs239-content/interfaces/magic_spellbook.compack`,
`configs/all.obj`, `all.loc`, `all.npc`, `all.spotanim`, `all.seq`) is the
authority for **names** and for what is *expressible* at this revision.

Companion docs:
[`MAGIC_CONTENT_PORT_PLAN.md`](MAGIC_CONTENT_PORT_PLAN.md) (the M1–M10 port loop
this supersedes for M7–M10) ·
[`SKILLS_CONTENT_PORT_QUEUE.md`](SKILLS_CONTENT_PORT_QUEUE.md) rows **#30–36** ·
[`PORTING_GUIDE.md`](PORTING_GUIDE.md) §2 / §4.1 / §4.7 / §7.

**Gap authority (top-level wiki pages):**
[Magic](https://oldschool.runescape.wiki/w/Magic) ·
[Standard spellbook](https://oldschool.runescape.wiki/w/Standard_spellbook) ·
[Ancient Magicks](https://oldschool.runescape.wiki/w/Ancient_Magicks) ·
[Lunar spells](https://oldschool.runescape.wiki/w/Lunar_spells) ·
[Arceuus spellbook](https://oldschool.runescape.wiki/w/Arceuus_spellbook) ·
[Spellbook](https://oldschool.runescape.wiki/w/Spellbook) ·
[Magic/Training](https://oldschool.runescape.wiki/w/Magic/Training)

---

## 0. Where Magic stands today — measured

### 0.1 The headline number

Interface **218 `magic_spellbook`** has **211 components**, of which ~193 are
spells or spell-adjacent buttons. A diff of every `magic_spellbook:<component>`
reference across the whole content tree against the compack gives:

```
132 UNBOUND of 211  |  64 bound
```

**132 spellbook components have no server binding.** Every one of them is a
button the client already draws and the player can already click; clicking it
today does nothing at all. That is the work list, and §2–§5 enumerate all 132.

Split by book:

| Book | Unbound | Bound | Notes |
|---|---:|---:|---|
| Standard | 17 | ~62 | The book is *mostly* done — surges, teleother, teleblock, house/Kourend/Fortis/Ape, enchant 6–7 remain |
| Ancient | 23 | 2 | Only Ice Barrage and Blood Barrage exist |
| Lunar | 44 | 0 | Nothing — including the book's Home Teleport |
| Arceuus | 48 | 0 | Nothing |
| **Total** | **132** | **64** | |

### 0.2 What *is* wired

`skill_magic/` is 1,134 lines (10 scripts + 6 config files);
`skill_combat/.../player_magic.rs2` is 468 lines with
`configs/magic/magic_combat_spells.dbrow` at 616.

| Area | In tree |
|---|---|
| Utility | tele (7) · alch ×2 · enchant 1–5 · telegrab · superheat · charge · 4 orbs · bones→bananas/peaches |
| Combat | strike→wave (16) · curse/weaken/confuse · bind/snare/entangle · vuln/enfeeble/stun · Crumble Undead · 3 god spells · Iban Blast · Magic Dart |
| Ancient | Ice Barrage + Blood Barrage only (3×3 AoE, freeze, blood leech, bloodbark/sceptre scaling) |
| Tables | `magic_spell_table` (23 columns) + `magic_staff_table` — the schema is already rich enough for almost everything below |
| Autocast | `auto_cast.rs2` on modern `combat_interface` + IF 201; strike→wave index map |
| Rune substitution | 4 elemental staff families + `tome_of_fire` in the shield slot |
| Magic damage | `%com_magicdamage` (tenths of a percent) applied in `~magic_spell_maxhit` |
| Freeze | live — `npc_freeze`, `freeze_time` column, re-freeze refusal |
| MTA | live — **do not park** |

### 0.3 Three premises in the old plan that are now wrong

**1. "`npc_statsub` — no mutable npc combat levels."**
`player_magic.rs2:356` still reads:

```
[proc,pvm_stat_change_effect](dbrow $spell_data)
// npc_statsub needs mutable npc levels — not modelled yet.
return;
```

`SS_OP_NPC_STATSUB` (2541) is **hosted and complete**
(`src/torirsserver/torirs_server_scripts.c:5742`): it writes `ToriRSServerNpc.stat_drain[]`,
`npc_stat()` reads the drain back (`:5710`), `npc_statadd` is the mirror, and
`torirs_server_combat.c:2094` clears the drain on respawn. So **Confuse, Weaken,
Curse, Vulnerability, Enfeeble and Stun all spend runes, pay XP, play their
graphic — and drain nothing.** Six live spells are cosmetic. This is a one-proc
fix (§1.4), not an engine gap.

**2. "No spellbook-switch opcode surface yet"**
(`quests/quest_deserttreasure/scripts/deserttreasure.rs2:1351`).
The client selects its book from **`%varbit4070`** — cache name **`spellbook`**,
`basevar=alternate_spells`, `startbit=0 endbit=1`, so values 0–3 =
standard/ancient/lunar/arceuus (`configs/all.varbit:20353`, read by
`scripts/script_2611.cs2` `[proc,magic_spellbook_redraw]`). Content already
writes cache varbits by name (`%varbit_108 = ^true;` in
`quests/quest_mm/scripts/mm_zooknock.rs2:99`), and `ToriRSServer_VarbitSet` on the
server side is the only write the client honours — the client's own `pop_varbit`
is discarded (`torirs_server_world.c:17306`). **`%spellbook = 1` should be all a
spellbook switch takes.** Verify before building on it (§1.1).

**3. "The ancient/lunar/arceuus assets aren't in this cache."**
They all are, under names a search has to find rather than guess:

| Wanted | Cache name(s) |
|---|---|
| Every ancient spell graphic | `ice_rush_travel`/`_impact`, `ice_blitz_impact2`/`3`, `smoke_*`, `shadow_*`, `blood_*` — 44 spotanims |
| Ensouled heads | `arceuus_corpse_goblin` … (+ `_initial`, `cert_`) |
| Thralls | `arceuus_thrall_{ghost,skeleton,zombie}_{lesser,superior,greater}` (npc 10878–10886) |
| Reanimated monsters | `arceuus_reanimated_{goblin,monkey,imp,minotaur,scorpion,bear,unicorn,dog,…}` (npc 7018+) |
| Ancient altar | `dt_zaros_altar` (loc 6552) |
| Arceuus altar | `arceuus_altar` (loc 28455) · `astral_altar` (34771) |
| POH occult altar | `poh_altar_occult{,_standard,_ancient,_lunar,_arceuus}` (31858–31861) |
| Combination-rune staves | `mud_battlestaff` `steam_battlestaff` `smoke_battlestaff` `mist_battlestaff` `dust_battlestaff` + all five `mystic_*` |
| Tomes | `tome_of_fire` (20714) · `tome_of_water` (25574) · `tome_of_earth` (30064) |
| High-end weapons | `sotd` (11791) · `tots` (11905) · `toxic_sotd` · `toxic_tots_charged` · `kodai_wand` (21006) · `sanguinesti_staff` · `nightmare_staff{,_harmonised,_volatile,_eldritch}` · `magictraining_wand_master` (6914) · `magictraining_bookofmagic` (6889) · `slayer_staff{,_enchanted}` |
| Teleport tablets | `poh_tablet_*teleport` (8007–8013) · `teletab_*` (19613–19631, `teletab_ape`, `teletab_battlefront`, …) · `nzone_teletab_*` |
| Runes | every one, including `mistrune` `dustrune` `mudrune` `smokerune` `steamrune` `lavarune` `astralrune` `wrathrune` |

---

## 1. Cross-cutting prerequisites — do these first

Every slice in §2–§5 depends on some of these. None of them is per-spell work,
and skipping any of them means building 132 bindings on a book the player
cannot open.

### 1.1 P0 — Spellbook switching (`%spellbook`)

Without this **three of the four books are unreachable**, and 115 of the 132
components can never be clicked no matter what is bound to them.

- Write `%spellbook` = 0/1/2/3. Confirm `ToriRSServer_VarbitSet` transmits and the
  client redraws (`script_2611` is `if_setonvartransmit`-driven via
  `magic_spellbook_redraw`).
- **`basevar=alternate_spells` is a carrier**: other varbits share it. Follow
  the `quest_mortton.varp` pattern — `wholeread=allow`, never `wholewrite` —
  and write the varbit, not the varp.
- Switch surfaces, each its own loc/NPC binding:
  - Ancient — [Altar of Zaros](https://oldschool.runescape.wiki/w/Altar_(Jaldraocht_Pyramid)),
    loc `dt_zaros_altar`, gated on Desert Treasure I.
  - Lunar — [Astral altar / Lunar Isle](https://oldschool.runescape.wiki/w/Lunar_Isle),
    gated on [Lunar Diplomacy](https://oldschool.runescape.wiki/w/Lunar_Diplomacy).
    The altar loc name still needs a cache lookup (`astral_altar` is the
    Runecraft altar, not necessarily the switch).
  - Arceuus — [Dark Altar](https://oldschool.runescape.wiki/w/Dark_Altar) /
    `arceuus_altar`, gated on
    [A Kingdom Divided](https://oldschool.runescape.wiki/w/A_Kingdom_Divided) /
    Arceuus favour.
  - Any book — [Altar of the Occult](https://oldschool.runescape.wiki/w/Altar_of_the_occult),
    `poh_altar_occult*`, POH already exists (`skill_construction/scripts/poh_build.rs2`).
- **`%spellbook_sublist`** (`varbit9730`, `basevar=alternate_spells_2`,
  bits 23–25) drives the sub-book selector `script_2611` reads. Confirm what
  it selects at this revision before writing it.

**Quest unlocks that must set it:** `quests/quest_deserttreasure/scripts/deserttreasure.rs2:1351`
(the deferral is written in the file) and
`quests/quest_lunardiplomacy/scripts/lunardip_shared.rs2:48` (the completion
reward text already promises the book).

### 1.2 P0 — Spell names never reach the client

Per [`if3-opbase-and-target-hooks-dropped`], the cache's `name=` and
`ontargetenter` on IF3 components are dropped in this pipeline — every
`magic_spellbook` block in `interfaces/magic_spellbook.if` has a literal
`name=` with nothing after it. Tooltips and the minimenu will say the wrong
thing (or nothing) for all 132 new spells unless they are registered from CS2.
**Fix this once, before authoring 132 rows**, or the whole book ships nameless.

### 1.3 P1 — Spellbook filter + info panel

`filtermenu`, `filterbutton`, `infobutton`, `infolayer`, `tooltip` and
`com_202`/`204`/`205` are unbound. `%varbit6718 magic_spellbook_hidefilterbutton`
controls the button's visibility. Wiki:
[Spellbook § Filters](https://oldschool.runescape.wiki/w/Spellbook).
Low priority for correctness, high priority for a book with 90 entries.

### 1.4 P1 — Land the stat-drain effect (one proc)

Replace the empty `~pvm_stat_change_effect` with a loop over the row's
`stat_change` column calling `npc_statsub($npc_stat, $constant, $percent)`.
The rows already carry the data; `~pvm_debuff_allowed` already has the
"already lowered" message and already reads `npc_stat < npc_basestat` — that
branch is documented in-file as unreachable and this makes it reachable.

Closes: [Confuse](https://oldschool.runescape.wiki/w/Confuse) ·
[Weaken](https://oldschool.runescape.wiki/w/Weaken) ·
[Curse](https://oldschool.runescape.wiki/w/Curse) ·
[Vulnerability](https://oldschool.runescape.wiki/w/Vulnerability) ·
[Enfeeble](https://oldschool.runescape.wiki/w/Enfeeble) ·
[Stun](https://oldschool.runescape.wiki/w/Stun).
It is also a hard dependency for Shadow spells (§3) and Arceuus corruption (§5).

### 1.5 P1 — Teleport rules, in one shared proc

`~magic_teleport` handles the seven standard teleports. Everything in §2–§5
that teleports needs the same shared gates, and they should live in one place:
[Wilderness](https://oldschool.runescape.wiki/w/Wilderness) level ceiling
(20 standard / 30 for tabs) · [Teleblock](https://oldschool.runescape.wiki/w/Teleport_Block) ·
minigame/instance refusals · quest gates · item requirements (e.g. Ape Atoll's
banana + greegree).

### 1.6 P2 / blocked — everything two-player

`SS_OP_P_OPPLAYERT` (2084) is **declared but not hosted**, and
`TORIRSSERVER_PLAYER_MAX` is 1 (queue row #2, "secondary player"). That blocks
**every** player-targeted spell: Teleother ×3, Teleport Block, Bounty Target,
Heal Other, Heal Group, Vengeance Other, Energy Transfer, Stat Spy, Cure Other,
Cure Group, and all six Tele Group spells. Build these as **casts on a target
that resolves to self / no-op with the correct message**, so the rows, runes,
XP and graphics are all real and only the second player is missing.

### 1.7 P2 — Missing rune-substitution sources

`magic_staff.dbrow` has four rows (air/water/earth/fire). Missing:

- Combination battlestaves: `mud_battlestaff` (water+earth), `steam_battlestaff`
  (water+fire), `smoke_battlestaff` (air+fire), `mist_battlestaff` (air+water),
  `dust_battlestaff` (air+earth), plus each `mystic_*` and `*_pretty` variant.
  Wiki: [Battlestaff](https://oldschool.runescape.wiki/w/Battlestaff).
  **Note the table's shape:** one `rune` per row and the lookup already reads
  *two* rows per staff (`$staff_data2 = db_findnext`), so a combination staff is
  two rows sharing one `staff` value — the same way `lava_battlestaff` already
  appears under both earth and fire.
- `tome_of_water` (25574) and `tome_of_earth` (30064) alongside the existing
  `tome_of_fire` in `~tome_of_elements_rune`.
  Wiki: [Tome of water](https://oldschool.runescape.wiki/w/Tome_of_water) ·
  [Tome of earth](https://oldschool.runescape.wiki/w/Tome_of_earth).
- [Kodai wand](https://oldschool.runescape.wiki/w/Kodai_wand) (`kodai_wand`) —
  infinite water runes + 15% chance to save a rune.
- [Staff of the dead](https://oldschool.runescape.wiki/w/Staff_of_the_dead)
  (`sotd`, `toxic_sotd`) — infinite air runes.
- [Bryophyta's staff](https://oldschool.runescape.wiki/w/Bryophyta's_staff) /
  [Mystic mud staff](https://oldschool.runescape.wiki/w/Mystic_mud_staff) etc.

---

## 2. S1 — Standard spellbook remainder (17 components)

[Standard spellbook](https://oldschool.runescape.wiki/w/Standard_spellbook)

| Component | Spell | Lvl | Beyond the standard cast path |
|---|---|---:|---|
| `teleport_to_your_house` | [Teleport to House](https://oldschool.runescape.wiki/w/Teleport_to_House) | 40 | POH entry (`poh_enter_leave.rs2`) · inside/outside toggle |
| `kourend_teleport` | [Kourend Castle Teleport](https://oldschool.runescape.wiki/w/Kourend_Castle_Teleport) | 69 | Unlocked by reading [Transportation incantations](https://oldschool.runescape.wiki/w/Transportation_incantations) — needs an unlock varbit + the scroll obj |
| `fortis_teleport` | [Civitas illa Fortis Teleport](https://oldschool.runescape.wiki/w/Civitas_illa_Fortis_Teleport) | 54 | Requires [Twilight's Promise](https://oldschool.runescape.wiki/w/Twilight%27s_Promise) |
| `ape_teleport` | [Ape Atoll Teleport](https://oldschool.runescape.wiki/w/Ape_Atoll_Teleport) | 64 | Requires [Monkey Madness I](https://oldschool.runescape.wiki/w/Monkey_Madness_I) + a **banana** in inventory + a greegree; consumed on cast |
| `teleother_lumbridge` | [Teleother Lumbridge](https://oldschool.runescape.wiki/w/Teleother_Lumbridge) | 74 | §1.6 blocked — player target + accept dialog |
| `teleother_falador` | [Teleother Falador](https://oldschool.runescape.wiki/w/Teleother_Falador) | 82 | §1.6 blocked |
| `teleother_camelot` | [Teleother Camelot](https://oldschool.runescape.wiki/w/Teleother_Camelot) | 90 | §1.6 blocked |
| `teleport_block` | [Teleport Block](https://oldschool.runescape.wiki/w/Teleport_Block) | 85 | §1.6 blocked — PvP; the block timer belongs in §1.5's shared teleport gate |
| `bounty_target` | [Bounty Target Teleport](https://oldschool.runescape.wiki/w/Bounty_Target_Teleport) | 85 | §1.6 blocked — Bounty Hunter target tracking |
| `wind_surge` | [Wind Surge](https://oldschool.runescape.wiki/w/Wind_Surge) | 81 | Combat dbrow + `apnpct`; max 21 |
| `water_surge` | [Water Surge](https://oldschool.runescape.wiki/w/Water_Surge) | 85 | max 24 |
| `earth_surge` | [Earth Surge](https://oldschool.runescape.wiki/w/Earth_Surge) | 90 | max 27 |
| `fire_surge` | [Fire Surge](https://oldschool.runescape.wiki/w/Fire_Surge) | 95 | max 30 · all four also need autocast grid entries (§6) |
| `enchant_6` | [Lvl-6 Enchant](https://oldschool.runescape.wiki/w/Lvl-6_Enchant) | 87 | Zenyte jewellery → onyx row set in `convertobj`; `targetverb=Cast` (not `op1`) so the trigger form differs from enchant 1–5 |
| `enchant_7` | [Lvl-7 Enchant](https://oldschool.runescape.wiki/w/Lvl-7_Enchant) | 93 | Zenyte |
| `teleport_boat_to_me` | **verify** | — | `op1=Cast`, `op2=*`. Not a live standard-book spell under any name I can place; check whether it is a Leagues-only component before authoring |
| `teleport_me_to_boat` | **verify** | — | same |

Also in this slice, not spellbook components:

- **`xbows_enchant` / `enchant_jewellery`** (`op1=View`) are the two *category*
  sub-menus the filtered book folds enchants into. They are UI, not spells —
  bind with §1.3.
- **[Bolt enchantments](https://oldschool.runescape.wiki/w/Bolt_enchantments)**:
  every Lvl-N Enchant also enchants the matching bolt tier in stacks of 10.
  The `convertobj` column already carries the shape; the rows are missing.

---

## 3. S2 — Ancient Magicks (23 components)

[Ancient Magicks](https://oldschool.runescape.wiki/w/Ancient_Magicks) ·
unlock: [Desert Treasure I](https://oldschool.runescape.wiki/w/Desert_Treasure_I)

`~pvm_barrage_spell` in `player_magic.rs2:70` is already the general AoE cast —
Ice/Blood Barrage use it today. The 14 combat spells below are **rows plus one
`apnpct` line each**, with three shared effect procs to add. Freeze, leech and
the 3×3 sweep are all already built.

### 3.1 The 4×4 combat grid

Rush = single target · Burst = 3×3, ×1 rune scale · Blitz = single, stronger ·
Barrage = 3×3, strongest. Per the wiki, multi-target only applies in
[multicombat](https://oldschool.runescape.wiki/w/Multicombat_area) areas —
`~player_in_combat_check` returns a flat `true` today, which the file documents.

| Component | Spell | Lvl | Max | Element effect |
|---|---|---:|---:|---|
| `smoke_rush` | [Smoke Rush](https://oldschool.runescape.wiki/w/Smoke_Rush) | 50 | 13 | poison |
| `shadow_rush` | [Shadow Rush](https://oldschool.runescape.wiki/w/Shadow_Rush) | 52 | 14 | −10% Attack (§1.4 `npc_statsub`) |
| `blood_rush` | [Blood Rush](https://oldschool.runescape.wiki/w/Blood_Rush) | 56 | 15 | 25% leech |
| `ice_rush` | [Ice Rush](https://oldschool.runescape.wiki/w/Ice_Rush) | 58 | 16 | freeze |
| `smoke_burst` | [Smoke Burst](https://oldschool.runescape.wiki/w/Smoke_Burst) | 62 | 17 | poison, AoE |
| `shadow_burst` | [Shadow Burst](https://oldschool.runescape.wiki/w/Shadow_Burst) | 64 | 18 | −10% Attack, AoE |
| `blood_burst` | [Blood Burst](https://oldschool.runescape.wiki/w/Blood_Burst) | 68 | 21 | leech, AoE |
| `ice_burst` | [Ice Burst](https://oldschool.runescape.wiki/w/Ice_Burst) | 70 | 22 | freeze, AoE |
| `smoke_blitz` | [Smoke Blitz](https://oldschool.runescape.wiki/w/Smoke_Blitz) | 74 | 23 | poison |
| `shadow_blitz` | [Shadow Blitz](https://oldschool.runescape.wiki/w/Shadow_Blitz) | 76 | 24 | −10% Attack |
| `blood_blitz` | [Blood Blitz](https://oldschool.runescape.wiki/w/Blood_Blitz) | 80 | 25 | leech |
| `ice_blitz` | [Ice Blitz](https://oldschool.runescape.wiki/w/Ice_Blitz) | 82 | 26 | freeze |
| `smoke_barrage` | [Smoke Barrage](https://oldschool.runescape.wiki/w/Smoke_Barrage) | 86 | 27 | poison, AoE |
| `shadow_barrage` | [Shadow Barrage](https://oldschool.runescape.wiki/w/Shadow_Barrage) | 88 | 28 | −10% Attack, AoE |
| *(`blood_barrage` 92 / `ice_barrage` 94 — already bound)* | | | | |

Three effect procs to add, all mirroring `~barrage_leech`'s shape:

1. **Smoke → poison.** The tree has a `poison.varp`; route through the existing
   poison system, not a new one.
   Wiki: [Poison](https://oldschool.runescape.wiki/w/Poison).
2. **Shadow → Attack drain.** Blocked on §1.4 only.
3. **Blood → leech.** Generalise `~barrage_leech`: it currently early-returns on
   `$spell ! ^blood_barrage`, and the
   [ancient sceptre](https://oldschool.runescape.wiki/w/Ancient_sceptre) bonus
   differs per tier (the constant block in `spells.constant` already says
   2.5% rush/burst/blitz vs 3.5% barrage, and only encodes the barrage figure).

**Freeze durations** come from the wiki article, in seconds; the column is in
ticks. The tree's calibration point is Ice Barrage `freeze_time=32` = 19.2 s,
so `ticks = seconds / 0.6`. Do not guess the rush/burst/blitz values.

**Assets** are all present (§0.3): `{ice,blood,smoke,shadow}_{rush,blitz,burst,barrage}_{travel,impact}`
plus `ice_blitz_impact2`/`3`.

### 3.2 Ancient teleports (`zarosteleport1`–`8` + home)

The component names are positional; confirm the mapping against the book's
draw order before authoring. Wiki:
[Ancient Magicks § Teleports](https://oldschool.runescape.wiki/w/Ancient_Magicks).

| Component | Spell | Lvl |
|---|---|---:|
| `teleport_home_zaros` | [Ancient Home Teleport](https://oldschool.runescape.wiki/w/Home_Teleport) | 0 |
| `zarosteleport1` | [Paddewwa Teleport](https://oldschool.runescape.wiki/w/Paddewwa_Teleport) | 54 |
| `zarosteleport2` | [Senntisten Teleport](https://oldschool.runescape.wiki/w/Senntisten_Teleport) | 60 |
| `zarosteleport3` | [Kharyrll Teleport](https://oldschool.runescape.wiki/w/Kharyrll_Teleport) | 66 |
| `zarosteleport4` | [Lassar Teleport](https://oldschool.runescape.wiki/w/Lassar_Teleport) | 72 |
| `zarosteleport5` | [Dareeyak Teleport](https://oldschool.runescape.wiki/w/Dareeyak_Teleport) | 78 |
| `zarosteleport6` | [Carrallangar Teleport](https://oldschool.runescape.wiki/w/Carrallangar_Teleport) | 84 |
| `zarosteleport7` | [Annakarl Teleport](https://oldschool.runescape.wiki/w/Annakarl_Teleport) | 90 |
| `zarosteleport8` | [Ghorrock Teleport](https://oldschool.runescape.wiki/w/Ghorrock_Teleport) | 96 |

`teleport_home_standard` is already bound (`home_teleport.rs2`, 151 lines, with
a real cooldown in `teleport_cooldowns.varp`) — the three other Home Teleports
reuse it with a different destination and animation.

**Locs/NPCs this slice needs:** `dt_zaros_altar` for the switch (§1.1);
Dareeyak/Carrallangar/Annakarl/Ghorrock land in the Wilderness, so §1.5's level
ceiling applies in reverse (these are *into* the Wilderness and are allowed).

---

## 4. S3 — Lunar spells (44 components)

[Lunar spells](https://oldschool.runescape.wiki/w/Lunar_spells) ·
unlock: [Lunar Diplomacy](https://oldschool.runescape.wiki/w/Lunar_Diplomacy)
(the quest exists — `quests/quest_lunardiplomacy/`, and its reward text already
promises the book) · requires [Seal of passage](https://oldschool.runescape.wiki/w/Seal_of_passage)
at low Lunar Diplomacy completion.

This is the largest *behavioural* slice: unlike Ancients, almost none of these
are "cast, damage, done" — each is its own mechanic touching another skill.

### 4.1 Utility / skilling (the real work)

| Component | Spell | Lvl | Touches |
|---|---|---:|---|
| `bake_pie` | [Bake Pie](https://oldschool.runescape.wiki/w/Bake_Pie) | 65 | Cooking — raw→baked pie table |
| `geomancy` | [Geomancy](https://oldschool.runescape.wiki/w/Geomancy) | 65 | Farming — patch overview IF; already noted in queue #135 |
| `cure_plant` | [Cure Plant](https://oldschool.runescape.wiki/w/Cure_Plant) | 66 | Farming — `aploct` on a diseased patch |
| `monster_examine` | [Monster Examine](https://oldschool.runescape.wiki/w/Monster_Examine) | 66 | Reads npc stats → chatbox/IF |
| `npc_contact` | [NPC Contact](https://oldschool.runescape.wiki/w/NPC_Contact) | 67 | Opens a remote-dialogue picker IF |
| `humidify` | [Humidify](https://oldschool.runescape.wiki/w/Humidify) | 68 | Fills every vessel in inv |
| `hunter_kit` | [Hunter Kit](https://oldschool.runescape.wiki/w/Hunter_Kit) | 71 | Gives a kit obj; once/day |
| `spin_flax` | [Spin Flax](https://oldschool.runescape.wiki/w/Spin_Flax) | 76 | Crafting — 5 flax→bowstring |
| `superglass` | [Superglass Make](https://oldschool.runescape.wiki/w/Superglass_Make) | 77 | Crafting — sand+seaweed→molten glass |
| `tan_leather` | [Tan Leather](https://oldschool.runescape.wiki/w/Tan_Leather) | 78 | Crafting — 5 hides |
| `dream` | [Dream](https://oldschool.runescape.wiki/w/Dream_(spell)) | 79 | HP regen while sleeping; needs an anim + interrupt |
| `string_jewel` | [String Jewellery](https://oldschool.runescape.wiki/w/String_Jewellery) | 80 | Crafting — amulets |
| `magic_imbue` | [Magic Imbue](https://oldschool.runescape.wiki/w/Magic_Imbue) | 82 | Runecraft — 12-tick buff varp; combination runes without a talisman |
| `fertile_soil` | [Fertile Soil](https://oldschool.runescape.wiki/w/Fertile_Soil) | 83 | Farming — supercompost a patch (`aploct`) |
| `plank_make` | [Plank Make](https://oldschool.runescape.wiki/w/Plank_Make) | 86 | Construction — log→plank + coins |
| `recharge_dragonstone` | [Recharge Dragonstone](https://oldschool.runescape.wiki/w/Recharge_Dragonstone) | 89 | Charges dragonstone jewellery (`ITEM_CHARGES.md`) |

### 4.2 Shared buffs (all §1.6-blocked)

| Component | Spell | Lvl |
|---|---|---:|
| `cure_other` | [Cure Other](https://oldschool.runescape.wiki/w/Cure_Other) | 68 |
| `cure_me` | [Cure Me](https://oldschool.runescape.wiki/w/Cure_Me) | 71 |
| `cure_group` | [Cure Group](https://oldschool.runescape.wiki/w/Cure_Group) | 74 |
| `stat_spy` | [Stat Spy](https://oldschool.runescape.wiki/w/Stat_Spy) | 75 |
| `rest_pot_share` | [Stat Restore Pot Share](https://oldschool.runescape.wiki/w/Stat_Restore_Pot_Share) | 81 |
| `stren_pot_share` | [Boost Potion Share](https://oldschool.runescape.wiki/w/Boost_Potion_Share) | 84 |
| `energy_trans` | [Energy Transfer](https://oldschool.runescape.wiki/w/Energy_Transfer) | 91 |
| `heal_other` | [Heal Other](https://oldschool.runescape.wiki/w/Heal_Other) | 92 |
| `vengeance_other` | [Vengeance Other](https://oldschool.runescape.wiki/w/Vengeance_Other) | 93 |
| `heal_group` | [Heal Group](https://oldschool.runescape.wiki/w/Heal_Group) | 95 |

**`vengeance` (94)** is the exception — [Vengeance](https://oldschool.runescape.wiki/w/Vengeance)
is self-cast and fully buildable now: a varp flag, a 30-second cooldown, a
75%-damage reflect hook in the damage path, and the "Taste vengeance!" overhead.

**`spellbook_swap` (96)** — [Spellbook Swap](https://oldschool.runescape.wiki/w/Spellbook_Swap).
Note its component carries `op2=Standard op3=Ancient op4=Arceuus`, so it is a
direct second consumer of §1.1's `%spellbook` write, with a one-cast timer.

### 4.3 Lunar teleports

`teleport_home_lunar` (0) plus six destinations, each with a **Tele Group**
partner (§1.6-blocked for the group form; the solo form is buildable now).

| Solo | Group | Spell | Lvl (solo/group) |
|---|---|---|---|
| `tele_moonclan` | `tele_group_moonclan` | [Moonclan Teleport](https://oldschool.runescape.wiki/w/Moonclan_Teleport) | 69 / 70 |
| `tele_waterbirth` | `tele_group_waterbirth` | [Waterbirth Teleport](https://oldschool.runescape.wiki/w/Waterbirth_Teleport) | 72 / 73 |
| `tele_barb_out` | `tele_group_barbarian` | [Barbarian Teleport](https://oldschool.runescape.wiki/w/Barbarian_Teleport) | 75 / 76 |
| `tele_khazard` | `tele_group_khazard` | [Khazard Teleport](https://oldschool.runescape.wiki/w/Khazard_Teleport) | 78 / 79 |
| `tele_fish` | `tele_group_fishing_guild` | [Fishing Guild Teleport](https://oldschool.runescape.wiki/w/Fishing_Guild_Teleport) | 85 / 86 |
| `tele_cather` | `tele_group_catherby` | [Catherby Teleport](https://oldschool.runescape.wiki/w/Catherby_Teleport) | 87 / 88 |
| `tele_ghorrock` | `tele_group_ghorrock` | [Ice Plateau Teleport](https://oldschool.runescape.wiki/w/Ice_Plateau_Teleport) | 89 / 90 |
| `ourania_teleport` | — | [Ourania Teleport](https://oldschool.runescape.wiki/w/Ourania_Teleport) | 71 |

⚠ **`tele_ghorrock` is not Ghorrock here.** The lunar book reuses the ancient
component name for Ice Plateau. Do not let a name match decide the destination.

---

## 5. S4 — Arceuus spellbook (48 components)

[Arceuus spellbook](https://oldschool.runescape.wiki/w/Arceuus_spellbook) ·
unlock: [A Kingdom Divided](https://oldschool.runescape.wiki/w/A_Kingdom_Divided)
/ Arceuus favour · no LostCity reference exists (post-2009 content) — the wiki
and the cache are the only authorities.

### 5.1 Reanimation (`arceuus_corpse_*` → `arceuus_reanimated_*`)

[Reanimation](https://oldschool.runescape.wiki/w/Reanimation) — cast on an
[ensouled head](https://oldschool.runescape.wiki/w/Ensouled_head) at the
[Dark Altar](https://oldschool.runescape.wiki/w/Dark_Altar) to spawn a monster
that gives Prayer XP on kill. This is a **whole content system**, not a spell:
a head→npc table, an `npc_add` + despawn lifecycle
(see [`npc-add-does-not-respawn`], [`npc-index-is-per-client`]), and
the Prayer XP award.

| Component | Spell | Lvl |
|---|---|---:|
| `reanimation_basic` | [Basic Reanimation](https://oldschool.runescape.wiki/w/Basic_Reanimation) | 16 |
| `reanimation_adept` | [Adept Reanimation](https://oldschool.runescape.wiki/w/Adept_Reanimation) | 41 |
| `reanimation_expert` | [Expert Reanimation](https://oldschool.runescape.wiki/w/Expert_Reanimation) | 72 |
| `reanimation_master` | [Master Reanimation](https://oldschool.runescape.wiki/w/Master_Reanimation) | 90 |
| `necromancy_dog` | **verify** — `targetverb=Reanimate`, no live OSRS spell of this name | — |

### 5.2 Thralls (`arceuus_thrall_*`, npc 10878–10886)

[Resurrect Thralls](https://oldschool.runescape.wiki/w/Thrall) — a summoned
follower that attacks the player's target for a duration. Depends on the
follower/pet lifetime work already in the tree ([`summoning-familiar-footprint`],
[`follower-leash-pets-and-instances`]).

| Components | Spell | Lvl |
|---|---|---:|
| `resurrect_lesser_{ghost,skeleton,zombie}` | [Resurrect Lesser Ghost/Skeleton/Zombie](https://oldschool.runescape.wiki/w/Resurrect_Lesser_Ghost) | 38 |
| `resurrect_superior_{ghost,skeleton,zombie}` | [Resurrect Superior …](https://oldschool.runescape.wiki/w/Resurrect_Superior_Ghost) | 57 |
| `resurrect_greater_{ghost,skeleton,zombie}` | [Resurrect Greater …](https://oldschool.runescape.wiki/w/Resurrect_Greater_Ghost) | 76 |

### 5.3 Combat / utility

| Component | Spell | Lvl | Needs |
|---|---|---:|---|
| `ghostly_grasp` | [Ghostly Grasp](https://oldschool.runescape.wiki/w/Ghostly_Grasp) | 35 | air spell, max 12 |
| `skeletal_grasp` | [Skeletal Grasp](https://oldschool.runescape.wiki/w/Skeletal_Grasp) | 56 | earth, max 17 |
| `undead_grasp` | [Undead Grasp](https://oldschool.runescape.wiki/w/Undead_Grasp) | 79 | fire, max 24 |
| `inferior_demonbane` | [Inferior Demonbane](https://oldschool.runescape.wiki/w/Inferior_Demonbane) | 44 | fire; bonus vs demons — needs a `demon` npc param, the way M3 added `undead` |
| `superior_demonbane` | [Superior Demonbane](https://oldschool.runescape.wiki/w/Superior_Demonbane) | 62 | same |
| `dark_demonbane` | [Dark Demonbane](https://oldschool.runescape.wiki/w/Dark_Demonbane) | 82 | same |
| `mark_of_darkness` | [Mark of Darkness](https://oldschool.runescape.wiki/w/Mark_of_Darkness) | 59 | self-buff varp; empowers the four spells below |
| `shadow_veil` | [Shadow Veil](https://oldschool.runescape.wiki/w/Shadow_Veil) | 47 | Thieving — 15% failure reduction, buff varp |
| `dark_lure` | [Dark Lure](https://oldschool.runescape.wiki/w/Dark_Lure) | 50 | Hunter — lures dark beasts |
| `lesser_corruption` | [Lesser Corruption](https://oldschool.runescape.wiki/w/Lesser_Corruption) | 64 | §1.4 stat drain |
| `vile_vigour` | [Vile Vigour](https://oldschool.runescape.wiki/w/Vile_Vigour) | 66 | Prayer→run energy |
| `degrime` | [Degrime](https://oldschool.runescape.wiki/w/Degrime) | 70 | Herblore — cleans all grimy herbs in inv |
| `ward_of_arceuus` | [Ward of Arceuus](https://oldschool.runescape.wiki/w/Ward_of_Arceuus) | 73 | anti-drain buff |
| `death_charge` | [Death Charge](https://oldschool.runescape.wiki/w/Death_Charge) | 80 | spec-restore on kill |
| `demonic_offering` | [Demonic Offering](https://oldschool.runescape.wiki/w/Demonic_Offering) | 84 | Prayer — burns 2 demonic ashes |
| `greater_corruption` | [Greater Corruption](https://oldschool.runescape.wiki/w/Greater_Corruption) | 85 | §1.4 |
| `sinister_offering` | [Sinister Offering](https://oldschool.runescape.wiki/w/Sinister_Offering) | 92 | Prayer — burns bones |
| `resurrect_crops` | [Resurrect Crops](https://oldschool.runescape.wiki/w/Resurrect_Crops) | 78 | Farming — revives a dead patch (`aploct`) |
| `monster_inspect` | **verify** | — | possibly a Leagues component |
| `transmute_upgrade` / `transmute_downgrade` | **verify** | — | `op9=Warnings op10=Swap-Effect` — looks like a relic/Leagues control, not a spell |

### 5.4 Arceuus teleports

| Component | Spell | Lvl |
|---|---|---:|
| `teleport_home_arceuus` | [Arceuus Home Teleport](https://oldschool.runescape.wiki/w/Home_Teleport) | 0 |
| `teleport_arceuus_library` | [Arceuus Library Teleport](https://oldschool.runescape.wiki/w/Arceuus_Library_Teleport) | 6 |
| `teleport_draynor_manor` | [Draynor Manor Teleport](https://oldschool.runescape.wiki/w/Draynor_Manor_Teleport) | 17 |
| `teleport_battlefront` | [Battlefront Teleport](https://oldschool.runescape.wiki/w/Battlefront_Teleport) | 23 |
| `teleport_mind_altar` | [Mind Altar Teleport](https://oldschool.runescape.wiki/w/Mind_Altar_Teleport) | 28 |
| `teleport_respawn` | [Respawn Teleport](https://oldschool.runescape.wiki/w/Respawn_Teleport) | 34 |
| `teleport_salve_graveyard` | [Salve Graveyard Teleport](https://oldschool.runescape.wiki/w/Salve_Graveyard_Teleport) | 40 |
| `teleport_fenkenstrain_castle` | [Fenkenstrain's Castle Teleport](https://oldschool.runescape.wiki/w/Fenkenstrain%27s_Castle_Teleport) | 48 |
| `teleport_west_ardougne` | [West Ardougne Teleport](https://oldschool.runescape.wiki/w/West_Ardougne_Teleport) | 61 |
| `teleport_harmony_island` | [Harmony Island Teleport](https://oldschool.runescape.wiki/w/Harmony_Island_Teleport) | 65 |
| `teleport_cemetery` | [Cemetery Teleport](https://oldschool.runescape.wiki/w/Cemetery_Teleport) | 71 |
| `teleport_barrows` | [Barrows Teleport](https://oldschool.runescape.wiki/w/Barrows_Teleport) | 83 |
| `teleport_ape_atoll_dungeon` | [Ape Atoll Teleport (Arceuus)](https://oldschool.runescape.wiki/w/Ape_Atoll_Teleport_(Arceuus)) | 90 |

Every one of these has a matching `teletab_*` obj already in the cache
(`teletab_mind_altar`, `teletab_salve`, `teletab_fenk`, `teletab_westardy`,
`teletab_harmony`, `teletab_cemetery`, `teletab_barrows`, `teletab_ape`,
`teletab_battlefront`, `teletab_draynor`, `teletab_lumbridge`) — see §7.

---

## 6. S5 — Casting infrastructure

Not spellbook components, but the difference between "the button works" and
"Magic works".

| # | Item | Wiki | Notes |
|---|---|---|---|
| 6.1 | Autocast grids for ancient + surge + Arceuus | [Autocast](https://oldschool.runescape.wiki/w/Autocast) | `auto_cast.rs2` maps only the 16 strike→wave client indices (`enum_1986`). Ancients, surges and the Arceuus grasps/demonbanes all autocast in OSRS. Also deferred there: staff-set gating (`autocast_set`), equip-change reset |
| 6.2 | Defensive autocast | — | `%autocast_defmode` exists and is read; verify it actually routes Defence XP |
| 6.3 | Splash / accuracy | [Magic § Accuracy](https://oldschool.runescape.wiki/w/Magic) | `failedspell_impact` is played, but confirm the 0-damage splash still awards the base 0 XP and that the accuracy roll uses magic level + `%com_magicattack` |
| 6.4 | Magic damage sources | [Magic damage](https://oldschool.runescape.wiki/w/Magic_damage) | `%com_magicdamage` is applied in one place; audit that occult/tormented/ancestral/Virtus/magus all populate it |
| 6.5 | Magic level boosts | [Temporary skill boost](https://oldschool.runescape.wiki/w/Temporary_skill_boost) | `magic_potion.rs2` is live (+4). Missing: [Imbued heart](https://oldschool.runescape.wiki/w/Imbued_heart) (`imbued_heart.rs2` exists under `skill_slayer/` — check it boosts Magic), [Saturated heart](https://oldschool.runescape.wiki/w/Saturated_heart), [Battlemage potion](https://oldschool.runescape.wiki/w/Battlemage_potion), [Divine battlemage](https://oldschool.runescape.wiki/w/Divine_battlemage_potion), [Ancient brew](https://oldschool.runescape.wiki/w/Ancient_brew), [Forgotten brew](https://oldschool.runescape.wiki/w/Forgotten_brew) |
| 6.6 | Magic cape | [Magic cape](https://oldschool.runescape.wiki/w/Magic_cape) | `skillcape_boost.rs2` has no `skillcape_magic` branch. The cape also gives unlimited spellbook switches — a third consumer of §1.1 |
| 6.7 | Magic skill guide | [Magic](https://oldschool.runescape.wiki/w/Magic) | `interface_skill_guide/` exists; check the Magic page is populated |
| 6.8 | Powered staves | [Trident of the seas](https://oldschool.runescape.wiki/w/Trident_of_the_seas) | `tots`, `toxic_tots_charged`, `sanguinesti_staff`, `merfolk_trident`, the three `nightmare_staff_*` orbs — these cast *without* a spellbook click and have their own charge/max-hit rules. See [`powered-staff-projectiles`] |

---

## 7. S6 — Items

| Group | Cache names | Wiki | Status |
|---|---|---|---|
| Runes | all present incl. `mistrune` `dustrune` `mudrune` `smokerune` `steamrune` `lavarune` `astralrune` `wrathrune` | [Rune](https://oldschool.runescape.wiki/w/Rune) | present, combination runes must satisfy **both** elements in `~staff_runes` |
| Combination battlestaves | `mud_/steam_/smoke_/mist_/dust_battlestaff` + `mystic_*` | [Battlestaff](https://oldschool.runescape.wiki/w/Battlestaff) | **missing from `magic_staff.dbrow`** (§1.7) |
| Tomes | `tome_of_water` `tome_of_earth` (+ `_uncharged`) | [Tome of water](https://oldschool.runescape.wiki/w/Tome_of_water) | missing; `tome_of_fire` is done |
| Teleport tablets | `poh_tablet_*teleport` (8007–8013) · `teletab_*` (19613+) · `nzone_teletab_*` | [Teleport tablet](https://oldschool.runescape.wiki/w/Teleport_tablet) | **zero references in the tree** — no tablet does anything. Each needs an `[opheld1]` and, for the POH set, a lectern to make them |
| Ancient sceptres | `ancient_sceptre_blood` (already read by `~barrage_leech`) + the ice/smoke/shadow variants | [Ancient sceptre](https://oldschool.runescape.wiki/w/Ancient_sceptre) | one of four wired |
| Bloodbark | `bloodbark_{helm,body,legs,gauntlets,greaves}` | [Bloodbark armour](https://oldschool.runescape.wiki/w/Bloodbark_armour) | done (`~bloodbark_pieces_worn`) |
| Ensouled heads | `arceuus_corpse_*` | [Ensouled head](https://oldschool.runescape.wiki/w/Ensouled_head) | present, unused |
| Charge orbs | unpowered orb → air/water/earth/fire orb | [Charge Orb](https://oldschool.runescape.wiki/w/Charge_Air_Orb) | done |
| Iban's staff | `%iban_staff_charges` | [Iban's staff](https://oldschool.runescape.wiki/w/Iban%27s_staff) | done (M4) |
| Seal of passage | `lunar_seal_of_passage` | [Seal of passage](https://oldschool.runescape.wiki/w/Seal_of_passage) | granted by the quest; the Lunar Isle gate is not enforced |

---

## 8. S7 — Locs and NPCs

| Kind | Cache name | Purpose | Wiki |
|---|---|---|---|
| loc | `dt_zaros_altar` (6552) | Ancient book switch | [Altar (Jaldraocht Pyramid)](https://oldschool.runescape.wiki/w/Altar_(Jaldraocht_Pyramid)) |
| loc | `arceuus_altar` (28455) | Arceuus book switch | [Dark Altar](https://oldschool.runescape.wiki/w/Dark_Altar) |
| loc | `astral_altar` (34771) | Astral runecrafting; possible Lunar switch — **verify** | [Astral altar](https://oldschool.runescape.wiki/w/Astral_altar) |
| loc | *(name lookup needed)* | Lunar book switch on Lunar Isle | [Lunar Isle](https://oldschool.runescape.wiki/w/Lunar_Isle) |
| loc | `poh_altar_occult{,_standard,_ancient,_lunar,_arceuus}` (31858–61) | All-four switch in POH | [Altar of the occult](https://oldschool.runescape.wiki/w/Altar_of_the_occult) |
| loc | `poh_lectern_*` | Make teleport tablets | [Lectern](https://oldschool.runescape.wiki/w/Lectern) |
| loc | obelisks | Charge Orb — **done** | [Obelisk of Air](https://oldschool.runescape.wiki/w/Obelisk_of_Air) |
| npc | `arceuus_reanimated_*` (7018+) | Reanimation targets | [Reanimation](https://oldschool.runescape.wiki/w/Reanimation) |
| npc | `arceuus_thrall_*` (10878–86) | Thralls | [Thrall](https://oldschool.runescape.wiki/w/Thrall) |
| npc | `lunar_oneiromancer` (3835) | Lunar Diplomacy / Dream | [Oneiromancer](https://oldschool.runescape.wiki/w/Oneiromancer) |
| npc | `azzanadra` (1973) / `azzanadra_real` (730) | Desert Treasure ancient unlock | [Azzanadra](https://oldschool.runescape.wiki/w/Azzanadra) |
| npc | Mage of Zamorak / Ourania | Ourania Teleport destination | [Ourania Altar](https://oldschool.runescape.wiki/w/Ourania_Altar) |

---

## 9. Engine / host gaps

| Gap | Blocks | Disposition |
|---|---|---|
| `SS_OP_P_OPPLAYERT` (2084) declared, **not hosted**; `TORIRSSERVER_PLAYER_MAX = 1` | 15 spells (§1.6) | **blocked → skills queue #2.** Build the rows + graphics + costs; degrade the target |
| IF3 `name=` dropped before the client | tooltips/minimenu on all 132 | **P0**, register from CS2 ([`if3-opbase-and-target-hooks-dropped`]) |
| `~pvm_stat_change_effect` is an empty `return;` while `NPC_STATSUB` is live | 6 live spells + Shadow ×4 + corruption ×2 | **not an engine gap any more** — content fix (§1.4) |
| Multiway not modelled (`~player_in_combat_check` returns `true`) | AoE burst/barrage should be single-target outside multi | documented in-file; decide whether to model or keep |
| Poison system reachability from a spell | Smoke ×4 | check `skill_combat/configs/poison.varp` exposes an apply proc |
| `%spellbook` write path | 115 components | **verify first** (§1.1); expected to work |
| Wilderness/teleblock gate is per-script | every teleport | consolidate in §1.5 before adding 25 more teleports |

---

## 10. Verification

Per [`serverscript-guard-testing-confounds`] and
[`verify-blocker-and-failing-test`] — a green suite that never exercised the new
path proves nothing. For each slice:

```
make -C src torirsserver-scripts          # after EVERY config change
./src/build/ToriRSServer_Pack --check-only # expect 0 errors
make -C src test-ToriRSServer              # re-depends on torirsserver-scripts
```

Then a real cast, not just a compile:

- Extend `selftest_cast.rs2` — it already drives `apnpct` / `aploct` / `opobjt`
  / `opheldt` against `magic_spellbook` components, so a new spell's trigger
  form is testable without a client.
- `gear_selftest.rs2` already asserts `~magic_spell_maxhit(^ice_barrage)` and
  `~barrage_leech`. Every new max-hit / effect constant belongs there.
- **Prove each assertion can fail** by mutating the implementation, not the
  constant.
- Headless UI: per [`headless-ui-testing`], the spellbook switch is worth a
  screenshot — a book that redraws is the only proof `%spellbook` transmitted.

---

## 11. Slice order

Deps-first. Each row is one agent-loop claim.

| # | Slice | Blocks | Size |
|---|---|---|---|
| **P0a** | `%spellbook` write + 4 altar switch locs (§1.1) | 115 components | S |
| **P0b** | CS2 spell-name registration (§1.2) | all 132 | M |
| **P1a** | `~pvm_stat_change_effect` → `npc_statsub` (§1.4) | 12 spells | XS |
| **P1b** | Shared teleport gate proc (§1.5) | 25 teleports | S |
| **P1c** | `magic_staff.dbrow` combination staves + tomes (§1.7) | correctness of every cast | S |
| **S1** | Standard remainder — 17 (§2) | — | M |
| **S2a** | Ancient combat 4×4 — 14 (§3.1) | poison/drain/leech procs | L |
| **S2b** | Ancient teleports — 9 (§3.2) | P1b | S |
| **S3a** | Lunar teleports + Vengeance + Spellbook Swap (§4.2–4.3) | P0a, P1b | M |
| **S3b** | Lunar utility/skilling — 17 (§4.1) | touches 7 other skills | XL |
| **S3c** | Lunar shared buffs — 10 (§4.2) | §1.6 blocked → degraded | M |
| **S4a** | Arceuus teleports — 13 (§5.4) | P1b | M |
| **S4b** | Arceuus combat/utility — 19 (§5.3) | P1a, demon param | L |
| **S4c** | Reanimation — 4 + head table (§5.1) | npc lifecycle | L |
| **S4d** | Thralls — 9 (§5.2) | follower lifetime | L |
| **S5** | Casting infrastructure (§6) | — | L |
| **S6** | Teleport tablets + lecterns (§7) | P1b | M |

Mirror `SKILLS_CONTENT_PORT_QUEUE.md` #30–36 as each closes; retire
`MAGIC_CONTENT_PORT_PLAN.md` M7–M10 in favour of S2–S4 and M10 in favour of
§2 (surges) + §6.6 (cape).

---

## 12. Open questions to resolve before authoring

1. **`teleport_boat_to_me` / `teleport_me_to_boat`** — no standard-book spell
   maps to these. Leagues-only?
2. **`necromancy_dog`** (`targetverb=Reanimate`) — not a live Arceuus spell.
3. **`monster_inspect`** vs the lunar `monster_examine` — two components, one
   wiki spell.
4. **`transmute_upgrade` / `transmute_downgrade`** — the `op9=Warnings
   op10=Swap-Effect` shape reads as a relic control, not a spell.
5. **`%spellbook_sublist`** (`varbit9730`) — what the sub-list selects at this
   revision, and whether any book here uses it.
6. **The Lunar switch loc name** — not found by any obvious search.
7. **`zarosteleport1`–`8` ordering** — assumed Paddewwa→Ghorrock by level;
   confirm against the book's draw order, not the component index.

## Log

- plan created 2026-08-16: measured 132/211 unbound spellbook components. Found
  three stale premises in `MAGIC_CONTENT_PORT_PLAN.md` — `npc_statsub` is hosted
  (six live spells drain nothing), `%spellbook`/varbit 4070 is the switch the
  Desert Treasure deferral said did not exist, and every ancient/lunar/arceuus
  asset is in the cache under a findable name. Emitted P0a–S6.
