# Shamans room status

Branch: `cursor/cox-shamans-wallhug-da39`

## Strategy (owner 2026-10-07)

**Synq wall-hug** (`synq_transcript.md` [0:34:39]–[0:39:04]):
Protect from Missiles, anti-poison, hug walls so the 3×3 jump cannot land,
isolate one shaman at a time with a long-range weapon. Not face-tank melee.
Full Shayzien tier-5 nulls the unprayerable poison splash (wiki).

## Gate

- `cox_shamans` **GREEN** + coverage **FULL** (7/7 spec rows)
- Poison land rebinds `npc_uid` before hunt (was `active_npc=0` abort)

## Implemented

- Content: `[ai_timer]` → regen + spawn special + tendril gate
- Content: poison land `npc_finduid` then splash hunt / blob hit
- Test SM: `LAND → ARM → MEASURE_REGEN → TANK_POISON → KILL → DONE`
- Harness: TBow + Shayzien 5 + 24 sharks; severity-12 tank then clear
- Spec: `encounters/shamans.tsv` (7 rows, scope all)
