# Tekton room status

Branch: `cursor/cox-tekton-runaround-da39`
Agent: `bc-6e8e01ee` (replacement for stuck `bc-d8bc5247`)

## Strategy

**Synq run-around cycle** with Protect from Melee from LAND (BAIT-without-
prayer died under wedge hits). Adamant opener → anvil → DWH on REENGAGE.

## Content

- `cox_tekton.rs2`: re-issue `npc_walk` while `walking_in` (anvil path was
  cancelled after one walk; never reached hammering). Submodule commit
  `919ab24e4` on `cursor/cox-tekton-anvil-repath-da39` (OSRS-Content push
  may still be retrying HTTP 500).

## Harness

- SM: `LAND → LURE → CYCLE ⇄ ANVIL_DODGE → REENGAGE → DONE`
- Shark 8 (unstackable), 6 restores, adamant plate, DWH in pack

## Gate

- Iterating under `flock` + `QUEST_BINARY=src/torirs_tekton`
