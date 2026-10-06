# How real trios handle Maiden's crabs (Blert, 26 Regular-mode scale-3 rooms, 2026-10-06)

Streams: 26 Maiden rooms, mode 11 (Regular), scale 3, fetched from blert.io at one request
per three seconds (`fetch.py`; uuids in `rooms.json`; the streams themselves are cached under
`build/blert_maiden/<uuid>.json`, not committed). Read with `analyse.py`. A crab's wave is
NPC_SPAWN's `maidenCrab.spawn` (0 = 70%, 1 = 50%, 2 = 30%); killed = NPC_DEATH at hp 0 with no
leak event; leaked = a TOB_MAIDEN_CRAB_LEAK (type 100) event; "frozen" is INFERRED (a crab whose
tile does not change for 20+ ticks while alive: Blert never marks a freeze); weapons from
Blert's attack_definitions.json.

Per threshold, median [min-max] across the 26 rooms:

| | 70% | 50% | 30% |
|---|---|---|---|
| Crabs spawned | 6 | 6 | 6 |
| Killed before reaching her | 5 [1-6] | 4 [1-6] | 0 [0-6] (undercount: she dies) |
| Leaked (leak event) | 1 [0-5] | 2 [0-5] | 0.5 [0-6] |
| Stationary 20+ ticks (inferred frozen) | 5 [0-6] | 3 [0-5] | 4 [1-6] |
| Ticks, spawn to first barrage hit on a crab | 1 [1-11] | 1 [1-16] | 1 [1-7] |
| Ticks, spawn to first kill | 28 [9-51] | 21 [9-52] | 11.5 [9-32] |
| Ticks, spawn to first leak | 9 [6-52] | 6 [6-52] | 6 [6-41] |
| Wave duration, ticks | 51.5 [31-57] | 52 [25-56] | 43.5 [27-56] |

Room total, start to her death: 164 ticks [132-242].

What the real trios do:
- ONE freezer per room, the same player at all three thresholds in all 26 rooms, casting a
  barrage-type spell at the crabs in all 78 waves: the Accursed sceptre's barrage in about 80
  percent of casts (ids 28266, 28474, 28262), else the Nightmare staff or Kodai; median 4 / 5 /
  3.5 casts per wave. The first barrage HIT lands about one tick after the spawn: the cast is
  pre-aimed at the spawn tile.
- The other two KILL crabs: the scythe on crabs in 25 / 25 / 19 of 26 rooms per threshold, the
  toxic blowpipe in 6 / 7 / 4; minor: Dinh's special, Eye of Ayak, chinchompas, twisted bow.
- LEAKS ARE NORMAL: median 5 leak events per room (0-13); only 1 of 26 rooms had none; no room
  had zero leaks at both 70% and 50%; 41 of 78 waves had one leak or fewer. The first leak is
  typically 6-9 ticks after the spawn (the nearest crab reaches her before the barrage tells).
- "All but one frozen" is NOT what real trios achieve as a rule: 5 of 6 stood still 20+ ticks
  in 26 of 78 waves; the 70% median is 5 of 6.

What the data cannot tell: Ice Barrage from Ice Burst (one attack type); how many crabs each
cast froze (one primary target per event); the leak event misses some leaks (52 crab deaths
with hp above 0 and no leak event).
