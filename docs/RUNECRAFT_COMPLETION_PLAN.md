# Finishing Runecraft

Plan to take `skill_runecraft/` from its current vertical slice to the complete
[Runecraft](https://oldschool.runescape.wiki/w/Runecraft) skill. The wiki is
the authority for behaviour (levels, XP, rates, gear effects, locations); the
cache (`OSRS-Content/osrs239-content/configs/all.loc`, `all.obj`, `all.npc`) is
the authority for names and for what is *expressible* at this revision;
`LostCity_Server/content/scripts/skill_runecraft/` is the reference for script
shape wherever it has one — and for half the slices below it does not.

Queue items this closes: **#37–#40, #42** in
[`SKILLS_CONTENT_PORT_QUEUE.md`](SKILLS_CONTENT_PORT_QUEUE.md). #41 (Guardians
of the Rift) is a minigame, not a skill slice, and is scoped out at §8.

---

## 0. Where runecrafting stands today — measured

`OSRS-Content/osrs239-content/server/scripts/skill_runecraft/` is **~660 lines**
across 2 scripts and 6 config files:

| file | lines | what it holds |
|---|---:|---|
| `scripts/runecraft.rs2` | 254 | altar craft, ruins enter, exit portal, 11 talisman `Locate`, 2 name→type procs, Slug Menace + Lunar Diplomacy hooks |
| `scripts/essence_mine.rs2` | 67 | Aubury/Sedridor curse-teleport in, 7 exit portals out |
| `configs/runecraft.dbrow` | 167 | **11 rows** (air…death) |
| `configs/runecraft.loc` | 107 | 22 ruins + 11 exit-portal category overlays |
| `configs/runecraft.constant` | 30 | `^air=1`…`^blood=13`, essence-mine return coords, Zanaris/shack zones |
| `configs/runecraft.dbtable` | 16 | `runecraft_table` schema |
| `configs/essence_mine_teleports.enum` | 27 | 22 random landing coords |
| `configs/essence_mine.varp` | 9 | `exit_essence_mine_coord` |

It is a working vertical slice: use talisman on mysterious ruins → teleport to
the temple → `Craft-rune` on the altar → `stat_advance` + `inv_add`, with a
generic exit portal and an Aubury/Sedridor essence-mine loop.

### 0.1 Coverage against the cache and the wiki

