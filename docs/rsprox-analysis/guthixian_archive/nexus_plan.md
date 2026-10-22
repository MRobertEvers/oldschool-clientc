# The nexus between the fights: doors, the atrium, the navigator

The raid's spine: the Guardian's room (plane 0) opens two doors after the
kill; each later boss is left by following an Alliance Navigator back to the
atrium (the nexus again, plane 2) where the next pair of doors stands open.
Sessions in `RAID_PLAN.md`. Every raider in these recordings is someone else
(the recorder spectates), so what a raider's own client was sent on an op is
not recorded: the spectator's own transitions (same fades, same vars) are the
template, and the raiders' ticks come from when the others see them leave and
arrive.

## 1. After the Guardian (kill line K)

| tick | recorded | evidence |
|---|---|---|
| K | the dead form, `tfa_nexus_section1_door_west_slightly` (4113,5584 r3) and `..._east_slightly` (4141,5584 r1), exit 0b1 | built (`guardian_plan.md` §11) |
| K+3 | the two doors `..._slightly_opened` | built |
| K+15 | `armacus_selles_postguardian` added at (4127,5577) spawnangle north, `[facing] angle=1024 instant`, transparency 255 -> 0 over 30 cycles; runs north 2 tiles a tick | 4300 [862], 4301 [42], 4303 [326] (+15 3/3) |
| K+16 | four Guardians of Armadyl (`alliance_guardian_of_armadyl_1..6`, any four) at (4126,5574), (4126,5573), (4128,5574), (4128,5573), north, the same fade-in; run north 2 a tick | 3/3 |
| K+20 | Armacus stops at (4127,5586) (his last step a walk) and says "Pick a side and get to work!" | 4301 [47] |
| K+24 | a second four (4301 [51]) on the same tiles | 4301 |

The guardians run north 4 ticks and are gone with the room; their later path
is not recorded (the party had left). Ours: Armacus and the first four, the
second four not (cosmetic, one session).

## 2. The doors (Enter / Quick-enter)

`tfa_nexus_section{1,2,3}_door_{west,east}*`: op1 Enter, op2 Quick-enter
(cache). A raider walks to the door and faces it (`[facing] coord=(4113, 5584,
0), sizez=5`, 4301 [30]); the raiders vanished from the room 4..5 ticks after
the last of them arrived (4301 [43..52]). What Enter asks a party (a vote, a
wait) is not recorded: ours, solo, both ops take the player straight in.

| door | leads to |
|---|---|
| section 1 west (4113,5584) | Ezekiel (4300, 4301) |
| section 1 east (4141,5584) | Bork (4302, 4303, 4294) |
| section 2 west (4113,5597) | Moia (4314, 4315: 9/9) |
| section 2 east (4141,5597) | Dreadnoth (4301, 4302, 4303) |
| section 3 west (4113,5610) | the Dagon'hai (4314, 4315: 8/8) |
| section 3 east (4141,5610) | Xarth (4301, 4302) |
| Ocellus door (4124,5621) | Ocellus, plane 1 (4301 [867..876]) |

When each one moves the raider and where it lands: §6.

## 3. Moving between rooms (the spectator's own packets: 4301 [49..54], [283..288], [559..567], [868..877])

| tick | recorded |
|---|---|
| O | `fade_overlay` (174) opened on `overlay_atmosphere` with its text "", script 948 (0,255,0,0,50) — 30 instead of 50 out of Dreadnoth and through the Ocellus door; `hpbar_hud:hp` hidden, script 2249 on `hpbar_hud:container` |
| O+1 | `minimap_state` 2, `busy` 1, `minimap_toggle 2` |
| O+2 | the teleport |
| O+3 | arrived; the new room's locs and npcs; `minimap_state` 0, script 948 (0,0,0,255,50), `minimap_toggle 0`, `hpbar_hud:hp` hidden again |
| O+5 | `busy` 0, the fade overlay closed, `tfa_hud` (974) re-opened on `overlay_atmosphere` with its two `if_setevents` |

The raiders' own: they disappear from the old room on O+3/O+4 of their op
(Vampyire faced the navigator [279], gone [283]; Fatalsystem [282], [285]) and
are added in the new room facing their arrival angle.

## 4. The atrium (the nexus on plane 2)

Each visit is a fresh instance of square (64,87) on plane 2, built with the
room's state for that stage. Arrival: any tile of a 3x3 by the side and the
visit, from a west room facing east (`angle=1536`), from an east room facing
west (`angle=512`); §6 has the boxes.

