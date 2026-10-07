# Tekton room status

Branch: `cursor/cox-raid-rooms-da39`

## Strategy (owner 2026-10-07)

**Synq run-around cycle** (`synq_transcript.md` [1:16:04] / [1:17:50]):
lure far from anvil, counterclockwise attacks on pre-corner tiles, spark dodge
on anvil, re-engage. Not stand-and-tank.

## Implemented

- Test SM: `LAND → LURE → CYCLE ⇄ ANVIL_DODGE → REENGAGE → DONE`
- Spec assertions retained from prior harness
- Content: `cox_tekton.rs2` (prior parity pass)

## Next

- Gate under `flock /tmp/cox_raid_gate.lock`
