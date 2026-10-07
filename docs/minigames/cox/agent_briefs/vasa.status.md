# Vasa Nistirio — room orchestrator status

**Agent:** Vasa only (`bc-47112672-81a0-5c55-8de1-ed8cc4958456`)  
**Branch:** `cursor/cox-vasa-gate-8456`  
**Binary:** `/workspace/src/torirs_vasa_qt` (`PLATFORM_CFLAGS_EXTRA='-DTORIRS_HAVE_GL3=1 -DGL_GLEXT_PROTOTYPES'`)

## Owned files touched

| Path | Change |
|---|---|
| `OSRS-Content/.../scripts/cox_vasa.rs2` | Approach wake via `huntall`; crystal timer starts on arrival and pauses on crystal kill; boulder ring `(1,8]` at **3-tick** cadence with `vasa_attack` + `^cox_trace_vasa_boulder`; defence restore `(10%+1)` each siphon cycle |
| `OSRS-Content/.../configs/cox.constant` | flock `^cox_vasa_*`: `^cox_vasa_boulder_range=8`, `^cox_vasa_spark_interval=3`, `^cox_vasa_wake_range=8`, `^cox_trace_vasa_boulder=33`, defence restore pct/flat |
| `docs/minigames/cox/encounters/vasa.tsv` | Grades promoted (cadence/regen/heal/boulder → A where cache/Jagex apply) |
| `test/raids/cox_vasa.lua` | `t.raid.enter("cox","vasa",{seed=1})` driven fight + `spec.vasa.*`; non-`br_` potions for this pack |

## Mechanics (COX_MECHANICS.md §5)

| Spec | Implementation |
|---|---|
| Attack speed 3 | `^cox_vasa_spark_interval=3` + `npc_anim(vasa_attack)` / cache `attackrate,3` |
| Crystal timer 66–67 on **arrival** | Set only when healing form reaches crystal tile; **not** on activate; shared window pauses on kill |
| 1% heal / 2 ticks | Siphon bank `^cox_vasa_heal_interval=2` (+ defence restore); HP applied on full siphon (2023 CoX change) |
| Teleport special | Announce → teleport → shared (HP−5) explosion → crystal walk |
| Boulder 8 tiles | `huntall` + distance `(1, ^cox_vasa_boulder_range]` |
| Stomp ≤8 / tick | `~cox_vasa_stomp_tick` / `^cox_vasa_stomp_maxhit=8` |
| Four crystals | `^cox_vasa_crystals=4` without-replacement until next special |

## Gate / evidence

| Check | Result |
|---|---|
| `spec_check.py …/vasa.tsv` | **ok, 7 rows** |
| `cox_check_timers.py` | **all ai_timer armed** |
| `lint_quest.py test/raids/cox_vasa.lua` | **clean** |
| Private binary | `/workspace/src/torirs_vasa_qt` with `-DGL_GLEXT_PROTOTYPES` |
| `tools/raid_gate/run.py cox_vasa` | **in progress** — flocked with `QUEST_BINARY` |

```sh
flock /tmp/raid_gate_cox_vasa.lock \
  env QUEST_BINARY=/workspace/src/torirs_vasa_qt \
  python3 tools/raid_gate/run.py cox_vasa --no-publish --no-build
flock /tmp/raid_gate_cox_vasa.lock python3 tools/raid_gate/gate.py cox_vasa
```

## Not edited

- Sibling room scripts, `cox_layout.rs2`, `cox.rs2`, `raid.lua`
- No `*.skip` parking
- Shared `platform.mk` untouched (lane-local binary flags only)
