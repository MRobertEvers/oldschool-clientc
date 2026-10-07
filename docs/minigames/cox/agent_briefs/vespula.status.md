# Vespula room agent status

Branch: `cursor/cox-vespula-redemption-run-da39` (base `cursor/cox-raid-rooms-da39`)

## Strategy (owner 2026-10-07)

**Synq redemption method** for the solo script (`synq_transcript.md` [1:25:36] /
[1:29:03]): Redemption + rigor/eagleeye (not eagleeye+augury — they conflict),
safe tile outside her range, attack portal → step back, restores as needed.
Not ground-then-portal face-tank.

## Implemented

- Spec: `encounters/vespula.tsv`
- Test: `test/raids/cox_vespula.lua` SM:
  `LAND → ARM_PRAYERS → TO_SAFE → ATTACK_PORTAL ⇄ STEP_SAFE / RESTORE → DONE`
- Setup: `::give 4doseantipoison 1` (valid obj)
- Safe tiles: rotate cardinal/diagonal candidates at Chebyshev 7 clear of boss
- Stuck detector if no movement before first portal hit

## Gate evidence

| Attempt | Outcome |
|---|---|
| Pack build | RED — sibling `cox_resource_fishing_spot` refused |
| `--no-build` + `antipoison4` | FAIL setup (invalid obj) |
| `--no-build` + `4doseantipoison` | PASS through `room.mark`; then hung on unreachable safe tile until frame budget (`run.unfinished`) |
| Harden SM (candidates + prayer arm check) | queued |

## Next

- Green `run.py` / `gate.py` / `raid_coverage.py`
- Copy play shots to `/opt/cursor/artifacts/cox_vespula_*.png`
