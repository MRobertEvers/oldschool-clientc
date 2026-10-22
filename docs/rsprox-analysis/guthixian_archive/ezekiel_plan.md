# Ezekiel: spec from RSProx

Two fights, both spectated: index 4300 [865..1188] (party of 2, Will Warlock
tanking, killed [1181]; the session ends before the line) and index 4301
[52..285] (party of 3, Visqo tanking, killed [262], line [273]). Re-audited
2026-10-21 over every Ezekiel life on the index: 4300, 4301, 4304 [1370..1500]
(three raiders, a wipe), 4314 [524..610] (a wipe) and [1018..1300], and 4315
[857..1100], [2511..2830], [4414..4600] -- 8 engages, 42 specials (§2b,
§5, §6b; `tools/ez_spec.py`, `tools/ez_engage.py`, `tools/ez_freeze.py` on
the `--all-players` transcripts). The wiki
(`sources/Ezekiel.wiki`, `sources/TFA_Strategies.wiki`) has the stats and the
finale's three patterns. Tools: `tools/boss_view.py`, `tools/ez_swords.py`,
`tools/ez_finale.py`; tiles below are relative to Ezekiel's south-west tile
(he is 3x3) unless absolute.

## 1. The room

| fact | evidence |
|---|---|
| Template square (63,86); the raiders arrive at (4076..4081, 5534..5536) facing west (`angle=512`) | 4301 [52..54], 4300 [867] |
| The arena is walled; the only way in is the "Archive transporter" (`tfa_boss_teleporter`, Use / Quick-use) at (4073,5535) rot 3 in the map: a raider beside it is put 7 tiles across to (4066,5535) two ticks after the op, with spotanim `abyssal_demon_teleport` (409) and `[facing] type=reset` | 4301 [63..65] Visqo, [68] Vampyire, [69] Fatalsystem; 4300 [878] Will |
| `tfa_ezekiel` added with the room at (4064,5536), spawnangle east, `[facing] coord=(4081,5535)` (the arrivals) | 2/2 |
| The arena is the 9x9 at (4059..4067, 5531..5539): his 3x3 stays in it (59 walks), the star is clipped to it (§5), the finale's cage is its middle 3x3 cell (§8) | 2/2 |
| Music 890 from the engage; 147 before it | 4301 [65], 4300 [877] |

## 2. Before and at the engage

- He wanders 3 from the arena's middle cell (4062,5534): every wander tile of
  the 8 lives is in (4059..4065, 5531..5537), all four corners seen (4300
  [876] (4059,5531), 4304 [1372] (4065,5537), 4315 [4431] (4059,5537), [861]
  (4065,5531)). The `[add]` tile is wherever he had wandered to.
- The engage is the first raider through the transporter, not a distance:
  A = the tick that raider stands on the landing tile (4066,5535), whatever
  his distance (5 in 4300 [877] and 4315 [4435], 1 in 4314 [533]). The bar
  (`hpbar_hud_npc` 16652, `hpbar_hud_boss` 1), music 890 and `[facing]
  player=` the tank at A+1 (6/8; A itself in 4301 [65], 4304 [1375]); the line
  at A+2 (8/8), the first melee at A+6 (8/8). Earlier "within 2 of his 3x3"
  was a coincidence of the landing tile.
- The line, one of three: "You choose to stand against me? Fall then." (3),
  "To stand against good is to be evil, regardless of reason." (3), "Your
  defence of this place ends with me." (2: 4314 [1030], 4315 [4437]).
- The tank is the first raider in (8/8; the wiki: "the first player to enter
  the room is marked as the tank until they die"). When the tank dies, the
  raider nearest his 3x3 is the new one, faced at once with "Face me!" (3/3):
  4304 [1437] the tick after John B Shigg's last hit (Vivoxe at 1 chosen over
  Song19 at 2), 4304 [1476] and 4314 [572] at W+9, the tick after he rises
  from a weaken that the tank died in.

## 2b. How he moves

He steps one tile a tick toward the tank (diagonals included) until the
tank is edge-on to his 3x3, and steps out when the tank stands under him
(4314 [558], [559], [561], [1053..1055]). Rooted, the tank out of reach:

| window | steps / chances | evidence |
|---|---|---|
| S..S+4 (the special) | 0 / 139 | 42 specials |
| S+5 | 38 / 39 | |
| W+1..W+9 (weakened) | 0 / 30 | |

A step that brings the tank edge-on swings on the same tick when the attack
is due (`tools/ez_stepswing.py`: 4300 [906] melee on its due tick; after a
special every step on S+7 or later swings with it, and a step on S+5 or S+6
waits for S+7). At due+1 the reach is judged before the step: the tank still
out of reach then is the magic (§4b), even when that tick's step reaches
them (4314 [1057], 4315 [924]).

