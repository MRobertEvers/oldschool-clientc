# Creature keeper (thieving) — room status

**Branch:** `cursor/cox-thieving-status-7978` (base `cursor/cox-raid-rooms-da39`)  
**Owner:** CoX thieving only — do not expand into sibling rooms

## Owned files

| Path | Role |
|---|---|
| `OSRS-Content/.../scripts/cox_puzzles.rs2` | Thieving procs (`~cox_thieving_*`, `::coxthieving`) |
| `OSRS-Content/.../configs/cox.constant` | `^cox_thieving_*` (counts 64/66/74, hunger 100, pts 115, stack 28) |
| `docs/minigames/cox/encounters/thieving.tsv` | Spec rows (7) |
| `docs/minigames/cox/encounters/thieving.scope.tsv` | Scope |
| `test/raids/cox_thieving.lua` | Gate harness: `t.raid.enter("cox","thieving",{seed=1})` |

## Mechanics (COX_MECHANICS.md §14)

- Chest layouts: CCW 64 / THRU 66 / CW 74 (`ChestData.java`)
- 115 points per grub; stack cap 28; min 1 grub per successful open
- Hunger delay 100 ticks after last feed; trough feed → beast sleeps
- Readout: `::coxthieving` prints counts / hunger_delay / pts_per_grub / stack_cap

## Gate proof (re-verify)

Private binary: `src/torirs_questtest_thieving`  
(`PLATFORM_OBJ_BASE=build_questtest_thieving`, `GL_GLEXT_PROTOTYPES`, `--no-build`)

```sh
flock /tmp/cox_raid_gate.lock python3 tools/raid_gate/run.py cox_thieving --no-publish --no-build
python3 tools/raid_gate/gate.py cox_thieving
python3 tools/raid_gate/raid_coverage.py cox_thieving
```

| Artifact | Path |
|---|---|
| Reverify log | `/opt/cursor/artifacts/cox_thieving_reverify.log` |
| Ledger | `build/quest_gate/cox_thieving/ledger.tsv` |
| Ticklog | `build/quest_gate/cox_thieving/ticklog.tsv` |
| Client log | `build/quest_gate/cox_thieving/client.log` |
| Gate grade (prior ledger) | `/opt/cursor/artifacts/cox_thieving_gate_prior.log` |
| Coverage (prior ledger) | `/opt/cursor/artifacts/cox_thieving_cov_prior.log` |

**Status:** PRIOR ledger green / FULL (28 PASS @ `2026-10-07T12:17`); fresh flock reverify in queue — update RUN/GATE/COV exits from reverify log when complete.

## Spec

`spec_check.py docs/minigames/cox/encounters/thieving.tsv` → 7 rows ok
