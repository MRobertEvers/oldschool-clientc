# Tekton room status

Branch: `cursor/cox-tekton-runaround-da39`
OSRS-Content: `cursor/cox-tekton-stand-da39` @ `81a2b52fa`
Agent: replacement for stuck `bc-d8bc5247`

## Strategy

Synq run-around; Protect from LAND; adamant → anvil → DWH REENGAGE.

## Content

- npc_var phase state after `npc_changetype`
- anvil SW via `loc_find` on CCW/THRU/CW tiles; stand at `-spawn_gap`

## Gate

- Iterating under flock + private `QUEST_BINARY`
