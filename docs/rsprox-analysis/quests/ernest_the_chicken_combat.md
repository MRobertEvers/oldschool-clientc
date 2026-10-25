# Ernest the Chicken: the Draynor Manor fights, against RSProx

Two encounters: the closet **skeleton** (`draynor_skeleton`, 3565) guarding the
rubber tube, and the **undead trees** on the path to the manor's front doors
(`nasty_tree`, 4418; the client shows `nasty_tree_unchoppable` 4416 /
`nasty_tree_choppable` 4417 by `%varb3185_anma_main`, the server fights as 4418).
Audited 2026-10-24 (rsprox-content skill §3, §9, §13, §13b).

## Sessions

Every row of `rsprox_index.py search npc 3565` (14) and `search npc 4418` (25),
26 sessions, transcribed `--all-players --bas`:

| kind | sessions |
|---|---|
| normal world | index 4362, 4363 (rev 241, the owner, LoveTori, combat 27), 3192, 3194, 3135, 3136 (232), 3978, 3979 (237), 1025 (235), 311, 354 (230), 1677 (226) |
| seasonal (Leagues V / Grid Master; used for behaviour only, never damage) | 1846, 213, 231, 1694, 1735, 2, 1702, 1821, 2132, 1827, 2131 (227), 3263, 830, 3258 (234) |

Fights with the skeleton exist only in normal sessions 4363 (the recorder),
4362 (Slimbone0, spectated) and 3192 (QuickDiverse, spectated). The trees swing
in 7 sessions (3192, 4362, 3136, 3263, 830, 3258 + one at m49_52 in 3192).
`search mapsquare 12340` lists 124 sessions; those without either npc id never
came near them.

Ours: `test/quests/ernest_combat_rsprox.lua` on scriptrun (one bot, fixture
`fresh_lumbridge.ini`, private packs), tick log `ticklog.tsv` of run "ectc3".
Legs: walk past the trees; stand beside (3108,3346); a diagonal; back; a
combat-80 player in the closet, then the tube, the kill, the respawn; a
combat-15 player in the closet; 150 ticks of his wander.

Tools: `rsprox_npcinfo.py`, `npc_block_inventory.py`, `rsprox_hit_causes.py`
(both fixed here to read multinpc lines: the trees were invisible to them),
`npc_movement_audit.py`, `ticklog_tells.py`, and the per-tick scripts kept
in the session scratchpad (hunt/near/attack extraction).

## The skeleton (draynor_skeleton)

### Npc info, per life (60 lives in normal sessions, 7 seasonal)

| block | recorded | ours (before) | ours (now) | verdict |
|---|---|---|---|---|
| attack seq | `skeleton_update_attack_weapon` (5485) 22/22 (3192 [12338..12378] x11, 4362 [534..570] x10, 4363 [313]) | `skeleton_update_giant_attack` | 5485 | **fixed** |
| defend seq | `skeleton_update_defend` (5489) 22/22, on the dealer's swing tick, the splat one tick later (3192 [12339]/[12340]) | 5489 | 5489, same two-tick shape (ours [75]/[76]) | matched |
| death | 5491 at D+1, `[del]` at D+3, 2/2 (3192 [12380] -> [12381] -> [12383]; 4362 [572] -> [573] -> [575]) | 5491 | D+1 / D+3 (ours [96] -> [97] -> [99]) | matched |
| respawn | `[add]` 59 ticks after the `[del]`, 2/2 (3192 [12383] -> [12442]; 4362 [575] -> [634]); tile (3110,3367) facing south (3192), first step west in 4362 | `respawnrate=25` | 59 (ours [99] -> [158]) at (3110,3367) | **fixed** |
| enabled_ops | `0b00000` on the death-seq tick (D+1) 2/2; the respawn `[add]` carries `[sequence] id=-1` + `enabled_ops 0b11111` 2/2 | `0b00000` on D+0, nothing on the add | same | **open, engine** (generic death/respawn) |
| facing | `[facing] player=` on the tick he engages (4363 [313], 4362 [532]), held through the fight, `type=reset` at disengage (4363 [334]) / death | -- | engage O+1 after the tube, held every swing ([71]..[93]) | matched |
| headbar | `healthbar_standard_30` (id 0); fills fit 29 hitpoints with one regen (3192 [12336..12380]) | 29 hp | 29 | matched |
| movement | walk, 1 tile/tick, tiles 3108..3112 x 3367..3368 + (3109,3366); gaps 1:292 2:18 3:11 4:10 5:15 6:13 7:7 8:4 9+:67, diagonal 13%, undo 22% (3 sessions, `npc_movement_audit.py paths`) | wander 5 | 28 steps / 163 ticks, gaps 1:20 4:1 5:1 9+:5, diagonal 7%, undo 27%, same tiles | matched (shape) |
| block kinds | `enabled_ops facing headbar hit sequence` (`npc_block_inventory.py`; `face_pathingentity` is the pre-238 name of `facing`) | | every kind sent | matched |

