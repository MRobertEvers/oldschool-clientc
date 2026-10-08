# Ice demon room status

Branch: `cursor/cox-icedemon-sm-da39`
Replacement for stuck agent `bc-bdf6ec09` (2026-10-07).

## Gate

- `flock /tmp/cox_raid_gate.lock python3 tools/raid_gate/run.py cox_icedemon --no-publish` → PASS
- `python3 tools/raid_gate/gate.py cox_icedemon` → green
- `python3 tools/raid_gate/raid_coverage.py cox_icedemon` → **FULL** (7/7)

## Strategy

**Synq solo learner** (`synq_transcript.md` [0:14:39]):
`::cox_icedemon_fuel` lights unguarded braziers → thaw → Protect from Missiles
→ attack (fire preferred; auto-attack fallback) with AoE dodge.

## Ownership landed

- `cox_icedemon.rs2`: icefiend kindling douse, prayer-scaled AoE, `::cox_icedemon_fuel`
- `cox.constant`: `^cox_icedemon_prayer_remaining_pct = 33`
- `test/raids/cox_icedemon.lua`: SM `LAND → FUEL → WAIT_THAW → ARM_PRAY → FIGHT ⇄ DODGE → DONE`
- `encounters/icedemon.tsv` (unchanged rows, all measured)

## Notes

- Tree/axe oploc still unwired; corsaircurse owns `[oploc1,raids_icedemon_tinderbox]`.
- No godmode.
