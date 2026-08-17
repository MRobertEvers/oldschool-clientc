# Temple Trekking / Burgh de Rott Ramble

> Written 2026-08-17. Nothing is implemented yet — this is the build plan.
>
> **Behaviour authority (wiki):**
> [Temple Trekking](https://oldschool.runescape.wiki/w/Temple_Trekking) ·
> [Burgh de Rott Ramble](https://oldschool.runescape.wiki/w/Burgh_de_Rott_Ramble) ·
> [Reward token](https://oldschool.runescape.wiki/w/Reward_token) ·
> [Ghast](https://oldschool.runescape.wiki/w/Ghast) ·
> [Nail beast](https://oldschool.runescape.wiki/w/Nail_beast) ·
> [Swamp snake](https://oldschool.runescape.wiki/w/Swamp_snake) ·
> [Giant snail](https://oldschool.runescape.wiki/w/Giant_snail) ·
> [Vampyre Juvinate](https://oldschool.runescape.wiki/w/Vampyre_Juvinate) ·
> [Shade](https://oldschool.runescape.wiki/w/Shade) ·
> [Head (Temple Trekking)](https://oldschool.runescape.wiki/w/Head_(Temple_Trekking)) ·
> [Tentacle (Temple Trekking)](https://oldschool.runescape.wiki/w/Tentacle_(Temple_Trekking)) ·
> [Lumberjack outfit](https://oldschool.runescape.wiki/w/Lumberjack_outfit) ·
> [Abidor Crank](https://oldschool.runescape.wiki/w/Abidor_Crank) ·
> [Druid pouch](https://oldschool.runescape.wiki/w/Druid_pouch) ·
> [Snelm](https://oldschool.runescape.wiki/w/Snelm) ·
> [Sanfew serum](https://oldschool.runescape.wiki/w/Sanfew_serum) ·
> [In Aid of the Myreque](https://oldschool.runescape.wiki/w/In_Aid_of_the_Myreque) ·
> [Darkness of Hallowvale](https://oldschool.runescape.wiki/w/Darkness_of_Hallowvale)
>
> Every id, coord, varbit bit range and map square below was **measured from
> this tree** (`OSRS-Content/osrs239-content/configs/*.compack`,
> `maps/*.jl2`, `configs/all.varbit`, `MULTI_NPCS.md`). Rows marked
> *(to measure)* have not been. Re-measure rather than trusting this prose —
> the same rule the [Gauntlet plan](../GAUNTLET.md) states.
>
> LostCity: **none** (post-254 content). Kronos: no Java port. There is no
> reference implementation to lean on; the cache data below *is* the spec.

---

## 1. Why this is cheaper than it looks

The rev239 cache already ships **the entire authored asset set** for both
minigames: 8 map squares of event terrain, 140 NPC records, 78 loc records,
~50 obj records, 4 interfaces, a dedicated inv, and a fully laid-out varp bit
plan. Almost none of it is referenced by server content today.

```
grep -rn "templetrek\|ttrek\|ramble_" OSRS-Content/osrs239-content/server/scripts/
```

Two real hits, and **both are trigger hazards, not head starts**:

- `quest_sinsofthefather/scripts/sinsofthefather.rs2:242-243` name-binds
  `[opnpc1,templetrek_zombie]` and `[oploc1,templetrek_bridge_broken]` into a
  shared soft-skip label. **A second name-bound trigger on either is a hard
  compile error** (`duplicate-triggers-error-use-categories`). The minigame
  must either route through that quest's existing handler or move both to a
  category — decide in Phase 0, not at the first failed build.
- Sins of the Father also carries its *own* parallel trek assets
  (`myq5_nail_beast_*`, `myq5_trek_juvinate_*`, `myq5_swamp_success_path_1..4`,
  `myq5_swamptree_branch*`). Those are quest-private. Do not reuse them, and do
  not "unify" them with the minigame's set.

Everything else — all twelve followers, all event monsters, every puzzle loc,
every reward obj, all four interfaces — is unreferenced.

The `templetrek_multi_*` / `ramble_multi_*` follower shells are already
**audited and spawning** (see `MULTI_NPCS.md` lines 975–980, 1028–1033 — all
twelve marked `[x]`, all with `0 content refs`). They stand in Burgh de Rott
and Paterdomus right now, hidden behind a varbit nobody sets.

So this is not "build a minigame from scratch". It is: **write the server
content that drives assets that already exist**, plus a small number of engine
gaps (§5).

---

## 2. Prerequisites and gates

| Gate | Constant in tree | Where |
|---|---|---|
| Temple Trekking | `%myreque_2_quest = ^myreque2_complete` | `server/scripts/quests/quest_inaidofthemyreque/configs/myreque2.constant` |
| Burgh de Rott Ramble | `%myq3_main_quest = ^doh_complete` (320) | `server/scripts/quests/quest_darknessofhallowvale/configs/darknessofhallowvale.constant` |

Both quests exist in-tree. In Aid of the Myreque already contains a
**miniature trek** (`myreque2_trek.rs2` — Ivan Strom escort, two juvinates,
teleport on ambush clear). That script is deliberately simplified and is *not*
the minigame; it stays as-is, but it is the closest existing model for
"escort NPC + ambush + advance state".

Wiki-stated player kit (not enforced, but the content must respect it):
axe + knife (puzzle events), a **silver** weapon or
[Efaritay's aid](https://oldschool.runescape.wiki/w/Efaritay%27s_aid)
(juvinates), [druid pouches](https://oldschool.runescape.wiki/w/Druid_pouch)
(ghasts), a [snelm](https://oldschool.runescape.wiki/w/Snelm) (giant snails),
[Sanfew serum](https://oldschool.runescape.wiki/w/Sanfew_serum) (nail beasts).
All of those objs exist in the cache (§4.3).

---

## 3. Session model

```
Burgh de Rott signpost  ──(Temple Trekking)──▶  Paterdomus
        ▲                                            │
        └──────────(Burgh de Rott Ramble)─────────────┘
```

One state machine, two directions. The direction decides which follower roster
is offered, which end coord is "home" and which is "destination", and nothing
else — every event, every reward, every point rule is shared.

A trek is a sequence of **event rooms**. Between rooms the player is teleported
into the next instanced room; inside a room they must clear/solve/evade, then
walk onto the exit loc (`templetrek_swamp_success_path` / `_fail_path` /
`_evade_path`) which advances the machine.

| Route | Difficulty | Events per trek (wiki) | Monster level band | Evasion | Token |
|---|---|---|---|---|---|
| 1 | Easy — circuitous Mort Myre path | 0–9 | ~80 | Free | `templetrek_blue_token` (7776) |
| 2 | Medium — River Salve path | 0–4 | ~110 | After 50% damage dealt | `templetrek_yellow_token` (7774) |
| 3 | Hard — Canifis shortcut / Hollows / boat | 0–5 | ~140 | None | `templetrek_red_token` (7775) |

A trek **fails** if the player *or* the follower dies. On failure the player is
returned to the origin with no token. On success the player receives the
route's token and may claim rewards anywhere.

---

## 4. Complete asset inventory (measured)

### 4.1 NPCs

**Followers — Temple Trekking** (six; the `_multi_` shell is the spawned one,
switched by `%templetrek_npcs_visible`; the bare id is its visible child).
Spawn coords from `MULTI_NPCS.md`:

| Shell (spawned) | Child | Display name | Tier | Spawn |
|---|---|---|---|---|
| `templetrek_multi_retired_soldier_easy` (2939) | `templetrek_retired_soldier_easy` (1567) | Fyiona Fray | easy | 3480, 3241, 0 |
| `templetrek_multi_retired_man_easy` (2938) | `templetrek_retired_man_easy` (1566) | Dalcian Fang | easy | 3481, 3241, 0 |
| `templetrek_multi_woman_med` (2936) | `templetrek_woman_med` (1564) | Jayene Kliyn | medium | 3479, 3237, 0 |
| `templetrek_multi_man_med` (2937) | `templetrek_man_med` (1565) | Valantay Eppel | medium | 3480, 3237, 0 |
| `templetrek_multi_child_hard` (2934) | `templetrek_child_hard` (1562) | Smiddi Ryak | hard | 3475, 3235, 0 |
| `templetrek_multi_oldman_hard` (2935) | `templetrek_oldman_hard` (1563) | Rolayne Twickit | hard | 3476, 3235, 0 |

**Followers — Burgh de Rott Ramble** (switched by `%ramble_npcs_visible`):

| Shell (spawned) | Child | Display name | Tier | Spawn |
|---|---|---|---|---|
| `ramble_multi_easy_fighter` (2944) | `ramble_easy_fighter` (1577) | Adventurer (easy) | easy | 3433, 3486, 0 |
| `ramble_multi_easy_mage` (2945) | `ramble_easy_mage` (1578) | Mage (easy) | easy | 3434, 3486, 0 |
| `ramble_multi_med_mage` (2942) | `ramble_med_mage` (1575) | Apprentice (medium) | medium | 3434, 3483, 0 |
| `ramble_multi_med_ranger` (2943) | `ramble_med_ranger` (1576) | Ranger (medium) | medium | 3435, 3483, 0 |
| `ramble_multi_hard_fighter` (2941) | `ramble_hard_fighter` (1574) | Woman-at-arms (hard) | hard | 3437, 3487, 0 |
| `ramble_multi_hard_ranger` (2940) | `ramble_hard_ranger` (1573) | Forester (hard) | hard | 3438, 3487, 0 |

Wiki tier semantics (inverted from intuition — read carefully):
**easy followers are the *strongest*** (best Attack/Defence/HP, 3 monsters per
combat event, smallest reward); hard followers are the weakest (5–6 monsters,
largest reward).

**Event NPCs — combat:**

| Id range | Name | Event | Notes |
|---|---|---|---|
| 5615–5617 | `templetrek_snake_1/2/3` | Swamp snakes | one per route tier |
| 5618–5621 | `templetrek_snake_dead`, `templetrek_snake_{1,2,3}_dead` | Swamp snakes | knife the corpse → 2–5 hides |
| 5622–5624 | `templetrek_ghast_invis_1/2/3` | Ghasts | pre-reveal form |
| 5625–5627 | `templetrek_ghast_vis_1/2/3` | Ghasts | revealed by druid pouch |
| 5628–5630 | `templetrek_giantsnail_1/2/3` | Giant snails | magic-ranged attack |
| 5631–5633 | `templetrek_shade_1/2/3` | Shades | Riyl / Asyn / Fiyr *(mapping to measure)* |
| 5634–5636 | `templetrek_vampire_1/2/3` | Vampyre Juvinates | silver-only |
| 5637–5639 | `trek_vampire_juve_held_1/2/3` | Vampyre Juvinates | holding the follower |
| 5640–5642 | `trek_vampire_juve_angry_1/2/3` | Vampyre Juvinates | aggro form |
| 5643–5646 | `templetrek_tentacle_arm`, `templetrek_tentacle_head`, `_head_spawning`, `_arm_spawning` | Head & Tentacles | route 3 boat only |
| 5647 | `templetrek_zombie` | Bridge | drops the bronze axe |
| 2946–2948 | `ttrek2_nail_beast_1/2/3` | Nail beasts | triple-hit |
| 5648–5720 | `ttrek2_zombie_diff_{1..9}_ver_{1,2}_{1..4}` | Bridge | 72 records: 9 difficulty × 2 version × 4 — the Undead Lumberjacks |
| 1569–1572 | `trek_turned_vampyre_male_{ben,liam,miala,verak}` | Campsite | Benjamin / Liam / Miala / Verak, the failure form |

**Event NPCs — friendly / puzzle:**

| Id | Name | Role |
|---|---|---|
| 1568 | `templetrek_good_samaritan` | **Abidor Crank** — the friendly event |
| 1579 | `ramble_priest` | Ramble-side priest |
| 1580–1599 | `ramble_starved_type_{1..5}_stage_{1,2,3}` + `_well` | Campsite: the five starving adventurers (Marv, Hank, Wilf, Sarah, Rachel), 3 hunger stages + fed |

*(to measure)* Which of the five `starved_type_N` maps to which wiki name; the
cache stores them positionally.

### 4.2 Locs

| Id range | Name | Role |
|---|---|---|
| 13831 / 13832 / 13833 | `templetrek_swamp_evade_path` / `_success_path` / `_fail_path` | **Room exits.** The machine's advance triggers |
| 13869–13871 | `..._brown` variants of the same three | Alternate terrain theme |
| 13834–13837 | `templetrek_bridge_broken`, `_fixed_1/2/3` | Bridge puzzle, 3 repair stages |
| 13838 | `templetrek_route_direction` | Signpost inside a room *(not map-spawned — dynamic)* |
| 13839 / 13840 | `templetrek_bog_grass`, `templetrek_leaflessbush` | **Bog event — removed from OSRS 2024-09-04.** Vestigial; do not implement |
| 13841 / 13842 | `templetrek_rowboat`, `_ripples` | Route 3 boat |
| 13843–13850 | `templetrek_swamptree_base/_top/_branch/_branch_vine/_small_vines/_small_1/_small_2/_small_empty` | River puzzle — the vine trees |
| 13851–13855 | `templetrek_swamp_plant1..5` | Decoration |
| 13856 / 13857 | `templetrek_pier_rail_medium` / `_high` | Bridge/pier dressing |
| 13858–13860 | `templetrek_swamp_bubbles`, `_large`, `_small` | Decoration |
| 13861–13863 | `templetrek_grass1/2/3` | Decoration |
| 13864 | `templetrek_boat_dummy_poly` | Boat collision proxy (plane 2) |
| 13866 / 13867 | `templetrek_swamp_continue_1` / `_2` | Between-room continue |
| 13868 | `templetrek_swamp_burgh_return` | Bail-out to Burgh |
| 13872 | `templetrek_swing_rucksack` | River puzzle: search for a knife |
| 13873 | `templetrek_dangersign` | Route warning |
| 22497–22531 | `ttrek_starved_{1..5}_stage_{1,2,3}` + `_to_well` + `_fed` | Campsite loc forms (35 records) |
| 23275 / 23277 | `burgh_trek_sign` / `burgh_trek_sign_multi` | **Start signpost** |

`burgh_trek_sign_multi` is map-spawned at `m54_50` local `24,43` →
**absolute (3480, 3243, 0)**, shape 10. `burgh_trek_sign` (23275) is *not*
map-spawned — it is the multi-shell's child.

### 4.3 Objs

**Tokens** — `templetrek_blue_token` 7776, `_yellow_token` 7774,
`_red_token` 7775, plus `_var` variants `templetrek_blue_token_var` 10936,
`_yellow_token_var` 10934, `_red_token_var` 10935.
*(to measure)* what distinguishes `_var` — most likely the token that carries
an earned point value versus the flat one.

**Experience tomes** — 7 skills × 3 tiers, ids 7779–7799, named
`templetrek_tome_<skill>_level_{1,2,3}` for
`fishing, agility, thieving, slayer, mining, firemaking, woodcutting`.
Tier 1/2/3 ↔ blue/yellow/red.

**Drops and puzzle items:**

| Id | Name | Source |
|---|---|---|
| 7773 | `templetrek_swamptree_branch` | River / (ex-bog) |
| 7777 / 7778 | `templetrek_long_vine` / `templetrek_short_vine` | River puzzle: 3 short → 1 long |
| 7800 | `templetrek_snail_shell` | Giant snail |
| 3345–3361 | `shellround_*` / `shellpoint_*` | Snail shells that craft into snelms *(which id the trek snail drops: to measure)* |
| 7801 / 7802 | `templetrek_swamp_snake_hide` / `cert_…` | Knife a snake corpse |
| 10937 / 10938 | `nail_beast_nail` / `cert_…` | Nail beast |
| 10939–10941, 10933 | `ramble_lumberjack_top` / `_legs` / `_hat` / `_boots` | Undead Lumberjack rare drop |
| 10945 | `dummy_ramble_lumberjack_top` | Display dummy |

Note `forestry_lumberjack_*` (28169–28175) is the **separate modern Forestry
shop copy** of the outfit. The trek drops the `ramble_*` ids. Do not merge them.

**Player kit already in cache:** `druid_pouch` 2958 / `druid_pouch_empty` 2957,
`snelm_round_*` 3327–3343, `silver_sickle` 2961, `ivandis_flail` 22398,
`blisterwood_flail` 24699, Sanfew serum (herblore tree).

### 4.4 Inv, interfaces, clientscripts

| Kind | Id | Name |
|---|---|---|
| inv | 509 | `ttrek_follower_inv` — the follower's food pack, 15 slots (wiki) |
| interface | 274 | `trek_rewards` — 13 components, title "Temple Trekking Rewards", `op1=Claim` |
| interface | 329 | `templetrek_map` — 939 lines, the route/progress map |
| interface | 517 | `ttrek_food_give_inv` — the "give food" side inventory |
| interface | 518 | `trek_follower_inv` — "Follower inventory", 6 components, "Spaces left:" |

Client scripts referenced by those interfaces and **present in the cache**:
`227` (window frame title), `92`/`94` (button hover), `8070` (`trek_rewards`
contents build), `7046` (`templetrek_map` onload).

*(to measure)* whether the CS2 VM already covers every opcode 8070 and 7046
use. Run them under `cs2vm2` before writing any server content that opens
those interfaces — this is exactly the trap
[`popout-panel-listeners-and-open-cost`](../../MEMORY.md) describes.

### 4.5 Varps and varbits (measured from `configs/all.varbit`)

`templetrek_main_var` = **varp 700**:

| Varbit | Bits | Width | Meaning |
|---|---|---|---|
| `templetrek_npcs_visible` | 0–0 | 1 | Follower shells visible |
| `templetrek_npc_point_value` | 1–13 | 13 | Accumulated points (0–8191) |
| `templetrek_button_control` | 14–14 | 1 | UI arm |
| `templetrek_last_event` | 15–18 | 4 | Last event id (0–15) |
| `templetrek_event_flash` | 19–23 | 5 | Event flash / highlight (0–31) |
| `templetrek_multi_npc_food_give` | 24–27 | 4 | Food-give state |
| `templetrek_debug_room_id` | 28–31 | 4 | **Room id (0–15)** |

`templetrek_temp_var` = **varp 702**:

| Varbit | Bits | Width | Meaning |
|---|---|---|---|
| `npcs_allowed` | 0–2 | 3 | Follower roster gate (0–7) |
| `templetrek_reward_token_slot` | 3–7 | 5 | Which inv slot holds the token |
| `templetrek_reward_selected` | 8–12 | 5 | Selected reward row (0–31) |

`ramble_main_var` = **varp 983**:

| Varbit | Bits | Width | Meaning |
|---|---|---|---|
| `ramble_npcs_visible` | 0–0 | 1 | Ramble follower shells visible |
| `ramble_lumberjack_drop` | 1–1 | 1 | Lumberjack piece already dropped |

**Consistency check to do first:** `templetrek_npc_point_value` is 13 bits
(max 8191) while the wiki's Head & Tentacles event alone awards 4,000 points
and puzzles award 500. Confirm the wiki's point scale is the same scale this
varbit stores; if a full route-3 trek can exceed 8,191 the server must clamp,
and the *server* must own the running total with the varbit as a display
mirror only. Do not discover this by overflow.

Note also `templetrek_last_event` and `templetrek_debug_room_id` are both 4
bits — **16 slots**, against 8 authored map squares (§4.6). Resolve which is
the real event-type count before building the event table.

### 4.6 Instances / map squares (measured from `maps/*.jl2`)

The event terrain is a contiguous band: **`m31_78` … `m38_78`**
(absolute x 1984–2495, z 4992–5055) — the classic "off-map" trekking area.

| Square | Planes | Locs | Contents |
|---|---|---|---|
| `m31_78` | 0 | 606 | Combat/route rooms — 6 exit-marker sets |
| `m32_78` | 0, 1, 2 | 573 | **All puzzle events**: `bridge_broken` p1 (42,47), `rowboat` p1 (14,42) + `boat_dummy_poly` p2, river vine trees (34–47, 18–22), `dangersign` (5,51), `burgh_return` (3–4,48), `continue_1/2` (3–4,55) |
| `m33_78` | 0, 1 | 533 | Sparse — 8 markers only; two-plane room |
| `m34_78` | 0 | 704 | Combat/route rooms |
| `m35_78` | 0 | 602 | Combat/route rooms |
| `m36_78` | 0 | 591 | Combat/route rooms |
| `m37_78` | 0 | 492 | Combat/route rooms |
| `m38_78` | 0 | 409 | Combat/route rooms — 9 markers |

`m31_78`, `m34_78`, `m35_78`, `m36_78`, `m37_78` carry an **identical exit-marker
layout** (fail at ~29,12; success at ~29,29; fails at 15,36 / 44,36; evades at
5,50 / 25,50 / 37,57; successes at 15,59 / 16,59 / 56,56). That is five terrain
variants of the same room grid — each square packs ~6 rooms on a coarse 16-tile
lattice. Full marker dump:

```sh
cd OSRS-Content/osrs239-content && python3 - <<'EOF'
import re,os
NAMES={13831:'evade',13832:'success',13833:'fail',13869:'evade_br',13870:'success_br',
       13871:'fail_br',13866:'cont1',13867:'cont2',13868:'burgh_return',13873:'dangersign',
       13834:'bridge_broken',13841:'rowboat',13864:'boat_dummy'}
for sq in range(31,39):
    f=f'maps/m{sq}_78.jl2'
    if not os.path.exists(f): continue
    print(f'--- m{sq}_78')
    for line in open(f):
        m=re.match(r'(\d+) (\d+) (\d+): (\d+)',line)
        if not m: continue
        p,x,z,i=map(int,m.groups())
        if i in NAMES: print(f'   p{p} {x:2d},{z:2d} room({x//16},{z//16}) {NAMES[i]}')
EOF
```

**Instancing.** Rooms must be instanced per player — two treks cannot share a
room. The engine ops exist and are proven by the Gauntlet:
`map_instance_alloc` / `map_instance_setchunk` / `map_instance_build` /
`map_instance_coord` / `map_instance_find` / `map_instance_free`
(see `minigame_gauntlet/scripts/gauntlet.rs2:123` `~gauntlet_build_instance`).

A trek room is far smaller than the Gauntlet's 14×14 zones — one room is
~2×2 zones. Allocate per room and free on exit; do **not** allocate the whole
band. `MOCK230_MAPINSTANCE_ZONES` is currently 16 (raised for the Gauntlet) —
verify headroom before assuming a whole-square instance is possible.

---

## 5. Engine surfaces — what exists, what is missing

| Surface | State | Note |
|---|---|---|
| `map_instance_*` | **exists** | Proven by Gauntlet |
| `npc_setfollower` / `npc_findfollower` | **exists** | `mock230_scripts.c:5126` — implies ownership; **one follower slot per player** |
| `npc_setmode(playerfollow)` etc. | **exists** | `mock230_scripts.c:4922`; targeted modes bind the target player |
| npc-vs-npc combat | **exists** | `combat_target_npc` + `[ai_opnpc2]`; see `docs/…npc-vs-npc-combat` notes |
| npc walk / anim / stat / changetype | **exists** | `npc_walk` (2545), `npc_changetype`, `npc_basestat` |
| Multi-NPC per-player shells | **exists** | The twelve follower shells are already audited |
| Per-npc inventory | **MISSING** | Follower food pack. Model it as a **player-scoped inv** (`ttrek_follower_inv`, id 509), not npc state |
| Cache-side inv → server routing | **OPEN** | `pack/` has `npc.server`, `loc.server`, `varp.server`… but **no `inv.server`**. `pack/inv.alloc` says allocation *is* membership for server-allocated invs. Determine whether inv 509 (a cache inv) can be bound server-side at all, or whether a `pack/inv.server` membership file must be added (`content.ini` declares membership per namespace). **Answer this before Phase 1** — it decides whether the follower pack reuses 509 or needs a new allocated inv |
| Follower-slot conflict | **DESIGN** | `npc_setfollower` is single-slot. A player with a summoning familiar out cannot also have a trek follower. Decide: dismiss the familiar on trek start (simplest, matches "no familiars in most minigames"), or widen the engine to two slots. Prefer dismissal — widening the slot is exactly the kind of shared-state change that broke `call familiar` before |
| Follower death → trek fail | **CONTENT** | `[ai_queue3]` on the follower type, as `myreque2_trek.rs2` already does for its juvinates |

**Do not** add an engine op for anything content can express. The Gauntlet
needed exactly two engine surfaces; this should need zero or one.

---

## 6. Event catalogue

Points and evasion per the wiki. "Monsters" = 3 (easy follower) / 4 (medium) /
5–6 (hard).

### Combat events

| Event | NPCs | Mechanic | Evade | Wiki |
|---|---|---|---|---|
| **Vampyre Juvinates** | `templetrek_vampire_1/2/3`, `trek_vampire_juve_held_*`, `_angry_*` | Silver weapon or Efaritay's aid required; up to 10 damage. `_held` form is grappling the follower — kill those first | Routes 1–2 | [link](https://oldschool.runescape.wiki/w/Vampyre_Juvinate) |
| **Ghasts** | `templetrek_ghast_invis_1/2/3` → `templetrek_ghast_vis_1/2/3` | Invisible until a [druid pouch](https://oldschool.runescape.wiki/w/Druid_pouch) reveals them; 8-tick attack; rot food if unfought. Reuse `quest_druidspirit/scripts/ghast.rs2` — the pouch/reveal mechanic is already written there | Routes 1–2 | [link](https://oldschool.runescape.wiki/w/Ghast) |
| **Shades** | `templetrek_shade_1/2/3` | Riyl / Asyn / Fiyr; fast to engage; drop remains feeding [Shades of Mort'ton](https://oldschool.runescape.wiki/w/Shades_of_Mort%27ton). `minigames/game_mortton/` already owns the remains system | Routes 1–2 | [link](https://oldschool.runescape.wiki/w/Shade) |
| **Swamp snakes** | `templetrek_snake_1/2/3` → `_dead` corpses | Fast melee, up to 18 damage (route 3). Knife a corpse for 2–5 `templetrek_swamp_snake_hide` | Routes 1–2 | [link](https://oldschool.runescape.wiki/w/Swamp_snake) |
| **Giant snails** | `templetrek_giantsnail_1/2/3` | Magic-ranged attack up to 17 damage; snelm or Protect from Missiles mitigates; drops snail shell | Routes 1–2 | [link](https://oldschool.runescape.wiki/w/Giant_snail) |
| **Nail beasts** | `ttrek2_nail_beast_1/2/3` | **Three separate hits per attack** (≤4 each on route 3), high accuracy → Protect from Melee. Weak to Sanfew serum. Drops `nail_beast_nail` + big bones | Routes 1–2 | [link](https://oldschool.runescape.wiki/w/Nail_beast) |
| **Head & Tentacles** | `templetrek_tentacle_head` (140) + 4× `templetrek_tentacle_arm` (99), `_spawning` forms | Route 3 boat only. Poison on one part affects all; **the head heals while any tentacle lives**; parts attacking the follower must die first. Awards **4,000 points** | Never (route 3) | [head](https://oldschool.runescape.wiki/w/Head_(Temple_Trekking)) / [tentacle](https://oldschool.runescape.wiki/w/Tentacle_(Temple_Trekking)) |

### Puzzle events (never evadable, 500 points each)

| Event | Assets | Mechanic |
|---|---|---|
| **Bridge** | `templetrek_bridge_broken` → `_fixed_1/2/3`; `templetrek_zombie` (bronze axe); `ttrek2_zombie_diff_*` (72 Undead Lumberjacks) | Kill a zombie for an axe if you have none; chop trees for logs/planks **or** kill Undead Lumberjacks for planks; repair 3 gaps. Requires hammer or equipped axe. Rare `ramble_lumberjack_*` drop, one per trek — that is what `%ramble_lumberjack_drop` (1 bit) gates |
| **River** | `templetrek_swamptree_small_1/2`, `_small_vines`, `_small_empty`, `_branch_vine`, `templetrek_swing_rucksack` | Knife 3 trees → 3× `templetrek_short_vine` → wind into `templetrek_long_vine` → swing across. No knife? search `templetrek_swing_rucksack` |
| **Campsite** | `ramble_starved_type_{1..5}_stage_{1,2,3}` NPCs + `ttrek_starved_{1..5}_*` locs; failure → `trek_turned_vampyre_male_{ben,liam,miala,verak}` | Feed 3–5 starving adventurers (Marv, Hank, Wilf, Sarah, Rachel) before they turn. Three hunger stages, then `_to_well`, then `_fed`. **Counts as a combat event for reward scaling.** Note: this event is currently unreachable on live OSRS due to a Jagex bug — the authored data is complete, so implement it and record the deliberate divergence |
| **Bog** | `templetrek_bog_grass`, `templetrek_leaflessbush` | **Removed from OSRS 2024-09-04.** Do not implement. The two locs stay in the cache as dead data |

### Friendly event

| Event | NPC | Mechanic |
|---|---|---|
| **Abidor Crank** | `templetrek_good_samaritan` (1568) | Heals player *and* follower, boosts Defence by up to 12, restores 15 HP, converts rotten food to stew. **0 points.** [wiki](https://oldschool.runescape.wiki/w/Abidor_Crank) |

### Follower mechanics

- Follower carries **max 15 food** in `ttrek_follower_inv` (509), loaded before
  departure through interface 518, topped up mid-trek through 517.
- Pre-trek the follower accepts **only** the wiki food list (thin/lean/fat
  snail meat, salmon, stew, cooked slimy eel, cooked meat, tuna, cooked
  fishcake, lobster, bass, redberry/apple/garden/fish pie, cake, chocolate
  cake, cooked sweetcorn, potato with butter). **Mid-event resupply heals a
  flat 10 regardless of food** — that asymmetry is deliberate, keep it.
- Food is retained between treks; **lost on logout**, not on escape/teleport/
  completion. That means the pack is player-persistent state and must survive
  the save round-trip except across a logout — encode the wipe in the
  `[logout]` trigger (see the logout-trigger notes in `MEMORY.md`).
- Follower dies → trek fails. Player dies → trek fails.

---

## 7. Rewards

Token → `trek_rewards` (interface 274) → pick one row → `op1=Claim`.
`%templetrek_reward_token_slot` names the inv slot holding the token,
`%templetrek_reward_selected` the chosen row.

Reward amount scales with **token colour × monsters fought and their levels ×
puzzles completed** — i.e. the accumulated `templetrek_npc_point_value`.

| Reward | Blue (r1) | Yellow (r2) | Red (r3) |
|---|---|---|---|
| Pure essence | see note | see note | see note |
| Bow string | 100–200 | 150–250 | 200–300 |
| Silver bar | 5–50 | 50–100 | 100–150 |
| Grimy herbs (tarromin / harralander / toadflax) | 5–15 | 10–20 | 15–30 |
| Coal + iron ore (coal = 2× iron) | 21–72 | 66–165 | 150–216 |
| Watermelon seeds | 10–20 | 15–30 | 20–40 |
| Raw lobster | 2–35 | 30–65 | 60–80 |
| XP tome (1 of 7 skills) | 1,100–1,650 xp | 2,035–3,025 xp | 4,015–5,005 xp |

**Pure essence is unresolved.** The [Temple Trekking](https://oldschool.runescape.wiki/w/Temple_Trekking)
page and the [Reward token](https://oldschool.runescape.wiki/w/Reward_token)
page give different ranges (240–760 / 370–660 / 740–1,320 versus
50–150 / 120–380 / 370–660). Read the wikitext tables directly and pick one;
do not average them.

Tomes are objs, not instant XP: `templetrek_tome_<skill>_level_{1,2,3}`, tier
by token colour, 1/7 chance of skill *(to measure: whether the skill is rolled
on claim or chosen)*.

---

## 8. Content layout

Follow the existing minigame convention
(`server/scripts/minigames/minigame_<name>/{configs,scripts}`). The Mort'ton
shade minigame next door uses `game_mortton/` — match the newer
`minigame_` prefix.

```
OSRS-Content/osrs239-content/server/scripts/minigames/minigame_templetrek/
  configs/
    templetrek.constant        # route ids, event ids, point values, coords
    templetrek.varp            # only if a server-side varp is genuinely needed
    templetrek_rooms.dbrow     # room table: square, local anchor, event type, difficulty
    templetrek_followers.dbrow # follower table: shell/child id, tier, stats
    templetrek_rewards.dbrow   # reward rows: obj, per-colour min/max
  scripts/
    templetrek_shared.rs2      # ~templetrek_* procs: state accessors, point award
    templetrek_start.rs2       # signpost, follower select, direction, food loading
    templetrek_session.rs2     # the state machine: room build, advance, fail, finish
    templetrek_rooms.rs2       # instance alloc/build/free per room
    templetrek_follower.rs2    # follow, feed, damage, death, ttrek_follower_inv
    events/
      ev_vampyre.rs2  ev_ghast.rs2   ev_shade.rs2    ev_snake.rs2
      ev_snail.rs2    ev_nailbeast.rs2  ev_tentacles.rs2
      ev_bridge.rs2   ev_river.rs2   ev_campsite.rs2
      ev_abidor.rs2
    templetrek_rewards.rs2     # interface 274, token claim
    templetrek_debug.rs2       # ::~trek* debugprocs
```

Ramble is **not** a second directory. It is a direction flag threaded through
`templetrek_session.rs2` plus its own follower table rows and
`%ramble_npcs_visible` / `%ramble_lumberjack_drop` writes.

**Namespace hazard.** `MEMORY.md` records that duplicate script names and
duplicate debugproc names are a hard compile error, and that name binding
kills category binding. The 72 `ttrek2_zombie_diff_*` records must be handled
by a **category** or a dbrow lookup, never 72 name-bound `[ai_queue3]` blocks.
Check `configs/all.category` for an existing trek category before inventing one.

---

## 9. Phased plan

Each phase ends with something observable in the running client. Nothing ships
on "the code looks right".

**Phase 0 — resolve the four blockers.** (a) Can inv 509 be bound server-side,
or is a `pack/inv.server` membership file required? (b) Do clientscripts 8070
and 7046 run clean under `cs2vm2`? (c) Is `templetrek_npc_point_value`'s
13 bits the same scale the wiki quotes? (d) How does the minigame bind
`templetrek_zombie` / `templetrek_bridge_broken` given Sins of the Father
already name-binds both (§1)? Each is a half-day and each can invalidate a
later phase.

**Phase 1 — signpost and follower selection.** Set `%templetrek_npcs_visible`
and `%ramble_npcs_visible`, wire `burgh_trek_sign_multi` at (3480, 3243, 0) and
the Paterdomus counterpart, dialogue for all twelve followers, `npcs_allowed`
gate. *Verify:* the six Burgh followers appear and talk after IAOM; the six
Paterdomus followers appear after DoH; neither appears before.

**Phase 2 — follower pack.** Interfaces 518 and 517, `ttrek_follower_inv`,
the wiki food whitelist, 15-slot cap, retention across treks, logout wipe.
*Verify:* load food, log out, log in, pack is empty; load food, teleport away,
pack survives.

**Phase 3 — one room, no event.** Instance a single room from `m31_78`, walk
the player in with the follower via `npc_setfollower` + `npc_setmode`, exit on
`templetrek_swamp_success_path`, free the instance. *Verify:* screenshot in
the instance, follower behind the player, and a clean `map_instance_free`
(no zone leak across ten consecutive runs).

**Phase 4 — the route machine.** Route 1/2/3 lengths, room sequencing, the
`templetrek_map` interface (329), fail on player or follower death, return to
origin. *Verify:* a complete route-1 trek with zero events; a deliberate
follower kill fails the trek.

**Phase 5 — combat events.** Seven event types, difficulty scaling from the
follower tier, evade rules per route, point award per kill. *Verify:* one
headless combat-harness run per event type (the recipe in the Zuk notes),
asserting monster count by tier and that evade is refused on route 3.

**Phase 6 — puzzle events.** Bridge, River, Campsite. Not Bog.
*Verify:* each solved end-to-end; the bridge's Lumberjack drop fires at most
once per trek (`%ramble_lumberjack_drop`).

**Phase 7 — Head & Tentacles.** Route 3 boat, the heal-while-tentacle-alive
rule, shared poison. *Verify:* the head does not heal after all four arms die.

**Phase 8 — rewards.** Interface 274, token claim, all eight reward rows,
tomes. *Verify:* claim each row at each colour; amounts inside the wiki
ranges; the token is consumed exactly once.

**Phase 9 — Abidor Crank, polish, debugprocs.**

---

## 10. Debugprocs

Names must be globally unique — the compiler rejects duplicates, and
`CRYSTAL_SET_COMMAND.md` records what a duplicate cost last time.

| Command | Effect |
|---|---|
| `::~trek` | Force IAOM+DoH complete, teleport to the Burgh signpost |
| `::~trekroom <id>` | Drop straight into room `<id>` with a chosen follower |
| `::~trekevent <name>` | Force the next event type |
| `::~trektoken <colour> <points>` | Grant a token at a given point value |
| `::~trekfail` | Kill the follower, exercise the fail path |

---

## 11. Open questions

1. **16 vs 8.** `templetrek_last_event` and `templetrek_debug_room_id` are 4
   bits each; only 8 map squares carry event terrain, and the five identical
   squares suggest ~6 rooms per square. Recover the real room table before
   writing `templetrek_rooms.dbrow`.
2. **`_var` tokens.** What separates `templetrek_blue_token` (7776) from
   `templetrek_blue_token_var` (10936)?
3. **Shade mapping.** Which of `templetrek_shade_1/2/3` is Riyl / Asyn / Fiyr,
   and do their remains feed the existing `game_mortton` system unchanged?
4. **Snail shell.** Does the trek snail drop `templetrek_snail_shell` (7800),
   the snelm-crafting `shellround_*` / `shellpoint_*` (3345–3361), or both
   (the wiki's "perfect shell" variant)?
5. **Starved-adventurer names.** Positional `type_1..5` → Marv / Hank / Wilf /
   Sarah / Rachel.
6. **Familiar conflict.** Confirm the decision to dismiss a summoned familiar
   on trek start rather than widen the engine's single follower slot.
7. **Paterdomus signpost.** `burgh_trek_sign_multi` is map-spawned at Burgh;
   the Paterdomus-side start object was not found in `maps/*.jl2`. Find it or
   confirm the Ramble starts from dialogue alone.
