# Quetzal Transport System (Varlamore bird travel) — RSProx port

Content: `OSRS-Content/osrs-content/server/scripts/skill_hunter/scripts/quetzal_transport.rs2`,
`quetzal_whistle.rs2`, `skill_hunter/configs/quetzal_transport.npc`, `quetzal_transport.spawn`.
Twilight's Promise keeps Renu's first feed and `[opnpc1,quetzal_fortis]` / the green-blue-cyan
leaves' Travel (`quests/quest_twilightspromise/scripts/twilightspromise_teomat.rs2`); both call
`~quetzal_open`.
Test: `test/varlamore/quetzal_transport.lua` (scriptrun, PASS 2026-11-12).

Timing model: C = the click line; L = the player's last step; O = the op's tick; W = the tick the
whistle is blown. The quetzal menu is interface 874 `quetzal_menu`; the whistle's is 949
`quetzalwhistle_menu`. Destination id = the icon's sub + 1 (`quetzal` dbtable `id`).

## Sources

| index | rev | recorder | what it has |
|---|---|---|---|
| 4530, 4515, 4544, 4545, 4547, 4508 | 242.1 | LoveTori (owner) | Fortis -> Tal Teklan by Renu (6 flights) |
| 4479, 4480, 4536 | 242.1 | LoveTori | Fortis -> Aldarin (3 flights) |
| 4444 | 241.1 | LoveTori | Twilight's Promise feed + first map [2033..2050], then Sunset Coast -> Hunter Guild -> Fortis -> Teomat [2053..2133] |
| 1095, 1099, 1101..1105 | 237.1 | Z-Kris (Leagues) | 98 Renu flights between every automatic roost; Pet [9655], [10352]; Last-destination [30240]; blueprint Check/Craft (1105 [21120], [21217], [21240]) |
| 3111, 3217 | 232/233 | Blurite Rock | 5 flights; the LOCKED destination (3111 [3961]) |
| 1131, 1139, 3980, 4028, 4031 | 237.1 | Z-Kris (Leagues) | the whistle: Signal -> 949, Check, Rumour, Last-destination |
| all `home-default` / `home-bas` transcripts | | | Renu's `[add]` tile and spawn angle per roost (387 adds), 0 Renu movement steps |

Every flight pooled: `uses.txt` extraction over the 19 sessions above = 121 Renu flights.

## Action inventory (every quetzal row, with a verdict)

