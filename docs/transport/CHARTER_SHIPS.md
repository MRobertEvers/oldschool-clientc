# Charter ships — Trader Stan's fleet

> Written 2026-08-17. Status: **complete to the limit of what this cache
> contains** — transport, fares, the shop and the cache's own map picker are
> built and verified. The wiki's Gielinor-map voyage animation is **not in
> `cache.osrs239`**; §5.3 is the evidence. The implementation is
> `server/scripts/transport_charter/` — read its README first. §12 is the
> as-built record and §13 is what is genuinely still open.
>
> **Behaviour authority (wiki):**
> [Charter ship](https://oldschool.runescape.wiki/w/Charter_ship) ·
> [Template:Charter ship fares](https://oldschool.runescape.wiki/w/Template:Charter_ship_fares) ·
> [Trader Stan](https://oldschool.runescape.wiki/w/Trader_Stan) ·
> [Trader Crewmember](https://oldschool.runescape.wiki/w/Trader_Crewmember) ·
> [Trader Stan's Trading Post](https://oldschool.runescape.wiki/w/Trader_Stan%27s_Trading_Post) ·
> [Transcript:Trader Crewmember](https://oldschool.runescape.wiki/w/Transcript:Trader_Crewmember) ·
> [Transcript:Trader Stan](https://oldschool.runescape.wiki/w/Transcript:Trader_Stan)
>
> Every id, coord, varbit, dbrow and interface number below was **measured from this
> tree** (`OSRS-Content/osrs239-content/`, abbreviated `CT/` throughout), not copied from
> the wiki. Where a number is still unmeasured it is marked *(to measure)*.
> Reference implementation availability: **LostCity: none** (charter ships are 2006
> content, post-254). **Kronos / 2009scape: no port in this tree** — grep for
> `charter`/`trader_stan` under `CT/server/scripts/` returns nothing.
>
> Wiki revisions should be pinned through the OSRS Wiki API before this doc is treated
> as final, per `docs/quests/desert_treasure_i.md:27`.

---

## 1. Why this is cheaper than it looks

Charter ships have sat in `docs/MAPLINKS.md:498` since that document was written, parked
with fairy rings, spirit trees and gliders as *"every one needs a destination-picker
interface this tree does not have yet."* That is no longer true for this one. Canoes
landed and proved the shape (`docs/CANOES.md`), and unlike the other parked networks
**almost all of charter ships is already in `cache.osrs239`, unwired**:

| piece | state |
|---|---|
| NPCs (Trader Stan + 6 crew variants, 24 per-port leaves each) | in `CT/configs/all.npc`, already carrying `op1=Talk-to op3=Trade op4=Charter op5=Charter-to <port>` |
| world spawns | 23 rows at **11 ports** already in the generated roster |
| destination table | dbtable **206** `chartering_destinations`, 24 rows, with arrival coords, map pin pixels and zone boxes |
| the destination picker | interfaces **72** `sailing_menu` (map) + **885** `chartering_menu_side` (side list) |
| pin layout, ops, quest gating, "you are here" highlight | clientscripts **8939/8940/8941/9104/7334/7335/7336/9105/9107** — all decompiled in `CT/scripts/` |
| the shop container | inv **441** `trader_stan_shop`, `size=40` |
| the shop stock | 30 lines, all resolved, in `CT/wiki/shop_stock.csv` |
| the previous-destination state | varbit **11209** `chartering_previous_destination` |

Two things are **not** in the cache and are ours to author:

1. **The fare matrix.** `chartering_costs` is dbtable 207 with `columns=0` and one empty
   dbrow (9454). Fares live only on the wiki.
2. **Every line of server script.** Nothing under `CT/server/scripts/` mentions charter.

This is a content slice, not an engine slice.

---

## 2. How the system works (OSRS Wiki)

Trader Stan runs a fleet of merchant ships between port cities. Every port has **two
Trader Crewmembers**; Port Sarim (and, on live, Corsair Cove) additionally has Stan
himself. Talk to any of them, pay a fare, and you are sailed to any other port you have
unlocked. Every one of them also runs a branch of **Trader Stan's Trading Post**.

The right-click menu on a crewmember is `Talk-to` / `Trade` / `Charter` /
`Charter-to <last destination>`.

**Dialogue flow** ([transcript](https://oldschool.runescape.wiki/w/Transcript:Trader_Crewmember);
the wiki's copy is verbatim Jagex text and is paraphrased here):

```
"Can I help you?"
 ├─ "Yes, who are you?"                     → who Stan is, then the goods/charter offer
 ├─ "Isn't it tricky to sail about in       → the clothes are enchanted to self-repair,
 │   those clothes?"                          then back to the goods/charter offer
 └─ "No thanks."                            → ends

"Yes, I would like to charter a ship"
 → destination list
 → "Sailing to <destination> costs <n> coins"
     ├─ "Okay"          → fare taken, voyage plays, you arrive
     ├─ "Choose again"  → back to the destination list
     └─ "Cancel"        → ends
```

**Refusals** the crew make:

| carried / state | response |
|---|---|
| a plain bedsheet | *"we aren't a laundry ship"* |
| an ectoplasm-covered bedsheet | refused, contaminated |
| Karamjan rum | refused — the same smuggling gag `captain_shanks.rs2` already implements |
| Crandor, during Dragon Slayer I | *"No charter ship would take you"* |

**The voyage** takes only a few seconds. *"During this, a map of Gielinor is displayed
with the destinations labelled, with an animation of a ship travelling between them."*

**Discounts: there are none.** Neither the Karamja gloves/diary nor any outfit reduces a
charter fare — [Karamja gloves](https://oldschool.runescape.wiki/w/Karamja_gloves) lists
its discounted shops and charter is not among them. Recorded here so its absence is not
mistaken for an omission.

---

## 3. The ports

### 3.1 Destination table — dbtable 206 `chartering_destinations`

```
[chartering_destinations]                        # CT/configs/all.dbtable:1730
columndef=0:chartering_destination_id,int
columndef=1:chartering_destination_name,string
columndef=2:chartering_destination_inline_name,string
columndef=3:chartering_destination_port_coord,coord     # arrival tile
columndef=4:chartering_destination_x_pos,int            # pin pixel x on interface 72
columndef=5:chartering_destination_y_pos,int            # pin pixel y
columndef=6:chartering_destination_inzone,coord,coord    # "am I at this port" box
columndef=8:related_content,int                          # content-restriction gate
```

Server-side `db_getfield` field ids are `table*4096 + col*16`:

| col | field id |
|---|---|
| 0 `id` | 843776 |
| 1 `name` | 843792 |
| 2 `inline_name` | 843808 |
| 3 `port_coord` | 843824 |
| 4 `x_pos` | 843840 |
| 5 `y_pos` | 843856 |
| 6 `inzone` | 843872 |
| 8 `related_content` | 843904 |

All 24 rows, measured:

| id | dbrow (`chartering_destination_…`) | name | arrival tile | coord literal | pin x,y | in scope |
|---|---|---|---|---|---|---|
| 0 | `_crandor` | Crandor | — (`port_coord=0`) | — | 290,185 | **no** |
| 1 | `_brimhaven` | [Brimhaven](https://oldschool.runescape.wiki/w/Brimhaven) | 2763,3238 | `0_43_50_11_38` | 283,171 | yes |
| 2 | `_catherby` | [Catherby](https://oldschool.runescape.wiki/w/Catherby) | 2792,3417 | `0_43_53_40_25` | 288,210 | yes |
| 3 | `_mosleharmless` | [Mos Le'Harmless](https://oldschool.runescape.wiki/w/Mos_Le%27Harmless) | 3668,2931 | `0_57_45_20_51` | 404,126 | yes |
| 4 | `_musapoint` | [Musa Point](https://oldschool.runescape.wiki/w/Musa_Point) | 2957,3158 | `0_46_49_13_22` | 303,161 | yes |
| 5 | `_portkhazard` | [Port Khazard](https://oldschool.runescape.wiki/w/Port_Khazard) | 2674,3141 | `0_41_49_50_5` | 263,159 | yes |
| 6 | `_portphasmatys` | [Port Phasmatys](https://oldschool.runescape.wiki/w/Port_Phasmatys) | 3705,3503 | `0_57_54_57_47` | 416,217 | yes |
| 7 | `_portsarim` | [Port Sarim](https://oldschool.runescape.wiki/w/Port_Sarim) | 3038,3189 | `0_47_49_30_53` | 317,173 | yes |
| 8 | `_shipyard` | [Karamja Shipyard](https://oldschool.runescape.wiki/w/Shipyard) | 2998,3032 | `0_46_47_54_24` | 306,144 | yes |
| 9 | `_porttyras` | [Port Tyras](https://oldschool.runescape.wiki/w/Port_Tyras) | 2142,3125 | `0_33_48_30_53` | 191,154 | yes |
| 10 | `_corsaircove` | [Corsair Cove](https://oldschool.runescape.wiki/w/Corsair_Cove) | 2592,2851 | `0_40_44_32_35` | 258,114 | yes |
| 11 | `_prifddinas` | [Prifddinas](https://oldschool.runescape.wiki/w/Prifddinas) | 2157,3333 | `0_33_52_45_5` | 194,192 | yes |
| 12 | `_piscarilius` | [Port Piscarilius](https://oldschool.runescape.wiki/w/Port_Piscarilius) | 1811,3679 | `0_28_57_19_31` | 166,243 | yes |
| 13 | `_landsend` | [Land's End](https://oldschool.runescape.wiki/w/Land%27s_End) | 1493,3403 | `0_23_53_21_11` | 126,202 | yes |
| 14 | `_fortis` | [Civitas illa Fortis](https://oldschool.runescape.wiki/w/Civitas_illa_Fortis) | 1747,3136 | `0_27_49_19_0` | 158,162 | yes |
| 15 | `_aldarin` | [Aldarin](https://oldschool.runescape.wiki/w/Aldarin) | 1458,2968 | `0_22_46_50_24` | 109,135 | yes |
| 16 | `_sunsetcoast` | [Sunset Coast](https://oldschool.runescape.wiki/w/Sunset_Coast) | 1514,2968 | `0_23_46_42_24` | 126,137 | yes |
| 17 | `_tempestus` | Tempestus | 1248,3232 | `0_19_50_32_32` | 82,172 | **no** |
| 18 | `_pandemonium` | The Pandemonium | 3060,3000 | `0_47_46_52_56` | 327,138 | no — `related_content=1` |
| 19 | `_summer_shore` | The Summer Shore | 3187,2365 | `0_49_36_51_61` | 342,33 | no — `related_content=1` |
| 20 | `_red_rock` | Red Rock | 2808,2502 | `0_43_39_56_6` | 290,55 | no — `related_content=1` |
| 21 | `_barracuda_hq` | Barracuda HQ | 2290,2540 | `0_35_39_50_44` | 218,41 | no — `related_content=1` |
| 22 | `_deepfin_point` | Deepfin Point | 1944,2751 | `0_30_42_24_63` | 167,78 | no — `related_content=1` |
| 23 | `_port_roberts` | Port Roberts | 1871,3314 | `0_29_51_15_50` | 171,188 | no — `related_content=1` |

**16 ports are in scope.** Two of the 18 mainline rows are excluded on evidence, not
preference:

- **Tempestus** — `script_9104` has `case 4163: return(0)` with no condition. The client
  will never offer it.
- **Crandor** — `port_coord` is literally `0`. Chartering there teleports into the void.
  It exists as a row so the Dragon Slayer refusal line has something to name.

The six Sailing ports (18–23) carry `related_content=1` and are switched off by the
cache's own restriction system (`script_8943`, *"restrict_content_dbrow"*), so they cost
us nothing and need no server work.

> **⚠ Unresolved: the plane nibble.** The raw `port_coord` ints carry a `1` in bits 28+
> on 22 of 24 rows (`318213237 >> 28 == 1`), while `inzone` decodes cleanly at plane 0.
> That is consistent with a `level + 1` bias so `0` can mean *null* — Crandor's row is
> exactly `0`. The literals above are the masked values. Resolve this properly (check a
> known plane-1 dbrow coord elsewhere in `all.dbrow`) before shipping; do not mask blind.

### 3.2 Where the ports are, and what still needs spawning

Already spawned, from the generated roster `CT/server/scripts/areas/world/configs/`:

| port | spawn file | NPCs and tiles |
|---|---|---|
| Port Sarim | `m47_49.spawn` | Stan 3039,3192 · crew 3042,3192 · crew 3039,3193 |
| Brimhaven | `m43_50.spawn` | 2759,3239 · 2760,3239 |
| Catherby | `m43_53.spawn` | 2796,3415 · 2797,3415 |
| Musa Point | `m46_49.spawn` | 2954,3156 · 2954,3157 |
| Port Khazard | `m41_49.spawn` | 2673,3144 · 2675,3144 |
| Port Phasmatys | `m57_54.spawn` | 3701,3502 · 3701,3503 |
| Karamja Shipyard | `m46_47.spawn` | 3001,3033 · 3001,3034 |
| Port Tyras | `m33_48.spawn` | 2144,3122 · 2145,3122 |
| Prifddinas | `m33_52.spawn` | 2157,3329 · 2157,3330 |
| Corsair Cove | `m40_44.spawn` | 2587,2851 · 2589,2851 |
| Mos Le'Harmless | `m57_45.spawn` | 3671,2930 · 3672,2930 |

Missing, to be added in a **feature-owned** `charter.spawn`:

| port | anchor tile | map square | square built? | roster file exists? |
|---|---|---|---|---|
| Port Piscarilius | 1808,3679 | `m28_57` | yes, 257 KB | yes |
| Land's End | 1496,3403 | `m23_53` | yes, 104 KB | yes |
| Civitas illa Fortis | 1743,3136 | `m27_49` | yes, 162 KB | yes |
| Aldarin | 1455,2968 | `m22_46` | yes, 169 KB | **no spawn file at all** |
| Sunset Coast | 1514,2971 | `m23_46` | yes, 190 KB | yes |

Anchor tiles are the Origin column of `charter_ships.tsv` — the tile the player stands on
to interact, so each crewmember goes one or two tiles off it, nudged clear of the jetty
footprint and checked against `maps/*.jl2` blocking flags. That is exactly the derivation
`canoes.spawn`'s header documents.

**Never hand-edit `areas/world/configs/`.** `tools/gen_spawns.py` rewrites that directory
wholesale from an external dump; feature-owned spawns go in the feature's own `.spawn`.

---

## 4. The NPCs

### 4.1 The multinpc parents — the ids that actually get spawned

Seven parents, each `multivarbit=chartering_previous_destination` with 24 per-port leaves:

| id | name | display name |
|---|---|---|
| 1328 | `sailing_transport_trader_stan` | Trader Stan |
| 1329 | `sailing_transport_trader_stan_crew_man1` | Trader Crewmember |
| 1330 | `sailing_transport_trader_stan_crew_man2` | Trader Crewmember |
| 1331 | `sailing_transport_trader_stan_crew_man3` | Trader Crewmember |
| 1332 | `sailing_transport_trader_stan_crew_woman1` | Trader Crewmember |
| 1333 | `sailing_transport_trader_stan_crew_woman2` | Trader Crewmember |
| 1334 | `sailing_transport_trader_stan_crew_woman3` | Trader Crewmember |

```
[sailing_transport_trader_stan]
readyanim=human_ready  walkanim=human_walk_f  …
vislevel=1
multivarbit=chartering_previous_destination
multinpc1=sailing_transport_trader_stan_base
multinpc2=sailing_transport_trader_stan_brimhaven
…
multinpc24=sailing_transport_trader_stan_port_roberts
multinpc25=-1
```

Crew parents additionally carry `alwaysontop=yes renderpriority=1 category=420`.

### 4.2 The leaves — where the ops live

Leaf id ranges: Stan **9299–9311**, 12630–12631, 12779–12782, 15505+; crew **9312–9383**,
12632–12643, 12783–12806, 15506+.

```
[sailing_transport_trader_stan_base]          [sailing_transport_trader_stan_portsarim]
name=Trader Stan                              name=Trader Stan
op1=Talk-to                                   op1=Talk-to
op3=Trade                                     op3=Trade
op4=Charter                                   op4=Charter
vislevel=0                                    op5=Charter-to Port Sarim
                                              vislevel=0
```

`multivarbit` is the **previous destination**, so `op5` is a one-click repeat of the last
route. `_base` (varbit value 0) has no `op5` — the player has not sailed yet.

Wiki examine texts, one per crew model
([Trader Crewmember](https://oldschool.runescape.wiki/w/Trader_Crewmember)):

| leaf family | examine |
|---|---|
| `crew_woman1` (9360–9371) | *"High heels on a ship? What is she thinking?"* |
| `crew_man2` (9324–9335) | *"That suit looks a little briny around the edges."* |
| `crew_man3` (9336–9347) | *"First storm he is in that hat will blow away."* |
| `crew_woman2` (9372–9383) | *"The effect is sort of spoiled by all those tattoos."* |
| `crew_woman3` (9348–9359) | *"She'll never be able to climb rigging in that."* |
| `crew_man1` (9312–9323) | *"It's going to be hard to climb rigging dressed like that."* |
| Trader Stan | *"With the prices he charges, no wonder he can afford to look so sharp."* |

### 4.3 Two traps around these NPCs

**Multinpc does not resolve server-side.** mock230 has `mock230_loc_resolve_transform`
for locs (`src/net/mock/mock230_scene.c:354`) but no npc equivalent — grep for `multinpc`
under `src/net/mock/` finds only comments. Triggers therefore dispatch against the
**parent** name. Bind the seven parents, and belt-and-braces the leaves, exactly as
`CT/server/scripts/quests/quest_ethicallyacquiredantiquities/scripts/ethicallyacquiredantiquities.rs2:68`
already does:

```runescript
[opnpc1,sailing_transport_trader_stan_crew_man1]
[opnpc1,sailing_transport_trader_stan_crew_man1_base]
[opnpc1,sailing_transport_trader_stan_crew_man1_piscarilius]
[opnpc1,sailing_transport_trader_stan_crew_man1_portsarim]
@eaa_crew_talk;
```

**`opnpc1` is already taken.** That quest file owns `[opnpc1,…]` for
`sailing_transport_trader_stan`, `_base`, `_portsarim` and the whole `crew_man1` set, and
a duplicate name-bound trigger is a hard `sscompile` error
(`docs/SCRIPT_NAME_COLLISIONS.md`). `opnpc3`, `opnpc4` and `opnpc5` are free on all seven
— grep-verified. **Take 3/4/5 and leave 1 alone**; hang the "Yes, I would like to charter
a ship" chat branch off the quest file's existing `@eaa_crew_talk` instead of adding a
second `opnpc1`.

Category binding is not an option here: crew parents carry `category=420`, which is **not
named** in `CT/pack/category.pack`, and the resolved leaves carry no category at all.

---

## 5. The interfaces

### 5.1 The picker — 72 `sailing_menu` + 885 `chartering_menu_side`

| id | name | file | components |
|---|---|---|---|
| **72** | `sailing_menu` | `CT/interfaces/sailing_menu.if` | `universe` (512×334, `onload=i:8939,…,i:4718595`), `com_1` (map plate, model **50089**), `com_2` (Close, sprite 539/540), `content` (marker container, uid 4718595) |
| **885** | `chartering_menu_side` | `CT/interfaces/chartering_menu_side.if` | `universe frame list list_background list_content list_scroller title com_7`; `list_content onload=i:7335`; `com_7` reads "Destination" |

The client does all the layout. From `CT/scripts/`:

| clientscript | what it does |
|---|---|
| `script_8939` | `~chatdefault_stopinput`, then `~script8940(content)` |
| `script_8940` | `cc_deleteall`, `db_findall_with_count(206)`, walks every row, tracks the player's current port via `~script7334(coord)` |
| `script_8941` | per row: reads `x_pos`/`y_pos` (843840/843856); `cc_create`s a **graphic** at sub `2i` (`graphic_6918` if it is the current port, else `graphic_6916`) and a 30×30 transparent **rectangle** at sub `2i+1`; `cc_setop(1, <port name>)` **on the rectangle**; `cc_sethide(true)` on both when unavailable |
| `script_9104` | availability — quest/varbit gates plus the "you are already here / same-port pair" exclusions. Returns `1` for available |
| `script_8943` | *"restrict_content_dbrow"* — the `related_content` content-restriction gate |
| `script_7334` | `inzone` (col 6) lookup: which dbrow's box contains a given coord |
| `script_7335` / `script_7336` | the 885 side list, same two-children-per-row layout |
| `script_9105` / `script_9107` | display name for a row |

**The server's whole job is four lines**: `if_openmain_side(sailing_menu, chartering_menu_side)`,
`if_setevents` across both containers, an `[if_button1,…]` trigger, and the fare/gate
enforcement behind it.

**Sub-id decode.** The op sits on the **odd** sub-id (`2i+1`), and hidden rows still
consume both slots, so the index runs over the full `db_findall` order — not over the
visible pins:

```
row = (last_slot - 1) / 2   →   db_getrow(row)   →   dbrow id   →   destination id
```

**Do not use `if_addresumebutton` + `p_pausebutton` here.** `src/net/mock/mock230_world.c:7164`
returns early for `sub == 0` on a registered resume button, so the first row would be
silently swallowed — the trap `docs/CANOES.md:213` records. Use
`if_setevents(<container>, 0, <max>, ^if_event_op1)` and read `last_slot` inside an
`[if_button1,…]` trigger, as
`CT/server/scripts/minigames/minigame_templetrek/scripts/templetrek_rewards.rs2:283`
and `…/minigame_rs2012_qbd/scripts/rs2012_qbd_ui.rs2:139` do.

### 5.2 The availability gates the client already applies

`script_9104`, dbrow id → condition. **The server must mirror every one of these.** The
client greying a pin is a courtesy; the refusal has to be authoritative server-side, the
same rule `~canoe_reach_allowed` follows against clientscripts 3105–3108.

| dbrow | port | gate | quest |
|---|---|---|---|
| 4150 | Mos Le'Harmless | `%fever_quest >= 140` **and** `%priestperil >= 61` | [Cabin Fever](https://oldschool.runescape.wiki/w/Cabin_Fever), [Priest in Peril](https://oldschool.runescape.wiki/w/Priest_in_Peril) |
| 4153 | Port Phasmatys | `%priestperil >= 61` | [Priest in Peril](https://oldschool.runescape.wiki/w/Priest_in_Peril) |
| 4154 | Karamja Shipyard | `%mm_main >= 2` **and** `%grandtree >= 160` | [The Grand Tree](https://oldschool.runescape.wiki/w/The_Grand_Tree) |
| 4155 | Port Tyras | `%regicide_quest >= 15` | [Regicide](https://oldschool.runescape.wiki/w/Regicide) |
| 4157 | Prifddinas | `%varbit9016 sote >= 200` | [Song of the Elves](https://oldschool.runescape.wiki/w/Song_of_the_Elves) |
| 4158, 4159 | Piscarilius, Land's End | `varbit 4897 zeah_playerhasvisited != 0` | visited [Great Kourend](https://oldschool.runescape.wiki/w/Great_Kourend) |
| 4160–4162 | Fortis, Aldarin, Sunset Coast | `varbit 9650 varlamore_visited != 0` | visited [Varlamore](https://oldschool.runescape.wiki/w/Varlamore) |
| 4163 | Tempestus | `return(0)` unconditionally | — |
| 4164 | Crandor | `%dragonquest` in 2..9 | [Dragon Slayer I](https://oldschool.runescape.wiki/w/Dragon_Slayer_I) |

Varp/varbit names, resolved in this tree: varp 655 `fever_quest`, 302 `priestperil`,
365 `mm_main`, 150 `grandtree`, 328 `regicide_quest`, 176 `dragonquest`; varbit 9016
`sote`, 4897 `zeah_playerhasvisited`, 9650 `varlamore_visited`.

Also encoded in `script_9104` and worth mirroring: the **same-port-pair exclusions** —
Port Sarim never offers Musa Point, Piscarilius or Land's End; Musa Point never offers
Port Sarim; Piscarilius and Land's End never offer each other; Aldarin and Sunset Coast
never offer each other. Those are the `NA` cells in the wiki fare table, and they are
already consistent.

### 5.3 The voyage surface — **not in this cache**

The wiki describes the voyage as *"a map of Gielinor ... with an animation of a
ship travelling between them."* That animation is not in `cache.osrs239`, and
this is measured rather than assumed. Four checks, all offline:

1. **Nothing tweens a ship across the charter map.** The pin pixel columns
   `x_pos` (field 843840) and `y_pos` (843856) are read by exactly one
   clientscript in the whole cache — `script_8941`, the pin builder. If a
   voyage moved a marker between two ports it would read those columns; nothing
   does.

2. **Only six clientscripts touch dbtable 206 at all** — 8941 (pins), 8942/8943
   (content restriction), 9105/9106/9107 (display names). None animates.

3. **`ship_journey` (299) is a different feature.** Its animation binder is
   `script_2380` → `script_2382`, which reads `%journey_number` (varp 75) and
   sets a model seq on the boat, choosing between `animset_8173..8180` — eight
   canned routes, for values `-2, -1, 1..6`. Charter has 240 ordered routes.
   And 299's boat model is **3065 `obj/grandtree_warship`**, with eleven static
   pins spanning Ardougne to Ape Atoll: it is the Karamja/Ape Atoll boat
   journey, not Trader Stan's fleet.

4. **`sailing_menu` (72) has no ship child, and the server cannot create one.**
   The pins are `cc_create`d client-side by 8941; `cc_*` has no server-side
   form. The server's whole vocabulary for an existing child is `if_setposition`,
   `if_sethide`, `if_setmodel`, `if_setanim`, `if_setcolour`, `if_setobject`,
   `if_settext` — there is no `if_setgraphic`, so an existing pin cannot even be
   turned into a ship sprite.

So the voyage is the narrated hop, and that is a statement about the cache
rather than about the effort spent. Synthesising it would mean inventing
behaviour — repurposing a pin as a ship and guessing at a pixel space — which is
worse than not having it.

**A pre-existing defect found on the way, in someone else's feature.** Nothing
in this tree ever calls `script_2380`, which is the only thing that binds a boat
component to `script_2382` and arms its `if_setonvartransmit(...{var75})`. And
the journey numbers content actually writes — `0`, `11`, `12` (Fishing Trawler)
and `14`, `15` (Pest Control) — are none of them in `script_2382`'s switch,
which knows only `-2, -1, 1..6`. So `minigames/minigame_pestcontrol/scripts/pest_sail.rs2`
and the trawler open `ship_journey`, set a varp nothing is listening to, on a
component nobody bound, and show a static ship. Recorded here rather than fixed:
remapping their routes into `1..6` is their content's decision, not this
feature's.

One caveat on how far that was traced: `script_4731.cs2` is **absent** from this
tree's decompiled set (9,725 scripts, that one missing), so `script_4729`'s
layout half could not be followed to the end. It does not affect the conclusion
— the animation is entirely `script_2382`, whose two parameters are both typed
`component` and whose roles are unambiguous — but it is why this stops at
describing the binder rather than wiring it.

## 6. Fares

**The wiki fare template is the cost authority.** `charter_ships.tsv` is the coordinate
and quest-gate authority. They **disagree** on cost, and the tsv is the one that is
stale — spot-checked mismatches:

| route | `charter_ships.tsv` | wiki |
|---|---|---|
| Musa Point → Brimhaven | 200 | 480 |
| Karamja Shipyard → Port Khazard | 720 | 1,600 |
| Brimhaven → Catherby | 460 | 480 |
| Port Sarim → Sunset Coast | 1,550 | 3,100 |
| Sunset Coast → Civitas illa Fortis | 250 | 500 |

The whole Sunset Coast row of the tsv is exactly half the wiki's, which reads as a
plugin-side data bug rather than an asymmetry. Take the wiki table; use the tsv to
cross-check the destination tiles and the `Quests` column only.

Across the 16 mainline ports the wiki matrix is **fully symmetric** — checked cell by
cell, 0 mismatches. (The wiki's own prose calls the table asymmetric; that applies to the
Sailing ports, which are out of scope here.) The generator should therefore emit both
directions from 120 authored pairs and **assert the symmetry**, so a future wiki change
that breaks it is a test failure rather than a silent one-way fare. `*` marks routes the
wiki has **already halved**, because reaching
Mos Le'Harmless requires Cabin Fever anyway — **do not halve again**. `·` is the diagonal;
`—` is a pair the game never offers (§5.2).

[Template:Charter ship fares](https://oldschool.runescape.wiki/w/Template:Charter_ship_fares),
mainline ports only:

| from \ to | Sarim | Brim | Cath | MosLe | Musa | Khaz | Phas | Ship | Tyras | Cors | Prif | Pisc | LEnd | Fortis | Aldar | Sunset |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| **Sarim** | · | 1,600 | 1,000 | 650* | — | 1,280 | 1,300 | 400 | 3,200 | 1,200 | 4,800 | — | — | 3,000 | 3,100 | 3,100 |
| **Brim** | 1,600 | · | 480 | 1,950* | 480 | 400 | 2,900 | 400 | 3,200 | 680 | 3,450 | 2,000 | 2,200 | 2,400 | 2,500 | 2,500 |
| **Cath** | 1,000 | 480 | · | 1,250* | 480 | 1,600 | 3,500 | 1,600 | 3,200 | 1,000 | 3,560 | 2,000 | 2,200 | 2,400 | 2,500 | 2,500 |
| **MosLe** | 650* | 1,950* | 1,250* | · | 2,050* | 550* | — | 550* | 1,600* | 2,040* | 2,475* | 2,100* | 2,200* | 2,300* | 2,350* | 2,350* |
| **Musa** | — | 480 | 480 | 2,050* | · | 400 | 1,100 | 200 | 3,200 | 800 | 4,400 | 2,500 | 2,700 | 2,900 | 3,000 | 3,000 |
| **Khaz** | 1,280 | 400 | 1,600 | 550* | 400 | · | 4,100 | 1,600 | 3,200 | 600 | 2,800 | 1,800 | 2,000 | 2,200 | 2,300 | 2,300 |
| **Phas** | 1,300 | 2,900 | 3,500 | — | 1,100 | 4,100 | · | 3,200 | 3,200 | 4,040 | 5,200 | 4,000 | 4,200 | 4,400 | 4,500 | 4,500 |
| **Ship** | 400 | 400 | 1,600 | 550* | 200 | 1,600 | 3,200 | · | 3,200 | 800 | 4,000 | 2,600 | 2,800 | 3,000 | 3,100 | 3,100 |
| **Tyras** | 3,200 | 3,200 | 3,200 | 1,600* | 3,200 | 3,200 | 3,200 | 3,200 | · | 3,200 | 3,200 | 3,200 | 3,200 | 3,200 | 3,200 | 3,200 |
| **Cors** | 1,200 | 680 | 1,000 | 2,040* | 800 | 600 | 4,040 | 800 | 3,200 | · | 1,420 | 1,500 | 1,800 | 2,000 | 2,100 | 2,100 |
| **Prif** | 4,800 | 3,450 | 3,560 | 2,475* | 4,400 | 2,800 | 5,200 | 4,000 | 3,200 | 1,420 | · | 1,200 | 1,500 | 1,700 | 1,800 | 1,800 |
| **Pisc** | — | 2,000 | 2,000 | 2,100* | 2,500 | 1,800 | 4,000 | 2,600 | 3,200 | 1,500 | 1,200 | · | — | 1,000 | 1,100 | 1,100 |
| **LEnd** | — | 2,200 | 2,200 | 2,200* | 2,700 | 2,000 | 4,200 | 2,800 | 3,200 | 1,800 | 1,500 | — | · | 800 | 900 | 900 |
| **Fortis** | 3,000 | 2,400 | 2,400 | 2,300* | 2,900 | 2,200 | 4,400 | 3,000 | 3,200 | 2,000 | 1,700 | 1,000 | 800 | · | 600 | 500 |
| **Aldar** | 3,100 | 2,500 | 2,500 | 2,350* | 3,000 | 2,300 | 4,500 | 3,100 | 3,200 | 2,100 | 1,800 | 1,100 | 900 | 600 | · | — |
| **Sunset** | 3,100 | 2,500 | 2,500 | 2,350* | 3,000 | 2,300 | 4,500 | 3,100 | 3,200 | 2,100 | 1,800 | 1,100 | 900 | 500 | — | · |

240 live cells. Generate them; do not type them.

---

## 7. Trader Stan's Trading Post

One shop, identical at every port.
[Wiki](https://oldschool.runescape.wiki/w/Trader_Stan%27s_Trading_Post): *"a more
versatile and exotic form of a general store"* — the standard general-store list at
**double** price, plus the specialty lines.

Already staged in this tree:

| piece | where | state |
|---|---|---|
| inv | `CT/configs/all.inv:1326` — `[trader_stan_shop] size=40`, inv id **441** | exists; **no allocation needed** |
| inv mapping | `CT/wiki/shop_inv_map.tsv:1100` | `trader_stans_trading_post → trader_stan_shop, 441, verified` |
| pricing | `CT/wiki/shop_catalog.csv:1101` | `sellmultiplier 2500, buymultiplier 150, delta 20` (wiki: sells 250%, buys 15%, change 2%) |
| stock | `CT/wiki/shop_stock.csv` | **30 lines, 30 resolved, 0 needing review** |
| script | `CT/server/scripts/shop/**` | **missing** — grep for trader/stan returns nothing |

Stock, per the wiki (30 lines):

| item | stock | restock | sell | buy |
|---|---|---|---|---|
| Pot | 5 | 10t | 2 | 0 |
| Jug | 2 | 100t | 2 | 0 |
| Empty jug pack | 6 | 15t | 350 | 21 |
| Shears | 2 | 100t | 2 | 0 |
| Bucket | 3 | 10t | 5 | 0 |
| Empty bucket pack | 10 | 10t | 1,250 | 75 |
| Bowl | 2 | 50t | 10 | 0 |
| Cake tin | 2 | 50t | 25 | 1 |
| Tinderbox | 2 | 100t | 2 | 0 |
| Chisel | 2 | 100t | 2 | 0 |
| Hammer | 5 | 100t | 2 | 0 |
| Newcomer map | 5 | 100t | 2 | 0 |
| Security book | 5 | 100t | 5 | 0 |
| Rope | 2 | 300t | 45 | 2 |
| Knife | 2 | 100t | 15 | 0 |
| Pineapple | 15 | 100t | 5 | 0 |
| Banana | 15 | 100t | 5 | 0 |
| Orange | 10 | 100t | 5 | 0 |
| Bucket of slime | 10 | 100t | 2 | — |
| Glassblowing pipe | 15 | 100t | 5 | 0 |
| Bucket of sand | 10 | 100t | 5 | 0 |
| Seaweed | 20 | 100t | 5 | 0 |
| Soda ash | 10 | 100t | 5 | 0 |
| Lobster pot | 20 | 100t | 50 | 3 |
| Fishing rod | 20 | 100t | 12 | 0 |
| Swamp paste | 30 | 100t | 75 | 4 |
| Tyras helm | 25 | 200t | 1,375 | 82 |
| Raw rabbit | 20 | 100t | 50 | 3 |
| Right eye patch | 5 | 100t | 5 | 0 |
| Bronze cannonball | 250 | 100t | 5 | 0 |

Generate the `.inv` and the `.rs2` with `tools/gen_shop_scripts.py`
(`docs/SHOPS_PLAN.md` §4), same as the other 273 shops, and open it with the existing
proc (`CT/server/scripts/shop/scripts/shop.rs2:95`):

```runescript
[opnpc3,sailing_transport_trader_stan]
…seven parents + leaves…
@trader_stan_shop_open;

[label,trader_stan_shop_open]
~openshop(trader_stan_shop, 2500, 150, 20, "Trader Stan's Trading Post");
```

---

## 8. Content layout and phased plan

```
CT/server/scripts/transport_charter/
  README.md                     # scope, pointer here, Files table,
                                # "Three things that will bite", Testing
  configs/
    charter.constant            # ^charter_<port> ids, ^charter_voyage_delay
    charter_port.dbtable        # port_id INDEXED | arrive coord | name | dbrow id
    charter_port.dbrow          # 16 rows, generated
    charter_fare.dbtable        # route INDEXED (= from*32 + to) | cost
    charter_fare.dbrow          # 240 rows, generated
    charter.spawn               # 10 crew at the 5 new ports
    trader_stan_shop.inv        # 30 stock lines; inv 441 already exists
  scripts/
    charter_npc.rs2             # [opnpc4] Charter, [opnpc3] Trade, [opnpc5] repeat
    charter_menu.rs2            # open 72+885, arm events, decode last_slot
    charter_travel.rs2          # gates, fare confirm, coins, refusals, voyage, telejump
    charter_shop.rs2            # ~openshop(...)
    charter_cheat.rs2           # ::charter, ::charterport, ::charterfare
```

`transport_charter/` sits alongside `canoes/` as a top-level dir. **Adding a directory
requires no build edits** — `sscompile`, `cachepack` and `mock230` all walk recursively
and skip only a leading `.` (`docs/SUMMONING_PORT.md:240`). **No new id lane is needed**:
every npc, interface, varbit and inv already exists in `pack/*.pack`.
`tools/ss_allocate.py --tree` appends the dbtable/dbrow ids on every build.

### Reuse, don't reinvent

| need | existing |
|---|---|
| shop open | `~openshop(inv, buy, sell, haggle, title)` — `shop/scripts/shop.rs2:95` |
| shop `.inv` + `.rs2` generation | `tools/gen_shop_scripts.py`, `docs/SHOPS_PLAN.md` §4 |
| fare confirm menu | `~p_choice3_header` — `interface_chat/scripts/chat.rs2:246` |
| narrated hop, rum confiscation | `[proc,shanks_sail]` — `areas/area_karamja/scripts/captain_shanks.rs2` |
| interface voyage | `[proc,pest_sail]` — `minigames/minigame_pestcontrol/scripts/pest_sail.rs2` |
| coord-keyed dbtable | `canoes/configs/canoe_station.dbtable` |
| index → coord / name | `[proc,canoe_dest_coord]`, `[proc,canoe_dest_name]` |
| dynamic-row picker, `last_slot` decode | `skill_construction/scripts/poh_portal_nexus.rs2` (41 destinations) |
| feature-owned spawns | `canoes/configs/canoes.spawn` |
| world coord → literal | `coord_literal()` — `tools/gen_tele_table.py:358` |

### Phases

Each phase ends with something observable in the running client. Nothing ships on
"the code looks right."

**Phase 1 — data and the shop.** `tools/gen_charter_tables.py` emits `charter_port.dbrow`
(16 rows) and `charter_fare.dbrow` (240 rows), header-stamped with the pinned wiki
revision, cross-checking every destination tile against `charter_ships.tsv`. Shop `.inv`
and `.rs2` from `CT/wiki/shop_stock.csv` via `tools/gen_shop_scripts.py`; `[opnpc3,…]` on
all seven parents.
***Verify:*** `make -C src mock230-scripts` clean, script count moves; right-click **Trade**
at Port Sarim opens a 40-slot shop with 30 lines at double general-store price.

**Phase 2 — travel, on chat menus.** `[opnpc4,…]` → resolve the current port (mirror
`script_7334`'s `inzone` walk) → temporary `~p_choice5` pages so the network can be
exercised before the interface lands → `db_find(charter_fare:route, from*32+to)` →
*"Sailing to \<x\> costs \<n\> coins"* via `~p_choice3_header("Okay" / "Choose again" /
"Cancel")` → §5.2 gate check → `inv_del` coins → narrated hop → `p_telejump`. Also here:
the four refusals, the hard exclusion of Tempestus and Crandor, and writing
`chartering_previous_destination` so `op5` lights up. `[opnpc5,…]` re-runs the last route
through the same fare confirm.
***Verify:*** a mock230 selftest section — charter Port Sarim → Catherby, assert 1,000
coins gone and the player on `0_43_53_40_25`; charter Port Tyras without Regicide, assert
refusal and coins untouched.

**Phase 3 — the cache picker.** Replace the chat pages with
`if_openmain_side(sailing_menu, chartering_menu_side)` + `if_setevents` + `[if_button1,…]`
and the §5.1 sub-id decode. Keep the server-side gate mirror.
***Verify:*** by eye under `SDL_VIDEODRIVER=dummy` with `TORIRS_BMP_SERIES` — the map
opens with the current port highlighted (`graphic_6918`), gated ports absent, side list in
the same order; a pin click and its side-list row reach the same destination.

**Phase 4 — the voyage animation (measurement first).** Probe before writing: open 72, and
299 with `%journey_number` swept 0–20, under the dummy driver, screenshot each, and record
what actually renders — is model 50089 the Gielinor plate, does 299 carry a charter route,
can `if_setposition` move a marker on 72's `content`. Only then choose between tweening a
boat on 72 and driving 299 by route id. Until then Phase 2's narrated hop stays.
***Verify:*** a BMP series showing the ship crossing between two pins, then the arrival
mesbox.

**Phase 5 — the last five ports.** `charter.spawn` for Piscarilius, Land's End, Civitas
illa Fortis, Aldarin and Sunset Coast. Aldarin's square has no spawn file at all, so
confirm the jetty is built and walkable before promising the route.
***Verify:*** walk to each in the live client; two crewmembers on walkable tiles off the
jetty footprint; charter out and back.

**Phase 6 — registration.** Update this doc's status and its §10. A row in
`docs/CONTENT_PORT_QUEUE.md`. Strike charter ships from `docs/MAPLINKS.md:498`.
`tools/check_charter_contract.py` + a `.PHONY` target in `src/makefile`.

---

## 9. Verification

- **`make -C src mock230-scripts`** clean, before/after script counts recorded.
- **`make -C src test-mock230`** — a new section in `src/net/mock/mock230_world.c`,
  modelled on the canoe section at ~line 25784, driving real packets: `OPNPC4` → menu →
  `IF_BUTTON1` → fare confirm → assert coins and arrival tile. Cover a refused quest gate
  and an insufficient-coins case.
- **A mutation table**, as `docs/CANOES.md:336` requires: flip one `charter_fare` cost and
  one `charter_port` arrival coord and record the exact `FAIL` line each produces. A test
  that cannot fail proves nothing.
- **`tools/check_charter_contract.py`** — same shape as `tools/check_gauntlet_contract.py`:
  read this doc alongside the `.dbrow` sources and a fixture, and assert that every fare
  cell, arrival tile and quest gate agrees across doc, dbrow, dbtable 206 and
  `charter_ships.tsv` (flagging, not silencing, the §6 tsv/wiki cost mismatches).
- **By eye** via `run-live.sh` with `MOCK230_SAVES` set. A run that dies mid-voyage
  otherwise saves the player inside the cutscene and the next run reads as a hang — the
  trap `docs/CANOES.md:370` records.
- **Debugprocs** in `charter_cheat.rs2`: `::charter` (to the Port Sarim dock),
  `::charterport <id>`, `::charterfare <from> <to>`.

---

## 10. Open questions

1. **The `port_coord` plane nibble** (§3.1) — 22 of 24 rows carry a `1` in bits 28+.
   Resolve against another known dbrow coord rather than masking blind.
2. **Is model 50089 the whole-Gielinor plate?** Phase 4 measures it.
3. **Does `%journey_number` have a charter route id at all**, or only the trawler (11/12)
   and Pest Control (14/15) values?
4. **Is Aldarin's jetty built and walkable** in this cache? `m22_46` exists at 169 KB but
   has no spawn file, which usually means nothing has been placed there yet.
5. **`category=420`** on the crew parents is unnamed in `CT/pack/category.pack`. Left
   alone — name binds are used instead. Worth minting if more crew content lands.
6. **`inline_name` (col 2) is empty on every row.** Presumably the *"Sailing to \<x\>"*
   inline form; using col 1 `name` instead until something proves otherwise.

---

## 12. As built (2026-08-17)

Phases 1, 2, 3, 5 and 6 of §8 are done. Phase 4 — the voyage animation — is not,
and §13 says why.

**What a player can do now.** Right-click any of the seven trader parents at any
of the sixteen ports: **Trade** opens Trader Stan's Trading Post; **Charter**
opens the cache's own picker — `sailing_menu`'s Gielinor map with a pin per
destination, and `chartering_menu_side`'s scrollable list beside it — with the
current port highlighted and every port a quest has not opened hidden; and after
one voyage **Charter-to \<port\>** repeats the last route in one click. Clicking
a pin quotes the wiki's fare, takes it in coins, and lands the player on that
port's own jetty. Locked ports are refused with the fare untouched; bedsheets,
ectoplasm bedsheets and Crandor are refused too, and Karamjan rum is lost to a
dice game mid-voyage as it is on the Karamja ferry.

**Files.** `OSRS-Content/osrs239-content/server/scripts/transport_charter/`
(README, 7 configs, 6 scripts), `tools/gen_charter_tables.py`,
`tools/check_charter_contract.py`, a `check-charter-contract` target wired into
`mock230-scripts`, and a `mock230_world.c` selftest section with four new
helpers.

**The picker needed no new opcode, and that was the surprise.** It was parked in
§13 of the first draft of this document on the grounds that the server had no
`db_findall`/`db_getrow`. It does — under the RuneScript names `db_listall` and
`db_findbyindex`, both implemented in `mock230_ops_db.c` and both already
exposed to content. The original claim came from grepping the CS2 spellings.

**One engine change did land, and it is contract-hardening rather than a bug
fix.** `DB_LISTALL` is a positional cursor: `db_findbyindex(n)` is the n-th row,
and the picker turns a pin's sub-id into exactly that index. The order it walked
was `configs/all.dbrow` parse order; the order the *client* walks is the one
each `dbindex/dbindex_<table>.dbi` `[master]` block states — ascending row id.
Those agreed only because the exporter happens to emit rows sorted, which is not
a contract a positional API can rest on.

`mock230_db_row_in_table_ordered` (`mock230_db.c`) now serves the `db_listall`
cursor from a lazily-built sorted view, reached only from `query_row`'s
`db_query_column < 0` branch so the `db_find` scan is untouched. Measured
back-to-back on one tree: **50 suite failures with it and 50 without, identical
sets.** A new selftest section, *"db_listall walks ascending row ids"*, walks
every loaded dbtable and asserts it; inverting the comparator makes it report
`21342 step(s) went backwards, first in table 2055`, so it is a real check.

Being straight about its effect: on **this** cache every table's storage order is
already ascending, so the change alters no result today. What it removes is the
picker's dependence on that staying true.

**Verification, and what each layer actually proves.**

| layer | result |
|---|---|
| `sscompile` | clean. Proven to be compiling these files: renaming `charter_fare:cost` to a typo failed at `charter_travel.rs2:22` and nowhere else |
| `check_charter_contract.py` | ok, and mutation-proven — see the table below |
| `mock230 --selftest` | section **"a charter ship takes a fare and sails"**, 9 assertions, all passing |
| the rest of the suite | measured back-to-back on one tree: 57 failures without this work, 51 with it, and **nothing in the with-set that is not in the without-set** |

The remaining failures are other features mid-flight (plunder, zulrah, chompybird,
Inferno, movement) and were failing before this branch touched anything.

**Mutation table.** A check that cannot fail proves nothing.

| mutation | result |
|---|---|
| Catherby's `arrive` moved one tile | selftest: `FAIL the charter should land the player at Catherby's jetty (2792,3417), got 2792,3418` |
| Catherby's `arrive` moved one tile | checker: `charter_port_catherby: arrive (2793,3417), cache port_coord (2792, 3417)`, plus the doc-agreement check |
| one fare changed in one direction | checker: `Catherby -> Port Sarim is 1000 but the reverse is 999` |
| `^regicide_complete` swapped for `^regicide_started` | checker: `^charter_tyras gate does not test ^regicide_complete` |
| a resolved leaf spawned instead of the parent | checker: `charter.spawn spawns '..._crew_man1_piscarilius', which is a resolved leaf` |
| picker stops reading the row from the cache table | checker: `charter_map.rs2 walks the cache table — 'db_listall(chartering_destinations)' is gone` |
| crew speech put back on an `[if_button1]` path | checker: `charter_port.rs2 calls ~chatnpc_anim on a path an [if_button1] can reach — NPC_TYPE aborts there` |

**Four bugs the runtime caught that a clean compile did not:**

1. **`[opnpc4]` was unbound.** The first selftest run failed everything, and the
   direct trigger probe named it: `npc 1328 should have an [opnpc4]`. The cause
   was the harness — `mock230 --selftest` ignores `MOCK230_SCRIPTS` and loads
   `server/scripts/build` unconditionally, so it was running a pack that
   predated the feature. **Check `strings script.dat | grep -c <feature>` before
   believing a selftest failure.**
2. **Speech aborts inside an interface trigger.** Moving the refusal from
   `[opnpc4]` to `[if_button1]` made `~chatnpc_anim` fail with *"NPC_TYPE
   requires an active entity the script does not have"*, taking the refusal with
   it — the click looked like it did nothing. Every line on a path a pin click
   can reach is a `~mesbox` now, and the checker enforces it.
3. **The section left the player at Port Sarim.** A charter is a cross-region
   teleport; the sections after it are written against Lumbridge, and three of
   their assertions failed with nothing in the message to suggest a ship. It
   restores the entry tile now.
4. **The section shifted the shared RNG.** Ticking a voyage walks every npc in
   two scenes, which moved a later wander-radius check off its spawn. It saves
   and restores `srv->rng` — strictly better than the canoe section's
   pin-and-leave.

**Two language and one map trap, all now in the README:**

- The compiler will not take a comparison anywhere but an `if` head, which is
  why `~charter_port_unlocked` is an if-chain and not a `switch_int` of returns.
- A jetty is level-1 geometry with `LINK_BELOW` over a `BLOCK`ed level-0 water
  tile, so reading settings bit 0 alone calls every dock in the game solid. The
  first spawn-tile probe did exactly that and disagreed with eleven ports whose
  traders were already standing on those tiles.

## 13. Still open

**Phase 4 is closed, not deferred.** The voyage is a narrated hop because the
Gielinor-map ship animation is not in this cache — §5.3 sets out the four checks
that establish it, all offline. Nothing further is pending on a client run; what
would be needed is the animation itself, from a newer cache or authored.

**Not verified:** the ten new spawn tiles are checked for terrain walkability
and for falling inside their port's `inzone` box, but **not** for loc occupancy
— a crate already on the tile would only show in the built scene.

**Not resolved:** the `port_coord` plane-nibble bias (§3.1). The generator
asserts the bias is a uniform `1` across every port it emits and refuses to
guess for any row where it is not, which makes the assumption loud rather than
correct.

**`db_find`'s scan order is still storage order, and that is left alone
deliberately.** The engine change described in §12 covers `db_listall` only.
Reordering the shared walk was tried and measured: it also fixes the suite's own
`db_find(quest:id, 1) should resolve a row`, which is currently failing — so the
find path's order is wrong by the suite's own standard too — but it shifts which
row 30-odd other sections see first and destabilises 38 unrelated assertions.
That is a change worth making on its own, with its own verification pass, and
not one to smuggle in behind a content feature.

## 14. Deliberately not done

- The six Sailing ports (Pandemonium, Summer Shore, Red Rock, Barracuda HQ, Deepfin Point,
  Port Roberts) and Tempestus. They are switched off by the cache's own restriction system
  and by `script_9104`; wiring them would mean overriding the client.
- **Crandor.** No arrival tile exists (`port_coord = 0`). The Dragon Slayer refusal line
  is implemented; the destination is not.
- **Decorative patrolling charter ships** (added on live 5 November 2025) — scenery, no
  transport function.
- **Keyboard shortcuts on the destination menu** (added on live 10 September 2025) — a
  client-side affordance on top of the same picker.
