# Tekton room status

Branch: `cursor/cox-tekton-runaround-da39`
OSRS-Content: `cursor/cox-tekton-stand-da39` @ `08550b617` (pushed; local
compile tip `c7c6614de` on dda1 lineage when main-based tree lacks sibling fixes)
Agent: replacement for stuck `bc-d8bc5247`

## Strategy

**Synq run-around cycle** with Protect from Melee from LAND. Adamant opener
→ anvil → DWH on REENGAGE.

## Content

- `cox_tekton.rs2`: re-issue `npc_walk` while `walking_in`; stand at
  `anvil - ^cox_tekton_spawn_gap` so the 4x4 footprint clears the 6x4
  blockwalk anvil (anvil SW and anvil-1z never arrive).

## Harness

- SM: `LAND → LURE → CYCLE ⇄ ANVIL_DODGE → REENGAGE → DONE`
- Shark 8, 6 restores, adamant plate, DWH on REENGAGE

## Gate

- Iterating under `flock` + `QUEST_BINARY=src/torirs_tekton --no-build`
