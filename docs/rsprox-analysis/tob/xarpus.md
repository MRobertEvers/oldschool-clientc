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
| Targets | the **other** raiders' tiles at the lock, one a tile, and **never the spit's own tile** (0 of 603 party splashes). A raider sharing the target's tile is not splashed. Splashes beyond the other tiles, every splash solo, and every splash of a party stacked on the target go to a **random arena tile, pooled or not** (below) | the other raiders at the lock, a stacked raider included, so a stack's splashes went back onto the spit's own pool and vanished; random tiles uncovered only | **fixed** (2026-10-18) |
| Projectile | from the new pool, start 15, angle 45, progress 32, height 0 → 0, end **30 × (distance + 1)** | 10 → 0, angle 43, the spit's flight | **fixed** |
| Chains | a splash never splashes again | same | match |

### Where the splashes go, with every raider shown

The rows above were first read from the archive's transcripts, which show the
recorder alone. Re-transcribed with every raider shown
(`PLAYER_INFO_LOCAL_PLAYER_ONLY=false`), `tools/rsprox_xarpus_splash.py` takes
every raider's tile at the lock:

| Landings | Splashes went to |
|---|---|
| duo, the other raider on another tile (337) | 1 splash: that raider 182, random 3; 2 splashes: that raider + random 152 |
| trio, three tiles (95) | 1: another raider 46; 2: both others 46, another + random 3 |
| four, four tiles (42) | the others' tiles, 59 of 59 |
| four, **two raiders on the spit's tile** (2) | 0 splashes once; once 1, to a raider elsewhere. The target's tile-mate got none |
| solo (34) | random tiles, 45 of 45 |

**The whole party on one tile** never happened in these kills (every party
landing had at least two tiles). With no other raider off the spit's tile there
is no raider to splash, so every splash goes to a random tile, as solo, and as
the wiki says ("in the event that all players are on the same tile ... the
poison will splatter a random uncovered tile").

**The random tiles are uniform over the 15x15 arena, pooled tiles included:**
all 203 random splashes fell inside it, 42 of them (21%) on a pool while 23% of
the arena was pooled; some fell under him. The wiki's "uncovered" is not what
the server does.

**Open:** in a duo whose other raider stood under him, the single splash went
to a random tile 3 times of 20 instead of to them (outside his footprint: 317
of 317 to them). Distance does not separate the three. Ours always splashes
them.

## Hard: the opening ring

Only three Hard Xarpus rooms are recorded: 1852 (two) and 1837 (one), all
Leagues V. The local index and rsprox.net agree on that (npc 10770-10773).
All three are identical, packet for packet. F is the tick he turns into his
feeding form (10770 → 10771).

| | Recorded | Ours before | Status |
|---|---|---|---|
| The ring | **114 `tob_xarpus_acidspit` (1555)** from his centre on F, one per ring tile. Start 30, **end 90 at every distance**, angle 16, progress 32, height 100 → 0 | no projectile: 98 pools laid on F + 3 | **fixed** |
| Order | south rows z 0 then 1 (the gap skipped), north rows z 14 then 13, then the west columns x 1 then 0 and the east columns x 14 then 13, the full height of each. So the four 2x2 corners take two spits each | | **fixed** (`~tob_xarpus_hard_ring_walk`) |
| Shape | the outer two rings of the 15x15 arena, less the 3x2 gap at the entrance: 98 tiles | same | match |
| Landing, F + 3 | 98 pools with `acid_floor_hit` (range 7); the 16 doubled spits land on a pool and send `player_acid_burn` instead; 114 acidsplashes; no splash is thrown | pools only | **fixed** (through `~tob_xarpus_acid_lands`, the spit's own landing) |
| Hit | a raider beside a ring tile takes the poison splat ("You have been poisoned!", 5). Once, in 1852, it was a raider who walked in beside the gap | none on landing | **fixed** |
| Boil | `acid_splat_loop` on the 98 pools on F + 4 (F + 5 once) | F + 4 | match |
| Message | none | "The floor of the arena boils with acid." | **fixed** (removed) |

A scriptrun of `tob_xarpus_hard` (seed f1) now sends the 114 spits on the
retype tick, in the recorded order, with the recorded fields, and lands them
on F + 3.

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

## Damage (2026-10-19)

These are read from **14 sessions re-transcribed with every raider shown**:
3299, 1833, 1837, 1764, 3285, 788, 582, 1779, 553, 554, 1758 and 1163 for
Normal; 1852 and 1837 for Hard; 101 and 554 for Entry. Every one is from a
seasonal world. Leagues V takes about a tenth off what a raider is dealt
(maiden.md, "Damage, heals and her adds"), so each Normal figure below sits
about 0.9 of ours.

| | Recorded | Ours | Status |
|---|---|---|---|
| Standing on a pool | a poison splat **every tick** they stay (67 of 70 raider-ticks), 2–7 | the same: the ground sweep, every tick | match |
| Stepping onto a pool | no splat that tick (15 of 18); it starts the next | the crossing's delayed hit, a tick later | match |
| An exhumed underfoot (phase 1) | nothing (482 of 482) | nothing | match |
| Pool and spit poison | 2–7 (Normal), 1–10 (Hard) | 4–8, raised by the share absorbed in phase 1 | match after the ~0.9 |
| Guano (a raider under him) | two **damage** splats the next tick (29 of 45), 3–7 each | two, 2–8 | match |
| Stare: the retaliation | **one poison splat** with the line, 41–72 in Normal where the raider's hitpoints did not cap it; Hard 50–74 | 50–75, raised by the absorbed share, as a poison splat | match after the ~0.9 |
| Stare: how many | one line and one splat a tick. The only tick on which the recorder landed two splats of their own drew one | one per hitsplat landed [wiki "per hit"] | **open**: one sample |

The pools are reset at each of his spawns. Instance tiles translate to the same
coordinates every raid, so a reading that keeps them across raids counts
earlier fights' pools as live, and finds "standing on a pool" nearly harmless.

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
- **The splashes:** a stack locks one tile and has no other raider off it, so
  its splashes go to random tiles; each is dodged once its projectile is seen
  (a chain in flight). The old forecast "back onto the spit's own pool at L"
  matched our content's bug, not the server, and is gone.
- **The stack phase:** holds until the last spit + 6 (`_xarp_phase`).
- **The screech:** matched on the prefix `Scree`. The old match was the
  four-e spelling, which never saw the recorded five-e one.

The stack strategy itself was kept: the stack locks one tile, so the spit
lands two tiles from where the stack has moved, and its splashes scatter to
random tiles across the arena.

## Open
- **`apple_splat` loops 2** needs a loops argument on the engine's
  `sound_area`.
- **The hit sound** (`tob_xarpus_hit`, 4018) is recorded as a synth to the
  attacker. Ours plays it as the npc's own sound.
- **The debug readout** `tob_selftest.rs2 tobgates` counts open exhumeds from
  registers 46–49. In phase 2 these hold the lock's splash tiles.
