# Xarth: spec from RSProx

Spectated: index 4301 [617..847] (three raiders) and 4302 [500..752] (five).
The wiki (`sources/Xarth.wiki`, `sources/TFA_Strategies.wiki`) has the stats
and the shape of the fight. He is 3x3 and never moves; tiles are relative to
his south-west tile (4190,5534), local (30,30) of square (65,86). Tools:
`tools/xarth_view.py` (a tick's map anims as a grid, every loc, one npc type's
lives), `tools/room_inv.py`.

## 1. The room

| fact | evidence |
|---|---|
| Raiders arrive west of the arena at (4172,5535) and cross by the Archive transporter at (4178,5535) rot 1: six tiles east of it, to (4184,5535), whatever row they stood on, sprite 409 | 4301 [636], [638]; 4302 [535..539] (8/8) |
| Added with the room: `tfa_xarth` at (0,0) spawnangle west, `[facing] coord=(4173,5535)`; three pillars, each a `tfa_xarth_rubble_npc` (2x2, no ops) on its own loc: `tfa_xarth_rubble_2` (6,2), `tfa_xarth_rubble_1` (3,6) and (3,-4); `invisible_type8_blocking` on his nine tiles | 2/2 |
| Engaged by the first raider over the wall: music 896 and the bar (`hpbar_hud_npc` = him, 2600 at 1-2 raiders, 3770 at three) on that tick; his first attack 6 ticks later | 4301 [636] -> [642]; 4302 [535] -> [541] |

## 2. Phase 1: the pillars

A cycle of 31 ticks from C (the first C = the engage + 6), three times (2/2,
3 cycles each):

| tick | recorded |
|---|---|
| C, +4, +8, +12, +16 | `xarth_attack_magic_01`, facing the target's tile; `luc2_undead_demon_fireball_proj_big` at them, 30/90 angle 0 progress 160 heights 470/120 (ours 117/30); +3 `luc2_undead_demon_fireball_impact` slot 2 height 124 on them, the hit, and a burn (§5) |
| C+20 | `xarth_attack_aoe_01`; every raider `vfx_xarth_aoe_stunanim_01` and bound for 3 ticks (they walk again at C+23); at each raider's tile and one random neighbour of it (deduplicated: 4, 3 tiles at three stacked raiders, 4, 7, 10 at five): `vfx_xarth_aoe_projanim_01` from his middle 60/120 angle 30 progress 128 heights 470/0 (ours 117/0), `gargboss_debris_shadow_90` delay 30, `vfx_xarth_aoe_hitanim_01` delay 120; sounds 9221 d33, 9335 d3, 9215 d36, r15 at his middle |
| C+23 | the flame blast: `xarth_attack_flame_blast_01` delay 30 and `vfx_xarth_attack_flame_blast_01` slot 5 delay 30; sounds 9224 d30, 9210 d3, 9215 d36, 9259 d39, r15 at his middle |
| C+24 | the AoE lands: 9240 r4 on each of its tiles |
| C+26 | the pillar the raiders hid behind falls (4301: south (3,-4), east (6,2), north (3,6); 4302 the same): its `tfa_xarth_rubble_npc` is deleted and a `tfa_xarth_rubble_npc_destroyed` added there with `loc_xarth_rubble_collapse_01` and `vfx_xarth_rubble_collapse_01`; sound 10579 r15 size 15 at his middle; its loc becomes `invisible_type8_nonblocking`, then `invisible_type8_blockrange_size2` |
| C+29 | the destroyed npc deleted, the loc `invisible_type8_nonblocking` |

The wiki: the blast hits every raider not hidden behind a pillar, hard, with a
burn. No recording shows who took it (the spectated raiders all hid).

## 3. Phase 2 (the third pillar's fall, D)

| tick | recorded |
|---|---|
| D | the arena catches fire: `vfx_vmq4_ennius_fire_explosion` on every open tile within 7 of his 3x3, delay = the ring (1..7); eight `tfa_xarth_arenafire` on random tiles of it; the eight corpse piles `tfa_xarth01_corpse_pile_fire01..08` at (1,12), (10,2), (1,-9), (-10,2), (-10,9), (10,9), (10,-11), (-11,-10) (2/2) |
| D+7, D+11 | ranged (below) |
| M = D+15, then every 21 | n fireballs 2 ticks apart (two at three raiders, three at five): `xarth_attack_magic_01`, `luc2_undead_demon_fireball_proj_big` at a tile 8 from his middle, 30/90 heights 470/50 (ours 117/12); +3 a corpse rises there |
| the last fireball +3, then every 4, four times | ranged: `xarth_attack_ranged_01` + `vfx_xarth_attack_ranged_spotanim_01` slot 2; `vfx_xarth_attack_ranged_projectile_01` at a raider, 60/120 angle 10 progress 200 heights 470/120 (ours 117/30); +4 `vfx_xarth_attack_ranged_hitanim_01` slot 2 on them and the hit |
| M+12 | three more `tfa_xarth_arenafire` on random tiles (4301 [758, 778, 799, 820]; 4302 [659, 681, 705, 728]) |

The fires stay till the room ends. The wiki: a raider who walks on one is set
alight.

