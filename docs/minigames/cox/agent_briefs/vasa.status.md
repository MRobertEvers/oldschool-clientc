# Vasa Nistirio — room orchestrator status

**Agent:** Vasa only (`bc-47112672-81a0-5c55-8de1-ed8cc4958456`)  
**Branch:** `cursor/cox-vasa-gate-8456`  
**Binary:** `/workspace/src/torirs_vasa_qt` (`PLATFORM_CFLAGS_EXTRA='-DTORIRS_HAVE_GL3=1 -DGL_GLEXT_PROTOTYPES'`)

## Owned files touched

| Path | Change |
|---|---|
| `OSRS-Content/.../scripts/cox_vasa.rs2` | Approach wake via `huntall`; crystal timer on arrival / pause on kill; boulder `(1,8]` @ 3-tick + `vasa_attack` + trace; defence restore `(10%+1)` |
| `OSRS-Content/.../configs/cox.constant` | `^cox_vasa_boulder_range=8`, `^cox_vasa_spark_interval=3`, `^cox_vasa_wake_range=3`, defence restore, `^cox_trace_vasa_boulder=33` |
| `docs/minigames/cox/encounters/vasa.tsv` | Grades A where cache/Jagex apply |
| `test/raids/cox_vasa.lua` | seed=1 fight; glory amulet; pray-before-approach; eat/step after wake |

## Gate / evidence

| Check | Result |
|---|---|
| `spec_check.py …/vasa.tsv` | **ok, 7 rows** |
| `cox_check_timers.py` | **all ai_timer armed** |
| `lint_quest.py` | **clean** |
| Private binary | `/workspace/src/torirs_vasa_qt` |
| Prior gate | RED — enter auto-woke at range 8, special killed solo at HP 5 (fixed: wake_range=3 + harness) |
| Current | flocked re-run in progress (`/opt/cursor/artifacts/cox_vasa_gate.log`) |

```sh
flock /tmp/raid_gate_cox_vasa.lock \
  env QUEST_BINARY=/workspace/src/torirs_vasa_qt \
  python3 tools/raid_gate/run.py cox_vasa --no-publish --no-build
```

## Not edited

- Sibling room scripts / harnesses, `platform.mk`, no `*.skip`
