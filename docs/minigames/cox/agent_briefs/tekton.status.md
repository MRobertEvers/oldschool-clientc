# Tekton room status

Branch: `cursor/cox-tekton-runaround-da39`
OSRS-Content: `cursor/cox-tekton-stand-da39` @ `ddd0f11b8`
Agent: replacement for stuck `bc-d8bc5247`

## Strategy

**Synq run-around cycle** with Protect from Melee from LAND. Adamant opener
→ anvil → DWH on REENGAGE.

## Content

- `cox_tekton.rs2`: re-issue `npc_walk` while `walking_in`; stand at
  `anvil - spawn_gap` (4x4 clears 6x4 blockwalk); **npc_var** phase state
  for AI timers (player temp `%varp6749` unbound in `[ai_timer]`); packed
  anvil pinned at spawn.

## Harness

- SM: `LAND → LURE → CYCLE ⇄ ANVIL_DODGE → REENGAGE → DONE`

## Gate

- Iterating under `flock` + `QUEST_BINARY=src/torirs_tekton --no-build`