| visit | locs (all shape 10) | npcs | evidence |
|---|---|---|---|
| A1, after stage 2 | `tfa_nexus_section_door_closed` (4141,5610 r1), (4113,5610 r3); `tfa_nexus_section2_door_east_slightly` (4141,5597 r1), `..._west_slightly` (4113,5597 r3), +1 tick both `..._slightly_opened`; `tfa_nexus_ocellus_door_closed` (4124,5621 r0); `tfa_in_raid_scoreboard_broken` (4119,5578 r2); `tfa_exit_large` (4124,5569 r0) | `eva_cashien_sword` (4127,5597) S; `armacus_selles` (4132,5579) N (4301) or (4120,5582) E (4302, 4303) | 4301 [286], 4302 [117], 4303 [705], [2400] |
| A2, after stage 3 | `tfa_nexus_section3_door_east_slightly_opened` (4141,5610 r1), `..._west_slightly_opened` (4113,5610 r3), `tfa_loot_wheelbarrow` (4130,5608 r0), the Ocellus door closed, the scoreboard | `eva_cashien` (4135,5603) W; `tfa_midraid_supplies_npc` (the Quartermaster) (4129,5609) S: "Stock available!" (+5), "Shop's open!"; `tfa_ivy_sophista_noop` (4127,5613) S: "Give them no quarter, pick a side!" (+12, then every 14) | 4301 [565..591], 4302 [423] |
| A3, after stage 4 | `tfa_nexus_ocellus_door_open` (4124,5621 r0), the scoreboard, the exit | `armacus_selles_postguardian` (4128,5619) S, `eva_cashien_sword` (4126,5619) S, `tfa_ivy_sophista` (4127,5618) S: "The situation is almost under control!" (+9), "Check the vault for danger and let's end this!" (+13) | 4301 [859..872] |

The Alliance's workers stand about each visit: temple knights, druids and
Guardians of Armadyl at fixed spots ((4123,5602), (4131,5598), (4124,5588),
(4132,5589), (4122,5580), (4126,5596)...), playing `human_herbing_grind_npc`
7319, `human_pickuptable` 832, `zalcano_runecraft` 8444,
`human_fletching_add_feather` 8481, `human_pickupfloor` 827 in turn; four more
walk in from the south door (4126/4128, 5573/5574) 3 ticks after arrival. Ours:
the named npcs as recorded, five workers at the 4301 spots cycling those five
seqs every 4..6 ticks (approximate: the order is not derived).

The Quartermaster's shop is not recorded (the spectator's ops get no answer,
4301 [576], [582]): ours, Talk-to says "Stock available!" and the shop is a gap.

## 5. Leaving a boss room: the Alliance Navigator

| tick | recorded | evidence |
|---|---|---|
| boss line +3 | `tfa_room_navigator` (Follow, Quick-follow) added beside the boss's spot, facing back toward the entry, fade-in over 30 cycles | Ezekiel (4067,5535) W 4301 [276]; Dreadnoth (4184,5663) E [549]; Xarth (4183,5535) E [847]; Bork (4184,5599) 4302 [105], 4303 [694], [2368] |
| op | Follow: the raider walks up, faces it, and is gone at O+2 (the transition of §3, into the atrium visit for the next stage) | 4301 [281] -> [283], [283] -> [285] |

## 6. Every way between the rooms (audit 2026-10-22)

Every raider's move between rooms in all 24 sessions (index 4293..4316,
`--all-players`; `tools/doors.py --summary`): 497 moves. A raider added to
the new room still facing their arrival `angle=` had not moved since they
landed (the spectator was already there), so those adds are the landing
tiles; one added with `type=reset` was seen late, wherever they had run to.
O is the op tick: the face on the loc the tick after the last step, or for an
npc the face turning from the npc to a tile. Timing for loc doors is the
FIRST raider of each party through (the others leave when the spectator does).

