# Crabs room status

- Branch: `cursor/cox-crabs-room-da39`
- Replacement for stuck agent bc-e2a3208d
- Strategy: learner solo — lure/smash onto bounce tiles, recolour by style, clockwise beam
- Content: `cox_crabs.rs2` (+ `::coxcrabs`, splash/aggro constants, explicit beam start dirs)
- Spec: `encounters/crabs.tsv`
- Test: `test/raids/cox_crabs.lua` — SM `LAND → MEASURE → SOLVE → DONE`
  - Pack WORLD slots for tracking; Smash via click_minimenu op3 (not attack)
  - Exact bounce tile seat (vacate mark so crab steps on); step off for beam
- Gate: re-running after Smash + exact-seat fix
