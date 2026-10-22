# Bork and the Smorks: spec from RSProx

Thirteen fights over nine sessions, all rev 241, all players (`--all-players`):
index 4294, 4303 (two), 4304 (two), 4306 (two), 4308, 4309, 4310, 4312 (two)
and 4316. The first spec was built on 4303 alone; §9 is the audit of every
session, and where it disagrees with §1..§6 it wins. Tools: `tools/boss_view.py`,
`tools/dustgrid.py` (a beat's tiles and delays relative to Bork's south-west
tile), `tools/room_inv.py`. Bork is 4x4, the Smorks' cart 3x3 (cache).

## 1. The room

| fact | evidence |
|---|---|
| Template square (65,87); raiders arrive west of the arena on any tile of (4170..4172, 5598..5600), facing east (`nexus_plan.md` §6) | 37/37 |
| Added with the room: `tfa_bork` (4194,5603) spawnangle west (9 of 13 adds; the other four, 4303's (4192,5601) south-west among them, were re-adds after a wander); `tfa_smork` ("The Smorks", the cart) (4190,5598) west; `tfa_bork_smork_hp_ui_handler` (4161,5569) south (the bar's npc, no ops) | 2/2 |
| The Archive transporter (4180,5599) rot 1: over the wall to (4187,5599), sprite 409 | 4303 [345], [347] |
| Bork wanders the arena until the fight starts, the whole of it: 10 tiles west and 11 south of his spawn recorded (a 12-tile walk at 4316 [555..566]) | 4294, 4303, 4316 |
| Start: the first raider over the wall: sound 12769 r30 size 30 at the arena's middle (4189,5594); +1 the bar: `hpbar_hud_npc` = the handler, `hpbar_hud_hp`/`basehp` 1500 (Bork), `hpbar_hud_extra_current`/`base` 600 (the Smorks), `hpbar_hud_show_extra_bar` 1, `hpbar_hud_boss` 1, the main bar's health colours, the extra bar's 9312/22784/31104, script 10152 (false); music 888 | 4303 [345..346], [1887] |

## 2. Bork's drum (the gong)

- Bork chases his target (the first raider in, 2/2; after each enrage "Bork smash <name> now." names a new one) one tile a tick.
- `npc_bork_gong_melee01` every R ticks (§4): two beats, at T and T+2. Each
  beat is the tiles round his 4x4 out to its reach, each tile's delay the
  Manhattan distance from his drum at (2,2) of his 4x4 (every tile of 4303
  [381..390]); the cart's tiles and what it hides from him get none.
