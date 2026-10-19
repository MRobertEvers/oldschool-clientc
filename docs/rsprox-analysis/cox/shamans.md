# Lizardman shamans: recorded behaviour

Read from submissions 3282 (solo, normal) and 3389 (solo, Challenge Mode) in the raid, and 3276, the Molch temple's
surface shamans (`molch_lizardshaman_1`, 8565), which the wiki says share the raid shaman's attack set. 3276 is a
public area: transcribed with every player shown, its other raiders' hits are most of the data (84 swings, 28 acid
spits, 11 jumps, 13 summons, 31 spawn bursts). See [README.md](README.md). Tool: `tools/cox/rsprox_shamans.py`
(sections `inventory`, `attacks`, `cadence`, `choice`, `ranged`, `acid`, `jump`, `spawns`, `bursts`, `sounds`). The
room's spec is `docs/minigames/cox/rooms/shamans_spec.md`.

    python3 tools/cox/rsprox_shamans.py --section acid ~/.rsprox/binary/archive/{3282,3389}.txt <3276>.txt

**Bodies.** The raid shaman is 3x3; the Molch shaman is **2x2**, so every distance here is the **gap**: the
Chebyshev distance from the npc's footprint to the tile (0 under it, 1 in reach). The surface rules carry over to the
raid in gap terms: both raid recordings fit them once the turned rooms' mirrored chunk offsets are undone
(`rsprox_room.py`).

