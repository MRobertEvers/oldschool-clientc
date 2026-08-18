# The Nylocas — wave table (all 31 waves)

Generated from [`sources/blert_nylocas-waves.json`](sources/blert_nylocas-waves.json),
the dataset behind <https://blert.io/guides/tob/nylocas/mechanics>, cross-checked against
[Theatre of Blood/Strategies/Nylocas](https://oldschool.runescape.wiki/w/Theatre_of_Blood/Strategies/Nylocas)
and against `NYLOCAS_WAVES` in the OpenOSRS `theatre` plugin
([`sources/openosrs_theatre/NyloPredictor.java`](sources/openosrs_theatre/NyloPredictor.java)).

Reading the table:

- **Stall** is the *natural stall*: the number of ticks after wave *n* spawns before wave *n+1*
  is allowed to spawn. Always a multiple of the 4-tick room cycle. A wave only ever spawns on
  cycle tick 0, so the stall is the floor, not the actual gap — a room-cap stall adds 4 more.
- Each lane has **two spawn slots**. `—` means that slot is empty this wave.
- Lower case = small (1x1, level 162). **UPPER CASE BOLD** = big (2x2, level 260).
- `` `*` `` prefix = **aggro**: this nylo walks at the players instead of at a pillar.
  Aggro assignment is fixed, never random. Splits from bigs are never aggros.
- `a→b→c` = a **flicker** chain: the nylo spawns as *a*, switches to *b* after passing the
  lane's halfway point, holds *b* for 2 ticks, then settles on *c*. Flickers start at wave 16.

| Wave | Stall (ticks) | Stall (cycles) | East lane | South lane | West lane |
|---:|---:|---:|---|---|---|
| 1 | 4 | 1 | mel | mag | `*`rng |
| 2 | 4 | 1 | rng | `*`mel | mag |
| 3 | 4 | 1 | `*`mag | rng | mel |
| 4 | 4 | 1 | mel | **MAG** | rng |
| 5 | 16 | 4 | mag | mel | **RNG** |
| 6 | 4 | 1 | **MEL** | rng | mag |
| 7 | 12 | 3 | mel | mag + `*`**RNG** | — |
| 8 | 4 | 1 | rng | mel | `*`**MAG** |
| 9 | 12 | 3 | mag | — | `*`**RNG** + mel |
| 10 | 8 | 2 | `*`rng + **RNG** | rng + rng | rng + `*`rng |
| 11 | 8 | 2 | `*`mag + mag | mag + mag | `*`**MAG** |
| 12 | 8 | 2 | `*`mel + mel | **MEL** | mel + `*`mel |
| 13 | 8 | 2 | `*`**MEL** | rng + mel | rng + `*`mag |
| 14 | 8 | 2 | `*`**RNG** | mag + rng | mag + `*`mel |
| 15 | 8 | 2 | `*`mag + rng | **MAG** | mel + `*`rng |
| 16 | 4 | 1 | mag→mel→rng | mel→mag→rng | rng→mag→rng |
| 17 | 12 | 3 | **MAG→MEL→MAG** | **MAG→MEL→MAG** | **MAG→MEL→MAG** |
| 18 | 8 | 2 | **RNG→MAG→RNG** | **RNG→MAG→RNG** | `*`**RNG→MAG→RNG** |
| 19 | 12 | 3 | `*`**MAG→MEL→MAG** | **MAG→MEL→MAG** | **MAG→MEL→MAG** |
| 20 | 16 | 4 | **MEL→RNG→MEL** | `*`**MAG→RNG→MEL** | **MEL→RNG→MEL** |
| 21 | 8 | 2 | rng→mel→rng + `*`rng→mel→rng | mel→mag→mel + `*`mel→rng→mel | mag→rng→mag + mag→mel→rng |
| 22 | 12 | 3 | `*`**MAG→RNG→MEL** | rng→mag→mel + mag→rng→mel | **MEL→RNG→MEL** |
| 23 | 8 | 2 | **MAG→RNG→MEL** | `*`**RNG→MAG→MEL** | mag→rng→mel + rng→mag→rng |
| 24 | 8 | 2 | `*`**MEL** | `*`**MAG** | `*`**RNG→MAG→MEL** |
| 25 | 8 | 2 | **MAG→MEL→MAG** | **RNG** | `*`**MEL** |
| 26 | 4 | 1 | **MAG** | **MEL→MAG→MEL** | `*`**MAG** |
| 27 | 8 | 2 | **MAG→MEL→MAG** | `*`**MEL→MAG→RNG** | **MAG** |
| 28 | 4 | 1 | mag→mel→mag + rng→mag→mel | mel→rng→mel + mag→mel→rng | mel→mag→mel + `*`rng→mel→mag |
| 29 | 4 | 1 | mag→rng→mel + `*`rng→mel→mag | **MEL** | `*`mel→rng→mag + rng→mel→rng |
| 30 | 4 | 1 | `*`**MAG** | mel→rng→mel + mel→rng→mel | **RNG→MEL→RNG** |
| 31 | 0 | 0 | mag→rng→mag + rng→mel→rng | mel→mag→rng + mag→mel→rng | mel→rng→mag + rng→mag→rng |

Sum of natural stalls, waves 1-30: **232 ticks** (139.2 s). Wave 31 has no successor,
so its stall is 0. With no room-cap stalls at all, the last wave therefore spawns
`4 + 232 = 236` ticks after the room starts — the theoretical floor for the wave phase.

In Hard Mode the demi-boss waves (10, 20, 30) always carry a natural stall of 16 ticks
(4 cycles) regardless of the value above, and each spawns a Nylocas Prinkipas alongside
the listed nylocas.


---

## Which pillar each spawn attacks

**Measured, not published.** No source in [`sources/`](sources/) states the
spawn→pillar assignment; this table was derived from **199 recorded raids** off
blert's public event API by following every nylo from its spawn tile to the tile
it stops on. Method and provenance:
[`tools/derive_tob_nylo_pillars.py`](../../../tools/derive_tob_nylo_pillars.py),
raw result in
[`sources/blert_nylo_pillar_assignment.json`](sources/blert_nylo_pillar_assignment.json)
(which also lists the 199 raid uuids, so the measurement can be re-fetched).

Cells are in the same order as the wave table above, so the two read side by side.

- `NW` `NE` `SW` `SE` — the pillar this spawn walks to and attacks. Every raid in
  which that nylo lived long enough to reach a pillar agreed; there are **no
  conflicting rows**.
- `` `*` `` — an **aggro**: it walks at the players and never touches a pillar.
  The 35 cells marked here are exactly the 35 spawns blert's wave table flags as
  aggro, recovered independently (they change into `tob_nylocas_fighting_*`
  8348–8353 instead of ever standing beside a pillar).
- `?` — fewer than 5 raids produced a pillar contact for that spawn, because it is
  almost always killed in the lane. The pillar was consistent in all of them, but
  the row is thin: waves 6 south, 6 west, 7 south.

| Wave | East lane | South lane | West lane |
|---:|---|---|---|
| 1 | NW | NE | `*` |
| 2 | SE | `*` | NE |
| 3 | `*` | SE | NE |
| 4 | NE | SW | NW |
| 5 | NE | SE | SW |
| 6 | SE | NW? | NE? |
| 7 | NW | NE? + `*` | &mdash; |
| 8 | SE | NW | `*` |
| 9 | NW | &mdash; | `*` + NE |
| 10 | `*` + NE | SE + SW | NW + `*` |
| 11 | `*` + NE | SE + SW | `*` |
| 12 | `*` + NE | SW | NW + `*` |
| 13 | `*` | SE + SW | NW + `*` |
| 14 | `*` | SE + SW | NW + `*` |
| 15 | `*` + NE | SW | NW + `*` |
| 16 | NE | SW | NW |
| 17 | NE | SE | SW |
| 18 | NW | SW | `*` |
| 19 | `*` | SE | NE |
| 20 | NE | `*` | NW |
| 21 | NE + `*` | SE + `*` | NW + SW |
| 22 | `*` | SE + SW | SW |
| 23 | NE | `*` | NW + SW |
| 24 | `*` | `*` | `*` |
| 25 | SE | SW | `*` |
| 26 | NE | SW | `*` |
| 27 | SE | `*` | NW |
| 28 | NE + SE | SE + SW | NW + `*` |
| 29 | NE + `*` | SE | `*` + SW |
| 30 | `*` | SE + SW | SW |
| 31 | NE + SE | SE + SW | NW + SW |

**The assignment is data, not geometry.** It cannot be computed from the spawn
tile, the lane or the nearest pillar, and any implementation that tries will be
wrong on most of the table:

- the same tile feeds different pillars in different waves — west (3281, 4249)
  attacks NE on wave 2 and NW on wave 4;
- nylos routinely cross the whole room — east (3310, 4249) attacks the **NW**
  pillar on wave 1, and west (3281, 4248) attacks the **NE** pillar on waves 3, 9
  and 19;
- lanes do not own pillars. Of the 85 pillar-bound spawns, the east lane sends 15
  to NE, 7 to SE and **4 to NW**; the south lane 15 to SW, 14 to SE and **4 to the
  north pair**; the west lane 13 to NW, 8 to SW and **5 to NE**.

Load across the four, counting spawns rather than damage: SW 23, NE 22, SE 21, NW 19.

### Spawn tiles

Both slots of a lane spawn on adjacent tiles; a big is 2×2 and covers both, and is
reported on the lower-coordinate one. Slot order is blert's, resolved against the
tiles by matching size and style in the 25 lanes where the two spawns differ.

| Lane | Slot 0 | Slot 1 |
|---|---|---|
| East | (3310, 4249) | (3310, 4248) — big at (3309, 4248) |
| South | (3296, 4233) | (3295, 4233) — big at (3295, 4233) |
| West | (3281, 4249) | (3281, 4248) — big at (3281, 4248) |

### Pillar footprints

Each support is **2×2**, and its tiles are impassable — over 199 raids no player
or npc ever occupied one, which is how the footprints below were fixed rather than
assumed. Nylos attack from the two room-facing sides only.

| Pillar | Footprint | Attack tiles |
|---|---|---|
| NW | (3290–3291, 4253–4254) | (3292, 4253–4254) and (3290–3291, 4252) |
| NE | (3300–3301, 4253–4254) | (3299, 4253–4254) and (3300–3301, 4252) |
| SW | (3290–3291, 4243–4244) | (3292, 4243–4244) and (3290–3291, 4245) |
| SE | (3300–3301, 4243–4244) | (3299, 4243–4244) and (3300–3301, 4245) |

These are the same tiles as `PillarCorner` in the Zenyte/Near-Reality server tree
([`room/nylocas/model/PillarLocation.kt`](https://github.com/Winktabulous/regarded-dev/blob/main/plugins/excluded/theatreofblood/src/main/kotlin/com/zenyte/game/content/theatreofblood/room/nylocas/model/PillarLocation.kt)),
which is the only other place they are written down. That tree spawns the four
supports and nothing else of the wave phase — its `PillarLocation.getRandom()` is
a private-server shortcut, and is **not** evidence about the assignment.

### One correction to the wave table above: wave 30, south lane

The measurement audited its own inputs. Matching all 199 raids' spawns back onto
blert's 120 slots by size and style, **119 of 120 agree**. The one that does not is
the second south spawn of wave 30, which the table above renders from blert's data
as `mel→rng→mel`:

- the game spawns npc **8344** `tob_nylocas_incoming_magic` on (3295, 4233) there,
  in **197 of the 197** recorded raids that reached wave 30 — it starts **magic**;
- the [Wiki's wave table](https://oldschool.runescape.wiki/w/Theatre_of_Blood/Strategies/Nylocas)
  agrees, spelling that cell `mag→rng→mel`;
- only blert's `nylocas-waves.json` says `melee→ranged→melee`.

So `blert_nylocas-waves.json` is wrong in that one cell, and `tob_nylo_spawn` —
generated from it — inherits the error and would have players null the spawn.
Registered as **C9** in the plan's conflicts register.
