# Finishing Fishing

Plan to take `skill_fishing/` from its current vertical slice to the complete
[Fishing](https://oldschool.runescape.wiki/w/Fishing) skill. The wiki is the
authority for content (fish, levels, XP, tools, bait, locations, gates); the
cache (`OSRS-Content/osrs239-content/configs/all.obj`, `all.npc`, `all.loc`) is
the authority for names and for what is *expressible* at this revision.

Queue items this closes: **#63–#68** in
[`SKILLS_CONTENT_PORT_QUEUE.md`](SKILLS_CONTENT_PORT_QUEUE.md). #69 (Trawler
remainder) and #70 (Tempoross / aerial / drift-net) stay blocked and are scoped
at the bottom. #71 (members fish *cookables*) is the Cooking-side twin and is
flagged where it couples.

---

## 0. Where fishing stands today — measured

`OSRS-Content/osrs239-content/server/scripts/skill_fishing/` is **1,161 lines**
across 8 scripts plus a **1,426-line** `configs/fishing.npc`. It is a working
vertical slice, not a stub: the four classic spot families, karambwan, slimy
eels and lava eels all fish end to end, with the LostCity tick cadence
(`%action_delay` vs `map_clock`), the `p_opnpc(4)/(5)` continue-op loop, and
the skilling sounds from [`SKILLING_SOUNDS.md`](SKILLING_SOUNDS.md) §4.3.

### 0.1 What is wired

| cache category | name | script | ops | fish | spawned spots |
|---|---|---|---|---|---:|
| 280 | `freshfish` | `fishing_spots/freshfish.rs2` | Lure / Bait | trout, salmon, pike | 80 |
| 281 | `rarefish` | `fishing_spots/rarefish.rs2` | Cage / Harpoon | lobster, tuna, swordfish | 46 |
| 282 | `memberfish` | `fishing_spots/memberfish.rs2` | Big Net / Harpoon | mackerel, cod, bass, shark + junk | 62 |
| 283 | `saltfish` | `fishing_spots/saltfish.rs2` | Small Net / Bait | shrimp, anchovies, sardine, herring | 51 |
| 457 | `slimeyfish` | `fishing_spots/slimeyfish.rs2` | Bait | slimy eel | 17 |
| 632 / 633 | `karambwanji` / `karambwan` | `fishing_spots/tbwt.rs2` | Small Net / vessel | karambwanji, karambwan | 3 / 5 |
| — (name-bound) | `0_45_152_lavafish` | `fishing_spots/lavafish.rs2` | Bait | lava eel | 4 |

Plus `fishing.rs2` (equipment/bait checks, roll, four rate switches) and
`fishing_guild.rs2` (door, Master Fisher, Roachey).

**16 fish** appear in `~fish_productexp`. OSRS has roughly **30**.

### 0.2 Adjacent fishing content that is already done

