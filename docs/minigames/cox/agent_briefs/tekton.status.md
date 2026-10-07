# Tekton room status

Branch: `cursor/cox-tekton-runaround-da39` @ `c1838839c`
OSRS-Content: `cursor/cox-tekton-heal-da39` @ `4e2e6192f` (identical `cox_tekton.rs2` to local anvil-stand tip)

## Content

- closest-of-three anvil stand; `ai_queue4`; `npc_var` state
- flat sparks + `npc_finduid` dealer (no magic gear_reduce zeroing)
- anvil heal only on spark volley ticks

## Harness

- Synq SM: LAND → LURE → CYCLE ⇄ ANVIL_DODGE → REENGAGE
- Protect from Melee from LAND; DWH via `orbs:specbutton`; mage for water window
- anvil shot name without comma; min/max auto rows use `range` tol

## Gate (run30)

```
QUEST_BINARY=/workspace/src/torirs_tekton
flock /tmp/cox_raid_gate.lock python3 tools/raid_gate/run.py cox_tekton --no-build --no-publish
→ exit 0; ledger SUMMARY 26 PASS / 0 fail

python3 tools/raid_gate/gate.py cox_tekton
→ cox_tekton green

python3 tools/raid_gate/raid_coverage.py cox_tekton
→ cox_tekton: FULL (10 in-scope spec rows measured within tolerance)
```

## Play evidence

`/opt/cursor/artifacts/cox_tekton_*.png` — idle/landing, lure, anvil sparks, fallen, synq, survived
