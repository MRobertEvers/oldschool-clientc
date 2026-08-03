# Character Summary (`account_summary_sidepanel` 712): what the server owes

> **LANDED 2026-08-03.** The sidepanel already drew (clientscript 3174 →
> 3310). What was missing was arming `summary_click_layer` and the server
> state behind every counter the draw reads. Content lives under
> `server/scripts/interface_summary/` plus the feature packages named below.
> The wire contract is asserted by `mock230 --selftest` ("character summary").

## 0. Status at a glance

| row | sub-id | op(s) | target | status |
|---|---|---|---|---|
| Combat / Total Level / Total XP | 0–2 | — | click rectangles only | no server op |
| Quests Completed | 3 | op1 Quest List | `~journal_show(^journal_tab_quests)` → `questlist` in `side_journal:tab_container` | **landed** |
| Achievements Completed | 4 | op1 Achievement Diaries | `~journal_show(^journal_tab_diaries)` → `area_task` | **landed** |
| Combat Tasks Completed | 5 | op1–4 Overview/Bosses/Tasks/Rewards | `~ca_open` → interfaces 717/716/715/714 on `mainmodal` | **landed** |
| Collections Logged | 6 | op1 Log / op2 Overview | `~collection_open` / `~collection_overview_open` → 621 / 908 | **landed** |
| Time Played | 7 | op1 Reveal/Hide | varbit + `runclientscript* 3970` with minutes | **landed** |

Arming is **one** `if_setevents` on `summary_click_layer` covering sub-ids 0..7
with ops 1–4 (`^if_event_op1to4 = 30`). `App_IfEventsSet` keeps a single entry
per component id; overlapping ranges would clobber each other. Handlers are
`[if_button1..4]` branching on `last_slot`.

## 1. Engine prerequisite — cache enums

`mock230_content.c` loads `configs/all.enum` as rank-0 config (authored
`server/scripts/**/*.enum` still win on name collision). That is what makes
`enum()`, `enum_getoutputcount()`, and the CA/collection catalog walks answer
for cache tables. `SS_OP_STRUCT_PARAM` already reads the cache binary via
`mock230_structinfo`.

A second consumer of the same capability: `chrome_panels.rs2` arms
`popout:buttons` with `calc(enum_getoutputcount(enum_4067) - 1)` instead of a
literal `2`.

Compiler note: `ENUM_GETOUTPUTCOUNT`'s argument is an **enum value**, not a
ScriptVarType. Treating it as a type-position argument made
`enum_getoutputcount(enum_4067)` fail with "'enum_4067' is not a type"; that
hint was removed in `ssc_compile.c`.

## 2. Server state behind the counters

| counter | carriers / source | content |
|---|---|---|
| Quest points / completed / total | varp `qp` (101), `qp_total` (904), `qp_total2` (2920) | `quests/scripts/questpoints.rs2` — denominators from dbtable `quest`; `~quest_complete` |
| Combat Achievements | 21× `ca_task_completed_*` (`wholewrite=allow`), tier counts, `ca_points`, tier status | `interface_combat_achievements/` — `~ca_task_complete` / `::catask` |
| Collection Log | container 620 `collection_transmit`; counts 2943/2944/4612 + subsection + ring | `interface_collection/` — `~collection_earn`, catalog max from enums 2102–2107; death-drop hook in `npc_default_death` |
| Achievement Diaries | 10 diary count carriers (varbits summed by CS2 4072) | `interface_diaries/` — `~diary_task_complete` |
| Time Played | `%playtime_minutes` (server-only) + `%account_summary_display_playtime` | `player/scripts/playtime.rs2` timer @ 100 ticks; redraw via CS2 3970 |

Awarding CA/diary tasks from live gameplay, and wiring every drop table into
`~collection_earn`, remain ongoing content. Counters report the truth; most
truths stay 0 until that content exists.

## 3. Selftest contract

`mock230_world.c` "character summary":

1. `[if_open,account_summary_sidepanel]` emits `IF_SETEVENTS` mask 30 for
   sub-ids 0..7.
2. `IF_BUTTON1..4` on `summary_click_layer` with the row's sub-id reach the
   numbered triggers; `last_slot` matches; each op mounts a distinct
   interface (questlist / area_task / ca_* / collection*).
3. Time Played pushes `RUNCLIENTSCRIPT` 3970 with type string `iii`.
4. Cache enums `enum_4067` and `enum_3981` resolve from `configs/all.enum`.

## 3.1 Headless client (2026-08-03)

`SDL_VIDEODRIVER=dummy` + embed + `TORIRS_CLICK_DEBUG=1`. Quest tab at
`(553,186)`, then row centres from the tree dump (`summary_click_layer`
sub-ids 3..7 at ~`(555,345)`, `(640,345)`, `(555,400)`, `(640,400)`,
`(555,445)`):

```
clickdbg: … events=0x1e net=1
mock230: <- IF_BUTTON1 712:3 sub=3   # Quests
mock230: <- IF_BUTTON1 712:3 sub=4   # Diaries
mock230: <- IF_BUTTON1 712:3 sub=5   # CA Overview
mock230: <- IF_BUTTON1 712:3 sub=6   # Collection Log
mock230: <- IF_BUTTON1 712:3 sub=7   # Time Played
mock230: -> RUNCLIENTSCRIPT    op=84  payload=20   # iii + 3970
```

`events=0x1e` is mask 30. Ops 2–4 (CA Bosses/Tasks/Rewards, Collection
Overview) are covered by the selftest's numbered `IF_BUTTON2..4` injection;
right-click menu selection in the headless harness was not re-driven here.

## 4. LostCity

No Character Summary / Combat Achievements / Collection Log precedent
(post-date rev 254). Quest-point accumulation follows LostCity's
`send_quest_complete` *intent* (bump QP + completion), not its per-quest `if`
chain — see `questpoints.rs2`.
