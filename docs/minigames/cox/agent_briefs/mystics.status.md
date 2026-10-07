# Mystics room status

Branch: `cursor/cox-mystics-solo-da39`

Replacement for stuck agent `bc-d3cf9a9c` (empty transcript, no commits).

## Strategy (owner 2026-10-07)

**Synq learner solo** (`synq_transcript.md` [0:31:54]): Protect from Magic,
ranged + salve, blowpipe (tbow weaker on mystic magic level), focus one
mystic at a time until the room clears. Flick Protect from Melee when a
mystic is walk-adjacent (50/50 melee reroll). Corner safespot optional.

## Implemented

- Test SM: `LAND → ARM_PRAYER → FOCUS → DONE` (tick-loop FOCUS, no await_dead)
- Kit: loaded toxic blowpipe + salve + masori; shark + karambwan combo eat + brews
- Spec table: all six kill-path rows grade C
- Content: existing mystic procs in `cox_minions.rs2` (no shaman edits)

## Next

- Gate under `flock /tmp/cox_raid_gate.lock` with `--no-build` + private
  `QUEST_BINARY` when pack busy