- **[Fishing Contest](https://oldschool.runescape.wiki/w/Fishing_Contest)** —
  `quests/quest_fishingcompo/` incl. `hemenster_fishing.rs2`, with the Land of
  the Goblins whitefish branch merged onto the shared sinister spot.
- **[Fishing Guild Shop](https://oldschool.runescape.wiki/w/Fishing_Guild_Shop)**
  — `shop/fishing_guild/`, generated, 22 stock lines, wired to `fishguildshop`
  op3.
- **Fishing spots do not walk** — all 161 spot records carry
  `moverestrict=nomove` + `wanderrange=0` (`configs/fishing.npc`). See
  [Fishing spot](https://oldschool.runescape.wiki/w/Fishing_spot).
- **[Fishing Trawler](https://oldschool.runescape.wiki/w/Fishing_Trawler)** —
  Murphy, hull/net/bail, gangplank, sink, win (`minigames/game_trawler/`);
  control timer blocked, see §5.

### 0.3 The spots for the missing content are **already on the map**

This is the reason the remaining work is mostly script, not data. Every one of
these is spawned and every click on one is currently dead:

| npc / category | ops | spawned | what it should be | wiki |
|---|---|---:|---|---|
| `swan_fishingspot` (cat 590) | Net, Harpoon | 5 | monkfish, Piscatoris | [Raw monkfish](https://oldschool.runescape.wiki/w/Raw_monkfish) |
| `piscariliusfish` (cat 919) | Bait | 3 | anglerfish, Port Piscarilius | [Raw anglerfish](https://oldschool.runescape.wiki/w/Raw_anglerfish) |
| `49_61_crabs`, `52_59_crabs` (cat 251) | Cage | 6 | dark crab, Resource Area | [Raw dark crab](https://oldschool.runescape.wiki/w/Raw_dark_crab) |
| `*_cavefish` ×5 (cat 360) | Small Net, Bait | 5 | giant frogspawn + cave eel | [Raw cave eel](https://oldschool.runescape.wiki/w/Raw_cave_eel) |
| `*_brut_fishing_spot` ×2 (cat 1174) | Use-rod | 7 | barbarian fishing | [Barbarian Fishing](https://oldschool.runescape.wiki/w/Barbarian_Fishing) |
| `morulrek_eels_fishingspot` | Bait | 5 | infernal eel, Mor Ul Rek | [Infernal eel](https://oldschool.runescape.wiki/w/Infernal_eel) |
| `snakeboss_fishingspot` | Bait | 2 | sacred eel, Zul-Andra | [Sacred eel](https://oldschool.runescape.wiki/w/Sacred_eel) |
| `0_48_48_newbiefishing`, `0_26_95_tut2_fishing` (cat 453) | Net | 6 | tutorial shrimp | [Fishing](https://oldschool.runescape.wiki/w/Fishing) |
| `minnow_fishingspot1..4` (cat 1137) | Small Net | 4 | minnows, Kourend platform | [Minnow](https://oldschool.runescape.wiki/w/Minnow) |
| `camdozaal_fishingspot_1..3` (cat 1533) | Small Net, Big Net | 3 | Camdozaal fish | [Ruins of Camdozaal](https://oldschool.runescape.wiki/w/Ruins_of_Camdozaal) |
| `fishing_spot_aerial` | Catch | 20 | aerial fishing | [Aerial fishing](https://oldschool.runescape.wiki/w/Aerial_fishing) |
| `fairy2_fakefish` | Small Net | 1 | Fairy Tale II prop | [Fairy Tale II](https://oldschool.runescape.wiki/w/Fairy_Tale_II_-_Cure_a_Queen) |
| `deal_squid` (cat 308) | Fish | 3 | Rum Deal prop | [Rum Deal](https://oldschool.runescape.wiki/w/Rum_Deal) |

The last four rows are out of the `skill_fishing/` lane — see §5.

### 0.4 Every item and npc the remaining work needs is already named

Checked against `configs/all.obj` / `all.npc` — no new naming pass is required
for anything in §2:

| thing | cache name | thing | cache name |
|---|---|---|---|
| Raw monkfish | `raw_monkfish` | Barbarian rod | `brut_fishing_rod` |
| Raw anglerfish | `raw_anglerfish` | Leaping trout | `brut_spawning_trout` |
| Sandworms | `piscarilius_sandworms` | Leaping salmon | `brut_spawning_salmon` |
| Raw dark crab | `raw_dark_crab` | Leaping sturgeon | `brut_sturgeon` |
| Dark fishing bait | `wilderness_fishing_bait` | Roe | `brut_roe` |
| Raw cave eel | `raw_cave_eel` | Caviar | `brut_caviar` |
| Giant frogspawn | `giant_frogspawn` | Fish offcuts | `brut_fish_cuts` |
| Sacred eel | `snakeboss_eel` | Fishing cape | `skillcape_fishing` |
| Infernal eel | `infernal_eel` | Fishing hood | `skillcape_fishing_hood` |
| Raw rainbow fish | `hunting_raw_fish_special` | Angler hat/top/waders/boots | `trawler_reward_hat` / `_top` / `_legs` / `_boots` |
| Minnow | `minnow` | Fish barrel / sack | `fish_barrel_closed` / `fish_sack` |
| Raw cavefish (Camdozaal) | `raw_cavefish` | Spirit flakes | `spirit_flakes` |

### 0.5 What has no coverage at all

- **No selftest touches fishing.** `grep -rn fishing server/scripts/selftest*.rs2`
  returns nothing. Every other slice below lands untested unless §3 lands too.
- **Cooking stops at swordfish.** `cooking_generic.dbrow` has no shark,
  monkfish, karambwan, anglerfish, dark crab, cave eel or bass row — so most of
  what §2 catches cannot be cooked. That is queue #71, not this plan, but the
  two should land together or the new fish are dead weight.

---

## 1. The blocker: rates and messages are five parallel switch tables

Fix this before any fish lands, because every new fish multiplies it.

`fishing.rs2` carries **four `switch_obj` ladders keyed on the same fish**
(`~fish_productexp`, `~fish_productmessage`, `~fish_success_low`,
`~fish_success_high`) plus three more keyed on the equipment
(`~fish_equipment_failmessage`, `~fish_bait_required`, `~fish_bait_message`)
and one on the spot (`~fish_wrong_spot_message`). Its own header says why:

> Fish rates/XP and equipment fail text are switches here — LostCity puts them
> in `fishing.struct` / `fishing_equipment.struct` + `oc_param`; this tree has
> no authored `.struct` overlays yet.

**That premise is now stale.** `skill_cooking/configs/gnome_cooking/gnome_cooking.struct`
is an authored struct overlay in this tree today. But structs are not the right
target either — the established idiom here is the DB table, and cooking already
demonstrates the exact shape this needs
(`cooking_generic.dbtable` / `.dbrow`, 18 columns, one row per recipe,
`successchance,int,int` for the low/high pair).

### 1.1 Land `fishing.dbtable` first

```
[fishing_catch]
column=product,namedobj,INDEXED,REQUIRED
column=levelrequired,int
column=levelrequired_message,string
column=experience,int
column=successchance,int,int
column=productmessage,string
column=equipment,namedobj
column=bait,namedobj
column=stackable,int          // minnows, karambwanji
column=agility_experience,int // barbarian fishing
column=strength_experience,int
```

Rows carry XP ×10, matching the tree's existing `stat_advance(fishing, 900)` =
90 XP convention (and `crops.dbrow` in Farming).

Without it, adding the 14 fish in §2 costs **14 × 4 = 56 switch arms across
four procs**, none of them checked against each other — the same failure shape
`FARMING_COMPLETION_PLAN.md` §1 documents for patch varps.

### 1.2 Two `~fish_roll` limitations to fix in the same pass

- **It only rolls two fish.** `~fish_roll($fish1, $fish2, …)` is hard-wired to a
  primary/secondary pair. Barbarian spots roll three
  ([leaping trout/salmon/sturgeon](https://oldschool.runescape.wiki/w/Barbarian_Fishing)),
  Camdozaal rolls four. Make it take a table key and walk rows descending by
  `levelrequired`.
- **`~fish_roll_big_net` duplicates the table inline.** Seven `stat_random` +
  `inv_add` + `mes` + `stat_advance` blocks in `memberfish.rs2` re-state XP and
  messages the switches already hold — mackerel appears in both, at 200, twice.
  Fold the junk table into `fishing.dbtable` rows with a `junk` flag.

### 1.3 Reconcile the ported rates against the wiki

The current numbers are LostCity's (a 2004 recreation), not necessarily OSRS's.
Spot-checks agree for shrimp/sardine/herring/trout/salmon/pike/tuna/lobster/
swordfish/shark/mackerel/cod/bass/slimy eel/lava eel. **These do not, and must
be checked before the table is frozen:**

| fish | tree value | wiki says | where |
|---|---:|---:|---|
| Raw karambwan | 1050 (105 XP) | 50 XP | `fishing.rs2:126` — [Raw karambwan](https://oldschool.runescape.wiki/w/Raw_karambwan) |
| big-net junk (boots/gloves/seaweed/oyster/casket) | 10/10/1/100/100 | verify each | `memberfish.rs2:125-161` — [Big fishing net](https://oldschool.runescape.wiki/w/Big_fishing_net) |
| cat 282 "Net/Harpoon" spots (6 of 31) | treated as big net | verify the Net op's tool | `memberfish.rs2:81` |

The third row is a live correctness question, not bookkeeping: `[opnpc1,_memberfish]`
calls `~check_fish_equipment(big_net)` for every cat-282 record, but six of them
label op1 `Net`, not `Big Net` — including the Fishing Guild spots at
`2601,3422`. If the wiki says those take a small net, six spots currently
demand the wrong tool.

---

## 2. The slices

Ordered by dependency. Each is one commit's worth.

### S1 — `fishing.dbtable` refactor  *(blocks S2–S9)*

Per §1. No behaviour change; the four switches become one table and
`~fish_roll` becomes table-driven. Verify by A/B: same fish, same XP, same
messages before and after.

### S2 — Monkfish (Piscatoris) — queue #63

- **npc**: bind cat **590** (`swan_fishingspot`, 5 spawns at Piscatoris Fishing Colony).
- **ops**: op1 `Net` → monkfish; op3 `Harpoon` → `~fishing_wrong_spot_message(harpoon)`; op4/op5 hidden continue ops.
- **item**: `small_fishing_net` (`net`) → `raw_monkfish`.
- **rates**: 62 Fishing, 120 XP. *(verify)*
- **gate**: [Swan Song](https://oldschool.runescape.wiki/w/Swan_Song) complete —
  `quest_swansong` is ported, gate on `^ssq_complete = 200`.
- **note**: the cache record carries a Harpoon op the wiki's monkfish spot does
  not list. Verify before assuming it is inert.
- **wiki**: [Raw monkfish](https://oldschool.runescape.wiki/w/Raw_monkfish) ·
  [Piscatoris Fishing Colony](https://oldschool.runescape.wiki/w/Piscatoris_Fishing_Colony)
- **couples with**: cooking monkfish (#71).

### S3 — Anglerfish (Port Piscarilius) — queue #63

- **npc**: bind cat **919** (`piscariliusfish`, "Rod Fishing spot", 3 spawns).
- **ops**: op1 `Bait`; op3 hidden continue.
- **items**: `fishing_rod` + `piscarilius_sandworms` → `raw_anglerfish`.
- **rates**: 82 Fishing, 120 XP. *(verify)*
- **new bait path**: `~fish_bait_required(fishing_rod)` returns `fishing_bait`
  today — sandworms are a *spot-specific* bait, so the bait must move from the
  equipment table to the catch row (§1.1 `column=bait`).
- **gate**: verify whether Kourend/Piscarilius favour is required.
- **wiki**: [Raw anglerfish](https://oldschool.runescape.wiki/w/Raw_anglerfish) ·
  [Sandworms](https://oldschool.runescape.wiki/w/Sandworms) ·
  [Port Piscarilius](https://oldschool.runescape.wiki/w/Port_Piscarilius)

### S4 — Dark crab (Wilderness Resource Area) — queue #63

- **npc**: bind cat **251** (`49_61_crabs`, `52_59_crabs`, 6 spawns).
- **ops**: op1 `Cage`; op4 hidden continue.
- **items**: `lobster_pot` + `wilderness_fishing_bait` → `raw_dark_crab`.
- **rates**: 85 Fishing, 130 XP. *(verify)*
- **gate**: [Resource Area](https://oldschool.runescape.wiki/w/Resource_Area)
  entry (7,500 coins). Verify whether the area gate is already scripted; if not
  it is part of this slice.
- **wiki**: [Raw dark crab](https://oldschool.runescape.wiki/w/Raw_dark_crab) ·
  [Dark fishing bait](https://oldschool.runescape.wiki/w/Dark_fishing_bait)

### S5 — Cave eel + giant frogspawn (Lumbridge Swamp Caves) — queue #63

- **npc**: bind cat **360** (5 spawns; note these are the *swamp cave* spots,
  `readyanim=swamp_fishing_point` — **not** Camdozaal `raw_cavefish`, whose npc
  blocks happen to share the `cavefish` spelling).
- **ops**: op1 `Small Net` → `giant_frogspawn`; op3 `Bait` → `raw_cave_eel`.
- **rates**: frogspawn 33 Fishing / 75 XP; cave eel 38 Fishing / 80 XP. *(verify)*
- **gate**: a lit light source. Check whether `skill_firemaking` #79 (light
  sources, still `pending`) blocks this — if it does, note the dependency rather
  than inventing a check.
- **wiki**: [Raw cave eel](https://oldschool.runescape.wiki/w/Raw_cave_eel) ·
  [Giant frogspawn](https://oldschool.runescape.wiki/w/Giant_frogspawn) ·
  [Lumbridge Swamp Caves](https://oldschool.runescape.wiki/w/Lumbridge_Swamp_Caves)

### S6 — Infernal eel (Mor Ul Rek) — queue #63

- **npc**: name-bind `morulrek_eels_fishingspot` (5 spawns) — it has no shared
  category.
- **ops**: op1 `Bait`; op3 hidden continue.
- **items**: `oily_fishing_rod` + `fishing_bait` → `infernal_eel`.
- **rates**: 80 Fishing, 95 XP. *(verify)*
- **post-catch**: crushing with a hammer for tokkul + lava scale is a separate
  `[opheldu]` — include it, the objs exist.
- **wiki**: [Infernal eel](https://oldschool.runescape.wiki/w/Infernal_eel) ·
  [Mor Ul Rek](https://oldschool.runescape.wiki/w/Mor_Ul_Rek)

### S7 — Sacred eel (Zul-Andra) — queue #63

- **npc**: name-bind `snakeboss_fishingspot` (2 spawns).
- **ops**: op1 `Bait`.
- **items**: `fishing_rod` + `fishing_bait` → `snakeboss_eel`.
- **rates**: 87 Fishing, 105 XP. *(verify)*
- **gate**: Zul-Andra access — [Regicide](https://oldschool.runescape.wiki/w/Regicide)
  (`quest_regicide` is **not** in `quests/`; check whether the area is otherwise
  reachable before adding a gate that can never pass).
- **post-catch**: knife → Zulrah's scales.
- **wiki**: [Sacred eel](https://oldschool.runescape.wiki/w/Sacred_eel) ·
  [Zul-Andra](https://oldschool.runescape.wiki/w/Zul-Andra)

### S8 — Rainbow fish (freshfish location variant) — queue #63

- **npc**: no new binding — rainbow fish come from cat **280** `freshfish` spots
  at specific locations (Isafdar / Feldip Hills), so this is a *per-spot* roll
  override, not a new handler.
- **items**: `fly_fishing_rod` + `feather` → `hunting_raw_fish_special`.
- **rates**: 38 Fishing, 80 XP. *(verify)*
- **shape**: the cleanest expression is a `column=spot_override` on the catch
  table keyed by the spot's npc name; do **not** add a second name-bound
  `[opnpc1,…]` for those records — a name binding silently shadows the category
  binding and a grep for the category cannot see it.
- **wiki**: [Raw rainbow fish](https://oldschool.runescape.wiki/w/Raw_rainbow_fish)

### S9 — Barbarian fishing — queue #64 (pairs with Strength #8)

The largest slice, and the one that needs `~fish_roll` to be table-driven.

- **npc**: bind cat **1174** (`0_19_55_brut_fishing_spot`,
  `0_39_54_brut_fishing_spot`, 7 spawns).
- **ops**: op1 `Use-rod`; op4 hidden continue.
- **items**: `brut_fishing_rod` (Barbarian rod) + `fishing_bait` (trout/salmon)
  or `feather` (sturgeon — verify which baits map to which catch).
- **catches**: `brut_spawning_trout` 48 Fishing, `brut_spawning_salmon` 58,
  `brut_sturgeon` 70.
- **XP**: Fishing 50/70/80 **plus** Agility 5/6/7 and Strength 5/6/7 — hence the
  two extra columns in §1.1. This is the only Fishing method that grants
  off-skill XP.
- **gate**: [Barbarian Training](https://oldschool.runescape.wiki/w/Barbarian_Training)
  fishing chapter from [Otto Godblessed](https://oldschool.runescape.wiki/w/Otto_Godblessed),
  plus 15 Agility / 15 Strength. Otto's dialogue is part of this slice.
- **by-products**: `brut_roe`, `brut_caviar`, `brut_fish_cuts` — cut with a
  knife; roe → caviar at Otto's. Each is an `[opheldu]`.
- **wiki**: [Barbarian Fishing](https://oldschool.runescape.wiki/w/Barbarian_Fishing) ·
  [Leaping trout](https://oldschool.runescape.wiki/w/Leaping_trout) ·
  [Roe](https://oldschool.runescape.wiki/w/Roe) ·
  [Caviar](https://oldschool.runescape.wiki/w/Caviar)
- **closes**: also unblocks Strength queue #8's fishing half.

### S10 — Fishing spot movement — queue #66

The one slice that is engine-adjacent, and it is **not blocked** — every opcode
it needs is implemented (`NC_PARAM`, `NPC_TELE`, `NPC_SETTIMER`,
`NPC_FINDALLANY`, `NPC_FINDNEXT`, `NC_CATEGORY`, `ENUM_GETOUTPUTCOUNT`) and
`[ai_timer]` is used 85 times elsewhere in the tree.

- **shape**: port LostCity's `fishing_movement.rs2` verbatim —
  `[ai_timer,_freshfish]` (and the other three cats + lavafish) →
  `npc_delay(2)`, `npc_tele($rand)`, `npc_settimer(280 + random(250))`, with
  `~check_fishing_spot_empty` rejecting tiles another same-category spot holds
  (50 retries).
- **the data**: LostCity carries **20** `fishing_movement_*` coord enums —
  Catherby, Gnome Stronghold, Baxtorian, Ardougne, Fishing Guild, Fisher Realm,
  Seers', Fishing Platform, Shilo, Entrana ×2, Musa Point, Rimmington,
  Wilderness camp, Draynor, Barbarian Village, Lumbridge, Al Kharid, Taverley
  Dungeon, Observatory. These are absolute world coords for areas that have not
  moved between eras, so they port directly. The remaining locations need
  wiki-sourced tables or stay static.
- **param**: `fishing_movement_enum` (a `type=enum` param) must be declared and
  attached per spot record in `fishing.npc`. That file is hand-authored, not
  generated, so the lines are safe there.
- **hazard**: A/B this against the roam RNG — a shared-RNG selftest will report
  false regressions.
- **wiki**: [Fishing spot](https://oldschool.runescape.wiki/w/Fishing_spot)
  (250–530 ticks, teleport not walk).

### S11 — Lava eel: bind the category, add the loc spot

`lavafish.rs2`'s header says "Name-bound `0_45_152_lavafish` (no shared cat in
osrs239)". **That is wrong** — category **1313** exists and covers all three
lava spots (`0_42_138_lavafish`, `0_45_152_lavafish`, `0_47_59_lavafish`), of
which 4 are spawned. Two of the three are currently unclickable.

- Rebind the six triggers from the name to `_lavafish` (cat 1313), fix the
  header.
- Port LostCity's `lavafish_loc.rs2` — the loc-based lava spot (`loc_2630`
  there). Resolve the osrs239 loc id first; the header records it as
  unresolved.
- **wiki**: [Raw lava eel](https://oldschool.runescape.wiki/w/Raw_lava_eel) ·
  [Oily fishing rod](https://oldschool.runescape.wiki/w/Oily_fishing_rod)

### S12 — Waterfall loc fishing spots

LostCity's `fishing_spots/waterfall.rs2` handles a **loc**-based lure/bait spot
(`loc_2027`) with `oploc1/2/3/4` + `oplocu` and a `~fish_roll_loc` variant.
Nothing in this tree handles a loc fishing spot at all. Resolve the osrs239 loc
and port; the roll can share S1's table.

### S13 — Fishing Guild remainder — queue #65

Mostly done (door + level-68 gate + Master Fisher + Roachey + shop). What is
left:

- The guild's own spots are cat 281/282 records already handled — but see §1.3's
  third row, the Fishing Guild spots are among the six `Net`-labelled cat-282
  records.
- Fishing cape / Fishing potion boost interaction with the 68 gate (a boost lets
  you in at 63 with a cape — verify).
- **wiki**: [Fishing Guild](https://oldschool.runescape.wiki/w/Fishing_Guild)

### S14 — Fishing cape, angler's outfit, barrel/sack, spirit flakes — queue #68

- **Fishing cape**: add `skillcape_fishing` / `skillcape_fishing_trimmed` to
  `~skillcape_boost` (`skill_combat/scripts/player/skillcape_boost.rs2`) for the
  +1 Boost. One `if` block, matching the five capes already there.
  [Fishing cape](https://oldschool.runescape.wiki/w/Fishing_cape)
- **Angler's outfit**: `trawler_reward_hat` / `_top` / `_legs` / `_boots` —
  +0.4/0.8/0.6/0.2 % Fishing XP, +1% for the full set. Hooks into
  `stat_advance` at the roll site, so it belongs in S1's roll proc, not each
  spot script. [Angler's outfit](https://oldschool.runescape.wiki/w/Angler%27s_outfit)
  (Trawler-sourced — couples with #69).
- **Fish barrel / fish sack**: `fish_barrel_closed` / `fish_barrel_open` /
  `fish_sack` — auto-deposit on catch, 28-slot overflow.
  [Fish barrel](https://oldschool.runescape.wiki/w/Fish_barrel)
- **Spirit flakes**: `spirit_flakes` — consumed per catch for a bonus-catch
  chance. [Spirit flakes](https://oldschool.runescape.wiki/w/Spirit_flakes)

The last three are all "intercept the catch", which is exactly why S1 must land
first — otherwise each is edited into eight spot scripts.

### S15 — Miscellania fishing intercept — queue #67

`areas/area_miscellania/scripts/fisherman_frodi.rs2` exists with dialogue; its
header records "Deferred: fishing". Wire Frodi's approval/resource intercept.
[Managing Miscellania](https://oldschool.runescape.wiki/w/Managing_Miscellania)

### S16 — Tutorial and newbie spots (cat 453)

`0_48_48_newbiefishing` (3 spawns) and `0_26_95_tut2_fishing` (3 spawns) are
`Net` spots with no handler — dead clicks in the starting area, which is the
worst place to have one. The four Fishing Contest records in the same category
*are* handled, name-bound from the quest, so cat 453 cannot simply be bound
wholesale — bind by name, or bind the category and let the quest's name
bindings shadow it (verify which wins; a name binding shadowing a category
binding is a known silent trap here).

### S17 — Karambwan polish

`tbwt.rs2` is complete for the catch. Missing: loading the vessel with
karambwanji (`tbwt_karambwan_vessel` + `tbwt_raw_karambwanji` →
`tbwt_karambwan_vessel_loaded_with_karambwanji`) — verify it exists in
`quest_tbwt`; the fishing script assumes it. Also `~objbox` → `~mesbox` on the
casket path, and `mm_wearing_greegree`, both noted deferred in `memberfish.rs2`.
[Raw karambwan](https://oldschool.runescape.wiki/w/Raw_karambwan) ·
[Karambwan vessel](https://oldschool.runescape.wiki/w/Karambwan_vessel)

---

## 3. Selftests — land alongside S1, not at the end

There is no fishing selftest today. `server/scripts/selftest.rs2` is the
framework; `charges_selftest.rs2` and `gauntlet_selftest.rs2` are the per-system
pattern to copy. Minimum set:

| test | asserts |
|---|---|
| `selftest_fishing_table` | every `fishing_catch` row resolves obj + level + XP; no duplicate `product` |
| `selftest_fishing_roll` | `~fish_roll` at level N returns only rows with `levelrequired <= N` |
| `selftest_fishing_equipment` | `~check_fish_equipment` fails without the tool, fails without bait, passes with both — for each of the 8 tools |
| `selftest_fishing_bait_consumed` | a successful catch deletes exactly 1 bait; a failed catch deletes 0 |
| `selftest_fishing_categories` | every cat in §0.1 + §0.3 has at least one live `[opnpc1]` — the regression guard against a dead click |
| `selftest_fishing_movement` | after `~fishing_spot_random_coord`, the returned coord holds no same-category spot (S10) |

The last two are the ones that would have caught the two lava spots in S11.

---

## 4. Order

```
S1 (table)
 ├─ S2 monkfish ─┐
 ├─ S3 angler    │
 ├─ S4 dark crab ├─ each independent, any order
 ├─ S5 cave eel  │
 ├─ S6 infernal  │
 ├─ S7 sacred    │
 ├─ S8 rainbow  ─┘
 └─ S9 barbarian (needs N-fish roll + off-skill XP columns)
      └─ unblocks Strength #8

S11 lavafish cat  ─ independent, cheap, fixes 2 dead spots
S16 tutorial spots ─ independent, cheap, fixes 6 dead spots
S10 movement       ─ independent of S1 entirely
S12 waterfall locs ─ after S1
S13/S14/S15/S17    ─ after S1 (all intercept the catch)
§3 selftests       ─ with S1, extended per slice
```

Cheapest-first, if the goal is fewest dead clicks per commit: **S11, S16, S10**
before the table refactor.

---

## 5. Explicitly out of this lane

| what | why | where it goes |
|---|---|---|
| Whirlpools + afk macro events | LostCity's `afk_event` / `macro_*` random-event system, dropped tree-wide across *every* skill as an era decision — not a fishing gap | a cross-skill random-events slice, if ever |
| Trawler control timer + `%npc_*` varn | queue #69, blocked on the CONTENT skip list | `game_trawler/` |
| Minnows (`minnow_fishingspot1..4`, 4 spawns) | 2018 content; needs [Kylie Minnow](https://oldschool.runescape.wiki/w/Kylie_Minnow)'s 40:1 shark exchange, flying-fish theft, and the fast-moving spot cadence | KRONOS, queue #70 |
| Tempoross (`tempoross_harpoonfish_*`, cats 1513/1514) | 2021 | KRONOS, queue #70 |
| Camdozaal fish (`raw_guppy`/`raw_cavefish`/`raw_tetra`/`raw_catfish`, cat 1533) | 2021, [Below Ice Mountain](https://oldschool.runescape.wiki/w/Below_Ice_Mountain) — the quest *is* ported (`quest_belowicemountain`), so this is the closest post-2009 slice to viable | KRONOS |
| Aerial fishing (`fishing_spot_aerial`, **20 spawns**) | 2018 | KRONOS, queue #70 |
| Drift net fishing | 2018 | KRONOS, queue #70 |
| Varlamore spots (`aldarin_special_fishing`, `civitas_park_*`, `stranglewood_*`, `lanternfish`) | 2024; 0 spawns anyway | KRONOS |
| `fairy2_fakefish`, `deal_squid` | quest props, not Fishing | `quest_*` lanes |

---

## 6. Verification

- `make -C src mock230-scripts` after every config rename — the compack and the
  scripts must be rebuilt together.
- Headless: use a scratch `MOCK230_SAVES` — runs are not independent.
- Per slice: catch one of each new fish at the gate level and one below it;
  confirm the level message, the bait consumption, the XP, and that a full
  inventory stops the loop rather than eating bait.
- S10: tick ~900 and print `x,z` vs `spawn_x,spawn_z` for every npc whose name
  contains "Fishing spot" — expect movement now, and expect no two same-category
  spots to share a tile.
- S1: byte-compare the message/XP output of a 500-catch run before and after the
  refactor.
