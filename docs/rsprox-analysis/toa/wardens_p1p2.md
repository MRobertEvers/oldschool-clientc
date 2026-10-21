# The Wardens, phases 1 and 2

Read from 18 kills: 14 (solo, Entry), 101 (4 solos, 0-700), 553 (3), 1779,
1845 and 1852 (7 Expert raids), region 59,80.
Ticks are relative to entry (E) or the event named. Full inventory:
[wardens_p1p2.out](wardens_p1p2.out). Phase 3 is [wardens_p3.md](wardens_p3.md).

**Ours:** `minigame_toa/scripts/toa_wardens.rs2`. Both phases compared
(`test/raids/toa_wardens_p1_tells.lua`, `toa_wardens_p2_tells.lua`, below).

In every recorded kill Elidinis' Warden is the one fought in phase 2
("<col=3366ff>As Tumeken's Warden falls, Elidinis' Warden powers up!</col>",
8 of 8); the npc ids below are Elidinis'. Tumeken's have the same shape
(`toa_warden_tumeken_phase2_mage` 11756, `_range` 11757, `_exposed` 11758).

## At entry (E+0)

| Npc | Id | Tile |
|---|---|---|
| Elidinis' Warden (statue) | `toa_warden_elidinis_phase1_inactive` (11746) | (3792,5152) |
| Tumeken's Warden (statue) | `toa_warden_tumeken_phase1_inactive` (11747) | (3820,5152) |
| Obelisk | `toa_wardens_p1_obelisk_npc_inactive` (11750) | (3807,5153) |

"Challenge started: The Wardens." on E+10/11, and on that tick the three
retype: `toa_warden_elidinis_phase1` (11748), `toa_warden_tumeken_phase1`
(11749), `toa_wardens_p1_obelisk_npc` (11751; with `ghost_update_normal_despawn`, 5546).

## Phase 1: the obelisk

Region 59,80 is phases 1 **and** 2: the room does not change when the obelisk
breaks. S is the challenge's start (14: Osmumten's Begin 3585, S 3586).

