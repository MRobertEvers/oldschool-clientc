# settimer: InfernoStats/SetTimer (RuneLite plugin hub, `set-timer`)

Pinned 2026-10-03 by the waves loop corpus worker (`corpus.code`). Entry in `../LEDGER_code.md`; every constant below is a row of `../CODE_CONSTANTS.md` (the row id is in the first column).

* **Repository**: https://github.com/InfernoStats/SetTimer
* **Commit**: 45a47eb32763087006459e43e865975fdace68fe (committed 2022-08-15; equals the plugin-hub pin)
* **Licence**: BSD 2-Clause, Copyright (c) 2021 InfernoStats (`LICENSE`).
* **What it is for**: counts down to the next Zuk set; on a button it switches to the Jad time.
* **Files copied** (byte for byte, licence header intact): `src/main/java/com/settimer/` SetTimer.java, SetTimerPlugin.java; UPSTREAM_README.md (the upstream README, renamed).
* **What it credits as its own sources**: README: "This is a basic TzKal-Zuk set timer. Inspired by https://bistools.github.io/timers.html".

## Constants this source states (name, value, file and line)

The value column is the whole row of `../CODE_CONSTANTS.md`; a source often states only part of it (the notes below and the row's last column say which part).

| row | quantity | value | file:line (relative to this directory) |
|---|---|---|---|
| C076 | Zuk set timer: pause and resume | pauses once when Zuk is below 600 hp; Jad spawns and the timer resumes below 480 hp with +175 ticks (105 s) | `src/main/java/com/settimer/SetTimer.java:29` |

## Notes

* `SetTimer.java:28-29` setTime 3 * 60 + 30 = 210 s, jadTime 1 * 60 + 45 = 105 s (the rows above cite the lines).
