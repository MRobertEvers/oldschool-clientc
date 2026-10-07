# Tekton room status

Branch: `cursor/cox-tekton-runaround-da39`
OSRS-Content: `cursor/cox-tekton-stand-da39` @ `0a941539a`
Agent: replacement for stuck `bc-d8bc5247`

## Strategy

**Synq run-around cycle** with Protect from Melee from LAND (BAIT-without-
prayer died under wedge hits). Adamant opener → anvil → DWH on REENGAGE.

## Content

- `cox_tekton.rs2`: re-issue `npc_walk` while `walking_in`; walk target is
  `~cox_tekton_anvil_stand_coord` = one step south of anvil SW (anvil is
  6x4 blockwalk — walk to loc SW never arrives / never hammers).

## Harness

- SM: `LAND → LURE → CYCLE ⇄ ANVIL_DODGE → REENGAGE → DONE`
- Shark 8 (unstackable), 6 restores, adamant plate, DWH on REENGAGE

## Gate

- Iterating under `flock` + `QUEST_BINARY=src/torirs_tekton --no-build`
