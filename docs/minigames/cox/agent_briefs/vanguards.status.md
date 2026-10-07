# Vanguards room agent status

Branch: `cursor/cox-vanguards-gate-da39`
Replacement for stuck agent `bc-a4e27b30`.

## Gate

**RED** (run20): `alive` / `player.died` in BALANCE — solo still dies under
multi-style fire when attack engagement walks off the isolate tile.
Prayer-scaled AoE is live (max hit 7 under Protect); DPS works when
isolated (hit_npc > 0). Shuffle / FULL coverage not yet reached.

## Content landed (OSRS-Content dirty / local commit)

- `cox.npc`: `givechase=no` + `defaultmode=none` on combat forms (pads hold
  so shuffle tile-find works).
- `cox_vanguards.rs2`: prayer-scale ranged/magic AoE max (was unused arg);
  shuffle_all slot-var fallback if off-tile.

## Harness

SM LAND→WAKE→PROBE_HEAL→BALANCE⇄SHELL→DONE; Protect on wake; one-shot
cardinal isolate; re-isolate after attack; brew+shark sustain; spotanim
1331/1332 for attacks_per_action; combat-form defence record.

## Next

Survive BALANCE long enough to clear + measure shuffle. Likely need
post-attack kite that wins against engine engage-walk, or freeze melee.
