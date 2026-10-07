# Tekton room status

Branch: `cursor/cox-tekton-runaround-da39`
OSRS-Content: `cursor/cox-tekton-anvil-stand-da39` @ `abbd7c7d6c4d19de4a5945ae77456536ab39c9ab`

## Content

- closest-of-three anvil stand; `ai_queue4` arrival; `npc_var` state across changetype
- flat sparks 10–20 (`damage(uid)` with npc_uid; no magic `gear_reduce` zeroing)

## Harness

- Synq SM: LAND → LURE → CYCLE ⇄ ANVIL_DODGE → REENGAGE
- ANVIL_DODGE: dodge-first (±4 every 2 ticks) then mage if HP ≥ 60 (sparks ignore melee prayer)
- brew/shark sustain; adamant opener + DWH on REENGAGE; Protect from Melee from LAND
- spark volley = first anvil session gap > 3 with dmg ≥ 10

## Gate

- Private binary: `QUEST_BINARY=/workspace/src/torirs_tekton`
- Last: run18 FAIL `player.died` tick 165 (flat sparks real; mage-before-dodge)
- Next: flock `run.py cox_tekton --no-build --no-publish` after dodge-first harness
