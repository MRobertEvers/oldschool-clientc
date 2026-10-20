# Canoes from RSProx 4286

Source: local index session **4286** ("rsprox local (mrobe)", recorded 2026-10-19,
rev 241.1, world 305, normal world). One player, Woodcutting 90, rune axe,
felled, shaped, floated and paddled from all ten stations:

| # | from | canoe | to | scene |
|---|---|---|---|---|
| 1 | Lumbridge | log | Champions' Guild | river |
| 2 | Champions' Guild | dugout | Barbarian Village | river |
| 3 | Barbarian Village | stable dugout | Edgeville | river |
| 4 | Edgeville | waka | Ferox Enclave | **cave** |
| 5 | Ferox Enclave | waka | Lumbridge | **cave** |
| 6 | Castle Wars | waka | Tree Gnome Village | river |
| 7 | Tree Gnome Village | waka | Ardougne Zoo | river |
| 8 | Ardougne Zoo | waka | Chaos Druid Tower | river |
| 9 | Chaos Druid Tower | waka | Tree Gnome Stronghold | river |
| 10 | Tree Gnome Stronghold | waka | Castle Wars | river |

Plus both branches of Lara's Talk-to ([459]-[493]). The Wilderness Pond, Store-axe
and the other nine assistants are not in it; their behaviour comes from the wiki.

Transcribe: `TranscribeCommandKt -name archive/4286` against the rev-241 classpath
`~/rsprox-index-data/rsprox-upstream.classpath`, with a private `-Duser.home`.
Tick numbers below are the transcript's `[n]`.

## Station chain (every station, same shape)

| step | ticks | packets |
|---|---|---|
| chop trigger T | T | `canoe_startfrom` (+ `canoe_river`), face the loc |
| server walk | T+1 .. T+s | to the tree end, one **walk** step a tick though run is on |
| face | F = T+s+1, F+1 | `facesquare` the station tile next to the player, twice |
| swing | F+2 | `human_woodcutting_<axe>` |
| fall | F+3 | seq -1, state 9, `loc_anim 3304` on the station, synth `tree_fall` (2734) |
| fallen | F+4 | state 10. **No message, no panel.** |
| shape trigger | T' | a second click on the fallen tree |
| server walk | T'+1 .. T'+s | to the middle of the station |
| panel | T'+s | clientscript 2524 `(3612928, 0)`, `if_opensub 416`, events 0..0 on log/dugout/stable_dugout/waka/close |
| face | T'+s+1 | the station |
| carve click C | C | `canoe_type`, `canoe_avoid_if`=1, close |
| carve | C+1 .. C+4 | `human_canoeing_carve_<axe>` re-sent every tick |
| built | C+5 | seq -1, state 1..4, `canoe_avoid_if`=0, xp. No message. |
| float | click tick | state 5..8, facing, `canoeing_pushing_into_water`, `loc_anim 3304`, synth `canoe_roll` (2731) |
| afloat | +2 | state 11..14 |
| board | click tick | clientscript 2524 `(-1, -1)`, `if_opensub 953/952`. No getting-in seq. |

Stand tiles (chop = tree end, shape = middle, bank side) are in
`canoe_station.dbrow`; they follow the loc's rotation (rot 3: x0+2,z0 / x0+2,z0+2
facing west; rot 1: x0-1,z0+4 / x0-1,z0+2 facing east; rot 2: x0+4,z0+2 /
x0+2,z0+2 facing south; rot 0: x0,z0-1 / x0+2,z0-1 facing north).

Every recorded tree fell on the first swing; the wiki puts the success roll on
the shaping ("Success rates", per-axe low/high), which at 90 with a rune axe is
certain, consistent with the fixed four carving ticks.

## The ride (from the map button C)

| tick | river (8 trips) | cave (Ferox in or out) |
|---|---|---|
| C | map closed, `chatdefault_restoreinput`, fade out (`948(0,255,0,0,50)`) | same |
| C+2 | lock varbits/varps, hide ops, seat 1817,4515, `cam_shake` x5 zero, side panels closed, `highlighting_off`, `minimap_toggle 2` | seat 1845,4492 |
| C+3 | face west, `canoeing_rowing`, scenery, `canoe_paddle_loop` x16, camera (moveto 1812,4515 h2500 then h255, lookat seat h255) | loop x10; moveto 1838,4492 h275, lookat h100 |
| C+4 | fade in `948(0,0,0,255,50)` | same |
| C+10 / C+13 | scenery_1 1821,4500 / leaf 1819,4505 + bullrush 1820,4501 | cave props C+4/5/6/8 |
| C+25 / C+15 | fade out | |
| C+28 / C+18 | on the bank, seq -1, `cam_reset` | |
| A = +1 | unlock, `mes` type spam "You arrive at X.<br>Your canoe sinks into the water after the hard journey.", `canoe_type` 0, **the origin station's state 0**, hull `loc_add`, fade in, side panels, `highlighting_on`, synth `canoe_sink` (2732), `minimap_toggle 0` | |
| A+2 | fade overlay closed | |
| A+3 | hull `loc_del` (its own `anim=canoeing_sinking`) | |

Scenery: river C+3 bullrush 1820,4516, leaf 1819,4516, scenery_1 1822,4516 (gone
C+10), scenery_2 1821,4503; cave C+3 1842,4489, C+4 1846,4489, C+5 1842,4483,
C+6 1846,4485, C+8 cave_3 1846,4481. All added `spawnangle=south`, one tile north
a tick from the tick after they appear.

Arrival tiles and hulls:

| to | player | hull | rot |
|---|---|---|---|
| Lumbridge | 3240,3242 | 3239,3240 | 1 |
| Champions' Guild | 3199,3344 | 3197,3342 | 1 |
| Barbarian Village | 3109,3415 | 3109,3411 | 1 |
| Edgeville | 3128,3503 | 3127,3504 | 1 |
| Ferox Enclave | 3154,3638 | 3157,3636 | 1 |
| Tree Gnome Village | 2485,3188 | 2483,3186 | 1 |
| Ardougne Zoo | 2577,3261 | 2576,3258 | 2 |
| Chaos Druid Tower | 2571,3360 | 2574,3361 | 2 |
| Tree Gnome Stronghold | 2523,3408 | 2522,3411 | 2 |
| Castle Wars | 2436,3134 | 2439,3131 | 2 |

## Not done / not matched

- `busy` (varbit 12393) toggles through every delay; engine-wide, not sent.
- `cam_unlock(false)` on arrival: no such command in the engine.
- The orbs, friends, account and music panels are re-opened by Jagex on both
  edges of the lock; we leave them open instead.
- The lock's stone `if_setevents(-1,-1,[])` rows for closed side panels.

## Verifying

`test/canoes/canoe_trips.lua` (scriptrun) rides Lumbridge -> Champions' Guild
(river), Champions' Guild -> Ferox and Ferox -> Lumbridge (cave), talks to Lara,
stores the axe with her, makes the Castle Wars canoe from storage and gets it
back from Kaiquir. Its tick log reproduces every station and ride offset above.
`TORIRSSERVER_SELFTEST_CANOES_ONLY=1 ... --selftest` checks the arrival tick
and the hull's three ticks.