- **The larger beat** (`tools/bork_reach.py`, `tools/bork_beats.py`): the
  first beat's reach is how far his target stood from his 4x4 **on the tick
  before the gong** — one ring when they were in contact, out to them when
  they were not: reach = max(1, that distance) on 105 of 105 first beats in
  4303 and 4294 (84 in contact, one of them under him; 21 out of contact, at
  2: 41-44 tiles, 3: 63, 4: 86-98, 5: 107, 6: 135). The stun-tick gongs are
  left out (their dust is the cannonball's), and one whose target had left
  the arena (4294 [1885]). It shows after his anger (the target walks back
  in: 4303 [433], [654], [1975], [2140], 4294 [1681], [1823]) and whenever the
  tank steps off him mid-cycle (4303 [610], [614], [2230]). The second beat is
  always one ring. Checked again on 4304 [357..667], [1744..2126], 4306
  [469..1100], [1172..1740] and 4308 [523..781] (B0aty's, four raiders):
  117/117, once his silent retargets are counted -- after the anger at 4304
  [431] he turned to the raider in contact (Cuo OSRS) with no "Bork smash"
  line, the speed-up line "Bork get into it!" having taken that tick [446]
  (one overhead line a tick).
- A raider on a beat's tile is hit then (4..7 recorded under the raiders' prayers, 19 once). Ours: crush, max 28, a quarter under protect from melee (unverified).

## 3. The Smorks

- Attack every R ticks from 5 after Bork's first beat, in a cycle of 7 slots: shot, shot, napalm, -, shot, shot, big bomb (4303 [358..395], [440..478], [521..548], [590..614]).
- A shot: `npc_smork_cannon_attack01`, **one projectile at every raider but Bork's target** (268 shots, no target twice, none at his target while he lives; everyone once he is dead), all of one style at random (`vfx_smork_mage_attack_projanim01` or `vfx_smork_range_attack_projanim01`; runs like mmmm and rrrr, never alternating), starttime 0, endtime 150 / 120 / 90 / 90 at R = 7 / 6 / 5 / 4 (90 at the Smorks' 2 after Bork), angle 20, progress 100, heights 270/100 (v2: ours 67/25). Each lands on its raider at shot + (endtime+15)/30: up to 32 unprayed, 0..1 under the matching protection.
- The napalm: a line ("Burny gloop!" / "Fireeeeeeeeee!" / "Hot stuff!") with a voice clip **any of 12747..12756, whatever the line** (85 lines, each line with 8 or 9 of the ten) r15 size 15 at the cart; +1 `npc_smork_cannon_attack01`, `vfx_smork_napalm_fire_projanim01` 0/120 angle 30 heights 270/0 at a raider's tile, `gargboss_debris_shadow_120` on it, sound 12240 r15 size 15. +4 it lands: sound 12239 r6 at the tile, `vfx_smork_napalm_fire_spotanim01` + a `vfx_smork_napalm_splat_spotanim01(_90/_180/_270)` on the tile (delay 0) and spreading (§9: one or two streaks, 3..17 tiles; 4303's seven were one roll); +5 a `tfa_bork_ragged_tile` (random rotation) on every fire tile for 199 ticks (4303 [371..570], [458..657]), then `invisible_type8_nonblocking`.
- A raider on a ragged tile takes 2 a tick (hitsplat 67) and is set alight: a burn of 2..3 every 4 ticks, 5 times (hitsplat 74; 4303 [1916..1932], [2232..2248]).
- The big bomb: "Big bomb! Bork watch out!"; +2 `npc_smork_big_cannon_attack01`, `cannonball_travel_big` 15/150 angle 30 heights 270/0 at a raider's tile, `gargboss_debris_shadow_150` on it, sound 12248 delay 15 r15 size 15. +5 it lands: Bork under it is stunned (§5, 39/39); anywhere else it misses (§9).

## 4. The speed

