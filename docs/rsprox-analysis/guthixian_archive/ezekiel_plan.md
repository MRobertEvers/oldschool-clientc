# Ezekiel: spec from RSProx

Two fights, both spectated: index 4300 [865..1188] (party of 2, Will Warlock
tanking, killed [1181]; the session ends before the line) and index 4301
[52..285] (party of 3, Visqo tanking, killed [262], line [273]). The wiki
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

- He wanders (4301 [62..64] SE,S,S; 4300 [870..876] SW x6) until a raider comes
  close: engaged when one was within 2 of his 3x3 (4300 [878]: Will at +4,+3;
  4301 [65]: Visqo adjacent); not at 6 (4301 [64]). Ours: 4 (unverified
  between 3 and 5).
- The engage tick: the boss bar (`hpbar_hud_npc` 16652, hp = base = 3000 solo,
  `hpbar_hud_boss` 1), music 890, `[facing] player=` the tank; +1/+2 a line,
  one of "You choose to stand against me? Fall then." (4300 [879]) or "To stand
  against good is to be evil, regardless of reason." (4301 [67]).
- The tank is the raider who engaged him (Visqo 4301, Will 4300: every melee
  faced them, 39/39). Ours: the tank until they die, then the nearest.

## 3. The clock

One clock: melee, melee, special, repeat (4301 M M S x4; 4300 M M S x6).

| gap | ticks | count |
|---|---|---|
| engage -> first melee | 5..6 | 2/2 |
| melee -> melee, melee -> special | 5 | 38/38 |
| special -> melee | 7 (8 when he had to walk) | 9/10 |
| weakened (§6) -> next attack | `idle_to_weaken01` at W, `weaken_to_idle01` W+8, attack W+12, and the cycle starts over (melee first) | 6/6, 3/3 |

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

## 5. Special: the star and the swords

On the special's tick S (10/10):

- Seq `npc_ezekiel_special01` 14693, spotanim `ezekiel_special_crack_vfx01`
  slot 2, `[facing] type=reset`; `say` one of "Divine consequence!" (with the
  cross pattern), "Divine judgement!" or "Your evil plots end at me!" (with the
  others).
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

  | layout | swords (sw tile -> where it flies) | seen |
  |---|---|---|
  | cross | (4059,5540)->(4059,5528), (4059,5528)->(4059,5540), (4056,5534)->(4068,5534), (4068,5534)->(4056,5534), all together | 6 |
  | west column | (4056,5537), (4056,5534), (4056,5531) -> east, one a tick from the north | 2 |
  | north row | (4059,5540), (4062,5540), (4065,5540) -> south, one a tick from the west | 2 |
  | offset cross | (4068,5537)->W, (4056,5537)->E, (4059,5540)->S, (4059,5528)->N, together | 1 |
  | two rows | (4068,5537), (4056,5537), (4056,5532), (4068,5532) | 1 (cut by the finale) |

  The first special was the cross both times; the rest look random. Ours: the
  cross first, then any of the five.
- Each sword: added facing south, `transparency` 255->0 over 30 cycles,
  `[headbar] id=87 startfill=60 endfill=0 endtime=390` (its 13-tick charge
  clock). At S+14 (one a tick for the line layouts) it flies: `teleport` 12
  tiles across with `exact_move` (from, 0, to, 30, angle of travel), `[facing]
  angle=` the same, headbar 42 removed, `transparency` 0->255 cycles 30..60, ops
  0b00000; deleted two ticks later. A flight sound at his centre, one of
  12458..12461, r10.
- A sword's 3-wide band hits every raider in it (3 hits: 28, 31, 52) and leaves
  a burn of 2 (hitsplat 67, 16 splats every 2..5 ticks). Ours: 28..52, burn
  unverified (not built).

## 6. A sword through Ezekiel: weakened

- A sword whose lane equals his south-west tile's x (a column) or z (a row) at
  its flight weakens him (6/6 hits, 0/4 misses: 4300 [980..982], [1043..1045]).
