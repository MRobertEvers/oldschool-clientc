# Tekton room status

Branch: `cursor/cox-tekton-runaround-da39`
Agent: `bc-6e8e01ee` (replacement for stuck `bc-d8bc5247`)

## Strategy (owner 2026-10-07)

**Synq run-around cycle** (`synq_transcript.md` [1:16:04] / [1:17:50]):
lure far from anvil, counterclockwise attacks on pre-corner tiles, spark dodge
on anvil, re-engage. Not stand-and-tank.

## Implemented

- Test SM: `LAND → LURE → CYCLE ⇄ ANVIL_DODGE → REENGAGE → DONE`
- Spec assertions retained from prior harness (`test/raids/cox_tekton.lua`)
- Content: `cox_tekton.rs2` (prior parity pass; submodule `dda1f15`)
- Private binary: `src/torirs_tekton` (`QUEST_BINARY`, `GL_GLEXT_PROTOTYPES`)

## Next

- Gate under `flock /tmp/cox_raid_gate.lock`
  `QUEST_BINARY=src/torirs_tekton python3 tools/raid_gate/run.py cox_tekton --no-build --no-publish`
  then `gate.py` + coverage FULL + play screenshots