Ticks are relative to **T**, the swing (the shaman's animation).

## Projectiles

| Attack | Projectile | Start → end height (raw) | Start → end cycle | Angle, progress | Hit |
|---|---|---|---|---|---|
| ranged spit | 1291 `lizardman_spit` | raid 420 → 146 (surface 220) | 61 → 66 + 5 × gap | 5, 128 | lands end / 30: +2 to gap 4, +3 from gap 5 |
| acid spit | 1293 `lizardshaman_spit_acid` | raid 420 → 5 | 61 → 30 × (2 + gap / 2) | 30, 128 | splash and damage on T + 2 + gap / 2 |

Every recorded spit fits: the ranged at gaps 1–6 on the surface and 6 and 8 in the raid; the acid at gaps 1–6 on the
surface (28 of 28) and 7 and 8 in the raid. At gap 1 the acid's end (60) is before its start (61): it is never drawn.
The acid **lands after its shaman dies**: in 3389 a shaman died the tick after its spit and the splash came on time.

## The acid

| Row | Recorded |
|---|---|
| reach | the 5x5 round the target tile (hits at distance 0–2 from it, none at 3–4) |
| read | where each raider stood the tick before the splash |
| damage | the poison hitsplat (65 `hitsplat_poison_me`), 20–30 on the surface |
| splash | 1294 `lizardshaman_acid_splash` on the target tile, `dagganoth_splash` (1070) range 8 |

Many raiders inside the 5x5 took nothing: the area wears Shayzien armour, which the wiki says negates it.

## The swing's choice

Not adjacent (n = 67 attacks plus the summons): ranged 44%, acid 30%, jump 10%, summon 15% — 3 : 2 : 1 : 1.
Adjacent (n = 17): ranged 4, acid 4, jump 3, melee 5. The summon is **one of the choices**: it sits 4 ticks from the
swings either side of it. Near-Reality's roll (ranged 4, acid 1, jump 1 of 6, summon on a 1-in-61 per-tick timer)
does not fit.

## Cadence

4 ticks between swings; **9** after a jump (7 of 9).

## Melee

`shayzien_lizard_boss_tail_swipe` (7158) 4 times in 5 (the warrior's 7192 once), the hit on the swing tick,
`stabsword_slash` (2548) area range 6.

## The jump

| Row | Recorded (11 jumps) |
|---|---|
| tell | `shayzien_lizard_boss_jump` (7152) on T, `jump_no_land` (2467) range 8 |
| landing | **T + 6**: `shayzien_lizard_boss_land` (6946), the teleport and `moon_fall_land` (1695) range 8 on the same tick |
| where | the body centred on the target's tile the tick before the jump (7 of 10) |
| slam | not seen: no raider stood in reach of a landing |

## The summon and the spawns

| Row | Recorded (10 summons, 31 bursts) |
|---|---|
| tell | `shayzien_lizard_boss_minion_summon` (7157) on T |
| appear | T + 2, three `zeah_lizardshaman_spawn` with `shayzien_lizard_boss_rise` (7160) and `slug2_rumbling` (3033) range 8 |
| placement | a chain: the first on the target's tile the tick before the summon (10 of 10), the second on a neighbour of the first, the third on a neighbour of the second (never back on the first) |
| camera | the target's camera shakes as they appear (`cam_shake` axis 0, random 4, amplitude 2) and resets 3 ticks later |
| fuses | they burst in order on T + 10, + 12, + 14 |
| tell | `shayzien_lizard_boss_hands_to_head` (7159) and `screech2` (665, delay 20, range 5) 3 ticks before each burst; **no step after it** (31 of 31, 20 of them walking the tick before) |
| burst | 1295 `lizardshaman_spawn_explode`, `100_cauldron_explodes` (1021) range 8, the spawn deleted |
| reach | the 3x3 round the spawn: no one 2 out is hit by a lone burst (the wiki's 5x5 is not it) |
| damage | rising with the distance: 5–6 on its tile, 6–10 beside it, 9–10 on its corners |
| again | the next summon is at least 17 ticks on (17, 18, 19, 19, 26): after its last spawn has burst |

## Death and the drop (raid)

| Row | Recorded |
|---|---|
| hit sound | `crocodile_hit` (388) on every hit a shaman takes |
| death | `shay_lizard_warrior_death` (7196) and `crocodile_death` (387); deleted 3 ticks later |
| message | `<col=ef20ff>The Lizardman Shaman slain by <name> dropped seeds for all.</col>` (friends-chat notification) on the deletion tick |
| drop | big bones (despawn 199), golpar, buchu, noxifer 5–10 each (despawn 2999) |

## The clear (raid)

From the last shaman's deletion X:

| Row | 3282 | 3389 |
|---|---|---|
| the tendrils become `invisible_type8_nonblocking` | X | X + 3 |
| the exit blockers' `shayzien_lizard_boss_hands_to_head`, `screech2` range 6 | X + 3 | X + 2 |
| their burst (1295, `100_cauldron_explodes` range 5), the blockers gone | X + 6 | X + 5 |

The tendril locs are re-sent every 4 ticks while the room lasts (the same id): a 4-tick room loop.

## Fixed in our content (2026-10-18)

| Was | Now |
|---|---|
| NR's roll: ranged 4, acid 1, jump 1 of 6 (+ melee 1 of 7); the summon a 1-in-61 timer, 34 ticks apart | weights 3 : 2 : 1 : 1 (+ melee 3 in reach), the summon one of them while its last spawns are gone |
| spit 40 → 36, delay 32, angle 15, progress 11 | 105 → 36 (420 → 146 raw), 61 → 66 + 5 × gap, angle 5, progress 128 |
| acid 40 → 0, landing 1 + (1 + gap) / 3 (a ruling from video), on the shaman's queue | 105 → 1, landing 2 + gap / 2, on each raider's queue (lands after the shaman dies) |
| melee `shay_lizard_warrior_attack_melee` | the tail swipe, `stabsword_slash` |
| jump lands T + 4, next swing T + 8 | T + 6, T + 9; the recorded sounds |
| spawns at once, scattered round the target, fuses 10–20, a 5x5 burst; they kept walking through their tell | T + 2, a chain from the target's tile, bursts T + 10 / 12 / 14, the 3x3; they stand still for it |
| sounds by synth to the room (NR's ids) | the recorded area sounds, ids and ranges |
| no message; every drop 200 ticks | the slain message; seeds 3000 ticks |
| no hit or death sound | `crocodile_hit`, `crocodile_death` |
| the clear: tendrils at + 2, blockers + 6, burst + 2 more | tendrils at once, blockers + 3, burst + 3 more |

## Still open

- The melee animation: the tail swipe 4 of 5; the content always plays it.
- The jump's slam (20–25, the wiki's): no recorded raider was in reach.
- The spawn burst's damage is fitted (5, 7, 10 over the 3x3) to noisy samples.
- The acid's damage in the raid (20–39, the wiki's): no raid raider was hit by one.
- Our clear deletes the tendrils rather than swapping in `invisible_type8_nonblocking`, and does not re-send the
  tendril locs every 4 ticks (neither is visible).