| | Recorded |
|---|---|
| E | the three statues (table above) facing (3808,5154); `toa_wardens_default_charging_platform_0` (45606) shape 10 at (3792,5152) rot 3 and (3820,5152) rot 1; `invisible_type8_blocking_size3` (42838) on the obelisk |
| S | the three retype; Osmumten `ghost_update_normal_despawn` (5546), gone S+2; `toa_wardens_obelisk_ignite_01` (6081) range 15 at (3808,5154) |
| Orbs | from S+2, every 2 ticks, one a side: `toa_wardens_energy` (11769) added at (3805,5154) / (3811,5154) with `zuk_proj` (7571), an exactmove one tile on (d2 30, angle 512 west / 1536 east) and a face 9 tiles on. Then a tile a tick, teleport + exactmove from the tile behind + `zuk_proj`, 8 steps down one of three troughs: north by (3804,5155) to z 5156, middle along 5154, south by (3804,5153) to 5152, to x 3797 (east mirrored, to 3819). On step 9 `fx_wardens_energy_death01` (9735), deleted the tick after |
| Troughs | each side runs the three in an order of its own (one of the six; differs raid to raid and side to side) and moves on only when an orb goes |
| Obelisk | `fx_obelisk_wardens01_cast` (9721) on every spawn tick (224 of 224 gaps); a hit sounds `toa_wardens_obelisk_hit_01` (6595) range 9 at (3808,5154) (14: 9 of 9; 1852: 372); headbar 20 (`healthbar_standard_120`); 260 at solo Entry (14: hits summed to 260) |
| Blocking | a raider on the gap tile (3806/3810,5154) or the orb's first tile (3805/3811,5154) takes 3 on the spawn tick and that side sends nothing (14: 11 spawns on (3810,5154), none east from 3588 to 3612; 553, 101, 1779). No absorb was recorded anywhere else on a trough |
| Charge | the Warden's headbar 36 (100 wide), every tick from S+4. In units: 2 a tick, 4 an orb (on its delete tick), full at 125, the bar `1 + u * 99 / 125` (77 of 77 fills in 14, both Wardens). The platform is `_platform_<fill / 5>` (45606..45626) every tick |
| Discharge (D) | the bar reads 100 and the platform 0 on D; nothing gained D+1..D+3 and no bar or platform sent; the overflow carries (14: Elidinis 100 on 3624, 6 on 3628 = 1 + 2 + an orb). `npc_wardens_impact01` (9667) with `_magic01_activate01` (2214) for pyramids or `_magic02_activate01` (2215) for the shot, alternating per Warden, pyramids first; synths 6142 d2, 6085 d40, 6043 d52, 6155 d60, 6226 d90, 6061 d99, 6220 d101, 6261 d159 |
| Pyramids | D+3 `toa_wardens_pyramid01` (Elidinis, 30 tiles) / `02` (Tumeken, 32) and synth 6132; D+4 `_load`; D+7 `toa_wardens01_pyramid_attack` (9524); D+11 `toa_wardens01_pyarmid_leave` (9526) + `toa_wardens_magic_ceiling_lights_despawn_01` (6077) range 2 at each; D+12 `invisible_type0_nonblocking` (32545). The tiles are a 3-tile lattice from (3798,5141), 7 x 9: Elidinis' where i + j is odd, Tumeken's even, none on the obelisk. No raider was ever under one |
| Shot | D+3 "A large ball of energy is shot your way..." (Elidinis `3366ff`, Tumeken `ff8e32`) and `spotanim_wardens_phase01_ball02` (2238) / `_ball01` (2237) from the Warden's middle (3794/3822,5154) at every raider: start 30, end 420, angle 40, slope 64, height 87 → 20; synth 6119 / 6148 d30. D+16 `tob_sotetseg_sharedattack_impact` (1605) on the raider and synth 6073 d15; D+17 the hit (1852: 24347 / 24350 / 24363 / 24364). 30 a hit in a group for Elidinis, once more per raider within a tile (30, 30); Tumeken's 7 four-stacked |
| Break | the last hitpoint on T (bar 20 at 0), **O = T+1**: `fx_obelisk_wardens01_charge` (9723) and `toa_wardens_p2_obelisk_npc` (11752); `obelisk_explode_energy` (2196) toward the destroyed Warden: (3811,5154) d6, (3812,5153..5155) d12, then x 3813..3818 at z 5152/5154/5156 d18..48; `vfx_scarab_explosion01..04` on its plinth, three on the floor height 25 d54, (3820,5153..5155) d60 and (3821,5154) d66 raised 100-200; `toa_wardens_thunder_travelling_warden_01` (6191) range 15 along z 5154, delay 0 to x 3814, 1 to 3819, 2 beyond; synths 6131, 4386 x4 |
| After O | orbs still spawn on O+1 and move; the pyramids run on. The destroyed Warden's shot in the air does not land (1852: Tumeken's from 24357, O 24370), the survivor's does (19950: 30 on O+2) |
| O+2 | every orb deleted; the message; the destroyed: hit 800, headbar 21 at 0, `spotanim_wardens01_explosion01_explode01` (2216), `npc_wardens_explosion01` (9669), `toa_warden_tumeken_phase3_inactive` (11760), then `_explosion01_idle01` (2217) every tick; the survivor `npc_wardens_wake01` (9663) and `toa_warden_elidinis_phase2_mage` (11753); synths 6065, 6205 d17, 6249 d109, 6167 d164, 6150 d197, 6091 d233, 6178 d269, 6101 d309, 6037 d348 |
| O+3 | the obelisk's `fx_obelisk_wardens01_charge_part1` (9724) |

The survivor is the Warden with more charge (the wiki: "the Warden whose
energy gain was disrupted the most will be destroyed"); Elidinis in all 8.

**Unrecorded, from the reference ([nr], regarded-dev `WardenEncounter`):**
pyramid damage, 11 x the damage factor + random(2) per tick D+6..D+10 inside
a pyramid's 3x3; `Ancient Haste` doubling the charge (the wiki says only
"faster"); a tie going to Tumeken. Tumeken surviving is unrecorded: its line
is written in Tumeken's colour and it wakes `_phase2_range` (the wiki: ranged
first).

### Ours against it

`test/raids/toa_wardens_p1_tells.lua` (solo, raid level 0, the east gap
blocked from S+32 for 20 ticks) and `rsprox_tells.py --anchor
11746-11751,11769 14.txt --ours`. Every recorded P1 tell kind is sent:
the room's build, the retypes, the orbs (add, exactmove, `zuk_proj`, death),
the cast, the hit sound, both charge bars, the platforms, the discharges and
their synths, both pyramid sets, both shots with impact and sound, the break's
map anims and sounds, the wake and the explosion. Elidinis' bar, unblocked,
is the recording's to the tick: 2, 4, 5, 7, 8, 10, 12, 13, 18, 20, 24 ... 96,
100 on S+38 (14: 3624 = S+38), nothing for 3 ticks, then 6, 8, 12, 14. The
last hit on 193 and O on 194, the wake on O+2, the obelisk's charge on O+3.

Differences: the charge bars go out when the fill changes, not every tick;
the bar removals at O+2 (headbar 38 at -1) are not sent; the scarab
explosions' variants and raised heights are random, as recorded, so differ
run to run.

## Phase 2: the warden

