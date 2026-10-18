# The Wardens, phases 1 and 2

Read from 8 kills: 14 (solo, Entry) and 1852 (7 Expert raids), region 59,80.
Ticks are relative to entry (E) or the event named. Full inventory:
[wardens_p1p2.out](wardens_p1p2.out). Phase 3 is [wardens_p3.md](wardens_p3.md).

**Ours:** `minigame_toa/scripts/toa_wardens.rs2` (not yet compared).

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

| | Recorded |
|---|---|
| Obelisk | `fx_obelisk_wardens01_cast` (9721) every **2 ticks** (224 of 224 gaps) |
| Energy | `toa_wardens_energy` (11769) added at (3805,5154) or (3811,5154), beside the obelisk, one about every 2 ticks; it plays `zuk_proj` (7571) every tick and moves one tile a tick (`exact_move` to1 = one tile behind, d2 30, angle 512/1536 or a diagonal). It ends with `fx_wardens_energy_death01` (9735) |
| Wardens | each plays `npc_wardens_impact01` (9667) every **24 ticks** (Elidinis about E+38, Tumeken about E+48) with spotanim `spotanim_wardens01_magic01_activate01` (2214) / `_magic02_activate01` (2215); the slow ball `spotanim_wardens_phase01_ball02` (2238) to a raider: start 30, **end 420**, angle 40, slope 64, height 87 → 20 |
| Obelisk dies (O) | retypes to `toa_wardens_p2_obelisk_npc` (11752) with `fx_obelisk_wardens01_charge` (9723); map_anim `obelisk_explode_energy` (2196) out from it (delays 6, 12, 18, 24, 30 by distance) and `vfx_scarab_explosion01` (2156) height 25 delay 54 |
| O+2 | the message; Tumeken's Warden plays `npc_wardens_explosion01` (9669) with spotanim `spotanim_wardens01_explosion01_explode01` (2216) and retypes to `toa_warden_tumeken_phase3_inactive` (11760) (which then carries `spotanim_wardens01_explosion01_idle01` (2217) for the rest of P2). Elidinis' plays `npc_wardens_wake01` (9663) and retypes to `toa_warden_elidinis_phase2_mage` (11753) |

## Phase 2: the warden

### Basic attacks

Every **6 ticks** from about O+25, a mix of:

| Sequence | Projectile (warden → raider) |
|---|---|
| `npc_wardens_magic01` (9661) | `fx_wardens_bomb01` (2224): start 0, end 90, angle 12, slope 128, height 100 → 25 |
| `npc_wardens_ranged01` (9660) | `projectile_warden_ranged01` (2241): the same parameters |

### Prayer specials

"<col=a53fff>The warden launches an arcane spell.</col>" or "<col=ff3045>The
warden throws an arcane scimitar.</col>": `toa_wardens_prayer_magic_travel`
(2208), `_melee_travel` (2204) or `_ranged_travel` (2206) to a raider: start 0,
**end 120**, angle 12, slope 64, height 100 → 25. Also seen: "<col=ff3045>Your
protection prayers have been disabled!</col>", "You've been injured and can't
use protection prayers!".

### Entomb

`toa_wardens_entomb_travel` (2210) to a tile: start 0, end 90 (some 120),
angle 12, slope 64, height 100 → 10. The tile gets map_anim
`gargboss_debris_shadow_90` (1446) on the throw and `toa_wardens_entomb_miss`
(2212) height 10 on the landing. Most land on (3803,5157), beside the core's
tile, repeating every 24 ticks; seen from the second half of phase 2.

### The obelisk's specials

The P2 obelisk opens (`fx_obelisk_wardens02_open`, 9728) every 38–46 ticks
(more when the warden is down), plays one special the next tick, and closes
(`fx_obelisk_wardens02_close`, 9729) 13 or 21 ticks after opening. It also
charges between them (`fx_obelisk_wardens01_charge_part1/2/3`, 9724–9726,
every 2 ticks).

| Special | Recorded |
|---|---|
| Beam (`fx_obelisk_wardens02_beam`) | warn tiles map_anim `wardens_darkness_warn` (2199) on cast+0..2; cast+7 and +13 `spotanim_amascut01_strike01` (2234); cast+8 `spotanim_wardens01_dronestrike01_fire01` (2235) on a large area with delays 270–330 |
| Sweep (`_sweep`) | `wardens_burn_warn` (2200) on cast+0..2; then every 3 ticks a row of `spotanim_amascut01_strike01_highlight01` (2236) delay 30, followed 3 ticks later by `spotanim_amascut01_strike01` (2234) on it, sweeping across the arena |
| Bombs (`_bomb`) | `wardens_lightning_warn` (2198) on the targets; every 2 ticks from cast+1 a `fx_wardens_bomb02` (2225) to a tile: start 23, end 120, angle 40, slope 64, height 100 → 3; the tile's shadow `gargboss_debris_shadow_120` (1447) |

`spotanim_amascut01_strike01` delays fall by 2 cycles a tile along a line (24 at
(3808,5142) down to 10 at (3808,5148)), so the strike runs towards the warden.

### Kneel and the core

| | Recorded |
|---|---|
| Kneel K | `npc_wardens_kneel01` (9670); 3–6 times a kill (more in larger raids) |
| K+5 | retype `toa_warden_elidinis_phase2_exposed` (11755); `projectile_warden_elidinis_core` (2240) from the warden to the core's tile: start 0, end 60, angle 127, slope 124, height 95 → 12 |
| K+7 | the core `toa_warden_elidinis_core` (11771) added at (3802,5157) (sometimes a tile off); the boss bar shows the core: base hp 140 solo Entry, 830 (2 raiders), 1220 (3), 1980 (6) |
| K+29..43 | the core returns: `seq -1` on it and `projectile_warden_elidinis_core` (2240) back to the warden: start 60, end 90, angle 20, slope 64, height 20 → 62 |
| K+32/40/48 | `npc_wardens_standup01` (9672); the warden retypes to the **other style** (`_phase2_range` 11754 ↔ `_phase2_mage` 11753) |
| Last | the warden falls: `npc_wardens_death01` (9662); "<col=ff8e32>Elidinis' Warden uses the last of its power to restore Tumeken's Warden!</col>" → phase 3 |