| action (recorded click) | count / cites | verdict |
|---|---|---|
| `opnpc1` Travel on a roost shell (`quetzal_fortis`, `_teomat`, `_sunsetcoast`, `_hunterguild`, `_aldarin`, `_quetzacalligorge`, `_talteklan`, `_auburnvalley`) | 121 | **fixed**: only `quetzal_fortis` had a binding (the engine runs the SHELL's trigger, never the leaf's); all 13 other shells bound in `quetzal_transport.rs2` |
| map icon click `if_buttonx quetzal_menu:icons sub=N` | 121 + 1 locked | **matched/fixed** (timeline below); locked line fixed |
| `opnpc3` Last-destination (`quetzal_fortis`) | 1101 [30240] | **fixed**: bound on all 14 shells (was leaves only = dead) |
| `opnpc4` Pet (`quetzal_fortis`, `quetzal_hunterguild`) | 1095 [9655], 1101 [10352] | **fixed**: shells bound; the walk to the front tile, the face, the pat on O+3, no `seq -1` |
| `opnpcu` feed on Renu | 4444 [2019] | Twilight's Promise's (not changed here) |
| `opheld1` Signal (whistle, `op=2`) | 1131 [1417], 1139 [6409], [6970], 3980 [610], [819], [2053], 4031 [1708], [3788], [4001], [4697] | **fixed**: opens 949 `quetzalwhistle_menu` (ours opened 874) |
| `quetzalwhistle_menu:icons` click | 1131 [1422], 1139 [6411], 3980 [2057], 4031 [1713], [3792], [4004] | **fixed** (whistle timeline below; ours used the 50-cycle fade at once with a 2-tick whistle) |
| `opheld2` Check (whistle, `op=3`) | 3980 [242] "Your quetzal whistle has 15 charges remaining." | **matched** (text unchanged) |
| `opheld3` Rumour (whistle, `op=4`) | 3980 [1782] | **OPEN**: recorded "Your current rumour target is a embertailed jerboa. You'll need to bring back its tail. Gilman was the source of the rumour."; ours "Your current rumour target is embertailed jerboas" (needs a singular name, the body part and the giver per task: hunter rumours' data, not this file) |
| `opheld4` Last-destination (whistle, `op=6`) | 3980 [337], 4028 [352], [631] | **fixed**: whistle shape below |
| blueprint `opheld1` Check (`hg_whistle_blueprint_2`) | 1105 [21120] | **fixed**: `~objbox(yew_logs, "This whistle requires a knife and one set of yew logs.")` |
| blueprint `opheld2` Craft, missing parts | 1105 [21217], [21240] | **fixed**: `~objbox(yew_logs, "You need yew logs and a basic quetzal whistle to craft this.")` |
| blueprint 1 / 3 Check and Craft | none | UNVERIFIED (same shape, willow / redwood) |
| successful craft, recharge at Pitri, unlimited whistle, the default-tp setting | none | UNVERIFIED (wiki) |
| `oploc1` Build on `quetzal_landing_site_unbuilt` | none | UNVERIFIED (wiki recipe, anim, lines) |

Generic content in these windows: the fades are the shared `~fade_move` (also Twilight's
Promise's stairs); music changes on landing are the region's (engine).

## Renu's flight (npc Travel, then the map)

Recorded, 7/7 opens and 121/121 flights (4530 [281..294] as the worked example):

| tick | recording | ours (scriptrun tick log, run qz0) | verdict |
|---|---|---|---|
| L+0 (the arrival beside her; on C when already there, 4444 [2053]) | `toplevel_mainmodal_open (2524) (-1,-1)`, `if_opensub mainmodal quetzal_menu`, `synth quetzal_map_open_02 (8413)`, `if_setevents quetzal_menu:icons 0..13 OP1` | opnpc1 + synth 8413 on the arrival tick (t25, player at 1696,3140) | **matched** (2524 added; synth moved after the open) |
| L+1 | `busy` 1, player `[facing] angle=1176` | engine | engine-generic |
| C (the icon) | `quetzal_last_destination` = id, `hpbar_hud_show_extra_bar` -1->0 (first fade of a session only, 4444 [241], 4479 [11169], 4536 [17381], 4544 [6549]), `if_closesub` 874, `synth varl_quetzal_wing_flap02 (8501) loops 3 delay 55`, fade out (`fade_overlay` 948 `0,255,0,0,50`, hp hud hidden, `cc_deleteall`) | t51: synth 8501 3/55, varp4183 (last destination) | **fixed** (hpbar varp added) |
| C+1 | `minimap_state` 2, `minimap_toggle 2` | `~fade_move` | matched |
| C+4 | the teleport onto the dbrow tile (121/121 at C+4) | t55 player at 1226,3091 | **matched** |
| C+5 | `minimap_state` 0, `busy` 0, fade in `0,0,0,255,50`, `minimap_toggle 0`, region music | `~fade_move` | matched |
| C+7 | `if_closesub overlay_atmosphere` | `~fade_move` | matched |

Last-destination (`opnpc3`, 1101 [30243]): the same from C with no map, on L+0. Ours: `~quetzal_fly`.

Locked destination (3111 [3961..3962]): `if_closesub` 874 + messagebox
"You haven't built a landing site at that destination yet." on the click. Ours had a game
message with another text: **fixed** (`~quetzal_locked`).

## The whistle

Signal (`opheld1`), 6 recorded opens: on C `busy` 1, `toplevel_mainmodal_open (-1,-1)`,
`if_opensub mainmodal quetzalwhistle_menu (949)`, synth 8413, `if_setevents
quetzalwhistle_menu:icons 0..13 OP1`. Ours opened Renu's 874: **fixed**.

The flight, from W (the icon click, ±1 proxy jitter: 1131 [1422]->[1423], 1139 [6411], 3980
[2057]->[2058], 4031 [1713]; Last-destination: on its click, 3980 [337], 4028 [352], [631]):

| tick | recording (1131, 3980, 4031 agree) | ours (run qz0) | verdict |
|---|---|---|---|
| W | `if_closesub` 949, player seq `human_quetzal_whistle` (10944), spotanim slot 1 `spotanim_whistle_quetzal_backpack01` (2735) | t64: anim 10944, spotanim 2735 slot 1 | **fixed** (was anim, 2 ticks, anim null, then Renu's fade) |
| W+7 | `minimap_state` 2, fade out `0,255,0,0,15` (15 cycles), `minimap_toggle 2` | t71 | **fixed** |
| W+8 | `minimap_state` 0, teleport + seq -1, `cam_reset`, `minimap_toggle 0` | t72: seq -1, 1389,2901 | **fixed** |
| W+9 | "Your quetzal whistle has N charges remaining." (the count after the flight: 3980 15 -> 14), `quetzal_last_destination` (4031 [1722]), `busy` 0, seq -1, fade in `0,0,0,255,50`, `minimap_toggle 0` | t73: seq -1, last destination | **fixed** |
| W+11 | `if_closesub overlay_atmosphere` | p_delay(1) | **fixed** |

The whistle sessions are Leagues; their W+8 also re-sends sides, combat tab and appearance (a
Leagues area change), left out. `cam_reset` is kept: 3/3 whistle flights, 0/121 Renu flights.

## Pet

1095 [9655..9666]: op at the arrival beside her (O=[9658], 1696,3140); `busy` 1 and a walk step
to 1697,3140 (the roost's landing tile, in front of the pad's middle) at O+1; at O+3 the player
faces her (`face_angle 1024`) with `anim_specific human_quetzal_headpat` (11122) and
`npc_anim_specific npc_quetzal_headpat` (11123); `busy` 0 at O+8; no `seq -1`. 1101 [10352],
standing on 1585,3053: the pat on C. Ours: `p_walk` + `p_delay(2)` when off the tile, then
`facesquare` her middle, both seqs, `p_delay(4)`. Differences: Jagex sends the seqs as
`*_specific` (only the petter sees them); ours broadcasts (no specific-anim command in the
engine). The old `anim(null)` after 3 ticks is gone.

## Landings (pooled, every flight)

| id | destination | dbrow tile | recorded landings | sessions | verdict |
|---|---|---|---|---|---|
| 1 | Civitas illa Fortis | 1697,3140 | 2/2 exact | 1095, 4444 | matched |
| 2 | The Teomat | 1437,3171 | 3/3 + whistle 1131 [1431] | 1101, 4444 | matched |
| 3 | Sunset Coast | 1548,2995 | 10/10 | 1095, 1099, 1103, 1105, 4444 | matched |
| 4 | Hunter Guild | 1585,3053 | 29/29 | 8 sessions | matched |
| 5 | Cam Torum Entrance | 1446,3108 | whistle 3980 [345] | 3980 | matched |
| 9 | Aldarin | 1389,2901 | 29/29 | 1095 .. 4536 | matched |
| 10 | Quetzacalli Gorge | 1510,3222 | 12/12 | 1095, 1102, 3111 | matched |
| 12 | Tal Teklan | 1226,3091 | 19/19 + whistle 4031 [1721] | 12 sessions | matched |
| 13 | Auburnvale | 1411,3361 | 16/16 | 1095, 1099, 1101, 1102, 1105 | matched |
| 6, 7, 8, 11, 14 | player-built | dbrow | none | | UNVERIFIED (dbrow tile) |

No scatter: 121/121 Renu landings and 3/3 whistle landings are the row's tile exactly.

## Renu at every roost (npc info)

| shell | tile (SW) | spawn angle | source |
|---|---|---|---|
| quetzal_fortis | 1696,3141 | south | 116+21+3 adds (twilightspromise.spawn) |
| quetzal_teomat | 1436,3168 | north | 22 adds |
| quetzal_sunsetcoast | 1547,2996 | south | 17 adds |
| quetzal_hunterguild | 1584,3054 | south | 68 adds |
| quetzal_camtorum | 1447,3107 | west | 1 add |
| quetzal_aldarin | 1388,2898 | north | 53 adds |
| quetzal_quetzacalligorge | 1511,3221 | west | 26 adds |
| quetzal_talteklan | 1225,3088 | north | 34 adds |
| quetzal_auburnvalley | 1410,3362 | south | 27 adds |
| quetzal_colossalwyrm | 1671,2932 | west | pad loc (rot 1) in the map, UNVERIFIED |
| quetzal_outerfortis | 1701,3036 | west | pad loc (rot 1), UNVERIFIED |
| quetzal_colosseum | 1776,3110 | east | pad loc (rot 3), UNVERIFIED |
| quetzal_salvageroverlook | 1612,3301 | south | pad loc (rot 0), UNVERIFIED |
| quetzal_kastori | 1343,3019 | north | pad loc (rot 2), UNVERIFIED |

Her tile is the pad's SW tile and the landing is the pad's front middle (north pad: +1,+3; south:
+1,-1; west: -1,+1; east: +3,+1): all 9 recorded roosts and all 14 dbrow tiles fit.

- **Before:** only Fortis was spawned; the other 13 roosts had no Renu, so a flight could
  never come back. **Fixed**: `quetzal_transport.spawn` (13 rows).
- **Movement:** 0 walk/run/crawl steps for any shell in any transcript; the generated block
  had `defaultmode=wander, wanderrange=5, facing=south` for all 14. **Fixed**: those 42 lines
  deleted from `_explicit/configs/server_fields.generated.npc`, `quetzal_transport.npc`
  states `defaultmode=none, wanderrange=0, facing=<pad>` (run qz0: Tal Teklan's Renu adds
  with spawnangle 1024, north).
- Her only seqs: `npc_quetzal_headpat` on Pet; the feed's tame seq is the quest's.
- Forms: the shells' multinpc on each roost varbit (0 hidden, 1 green, ... ; Fortis 0 noop,
  1 feed, 2 green). The login burst at rev 242 (4530 [login]) sets base 1, Fortis 2 and the
  seven automatic roosts 1: `~quetzal_prepare`.

## Open

- **Rumour on the whistle**: recorded line not reproduced (see the inventory).
- **`*_specific` seqs** for Pet: no engine command; ours is seen by everyone.
- **Charges**: the message at 1 charge ("charge"?) and an empty whistle's refusal are
  unrecorded; the infinite whistle's line on a flight is unrecorded (ours sends none).
- **Whistle refusals**: wilderness above 20, a whistle lost with the map open: wiki/invented
  text, UNVERIFIED.
- **Building a landing site** (`oploc1 quetzal_landing_site_unbuilt`), the recipe and its
  lines: wiki, UNVERIFIED. The five player-built roosts' Renu tiles are from the map's pad
  locs, not a recording.
- **Before Twilight's Promise**: the refusal line of Travel is invented, UNVERIFIED.
- **Last-destination with no previous flight**: line invented, UNVERIFIED.
- **Whistle crafting success lines, Pitri's recharge and unlimited whistle**: unrecorded.
- **Renu colours** (orange seen at Hunter Guild/Cam Torum/Fortis in older sessions): the
  base-appearance choice is not ported.
- The wire audit of the interface packets (2524, if_setevents) was read from the code, not from
  a `TORIRSSERVER_TRACE_OUT=2` run.
