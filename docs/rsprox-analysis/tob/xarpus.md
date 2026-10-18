# Xarpus

This file records what Jagex's server sends in the Xarpus room. It is read
from 28 Normal Mode kills: 3299 (5), 3285 (1), 788 (1), 1833 (7), 1764 (4),
553 (3), 1779 (3), 1837 (3) and 582 (1). Party sizes are read off the heal
per orb: 20 is solo, 16 duo, 12 trio and 9 four. The kills are 1 solo, 18
duos, 7 trios and 2 fours.

Sources, caveats and units are in the [README](README.md). Heights are in our
units (a v2 packet's raw value ÷ 4).

The tool is `tools/rsprox_xarpus.py`, with sections phases, exhumed, spit,
splash, acid, pools, stare, guano and death. Its full output over these kills
is [xarpus.out](xarpus.out).

**Ours:** `tob_xarpus.rs2`, with its constants in `tob.constant`.

**Status key:**
- **fixed:** changed to match the recording.
- **match:** ours already sent it.
- **open:** differs, not changed, with the reason.

## The lock: when a spit's target is chosen

This is the one timing that is easy to get wrong, so it comes first.

**The target tile is locked one tick before the spit.** That covers the spit's
own target and its splashes' targets. The spit packet (seq and projectile) is
on tick S. Every target is the tile a raider stood on at the **end of tick
S − 2**. That is where they were at the start of tick S − 1, before the
raiders move that tick.

How this was measured: take only the spits where the local raider moved on
tick S − 1, so the end-of-(S − 2) tile and the end-of-(S − 1) tile differ.
- **22 of 22 spits** at the local raider went to the end-of-(S − 2) tile.
- **29 of 29 splashes** at the local raider went to the end-of-(S − 2) tile.
- None went to the tile they had moved onto.

Xarpus also turns to his target on tick S − 1: there is a `face_angle` the
tick before each spit.

**Ours (fixed).** Our npcs run before the players in a tick (phase 4 before
phase 5, `torirs_server_world.c`). So an npc script on tick S − 1 reads exactly
the end-of-(S − 2) tiles.
- `~tob_xarpus_lock` runs on S − 1 (`^tob_xarpus_lock_lead` = 1). It draws the
  target, stores its tile and the splash tiles, and turns him.
- `~tob_xarpus_spit` on S throws at the stored tile.
- The landing throws its splashes at the stored splash tiles.

The constants block above `^tob_xarpus_lock_lead` in `tob.constant` says the
same.

## Phase 1: the exhumeds

| | Recorded | Ours | Status |
|---|---|---|---|
| First exhumed | 8–11 ticks after the start | 8 + up to 3 | match |
| Count and gap | solo 7 every 12, duo 9 every 8, trio 12 every 8, four 15 every 4 | same | match |
| Life | 11 ticks, add to close (286 of 286) | 11 | match |
| Orbs | the first 3 ticks after the add, then one every tick while uncovered | same | match |
| Orb projectile | `tob_xarpus_exhumed_energyorb` at a **tile**, his south-west + (1, 1) (551 of 551). Start 0, end **10 × Chebyshev distance**, angle 50, progress 0, height 0 → 67 | aimed at him; 0 → 80, angle 43, 30 + 1 a tile | **fixed** |
| Heal | lands **floor(end ÷ 30) + 1** ticks after the orb leaves (88 of 88 isolated orbs) | on the tick it was fired | **fixed** (`[ai_queue6]`) |
| Heal amount | 20 / 16 / 12 / 9 by party | same | match |
| Sounds | `exhume_health_shots` at the exhumed on the orb's tick (delay random 0–5, range 7). `swollowing_health` at the orb's target on the heal tick (delay random 0–10, range 12). Open and close at range 7 | swallow at the exhumed when fired, range 20 | **fixed** |
| Hand-off | last close to the combat transform: 8–9 ticks | 9 | match |

## Phase 2: the spit

| | Recorded | Ours | Status |
|---|---|---|---|
| Clock | a spit every 4 ticks (464 of 464 gaps) | 4 | match |
| First spit | **9 ticks** after the combat transform (28 of 28) | 7 (Blert) | **fixed** |
| Projectile | from his centre to a **tile**. Start 30, angle 16, progress 32, height 100 → 0. End **30 + 30 × max(1, distance from his footprint)** | 104 → 0, delay 27, angle 28, 60 + 5 a tile | **fixed** |
| Landing | the pool on tick S + end ÷ 30 (2–6 ticks) | about 4 ticks at any distance | **fixed** |
| Pool | rotation by quadrant from his centre: NE 3, SE 0, SW 1, NW 2 | same | match |
| Fresh tile | pool + `acid_floor_hit` (range 7) + acidsplash | same, range 20 | **fixed** (range) |
| Tile already pooled | no new pool. Acidsplash + `player_acid_burn` (range 7) instead (354 of 354) | floor-hit sound | **fixed** |
| Damage | raiders within 1 tile (3×3). A **poison** splat (hitsplat 65, 151 of 151), 2–7, with "You have been poisoned!" on the same tick. Poison ticks every 30 after that, the first at +31 for 2 or 3 | damage splat, no poison | **fixed** (severity 10–11) |

## Phase 2: the splash

| | Recorded | Ours | Status |
|---|---|---|---|
| Count | **1 or 2 a landing**, about evenly, at every party size and on the first spit too (duo 156 / 132, trio 27 / 33, solo 29 / 18) | first spit 1, later 2 (wiki); solo 1–2 (Near-Reality) | **fixed** |
| Targets | the **other** raiders' tiles at the lock, one each, never the spit's own target. Extra splashes, and every splash solo, go to random tiles. In a trio with two splashes, one was the local raider's tile in 21 of 25 | the next raiders in orb order, at **landing** time | **fixed** |
| Projectile | from the new pool, start 15, angle 45, progress 32, height 0 → 0, end **30 × (distance + 1)** | 10 → 0, angle 43, the spit's flight | **fixed** |
| Chains | a splash never splashes again | same | match |

## Phase 2: a raider under him (the guano)

| | Recorded | Ours | Status |
|---|---|---|---|
| Trigger | a raider inside his 5×5. It can fall every tick they stay | same | match |
| Projectiles | two from his centre that **follow the raider**: start 0, end 20, height 40 → 24; then start 10, end 30, height 37 → 24 | at a tile; 40 → 20, delays 0 and 15, end 30 / 45 | **fixed** |
| Sound | `apple_splat` at the raider's tile, delay 20, range 4, **loops 2** | none | **fixed**, but loops is **open**: `sound_area` always sends 1 |
| Damage | two damage splats the next tick | same | match |
| The spit | **never held back**: all 464 gaps are 4, with 32 guanos among them | a raider under him skipped the spit | **fixed**: only a full stack interrupts (below) |

**The whole party under him.** No recording has it. Three locks had the local
raider under him, and each time the spit went to another raider standing
outside. Ours keeps the wiki's "interrupts his poison attack" for that one
unrecorded case: if every raider still fighting is under him at the lock,
nothing is locked and that spit does not come.

## Phase 3: the stare

| | Recorded | Ours | Status |
|---|---|---|---|
| Start | `npc_say` **"Screeeeech!"** (five e's), `damaged_screech` at his south-west tile (range 15), and "Xarpus begins to stare intently." on the same tick (28 of 28), 3 ticks after the last spit | "Screeeech!", range 20, no line | **fixed** |
| Threshold | the HUD at 237–281 ‰ at the screech | 25% | match in shape |
| Turn | every 8 ticks (122 of 122) | 8 | match |
| Retaliation | the line; an acidsplash on the raider (height 80); his spit seq with delay s; `leech_suck` at his centre (delay s, range 8); **one** acidspit following the raider, height 87 → 20, angle 16, progress 32 | an instant damage splat, nothing else | **fixed** |
| Its timing | at footprint distance 1, s = 0 and end 25. At 2, s = 1 and end 30. At 3, 4 and 5, s = 2 and end 60, 90, 120. The poison splat lands floor(end ÷ 30) ticks after the line | | **fixed** |

## His death

All 28 kills give the same sequence. T is the killing hit.
- **T:** `tob_xarpus_death_a` and `death_screech` (range 10, at his south-west).
- **T + 1:** the transform to `xarpus_death` and `death_b`.
- **T + 4:**
  - he is deleted;
  - the "complete" line;
  - the book;
  - song 147;
  - jingle 250;
  - then song 567.
- **T + 4 to T + 8:** the pools end with their `acidpool_end_N` graphics. The
  server swaps each one for an invisible loc rather than deleting it. Ours
  deletes it, which looks the same.

Ours, measured from the killing hit T (`tob_xarpus` seed sa):

| | Recorded | Ours | Status |
|---|---|---|---|
| `death_a` | T | T | **fixed** |
| Death screech | T | T (from the killing hit's `[ai_queue2]`) | **fixed** |
| Retype + `death_b` | T + 1 | T + 1 | **fixed** |
| Deleted | T + 4 | T + 4 | match |
| Books, song 147 + jingle 250 | T + 4 | T + 4 | **fixed** |
| Song 567 | T + 4..7 | T + 5 | match |
| Pools end | T + 4..7 (four batches) | T + 4..7 | **fixed** |

How it was fixed:
- **The death animation:** the engine's `npc_setdeathanimnow`, set at his
  stand-up, sends it with the killing hit.
- **The death queue:** his combat records state `death_delay=0`, the engine's
  "no corpse wait", so `[ai_queue3]` runs at T + 1.
- **The deletion:** the collapse queue now waits one tick more, for T + 4.
- **The pools:** the dissolve delay is now 3 ticks after `[ai_queue3]`.
- **The room end:** it comes on the delete (`~tob_xarpus_killed`, as Bloat's
  does). Normal and Hard give a private book to each raider, as recorded.

## The solver (tob2_xarpus.lua)

The v2 solver's forecast was moved to these mechanics:
- **The first spit:** U + 9 (it was 7). With 7, its A/B cycle ran two ticks
  out of phase with the lock. The stack stood beside him at every lock, every
  pool landed beside him, and the tiles to attack from ran out.
- **The spit mark:** on the end-of-S − 2 tile, reading S + 1 .. S + 5, which
  covers every landing distance.
- **The splashes:** forecast back onto the spit's own pool at L, because a stack
  locks one tile. This replaces the old landing-time chain marks.
- **The stack phase:** holds until the last spit + 6 (`_xarp_phase`).
- **The screech:** matched on the prefix `Scree`. The old match was the
  four-e spelling, which never saw the recorded five-e one.

The stack strategy itself was kept. It still works under the recorded rules:
the stack locks one tile, so the splashes come back onto the spit's own pool,
two tiles from where the stack has moved.

## Open
- **`apple_splat` loops 2** needs a loops argument on the engine's
  `sound_area`.
- **The hit sound** (`tob_xarpus_hit`, 4018) is recorded as a synth to the
  attacker. Ours plays it as the npc's own sound.
- **The debug readout** `tob_selftest.rs2 tobgates` counts open exhumeds from
  registers 46–49. In phase 2 these hold the lock's splash tiles.
