# Verzik P2, Hard Mode, five raiders, on v2 (`tob2_verzik_p2.lua`)

Entry: `QD.raid.verzik_p2_v2_solve(opts)`, opts `{max_ticks, base, weapon}`. Done = her P2->P3
transition row seen (v1). Perception is v1's, called: `_vzp2_clock` (attacks every 4, first +3,
summon +12, seventh +8; F.scans, F.zap_scan, F.heal_ticks), `_vzp2_projectiles` (urn/zap/
Athanatos projectiles -> S.hazards, the zap counter), `_vzp2_crabs` (S.blasts: kill blasts and
stops, `crab_look` 1 as v1 HM), `_vzp2_formation` (the zap pair, latched), `_vzp2_side`.

Geometry: her SW (31,25) local, size 3. Melee = gap 1, cardinal.

## Threat field (forbid unless said; owner "no avoidable damage", "no seat poisoned")

| hazard | B term | window (tick ends) | dmg |
|---|---|---|---|
| body slam (adjacent at T-1) | `B.hit` forbid, her 3x3 +1 | t = T-1 for every predicted attack T | 45 + stun |
| under her (stomp, bounce) | `B.hit` forbid her 3x3 | always | 82 |
| urn on my tile (thrown at T at every raider's end-of-(T-1) tile) | `B.mark` t_mark = T-1, r 0, forbid | [T, T+21] (land T+n-2, n = 2..4, + the 16-tick pool + 2 margin, main tree) | 53 + pool 1-10/tick + poison |
| the same, T-1 = now or T-1 = now-1 | `B.hit` forbid on tile_at(T-1) | same | |
| reds phase urn (25% of attacks) | the mark, PRICED exp 20, not forbid | same | |
| urn seen in flight (S.hazards urn) | `B.hit` forbid | [last-1, last+18] | |
| Athanatos landing (seen in flight) | `B.hit` forbid | [last-1, last] | 78 |
| crab blast (S.blasts) | `B.hit` forbid, footprint +3 (HM: the 7-band too) | [from, to] | 63/26/7 |
| HM red at her next summon | `B.hit` forbid, red footprint +2 | [summon-1, summon+1] | 46 |
| a walking red | `B.hit` forbid, its footprint stepped on | now+1..now+3 | routed into her |
| lightning | `B.pull` weight 60 to my formation tile at the zap scan end; `B.zone` soft 300 on the other stack's tile | t = zap_scan | 48 |

## Mode machine (per seat, chosen fresh each tick)

| mode | when | target(s) | pulls |
|---|---|---|---|
| wait | no P2 body (she is flying in) | - | my side's stand (v1 predicted body) |
| ath | Athanatos alive, I am a killer (3 east, 5 west) and it landed on my side or over her columns | Athanatos (value 10: one poisonous hit pops it; tentacle hits are poisonous in content) + her | |
| red | reds out, my red alive (east seats 3,4,5 eastmost, 1,2 westmost; chosen once per summon) | my red, her | |
| her | otherwise (the last set left alone when her bar goes inside the absorb at the measured rate, v1) | her, t_ok outside the absorb window (v1 heal_ticks) | |
| + pop | my crab (the one facing me, else nearest) alive, unlit | | `B.pull` its footprint +2, weight 1 (owner: pop by lighting the fuse and running) |
| + formation | a zap scan inside the horizon | | as above; 3 west (1,2,5), 2 east (3,4) |

Overhead: Protect from Missiles before the reds, Protect from Magic after (v1). Piety boost when
`vz_prayer_spare` says the P3 budget allows (v1). Weapon back in hand if it is in the pack.
Floors: eat 49, 75 with a crab within 5 (v1).

The one-tick-at-a-time walk (E6, v1 HM acid14c) is the executive's: every walk is the tick-end tile.

## Value (HP_PER_BOSS_HP = 0.10, PROBE)

Oracle all128i: Verzik 288/seat-run with P3 autos 222 -> P1+P2 ~66/seat = 330 team; dealt in P2:
its share of the 3500 pool (~65% = 2275) + Athanatos/red heals (~600) = ~2900 -> 0.11.
Tentacle on her (def 200) exp 9 (PROBE) -> 0.9 a swing; a red 1.5x (its hp heals her at the
summon); the Athanatos 10.

## PROBES

- P2 ticks, swings/seat, damage/seat by source (urn, acid, slam, zap, crab, red) per seed.
- urn mark window start T (n = 2 at d <= 4): a `hit_player` from an urn = a model miss.
- the zap formation with 3/2 stacks under HM pools (zap damage per seed).

PASS: P2 cleared, no raider dead, no seat poisoned, 0 avoidable damage.
