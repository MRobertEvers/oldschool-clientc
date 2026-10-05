# infernostats: InfernoStats/InfernoStats (RuneLite plugin hub, `inferno-stats`)

Pinned 2026-10-03 by the waves loop corpus worker (`corpus.code`). Entry in `../LEDGER_code.md`; every constant below is a row of `../CODE_CONSTANTS.md` (the row id is in the first column).

* **Repository**: https://github.com/InfernoStats/InfernoStats
* **Commit**: 0efd7441d0a33486bbf2f13fd54cea846f426ee8 (committed 2026-06-24; equals the plugin-hub pin)
* **Licence**: BSD 2-Clause, Copyright (c) 2021 InfernoStats (`LICENSE`).
* **What it is for**: records per-wave splits and tick loss; the trainer replays its spawns.
* **Files copied** (byte for byte, licence header intact): `src/main/java/com/infernostats/` InfernoStatsPlugin.java (region ids), model/InfernoNpc.java (npc names and footprint sizes), controller/ChatHandler.java (wave message). The splits panel, tick-loss handler and wave-time defaults are not mechanics and were not copied (the default wave split times in InfernoStatsConfig.java:196-328 are user goals).
* **What it credits as its own sources**: Nothing is cited.

## Constants this source states (name, value, file and line)

The value column is the whole row of `../CODE_CONSTANTS.md`; a source often states only part of it (the notes below and the row's last column say which part).

| row | quantity | value | file:line (relative to this directory) |
|---|---|---|---|
| C001 | arena: region id | 9043 (Inferno interior) | `src/main/java/com/infernostats/InfernoStatsPlugin.java:45` |
| C012 | wave table: wave announcement chat line | game message "Wave: <n>" | `src/main/java/com/infernostats/controller/ChatHandler.java:41` |
| C029 | Jal-MejRah: hitpoints / level / size | 25 / 85 / 2 | `src/main/java/com/infernostats/model/InfernoNpc.java:10` |
| C032 | Jal-Ak: hitpoints / level / size / range | 40 / 165 / 3 / 15 | `src/main/java/com/infernostats/model/InfernoNpc.java:11` |
| C040 | Jal-ImKot: hitpoints / level / size / speed / range | 75 / 240 / 4 / 4 / 1 | `src/main/java/com/infernostats/model/InfernoNpc.java:12` |
| C045 | Jal-Xil: hitpoints / level / size / speed | 125 / 370 / 3 / 4 | `src/main/java/com/infernostats/model/InfernoNpc.java:13` |
| C049 | Jal-Zek: hitpoints / level / size / speed | 220 / 490 / 4 / 4 | `src/main/java/com/infernostats/model/InfernoNpc.java:14` |

## Notes

* `model/InfernoNpc.java:10-14` states only the monster names and footprint sizes (bat 2, blob 3, melee 4, ranger 3, mager 4); no hitpoints, levels or ticks.
* `InfernoStatsPlugin.java:45-46` region ids 9043 (Inferno) and 9551 (Fight Caves).
