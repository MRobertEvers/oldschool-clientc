# inferno_timer_maxswa: maxswa/inferno-timer (infernotimer.com, web app)

Pinned 2026-10-03 by the waves loop corpus worker (`corpus.code`). Entry in `../LEDGER_code.md`; every constant below is a row of `../CODE_CONSTANTS.md` (the row id is in the first column).

* **Repository**: https://github.com/maxswa/inferno-timer
* **Commit**: e36b1ca99f2522d74c37694ffa9c2ff6e85fc02a (committed 2023-03-06). Not in the plugin hub.
* **Licence**: no LICENSE file in the repository.
* **What it is for**: a manual Zuk set timer.
* **Files copied** (byte for byte, licence header intact): `src/App.tsx` (SET_SECONDS 210, JAD_SECONDS 105, WARNING_SECONDS 10), UPSTREAM_README.md (the upstream README, renamed). (The helper text "Click anywhere to pause timer. (Once Zuk is under 600 HP)" / "start Jad. (Once Zuk is under 480 HP)" is in `src/utils.ts`, not copied: it is stated in the rows as read at `utils.ts` getHelperText.)
* **What it credits as its own sources**: Nothing is cited.

## Constants this source states (name, value, file and line)

The value column is the whole row of `../CODE_CONSTANTS.md`; a source often states only part of it (the notes below and the row's last column say which part).

| row | quantity | value | file:line (relative to this directory) |
|---|---|---|---|
| C076 | Zuk set timer: pause and resume | pauses once when Zuk is below 600 hp; Jad spawns and the timer resumes below 480 hp with +175 ticks (105 s) | `src/App.tsx:46` |
