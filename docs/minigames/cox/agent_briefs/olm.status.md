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

## Known content gaps (pre-gate, from `cox_olm.rs2` read)

- `cox_olm_crystal_burst` — uid damage after delay, not seedling tile check
- `cox_olm_lightning` — huntall damage + prayer sap, no moving bolts
- `cox_olm_teleport` — solo flat separation damage, no portals
- `cox_olm_life_siphon` — delayed uid damage + heal, no standable marks

Gate run should turn these into named `content.olm.*` check failures.

## Next

```sh
flock /tmp/cox_raid_gate.lock \
  python3 tools/raid_gate/run.py cox_olm_solo_4t41 --no-publish
python3 tools/raid_gate/gate.py cox_olm_solo_4t41
```
