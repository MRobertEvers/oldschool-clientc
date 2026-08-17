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