- W: `npc_ezekiel_idle_to_weaken01`, say "Agh!", sounds 7115 r10 and 12455 r20;
  W+1 `[facing] type=reset`, sound 12398 r10 (4/6); W+8
  `npc_ezekiel_weaken_to_idle01`; no attacks until W+12 (§3).
- A miss: nothing (no heal in either fight; the `sanguinesti_staff_heal` on
  him in 4300 is a raider's staff).
- What a raider's hit on a sword does (the wiki's "charge") is in neither
  fight (no sword took a hit): ours, the sword takes damage, unverified.

## 7. The raiders' roll and strafe

On the flight ticks and in the finale's cage, raiders move by
`tfa_ezekiel_roll` (exact move 2..3 tiles over 15 cycles) or
`tfa_ezekiel_strafe` (1 tile over 8): 3 in the fight (each on a flight tick),
every cage move (60). The input that does it is not recorded. Ours: not built;
raiders walk and run (a named gap).

## 8. The finale (at a third of his health)

| tick | recorded | evidence |
|---|---|---|
| F (hp <= 33.5%: 1457/4350, 1003/3000) | his swords deleted; `telejump` to the arena cell next to the cage's middle cell, the side he was nearest (south (4062,5531) 4301; east (4065,5534) 4300) with `exact_move` (old, 0, new, 60, 0), seq `npc_ezekiel_walk01`, `[facing] angle=0`; sound 12458 r10 | 2/2 |
| F+1 | his 3x3 under 9 `invisible_type8_blocking` (4301 [209]) | |
| F+2 | `[facing] coord=` the cage's centre (4063,5535) | 2/2 |
| F+3 | `npc_ezekiel_finale01`, spotanims `vfx_ezekiel_finale_cast01`, `vfx_ezekiel_finale_cast_glow01` slot 1 | 2/2 |
| F+5 | `vfx_ezekiel_finale_cast_glow_loop01` slot 5; sound 12407 r20 at his centre | 2/2 |
| F+6 | the 9x9 (4059..4067, 5531..5539) blocked but the cage (4062..4064, 5534..5536): 72 `invisible_type8_blocking`; raiders outside the cage moved in by `exact_move` (to (4062,5534)-ish, delay2 29); seven `tfa_ezekiel_sword_noop` (the fence) on the other 3x3 cells' sw tiles ((4059|4062|4065), (5531|5534|5537)) with `vfx_ezekiel_sword_fence_drop01..07` | 2/2 |
| F+8 | three `tfa_ezekiel_finale_vfx` in a V in front of him: the front one on his edge tile facing the cage, two one back and to the sides; `vfx_ezekiel_finale_sword_drop01/03/04`; three sounds 12400 r15 | 2/2 |
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
| K+1 | his spotanims cleared (slots 0, 5, 1, 6), `npc_ezekiel_finale_death01`, say "...Formidable.", transparency 0->255 cycles 210..270, ops 0b00000; the V `vfx_ezekiel_finale_sword_despawn01/03/04`; `tfa_ezekiel_death_sword` added on his tile, `[facing] angle=1024` | 4301 [263], 4300 [1181] |
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
| raiders' roll/strafe | 63 rolls/strafes | none | **open**: the input is not recorded (§7) |
| sword burn | hitsplat 67, 2 a splat, 16 splats | none | **open**: unverified, not built |
| defend sound | not sent to a spectator | null | **open**: unknown |
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
| Ezekiel | engage | `reset`, the entity +3 | same | fixed |
| Ezekiel | special | `reset`, the entity +5; weaken: the entity at recover+1 | same | fixed |
| Ezekiel | finale | the tile face a tick before the jump, then the angle; the vfx `angle=1024, instant=true` | same, but the jump glides (the engine moves 2 tiles as a walk) | **open (engine)** |
| swords | flight | `angle=` the flight's | same | fixed |
