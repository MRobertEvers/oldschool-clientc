# Canoes — River Lum (Misthalin) and River Dougne (Kandarin)

Everything the canoe system needs is already in `cache.osrs239`: the station
locs are placed in `maps/`, the assistants are in `all.npc`, all three
interfaces are unpacked, the state varbits exist, and — the part that is easy
to miss — **the paddling cutscene has its own map square, built and stocked,
sitting off the edge of the world at `m28_70`.** Nothing in this feature had to
be invented; it had to be *found* and then driven from the server.

This document is the map of what was found and what the server half has to do
with it. The implementation is `server/scripts/canoes/`.

---

## 1. How the system works (OSRS Wiki)

Two rivers, ten stations, four canoes.

**River Lum** (Misthalin, free-to-play), upstream → downstream:

| # | Station | Assistant | Station loc | Player origin | Arrival tile |
|---|---|---|---|---|---|
| 1 | Lumbridge | Barfy Bill (1326) | 12163 @ (3241,3235) | (3243,3237) | (3240,3242) |
| 2 | Champions' Guild | Tarquin (1323) | 12164 @ (3200,3341) | (3202,3343) | (3199,3344) |
| 3 | Barbarian Village | Sigurd (1324) | 12165 @ (3110,3409) | (3112,3411) | (3109,3415) |
| 4 | Edgeville | Hari (1325) | 12166 @ (3130,3508) | (3132,3510) | (3128,3503) |
| 5 | Ferox Enclave | Marten (10370) | 39638 @ (3155,3628) | (3154,3630) | (3154,3638) |
| 0 | Wilderness Pond | — | — | — | (3141,3796) |

**River Dougne** (Kandarin, members; added 1 April 2026), upstream → downstream:

| # | Station | Assistant | Station loc | Player origin | Arrival tile |
|---|---|---|---|---|---|
| 6 | Castle Wars | Lara (15605) | 60845 @ (2437,3133) | (2439,3135) | (2436,3134) |
| 7 | Tree Gnome Village | Porkai (15604) | 60846 @ (2483,3190) | (2485,3192) | (2483,3188) |
| 8 | The Clocktower / S. of Ardougne Zoo | Pete (15603) | 60847 @ (2575,3258) | (2579,3260) | (2577,3261) |
| 9 | Chaos Druid Tower | Amergin (15602) | 60848 @ (2573,3359) | (2573,3358) | (2571,3360) |
| 10 | Tree Gnome Stronghold / N. of Combat Training Camp | Kaiquir (15601) | 60849 @ (2525,3409) | (2525,3408) | (2523,3408) |

The station numbers in the left column are **not decoration** — they are the
values the cache's own clientscripts expect in `canoe_startfrom` (varbit 1846),
and they are also the destination index each `destination_N` component's
`onload` hook passes to clientscript 7886. Wilderness Pond is index 0.

The canoes, and how far each travels:

| Canoe | Woodcutting | XP | Reach | `canoe_type` (varbit 1843) |
|---|---|---|---|---|
| Log canoe | 12 | 30 | 1 stop | 1 |
| Dugout | 27 | 60 | 2 stops | 2 |
| Stable dugout | 42 | 90 | 3 stops | 3 |
| Waka | 57 | 150 | any stop + Wilderness Pond | 4 |

Cycle: **Chop-down** the station tree → **Shape-Canoe** (interface 416 picks
the type) → **Float Canoe** → **Paddle Canoe** (interface 953/952 picks the
destination) → the cutscene → you arrive and the canoe sinks. A canoe is
single-use; the return trip needs a new one, which is why an axe matters more
than the canoe does.

Two details that are easy to get wrong and are both confirmed by the cache's
own tables (§3):

- **Reach is asymmetric at Ferox.** Ferox → Edgeville is one stop (a log canoe
  does it), but Edgeville → Ferox needs a waka. The same holds for every other
  Lum station: Ferox is reachable *only* by waka from the south.
- **Wilderness Pond is waka-only and one-way.** There is no station there, so
  no canoe can be built for the return trip.

Since 1 April 2026 every assistant also has a **Store-axe** option (`op3` on
all ten npc records), and the stored axe is shared across all ten stations and
usable *without* withdrawing it.