## 3. The clock

One clock: melee, melee, special, repeat (4301 M M S x4; 4300 M M S x6).

| gap | ticks | count |
|---|---|---|
| engage -> first melee | 5..6 | 2/2 |
| melee -> melee, melee -> special | 5 | 38/38 |
| special -> melee | 7 (8 when he had to walk) | 9/10 |
| weakened (§6) -> next attack | `idle_to_weaken01` at W, `weaken_to_idle01` W+8, attack W+12, and the cycle starts over (melee first) | 6/6, 3/3 |
| heal (§6b) -> next attack | melee at heal+5, and the cycle starts over | 3/3 (4304 [1412], 4315 [906], [2720]) |

He walks to the tank (one tile a tick, `npc_ezekiel_walk01` is his walk) and
attacks only from a tile next to his 3x3.

## 4. Melee: the cleave

- Seq `npc_ezekiel_melee01` 14692; the hit lands on the seq tick on every raider
  standing on the 16-tile ring around his 3x3 except three (39/39).
- The three safe tiles are opposite the tank: mirror the tank's ring tile
  through his centre (2-x, 2-z), and take that tile and its two neighbours
  along the same side (39/39; e.g. tank (-1,1) -> safe (3,0),(3,1),(3,2); tank
  (0,-1) -> (1,3),(2,3),(3,3)).
- `map_anim tfa_wave_dust` (delay 5, 6 or 7 per tile, at random) on each of the
  13 hit tiles (39/39).
- Damage: 0..23 recorded (63 hits); the raiders' prayers are not in the
  transcript. Ours: crush, max 28 (unverified), protect from melee halves it
  (unverified).
- No area sound (the 211/212 pair in the room is the raiders' thralls).

## 4b. Magic: the tank out of reach

Not in 4300/4301 (their tanks stood in); 5/5 in today's kills (index 4314
[562], [1057], [1161]; 4315 [898], [924]), each after he walked a tick past an
attack's due tick without reaching his kiting tank:

- Sound 12432 delay 30, range and size 7, at his middle as the tick began,
  before that tick's step (4314 [562], [1057], 4315 [898], [924]: 4/4).
- Seq `npc_ezekiel_magic01` and spotanim `ezekiel_magic_attack_vfx01` on him; on
  every raider in the room `ezekiel_magic_attack_impact_vfx01` slot 2 delay 30,
  the same tick; each hit two ticks on: 21, 4 / 21, 21 / 8, 18, 8 / 20, 9, 10
  (4..21; prayers not on the wire, so the effect of protect from magic is
  unverified -- ours blocks it).
- It comes 6 after a melee (892 -> 898, 1051 -> 1057) or 8 after a special (916
  -> 924, 1153 -> 1161): the "8 when he had to walk" of §3.
- The melee count starts over: 898 magic, then melee 906, 911, special 916.
- Ours: `~tfa_ez_magic` (tfa_ezekiel.rs2), constants `^tfa_ez_magic_min/max`
  (tfa_west.constant); `tfa_ezekiel.lua` kites him for it (two casts, 344, 350).

## 5. Special: the star and the swords

On the special's tick S (10/10):

- Seq `npc_ezekiel_special01` 14693, spotanim `ezekiel_special_crack_vfx01`
  slot 2, `[facing] type=reset`; `say` one of "Divine consequence!" (13),
  "Divine judgement!" (13) or "Your evil plots end at me!" (16) over 42
  specials, whatever the swords ("Divine consequence!" with the two rows at
  4314 [585], three rows [1189], three columns 4315 [2585]). The 2-fight
  pairing of "Divine consequence!" with the cross was chance.
- Sounds at his centre: 12454 r31 size 31, 12441 delay 90 r31 size 31, 12441
  delay 150 r31 size 31.
- The star (map anims, each tile three stages: `vfx_ezekiel_star_explosion_spawn01`
  delay d, `..._marker01` d+30, `..._explosion01` d+60):
  - d=34: the 16-tile ring;
  - d=36: the arm tiles one further out, three wide, on all four sides;
  - d=38: the arm's middle tile two out; d=40: three out;
  - every tile clipped to the 9x9 arena (10/10, §1).
- At S+3, every raider standing on a star tile (their tile at the end of S+2)
  is hit (13 hits: 20..33; ours 20..33 typeless, unverified max).
