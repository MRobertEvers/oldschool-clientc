# Olm room status

Branch: `cursor/cox-olm-solo-4t41-a9fc` (base `cursor/cox-raid-rooms-da39`)

## Strategy (owner 2026-10-07)

1. **Solo Melee 4-tick 4:1** first (`synq_transcript.md` [2:48:05])
2. Then **duo**, then **trio** — each its own harness
3. Explicit state machine (not scythe spam)
4. Prayer flick on `%varp6766_cox_olm_style` (magic/ranged); no godmode

## Implemented

- `test/raids/cox_olm_solo_4t41.lua` — SM:
  `ENTER → WAIT_SPAWN → KILL_MAGE → SETUP_41 → CYCLE_{TANK,FREE,RUN,TURN} → WAIT_PHASE → HEAD → DONE`
- Spec table: `encounters/olm_solo_4t41.tsv` (+ `olm.tsv` phases_solo=4)
- Content: `cox_olm.rs2` basic hit respects protect prayer; `^cox_olm_prayer_mult_*`
- Entry: `^cox_olm_entry_lz = 25` (open arena aisle; lz=12 is sealed by pit/wall ring)
- Loc: `cox.loc` clears `blockwalk` on `raids_wall_top_*` floor surfaces
- Mage hand via charged Sanguinesti; melee 4:1 with whip; head with TBow

## Next

- `flock /tmp/cox_raid_gate.lock` → `run.py cox_olm_solo_4t41 --no-publish`
- After green: `gate.py` + `raid_coverage.py` FULL; then duo/trio harnesses