### Aggression

| case | recorded | ours (before) | ours (now) | verdict |
|---|---|---|---|---|
| combat 27 at one tile, no tube | faced, stepped and swung on the first tick she stood one tile off (4363 [313]); not at two tiles ([312]) | `huntmode=none` | `huntmode=aggressive`, `huntrange=1`: combat 15 hunted and swung the same tick ([171]) | **fixed** |
| combat 46 beside him, no tube | left alone 4 ticks at one tile (3192 [12331..12334]) until it punched him | never hunted | the engine's twice-his-level rule (> 44): combat 80 beside him 8+ ticks, untouched ([54..69]) | matched |
| anyone taking the tube | combat 46 takes it (4362 [531] `obj_del`), faced [532], swung [534], then every 4 with the face held; wiki: "it attacks players that take the rubber tube" | nothing | `[opobj3,rubber_tube]` (haunted_skeleton.rs2): take [70], faced [71], swung [73], every 4 ([77] .. [93]) | **fixed** (1 recording) |
| retaliation | swing 3 ticks after the player's first hit (3192 [12335] -> [12338]) | | 3 (run 1: [74] -> [77]) | matched |
| leash | still facing the recorder 12 tiles west of his spawn, reset the tick after she passed 14 (4363 [331..334]) | `maxrange=7` (drops past 8) | same | **open** (1 sample; not changed) |

### Sounds

| event | recorded | ours (before) | ours (now) | verdict |
|---|---|---|---|---|
| attack | `stabsword_slash` (2548) synth to the target on the swing tick, 1/1 (4363 [313]) | null (silent) | 2548 synth on every swing | **fixed** |
| defend (hit on him) | never recorded: no recorder hit him in any session | null | `armed_skeleton_hit` (774) synth to the dealer on the splat tick | **fixed, UNVERIFIED**: the npcs with his stance set (`skeleton_update_ready` + `walk_shield`, skeleton_armed..armed4) use 774 on 12/12 recorded hits (index 457, 480, 3048) |
| death | never recorded for a killer; two bystanders 9-10 tiles off heard nothing (3192 [12381], 4362 [573]) | null | `skeleton_death` (777) | **fixed, UNVERIFIED**: 46/46 recorded deaths across the 8 `skeleton_update` types killed in index 457, 480, 3048 carry 777 as a synth |
| death sound to bystanders | none, 2/2 (above) | -- | the engine synths it to every player within 12 tiles (`npc_sound_nearby`) | **open, engine** |
| player's hit / block sound | `human_hit_*` / `human_block_1` delay 20 | | same | engine, matched |

### Damage (§13)

Cache record: stat1 15 attack, stat2 17 defence, stat3 18 strength, stat4 29
hitpoints, strengthbonus 14, attackrate 4, defences 9/11/-2/1, `param_26=1`
(param_26 equals `damagetype` on every npc that states both: al_kharid_warrior,
barbarian, man, mossgiant, chaos_druid, highwayman). Wiki: max hit 3, Slash,
speed 4, str 18, def 17.

