# Vanguards room agent status

Branch: `cursor/cox-vanguards-gate-da39`
Replacement for stuck agent `bc-a4e27b30` (empty transcript / no updates).

## Strategy (owner 2026-10-07)

**Synq learner balance method** (`synq_transcript.md` [1:38:50]–[1:42:08]):
all three attack styles on the weakness triangle (mage→ranged, melee→magic,
ranged→melee); always hit the highest-HP combat form so the 40% spread heal
does not fire on the kill path. Probe one forced heal early to measure
`heal_threshold_small`.

## Implemented

- Content (prior pass): `cox_vanguards.rs2` — dormant→rise→combat, AoE×3,
  shuffle 20–36, force-heal, survive-at-1, stomp.
- Spec: `encounters/vanguards.tsv` (`spec_check` ok, 7 rows).
- Test: `test/raids/cox_vanguards.lua` — SM:
  `LAND → WAKE → PROBE_HEAL → BALANCE ⇄ SHELL → DONE`

## Next

- Private `QUEST_BINARY=src/torirs_qd_vang` build (`build_qd_vang_opt_es`).
- Gate under `flock /tmp/cox_raid_gate.lock`:
  `run.py cox_vanguards --no-publish` then `gate.py` + `raid_coverage.py`
  (FULL).
- Copy play shots to `/opt/cursor/artifacts/cox_vanguards_*.png`.
