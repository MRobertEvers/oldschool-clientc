# scavenger_small room status

Branch: `cursor/cox-scavenger-small-da39`

Replacement orchestrator (prior `bc-231b0847` stuck RUNNING, empty transcript).

## Strategy (owner 2026-10-07)

**Solo kill-beast** (synq [0:10:01] / COX_MECHANICS §15): land in the small
scavenger room, kill the beast unprotected for max-hit samples, verify bones +
two drop rolls. Not a shortcut room (large only).

## Implemented

- Content: `cox_scavengers.rs2` (party count, scatter spawn, 2-tick respawn,
  drop table, large-room shortcut) — prior parity pass; ownership unchanged
- Spec: `encounters/scavenger_small.tsv` (hp / cadence / max_hit / drop_rolls)
- Test SM: `LAND → MEASURE → ENGAGE → FIGHT → LOOT → DONE`
  (`test/raids/cox_scavenger_small.lua`)

## Next

- Gate under `flock /tmp/cox_raid_gate.lock`:
  `python3 tools/raid_gate/run.py cox_scavenger_small --no-publish`
  then `gate.py` + `raid_coverage.py` FULL
