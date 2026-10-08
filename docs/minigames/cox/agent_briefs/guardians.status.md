# Guardians room status

Branch: `cursor/cox-raid-rooms-da39`

## GREEN (run17)

- `run.py` exit 0 — ledger **27 PASS / 0 FAIL** (ticks 1354)
- `gate.py` — `cox_guardians green`
- `raid_coverage.py` — **FULL** (8/8 in-scope spec rows)
- Shots: `/opt/cursor/artifacts/cox_guardians_*.png` (33)

## Implemented

- `cox_guardians.rs2`: pickaxe-only damage, 4-tick cadence, 3×3 stomp with
  1-tick dodge, flinch (`floor(rate/2)`), gap pushback, 8-tick regen, solo HP
  250; typed `npc_findall(coord, npc, distance, checkvis)`; stomp resolve
  passes `npc_uid` into the delayed queue.
- Spec: `encounters/guardians.tsv` + `.scope.tsv` (8 rows, scope `all`).
- Driver: `test/raids/cox_guardians.lua` via `t.raid.enter("cox","guardians",{seed=1})`,
  outside-face flinch fight, whip→0 / pickaxe damage / both statues down /
  `spec.guardians.*`.

## Evidence

<TextReference path="/opt/cursor/artifacts/cox_guardians_run17.log" start={1} end={40} alt="run17 gate tail with FULL coverage"></TextReference>

**No commit** (room orchestrator).
