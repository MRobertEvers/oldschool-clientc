# Olm room status

Branch: `cursor/cox-raid-rooms-da39`

## Strategy (owner 2026-10-07)

1. **Solo Melee 4-tick 4:1** first (`synq_transcript.md` [2:48:05])
2. Then **duo**, then **trio** — each its own harness
3. Explicit state machine (not scythe spam)

## Implemented

- `test/raids/cox_olm_solo_4t41.lua` — SM:
  `ENTER → WAIT_SPAWN → KILL_MAGE → SETUP_41 → CYCLE_{TANK,FREE,RUN,TURN} → WAIT_PHASE → HEAD → DONE`
- Spec table: `encounters/olm.tsv`
- Older `cox_olm.lua` spam harness superseded for the kill path by `cox_olm_solo_4t41`

## Next

- `flock /tmp/cox_raid_gate.lock` → `run.py cox_olm_solo_4t41 --no-publish`
- After green: `cox_olm_duo.lua`, `cox_olm_trio.lua`