P is the wake (O + 2). Same room. 18 windows, 47 downs.

### The Warden

| | Recorded |
|---|---|
| Walk | 5 tiles east, a step a tick, P+14..P+18 (18 of 18); after that it steps only now and then (10 steps in 150 retargets, no distance rule) |
| Attacks | from P+23, every **7** ticks (solos), **6** with Acceleration (553 #2-3, 1845, 1852); Tumeken's 8 (the wiki). Each attack fires at **every raider** (projectiles x the party, every attack). Faces a new raider each attack |
| Plain | a coin flip, 373 magic / 401 ranged: `npc_wardens_magic01` (9661) then `fx_wardens_bomb01` (2224) from SW+(4,3), or `npc_wardens_ranged01` (9660) then `projectile_warden_ranged01` (2241) from SW+(2,2); launched A+2 (start 0, end 90, angle 12, slope 128, 100 → 25), landing A+5 (373 of 377): magic with `toa_wardens_magic_impact` (2213) height 124 and `fireblast_hit`, ranged with `toa_wardens_range_projectile_impact_01` |
| Damage | through the right prayer at most 5 at Entry, 8 at 500-530, 9 at 700; unprotected up to 50 at the cap (the wiki's 20 x 2.5) |
| Specials | none until 4 attacks since the last; then **Entomb** at once (32 of 32 gaps exactly 4) at or below 55% health, the **Divine Projectile** above it at 30% an attack, forced on the 8th (26 gaps: 8, 5, 4, 2, 7 at 4..8). The count runs on across downs |
| Divine | the cast sound (`waterblast_`/`windblast_`/`earthblast_cast_and_fire` d26) on A; A+2 the type line ("launches an arcane spell" `a53fff` / "throws an arcane scimitar" `ff3045` / "fires an arcane arrow" `229628`), "Your protection prayers have been disabled!" and `toa_wardens_prayer_magic/melee/ranged_travel` (2208/2204/2206: end 120, slope 64); A+6 the hit, 0 under its prayer (33 of 36), with `_prayer_*_impact` (2209/2205/2207) and `*blast_hit`. 17 magic / 12 melee / 8 ranged |
| Entomb | `fireblast_cast_and_fire` d26 on A; A+2 `toa_wardens_entomb_travel` (2210) at each raider's tile (end 90 in a party, 120 solo; slope 64, 100 → 10) with `gargboss_debris_shadow_120`; landing on A+5 / A+6: every raider on the tile gets "You've been entombed in stone!" (one per projectile on it) and `toa_wardens_entomb` (9713), the loop (9714) to C+5, `toa_wardens_entomb_end` (9715 + spotanim 2211) on C+6; no damage. An empty tile gets `toa_wardens_entomb_miss` height 10 |
| Sound | its hits `toa_wardens_defend_01` (6591) range 9 at its middle |

### The shield and the core

| | Recorded |
|---|---|
| Shield | healthbar 38 (140 wide) **counting up** with damage; the HUD (varbit 6099/6100) the same. Pool 140 scaled like any boss: 225 at 150, 310 at 300, 420 at 500, 530 at 700, 830 two at 530, 1220 three, 1720 five at 520, 1980 six (rounded: to 5 under 300, to 10 above) |
| Hits on it | splat 53 (own), 54 (others'), 55 (own max); always land. The style it lets through (`_mage` magic, `_range` ranged) between half the max and the max; **any other nothing**: the cache's overheads are `_mage` headicon 7 (Protect from Melee + Missiles), `_range` headicon 8 (Melee + Magic). The "third of that" once read off 101 (blowpipe 2-25 on `_range`, 2-9 on `_mage`) and 1852 (twisted bow 3-72 against 1-25) is Leagues V only; the normal-world 4292 switches weapon with each form and its 1-3s on either form are the thrall's (owner 2026-10-21) |
| Kneel K | on the hit that fills it: `npc_wardens_kneel01` (9670), facing the core's tile (three out from its middle toward its last target: (3802,5157) from (3799,5154)); attacks stop |
| K+5 | `_phase2_exposed` (11755), headbar 21 (140 wide) at the Warden's own health; `projectile_warden_elidinis_core` (2240) to the tile: start 0, end 60, angle 127, slope 124, 95 → 12 |
| Its health | 4500 at Entry, scaling a quarter as fast as other bosses (+0.1% a level: 5180 at 150, 6750 at 500, 7650 at 700) and with the team as usual (13080 two at 530, 31460 six at 520) |
| K+7 | the core (11771) and `toa_wardens_core_land_impact_01` range 15 at its tile. Its hits show on it (16/17, 43 for a max; no headbar) and on the Warden x5 in the same splat (185 for 37) |
| Back | after 1 + 21 / 29 / 37 ticks out by health (the wiki's bands; 22, 30, 38 recorded): the projectile back from the tile (start 60, end 90, angle 20, slope 64, 20 → 62); the tick after, the core stops, `toa_wardens_destroy_core_from_warnden_01` and it is gone; 2 ticks later `npc_wardens_standup01` (9672) and the **other** style (`_mage` ↔ `_range`), the shield from empty; first attack 4 later |
| End X | the core's hit that empties it: the remainder on the Warden, `npc_wardens_death01` (9662), the core gone, "<col=ff8e32>Elidinis' Warden uses the last of its power to restore Tumeken's Warden!</col>", `invisible_type8_nonblocking` on the core's tile, synths 6228 d125, 6109 d249, 6196 d254 → phase 3 |

### The obelisk's specials

| | Recorded |
|---|---|
| Charge | `_charge_part1/2/3` every 2 ticks; the first open 36 ticks after P+1 (parts from 10, 26), then 30 after each restart (8, 20); with Acceleration 28 (8, 20) and 22 (6, 14). It **holds while the core is out** and goes on from where it was at the stand-up |
| Open | `fx_obelisk_wardens02_open` (9728) and `square_thunder1..4` d30/60/90/120 range 9 at its middle; the special 1 tick later (beam, sweep) or 2 (skulls); `_close` (9729) 13 / 37 / 21 after opening, charging again 3 after |
| Order | always beam → sweep → skulls → beam, from a random one (49 of 49) |
| Kneel | an open obelisk closes the tick after and its special stops (every cut pattern a prefix of the whole) |
| Warnings | 4 ticks from the open, ~3 a tick on the ring round the obelisk: `wardens_darkness_warn` (beam), `_burn_warn` (sweep), `_lightning_warn` (skulls) |
| Beam | S+6 / S+12 `toa_wardens_single_tile_sweep_01` d120; S+7 `spotanim_amascut01_strike01` along x 32 (delay 24 at the ends down to 4); S+8 `spotanim_wardens01_dronestrike01_fire01` rings 0..11 out of (32,22) and (32,46), 30 a ring, lateral 10 (9 on ring 11), never on the obelisk, with `red_thunder_02..11` range 10 at both ends; S+13 / S+14 the same east-west from (21,34) and (43,34). 588 of 588 tiles, 13 uncut beams |
| Sweep | 12 rounds, 3 ticks apart from S+3: `_strike01_highlight01` d30 on one of five 4-arm patterns in turn (toa.enum `toa_wd_sweep_0..4`, all 7 uncut sweeps identical), `red_tille_quarter_whoosh_01` d60 range 15; 3 ticks later `_strike01` on the same tiles, delay 2 a tile out |
| Skulls | 10, one every 2 ticks from S+1, to random tiles (x 22..42, z 24..44; none followed a raider): `fx_wardens_bomb02` from (32,34) (start 23, end 120, angle 40, slope 64, 100 → 3), `gargboss_debris_shadow_120`, `obelisk_red_thunder_projectile_falling_01` range 9; +1 `_fall_impact_02` range 10; +4 `wardens_lightning_warn` 3 rings out (10 cycles a ring), the 4 tiles each way along the cardinals spared, `4_square_thunder_01` range 15 |
| Damage | the sweep's 30-33 at the cap = 13 scaled + random(3) |

**Unrecorded, from the reference ([nr], regarded-dev `MovingWardenNPC`,
`WardensObelisk`):** the beam and skull damage (13 scaled + random(3), the
skulls' outer ring a tick later), the warning ring's 9, Penetration's 18 / 14
with prayers off, Divine's unprotected roll (22 scaled), a melee hit on the
shield (taken as a wrong style). Tumeken's Warden surviving is unrecorded:
its rate (8) and max (22) are the wiki's, its end line turned round.

### Ours against it

`test/raids/toa_wardens_p2_tells.lua` (solo, Entry, twisted bow) and
`rsprox_tells.py --anchor 11753-11758,11770,11771 14.txt --ours`. Every
recorded phase-two tell kind is sent: the walk (P+14..P+18), the attacks from
P+23 every 7, the specials and their sounds and impacts, the shield bar
counting up in its own splats, the kneel, exposure, core, return, stand-up in
the other style, the obelisk's charge, open, three specials and close, and the
end. A down to the tick: K, K+5 form and throw, K+7 core, K+29 back, K+30
gone, K+32 stand-up, K+36 attack; the obelisk's first open P+37, close +21,
restart +3, next open +30, as 14.

Differences: the Warden does not take the occasional step toward a raider
(no rule found for them); the headbar removals at each change of bar (38 /
21 at -1) are not sent, the new bar simply replaces the old.

