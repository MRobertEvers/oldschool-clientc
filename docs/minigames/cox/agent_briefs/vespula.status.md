# Vespula room agent status

Branch: `cursor/cox-vespula-redemption-run-da39` (base `cursor/cox-raid-rooms-da39`)
Replacement orchestrator for stuck `bc-fb0ab973`.

## Strategy (owner 2026-10-07)

**Synq redemption method** for the solo script (`synq_transcript.md` [1:25:36] /
[1:29:03]): Redemption + rigor/eagleeye (not eagleeye+augury — they conflict),
safe/gap tile outside her range, attack portal → step back, restores as needed.
Not ground-then-portal face-tank.

## Implemented

- Spec: `encounters/vespula.tsv` (`spec_check` ok, 12 rows)
- Test: `test/raids/cox_vespula.lua` SM:
  `LAND → ARM_PRAYERS → TO_GAP → ATTACK_PORTAL ⇄ STEP_SAFE / RESTORE → DONE`
  (seed-1 landing is already portal range 6 → ARM skips TO_GAP and attacks)
- Setup: `::give 4doseantipoison 1` (valid obj; not `antipoison4`)
- Step-once away from portal (absolute long walks were a no-op under gate)
- Prayer arm: redemption + rigor, else eagleeye; never both style prayers

## Gate evidence

| Attempt | Outcome |
|---|---|
| Prior: pack build | RED — sibling `cox_resource_fishing_spot` refused |
| Prior: `--no-build` + `antipoison4` | FAIL setup (invalid obj) |
| Prior: `--no-build` + `4doseantipoison` | hung on unreachable safe tile (`run.unfinished`) |
| Prior: harden SM (attack from landing + step-once) | pushed; gate not re-run |
| This pass | in progress under `flock /tmp/cox_raid_gate.lock` |

## Next

- Green `run.py cox_vespula --no-publish` / `gate.py` / `raid_coverage.py` FULL
- Copy play shots to `/opt/cursor/artifacts/cox_vespula_*.png`
