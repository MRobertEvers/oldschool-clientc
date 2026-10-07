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
  `LAND → ARM_PRAYERS → ATTACK_PORTAL ⇄ STEP_SAFE / RESTORE → DONE`
- Setup: `::give 4doseantipoison 1` (valid obj; not `antipoison4`)
- `::coxvespula` debugproc: teleports onto authored `portalHitTile` 0
- Prayer arm: redemption + rigor, else eagleeye; `t.ui.tab("combat")` after arm

## Gate evidence

| Attempt | Outcome |
|---|---|
| Seed-1 `::coxgoto` room centre | walk stalls on every neighbour (combat_a plane 2; guardians same, crabs/puzzle_a OK) |
| Attack from centre | `I can't reach that!` (barrier LoS) |
| `::coxvespula` → hit tile 6479,109 (thru) | portal Attack lands; redemption SM clears |
| `run.py cox_vespula --no-publish` + `gate.py` | **green** |
| `raid_coverage.py cox_vespula` | **FULL** (12/12) |

## Note (layout, not owned)

combat_a template plane 2 room centres are collision-dead under soft3d gate
(`walk_to` / `step_tick` stall). Guardians shares it. Workaround: room debugproc
lands on the Synq gap tile. A layout/collision fix for the stamp is out of this
room's ownership (`cox_layout.rs2` forbidden).
