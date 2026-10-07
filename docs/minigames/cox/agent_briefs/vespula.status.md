# Vespula room agent status

Branch: `cursor/cox-raid-rooms-da39`

## Strategy (owner 2026-10-07)

**Synq redemption method** for the solo script (`synq_transcript.md` [1:25:36] /
[1:29:03]): Redemption + eagleeye/augury, safe tile outside her range, attack
portal → step back, restores as needed. Not ground-then-portal face-tank.

## Implemented

- `cox_vespula.rs2` mechanics (prior pass): enrage, portal, grubs, grounding.
- Spec: `encounters/vespula.tsv` (`spec_check` ok).
- Test: `test/raids/cox_vespula.lua` — explicit SM:
  `LAND → ARM_PRAYERS → TO_SAFE → ATTACK_PORTAL ⇄ STEP_SAFE / RESTORE → DONE`

## Next

- Gate under `flock /tmp/cox_raid_gate.lock`: `run.py cox_vespula --no-publish`
- Copy play shots to `/opt/cursor/artifacts/cox_vespula_*.png`