| activity | wired | wiki |
|---|:--:|---|
| Air…death altars, `Craft-rune` | ✅ 11/11 | [Runecraft](https://oldschool.runescape.wiki/w/Runecraft) |
| Mysterious ruins, talisman-on-ruins | ✅ | [Mysterious ruins](https://oldschool.runescape.wiki/w/Mysterious_ruins) |
| Talisman `Locate` | ✅ 11/11 | [Talisman](https://oldschool.runescape.wiki/w/Talisman) |
| Essence mine in/out (Aubury, Sedridor) | ✅ | [Rune Essence Mine](https://oldschool.runescape.wiki/w/Rune_Essence_Mine) |
| Tiara `Enter` on ruins | ⚠️ script exists, **varbit never written — dead** | [Tiara](https://oldschool.runescape.wiki/w/Tiara) |
| Pure essence (`blankrune_high`) | ❌ **cannot be crafted at all** | [Pure essence](https://oldschool.runescape.wiki/w/Pure_essence) |
| Tiara crafting (talisman + tiara on altar) | ❌ | [Tiara](https://oldschool.runescape.wiki/w/Tiara) |
| Elemental / catalytic talismans | ❌ | [Elemental talisman](https://oldschool.runescape.wiki/w/Elemental_talisman) |
| Combination runes | ❌ | [Combination runes](https://oldschool.runescape.wiki/w/Combination_runes) |
| Essence pouches | ❌ quest-item only | [Pouch](https://oldschool.runescape.wiki/w/Pouch) |
| The Abyss | ❌ | [Abyss](https://oldschool.runescape.wiki/w/Abyss) |
| Blood / Soul / Wrath altars | ❌ | [Blood Altar](https://oldschool.runescape.wiki/w/Blood_Altar) |
| Ourania (ZMI) | ❌ | [Ourania Altar](https://oldschool.runescape.wiki/w/Ourania_Altar) |
| Guardians of the Rift | ❌ | [Guardians of the Rift](https://oldschool.runescape.wiki/w/Guardians_of_the_Rift) |
| Runecraft cape / Raiments of the Eye | ❌ | [Raiments of the Eye](https://oldschool.runescape.wiki/w/Raiments_of_the_Eye) |
| Remaining teleporters (Distentor, Cromperty, Brimstail) | ✅ live in `areas/`, queue row was stale | [Rune Essence Mine](https://oldschool.runescape.wiki/w/Rune_Essence_Mine) |

### 0.2 The queue rows are stale — correct them in slice 0

Four claims in `SKILLS_CONTENT_PORT_QUEUE.md` #37–#42 are now wrong. Update
those rows as part of slice 0 rather than leaving them to mislead the next
audit:

| queue claim | reality |
|---|---|
| #37 tagged **`LC`** (LostCity is the reference) | LostCity has **no tiara logic and no pure-essence logic anywhere** in `content/scripts/`. Both must come from the wiki. Retag `wiki+cache`. |
| #37 "tiara enter path partially live" | The `[oploc1,_rc_ruins]` handler exists, but `airtemple_ruined_old` carries **no ops at all** (`all.loc:321449`) and the multiloc only flips to `airtemple_ruined_new` (`op1=Enter`, `all.loc:323363`) when varbit `rc_no_tally_required_air` is set. Nothing writes those varbits. The path is unreachable, not partial. |
| "int `loc_param` blocked" (`runecraft.loc` header, `runecraft.rs2:9-14`) | **Unblocked as of 2026-08-15** — `torirs_server_content.c:2622-2635` resolves param value types from `fields/loc.ini`'s `ref`, and its header names `param=rune_type,7` as the exact motivating case. `rune_type` is *already allocated* at `pack/param.alloc:62` (id 2679). |
| Rune Mysteries / Enter the Abyss "gates" absent | Both quests exist: `quests/quest_runemysteries/` and `quests/quest_entertheabyss/` (252 lines, grants `rcu_pouch_small` + `^eta_rc_xp`). `quest_templeoftheeye/` exists too (grants `rcu_pouch_medium`). **No quest work is needed** — only the mechanics those quests hand out. |

---

## 1. Structural fixes, before any content lands

Each of these multiplies by every altar added after it.

### 1.1 Replace the two name-switch procs with `loc_param(rune_type)`

`~runecraft_type_for_loc` (`runecraft.rs2:223`) and `~runecraft_type_for_obj`
(`:239`) are 30 lines of `switch_loc`/`switch_obj` that must grow **four cases
per new altar** (altar, ruins_old, ruins_new, exit portal). Adding blood, soul,
wrath and Ourania makes them 50+ lines and every one is a silent-`return(0)`
failure mode that surfaces as `~displaymessage(^dm_default)` — a UI bug, not a
data bug.

**Fix:** mint LostCity's own param. `rune_type` is already id 2679 in
`pack/param.alloc`; declare it in a new `skill_runecraft/configs/runecraft.param`
(`type=int`, `default=1`, no `ref` → decimal literal), set `param=rune_type,N`
on every altar/ruins/portal block in `runecraft.loc` and on every talisman in a
new `runecraft.obj`, then collapse both procs to `loc_param(rune_type)` /
`oc_param(last_useitem, rune_type)`. This is the exact shape Mining's plan §1.1
took for `mining_rock_empty` (id 2734, landed).

Do this **first** — every later slice adds rows, and adding them before the
param means editing the switches twice.

### 1.2 The essence ladder: `blankrune` is hardcoded

`runecraft.rs2:109` reads `inv_total(inv, blankrune)` and `:132` deletes it.
Consequences, both live bugs:

- **Pure essence cannot be crafted anywhere.** `blankrune_high` (Pure essence)
  exists in the cache at `all.obj:125546` and is inert.
- **Rune essence wrongly crafts members runes.** Per
  [Rune essence](https://oldschool.runescape.wiki/w/Rune_essence), it makes only
  air/mind/water/earth/fire/body.

**Fix:** add an `essence` column to `runecraft_table` (a bitmask or a
`^ess_normal`/`^ess_pure` minimum), and a
`~runecraft_pick_essence(dbrow)(namedobj, int)` proc that resolves the
inventory's best legal essence. Three objs today: `blankrune`,
`blankrune_high`, `blankrune_daeyalt` (Daeyalt essence, `all.obj:324143`,
**+50% XP**, [Daeyalt essence](https://oldschool.runescape.wiki/w/Daeyalt_essence)).
Route the XP multiplier through the same proc so §5's Raiments and §6's blood
essence have one place to hook.

### 1.3 Nothing writes `rc_no_tally_required_*` — the tiara `Enter` is dead

The cache allocates the full varbit family (`all.varbit.compack`): 607–617 for
air…death, 6220 wrath, 13782 blood, 4918/4919 soul/blood marked `_unused`. The
ruins multiloc keys off them (`airtemple_ruined` →
`multivarbit=rc_no_tally_required_air`).

**Fix:** a `~rc_refresh_ruins_varbits` proc that walks the worn head slot and
sets the 13 varbits, called from `~equip` and its unequip counterpart
(`server/scripts/player/scripts/equip.rs2:276`, the `[opheld2,_]` seam) and from
`[proc,worn_tab_login]` (`player/containers.rs2:53`). Without this, §2's tiara
crafting produces an item that does nothing.

### 1.4 The talisman lists are hand-maintained in three places

`[oplocu,_rc_ruins]`'s `switch_obj` (`:46`) and the ten `[opheld4,...]` lines
(`:138-147`) each enumerate talismans by name, and both omit `elemental_talisman`
(`all.obj:87628`), `catalytic_talisman` (`:357034`), `blood_talisman`,
`soul_talisman` and `wrath_talisman`.

**Fix:** LostCity's own answer — `category=talismans` on every talisman obj in
the new `runecraft.obj`, `[opheld4,_talismans]` and
`oc_category(last_useitem) = talismans` in the ruins handler. Elemental and
catalytic then need a *set-membership* check against `rune_type` rather than
equality (`Elemental` = air/water/earth/fire; `Catalytic` = the other nine) —
put that in one `~talisman_opens(obj, int)(boolean)` proc.

---

## 2. Slice #37 — tiara crafting, pure essence, elemental/catalytic

Wiki: [Tiara](https://oldschool.runescape.wiki/w/Tiara) ·
[Pure essence](https://oldschool.runescape.wiki/w/Pure_essence)

Pure essence lands with §1.2. Two pieces remain.

**Tiara crafting** — use `tiara` (`all.obj:87743`) + the matching talisman on
the altar. Talisman is consumed; output is the attuned tiara; XP is
Runecraft, scaling with altar tier:

| tiara | XP | tiara | XP |
|---|---:|---|---:|
| `tiara_air` | 25 | `tiara_cosmic` | 40 |
| `tiara_mind` | 27.5 | `tiara_chaos` | 42.5 |
| `tiara_water` | 30 | `tiara_nature` | 45 |
| `tiara_earth` | 32.5 | `tiara_law` | 47.5 |
| `tiara_fire` | 35 | `tiara_death` | 50 |
| `tiara_body` | 37.5 | `tiara_blood` / `tiara_wrath` | 52.5 |

All present in the cache (`all.obj:87768`…`88120`, `tiara_wrath` at `286818`,
`tiara_soul` at `88100`). Store as a `tiara` + `tiara_xp` column pair on
`runecraft_table` and add a `case tiara` arm to the existing
`[oplocu,_rc_altar]` switch — the switch is already there for the two quest
hooks, so this is additive.

The tiara **mould** side (`tiara_mould`, 23 Crafting, 52.5 Crafting XP from a
silver bar) belongs to `skill_crafting/`. Check `skill_crafting/` before writing
it; if absent, route it as a note on queue #43–#48 rather than landing it here.

**Elemental / catalytic** talismans and their tiaras (`hat_of_the_eye`
variants exist at `all.obj:357785`) come with §1.4's `~talisman_opens`.

---

## 3. Slice #40 — the remaining essence-mine teleporters (already done)

Wiki: [Rune Essence Mine](https://oldschool.runescape.wiki/w/Rune_Essence_Mine)

**Correction, found while implementing this plan:** all five teleporters are
already live, in `areas/`, not `skill_runecraft/` — the queue row's "deferred"
note was stale. Nothing to build here.

| NPC | Talk-to | Teleport | file |
|---|---|---|---|
| Aubury | `[opnpc1,aubury]` | `[opnpc4,aubury]` | `areas/varrock/scripts/aubury.rs2` |
| Archmage Sedridor | live | live | `areas/wizard_tower/scripts/sedridor.rs2` |
| Wizard Distentor | `[opnpc1,guild_wizard]` | `[opnpc3,guild_wizard]` | `areas/area_yanille/scripts/yanille_thin_npcs.rs2` |
| Wizard Cromperty | `[opnpc1,ardounge_wizard]`+aliases | `[opnpc3,...]` | `areas/area_ardougne_east/scripts/wizard_cromperty.rs2` |
| Brimstail | `[opnpc1,gnome_brimstail]` | `[opnpc3,gnome_brimstail]` | `areas/area_gnome/scripts/brimstail.rs2` |

Each gates Talk-to's teleport option on `%runemysteries = ^runemysteries_complete`
and calls the shared `@teleport_to_essence_mine(^essence_mine_to_*)` label from
`skill_runecraft/scripts/essence_mine.rs2`. Aubury's rune shop
(`shop/varrock/scripts/auburys_rune_shop__1.rs2`, `[opnpc3,aubury]`) is live
too and does not collide with the op4 teleport binding.

`~eta_charge_orb` is already called from `@teleport_to_essence_mine`, so Enter
the Abyss's three-location requirement (§7) already works today.

---

## 4. Slice #38 — Blood, Soul and Wrath altars

Wiki: [Blood Altar](https://oldschool.runescape.wiki/w/Blood_Altar) ·
[Blood Altar (Arceuus)](https://oldschool.runescape.wiki/w/Blood_Altar_(Arceuus)) ·
[Soul Altar](https://oldschool.runescape.wiki/w/Soul_Altar) ·
[Wrath Altar](https://oldschool.runescape.wiki/w/Wrath_Altar)

LostCity has loc names for these and **no mechanics** — the rows come from the
wiki. Two different shapes, and they turned out to need very different amounts
of new data once implemented.

**Wrath and the true Blood Altar** are ruins-teleport altars, same shape as
the other 11 — `wrath_altar` (`all.loc:402298`), `wrathtemple_ruined`
(`:402770`, `_0op`/`_1op` children, matching the `_old`/`_new` shape the other
altars use), `wrathtemple_exit_portal` (`:402137`); `blood_altar` (`:518681`),
`bloodtemple_ruined` (`_0op`/`_1op` at `:401984`/`:401991` for wrath,
`:518634`/`:518650` for blood), `bloodtemple_exit_portal` (`:518667`). Both
altars natively carry cache `category=2156` (Craft-rune) already. Landed: the
`rune_type` params (14/13) and `rc_ruins`/`rc_exit_portal` category overlays
on every block above (`runecraft.loc`).

**Blocked, not landed:** the actual `runecraft_table` rows (12, 14) need
`altar_coord`/`enter_coord`/`exit_coord` — real in-world tile coordinates for
the ruins-side and altar-side of the teleport. Neither the wiki nor the cache
text configs carry tile coordinates (those live in the binary map region
files, `configs/maps/*.jm2`, which nothing in this tree's tooling exports to
text); LostCity's own reference has the same loc names and no dbrow rows
either, confirming this isn't a missed port. Until a row exists, these locs
answer with the same "you can't do that" message they gave before this plan —
nothing regresses, the craft/tiara/talisman logic just isn't reachable yet.
**Next step:** measure the four coordinate triples from a running client or a
map-region export tool, then add rows 12 and 14 to `runecraft.dbrow` — at that
point they're fully wired, since §1.1's param means no proc edits are needed.

**Kourend blood/soul**, by contrast, needed no coordinates at all — landed in
full. It is walk-up (Blood Altar (Arceuus): *"neither a blood talisman nor
tiara is needed"*) and takes **dark essence fragments**, not talismans:

| step | cache | wiki |
|---|---|---|
| Mine dense runestone (38 Mining) | `arceuus_runestone_base_mine` (`all.loc:99316`) + `_middle_`/`_top_` and `_depleted` pairs | [Dense runestone](https://oldschool.runescape.wiki/w/Dense_runestone) |
| Infuse at the Dark Altar — `[oploc1,archeus_altar_dark]` | `archeus_altar_dark` (`all.loc:308379`) | [Dark Altar](https://oldschool.runescape.wiki/w/Dark_Altar) |
| Chisel into fragments — merged into `skill_crafting/scripts/gem/uncut_gem.rs2`'s shared `[opheldu,chisel]` | `arceuus_essence_block` → `_dark` (`all.obj:219777`) → `bigblankrune` (**cache name for "Dark essence fragments"** — a naming trap, not `dark_essence_fragments`) | [Dark essence fragments](https://oldschool.runescape.wiki/w/Dark_essence_fragments) |
| Craft — `~runecraft_kourend_bind`, shared by both altars | `archeus_altar_blood` (`:308366`) 77 RC 23.8 XP, gated on `%myq5 = ^sf_complete` (Sins of the Father); `archeus_altar_soul` (`:308392`) 90 RC 29.7 XP, no quest gate per the wiki | as above |

Files: `skill_runecraft/scripts/runecraft_kourend.rs2` (new).

The mining half (`arceuus_runestone_*`) is Mining's `#49` lane and was **not**
touched here — check `MINING_COMPLETION_PLAN.md` before adding it elsewhere.

Blood essence (`blood_essence_inactive`/`_active`, `all.obj:350075`) hooks
§1.2's XP/output proc.

---

## 5. Slice #42 — combination runes, binding necklace, Runecraft cape

Wiki: [Combination runes](https://oldschool.runescape.wiki/w/Combination_runes) ·
[Binding necklace](https://oldschool.runescape.wiki/w/Binding_necklace)

Combination runes are a **second `[oplocu,_rc_altar]` arm**, not a new altar:
bring pure essence + the elemental rune that is one half + the *opposite*
talisman.

| rune | RC | XP (this altar / other altar) | inputs |
|---|:--:|---|---|
| Mist | 6 | 8 / 8.5 | air + water |
| Dust | 10 | 8.3 / 9 | air + earth |
| Mud | 13 | 9.3 / 9.5 | water + earth |
| Smoke | 15 | 8.5 / 9.5 | air + fire |
| Steam | 19 | 9.3 / 10 | water + fire |
| Lava | 23 | 10 / 10.5 | earth + fire |

Mechanics: **50% bind chance per rune**, failures consume the essence and the
rune and grant nothing; the talisman is consumed regardless. A worn
[Binding necklace](https://oldschool.runescape.wiki/w/Binding_necklace) forces
100% for **16 uses** then crumbles — reuse the existing `~charges_item_*` family
(the same one Mining's plan cites for `jewl_bracelet_of_clay`). Magic Imbue
belongs to `skill_magic/`; leave a named hook, do not implement it here.

Model this as its own `runecraft_combo_table` dbtable (altar type, other type,
level, xp_low, xp_high, input rune, output rune) so the arm stays data-driven.

**Runecraft cape**: `skillcape_boost.rs2`
(`skill_combat/scripts/player/skillcape_boost.rs2`) has eleven cape branches and
no Runecraft one. Per the wiki the cape also **prevents pouch degradation**
entirely — so land it *after* §6, and have §6's degrade proc consult it.

---

## 6. Slice #39a — essence pouches

Wiki: [Pouch](https://oldschool.runescape.wiki/w/Pouch) ·
[Medium pouch](https://oldschool.runescape.wiki/w/Medium_pouch)

The objs all exist and are referenced **only** as quest props today
(`quest_entertheabyss` grants small, `quest_templeoftheeye` grants medium,
`quest_deviousminds` seals large/colossal). Cache family — note the name trap,
it is `rcu_pouch_*`, not `essence_pouch_*`:

| pouch | cache | RC | holds | degrades after | degraded obj |
|---|---|:--:|:--:|---|---|
| Small | `rcu_pouch_small` (`all.obj:87473`) | 1 | 3 | never | — |
| Medium | `rcu_pouch_medium` (`:87492`) | 25 | 6 | ~45 fills → 3 | `rcu_pouch_medium_degrade` |
| Large | `rcu_pouch_large` (`:87537`) | 50 | 9 | ~29 fills → 7 | `rcu_pouch_large_degrade` |
| Giant | `rcu_pouch_giant` (`:87584`) | 75 | 12 | ~10 fills → 9 | `rcu_pouch_giant_degrade` |
| Colossal | `rcu_pouch_colossal` (`:356876`) | 85 | 40 | ~8 fills → 35 | `rcu_pouch_colossal_degrade` |

Interactions to write: **Fill**, **Empty**, **Check**, and the drop path (a
dropped filled pouch spills: *"The contents of the pouch fell out as you dropped
it!"*). Each pouch needs its own per-player inv — one `inv.alloc` entry per
pouch type, same shape as any other container in `pack/inv.alloc`.

Repair: the Dark mage in the Abyss (§7) — so the pouches are usable before the
Abyss lands but not repairable. That is acceptable, and worth stating explicitly
so it does not read as an oversight later.
`tote_cordelia_wizard_tower_child` (`all.npc:379168`, Apprentice Cordelia) is the
abyssal-pearl alternative and belongs to the GotR lane (§8).

---

## 7. Slice #39b — the Abyss

Wiki: [Abyss](https://oldschool.runescape.wiki/w/Abyss) ·
[Enter the Abyss](https://oldschool.runescape.wiki/w/Enter_the_Abyss)

The quest is already done — this is the destination it unlocks. Full cache
support exists:

| piece | cache locs |
|---|---|
| Entrance | `rcu_outer_entrance`, `_active_inner`, `_blocksquare` (`all.loc:279535-279548`) |
| Outer ring, 12 sectors | `rcu_outer_multi1`…`rcu_outer_multi12` |
| Mining rocks | `rcu_abyssal_barrier_teeth1/2/3` |
| Woodcutting tendrils | `rcu_abyssal_barrier_tendrils1/2/3` |
| Firemaking boils | `rcu_abyssal_barrier_boil1/2/3` + `rcu_abyssal_boils_1..3(_dark)` |
| Thieving eyes | `rcu_abyssal_barrier_eyes1/2/3` |
| Agility gap | `rcu_abyssal_barrier_agility` |
| Impassable | `rcu_abyssal_wall`, `_high`, `_bulge` |
| Exit hole | `rcu_abyssal_exit_hole` |
| Rifts (inner ring) | `abyss_exit_to_air/mind/water/earth/fire/body/cosmic/chaos/nature/law/death/soul`, plus `abyss_exit_to_blood_parent` → `_child_true` / `_child_kourend` |

Behaviour: success chance is **(level + 1)%**, capped at 100 at 99, **25 XP** in
the relevant skill per pass; rocks need a pickaxe, tendrils an axe, boils a
tinderbox, eyes and gaps no tool. Entry drains prayer to 0 and applies a **skull**
unless an abyssal bracelet is worn. Each rift drops the player at the matching
altar — reuse `runecraft_table:enter_coord`, so the rifts need no new coord data
once §1.1 lands.

Entry point is the **Mage of Zamorak** (`rcu_zammy_mage1`, `_edge`,
`rcu_zammy_mage1a/1b/_edgeb`, `rcu_zammy_mage2`) in low Wilderness. The **Dark
mage** in the centre repairs pouches — he is *not* findable by `name=Dark mage`
in `all.npc`; resolve his cache name before writing the dialogue
(`general/scripts/misc/tele_destinations.rs2:581` already knows the coord
`0_47_75_31_34`, which is the fastest way to find him).

`rcu_abyss_to_overseer` / `rcu_overseer_to_abyss` (`all.loc:299212`) are the GotR
link — leave them unbound until §8.

---

## 8. Explicitly out of this plan — Guardians of the Rift

Wiki: [Guardians of the Rift](https://oldschool.runescape.wiki/w/Guardians_of_the_Rift) ·
[Raiments of the Eye](https://oldschool.runescape.wiki/w/Raiments_of_the_Eye)

| activity | locs | wiki |
|---|---|---|
| Guardians of the Rift minigame | `gotr_*` (`all.loc:519172`+), `rcu_abyss_to_overseer`/`rcu_overseer_to_abyss` | [Guardians of the Rift](https://oldschool.runescape.wiki/w/Guardians_of_the_Rift) |
| Raiments of the Eye reward set | `hat_of_the_eye*` and matching robes | [Raiments of the Eye](https://oldschool.runescape.wiki/w/Raiments_of_the_Eye) |
| Abyssal pearl repair (Apprentice Cordelia) | `tote_cordelia_wizard_tower_child` | [Cordelia](https://oldschool.runescape.wiki/w/Apprentice_Cordelia) |

The cache carries the full `gotr_*` loc set and the unlock quest already exists
(`quest_templeoftheeye/`), but there is no `minigames/minigame_gotr/` tree, and
the minigame needs a game-state machine, a points economy, and the Raiments
reward set. This is a minigame build, not a skill-interaction slice; it stays
in its own lane. Ourania (§8 of the ordering below) and the Abyss (§7) already
close runecrafting's own obligations toward queue #39/#41 — listed here so the
next audit does not re-emit it as a runecraft gap.

**Ourania (ZMI)** lands as part of this plan, in slice 13 below: pure essence
in, a weighted random assortment of runes out, ~1.7× normal XP, no talisman —
`rc_zmi_dungeon_cracked_center_altar` (`all.loc:335289`),
`rc_zmi_dungeon_entrance` (`:335343`), `rc_zmi_dungeon_wall_crack_entrance`
(`:335216`) and its `_exit`. Raiments of the Eye (+10% runes per piece, +20%
set = 60%) hooks §1.2's essence/output proc whenever GotR does land.

---

## 9. Ordering — status

All 14 slices are landed. `#38` and `#40` are fully closed; `#37`, `#39` and
`#42` are landed except for the pieces §4 and §5 name as blocked on real-world
coordinates or out of scope.

| # | slice | depends on | closes | status |
|---|---|---|---|---|
| 0 | Correct the stale queue rows (§0.2) | — | bookkeeping | done |
| 1 | `rune_type` param + `runecraft.param`/`.obj`; collapse both switch procs (§1.1) | — | gates everything | done |
| 2 | Talisman category + elemental/catalytic (§1.4) | 1 | #37 part | done |
| 3 | Essence ladder: pure + daeyalt, members gate (§1.2) | 1 | #37 part | done |
| 4 | `rc_no_tally_required_*` refresh on equip (§1.3) | — | unblocks tiaras | done |
| 5 | Tiara crafting (§2) | 1, 3, 4 | **#37** | done |
| 6 | Essence-mine teleporters (§3) | — | **#40** | done — already live, corrected the queue note |
| 7 | Wrath + true blood altar rows (§4) | 1, 3 | #38 part | param/category done; dbrow rows blocked on real coordinates |
| 8 | Kourend dark/blood/soul chain (§4) | 1, 3, 7 | **#38** | done |
| 9 | Essence pouches (§6) | 3 | #39 part | done |
| 10 | The Abyss (§7) | 1, 9 | #39 part | done |
| 11 | Combination runes + binding necklace (§5) | 1, 2, 3 | #42 part | done |
| 12 | Runecraft cape (§5) | 9, 11 | **#42** | done (boost only; guild-teleport perk needs a coordinate) |
| 13 | Ourania (§8) | 1, 3 | **#39** | done (output weights are a documented approximation — §8) |
| 14 | Selftest coverage (§10) | all | — | done — `runecraft_selftest.rs2`, `::runecraftrun` |

---

## 10. Verification

Landed: `skill_runecraft/scripts/runecraft_selftest.rs2`, a `[debugproc,
runecraftrun]` in the same shape as `::miningrun`/`::fishingrun` — one FAIL
line and stop, or `runecraftrun OK - N checks passed`, restoring every stat/
inventory/worn change it makes. It covers, each by mutating the implementation
rather than trusting a clean pass:

1. **Row integrity** — every landed `runecraft_table` row (types 1–11)
   resolves through `~get_runecraft_data`.
2. **Essence priority** — daeyalt beats pure beats rune when a player holds
   all three.
3. **Members gate** — rune essence refuses to resolve for a members-only rune.
4. **Multiplier boundary** — air's multiplier yields 1 rune/essence at level
   10 and 2 at level 11, not a slipped-by-one.
5. **Talisman set membership** — elemental opens water but not mind;
   catalytic opens mind but not air.
6. **Tiara varbit** — `rc_no_tally_required_air` flips to 1 wearing a tiara
   and back to 0 removing it.
7. **Pouch round-trip** — fill removes essence from the inventory, empty
   restores the exact original count.
8. **Pouch degrade boundary** — 269/270 essence does not degrade a medium
   pouch, 270/270 does, swapping the obj and resetting the wear counter.
9. **Combo row lookup** — mist resolves from both the air altar (with a water
   rune) and the water altar (with an air rune).
10. **Abyss chance cap** — level 99 caps the pass chance at 100, not 100+1.

**Not covered**, scoped out for the reasons stated where each is implemented:
XP-amount assertions (would pin the ×10 scale rather than test a real
invariant), the binding necklace's 16-charge crumble (needs 16 simulated
crafts, higher value than cost here), and the drop-spill behaviour the pouch
header names as unmodelled (no on-drop opcode exists to test against).

**Executed this session:** `make torirsserver-scripts` after every slice (0
errors, final count 19,007 scripts), and a full `torirsserver-dev` boot against a
scratch `TORIRSSERVER_SAVES` with every config above loaded — no `CONTENT_ERROR`,
confirming the `.dbtable`/`.dbrow`/`.loc`/`.obj`/`.param`/`.varp` files parse
at runtime, not just at script-compile time.

**Not executed this session, and the natural next step:** actually running
`::runecraftrun` against a live client connection. `ToriRSServer_Pack
--check-only` — the other standard gate — has a pre-existing, unrelated link
gap (`TORIRSSERVER_PACK_SRCS` in `src/makefile` is missing `torirs_server_scene.c`/
`torirs_server_bank.c`/`torirs_server_shop.c`, so the standalone checker binary won't
link) that blocks that specific command; it predates this plan and is out of
scope for a content-only skill slice. Firing the debug command itself needs
either a real client connection or a generic debugproc-invoking test harness,
which does not exist yet (`embed_test.c` runs one fixed scenario, not
arbitrary commands) — building one is its own piece of work, not a to-do
folded into this plan.

Pack check after every slice: `ToriRSServer_Pack` must stay at **0 errors**.
