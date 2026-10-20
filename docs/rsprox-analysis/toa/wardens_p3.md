# The Wardens, phases 3 and 4

Read from 18 kills: 14 (solo, Entry), 101 (4 solos, 0-700), 553 (3), 1779,
1845 and 1852 (7 Expert raids), regions 59,80 (the transition) and 61,80.
X is phase two's end (the killing hit on the core); P is the party's arrival
in the next square (X+15); C is a charge. Full inventory:
[wardens_p3.out](wardens_p3.out). Phases 1–2: [wardens_p1p2.md](wardens_p1p2.md).

**Ours:** `minigame_toa/scripts/toa_wardens.rs2` (`~toa_wd_transition`,
`~toa_wardens_p3_tick`, `~toa_wd4_*` for phase four), constants `^toa_wd_x_*` and `^toa_wd3_*` in
`toa.constant`, enums `toa_wd_slam_*`, `toa_wd_siphons_*` and `toa_wd4_rip_mask` in `toa.enum`.
Compared below (`test/raids/toa_wardens_p3_tells.lua`).

Tumeken's Warden is the phase-3 Warden in the 18 kills above: Elidinis'
survives phase one and restores Tumeken's. RSProx index 4292 (rev 241, solo,
raid level 160, "Tombs Elidinis And Failures") is the other way round, with
Akkha's and Kephri's phantoms: [Elidinis' Warden](#elidinis-warden-rsprox-4292)
below. Ticks there are from the visit's start, [3362] = +0.

## Phase 2 to 3 (18 of 18 alike)

| Tick | Recorded |
|---|---|
| X | the dying Warden's `npc_wardens_death01` (9662), the core gone, `invisible_type8_nonblocking` on its tile, the line, synths 6228 d125 / 6109 d249 / 6196 d254 (phase 2); the fallen Warden (`_phase3_inactive`) gets spotanim `spotanim_wardens01_wakeup02` (2218) and seq `npc_wardens_wake02` (9664), both delay 120; `vfx_scarab_explosion03` (2158) delay 30 height 50 on the core's tile |
| X+6 | the fallen Warden takes one `hitsplat_heal` (6) of phase three's health, headbar 21 full (880 solo Entry; 1410 / 1940 / 2640 / 3340 solos; 5220, 7690, 10840, 12470 parties); ~22 scarab explosions (2156–2159) in local (26..36, 28..41), delays 30..210 by 30; `toa_wardens_4_square_thunder_01` (6266) range 15 at (32,34) at each delay; synth `selfheal` (167); `cam_shake` on axes 0, 1 and 2, random 2–5 |
| X+9 | the obelisk's `fx_obelisk_wardens02_death` (9734); 6266 d30/60/90; synth `toa_wardens_obelisk_destroyed_01` (6195) |
| X+9..12 | about the obelisk, (30..34, 32..36): 1–2 `wardens_lightning_warn` (2198) and 1–2 explosions a tick (14: 7 and 5) |
| X+12 | fade out (`fade_overlay`, 0→255 over 15), `cam_reset`; the next square's Warden `toa_warden_tumeken_phase3` (11762) at (30,32) and `toa_amascut_p3` (11767) at (28,7) are added, both facing (32,41) |
| X+13 | the Warden's idle `spotanim_wardens01_idle05` (2219), every 3 from here |
| X+15 = P | every raider on (31..33, 41..42), fade in (255→0 over 50); `invisible_type8_blocking` (26209) on the Warden's 5x5; no line |
| P+3 | the raid HUD back |
| P+6 | the first slam |

## Phase 3

| | Recorded |
|---|---|
| Health | the X+6 pool, `healthbar_21` |
| Slams | east `npc_wardens_attackleft01` (9674), west `attackright01` (9676), both `attackcenter01` (9678), in that order; the first P+6; every **7** from 50% health up and **6** below (decided at each slam; 14: 181→188 at 51%, 188→194 at 48%); Overclocked 5, Overclocked 2 4 and Insanity 3 with the `02` slams (9675/9677/9679). Each kind's tiles and delays are fixed (east and west 78, both 138; the `02` tiles 30 cycles sooner); every tile a random `spotanim_wardens01_undertile01_explode01..04` (2220–2223); no sound |
| Slam damage | [fitted] half to all of 18 x the raid factor, uncapped, on a raider whose tile's explosion lands that tick (13–16 at 0, 33 at 310, 42–52 at 500, 63 at 700) |
| Charge | under 80 / 60 / 40 / 20%, on the tick before the next slam: `npc_wardens_charge01` (9682), retype `_phase3_charging` (11764), `lunar_humidify` (3614) range 14 at (32,34) |
| Skulls | C+2: `fx_wardens_bomb03` (2226) from (32,36) to each siphon tile (start 30, end 120, angle 20, height 87 → 20) with `gargboss_debris_shadow_120` (1447); 4 / 5 / 6 / 7 tiles solo, 9 / 10 / 13 / 15 in a party, the same tiles every kill |
| Siphons | C+6: `wardens_p3_orb_blue` (11772) on each, facing north, `invisible_type8_blocking` under it, `toa_wardens_blue_orb_landing_01` (6159) range 6; 1 hitpoint solo at Entry |
| Beams | from C+7 every 2: `fx_wardens_charging01` (2227) from (32,35) to every siphon, blue or red (start 0, end 90, slope 70, height 100 → 5), `toa_wardens_phase3_red_projectiles_01` (6230) range 7 at each |
| Reversal | a siphon emptied (melee) keeps its empty bar and turns `wardens_p3_orb_red` (11773) the tick after, facing south |
| All reversed | R, the tick after the last: the siphons gone, `invisible_type8_nonblocking` on their tiles (22 of 22), `fx_wardens_bomb03` from each back to (32,36) (start 0, end 58, height 20 → 87), synth `toa_wardens_skulls_travel_01` (6087). R+1: `npc_wardens_stagger01` (9680), the form back, a `vfx_scarab_explosion01..04` (2156–2159, any: 17 / 15 / 14 / 20) d21 height 250 at (32,36), synths 6183 d20, 6038 d29, 6110 d43, 6243 d43, 6215 d90, 6115 d158. R+2: the Warden takes `hitsplat_damage_other` (17), the same every stagger of a kill |
| Not in time | landing+18 (Insanity +8): `npc_wardens_release01` (9681), the siphons gone, synth `toa_wardens_tiles_central_ripple_01` (6179) d30; the floor (lx 22..42 at lz 43..45, one tile in a side a row down to lz 37) less the siphons' tiles erupts from (32,41), 30 + 6 cycles a ring (101, 553); 30 a siphon left, scaled, to a raider it reaches (553: 71 at 310); the form back +5 |
| After | the slams again 9 after a stagger or a release, from the east (but under Insanity) |
| Zebak's phantom | `toa_wardens_zebak` (11774) with batch 2 at (39,33), Ba-Ba's (11775) with batch 3 at (21,33), C+2, facing (32,41), synth 6186 d15 each; neither attackable. Each attacks every **8** from 8 after it comes (a few gaps of 6–7 or 10–13 unexplained) |
| Zebak's attack | `npc_zebak01_attack_ranged_enraged` (9626); its split shot from (39,36) to (37,39) (start 30, end 120, angle 20, height 50 → 125), magic 2176 or ranged 2178 (`_enraged` 2177 / 2179 in the raids whose Zebak path is 2+), sounds 5823 + 5878 d120 or 5819 + 5896 d120; A+4 the carrier `spotanim_zebak_ranged01_npc` (11744) at (34,36) with `zebak_mage_split` / `zebak_ranged_split` height 535 and a fragment to every raider (`zebak_mage_projanim_split`, or `zebak_ranged_fragment01..04` by raider: solo 01, 1845's four 01..04 alike) with the Zebak room's impact, sound and hit. Runs of one style, magic first |
| Ba-Ba's attack | `npc_baba_attack_melee` (9743); `toa_baba_rock_fall` (by her path) delay 20 on every raider's tile, synths 5949 and 6014; her room's rockfall on a raider still there |
| Stagger hit | 880 → 50, 1410 → 70, 1940 → 100, 1970 → 100, 2640 → 140, 3340 → 160, 5220 → 260, 7690 → 380, 10840 → 540, 12470 → 620 |
| Hits while charging | land (1852: 7–33 on 11764 at C+6..C+24); players are on the siphons |
| 5% | phase four (the enrage), on the tick of the hit that crosses it (14: 28 to fill 5 and the enrage on 391; 101 #1: the stagger's own hit): see below |

**Fitted / unrecorded.** The stagger hit is a twentieth of the pool to the
nearest 10, at least 50; it misses 101's solos at 500 (140, ours 130) and 700
(160, ours 170). The slam damage is fitted. A raider on a siphon's landing
tile is pushed one tile north, unhurt ([wiki]; never recorded). Melee alone
damages a siphon ([wiki]); a red one takes nothing.

## Ours against it

`test/raids/toa_wardens_p3_tells.lua`: solo, Entry, as the phase-two test,
then the transition, the Warden shot with a twisted bow and the siphons hit
with a dragon scimitar to the enrage. Our driver is slow to the siphons, so
the run has one stagger and three releases (14 had four staggers): both ends
are exercised. `rsprox_tells.py --anchor 11762-11764,11772-11773 --gap 30
14.txt --ours`:

- The transition to the tick: wake and explosion on X, heal of 880 and 21
  explosions with their thunder on X+6, the obelisk's death on X+9, the
  lightning to X+12, the Warden and Amascut on X+12, the idle from X+13, the
  party in on X+15, the first slam on X+21 and the next 7 later.
- Per charge, to the tick: C, skulls and shadows C+2, siphons, their blocking
  and sounds C+6, beams from C+7 every 2; reversal to red the tick after;
  stagger and its seven synths and the hit; the release and its ripple.
- Every phase-three tell kind is sent: 47 `invisible_type8_blocking` and 22
  `invisible_type8_nonblocking` (14: 47 and 22), 22 siphons and 22 skulls
  with shadows, both phantoms with their attacks, projectiles, carriers,
  impacts and sounds.

Not in our tick log (so unchecked here): `cam_shake`, `cam_reset`, the fade
and HUD interfaces.

## Elidinis' Warden (RSProx 4292)

One kill, solo, raid 160 (`toa_client_raid_level`), with Aerial Assault (her
room's fireballs were 3x3: 216 shadows for 24). "As Elidinis' Warden falls,
Tumeken's Warden powers up!" (`@mes_hl_ora@`) ends phase one; phase two is
Tumeken's (ranged first, every 8, as the wiki said); "Tumeken's Warden uses
the last of its power to restore Elidinis' Warden!" (`@mes_hl_blu@`, Elidinis'
colour) ends it. Phases three and four are Tumeken's in every respect but the
ids (`toa_warden_elidinis_phase3` 11761, `_charging` 11763, the death npcs
`toa_warden_p3_death_elidinis` 11766) and the phantoms:

| | Recorded |
|---|---|
| Charges | +75, +109, +156, +211; staggers +87, +123, +173, +229; E +280 (`enraging01`), D +398 |
| Akkha's phantom | `toa_wardens_akkha` (11777) at (39,33), the Zebak place, C+2 of the second charge (+111) with `toa_wardens_red_thunder_11` d15, facing (32,41) |
| Kephri's phantom | `toa_wardens_kephri` (11776) at (21,33), Ba-Ba's place, C+2 of the third (+158), the same |
| Cadence | each from 8 after it comes, every 8 (Akkha 119, 127 ... 274), every 5 from its attack after E (282, 287, 292 ...); Kephri's 166 ... 274, 282, 287 ... (one gap of 11, 151 -> 162, over the third charge, unexplained) |
| Akkha's cycle | `npc_akkha_attack_ranged_spear` (9772) x3, `npc_akkha_attack_magic_02_spear` (9777) with no throw, `npc_akkha_attack_magic_spear` (9774) x3, 9777, again; ranged first (44 of 44 attacks in that order) |
| Akkha's throw | A+2: `akkha_ranged_travel` (2255) or `akkha_magic_travel` (2253) from his middle (41,35) at the raider, start 0, end 6 (ranged) / 10 (magic) x the distance (66 at 11, 100 at 10, 140 at 14), angle 12, slope 32, height 101 -> 34; synth `toa_akka_non_magic_knife_projectile_01` (5640) / `toa_akka_projectle_impact_01` (5774) at the end; magic `akkha_magic_impact` (2254) h124 at the end; the raider's block at the end. His room's throw exactly |
| Akkha's hit | (end + 15) / 30 after the throw (+123 for 121's 66, +156 for 153's 100); 0 through the right prayer (12 of 12), 13 ranged unprotected |
| Kephri's cast | `npc_kephri_attack_magic_radial` (9577), `vorkath_area_travel` (1481) from her middle (23,35) to (24,36) (38 of 38: start 39, end 90, angle 50, 175 -> 250), synth `toa_kephri_fire_projectile` (6435) |
| Kephri's volley | A+3: `vorkath_area_travel_small` (2266) from (24,36) to the raider's tile (where they stood at the end of A+2), start 0, end 120, angle 115, 250 -> 9; `gargboss_debris_shadow_120` on the 3x3 round it; `toa_zebak_redirected_projectile_02` (5896) r12 at (23,35). Her room's, at her path's flight and with Aerial Assault's 3x3 |
| Kephri's landing | A+7: one `vfx_scarab_explosion` variant on all nine tiles (2158 x9, then 2156 x9, ...), `toa_kephri_fire_proj_impact_01` (6475) r6 on the centre; the raider was never in one, so its hit is her room's [unrecorded] |
| D | as Tumeken's (the five death npcs, the book on (32,37), 6143 / 6044 d330, `cam_reset`, the loot clientscript 7192 naming 11761) |
| D+2 | both phantoms' ops off (`enabled_ops 0b00000`), Akkha's `npc_akkha_death_spear` (9792), an area -1 r10 on each (Kephri's has no death seq) |
| D+4 | both phantoms gone (Tumeken's went at D+2 in the rev 223-227 kills) |

### Pushed off (4292 +286, +340; 1852 [28848], [28900])

A raider on a tile as it flies away (P4), or on a siphon's tile as it lands
(P3, 1852), is pushed off, on that tick:

- `message_game` spam "You are pushed off.";
- `busy` 1 that tick, 0 the next;
- a 1-tile `teleport` to the south-west, (40,65) -> (39,64) and (36,62) ->
  (35,61) in 4292, (30,64) -> (29,63) and (35,64) -> (34,63) in 1852: 4 of 4
  (-1,-1), so ours takes the first open tile from the south-west (the rubble's
  rule, which went (+1,-1) off the east side of a rubble);
- `agilityarena_player_spikedback` (1114), an `exactmove` to1 = the tile they
  stood on, delay1 0, to2 the new tile, delay2 29; angle 1024 / 1023 (4292;
  1852 255 / 1536);
- in P4 a hit: 18 and 19 at raid 160 (fitted: 11 x the damage factor +
  random(2), the pyramids' rule); a siphon's push hurts nothing.

Both 4292 pushes were on a row's first tick (the middle tile always goes
first): (40,65) was in the first tick's batch and (36,62) was the middle. Ours
pushes the raider whose own tile goes, on its tick. A raider who walks back
onto a row already gone is pushed the same way [nr].

### Ours against it

`test/raids/toa_wardens_elidinis_tells.lua`: solo, raid level 0, Elidinis'
gap blocked in phase one, so Tumeken's Warden survives; Tumeken's phase two;
Elidinis' restored; the siphons to the enrage; at the enrage the raider stands
on (32,45) and is pushed off by the first row; then the Warden shot to death.
`::toawardens 7` / `8` puts a raider straight into Elidinis' phase three / the
enrage.

## Phase 4: the enrage (E) and the death (D)

Every kill alike (18 of 18) unless said.

| | Recorded |
|---|---|
| E | `npc_wardens_enraging01` (9684); a `hitsplat_heal` of a fifth of the pool on what is left (14: 27 + 176 of 880; 101 #1: 0 + 176); `selfheal` (167) range 15 at (32,34); Amascut's phantom `npc_amascut_giant01_enrage01` (9718) and `spotanim_amascut01_sucking01_enraged01` (2231), both delay 1. The slams and siphons stop; slams in the air still land. [wiki] its defence back to 180 |
| The Warden | `npc_wardens_enraged01` (9685) every **2** from E+2 (188 of 188 gaps); its idle 2219 every 3 as before |
| Amascut | E+9 `spotanim_amascut01_sucking01` (2229) and retype `toa_amascut_p3_enrage` (11768); 2229 on every odd tick after, but 2 before each row from the second `_flash01` (2230) and the odd tick after skipped (22F 25, 40F 43 ...) |
| The floor | a row every **18** from E+6, the back (lz 45) first (18 of 18, every invocation set seen): over four ticks (the middle tile, lx 32, always first; the rest any of the four), each tile a broken-tile ground decor and the tile behind it another, and `spotanim_wardens_tiles01` (2228) from it to (32,10) (start 0, end 90, angle 20, height 0 → 200). The decor is fixed: the first row `_02b` rot 1 (ends `_03b` / `_03b_mirror`); behind it on the second `_01b` rot 1 (ends `_04b` / `_04b_mirror`); every later row its ends `_04c` rot 0 / 3, the rest `_01` rot 3 or `_01c` rot 0 by tile (the same tiles every kill: `toa_wd4_rip_mask`), behind it `_05` rot 0 (the second row's ends `_01` rot 0 / 2). The row's tick: `toa_wardens_enraged_tile_swoosh_01..04` (6247, 6153 d30, 6124 d60, 6219 d90) range 9 at (32, row); 2 before, `contact_rumble_long` (1465) and `toa_wardens_enraged_strong_wind1_01` (6160); 4 after `_wind2_01` (6216); 10 after `_wind3_01` (6107). Five rows at most were recorded (lz 45..41) |
| Lightning | from E+5 in sixes: three ticks of `gargboss_debris_shadow_90` (1446), then three of `wardens_lightning` (2197) on the same tiles, tick for tick (counts equal in every six), with `toa_wardens_enrage_lightning_01` (6116) each strike tick. The first tick of each of the first three sixes is exactly 9 tiles (39 of 40); after, 13–23, fewer as the floor goes. Each later tick's tiles are a step of 1 (777) or 2 (264) from the tick before's, and fewer (≈ 0.85 a tick) |
| Lightning damage | a raider on a struck tile that tick: 7–16 solo at 0, 20–28 in 1852, 27–42 at 500 |
| Phantoms | every **5** from their attack after E (14: 8 then 5 5 5; 101, 1852) |
| D | the tick after the hit that empties the Warden (14: 2 to fill 0 on 481, D 482): the Warden gone; five `toa_warden_p3_death_tumeken` (11765) from its tile south, 5 apart ((30,32) .. (30,12)), playing `spotanim_wardens01_death02_offset01..05` (9691–9695), facing (32,39); the phantoms' ops off; `cam_reset`; synths `toa_wardens_full_wind1_01` (6143) and `_wind2_01` (6044) d330; the book `toa_book_wardens` on (32,37); "Challenge complete: The Wardens…" and the raid's total, the vault's song (731) |
| After | D+2 the phantoms gone; D+13 `toa_teleport_crystal_continue_wardens` (45138) on (32,34) (to the vault: fade out, D+3 in the vault); D+22 Amascut's `npc_amascut_giant01_exit01` (9719) with `spotanim_amascut01_sucking01_glow01` (2232) d30 and synth `toa_wardens_amascut_despawn_03` (6051) d30, the five death npcs gone; D+25 Amascut gone (D+22 / D+25 in 18 of 18, whenever the party left) |

**Fitted / unrecorded.** The rows' tick order and the lightning walk are rolls
fitted to the counts (22 later starts on the old floor, one lost on a ripped
row; one step in four of two tiles; a bolt off the floor or onto another's
tile ends). Lightning damage is a third to all of 18 x the raid factor
[fitted]. Rows lz 40..38 never broke in a recording: they take lz 41's tile
pattern. A raider on a tile as it goes is pushed off it (4292, above). [wiki] Insanity speeds the phantoms in the
enrage by an unstated amount: ours keeps 5. The clientscripts sent at D (7191,
5931, 7192: the kill's loot notice) and the vault's Osmumten being added at D
(he comes with the vault, on arrival) are not sent.

### Ours against it

`test/raids/toa_wardens_p4_tells.lua` (phases 1–3 as the phase-three test,
then the enraged Warden shot to death, 30 ticks on). `rsprox_tells.py
--anchor 11762-11768,11772-11775 --gap 30 14.txt --ours`: every phase-four
kind is sent and none extra: the enrage tick, 9685 every 2, Amascut's retype,
2229 cadence and flashes, every broken-tile decor and rotation, 2228 per tile,
the four swooshes and three winds a row, shadows and strikes, the death npcs
and their five seqs, 6143 / 6044, the book, the crystal on D+13, Amascut's
exit, glow and despawn sound on D+22, gone D+25. The per-tick timeline matches
14's (E, E+4 rumble, E+6 first row, E+9 Amascut, E+10 / E+16 winds, E+22 flash,
E+24 second row).