| way (op) | recorded | ours before | ours now | verdict |
|---|---|---|---|---|
| lobby door (Enter) | O+3 staging teleport (4127,5608) facing north; O+4 a tile of (4126..4128, 5571..5573) (the recorder's own 4/4: 4297 [96..97], [352..353], [816..817], 4298 [130..131]) | O+4 to (4126,5573), no staging | staging at O+3; O+4 the box's north row | fixed; the south rows **open (engine)**: our npc view stops at 24 tiles, so the sleeping Guardian (29,29) is not sent from them (Jagex adds him from (31,3), 4298 [131]) |
| Guardian's west door (Enter, Quick-enter) | Ezekiel: O+4 (with all section doors: the first raider of 36 of 43 parties; 5..7 the rest); (4080..4082, 5534..5536) facing west, 15/15 | O+2, (4080,5535) | O+4, the box | fixed |
| Guardian's east door | Bork: O+4; (4170..4172, 5598..5600) facing east, 37/37 | O+2, (4174,5600): outside the box | O+4, the box | fixed |
| atrium section 2 east | Dreadnoth: O+4; (4172..4174, 5662..5664) east, 38/38 | O+2, (4172,5662) | O+4, the box | fixed |
| atrium section 2 west | Moia: O+4; (4080..4082, 5662..5664) west, 8/8 | O+2, (4080,5663) | O+4, the box | fixed |
| atrium section 3 east | Xarth: O+4; (4172..4174, 5534..5536) east, 24/24 | O+2, (4172,5535) | O+4, the box | fixed |
| atrium section 3 west | the Dagon'hai: O+4; (4082..4084, 5598..5600) west, 7/7, then running on to the transporter's tile (4080,5599) | O+2, (4080,5599): outside the box | O+4, the box | fixed |
| section 3 doors still `_slightly` (5 visits of 13 add them a tick before `_slightly_opened`) | Enter, Quick-enter as the opened ones | no handler (nothing happened) | handled | fixed |
| Ocellus door (Enter, Quick-enter) | O+1 a glide to (4127,5622), O+2 one to (4127,5624) (two tiles each, from (4128,5620) and (4129,5620): `[walk]` 2 tiles a tick), the model fading out 30..60; O+8 a tile of (4126..4128, 5635..5637), facing reset, fading in 0..30 (O+8 5 of 7 first raiders: 4308 [1451] -> [1459], 4309, 4312 x2, 4315; O+9, O+10) | Enter only, O+2, (4126,5636) facing north, no glides | both ops; the glides at O+1, O+2 with `p_transparency(30, 60, 0, 255, false)`; O+8, the box, no face, `p_transparency(0, 30, 255, 0, false)`; fade 30 out | fixed (2026-10-22: `p_transparency`, the rev-241 PLAYER_INFO TRANSPARENCY block) |
| navigator (Follow, Quick-follow) | O+2 (above); atrium boxes by side and visit: from the west (4114..4116, 5585..5587) A1 11/11, (5598..5600) A2 6/6, (5611..5613) A3 2/2 facing east; from the east (4138..4140, 5585..5587) A1 26/26, (5598..5600) A2 20/20, (5611..5613) A3 11/11 facing west | O+2; (4116,5586), (4139,5587), (4139,5600), (4136,5612) (outside the box), (4116,5600), (4116,5612) | O+2; the boxes | fixed (tiles) |
| The Blade (Commune, Travel) | O+4 (4316 [2845] -> [2849] twice, 4301 [1965] -> [1969]); (4127,5774) facing north up the one-tile hall, 4 of 5 (the fifth, landing beside another, (4127,5775)) | O+2, (4127,5777) | O+4, (4127,5774) | fixed |
| the vault's Rift (Leave, Quick-leave) | the lobby on the world's 5-tick clock (map_clock 1 mod 5, 6 of 7; 4308 [2431] three raiders with ops [2424], [2427], [2429]), O+2..O+7; (4128..4130, 5559..5561) facing south (`angle=0`), 36/36 lobby arrivals in the box, 34 south | O+2, (4130,5559), no face | first clock tick from O+2; the box; facing south | fixed |
| dying (to the lobby) | the same box and face; the same clock (62 of 83) | (4130,5559), no face | the box, facing south | tiles fixed; the death's clock **open** (not a door: D+13 stays) |
| the Archive transporters (Use, Quick-use) | across on the loc's row, whatever row the raider stood on | `coordx(coord) > coordx(loc)` picked the side, so a raider level with the loc (north or south of it) went the wrong way; the Dagon'hai's new landing walks there | level with the loc counts as outside | fixed (`tfa_dagonhai.lua` went east, over the wrong wall) |
| `tfa_exit_large` (Exit) | no raider used it | the lobby | the lobby's box, facing south | unverified |
| `tfa_nexus_section{1,2,3}_door_{west,east}` (no suffix; the atrium's beaten doors) | the spectator's click (4316 [1093]) got only the spectate line | no handler | no handler | unverified (no raider's click) |

Every landing box is uniform enough to call random (Bork's 37: 2..8 a tile;
Dreadnoth's 38: 1..7). The landing face is an `angle=` on Jagex's wire, and
ours now is too (`p_faceangle`, below); the lobby's `angle=0` the same.

### The engine side (2026-10-22)

Two PLAYER_INFO blocks our server could not send, checked against the rev-241
deob (`Deobfuscator/src_osrs241_rl1_13_3`):

| block | deob | ours |
|---|---|---|
| FACING, angle kind | class64.method1691: header (bits 0-2 walk mode, 3-5 kind, 6 instant), kind 2 = one smart, the angle; it replaces the coord/entity face (one state) | `p_faceangle(int $angle, boolean $instant)` (opcode 11280), written by mock241's FACE block; a `facesquare` the same tick loses to it |
| TRANSPARENCY | class81 flag 0x10000: g2sAlt3 start, g2sAlt3 end (each + the client cycle), g1sAlt1 from, g1sAlt1 to, g1Alt3 use-start, into class17.method328 -- the same state an npc's fade lives in (class83.field844) | `p_transparency(int $start, int $end, int $from, int $to, boolean $usestart)` (opcode 11281), after EXACT_MOVE in mock241's tail; the client's 241 reader now keeps it (PKT_PLAYER_INFO_OP_TRANSPARENCY) and fades the player's body as it does an npc's (`app_world_sync_player_transparency`) |

Both are rev 241 only, like `npc_transparency`. `src/torirsserver/test/
mock241_playerinfo_test.c` round-trips them through the client's reader.

Found on the way: on the tick a move into another instance armed the rebuild
barrier, `maybe_rebuild` sent that tick's PLAYER_INFO but phase 11 kept the
player's masks for the barrier, so the first PLAYER_INFO after
MAP_BUILD_COMPLETE sent every arrival block a second time (and a zero-length
teleport). The tick that sent the prefix now cleans up like any other
(`cleanup_held`, torirs_server_world.c); the arrivals' faces and the fade-in
go out once, as Jagex's do.
`test/guthixian_archive/tfa_doors.lua` walks every door above but the
navigator and The Blade (both behind a boss) and checks each landing and
the ticks against the lobby door's (44/44).

Open: the landing box draws two `random()` calls, which shifts every roll
after it. `tfa_ezekiel.lua` (its first special must be the cross) and
`tfa_bork.lua` (its bot never leaves a stomp, and one sequence lands 43, 48,
50 and 28 in seven ticks) fail on the shifted rolls and pass with the new
tiles and no draws (2026-10-22): both tests lean on one roll sequence.

## NPC info audit (2026-10-21)

Every NPC_INFO block of every npc of the room, recording against our tick log, per life:
`tools/rsprox/npc_stream.py --match <type> --summary <transcript> <ticklog>` (counts per
life), `tools/rsprox/npc_timeline.py` (the recording's blocks) and
`tools/rsprox/ticklog_npc_timeline.py` (ours, with the file-only `npc_info` rows:
spawn angle, ops, level, transparency, exact move, timed bar). Facing is part of npc info
and is audited block by block (the entity face, the coord face and its `instant`, the angle,
the reset, the walk type).

| npc | block | recorded | ours now | verdict |
|---|---|---|---|---|
| Armacus (after the Guardian) | add | north, `angle=1024, instant=true`; `reset` with the first step +1; the line +5 | ours: no angle face, no reset | fixed |
| Guardians of Armadyl | fours | one four fewer than the party, 8 ticks apart (two raiders 1, three 2, four 3: 3/3); any of the six kinds; the angle face, reset and run at +5 (16/16) | ours: one four, running at +1 | fixed (solo: one, unrecorded) |
| atrium speakers | add | turned to their face tile and facing it (15/15; the Quartermaster no face) | ours: the angle only | fixed |
| Eva / Ivy | lines | `angle=0` every 15 from +8..+13, a line 0..5 after each, from a pool (Eva 3 lines, Ivy 5) (14/14) | ours: no turns, one fixed line | fixed |
| Quartermaster | lines | +5..+10, then every 12, from 4 lines (4/4) | ours: one line once | fixed |
| workers | benches | grindstone (26,12) `human_herbing_grind_npc` every 4..6, table (28,20) `human_pickuptable` every 3..5, runes (36,21) `zalcano_runecraft` every 6..8, each its own beat for its life; facing the bench (21 lives) | ours: a random seq every 5 | fixed |
| workers | builders | facing the middle, `reset` +1, `human_pickupfloor` then +2 the feather, 10 and 7 apart in turn (18/18); they walk between | the anims and faces; no walk | **open (the builders' walks)** |
| workers | kinds | any of five kinds at any place (49 lives) | random of the five | fixed |
| reinforcements | fours | before the third stage two fours at the south door +1..+3 and +15 later, one kind each (knights, druids, Guardians); the angle face, reset and run at +5 | entry built; they run up the hall | **open (they go on to the benches)** |
