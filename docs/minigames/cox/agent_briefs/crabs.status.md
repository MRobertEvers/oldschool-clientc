# Crabs room status

- Branch: `cursor/cox-crabs-room-da39`
- Replacement for stuck agent bc-e2a3208d
- Strategy: learner solo — lure/smash onto bounce tiles, recolour by style, clockwise beam
- Content: `cox_crabs.rs2` (+ `::coxcrabs`, splash/aggro constants, explicit beam start dirs)
- Spec: `encounters/crabs.tsv`
- Test: `test/raids/cox_crabs.lua` — SM `LAND → MEASURE → SOLVE → DONE`
  - Fix: use pack WORLD slots (not client nearest slots) for smash/stun tracking
  - Fix: stand on bounce tile to seat, step off before smash
- Gate: re-running after harness seat/slot fix
