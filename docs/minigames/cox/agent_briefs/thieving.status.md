# Thieving (creature keeper) — room status

Branch: `cursor/cox-raid-rooms-da39`  
Owner: CoX ROOM ORCHESTRATOR (thieving only)  
Commit: parent `cursor/cox-thieving-green-da39` + OSRS-Content `79281cef10`

## Result — GREEN

| Check | Outcome |
|---|---|
| `tools/cox_check_thieving_chests.py` | all 204 entries match cache |
| `spec_check.py encounters/thieving.tsv` | ok, 7 rows |
| `run.py cox_thieving --no-publish` | SUMMARY 28 PASS / 0 FAIL |
| `gate.py cox_thieving` | green |
| `raid_coverage.py cox_thieving` | **FULL** (7/7) |

## Mechanics landed

- Chest tables per variant from `sources/de0/ChestData.java` (already ported;
  now named `^cox_thieving_chest_count_{ccw,thru,cw}` = 64 / 66 / 74).
- 115 points/grub, stack cap 28, min 1 grub/open, hunger delay 100 ticks.
- Trough deposit + corrupted scavenger → sleeping when fed ≥ solo 30.
- Read-only `::coxthieving` prints table sizes + delay/points/stack constants.

## Own files touched

- `OSRS-Content/.../scripts/cox_puzzles.rs2` (thieving procs / `::coxthieving`;
  flock `/tmp/cox_puzzles.lock`) — tightrope procs untouched
- `OSRS-Content/.../configs/cox.constant` (`^cox_thieving_*`; flock `/tmp/cox_config.lock`)
- `docs/minigames/cox/encounters/thieving.tsv` (unchanged; already valid)
- `test/raids/cox_thieving.lua` — enter seed=1, open chests, feed, clear

## Evidence

- Ledger: `build/quest_gate/cox_thieving/ledger.tsv`
- Artifacts: `/opt/cursor/artifacts/cox_thieving_{idle,mid,clear}.png`
- Logs: `/opt/cursor/artifacts/cox_thieving_{run,gate,coverage,chests_check,spec_check}.log`

## Gate commands used

```sh
python3 tools/cox_check_thieving_chests.py
python3 tools/raid_gate/spec_check.py docs/minigames/cox/encounters/thieving.tsv
# private binary (shared build_questtest was raced by siblings; needs -DGL_GLEXT_PROTOTYPES)
QUEST_BINARY=/workspace/src/torirs_questtest_thieving \
  flock -x /tmp/raid_gate.lock python3 tools/raid_gate/run.py cox_thieving --no-build --no-publish
QUEST_BINARY=... python3 tools/raid_gate/gate.py cox_thieving
python3 tools/raid_gate/raid_coverage.py cox_thieving
```
