# Olm room status

Branch: `cursor/cox-olm-4t41-recover-da39` (parent) / room agents on `cursor/cox-raid-rooms-da39`

## Strategy (owner 2026-10-07)

1. **Solo Melee 4-tick 4:1** first (`synq_transcript.md` [2:48:05])
2. Dedicated recovery states per special/power so gate failures = content bugs
3. Then **duo**, then **trio** — each its own harness

## SM (`test/raids/cox_olm_solo_4t41.lua`)

Flow:
`ENTER → WAIT_SPAWN → KILL_MAGE → IDENTIFY → LOCKED ⇄ NOODLE`
plus dedicated recovery that returns via `IDENTIFY`:

| State | Trigger | Player response | Content probe |
|---|---|---|---|
| `REC_BURST` | TRACE burst | step off tile in ≤3t | `content.olm.burst_undodgeable` if HP drops after leave |
| `REC_LIGHTNING` | TRACE lightning | side-wall + re-pray | `content.olm.lightning_no_bolts` if still hit |
| `REC_TELEPORT` | TRACE teleport | seek portal / empty | `content.olm.teleport_no_portals` if none |
| `REC_SIPHON` | TRACE siphon | stand mark window | `content.olm.siphon_no_safe_tiles` if none |
| `REC_ACID` | TRACE power + acid | leave pool tile | tile-dodge contract |
| `REC_FLAME` | TRACE power + flame | flame-null / leave trap | firewall escape |
| `REC_CRYSTAL` | TRACE power + crystal | leave fall/bomb tile | crystal dodge |
| `REC_SPHERE` | TRACE sphere | overhead from chat | prayer set |
| `NOODLE` | basic lands on skip-b2 | tank b2 → skip special | re-lock 4:1 |

## Content status

| Mechanic | Status |
|---|---|
| Crystal burst tile dodge | **FIXED** in OSRS-Content `85560cee9b` (`cox_olm_crystal_burst_resolve` on seedling tile) |
| Lightning bolts / side dodge | OPEN — huntall damage + prayer sap → probe `content.olm.lightning_no_bolts` |
| Teleport portals | OPEN — solo flat damage → probe `content.olm.teleport_no_portals` |
| Life siphon safe tiles | OPEN — delayed uid damage + heal → probe `content.olm.siphon_no_safe_tiles` |

Gate run turns OPEN rows into named `content.olm.*` check failures.

## Kit

Setup pack ≤28 after Sang charge: melee worn + `sanguinesti_staff` (magic
claw — right hand mitigates non-MAGIC `/3`) + TBow/arrows (head) + restore 4 +
sara 4 + combat 2 + shark 12. Potions before food; drink uses the full dose
ladder. Melee claw: `goto_tile` onto Synq thumb (pathing cannot walk the
size-5 footprint).

## Proven (2026-10-08)

`cox_olm_solo_4t41_sang` ledger **SUMMARY PASS** (12/12):
`sm.done` head_dead cycles=5 skips=11; `tech.synq_4t41` PASS; chamber
`6416,158,2`. Artifacts: `/opt/cursor/artifacts/cox_olm_solo_4t41_final/`.

## Enter

`raid.enter("olm")` lands in floor-2 **resource** (room 7) at the hole.
Harness must: click `raids_bossentrance` → plane-2 corridor → click
`raids_olm_barrier` → dialogue → chamber spawn. Clicking the hole as the
barrier left the player at resource `6416,112,1` (nonsensical "corridor" shot).

## Pass bar

Green only when the full SM reaches `sm.done` with `head_dead` and
`tech.synq_4t41` (cycles≥1, skips≥1). Kit smoke / enter shots alone are not
a pass. Known OPEN content probes (lightning bolts, teleport portals, siphon
marks) are recorded but do not red-gate the 4:1 proof.

## Next

```sh
flock /tmp/cox_raid_gate.lock \
  env QUEST_BINARY=/workspace/src/torirs_questtest \
  python3 tools/raid_gate/run.py cox_olm_solo_4t41 --no-publish
python3 tools/raid_gate/gate.py cox_olm_solo_4t41
```
