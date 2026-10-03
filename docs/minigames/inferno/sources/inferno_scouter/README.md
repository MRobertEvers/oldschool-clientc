# inferno_scouter: jeremiah855/inferno-scouter (RuneLite plugin hub, `inferno-scouter`)

Pinned 2026-10-03 by the waves loop corpus worker (`corpus.code`). Entry in `../LEDGER_code.md`; every constant below is a row of `../CODE_CONSTANTS.md` (the row id is in the first column).

* **Repository**: https://github.com/jeremiah855/inferno-scouter
* **Commit**: ff025377ea34d01c30d3bace220b9b14e359cfcb (committed 2026-07-14; equals the plugin-hub pin in `plugins/inferno-scouter`)
* **Licence**: BSD 2-Clause, Copyright (c) 2026 jeremiah855 (`LICENSE`).
* **What it is for**: reads the nine spawn tiles on the first tick of each wave and prints a nine-letter code.
* **Files copied** (byte for byte, licence header intact): `src/main/java/com/infernoscouter/InfernoScouterPlugin.java` (spawn tiles, npc ids, pillar ids, grid, first-tick spawn batching), UPSTREAM_README.md (the upstream README, renamed).
* **What it credits as its own sources**: States it mirrors the inferno-2d-map plugin's pillar object ids and coordinate matching (InfernoScouterPlugin.java:72).

## Constants this source states (name, value, file and line)

The value column is the whole row of `../CODE_CONSTANTS.md`; a source often states only part of it (the notes below and the row's last column say which part).

| row | quantity | value | file:line (relative to this directory) |
|---|---|---|---|
| C001 | arena: region id | 9043 (Inferno interior) | `src/main/java/com/infernoscouter/InfernoScouterPlugin.java:58` |
| C002 | arena: playable grid | 29 x 30 tiles | `src/main/java/com/infernoscouter/InfernoScouterPlugin.java:61`, `src/main/java/com/infernoscouter/InfernoScouterPlugin.java:62` |
| C003 | arena: scout-grid transform | x = regionX - 17; y = 46 - regionY (SW tile of a footprint) | `src/main/java/com/infernoscouter/InfernoScouterPlugin.java:59`, `src/main/java/com/infernoscouter/InfernoScouterPlugin.java:60` |
| C004 | ids: npc ids Jal-Nib / Jal-MejRah / Jal-Ak / bloblets | 7691 / 7692 / 7693 / 7694 (mage) 7695 (range) 7696 (melee) | `src/main/java/com/infernoscouter/InfernoScouterPlugin.java:82`, `src/main/java/com/infernoscouter/InfernoScouterPlugin.java:83` |
| C005 | ids: npc ids Jal-ImKot / Jal-Xil / Jal-Zek (final-wave variant) | 7697 / 7698 (7702) / 7699 (7703) | `src/main/java/com/infernoscouter/InfernoScouterPlugin.java:84`, `src/main/java/com/infernoscouter/InfernoScouterPlugin.java:86` |
| C007 | ids: npc ids TzKal-Zuk / shield (Ancestral Glyph) / Jal-MejJak / pillar placeholder / dying pillar | 7706 / 7707 (INFERNO_MOVING_SAFESPOT) / 7708 / 7709 / 7710 | `src/main/java/com/infernoscouter/InfernoScouterPlugin.java:74` |
| C008 | ids: pillar loc ids | 30353, 30354, 30355 | `src/main/java/com/infernoscouter/InfernoScouterPlugin.java:73` |
| C013 | spawn: candidate spawn tiles (region-local SW tile of the footprint) | (18,41) (39,41) (20,35) (40,34) (33,29) (22,23) (40,21) (18,18) (32,18); world = region + (2240, 5312) [derived] | `src/main/java/com/infernoscouter/InfernoScouterPlugin.java:1091` |
| C021 | pillar: size and positions | 3x3; south (21,37), west (11,23), north (28,21) in the trainer frame = region SW tiles (27,23) (17,37) (34,39) | `src/main/java/com/infernoscouter/InfernoScouterPlugin.java:70` |

## Notes

* States ids, region, grid, spawn tiles and pillar ids; no timing. `InfernoScouterPlugin.java:258-270` reads spawns only within 2 client ticks of the wave message (`tick - pendingWaveStartTick > 2` is dropped).
