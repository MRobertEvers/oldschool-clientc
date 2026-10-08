# Tightrope room status

- Branch: `cursor/cox-tightrope-traversal-da39`
- Strategy (owner handoff 2026-10-07): **COX_MECHANICS §13 rope traversal**
  — passive until Cross, one-tick landing dump, keystone Dispel clears
  survivors. Not kill-guards; not phoenix-necklace skip.
- Test: `test/raids/cox_tightrope.lua` — SM:
  `LAND → PASSIVE → CROSS → TAKE → RETURN → DISPEL → DONE`
- Content: `cox_puzzles.rs2` (tightrope procs: step_on forcemove, dump, resume)
- Spec: `encounters/tightrope.tsv` (9 rows, `spec_check` ok)
- Gate: **green** — `raid_coverage` FULL (9/9)
