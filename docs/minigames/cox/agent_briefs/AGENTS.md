# Live per-room orchestrators (this run)

## HARD STOP — 2026-10-07 (parent)

**All room agents: STOP WORK. Idle. Do not edit the tree.**

Cause: parallel agents share one checkout and are still racing it
(`cp` of `.owned` pins, `--rebuild-scripts`, branch hops, pin-restore
watchers). Rules were not enough; work is frozen until parent unfreezes
**your room by name**.

### Forbidden until unfreeze (every agent)

- Any edit under `OSRS-Content/`, `test/raids/`, `docs/minigames/cox/`
- `git checkout` / `switch` / `checkout -f` / `reset` / `clean` / `stash`
- `cp` / `sed` of harnesses or `.owned` pin restores
- `run.py` / `gate.py` / pack rebuild / `--rebuild-scripts`
- Background watchers / gate shell scripts
- Touching sibling rooms or `AGENTS.md` on another branch

### Allowed

- Reply “idle” to parent. Nothing else.

### Green lanes (already done — stay idle)

| Room | Agent | Branch / PR |
|---|---|---|
| tightrope | bc-0e60f709-4b14-5912-b2ee-beb76784676d | `cursor/cox-tightrope-traversal-da39` #137 |
| icedemon | bc-28036115-f582-5175-aaf5-64487f2d13ab | `cursor/cox-icedemon-sm-da39` #138 |
| scavenger_small | bc-96c722d5-2e31-5f0a-87cc-8fc0811baac7 | `cursor/cox-scavenger-small-da39` #139 |
| thieving | bc-311e5e87-f634-5584-8481-aa63d95c7978 | `cursor/cox-thieving-status-7978` #140 |

### Still RUNNING (will be stopped on next idle)

Do not start new work. When your turn ends, stay idle.

See `cox-room-agent-no-clobber` and `ROOM_AGENT.md`.