Sources: [Canoe](https://oldschool.runescape.wiki/w/Canoe),
[Canoe Station](https://oldschool.runescape.wiki/w/Canoe_Station),
[River Dougne](https://oldschool.runescape.wiki/w/River_Dougne),
[Update:Remaining Getting Around Changes](https://oldschool.runescape.wiki/w/Update:Remaining_Getting_Around_Changes).
The reach table above is cross-checked against
`tools/data/shortest_path/transports/canoes.tsv` (the RuneLite shortest-path
plugin's own transport dump, already vendored here), which agrees row for row.

---

## 2. The cutscene backdrop — `m28_70`

> During the canoeing cutscene, the camera, player, and boat are in fixed
> positions on the map, while the background scenery like bullrushes and cave
> scenery move across the scene. Scenery objects are unable to move, so instead
> they are programmed as NPCs that "walk" across the frame, and being NPCs,
> their names are coloured yellow in their Choose Option menus, rather than the
> cyan which is typical of scenery.
>
> — [Canoe](https://oldschool.runescape.wiki/w/Canoe), OSRS Wiki

The paddling sequence is not an animation played where you stand. It is a real
place. `maps/m28_70.jm2` / `.jl2` is a map square that exists for nothing else:
**world x 1792–1855, z 4480–4543, plane 0** — up in the off-world band where
instances and cutscene sets live.

It holds 229 locs and not one of them is scenery you could walk to:

| Loc | id | count | what it is |
|---|---|---|---|
| `waterfall_overlay` | 754 | 221 | the flowing water, laid down both channels |
| `canoeing_river_multi_canoe` | 12167 | 2 | the canoe you sit in |
| `wake2` / `wake2_left` | 289 / 290 | 2 each | the bow wake, one either side of each canoe |
| `bgsound_stream_medium_20` | 16393 | 1 | ambient water, wide channel |
| `bgsound_stream_small_20` | 16394 | 1 | ambient water, narrow channel |

Two channels run north–south through the square, and there is one staged canoe
in each:

```
   local x:  0        23-27                     52-54        63
             |          ||                        ||          |
  z 0..63    .......... ~~~~~ ..................... ...........   wide channel
  z 3..24    .......... ~~~~~ ..................... ~~~ .......   narrow channel
```

| Scene | Canoe tile | Wake tiles | Ambient | Channel |
|---|---|---|---|---|
| River | **(1817, 4514, 0)** = `0_28_70_25_34` | (1816,4514), (1818,4514) | medium stream @ (1820,4514) | 5 tiles wide, full height |
| Cave | **(1845, 4491, 0)** = `0_28_70_53_11` | (1844,4491), (1846,4491) | small stream @ (1843,4489) | 3 tiles wide, z 4483–4504 |

`canoeing_river_multi_canoe` is a multiloc on **`canoe_type`** — the same varbit
the shaping menu writes — so the boat under the player is whichever canoe they
actually built:

```
[canoeing_river_multi_canoe]
width=3
multivarbit=canoe_type
multiloc1=canoeing_log        // 0 (unused)
multiloc2=canoeing_log        // 1  log canoe
multiloc3=canoeing_dugout     // 2  dugout
multiloc4=canoeing_catamaran  // 3  stable dugout
multiloc5=canoeing_waka       // 4  waka
multiloc6=-1
```

Each of those four is `blockwalk=0 blockrange=0` with `anim=canoeing_bobbing_inwater`
(seq 3306) — they bob on the spot while the player sits in them. The player's
own animation is `canoeing_rowing` (seq 3302, 23 frames, `verticaloffset=8`,
`righthand=ogre_arrow_shaft_5` — that is the paddle model).

### The moving scenery is npcs

This is the part the wiki quote is about, and the cache backs it up: eight
records in `all.npc` with no combat level, no minimap dot, `turnspeed=0`, and a
`footprintsize` far larger than any creature's.

| npc | id | model | size | used in |
|---|---|---|---|---|
| `canoeing_scenery_1` "Trees" | 3332 | 12021 | 5 | river channel |
| `canoeing_scenery_2` "Trees" | 3333 | 12022 | 5 | river channel |
| `canoeing_bullrush` "Bullrush" | 3335 | 5564 | 1 | river channel |
| `canoeing_bullrush_leaf` "Bullrush" | 3336 | 1674 | 1 | river channel |
| `canoeing_cavemouth` "Cavemouth" | 3334 | 12018 | 5 | cave approach |
| `canoeing_cave_scenery_1` | 3337 | 12015 | 3 | cave channel |
| `canoeing_cave_scenery_2` | 3338 | 12016 | 3 | cave channel |
| `canoeing_cave_scenery_3` | 3339 | 12017 | 4 | cave channel |

They are `npc_add`ed upstream of the canoe and walked past it. The canoe never
moves; the trees do.

### Camera

`cam_moveto` / `cam_lookat` / `cam_reset` are all hosted
(`torirs_server_scripts.c:7713`), signature `(coord, height, speed, acceleration)`.
The camera sits off the bank looking at the canoe tile, and `cam_reset` runs
before the teleport out — a camera left locked survives the scene change.

The shot is **across** the channel, not along it: the eye stands on the boat's
own z line on the east bank, so the river lies flat across the frame and the
canoe is broadside. Two things are deliberately off-centre, both compensating
for the same thing — this client draws the world across the whole canvas with
the gameframe over it, so the look point lands at *canvas* centre and not at the
centre of the visible 3D viewport. `^canoe_cam_look_height` (40) aims below the
boat to lift it clear of the chatbox; the look-at's z is one tile past the boat
to slide it left of the sidebar. Screen-right is +z for a camera facing west.

`cinemaCamera` clamps the derived pitch to `[128, 383]` (Client-TS 3364,
`app_cinema_angles` matches), so 22.5 degrees is the flattest shot available no
matter how low the eye goes — an eye height that asks for less just wastes
distance. The river and cave eyes stand on very different ground (flat h13
against the cave trench's h44 face) and so carry separate heights.

---

## 3. What the cache already does client-side

The entire client half is decompiled in `scripts/`, and it decides more than it
looks like. The server's job is to set three varbits and open the right
interface; the client draws the rest.

### Interface 416 `canoeing` — the shaping menu

`universe`'s `onload` runs **clientscript 3093** (`canoe_init`), which builds
the title, the close button, and then calls `~canoe_setup` (3095). That proc is
where the level gating lives:

```
[proc,canoe_setup](...)
if (stat(woodcutting) < 12) { if_close; mes("You must have at least level 12 woodcutting to start making canoes."); return; }
~canoe_name("A Log.", <log>, "Log canoe");
if (stat(woodcutting) < 27) { if_setmodel(model_12104, <dugout_model>); }
else { if_setmodel(model_12103, <dugout_model>); ~canoe_name("A Dugout.", <dugout>, "Dugout canoe"); }
...
```

Below the level the model is swapped for a greyed-out variant and **no op is
set** — an unreachable canoe simply has no "Make" verb. The op itself is
`cc_setop(1, "Make<col=ff9040> <name>")` on a `cc_create`d child of the four
container components:

| component | id | canoe |
|---|---|---|
| `canoeing:waka` | 11 | Waka |
| `canoeing:stable_dugout` | 12 | Stable dugout |
| `canoeing:dugout` | 18 | Dugout |
| `canoeing:log` | 20 | Log |

`if_setonstattransmit(... {woodcutting})` on `universe` re-runs the whole setup
when Woodcutting changes, so the menu un-greys itself live.

**The op is on a dynamic child at sub-id 0, and that rules out
`if_addresumebutton`**: `handle_if_button_op` returns early for `sub == 0` on a
registered resume button (`torirs_server_world.c:6800`) precisely so a `~p_choice*`
title row cannot answer for a real row. These four are driven with
`if_setevents(..., 0, 0, ^if_event_op1)` and `[if_button1,canoeing:log]`-style
triggers instead.

### Interfaces 953 `canoe_map_lum` / 952 `canoe_map_dougne` — the travel maps

**These take the keyboard, and the cache never gives it back.**
`canoe_map_init` (clientscript 3099) opens with `~chatdefault_stopinput` —
`%varcint11 = 1` plus `if_setonkey(null, chatbox:universe)` — because each
destination component binds the place's initial letter (`canoe_location_setup`,
3100). Nothing releases it: the X runs `canoe_close` (3098), which is `if_close`
and nothing else. The release is the server's, `runclientscript*(2158)`, and
`~canoe_travel_release` in `canoe_travel.rs2` is where it lives; every path that
closes the map calls it, including the `[if_close]` triggers that Escape and the
X arrive on. Without it the chatbox cannot accept a key for the rest of the
session **and** every client-side hotkey stands down with it — this client reads
varcint11 in `app_iface_text_input_focused` — which is how a canoe trip used to
leave the arrow-key camera dead. Same bug as the settings search box; see
`interface_settings_side/scripts/settings_side.rs2` §"Search-box teardown".

`universe`'s `onload` runs **clientscript 3099** (`canoe_map_init`), which
branches on `canoe_river` (varbit 20259): `0` → River Lum wiring (7884), else
River Dougne wiring (7134). Each sets one destination's op:

```
[proc,canoe_location_setup](component $c, int $blocked, int $river, string $name, string $idx)
if ($blocked = 0) { if_setop(1, append("Travel to<col=ff9040> ", $name), $c); ... }
else              { if_setop(1, "", $c); }
```

`$blocked` comes from `~canoe_return_locations` (3104), which reads
`canoe_startfrom` and that station's own state varbit and then dispatches to one
of four hard-coded reach tables — `canoe_paddle_log` (3105), `_dugout` (3106),
`_stable_dugout` (3107), `_stable_waka` (3108). **`1` means blocked, `0` means
available**, and the six-tuple is ordered
`(dest1, dest2, dest3, dest4, wilderness/dest5, ferox)`.

Those four tables are the authority on reach, and `canoes.dbrow` mirrors them
exactly so the server can refuse a destination the client greyed out. They also
settle the Ferox asymmetry noted in §1 — it is in the cache, not a wiki typo.

The clickable components are the six `destination_N` containers (static, so the
click arrives with `sub == -1` and the `if_button1` route is clean):

| interface | component ids | destination index |
|---|---|---|
| 953 lum | 7, 12, 17, 22, 27, 28 | 1, 2, 3, 4, 5, **0** (wildy) |
| 952 dougne | 7, 12, 17, 22, 27 | 6, 7, 8, 9, 10 |

`clientscript 7886` on each `destination_N` also draws the "you are here"
marker: `if ($idx > 0 & $idx = %varbit1846) { if_setmodel(model_60136, ...) }`.
That is why `canoe_startfrom` has to be right *before* the interface opens.

### The station multiloc

All ten station records (12163–12166, 39638, 60845–60849) are the same shape:
`width=5 length=2 blockwalk=1 forceapproach=27`, one `multivarbit` each, and
fifteen `multiloc` slots. The varbit value *is* the state:

| value | child | op1 |
|---|---|---|
| 0 | `canoestation_tree` | Chop-down |
| 1–4 | `canoestation_{log,dugout,stabledugout,waka}` | Float Log / Float Canoe |
| 5–8 | `canoestation_*_inwater` | Paddle … (has `anim=canoeing_station_animations`, the launch splash) |
| 9 | `canoestation_tree_falling` | — (animated) |
| 10 | `canoestation_fallen_tree` | Shape-Canoe |
| 11–14 | `canoeing_*_canoeing_station_in_water` | Paddle Canoe — **the settled, travel-ready state** |

11–14 and not 5–8 is not a guess: `~canoe_return_locations` switches on exactly
`11 / 12 / 13 / 14` and returns "everything blocked" for anything else, so a
player who reached the map in state 5–8 would find every destination greyed
out. 5–8 is the transitional splash and the script only holds it for the two
ticks the push-off animation runs.

State varbits, one per station (all under player varps, so canoe state is
per-player):

```
1839 canoestation_state_lumbridge              10527 …_sanctuary        (Ferox)
1840 canoestation_state_championsguild         20254 …_castle_wars
1841 canoestation_state_barbarianvillage       20258 …_tree_gnome_village
1842 canoestation_state_edgeville              20256 …_ardougne_zoo
                                               20257 …_chaos_druid_tower
                                               20255 …_tree_gnome_stronghold
```

RuneScript cannot index a varbit, so `~canoe_state_get` / `~canoe_state_set`
are ten-way switches on the station index — the same shape clientscript 3103
already is.

---

## 4. Server-side model

`loc_type` inside an `[oploc]` trigger is the **multiloc-resolved child**
(`torirs_server_world.c:1554`), not the base, so a trigger cannot be bound per
station: `canoestation_tree` is one record shared by all ten. Station identity
comes from `loc_coord` through a `db_find` on `canoe_station:coord`, the same
shape `~maplink_try` uses.

```
[canoe_station]
column=coord,coord,INDEXED,REQUIRED   // the station loc's own SW tile
column=station,int,REQUIRED           // 1..10, the canoe_startfrom index
column=river,int,REQUIRED             // 0 = Lum, 1 = Dougne
column=arrive,coord,REQUIRED          // where a canoe landing here puts you
```

Reach is a second table keyed on `(station, canoe_type)` giving the six
destination flags, mirroring clientscripts 3105–3108.

---

## 5. Assets

| kind | what |
|---|---|
| seq 3301 | `canoeing_pushing_into_water` — Float Canoe |
| seq 3302 | `canoeing_rowing` — the cutscene paddle |
| seq 3303 | `canoeing_getting_in` — boarding |
| seq 3304 | `canoeing_station_animations` — on the loc, states 5–8 |
| seq 3305 | `canoeing_sinking` — on locs 12159–12162 |
| seq 3306 | `canoeing_bobbing_inwater` — on the cutscene canoes |
| seq `human_canoeing_carve_<axe>` | shaping, one per axe tier (plus 2h forms) |
| sound 2729 | `build_canoe` |
| sound 2731 | `canoe_roll` |
| sound 2732 | `canoe_sink` |
| sound 2728 | `canoe_paddle_loop` |
| obj 7414 | `canoeing_paddle` "Paddle" — carried by the rowing seq, not an inventory item |

`human_canoeing_carve_*` seqs already carry their own frame sounds
(`sound=6,2735` and `sound=13,3775`), so the carve is **not** given a
`sound_synth` — same trap as the woodcutting swing, see
`docs/SKILLING_SOUNDS.md` §4.2.

---

## 6. Verification

**Selftest.** `ToriRSServer selftest: a canoe is chopped, shaped, floated and
paddled` (`torirs_server_world.c`) drives the whole chain through the real packets at
the real Lumbridge station: `OPLOC1` on the station until the tree falls,
`IF_BUTTON1` on `canoeing:log` to shape it, `OPLOC1` to float it, `OPLOC1` to
board, then two destination clicks — Edgeville, which a log canoe must be
refused, and the Champions' Guild, which it must reach. It asserts the player
is seated at 1817,4514 on the way past, which is the only thing that
distinguishes "rode the cutscene" from "teleported straight there".

Both halves were mutation-tested rather than assumed:

| mutation | result |
|---|---|
| `canoe_station.dbrow` Lumbridge coord moved one tile | `FAIL chopping the station should leave a fallen tree (state 10), got 0` |
| `^canoe_reach_log` 1 → 3 | `FAIL a log canoe asked for Edgeville should be refused, not flown` + the player found at 1817,4514 |

The suite is otherwise clean. Note that this section pins `srv->rng`, as the
woodcutting section above it already does, so it shifts the draws every later
section sees — a pre-existing flaky npc-pursuit check changes answer either
way, in both directions, with and without this content.

**The reach rule** in `~canoe_reach_allowed` was checked cell by cell against
clientscripts 3105–3108: all 220 cells (4 canoe types × 10 stations × 5–6
destinations) agree, with zero mismatches.

**By eye,** through the real client under `SDL_VIDEODRIVER=dummy` with
`TORIRS_NET_CHEAT="canoeride 4"`: the ride seats the player in the waka on
`m28_70`, the camera locks to the bank shot, `canoeing_rowing` plays, the tree
and bullrush npcs drift past, `cam_reset` restores the follow camera and the
player lands at Barbarian Village with "Your canoe sinks behind you."
`::canoecave 4` shows the same with the cave scenery closing over the channel.

One trap worth writing down: **use `TORIRSSERVER_SAVES` for these runs.** A run that
ends mid-ride saves the player sitting in the canoe at 1817,4514, and the next
run logs in there — which reads exactly like a cutscene that never exits. The
first two attempts at this verification chased that and not the code.

## 6b. The station never changed on screen (fixed 2026-08-22)

Everything in §6 passed while a player at a canoe station watched the tree stand
still through the whole chop / shape / float / paddle chain. The chain was
working; none of it was reaching the client. Two independent breaks, one on each
side of the wire.

### The varbits were never transmitted

A canoe station is ONE placed loc — `canoeing_canoestation_lumbridge` and its
nine siblings — and its fifteen states are multiloc children the **client**
picks with `canoestation_state_<name>`. So "the loc changes" is not something
the server draws; it is a varbit reaching the client and the client remorphing.

`ToriRSServer_WorldMarkVarp` sends nothing for a varp whose `.varp` config does
not say `transmit=yes`, and an *undeclared* varp is server-only. Five of the six
carriers were undeclared:

| varp | carries |
|---|---|
| `canoeing_river_lum` | Lumbridge, Champions' Guild, Barbarian Village, Edgeville |
| `canoeing_river_lum_2` | Ferox Enclave |
| `canoeing_river_dougne` | Castle Wars, Tree Gnome Stronghold, Clocktower, Chaos Druid Tower |
| `canoeing_river_dougne_2` | Tree Gnome Village |
| `canoeing_menu_2` | `canoe_startfrom`, `canoe_river` |
| `canoeing_menu` | `canoe_type` — the one that WAS declared, by `interface_farming` |

`canoe_type` working while the rest did not is why the *cutscene* verified fine
in §6 and the station did not: the boat under the player is chosen from the one
carrier something else had already declared.

They are now declared in `canoes/configs/canoes.varp`, at the default
session scope — "made canoes remain at the station until you log out" is the
wiki's own wording, so a station state that survived a logout would be wrong.

**Why the suite did not catch it.** Every §6 assertion read the state back with
`~canoe_state_get`, which reads the server's own copy. Reading a varbit back is
not evidence that it was sent. The canoe section now checks the six carriers by
name; without the declarations it produces exactly five failures.

### A multiloc child's `anim=` never played

`canoeing_canoestation_lumbridge` carries no `anim=` of its own. The four
animated states do: `canoestation_tree_falling` (state 9, the tree going over)
and `canoestation_*_inwater` (states 5–8, the launch splash), all on
`canoeing_station_animations` (seq 3304).

`world_builder_resolve_loc_for_place` took `seq_id` off the BASE unconditionally
— documented, and half right: what makes a transformed loc a DynamicObject is
the base `animationId` *or* the presence of a transform table, but the frame it
draws comes from the def the transform resolved to. Taking the base's meant a
shell with no `anim=` froze every animated child. That is not a canoe bug —
**474 multiloc families in this cache have exactly that shape**
(`blast_furnace_dispenser`, `golem_portal`, the mourning doors, …).

The resolved child's `seq_id` now wins when it has one, with the base as the
fallback so the 445 families that animate from the shell are untouched.

Under `TORIRS_SCENERY_DEBUG` a `loc anim: element=… seq=… frames=…` line says
whether the bind took — which is what separates "the seq never reached the
element" from "it did and nothing is advancing it". Both look identical on
screen.

### What it looks like now

Driven through the real packets under `SDL_VIDEODRIVER=dummy`, one `OPLOC1` on
the station at 3241,3235:

```sh
TORIRSSERVER_SAVES=$scratch/saves SDL_VIDEODRIVER=dummy \
TORIRS_ORBIT_CAM=512,300,110 TORIRS_MAX_FRAMES=600 \
TORIRS_BMP_SERIES=$scratch/chop,330,12,20 \
TORIRS_SIM_OPLOC="330,1,3241,3235,12163" TORIRS_NET_CHEAT="canoe 1" \
./run-live.sh --skip-checks manifests/manifest_osrs239_torirs.ini asdf a
```

The player swings the axe, the tree **falls over through seq 3304** rather than
popping, the fallen trunk lands, and interface 416 opens on the four canoe
models. `::canoestate 1..4` and `11..14` each draw their own hull on the bank
and in the water; `::canoestate 1` plus an `OPLOC1` plays
`canoeing_pushing_into_water` and slides the canoe down the bank; `::canoestate
11` plus an `OPLOC1` plays `canoeing_getting_in` and opens the travel map with
the "you are here" arrow on the right station — which is `canoe_startfrom`
arriving, the second half of the transmit fix.

`TORIRS_ORBIT_CAM=512,…` is the yaw that frames a Lum station from the bank;
the default login camera looks past it.

## 7. Open / deliberately not done

- The five River Dougne assistants have no entry in the generated world spawn
  roster (`tools/gen_spawns.py` rewrites that directory wholesale from an
  external dump that predates the April 2026 update), so they are spawned from
  a hand-authored `canoes/configs/canoes.spawn` instead — the same escape hatch
  `areas/lumbridge/configs/lumbridge.spawn` already uses.
- Axe success rates: the wiki publishes a per-axe/per-level probability curve
  for the *chop*. The station chop is modelled on the same `stat_random`
  low/high interpolation woodcutting uses rather than that curve, and
  consecutive attempts auto-retry as they do in game.
- `canoe_avoid_if` (varbit 1844) suppresses the Wilderness warning. The warning
  itself is a plain `~p_choice2` here rather than the struct-driven wilderness
  panel (structs 1086/1088/1096 carry its text).
