# Mystics room status

Branch: `cursor/cox-mystics-solo-da39`

Replacement for stuck agent `bc-d3cf9a9c` (empty transcript, no commits).

## Strategy (owner 2026-10-07)

**Synq learner solo** (`synq_transcript.md` [0:31:54]): Protect from Magic,
ranged + salve, focus one mystic at a time. Twisted bow is acceptable (5-tick
cycle survives await eating better than blowpipe under stacked DPS). Corner
safespot optional.

## Implemented

- Test SM: `LAND → ARM_PRAYER → FOCUS → DONE` (tick-loop FOCUS so brews sip)
- Kit: tbow + salve + masori; brew-first sustain + sharks/karambwan
- Prayer-reduction samples magic-style hits only
- Spec table: all six kill-path rows grade C
- Content: existing mystic procs in `cox_minions.rs2` (no shaman edits)

## Next

- Gate under `flock /tmp/cox_raid_gate.lock` with `--no-build` + private
  `QUEST_BINARY` when pack busy