- The swords (`tfa_ezekiel_sword` 16653, "Animated Sword", size 3, Attack,
  80 hp, cache) spawn with the special, one of five layouts on the arena's
  lanes (absolute; the arena's border lines x=4056/4068, z=5528/5540):

  | layout | swords (sw tile -> where it flies) | seen (42) |
  |---|---|---|
  | cross | one column x of 4059/4062/4065 (from (x,5540) S and (x,5528) N) and one row z of 5531/5534/5537 (from (4056,z) E and (4068,z) W), all together | 21: (4059,5534) 6, (4065,5534) 3, (4065,5531) 3, (4065,5537) 3, (4062,5531) 2, (4062,5537) 2, (4059,5537) 1, one cut by the finale; never (4062,5534) or (4059,5531) |
  | three rows | (4056,5537), (4056,5534), (4056,5531) -> east, one a tick from the north | 9 |
  | three columns | (4059,5540), (4062,5540), (4065,5540) -> south, one a tick from the west | 6 |
  | two rows | z 5532 and 5536, a sword from each end, together | 6 flown, all 5532/5536 (4300 [1056]: 5532/5537, cut by the finale) |

  The first special: the cross on (4059,5534) 4, on (4065,5537) 3, the two
  rows 1 (4315 [4451]). After it the layouts read as random (no tie to his
  tile or the raiders'). Ours: the first of the two crosses at even odds,
  then a cross 2/5 on any lane pair but the middle, each other layout 1/5
  (approximate).
- Each sword: added facing south, `transparency` 255->0 over 30 cycles,
  `[headbar] id=87 startfill=60 endfill=0 endtime=390` (its 13-tick charge
  clock). At S+14 (one a tick for the line layouts) it flies: `teleport` 12
  tiles across with `exact_move` (from, 0, to, 30, angle of travel), `[facing]
  angle=` the same, `[headbar] id=42, removed=true`, `transparency` 0->255 cycles 30..60, ops
  0b00000; deleted two ticks later. A flight sound at his centre, one of
  12458..12461, r10.
- A sword's 3-wide band hits every raider in it (3 hits: 28, 31, 52) and leaves
  a burn of 2 (hitsplat 67, 16 splats every 2..5 ticks). Ours: 28..52, burn
  unverified (not built).

## 6. A sword through Ezekiel: weakened

- A sword whose lane equals his south-west tile's x (a column) or z (a row) at
  its flight weakens him (6/6 hits, 0/4 misses: 4300 [980..982], [1043..1045]).
- W: `npc_ezekiel_idle_to_weaken01`, `[bas] turnleft, turnright, ready =
  npc_ezekiel_weaken01`, `[headbar] id=85, startfill=0, endfill=0, endtime=1`
  (32/32 weakens), say "Agh!", sounds 7115 r10 and 12455 r20; W+1 `[facing]
  type=reset`, sound 12398 r10 (4/6); W+8 `npc_ezekiel_weaken_to_idle01` with an
  empty `[bas]`; no attacks until W+12 (§3).
- His tile is the one after his move on the flight tick: 4315 [899] he
  stepped from (4060,5537) to (4061,5536) as the row 5537 flew, and healed.

## 6b. A miss: the heal

The wiki (TFA_Strategies: "If Ezekiel's south-west tile is not hit by the
south-west tile of Flying Sword, he heals") and five recordings:

- A cross or the two rows that all miss: at fly+2 (S+16) `npc_ezekiel_heal01`,
  spotanims `ezekiel_heal_glow_vfx01` (slot 0) and `ezekiel_heal_spotanim_vfx01`
  (slot 2), say "Saradomin, give me strength.", bar 85 as a weaken's (5/5);
  at fly+4 a heal splat
  (`hitsplat` 6) of a tenth of his health, never past full: 300 of 3000 (4314
  [603]), 435 of 4350 (4315 [2717]); 263, 401, 361 each cut at full (4304
  [1409], [1440], 4315 [903]). 5/5. No sound; his face is kept.
- A line that misses: nothing (2/2: 4300 [966] the rows, [1029] the columns;
  the `sanguinesti_staff_heal` on him is a raider's staff).
- The clock starts over (§3).
- What a raider's hit on a sword does (the wiki's "charge") is in neither
  fight (no sword took a hit): ours, the sword takes damage, unverified.

## 6c. Wind surge on the swords: the charge, and the sword's hit on him

`tools/ez_charge.py` (per sword) and `tools/ez_sworddmg.py` (per flight), over
4300, 4301, 4304, 4314, 4315 (32 flights through his south-west tile, 14 of
them charged).

**The charge.** Every charge in the recordings is wind surge: 54 casts at a
sword (B0aty, Saxerpillar, Woox; `windsurge_casting`, `windsurge_travel`,
`windsurge_impact` on the sword), 51 landed, 3 splashed (`failedspell_impact`,
4315 [897], [963], [1039]). A landed surge is a `hitsplat` 23 on the sword
and its bar `headbar 42` (the 87 charge clock's partner) fills: the bar is
the damage taken out of 80, 70 wide (54 -> fill 47, 80 -> 70). The first
surge 48..55, the second 26..27, exactly what tops it at 80 (4314
[1200]+[1204], 4315 [2552]+[2556], [2630]+[2632]); nothing past 80. The sword
never dies, plays no seq when hit (0 sword seqs in 5 sessions: the wiki's
"stagger" is not on the wire), and turns to whoever hit it (`[facing]
player=`, kept into its flight). It flies at S+14 charged or not.

**The sword's hit on him.** Every sword whose lane takes his south-west tile
hits him, charged or not: one splat for the flight, worth the sum over those
swords of (its charge before the flight tick + 12..15). 32/32 flights. The
splat is the tick after the flight when the swords' npc indexes are above
his (4300, 4301, 4314, 4315: 30/30), on the flight tick when below (4304,
swords 33568.. against his 62765: 2/2): the hit is queued by the sword and
taken when he is next processed. Ours: fly+1 (he is added before them).

| swords through him | charge before the flight | his splat | less the charge |
|---|---|---|---|
| 1, uncharged | 0 | 12, 12, 13, 13, 15 | 12..15 |
| 2, uncharged | 0 | 25, 25, 25, 26, 26, 27, 27, 28, 28, 29, 29, 30 | 25..30 |
| 4, uncharged (a cross through both his lanes) | 0 | 53 (4304 [1503]) | 4 x 13.25 |
| 1 | 54 | 66, 68, 69 | 12, 14, 15 |
| 1 | 80 | 92, 94, 95, 95 | 12..15 |
| 2 | 54 | 81, 84 (55) | 27, 29 |
| 2 | 80 | 108 | 28 |
| 2 | 133, 134, 134 | 159, 160, 158 | 24..26 |
| 2 | 160 | 188 | 28 |

- A charge landing on the flight tick is too late: 4314 [1094] took 52 with
  the flight, and the splat was the bare 25 of two swords.
- The 12..30 splats of the uncharged weakens are this, not a raider's hit
  (no raider attack seq on the tick in 4314 [1095], 4315 [930], [1039]).
- A spectator sees `hitsplat` 17 (another player's damage) whoever charged it.
- A charge does not change the weaken: `weaken_to_idle01` at W+8 and the
  attack at W+12 in every charged weaken (4314 [563], [1204], [1240]; 4315
  [2559], [2636], [4542]). "Charged swords stun him, preventing the heal"
  (wiki) is true of every sword through him (§6b), charged or not.

**On the raiders** (unverified, prayers not on the wire): an uncharged
sword's band 27..31 (one sword) or 52..63 (two); a charged one 75, 84, 102
(4314 [1277], 4315 [2745], [4587]: charge + 21..30), but Woox in two bands at
4314 [563], one of them charged to 54, took 60.

**Models.** Every sword, charged or not, carries one of four models (62645..
62648, `[body_customisation]`) on its add and its flight.

**Ours (2026-10-21):** `[ai_queue2,tfa_ezekiel_sword]`: a raider's hit is a
`hitsplat_23` of what it took, the sword's hitpoints are its charge (1 for
none, so healthbar 42 fills), it never dies, faces the raider, and keeps
facing them in flight. What landed before the flight tick counts: each sword
through him adds its charge + 12..15 into one `hitsplat_damage_other` the
tick after (his reserved point kept). A sword's band on a raider: 27..31 +
its charge, summed over the swords holding them, one splat. The four models
by `npc_bodymodel` on add and flight. Test: one sword through him at [258]
and [363], his splat [259], [364]; no charges in the solo test (no magic).

## 7. The raiders' dodge: roll and strafe

**The model, The Blood Moon Rises** (index 4262 = rsprox.net 4288, Lowerniel
Drakan, the recorder dodging; transcribed with the `MOVE_GAMECLICK` filter on,
`tools/dodge_clicks.py`): a dodge is an ordinary walk click handled inside a
window the encounter opens. That tick the player takes up to three steps of
the click's route at once, as one teleport with an exact move
(`human_myq6_wyrd_roll` over 15 cycles for two or three tiles,
`myq6_strafe` over 8 for one; `angle=` the way it goes), and the rest of
the route walks or runs on from the next tick (33105 -> 33106, 33129 ->
33130). The click is filed on the dodge's own tick, so it was sent the tick
before. A route already being walked never dodges. A click 5 tiles away
lands on the route's third step ((1,5) -> (0,3)), never past 3 (360 dodges).
Drakan's window is his jab combo after `telegraph_to_ready01`.

**Ezekiel** (spectated, so no clicks on the wire; `tools/ez_dodge.py`,
`tools/ez_dodge_band.py`): the same seqs `tfa_ezekiel_roll` (14705) and
`tfa_ezekiel_strafe` (14706), the same exact moves (2..3 tiles over 15, 1
over 8, four of 3 tiles: 4300 [908], [1007], 4301 [132], 4315 [2601]), and
two windows, each the tick the hazard lands:

| phase | window | dodges | ordinary steps |
|---|---|---|---|
| the fight | each sword's flight tick (S+14; a line's S+14..S+16) | 20/20 on a flight tick, none on S+13 or S+17 | on the same ticks, the routes already being walked (51) |
| the cage | the three strikes of each volley, V+4..V+6 | 253/253 (83, 86, 84) | 119 from V+7; 7 on V+2..V+6, routes already being walked |

**Ours:** `p_dodgewindow(roll, strafe, from, to)` (engine, opcode 11235;
`player_dodge` in torirs_server_world.c): a move click handled on a tick in
the window takes up to three route steps as one exact move (a free yaw,
`exact_yaw`) with the roll or the strafe. `~tfa_ez_dodge_window` opens it
for every raider in the room at the special (the flight ticks) and at each
volley (V+4..V+6). Test: the cage leg of `tfa_ezekiel.lua` -- volleys [428],
[439]; rolls [432..434], [443..445]; runs between (2026-10-21).

## 8. The finale (at a third of his health)

| tick | recorded | evidence |
|---|---|---|
| F-1 (hp <= 33.5%: 1457/4350, 1003/3000) | `[facing] coord=` the middle of his 3x3 to come; his swords fade out (`transparency 0..30, 0->255`) | 4301 [203], 4300 [1067] |
| F | his swords deleted; `telejump` (the same index, re-added: `level_change` rides it) to the arena cell next to the cage's middle cell, the side he was nearest (south (4062,5531) 4301; east (4065,5534) 4300) with `exact_move` (old, 0, new, 60, 0), seq `npc_ezekiel_walk01`, `[facing] angle=0`; sound 12458 r10 | 2/2 |
| F+2 | `[facing] coord=` the cage's centre (4063,5535) | 2/2 |
| F+3 | `npc_ezekiel_finale01`, spotanims `vfx_ezekiel_finale_cast01`, `vfx_ezekiel_finale_cast_glow01` slot 1 | 2/2 |
| F+5 | `vfx_ezekiel_finale_cast_glow_loop01` slot 5 `loop=true`; sound 12407 r20 size 20 at his centre; his 3x3 under 9 `invisible_type8_blocking` (4301 [209], 4300 [1073]; this row said F+1) | 2/2 |
| F+6 | the 9x9 (4059..4067, 5531..5539) blocked but the cage (4062..4064, 5534..5536): 72 `invisible_type8_blocking`; raiders outside the cage moved in by `exact_move` (to (4062,5534)-ish, delay2 29); seven `tfa_ezekiel_sword_noop` (the fence) on the other 3x3 cells' sw tiles ((4059|4062|4065), (5531|5534|5537)) with `vfx_ezekiel_sword_fence_drop01..07` | 2/2 |
| F+8 | three `tfa_ezekiel_finale_vfx` in a V in front of him: the front one on his edge tile facing the cage, two one back and to the sides, added and snapped facing the cage (north from the south, `angle=1024`; west from the east, `512`, `instant=true`); `vfx_ezekiel_finale_sword_drop01/03/04`; three sounds 12400 r15 size 15 | 2/2 |
| F+12, then every 11 | a volley (below) | 4301 5 volleys, 4300 9 |

A volley is three strikes, one per pattern in a random order (14/14 volleys
used all three). Strike k (k = 0, 1, 2) lights two of the three swords:
`vfx_ezekiel_sword_telegraph01` (front), `..03` (one side), `..04` (other
side) as spotanims slot k, delay 30k; sound per lit sword: front 12417, left
12413, right 12423 (r15 size 15, delay 30k). Its tiles are hit at V+4+k:
map anims `vfx_ezekiel_finale_impact_medium01` or `..._small01` (either, at
random) with delay 124+30k (+2 per row away from him) on the unsafe tiles;
sounds 12407 at the cage centre, delays 120/150/180; 12416 delay 225 at his
centre; from the second volley on, his `vfx_ezekiel_finale_cast01` delay 225.

| lit | safe (in the cage, rows counted from his side) |
|---|---|
| both sides | the far row (3 tiles) |
| front + one side | the far two tiles of the other side's column |

(5/5 in 4301 and 9/9 in 4300: every impact tile is outside the safe set and
every non-safe tile has one.) A raider on an unsafe tile at V+4+k is hit (2
hits: 6, 19); ours 15..30 (unverified).

## 9. Death

| tick | recorded | evidence |
|---|---|---|
| K | his health 0 (the last hits) | 4301 [262] |
| K+1 | his spotanims cleared (slots 0, 5, 1, 6; 5 and 6 with `loop=true`), `npc_ezekiel_finale_death01`, say "...Formidable.", transparency 0->255 cycles 210..270, ops 0b00000; the V `vfx_ezekiel_finale_sword_despawn01/03/04`; `tfa_ezekiel_death_sword` added on his tile facing the cage (`angle=1024` from the south 4301 [263], `512` from the east 4300 [1181]) | 4301 [263], 4300 [1181] |
| K+2 | the V deleted | 2/2 |
| K+11 | "Challenge complete: Ezekiel. Duration: m:ss. Total: m:ss"; music 147; he is deleted; the fence's `transparency` 0->255 over 30, the 72 tiles `invisible_type8_nonblocking` | 4301 [273] |
| K+12 | the fence deleted | 4301 [274] |
| K+14 | the Alliance Navigator at (4067,5535) facing west (`nexus_plan.md` §5) | 4301 [276] |
| K+19 | the boss bar cleared (`hpbar_hud_npc` -1) | 4301 [281] |

## 10. Npc config (server half)

`tfa_ezekiel`: 3000 hp solo (cache stat4), attack rate 5, never retaliates on
its own (the script drives it), blockwalk npc, defend sound unknown (a
spectator gets no synths: null, named gap). `tfa_ezekiel_sword`: 80 hp,
attackable, nomove. The rest: `interactable=no` visuals.

## Audit: our scriptrun against 4301 and 4300 (2026-10-21)

Run: `test/guthixian_archive/tfa_ezekiel.lua` (the Guardian killed fast, the
west door, the transporter, the fight with `::tfaboss_at 31 30` lining him
up with the cross's row lane, `::tfaboss` to the finale and the kill, the
navigator, the atrium). Inventory: `tools/rsprox_tells.py --anchor
16652,16653,16654,16655,16656 idx4301.txt idx4300.txt --ours <ticklog>`;
timelines: `tools/rsprox/ticklog_tells.py`.

### Re-audit (2026-10-21, every life on the index)

Run: the same test on a private pack (`TORIRSSERVER_SCRIPTS`), tick log
`ticklog_npc_timeline.py --ids 16652`.

| rule | recording | ours before | ours now (tick log) | verdict |
|---|---|---|---|---|
| engage trigger | first raider on the landing tile, any distance; bar A+1 | a raider within 4 | landing [196], bar + face [197] | fixed |
| engage face | `player=` the tank on the bar tick (8/8) | reset, face +2 | [197] | fixed |
| engage line | A+2, one of three | one of two | [198] | fixed |
| first melee | A+6 (8/8) | A+6 only if the landing was in reach | [202] | fixed |
| wander | 3 from (4062,5534) | 3 from (4064,5536): his 3x3 could leave the 9x9 | spawn (4062,5534) | fixed |
| special line | one of three, any layout | tied to the cross | random | fixed |
| layouts | random cross lanes, two rows 5532/5536 | two fixed crosses, two rows 5532/5537 | as recorded | fixed |
| miss | heal 10% at fly+2/+4 (cross, two rows), nothing for a line | nothing | heals [228]/[230], [276], [324] | fixed |
| after a heal | melee +5, M M S | | [233] | fixed |
| post-special hold | no step S..S+4 (0/139) | stepped from S+1 | special [339], first step [344] | fixed |
| weaken hold | no step W+1..W+9 (0/30) | the queued route went on (W+1) | W [353], next step [365] | fixed |
| tank death | nearest raider, "Face me!", face at once / W+9 | first raider in the list, no line | built; solo test cannot exercise it | unverified on our wire |
| sword charge | wind surge fills an 80-point charge (hitsplat 23, bar 42); every sword through him hits him for its charge + 12..15 (32/32, §6c) | the sword takes damage | built (§6c) | fixed; charging not exercised by the solo test |

### First audit (4300, 4301)

| tell | recording | ours | verdict |
|---|---|---|---|
| room entry | the transporter (409, 7 tiles across, op+2) | same | matched |
| engage | bar, music 890, a line one of two at +1/+2 | same (line at +1) | matched |
| clock | M, M, S; S+7; W+12 and the cycle restarts | same (198, 205, 210, 215; W 212, 220, 224) | matched |
| melee | seq 14685, 13 `tfa_wave_dust` delays 5..7 | same | matched |
| special | seq 14687, `ezekiel_special_crack_vfx01` slot 2, a line, 12454/12441 x2 r31 size 31, the star's three stages at 34/36/38/40 (+30, +60) clipped to the 9x9 | same | matched |
| star hit | S+3 on the star tiles | same | matched (damage unverified) |
| swords | five layouts, spawn facing south, fade-in, flight at S+14 (+1 a line sword), exact move 30, yaw, ops 0, fade-out, del +2, a flight sound 12458..61 r10 | same | matched |
| weaken | lane = his sw x/z; seq, "Agh!", 7115 r10 + 12455 r20, 12398 W+1, `weaken_to_idle01` W+8 | same (212..224) | matched |
| finale | F tele + exact move 60 + walk01 + 12458; F+1 9 blocking; F+3 finale01 + cast01 + glow01 s1; F+5 glow_loop01 s5 + 12407 r20; F+6 72 blocking + raiders pulled in + 7 fence swords; F+8 the V + 3x 12400 | same ticks (312..320) | matched |
| volleys | every 11 from F+12; three strikes, each lighting two swords (slot k, delay 30k), the per-sword sounds, 12407 at 120/150/180, 12416 at 225, cast01 delay 225 from the second; impacts medium/small at 124+30k+2r on the unsafe tiles | same | matched |
| death | K+1 spots cleared, finale_death01, "...Formidable.", fade 210..270, ops 0, V despawns, the death sword; K+2 V gone; K+11 line, he goes, fence fades, 72 nonblocking; K+12 fence gone; K+14 navigator | same | matched |
| npc fades | `transparency` on every add/flight/death | `npc_transparency` (new, opcode 11225) | fixed (engine) |
| sword charge bar | `headbar id=87 60->0 over 390` | `npc_headbar_timed` (new, opcode 11226) | fixed (engine) |
| glow loop | `vfx_ezekiel_finale_cast_glow_loop01` slot 5 `loop=true` | sent with loop 0 | **open (engine)**: no loop flag on `spotanim_npc_slot` |
| music | `midi_song_v2` with fade 60/60 | plain `midi_song` | **open (engine)** (the Guardian's too) |
| raiders' roll/strafe | the dodge windows (§7) | `p_dodgewindow` | fixed (the cage tested; the flight ticks share the code, not run) |
| sword burn | hitsplat 67, 2 a splat, 16 splats | none | **open**: unverified, not built |
| defend sound | not sent to a spectator | null | **open**: unknown |
| magic (4314/4315) | `npc_ezekiel_magic01` when the tank is out of reach (5/5) | not built | fixed (§4b) |
| weaken pose | `[bas]` turnleft/turnright/ready = `npc_ezekiel_weaken01` at `idle_to_weaken01`, cleared at `weaken_to_idle01` (4314 [563], [1058], [1094], [1131], [1167]) | none | **open (engine)**: no npc BAS block on our wire (the Dagon'hai's empty `[bas]` too) |
| wipe (4314 [524..610]) | the raiders out to (4033,5505) [606], song 147 [607], bar -1 and the fade [609] | the generic `~tfa_death_spectate` | not diffed line by line |
| damage | melee 0..23, star 20..33, sword 28..52, volley 6..19 (prayers unknown) | melee 0..28 (half under protect from melee), star 20..33, sword 28..52 (one a tick), volley 15..30 | approximate |
| combat level | `level_change 1,043` at 3 raiders | the cache's 984 | party scaling not built (solo) |

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
| Ezekiel | spawn | `spawnangle=east` | same | fixed |
| Ezekiel | engage | the entity on the bar tick (8/8; the `reset` at 4301 [62] was his wander) | same (re-audit) | fixed |
| Ezekiel | special | `reset`, the entity +5; weaken: the entity at recover+1 | same | fixed |
| Ezekiel | finale | the tile face a tick before the jump, then the angle; the vfx `angle=1024, instant=true` | same, but the jump glides (the engine moves 2 tiles as a walk) | **open (engine)** |
| swords | flight | `angle=` the flight's | same | fixed |

## Damage audit (2026-10-21)

Every splat on every raider of the room, put down to its mechanic by
`tools/tfa_mech.py --room ezekiel` over the `--all-players` transcripts of index
4301..4308 (the party runs are spectated, so the raiders' prayers are not on the
wire: a mechanic's zeros are its prayed hits, its nonzero tail mostly the unprayed
ones). Each mechanic is matched by its own geometry: a projectile by its target and
landing tick, a tile hazard by the map anim or loc on the raider's tile, a swing by
the seq beside the raider.

| mechanic | recorded | ours before | ours now | verdict |
|---|---|---|---|---|
| melee wave | 1..9 under protect from melee (32/38 nonzero), 14..27 unprayed | 0..28, half prayed | 0..28, a third prayed | fixed |
| special / star | 21..33 (9) and 24..34 on the star tiles (6) | 20..33 | 21..34 | fixed |
| special / star (4314/4315) | 20..34 (15), star tiles 21..34 (8) | | 21..34 | the floor is 20 in the new data (two 20s): **approximate** |
| swords | 28, 51, 59, 63 | 28..52 | 28..63 | fixed |
| sword burn | 2 every 5 ticks, three times (4304 [1411..1421]); more on its trail | none | the burn; no trail | fixed (the trail open) |
| volley | 6, 19 (2) | 15..30 | same | unverified |

## Wire audit, every block (2026-10-21, third pass)

Run: `tfa_ezekiel.lua` on a private server and pack; compared with all eight
lives of 4300, 4301, 4304, 4314, 4315.

- `tools/rsprox/npc_stream.py --summary`: every NPC_INFO block of Ezekiel, the
  swords, the fence, the V and the death sword.
- `tools/ez_room_diff.py`: every room map anim, area sound, loc and song per
  section, aligned on the section's own tick, tiles from his tile on the row's
  own tick.
- `tools/ez_sounds.py`: id, delay, range, size and loops of every area sound.
- The geometry against its rule, on the recordings and ours:
  `tools/ez_star_rule.py` (42/42 recorded, 6/6 ours), `tools/ez_wave_rule.py`
  (ours 21/21), `tools/ez_volley_rule.py` (249/249 recorded strikes, ours
  30/30).
- The clock: `tools/ez_stepswing.py` and `tools/ez_stepswing_ours.py`.

| row | recording | ours before | ours now | verdict |
|---|---|---|---|---|
| step and swing | the swing on the due tick's step that reaches the tank | a tick later (341 melee, step 346, swing 347) | 346 | fixed |
| magic at due+1 | judged on the reach before the step | judged so (unchanged) | 364, on a step that reached | matched |
| magic sound | 12432 d30 r7 s7 at his pre-step middle (4/4) | none | (6432,160) from (6431,159) | fixed |
| weaken stance | `[bas]` turn/turn/ready `weaken01` at W, empty at W+8 (every weaken) | none | `npc_basready` + `npc_basturn` (new, opcode 11264) at W, `npc_basreset` at W+8 | fixed (engine) |
| bar 85 | `0 -> 0` over 1 cycle on every weaken (32/32) and heal (5/5) | none | every weaken and heal | fixed |
| sword flight bar | `[headbar] id=42, removed=true` | none | the 0x7FFF removal | fixed |
| finale swords | fade 0..30 at F-1, deleted at F | deleted at F-1 | as recorded (per-life rates equal) | fixed |
| finale jump | `telejump`, same index, `level_change` on the re-add | a two-tile glide (walk steps) | `npc_telejump` (new, opcode 11263) | fixed (engine) |
| his 9 locs | F+5 (2/2) | F+1 (the doc's error) | F+5 | fixed |
| 12407 at F+5, 12400 x3 at F+8 | size 20, size 15 | no size | sized | fixed |
| glow loop / death clears | `loop=true` on slot 5 and the slot 5/6 clears | plain | `spotanim_npc_slot_loop` | fixed |
| the V, the death sword | facing the cage from his side | north always | by side | fixed |
| engage, melee, star, swords, heal, weaken, volleys, death | | matched | matched | matched |
| sword flight's jump bit | `[teleport]`: re-added under the same index with the jump bit clear, the client moving the sword it has | re-added with the jump bit set | `npc_teleslide` (new, opcode 11265): the same index, bit clear (`TORIRS_SCRIPTRUN_NPC_MOVE_TRACE`: `jump=0` every flight, `jump=1` for his finale jump); the client keeps an npc removed and re-added in one packet and moves it by the bit (`task_exec_entity_info.c`) | fixed (engine, client) |
| `level_change` at 1..2 raiders | none for Ezekiel (4300, 4314), though the Guardian gets 984 | 984 once | same | **open, invisible**: the drawn level is the same; the rule (skip when the level equals the type's own?) unconfirmed |
| tank death ("Face me!") | the nearest raider (3/3) | built | built | **unverified on our wire**: needs a party test |
| damage | melee, star, swords, volley, burn trail | §Damage audit | same | approximate (prayers not on the wire); the burn trail not built |

Not compared: the raiders' own kit (wind surge's 227 and 4027 on the swords,
a bloom's 2892).

