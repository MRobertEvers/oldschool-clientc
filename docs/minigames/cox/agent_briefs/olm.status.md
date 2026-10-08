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

Setup pack is **28 slots**: 6 range-switch + restore 4 + sara 4 + combat 2 +
shark 12. The prior shark-24 kit overfilled and potions never landed (setup
FAIL). Potions are given before food.

## Next

```sh
flock /tmp/cox_raid_gate.lock \
  python3 tools/raid_gate/run.py cox_olm_solo_4t41 --no-publish
python3 tools/raid_gate/gate.py cox_olm_solo_4t41
```
