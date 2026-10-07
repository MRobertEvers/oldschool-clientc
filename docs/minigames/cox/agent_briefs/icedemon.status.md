# Ice demon room status

Branch: `cursor/cox-icedemon-sm-da39`
Replacement for stuck agent `bc-bdf6ec09` (2026-10-07).

## Strategy (owner 2026-10-07)

**Synq solo learner** (`synq_transcript.md` [0:14:39]):
light unguarded braziers with kindling → thaw → Protect from Missiles →
fire spells (dodge 3×3 snow). Not face-tank without prayer.

## Ownership

- `OSRS-Content/.../scripts/cox_icedemon.rs2`
- additive icedemon constants / varps / npc (already in tree)
- `docs/minigames/cox/encounters/icedemon.tsv`
- `test/raids/cox_icedemon.lua`
- this status brief

## Implemented

- Content: stage machine, braziers, icefiend douse on per-brazier kindling,
  damage scale, prayer style (prior parity + icefiend kindling fix)
- Test SM: `LAND → LIGHT → WAIT_THAW → ARM_PRAY → FIGHT ⇄ DODGE → DONE`
- Spec rows: all icedemon.tsv mechanics

## Notes

- Kindling via `::give raids_wood` — tree/axe oploc not wired; corsaircurse
  currently owns `[oploc1,raids_icedemon_tinderbox]` (out of room ownership).
- No godmode.

## Next

- Gate under `flock /tmp/cox_raid_gate.lock`
- Promote grades when measured live
