# Tightrope room status

- Branch: `cursor/cox-raid-rooms-da39`
- Strategy (owner 2026-10-07): **Synq solo guide** — kill guards, then keystone.
  Not the phoenix-necklace skip. See `ROOM_AGENT.md` Solo play strategy.
- Test: `test/raids/cox_tightrope.lua` — explicit SM:
  `LAND → KILL_MAGES → KILL_RANGERS → CROSS → TAKE → DISPEL → DONE`
- Content: `cox_puzzles.rs2` (tightrope procs)
- Spec: `encounters/tightrope.tsv` (`spec_check` ok)
- Gate: pending `flock /tmp/cox_raid_gate.lock` run of `cox_tightrope`
