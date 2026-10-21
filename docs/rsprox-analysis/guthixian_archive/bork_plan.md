# Bork and the Smorks: spec from RSProx

Spectated: index 4303 [331..705] (two raiders, Bork killed [668], the Smorks
[684], line [691]) and [1876..2370] (five, Bork [2332], Smorks [2356..]);
the tails of index 4302 [85..105] and 4294 [~1491..]. Tools: `tools/boss_view.py`,
`tools/dustgrid.py` (a beat's tiles and delays relative to Bork's south-west
tile), `tools/room_inv.py`. Bork is 4x4, the Smorks' cart 3x3 (cache).

## 1. The room

| fact | evidence |
|---|---|
| Template square (65,87); raiders arrive west of the arena at (4174,5600) | 4303 [331] |
| Added with the room: `tfa_bork` (4192,5601) spawnangle south-west; `tfa_smork` ("The Smorks", the cart) (4190,5598) west; `tfa_bork_smork_hp_ui_handler` (4161,5569) south (the bar's npc, no ops) | 2/2 |
| The Archive transporter (4180,5599) rot 1: over the wall to (4187,5599), sprite 409 | 4303 [345], [347] |
| Bork wanders the arena until the fight starts | 4303 [332..345] |
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
- A shot: `npc_smork_cannon_attack01`, a projectile at the target from the cart: `vfx_smork_mage_attack_projanim01` or `vfx_smork_range_attack_projanim01` (alternating, both seen in every cycle), starttime 0, endtime 150 / 120 / 90 / 90 at R = 7 / 6 / 5 / 4, angle 20, progress 100, heights 270/100 (v2: ours 67/25). It lands at shot + (endtime+15)/30 on every raider (4303 [1905]: five hits from one shot): up to 28 unprayed, 0..1 under the matching protection.
- The napalm: a line ("Burny gloop!" / "Fireeeeeeeeee!" / "Hot stuff!") with sound 12748 / 12753 / 12749 (12747) r15 size 15 at the cart; +1 `npc_smork_cannon_attack01`, `vfx_smork_napalm_fire_projanim01` 0/120 angle 30 heights 270/0 at a raider's tile, `gargboss_debris_shadow_120` on it, sound 12240 r15 size 15. +4 it lands: sound 12239 r6 at the tile, `vfx_smork_napalm_fire_spotanim01` + a `vfx_smork_napalm_splat_spotanim01(_90/_180/_270)` on the tile (delay 0) and spreading to two rings of neighbours (delays 3 and 6), seven tiles in all; +5 seven `tfa_bork_ragged_tile` (random rotation) on them for 199 ticks (4303 [371..570], [458..657]), then `invisible_type8_nonblocking`.
- A raider on a ragged tile takes 2 a tick (hitsplat 67) and is set alight: a burn of 2..3 every 4 ticks, 5 times (hitsplat 74; 4303 [1916..1932], [2232..2248]).
- The big bomb: "Big bomb! Bork watch out!"; +2 `npc_smork_big_cannon_attack01`, `cannonball_travel_big` 15/150 angle 30 heights 270/0 at a raider's tile, `gargboss_debris_shadow_150` on it, sound 12248 delay 15 r15 size 15. +5 it lands: Bork under it (10/10) is stunned (§5).

## 4. The speed

R starts at 7 (Bork's drum and the Smorks alike). At 75%, 50% and 25% of the Smorks' health it drops to 6, 5, 4 (both fights: 450/299/150 of 600; 1003/668/350 of 1370), with the lines (Bork, Smorks): "Bork get into it!" / "Smorks go faster!", "Bork make fast drum!" / "Smorks feeling drum beat now!", "Bork maximum crush!" / "Smorks go fastest!".

## 5. Stunned, then angry

| tick | recorded | evidence |
|---|---|---|
| L (the bomb lands) | `npc_bork_14_cycle_stun01` (+ `vfx_bork_14_cycle_stun_spotanim01` from the second), "Oof."; the Smorks one of "That you fault!", "Bork! We sorry! No go!", "No! Fearless leader!"; sounds 12249 r20, 3201 (`myarm_flyback`) r20 size 20, 12273 r15, 12758 r15 size 15 at him | 10/10 |
| L..L+13 | no beats; every raider's hit on him lands (4303 [400..414]: 1426 -> 1182) | |
| L+14 | `npc_bork_enraged_stomp_attack_in01`, "Bork very angry!!" | 10/10 |
| L+16 | `npc_bork_enraged_stomp_attack01` (+ `vfx_bork_enraged_stomp_spotanim01`) | |
| L+17, L+20, L+23, L+26 | the slams: `tfa_wave_dust_short` over the whole arena, delay the Manhattan distance from his front (2..17), but the tiles the cart shadows from him | 4303 [417..426] |
| L+22 | `npc_bork_enraged_stomp_attack_cycle_and_out01` (+ the spotanim) | |
| L+29 | "Bork smash <name> now." (the new target) | |
| L+33 | the drum again | |

## 6. Deaths

- Bork at 0 first (both fights): `npc_bork_death01` + `vfx_bork_death_spotanim01`, "Smorks... avenge... Bork."; +7 deleted, the Smorks "Human die for that!" and from then shoot every raider each slot.
- The Smorks at 0: "Uh oh." and sound 12761 r15 at the cart; +2 `vfx_smork_death_explosion_spotanim01`, `vfx_smork_goblins_death_spotanim01` slot 1, `vfx_smork_tower_death_spotanim01` slot 2, sound 12251 r15; +7 "Challenge complete: Bork. ..."; +3 the navigator at (4184,5599) facing east.
- **The Smorks dying first** is in no recording. The wiki and every guide
  agree: "If the Smorks are killed first, Bork's stomps will cover the entire
  arena" ([wiki strategies](https://oldschool.runescape.wiki/w/The_Fractured_Archive/Strategies),
  [runescapeguides](https://www.runescapeguides.com/the-fractured-archive-beginners-guide-osrs/),
  [bald.gg](https://bald.gg/blog/osrs-raids-4-fractured-archive-guide)). Ours,
  *unverified*: the cart's death as recorded ("Uh oh.", +2 the spotanims), at
  +9 the cart and its blockrange go, and Bork goes into his anger (§5's
  stomp-in, stomp, four slams, a new target) over and over until he dies —
  with no cart there is no shadow, so every slam covers the whole room. His
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
