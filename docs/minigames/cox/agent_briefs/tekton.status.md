# Tekton room status

Branch: `cursor/cox-tekton-runaround-da39`
OSRS-Content: `cursor/cox-tekton-stand-da39` @ `5f7165364`
Agent: replacement for stuck `bc-d8bc5247`

## Strategy

Synq run-around; Protect from LAND; adamant opener → anvil → DWH REENGAGE.

## Content

- npc_var phase state (AI timers); set after `npc_changetype`
- packed anvil pinned at spawn; stand at `anvil - spawn_gap`

## Gate

- Iterating `flock` + `QUEST_BINARY=src/torirs_tekton --no-build`
