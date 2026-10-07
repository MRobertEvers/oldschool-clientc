# Tekton room status

Branch: `cursor/cox-tekton-runaround-da39`
OSRS-Content: `cursor/cox-tekton-anvil-stand-da39` @ `63b4cdd9c` (spark finduid; remote push HTTP 500)

## Content

- closest-of-three anvil stand; `ai_queue4` arrival; `npc_var` state across changetype
- flat sparks 10–20; `npc_finduid` before `damage()` so ticklog dealer/type tag hammering

## Harness

- Synq SM + early DWH special; first anvil tank-for-sample + mage; later anvils dodge
- spark attribution accepts hammer type or typeless (-1) in first anvil window

## Gate

- Private binary: `QUEST_BINARY=/workspace/src/torirs_tekton`
- run19: survived anvil dodges; FAIL kill/water/spark (type -1) / hp_solo / cadence outliers
- Next: sscompile finduid + DWH/tank harness → flock run20
