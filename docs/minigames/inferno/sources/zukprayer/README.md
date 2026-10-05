# zukprayer: propagating/ZukPrayer (README only)

Pinned 2026-10-03 by the waves loop corpus worker (`corpus.code`). Entry in `../LEDGER_code.md`; every constant below is a row of `../CODE_CONSTANTS.md` (the row id is in the first column).

* **Repository**: https://github.com/propagating/ZukPrayer
* **Commit**: 1743f1d806f62ade70748549d3299d83096376e9 (committed 2026-09-13). Not in the plugin hub manifest at the time of the fetch.
* **Licence**: no LICENSE file in the repository; therefore only the README (the claim of tick 15) is copied, not the source.
* **What it is for**: shows a prayer rotation and the since-login tick counter.
* **Files copied** (byte for byte, licence header intact): UPSTREAM_README.md (the upstream README, renamed).
* **What it credits as its own sources**: Describes itself as a display of a ZukSharp plan (a fork of AUTOZUK).

## Constants this source states (name, value, file and line)

The value column is the whole row of `../CODE_CONSTANTS.md`; a source often states only part of it (the notes below and the row's last column say which part).

| row | quantity | value | file:line (relative to this directory) |
|---|---|---|---|
| C016 | spawn: monsters spawn on tick 15 of the wave (counted since login) | 15 | `UPSTREAM_README.md:5` |

## Notes

* `ZukPrayerPlugin.java:220` (not copied) repeats "Ticks since login (the wave spawns on tick 15)"; `:65` OUTSIDE_TICKS_TO_CLEAR = 5 is a UI debounce, not a mechanic.