| check | recorded | ours (before) | ours (now) | verdict |
|---|---|---|---|---|
| levels | -- | `strength=17`, `defence=18` (swapped) | 18 / 17 | **fixed** |
| style | Slash (cache param_26=1, wiki) | crush (`damagetype=2`) | slash (1) | **fixed** |
| cadence | 4 ticks, 19/19 gaps (3192, 4362) | 4 | 4 | matched |
| max | 3 (QuickDiverse 3192 [12366]); formula floor(0.5 + 26 x 78 / 640) = 3 | 3 | 3 (15 swings) | matched |
| values | 22 splats: 18 zeros, 1, 3, 2 (QuickDiverse), 2 (LoveTori, 4363 [313]); Slimbone0 0/10 | | 15 splats: 9 zeros, max 3 | consistent (unknown gear on the spectated players: no accuracy fit possible) |
| attack bonus | wiki infobox `attbns = 15`; the cache states no stab/slash/crush attack param | 0 | 0 | **open**: unverifiable from 4 nonzero splats |

### Residual splats (§13b)

`rsprox_hit_causes.py --npc 'draynor_skeleton|nasty_tree'` over all 26
sessions: `npc:draynor_skeleton:skeleton_update_attack_weapon@0:adj` 22 (0..3),
`npc:nasty_tree:deadtree_attack@0:adj` 18 (0..3), `none` 4:
index 4362 [141] and 4363 [252] "Something in the water bites you." (the
fountain, 1 + `[say] Ow!`: quest_haunted.rs2 sends the same), 3263 [2263]
(Morytania, another area), 830 [394] "The locator orb ... hurts you" (the
recorder's kit). No heals. The tool's `--ours` reader births npcs from
`npc_spawn` rows only, so it reads world-spawned npcs as absent; ours above is
counted from the tick log's `hit_player` + `npc_anim` rows directly.

## The undead trees (nasty_tree)

### Npc info and behaviour (281 lives: 195 normal, 86 seasonal)

| block / rule | recorded | ours (before) | ours (now) | verdict |
|---|---|---|---|---|
| attack seq | `deadtree_attack` (73) 19/19 | 73 (generated) | 73 | matched |
| who | any combat level: 27, 42, 46, 65, 94, 117, 126 (wiki: "regardless of a player's combat level"; 19 swings, 17 first swings) | never attacked (`huntmode=none`) | `huntmode=aggressive`, `huntrange=1` | **fixed** |
| which tiles | the four tiles beside the trunk: 17/17 first swings from a cardinal start tile (3109,3346 x12, 3108,3344 x3, 3112,3348, 3143,3366); 0 of 20 start ticks on a diagonal (e.g. 4362 [113],[114]; 3136 [5250],[5251]) | -- | `maxrange=0`: the leash's corner rule keeps the diagonals out (ours: diagonal [31..36] no swing) | **fixed** |
| when | the tick the player's start tile is beside it, while running past (4362 [120], [188]) | -- | ours [14] (ran past), [37] | matched |
| repeat | every 4 ticks while the target stays (3192 [7659] -> [7663]; 3136 [5410] -> [5414]) | -- | [20] [24] [28], [37] [41] [45] | matched |
| let go | `[facing] type=reset` / `entity=null` the tick after the target leaves the four tiles (4362 [189]; 3136 [5417] [5426] [5434] [5823] [5830] [5845]; 3192 [7667]; 8/8 checked) | -- | ours [15], [31], [49] | matched |
| single-way | a second tree waits 8 ticks after the player was last attacked (3136 [5425] -> [5433]); so does the SAME tree once it let go (3136 [5821] -> back on its tile [5825] -> swung [5829]) | -- | the engine's 8-tick claim refuses other npcs only, not the npc that let go | **open, engine** (1 recorded case) |
| movement | never a step (0 in 281 lives) | `wanderrange=0` (generated) | `moverestrict=nomove`, `nomove=1`, `wanderrange=0` | matched |
| standing on it | no player stands on a tree tile in any of the 26 sessions | `blockwalk=npc` | `blockwalk=all` (LostCity's rev-225 record) | fixed, unverified |
| spawn angle | per tile, rev 241 (4362, 4363): east at 3107,3337 / 3107,3342 / 3107,3344 / 3108,3346 / 3111,3348 / 3115,3344; south at 3103,3347 / 3120,3344 / 3122,3335; west at 3111,3339 / 3140,3360; north 3138,3352 | south (type `facing=south`) | south | **open**: `.spawn` rows carry no facing, and m48_52.spawn / m49_52.spawn are not this audit's |
| block kinds | `facing sequence` (+ `face_pathingentity`, the old name) | | `facing sequence` | matched |
| defend / death | none: it has no Attack op and cannot be attacked (wiki) | human_* (unreachable) | unchanged | n/a |

### Sounds and damage

| event | recorded | ours (before) | ours (now) | verdict |
|---|---|---|---|---|
| attack sound | `nasty_tree_attack` (651) synth to the target on the swing, 9/9 recorder swings | 651 (generated) | 651 | matched |
| hit/block sound | `human_block_1` (511) / `human_hit_2` (519) / `leather_hit_3` (2813, the recorder's armour) delay 20 | | engine, same shape | matched |
| style / levels | crush (cache param_26=2, LostCity); 20/40/20/250, no bonuses | same | same | matched |
| max | 3 (4362 [188]); formula floor(0.5 + 28 x 64 / 640) = 3; wiki "0 to 3" | 3 | 3 (ours [28]) | matched |
| values | 18 splats: 17 zeros, one 3 | | 7 splats: 5 zeros, 1, 3 | consistent (defence-30 bot vs mostly high-level recorders) |

## Fixed (files)

- `OSRS-Content/osrs-content/server/scripts/quests/quest_haunted/configs/quest_haunted.npc`
  `[draynor_skeleton]`: strength/defence unswapped, `damagetype=1`, the three
  anims, the three sounds, `huntmode=aggressive`, `huntrange=1`, `respawnrate=59`.
- `.../quests/quest_haunted/scripts/haunted_skeleton.rs2` (new): the tube take
  queues `npc_attackplayer` on the skeleton.
- `.../areas/draynor/configs/manor_trees.npc` (new): `[nasty_tree]` hunt,
  leash, nomove, anim, sound, blockwalk.
- Generated lines moved out (deleted where the content block now states them):
  `_explicit/configs/server_fields.generated.npc` `[draynor_skeleton]` (3 null
  sounds, damagetype, huntrange, huntmode, respawnrate) and `[nasty_tree]`
  (blockwalk, maxrange, huntrange, moverestrict, nomove, huntmode);
  `npc/configs/npc_anims.generated.npc` `[draynor_skeleton]` and `[nasty_tree]`;
  `npc/configs/npc_movement.generated.npc` `[nasty_tree]`.
- `tools/rsprox/rsprox_npcinfo.py`, `tools/rsprox/rsprox_hit_causes.py`: read
  `multinpc=` lines.

## Open

1. **Engine: an npc that steps into reach swings a tick late.** Recorded 2/2
   step-and-swing on the same tick (4363 [313], 4362 [534]); ours steps, then
   swings next tick (`ToriRSServer_CombatNpcTick` returns after the chase step;
   LostCity's `aiMode` tries the interaction again after moving).
2. **Engine: death ops and the respawn add.** Ours turns ops off on the killing
   blow (D+0, recorded D+1 with the death seq) and re-adds with no
   `[sequence] id=-1` / `enabled_ops 0b11111` (recorded 2/2).
3. **Engine: death sound to bystanders.** `npc_sound_nearby` synths it to every
   player within 12 tiles; two bystanders 9-10 tiles off heard nothing (2/2).
4. **Engine: single-way re-hunt.** The npc that let a player go may take him
   again inside 8 ticks (recorded: no, 3136 [5821]..[5829]).
5. **Engine: an npc on its target's tile** never steps off in the
   `opplayer` mode path (LostCity `pathToTarget` random-walks); seen in run 1
   when the bot took the tube from under him.
6. **Skeleton leash** (`maxrange`): one sample says further than 7 (4363 [334]).
7. **Skeleton attack bonus**: wiki 15, cache none.
8. **Tree spawn angles** per tile (table above): need a facing column in
   `.spawn` rows or per-tile blocks.
9. **Tolerance** (wiki: the trees "follow standard rules for tolerance";
   10 minutes in the area): not modelled, not recorded here.
10. **haunted.lua** (not edited): the quest now passes trees that swing (0-3)
    and meets a skeleton that hunts a combat-3 bot at one tile and attacks on
    the tube; the fresh fixture has 10 hitpoints.