R starts at 7 (Bork's drum and the Smorks alike). At 75%, 50% and 25% of the Smorks' health it drops to 6, 5, 4 (both fights: 450/299/150 of 600; 1003/668/350 of 1370), with the lines (Bork, Smorks): "Bork get into it!" / "Smorks go faster!", "Bork make fast drum!" / "Smorks feeling drum beat now!", "Bork maximum crush!" / "Smorks go fastest!".

## 5. Stunned, then angry

| tick | recorded | evidence |
|---|---|---|
| L (the bomb lands) | `npc_bork_14_cycle_stun01` + `vfx_bork_14_cycle_stun_spotanim01` (every stun: shown 31/39, and on the other 8 a raider's thrall impact or Sanguinesti heal took slot 0 that tick), his target's tile faced (31/31, theirs before their step), "Oof."; the Smorks one of "That you fault!", "Bork! We sorry! No go!", "No! Fearless leader!"; sounds 12249 r20, 3201 (`myarm_flyback`) r20 size 20, 12273 r15, 12758 r15 size 15 at him | 10/10 |
| L..L+13 | no beats; every raider's hit on him lands (4303 [400..414]: 1426 -> 1182) | |
| L+14 | `npc_bork_enraged_stomp_attack_in01`, "Bork very angry!!" | 10/10 |
| L+16 | `npc_bork_enraged_stomp_attack01` (+ `vfx_bork_enraged_stomp_spotanim01`) | |
| L+17, L+20, L+23, L+26 | the slams: `tfa_wave_dust_short` over the whole arena, delay the Manhattan distance (up to 20) from the middle of the edge he faces his target across, alternating its two middle tiles, but the tiles the cart shadows from him | 4303 [417..426] |
| L+22 | `npc_bork_enraged_stomp_attack_cycle_and_out01` (+ the spotanim) | |
| L+29 | a raider at random; "Bork smash <name> now." only when it is another (31/31 changes said, 6/6 keeps silent) | |
| L+33 / L+31 | the drum again: 4 ticks after a new target (29/29), 2 after keeping the old one (3/3; the one kept at +33 was a change whose line the speed-up took, 4304 [446]) | 34/34 |

## 6. Deaths

- Bork at 0 first (both fights): `npc_bork_death01` + `vfx_bork_death_spotanim01`, "Smorks... avenge... Bork."; +7 deleted, the Smorks "Human die for that!" and from then shoot every raider each slot.
- The Smorks at 0: with Bork dead "Uh oh." and sound 12761 r15 size 15 at the cart (4308 [772], 4312 [693], 4316 [1046]); with Bork alive "No, Smorks fail Bork!" and 12757 r15 size 15 (4310 [622]); +2 `vfx_smork_death_explosion_spotanim01`, `vfx_smork_goblins_death_spotanim01` slot 1, `vfx_smork_tower_death_spotanim01` slot 2, sound 12251 r15; +7 "Challenge complete: Bork. ..."; +3 the navigator at (4184,5599) facing east.
- **The Smorks dying first**: 4310 [622..630] has the first 8 ticks (Bork drums on as before, gongs 625 and 627; the cart deleted +9 [631]); the rest is in no recording. The wiki and every guide
  agree: "If the Smorks are killed first, Bork's stomps will cover the entire
  arena" ([wiki strategies](https://oldschool.runescape.wiki/w/The_Fractured_Archive/Strategies),
  [runescapeguides](https://www.runescapeguides.com/the-fractured-archive-beginners-guide-osrs/),
  [bald.gg](https://bald.gg/blog/osrs-raids-4-fractured-archive-guide)). Ours,
  *unverified* past 4310: the owner's description (2026-10-21) -- he says
  "Bork kill human for that!" (a stream frame the owner sent 2026-10-22:
  Bork at 93.3% over the Smorks' death, overhead, as an npc say) with the
  cart's going at +9, then stands and drums: the cache's
  `npc_bork_idle_to_enraged_gong_attack_in01` (+10) and
  `npc_bork_enraged_gong_attack01` (sounds 12790, 12768 in the seq), from +11
  each beat a smash over the whole room at his rate R (4 by then), the
  drum's damage, until he dies. The rate, reach and damage are ours. His
  death then: no "avenge" line; +7 he is gone and the line.

## Audit: our scriptrun against 4303 (2026-10-21)

Runs: `test/guthixian_archive/tfa_bork.lua` (Bork first; `::tfabkstun` lands
the bomb on him once) and `tfa_bork_smorks_first.lua`. Inventory:
`tools/rsprox_tells.py --anchor 16640,16641 idx4303.txt --ours <ticklog>`.

| tell | recording | ours | verdict |
|---|---|---|---|
| start | the transporter, 12769 r30 size 30, +1 bar (handler npc, Bork 1500, extra bar 600 + colours, 10152), music 888 | same | matched |
| drum | gong every 7/6/5/4, beats T and T+2, Manhattan delays, the cart's shadow | same; reach rule above | matched |
| Smorks | shot, shot, napalm, -, shot, shot, bomb at R; shots 0/150-120-90 a20 p100 67/25 hitting everyone at landing | same | matched |
| napalm | line + sound, +1 shot 0/120 + shadow 120 + 12240, +4 seven tiles (0/3/6) fire + splat, 12239 r6, +5 ragged tiles 199 ticks; then nonblocking with the rotation and the despawn variant | same | matched |
| fire / burn | 2 a tick (67), burn 2..3 every 4, 5x (74) | same | matched (values recorded) |
| big bomb | say, +2 big cannon 15/150 + shadow 150 + 12248 d15, +5 lands; on Bork: stun | same | matched |
| stun / anger | L stun seq (+spot from the 2nd), "Oof.", the Smorks' line, 4 sounds; L+14..L+33 | same | matched |
| speed | 75/50/25% of the Smorks, lines both | same | matched |
| deaths | Bork first: death seq + spot + line, +7 gone + vow; Smorks: "Uh oh." + 12761, +2 3 spots + 12251, +7 line, +3 navigator | same | matched |
| music | `midi_song_v2` with fades | plain `midi_song` | **open (engine)** |
| damage | gong 4..7 (prayed), shots up to 28 / 0..1, slams unknown | gong 0..28 (/4 prayed), shots 0..28 / 0..1, slams 20..39 | approximate |
| defend sounds | not sent to a spectator | null | **open** |
| Smorks first | no recording | the anger looped, no shadow (the wiki) | unverified |

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
| Bork, Smork | spawn | `spawnangle=west` and the way in as a coord face | same | fixed |
| Smork | walk type | `walktype=turnonwalk` on every face (4308 [532..]) | `npc_facewalktype(1)` | fixed |
| Bork | engage | `reset`, the entity +5 | same | fixed |
| Smork | lines | the entity a tick before say slots 2 and 6, held 4; else his target's tile every 2 ticks from engage+10 | same | fixed |
| Bork | stun | the stun spot unless stuns=2, and his target's tile | same | fixed (the tile choice unverified) |
| Bork | stomp, death | the entity at l=13; `reset` at death | same | fixed |

## Damage audit (2026-10-21)

Every splat on every raider of the room, put down to its mechanic by
`tools/tfa_mech.py --room bork` over the `--all-players` transcripts of index
4301..4308 (the party runs are spectated, so the raiders' prayers are not on the
wire: a mechanic's zeros are its prayed hits, its nonzero tail mostly the unprayed
ones). Each mechanic is matched by its own geometry: a projectile by its target and
landing tick, a tile hazard by the map anim or loc on the raider's tile, a swing by
the seq beside the raider.

| mechanic | recorded | ours before | ours now | verdict |
|---|---|---|---|---|
| drum beat | 3..9 through protect from melee and never 0 (360), 18..30 unprayed (11) | 0..28, a quarter prayed | 14..32, 27% through | fixed |
| Smork shots | 0 or 1 under the matching prayer (157/288), up to 32 | 0..28 / 0..1 | 0..32 / 0..1 | fixed |
| napalm landing | 23..28 on the fire's tile | none | 23..28 | fixed |
| ragged tile | 2 a tick (type 67) | 2 | same | matched |
| burn | 1..3 (type 74: 1 16, 2 25, 3 4) | 2..3 | 1..3 | fixed |
| slams | 14..47 (9) | 20..39 | 14..47 | fixed |
| big bomb on its tile | 4, 19 | none | none | **open** (two hits) |

## 9. Every session (2026-10-21, second pass)

The first pass (§1..§6) was built on 4303 with spot checks of 4294..4308. Asked
"What else did you miss in Bork's encounter?", every row of all nine sessions
was put through the extractors below (scratch scripts over the `--all-players`
transcripts, reproducible: per-tick npc info of `tfa_bork`/`tfa_smork`,
projectiles with their `[target]`, map anims, `sound_area`, `loc_add`). Why it
was missed: one session's rolls were taken for rules (seven napalm tiles, no
missed bomb, alternating shots), the miss branch came from the wiki, and no
inventory of every line and every sound pairing was made across sessions.

| rule | recorded | evidence |
|---|---|---|
| cadence | first gong engage+8, first Smork slot +13, both every R (7/6/5/4) | 13/13 fights |
| shots | one projectile per raider but Bork's target, one style, random | 268 shots |
| bomb under his 4x4 (on the tick before the landing) | stunned | 39/39 |
| bomb missed | L: 12249 r20 and 12250 delay 30 r20 at the landing tile; a patch: the tile and 2..4 of its 8 neighbours (delay 0, ragged tiles); the drum and the Smorks go on; L+2 his target's tile faced, "Bork crush now!" (5) or "Bork think it stomp time now!" (3); L+3 his target; L+5 `npc_bork_enraged_stomp_attack01` (no stomp-in); slams L+6, 9, 12, 15; L+11 the stomp's end; L+18 a new target; the drum L+20..22; he stands from L | 8/8 (4294 [1737], [1801], 4306 [623], 4316 [767], [934], [969], ...) |
| bomb a tile off his 4x4 | also 25..28 to him and 12254 r10 at his middle | 3 of 4 (4316 [969] none) |
| napalm on his 4x4 | 25..28 to him, 12254 r10 at his middle, the tick after it lands | 5/5 (4306 [1476], 4309 [551], 4312 [2818], 4316 [855], [921]) |
| napalm spread | its tile; one streak or two opposed, 1..3 long in any of 8 ways; a step k is three tiles across (straight) or the corner and its sides (diagonal), delay 3k; stray neighbours; 3..17 tiles, a ragged tile on each | 32 patches |
| fire tile | 2 a tick (hitsplat 67) | 58/58 (the wiki's "6-7" is not on the wire) |
| Bork dead | the Smorks from death+11 every 2 ticks at everyone, endtime 90, the cycle going on (napalm in its slot, a shot for the bomb) | 4308 [749..769], 4312 [687..692], 4316 [1033..1043] |
| retarget line | only when the target changes | 31 + 6 |
| new target | not the nearest (17) nor the farthest (13): random | 32 |

From the live wiki (2026-10-21, not on our older copy): Bork has 12 flat armour
and 2 magic resistance, gone while dazed, when every melee attack on him is a
successful hit. Tested on the wire (§10): both hold; the armour fits 10.
The owner: the Smorks' cart blocks walking (`blockwalk=all`).

### Our scriptrun against it

Runs: `test/guthixian_archive/tfa_bork.lua` (`::tfabkstun`, `::tfabkmiss`,
natural misses) and `tfa_bork_smorks_first.lua`. Inventory:
`tools/rsprox_tells.py --anchor 16640,16641,16642 idx4303.txt idx4308.txt
idx4316.txt --ours <ticklog>`; facing from the tick log's `npc_face` rows.

| tell | recording | ours | verdict |
|---|---|---|---|
| miss branch | §9 | L 12249/12250 + patch; L+2 face tile + line, L+3 entity, L+5 stomp, L+11 end, drum L+21 | fixed |
| bomb a tile off | 12254 + 25..28 | 12254 at his middle, 28 | fixed |
| napalm on Bork | 12254 + 25..28 at land+1 | same | fixed |
| shots | per raider but his target | same (solo: none while he lives) | fixed |
| napalm clip | any of ten | same | fixed |
| patches | 3..17 tiles, ragged on each, gone at +200 with its rotation's despawn, a newer patch's tiles kept | same | fixed (the streak model is a fit) |
| slams | from the edge facing the target | same | fixed |
| Bork dead | +11, every 2 | same | fixed |
| Smorks' death | the line by who is alive, 12757 / 12761 size 15; 12251 size 15 | same | fixed |
| Smorks first | 4310's first 8 ticks | drum on to +9, then the line and the gong rage | fixed / unverified past +8 |
| slam dust delays 16..20 | 55 + 21 + 8 + 3 per window | none past 15 | rate: Bork stood mid-arena in our solo run |
| `invisible_type8_nonblocking` / despawns | per expiry | 1 expiry in the run | rate (run length) |
| `sound_-1` delay 82/117 | thralls' projectiles | | recorder's kit |
| 10051, waterstrike, thrall, glaive rows | the raiders' kit | | recorder's kit |
| music | `midi_song_v2` fades | plain | **open (engine)** |

Open: the bomb's fire hurting a raider on landing (no raider was on one); the
Smorks' damage after Bork (the wiki: "increased"; not measured); 4316 [969]'s
bomb beside him with no hit; the gong rage's rate, reach and damage (no recording).

## 10. Damage, every mechanic (2026-10-21)

The owner: "Only do flat armour if it is statistically identified by the damage
recorded in the encounter. Do damage analysis on all mechanics." Scratch
attributors over the nine `--all-players` transcripts (and the `--bas` ones for
weapons), splats inside the arena (4184..4198, 5591..5607) and Bork's window
only. Other players' prayers are not on the wire; a beat's band says whether
protect from melee was up.

**On Bork.** One raider's melee swing at t, the splats at t+1 on the npc they
face, no other swing at him that tick, no thrall within a tick:

| weapon | undazed (nonzero / zero) | dazed (nonzero / zero) |
|---|---|---|
| inquisitor's mace | 38 / 48, max 41 | 37 / 0, max 61 |
| soulreaper axe | 29 / 19, max 65 | 18 / 1, max 70 |
| abyssal bludgeon | 11 / 14, max 37 | 16 / 0, max 41 |
| scythe (first splat) | 17 / 10, max 43 | 12 / 0, max 56 |

- Dazed, melee never misses: 1 zero in 84 swings against 91 in 186. **Identified**;
  ours `~tfa_bk_dazed_hit` in the hit funnel (a missed melee roll on a dazed Bork
  rolls its damage).
- Flat armour (the live wiki, 2026-10-21: `flatarmour = 10 (Base), 0 (Stunned),
  2 (Magic resistance)`). First pass, every undazed swing: pooled best k=5, k=0
  and k=12 both 6 log-units off: not identified. That pass was flawed twice: it
  kept ticks where another raider's or a thrall's hit landed too (the mace's
  "dazed 61" was one), and the soulreaper axe (+6% a stack) and bludgeon (more
  as prayer drains) climb through a fight, so their undazed hits far from a
  stun came at a higher max. Clean pass: one swing, nothing else landing on him
  that tick, the weapon's own splat count, undazed hits within 20 ticks of a stun:

  | weapon | hands | dazed / undazed | best k | 2-unit band | LL(10)-LL(0) |
  |---|---|---|---|---|---|
  | inquisitor's mace | one | 36 / 18 | 10 | 5..12 | +7.2 |
  | scythe (3 kinds) | two | 19 / 8 | 0 | 0..11 | |
  | soulreaper axe | two | 17 / 9 | 0 | 0..26 | |
  | bludgeon | two | 18 / 3 | 0 | 0..16 | |
  | two-handed, pooled | | | 0 | 0..9 | -2.6 |

  10 on every weapon: 98:1 over none, within a log-unit of the best single k
  (8). 10 on one-handed only fits 13.6:1 better still, but rests on the mace
  alone, has no mechanism (the Flat armour page: every melee and ranged hit),
  and crush-only or 4-tick-only split the weapons the same way (the bludgeon,
  which would tell them apart, has 3 clean hits near a stun). A hit the armour
  swallows is a 0, not a 1: 3 ones in 79 undazed hits; a floor of 1 fits 8.6
  log-units worse. **Ours: 10 off every melee and ranged hit, none while dazed,
  never below 0** (`~tfa_bk_flat_armour`). The 2 against magic and the Smorks'
  2: no splats to test, not added. No other Bork session exists in the index
  (the 12 others in square (65,87) are other rooms; 4302 has only the Smorks'
  end). Worth recording: two-handed crush against one-handed slash or stab,
  inside and outside a stun.

**On the raiders.**

| mechanic | recorded | n | ours before | ours now |
|---|---|---|---|---|
| first beat (in contact or out) | 16..28; protect from melee 4..7 (a quarter, floored); the rate changes nothing | 408 | 14..32, 27% | 16..28, /4 |
| second beat | 20..33 (17 once); prayed 5..8 | 314 | same roll | 20..33, /4 |
| slam | 14..48, none in the prayed band | 27 | 14..47 | 14..48 |
| napalm landing | 25..28 | 8 | 23..28 | 25..28 |
| napalm on Bork | 25..28 | 11 | 25..28 | same |
| fire tile (67) | 2 | 24 | 2 | same |
| burn (74) | 1: 39, 2: 40, 3: 8 | 87 | 1..3 even | 1 and 2 even, 3 one in 11 |
| shot, Bork alive | blocked 70%, 1 23%, 11..30 7%; the same on raiders praying melee (20 / 5 / 4 of 29) and after a style switch | 243 | 0..32, 0..1 prayed | the mixture |
| shot, Bork dead | blocked 52%, 1 22%, 2..9 20%, 10..28 6% | 181 | as alive | the mixture |
| big bomb on a raider | no splat within 1 of a landing | 0 | none | none |
| rage beat (Smorks first) | no recording | | | the first beat's roll |

55 splats of 3..11 stay unattributed: raiders' ruby-bolt specs (zcb at 4312
[523], 4310 [454]), a thrall's shock (4308 [746]), runs across a beat tile
between the two tiles a tick shows.

## 11. Every block again (2026-10-21, third pass)

Asked for "no mechanic, timing, damage, animation, or anything that is not
accounted for in the recordings": the thirteen fights against
`tfa_bork.lua` and `tfa_bork_smorks_first.lua` on a private server and pack.

- `tools/rsprox/npc_stream.py --summary` for every npc of the room.
- `tools/bk_sigs.py`: every map anim (id, delays, heights), projectile (id,
  times, angle, progress, heights, source) and loc (id, shape) of the arena,
  recordings against ours.
- `tools/bk_sounds.py`: every area sound's id, delay, range, size, loops.
- `tools/bk_engage.py`: the engage's first ticks per fight.
- `tools/bk_beatcheck.py`: each beat's tiles and delays against the rule
  (720 recorded gongs: 0 delay misses; the only misses single corner tiles
  of the box this tool uses for the walls).
- `tools/bk_miss.py`, `tools/bk_stunspot.py`, `tools/bk_stunface.py`,
  `tools/bk_smface*.py` for the rows below.

| row | recording | ours before | ours now | verdict |
|---|---|---|---|---|
| spawn | (4194,5603) west (9/13) | (4192,5601), 4303's wandered re-add | (4194,5603) | fixed |
| wander | the whole arena, 11 from his spawn | `wanderrange=4` | 11 | fixed |
| engage faces | the Smorks reset at A (13/13); Bork faces the raider at A+1 (7/13; A 6) and is never reset; the Smorks face him at A+2 (7/12; A+1 5) | Bork reset at A, both faced at A+5 | as recorded | fixed |
| chase | one tile a tick from A+1 (5/13; A 8) | 2 tiles a tick (`npc_approach` from the room tick steps and queues), from A+2 | one tile from A+1 | fixed |
| fight song | 888 with zeros on its first send (13/13) | no zero send (the room fights from A+1) | zero send at A+1 | fixed |
| beat centre | his tile before the tick's step (every tile of 720 gongs) | after his step | before | fixed |
| miss window | the gong due at L or L+1 sounds, its second beat never (0/5); no gong until the resume | the second beat went out on the first slam | dropped | fixed |
| resume | the retarget + 4 (changed) or + 2 (kept), 34/34 stuns, 5/5 misses | stun L+34, miss L+21 (fixed counts, a tick late) | the rule, the gong on its tick | fixed |
| stun spot | every stun (8 hidden by a raider's slot-0 graphic) | skipped on the second stun | every stun | fixed |
| stun face | his target's tile (31/31) | a random raider's | his target's | fixed |
| the Smorks' death | D reset (4/4), D+7 fade 0..30 (4/4), D+9 the cart and the bar's npc deleted (4/4) | no reset, no fade, the bar's npc at teardown | as recorded | fixed |
| dying on a shot tick | no shot (4308 [771..772]) | shot | none | fixed |
| sounds, songs, locs, projectiles, map anims | | | every signature recorded matches | matched |
| projectile start height | 270 (v2, raw) | 268 (our v1 byte x 4) | same | **open (engine)**: our server sends the old projectile packet |
| the Smorks' aim faces | after the first entity face, a tile every 2 ticks near the raiders: 109 of 419 on one, most within 1..3, 238 on no raider's tile in the 11 ticks before; no rule fits (not lagged, not leading, not Bork) | their target's tile every 2 ticks from A+10 | same | **open**: rule not identified |
| Smorks first, past death + 8 | 4310 [622..630]: Bork drums on, D+7 the cart fades, D+9 it goes; Bork killed at D+8 | as recorded to D+9 | then "Bork kill human for that!" (a stream frame) and the gong rage | **not in the recordings**: the owner's description and the wiki; rate, reach and damage ours |
| defend sounds | no synth in any of the nine transcripts (all spectated) | null | null | **open**: no data |
| damage | §10 | | | as §10 (prayers not on the wire) |

