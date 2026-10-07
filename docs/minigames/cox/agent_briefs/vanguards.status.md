# Vanguards room agent status

Branch: `cursor/cox-vanguards-gate-da39`
Replacement for stuck agent `bc-a4e27b30`.

## Strategy (owner 2026-10-07)

**Synq learner balance method**: weakness triangle; highest-HP focus;
probe one forced heal; isolate on far side of melee/magic (tbow/kodai),
stand under ranged (whip). Pads pinned with `givechase=no` so shuffle
tile-find works.

## Implemented

- Content: `cox_vanguards.rs2` + `cox.npc` givechase=no / defaultmode=none
  on combat forms; shuffle_all slot-var fallback.
- Spec: `encounters/vanguards.tsv` (7 rows).
- Test: `test/raids/cox_vanguards.lua` — LAND→WAKE→PROBE_HEAL→BALANCE⇄SHELL→DONE

## Next

- Gate under flock + `QUEST_BINARY=src/torirs_qd_vang --no-build`:
  green + FULL coverage; copy play shots to `/opt/cursor/artifacts/`.
