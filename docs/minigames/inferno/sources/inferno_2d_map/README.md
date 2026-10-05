# inferno_2d_map: TheRealGuru/inferno-2d-map (RuneLite plugin hub, `inferno-2d-map`)

Pinned 2026-10-03 by the waves loop corpus worker (`corpus.code`). Entry in `../LEDGER_code.md`; every constant below is a row of `../CODE_CONSTANTS.md` (the row id is in the first column).

* **Repository**: https://github.com/TheRealGuru/inferno-2d-map
* **Commit**: 463b31a2989d49d5b310f39fe7f3d01c70999db4 (the plugin-hub pin in `plugins/inferno-2d-map`, committed 2025-07-16; fetched at that commit, the repository head is 52503f8)
* **Licence**: BSD 2-Clause, Copyright (c) 2025 TheRealGuru (`LICENSE`).
* **What it is for**: draws the arena in 2D for a scouting tool.
* **Files copied** (byte for byte, licence header intact): `src/main/java/com/github/therealguru/` Inferno2dMapPlugin.java, Coordinate.java.
* **What it credits as its own sources**: Nothing is cited.

## Constants this source states (name, value, file and line)

The value column is the whole row of `../CODE_CONSTANTS.md`; a source often states only part of it (the notes below and the row's last column say which part).

| row | quantity | value | file:line (relative to this directory) |
|---|---|---|---|
| C001 | arena: region id | 9043 (Inferno interior) | `src/main/java/com/github/therealguru/Inferno2dMapPlugin.java:34` |
| C002 | arena: playable grid | 29 x 30 tiles | `src/main/java/com/github/therealguru/Inferno2dMapPlugin.java:50` |
| C008 | ids: pillar loc ids | 30353, 30354, 30355 | `src/main/java/com/github/therealguru/Inferno2dMapPlugin.java:49` |

## Notes

* `Coordinate.java` NPC coordinate = regionX - 17, regionY - 17; a game object (pillar) = regionX - 18, regionY - 18: a pillar's origin sits one tile SW of an npc footprint's.
