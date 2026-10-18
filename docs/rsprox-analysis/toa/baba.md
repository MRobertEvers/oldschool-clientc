# Ba-Ba

Read from 8 kills: 14 (solo, Entry) and 1852 (7 Expert raids). Ticks are
relative to entry (E) or the event named. Full inventory: [baba.out](baba.out).
Common entry/leaving: [README](README.md).

**Ours:** `minigame_toa/scripts/toa_baba.rs2` (not yet compared).

## Npcs

| Npc | Id | |
|---|---|---|
| Ba-Ba | `toa_baba` (11778) | added at (3815,5406); `npc_mandrill_spawn01` (9752) on E+3..5 |
| Digging | `toa_baba_digging` (11780) | |
| Dead | `toa_baba_coffin` (11779) | `npc_mandrill_despawn01` (9754) |
| Boulder | `toa_baba_boulder` (11782); breakable `toa_baba_boulder_weak` (11783) | |
| Rubble | `toa_baba_rubble_2` (11785), on `invisible_type8_blocking_size3` (42838) | |
| Baboons | `toa_baba_baboon` (11781) | 22 over the 8 kills |

## Phases

| | Recorded |
|---|---|
| Fighting | melee `npc_baba_attack_melee` (9743); the rock throw `npc_mandrill_attack_ranged_01` (9744) every 34–37 ticks |
| Dig 1 | at **66%** (headbar fill 91–92 of ~139, 8 of 8): `npc_mandrill_attack_special_jump01` (9748) with an `exact_move` (to1 = where she was, relative; d1 0, to2 = the tile, d2 30, angle the leap's direction), and 2 ticks later the retype to `toa_baba_digging` |
| Digging | the boulder volley every **8 ticks** (83 of 91 gaps) for about 56 ticks |
| Up | retype back to `toa_baba` with `npc_mandrill_attack_special_end01` (9751); if she is already under 33% she leaps again 3 ticks later |
| Dig 2 | at **33%** (fill 45–46), the same |
| Death | retype `toa_baba_coffin`, `npc_mandrill_despawn01` |

## The boulder volley (digging)

V = `npc_mandrill_attack_special_ranged01` (9749).

| Tick | Recorded (99 volleys) |
|---|---|
| V | Five projectiles `toa_baba_balls_travel` (2245) from **(3816,5409)** to (3815,5401), (3815,5404), (3815,5407), (3815,5410), (3815,5413): start 30, end 90, angle 35, slope 0, height 50 → 5 |
| V+2 | One boulder per row at x 3813, z 5401/5404/5407/5410/5413: **4 `toa_baba_boulder` and 1 `toa_baba_boulder_weak`**, the weak one's row random |
| After | each boulder rolls **west one tile a tick**: `toa_boulder_rolling` (9518) every tick with an `exact_move` to1 (1,0) from the tile, d2 30, angle 512 |

The weak boulder can be broken (hit type 43 on it; its headbar empties).

## The rock throw

R = `npc_mandrill_attack_ranged_01` (9744) on Ba-Ba.

| Tick | Recorded |
|---|---|
| R | One `toa_baba_ranged_travel` (2244) per target (1–6): start 90, end 210 (one 180), angle 40, slope 64, height 100 → 31 |
| R+4..8 | Each target tile's shadow: map_anim `gargboss_debris_shadow_60` (2111), `_90` (1446), `_120` (1447), `_150` (1448), with delays 30/60/90 (a falling sequence) |
| R+9 | map_anim `toa_baba_rock_fall_faster` (2251) or `toa_baba_rock_fall` (2250) delay 20; the rubble npc `toa_baba_rubble_2` added on the tile, on an `invisible_type8_blocking_size3` |

Also at the start (E+16..21) a set of `toa_baba_rock_fall_faster` and rubble
before any throw.

## Banana peels

`toa_baba_banana_peel` (45755) shape 10, random rotation, on tiles the
baboons pass (from E+59); removed later by `loc_del`.

## Other projectiles

Baboons in the room throw `toa_baboon_ranged_travel_2` (2243): start 67,
end 80 + 3 × distance (83 at 1 … 101 at 7), angle 8, slope 128, height 36 → 36.