Melee: a target beside him is struck in whichever attack slot comes next
(a phase-1 fireball's: 4304 [2678], [2937]; a phase-2 arrow's: 4308 [1373],
[1382]): `xarth_attack_melee_01` with `luc2_undead_demon_melee_spot` slot 2,
facing them.

Checked on 4304 [2640..3107] (two raiders) and 4308 [1140..1431] (four,
B0aty's): the 31-tick pillar cycle, the 21-tick volley. The fireballs (and so
the corpses) a volley follow the raiders still standing: two at two, three
and four, three at five, one once 4304's duo lost a raider ([3051]; its
volleys at [3088], [3107] one each): max(min(n, 2), (n + 1) / 2).

## 4. The corpses

`tfa_xarth_corpse` "Corpse", 62 hp. Added on its fireball's tile (8 from his
middle) with `vfx_xarth_corpse_walk_fire_01` slot 2 (looping), facing his
middle, fading in (`transparency` 0..30, 255 -> 0); from +1 it walks to him a
tile a tick (13 lives) until frozen. Dead: `xarth_corpse_death_01`,
`vfx_xarth_corpse_death_01` and `vfx_xarth_corpse_walk_doused_01` slot 2;
deleted at +4 (13/13). None reached him in either fight: the wiki says one
that does heals him for its health, and 200 healed sets off a blast over the
whole arena (not built: nothing recorded to copy).

## 5. Burns

Hitsplat 74, 1..5 every 4 ticks for as long as a raider keeps being set
alight (4301: Fatalsystem [646..682], from the first fireball). Ours: five hits
of 1..3, 4 ticks apart, restarted by each new burn (unverified length).

## 6. Death

| tick | recorded |
|---|---|
| K | `xarth_death_01`, `vfx_xarth_death_01` (4301 [838]) |
| K+6 | "Challenge complete: Xarth. ..."; he and every corpse deleted; his nine tiles `invisible_type8_nonblocking` (4301 [844]) |
| K+9 | the navigator at (4183,5535) facing east (4301 [847]; 4302 [752]) |

## Audit: our scriptrun against 4301, 4302 (2026-10-21)

Run: `test/guthixian_archive/tfa_xarth.lua` (solo; `::tfaroom 5 3`, the
transporter, phase 1 under protect from magic, phase 2 under protect from
missiles, `::tfaxacorpses` once so a corpse dies as a frozen one does,
`::tfaboss 3`). Inventory: `tools/rsprox_tells.py --anchor 16684,16687,16685
idx4301.txt idx4302.txt --ours <ticklog>` (counts per window).

| tell | recording | ours | verdict |
|---|---|---|---|
| room | Xarth (0,0) west, his 9 blocking tiles, 3 pillars (npc + loc) | same, [90] | matched |
| entry | transporter six across to (4184,5535); song 896 and the bar on that tick; first fireball +6 | [103] teleport, [109] first fireball | matched |
| phase 1 | fireballs C..C+16 (30/90 a0 p160 117/30, impact slot 2 h124 at +3), AoE C+20 (stun, tiles + a neighbour each, 60/120 a30 p128 117/0, shadow d30, hitanim d120, 9221/9335/9215), blast C+23 (seq + spot slot 5 d30, 9224/9210/9215/9259), 9240 r4 at C+24, fall C+26 (10579 r15 s15, nonblocking then blockrange_size2), gone C+29, cycle 31 | [109..138], [140..169], [171..200] | matched |
| wave | `vfx_vmq4_ennius_fire_explosion` delay = ring 1..7 on every open tile, 8 fires, 8 corpse piles at their tiles | same, [197] | matched (the recording's two `_smaller` explosions not built) |
| phase 2 | arrows D+7, D+11; volley at D+15 every 21: fireballs to a tile 8 out (117/12), corpse +3; four arrows (60/120 a10 p200 117/30, spot slot 2, hitanim slot 2 at +4) from the last fireball +3; three fires at M+12 | [204..269] | matched (solo: one fireball a volley) |
| corpses | add on the fireball's tile, walk-fire slot 2 **looping**, face him, fade in 0..30; walk a tile a tick; death seq + spot + doused slot 2, gone +4 | same but the loop flag | **open (engine: the 241 spotanim loop byte is always 0)** |
| death | K seq + spot; K+6 line, him and the corpses gone, 9 tiles nonblocking; K+9 navigator (4183,5535) east | [274], [280], [283] | matched |
| music | `midi_song_v2` with fades | plain `midi_song` | **open (engine)** |
| damage | not readable from these transcripts (hits over 99 on one raider) | fireball 0..30 + burn, arrow 0..30, AoE 20..40, blast 30..60 + burn | approximate |
| corpse heals him / the 200-heal blast | the wiki only | a corpse reaching him heals its health; no blast | unverified / not built |
| party scaling | fireballs a volley by the raiders standing: 1 alone, 2 at two to four, 3 at five | max(min(n, 2), (n + 1) / 2), n living | matched (4301, 4302, 4304, 4308; was (n + 1) / 2, one short for a duo) |
| melee | `xarth_attack_melee_01` + `luc2_undead_demon_melee_spot` slot 2 on a target beside him | same | matched (not exercised by the bow test) |
