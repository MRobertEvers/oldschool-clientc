# Shamans room status

Branch: `cursor/cox-shamans-wallhug-da39`

## Strategy (owner 2026-10-07)

**Synq wall-hug** (`synq_transcript.md` [0:34:39]–[0:39:04]):
Protect from Missiles, anti-poison, hug walls so the 3×3 jump cannot land,
isolate one shaman at a time with a long-range weapon. Not face-tank melee.

## Implemented

- Content: wire shaman `[ai_timer]` to 1-tick + `~cox_regen_counted` +
  `~cox_shaman_try_spawn_special` + `~cox_shaman_check_tendril_trigger`
  (procs existed; timer never dispatched them)
- Test SM: `LAND → ARM → MEASURE_REGEN → KILL → DONE`
- Spec table: `encounters/shamans.tsv` (7 rows, scope all)

## Next

- Gate under `flock /tmp/cox_raid_gate.lock` → FULL coverage
