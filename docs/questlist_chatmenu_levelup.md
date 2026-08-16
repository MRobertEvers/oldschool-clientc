# The next three interfaces: what the server owes each

> **What this is.** `docs/LOSTCITY_PORT_TRIAGE.md` §9 step 6 names the interface
> queue as **questlist → chatmenu → levelup → the rest on demand**. This is the
> §5.3 feature-checklist discovery pass for those three, done against the
> decompiled CS2 in `OSRS-Content/osrs239-content/scripts/` and the interface
> layouts in `OSRS-Content/osrs239-content/interfaces/`, cross-referenced
> against `src/net/mock/` and `src/game/` to say what mock230 already does and
> what it doesn't. It is a server-requirements spec, not an implementation —
> per `docs/PORTING_GUIDE.md` §2, the write-up below is a starting point for
> whoever writes the content/engine change, not a substitute for grepping the
> reference again at that time.
>
> One result up front, because it inverts the expected order: **chatmenu is
> essentially done.** Questlist's header vars and "Read journal" path are
> landed (§1.5); `levelup_display` remains the open interface gap.

---

## 0. Status at a glance

| interface | id | status | what's missing |
|---|---|---|---|
| `chatmenu` | 219 | **landed** | nothing server-side; two loose ends noted in §2.4 |
| `questlist` | 399 | **landed** (header + journal + overview switch) | ops 3–6 (map / wiki / pin) still open; see §1.5 |
| `levelup_display` | 233 | **gap** | level-up detection exists but stops at a chat string; the popup interface is never opened |

---

## 1. `questlist` (399)

### 1.1 The call graph

```
questlist.if [universe] onload=i:1350,i:26148866,i:26148868,i:26148870,i:26148869,
                                i:26148874,i:26148873,i:26148871,i:26148875
  component0=container  int1=list_container  int2=text_container  int3=scrollbar
  component4=questpoints  component5=completed  int6=list  int7=settings_button

script_1350 [clientscript,questlist_init]
  ~questlist_qp($component4)                              -> script_1356
  if_setonvartransmit("questlist_qp(...){var101,var904,var3368,var3369,var3766}")
  ~script5995($component5)                                -> script_5995
  if_setonvartransmit("script4030(...){var904,var2920,var3365}")
  cc_deleteall + ~steelbox (cosmetic panel background)
  ~questlist_draw($component0,$int1,$int2,$int3,$int6,0)   ** not in this corpus, see 1.3 **
  ~script5996($int7)                                        (gear icon, cosmetic)

script_1356 [proc,questlist_qp]          reads %var101, %varbit1782 (or ~script5893)
script_5995  [clientscript,script5995]   reads %varbit6347, %varbit11877 (or %var3365)
script_5849  [proc]                      reads %var3717 via testbit(...,8) — speedrun-mode switch
script_5893  [proc]                      reads %varbit13632/13633/13634/13635/6543 (speedrun trophy sum)
script_5262  [proc]                      reads %varbit1777 (ironman variant gate on a chat command)
script_1412  [proc,questdisplay_setup]   -> db_getfield/db_find_with_count against dbtable 0 "quest"
```

The quest **names, F2P/members flags, release-type and parent-quest chain** all
come from `db_getfield`/`db_find_with_count` against **dbtable 0 `quest`**
(`OSRS-Content/osrs239-content/configs/all.dbtable:3-63`, 213 rows). That table
already loads generically — no server work needed for the list content itself,
only for the header numbers below.

### 1.2 Server obligations

The header vars are declared and written under `server/scripts/quests/`.
Character Summary's Quests row reads the same completed/total pair via the
sidepanel draw (CS2 3174) — see
[`account_summary_server_reqs.md`](account_summary_server_reqs.md).

| state | read by | meaning | delivery | status |
|---|---|---|---|---|
| `%qp` (varp 101) | `script_1356` | current total quest points | varp transmit | **landed** — `quests/configs/questpoints.varp` |
| `%qp_max` (varbit 1782 on `qp_total`) | `script_1356` | max obtainable QP | varbit via carrier transmit | **landed** — `~questpoints_login` sums `quest:questpoints` |
| `%quests_completed_count` (varbit 6347 on `qp_total`) | `script_5995` | quests completed | varbit via carrier | **landed** — bumped by `~quest_complete` |
| `%quests_total_count` (varbit 11877 on `qp_total2`) | `script_5995` | total quests available | varbit via carrier | **landed** — `db_listall_with_count(quest)` at login |
| speedrun vars (`%var3717` bit 8, `%var3365`, trophies) | speedrun chrome | not in scope | — | **undeclared** — no speedrun system |

### 1.3 Corpus gap — answered: script 2633

`~questlist_draw` is **clientscript 2633**. It fails to decompile cleanly but
disassembles: rows are `cc_create(questlist:list, iftype_text, $n, 0)` where
`$n` runs `1..db_findall_with_count(quest)` and `~script6154($n)` resolves the
row via `db_find(quest:id, $n)`. **The dynamic sub-id is the quest's `id`
column**, which arrives as `last_slot` on `IF_BUTTON2`. Every op is
`cc_setop` with **no** `cc_setonop` — so op 2 `"Read journal:"` is the
server's (op 1 is blank outside speedrun mode).

The panel op 2 opens is **`questjournal` (119)** (`qj1..qj210`). `%qj_lines`
(varp **4398**) sizes the scrollbar via `script_6923`; **clientscript 2523**
is the rest of the contract:

```
[clientscript,script2523](int $reset_scroll, int $line_count)
def_component $component2 = interface_119:6;   // questjournal:textlayer
~script6949(7798791 /* scrollbar */, $component2, $line_count, ...);
```

When `lines*20` fits the textlayer, script 6949 sets `scrollsize(0,0)` then still
rebuilds the IF3 scrollbar chrome (`~scrollbar_vertical`) with a full-height
grip — same pattern as the skill guide. Only IF1 layers auto-hide native
scrollbars (`scrollHeight > height`); do not expect the journal bar to vanish
on short journals. Journal rows are 415x20 `p12_full` single-line widgets. The
authoritative rev-239 renderer (`class671.method14513` → `method14532`) disables
wrapping when a widget is too short for two lines, so wrapping is necessarily a
server obligation. `split_init` measures with archive 13's cache font metrics,
treats `|` as a hard break, and carries active colour/strikethrough markup into
continuation rows before `if_settext(qjN, split_get(...))`.

No journal text lives in any dbtable — content paints the lines.

### 1.4 LostCity precedent

`content/scripts/general/scripts/quests.rs2`:
- `[proc,update_questpoints]` (`:50-55`) recomputes `%qp` from
  `[proc,count_questpoints]` (`:57-230`) — one hardcoded
  `if (%<quest_varp> = ^<quest>_complete) { $questpoints = add($questpoints, ^<quest>_questpoints); }`
  per quest.
- `[proc,update_questlist]` (`:232-304`) pushes per-quest colour state.
- `[proc,send_quest_complete]` (`:15-48`) is what each quest's completion
  script calls.

This is enumerate-by-hand — one `if` per quest — which does not fit osrs239's
table-driven `questlist_draw`. **Port the intent (recompute `%qp` on
completion), not the shape**: sum dbtable 0 column 17 (`questpoints`, schema
exists at `configs/all.dbtable:28` but **no script anywhere reads it** — this
is the natural source of truth) over completed quests, rather than copying
LostCity's per-quest `if` chain. Per `docs/PORTING_GUIDE.md` §2.4 checklist
item 3, `%qp`/`%varbit6347`/etc. belong in `.varp`/`.varbit` config plus a
recompute proc in content — never as a literal in C.

### 1.5 Built record — quest journal + QP header

Landed under `server/scripts/interface_questjournal/` and `quests/`:

- `~quest_journal_login` arms `questlist:list` for op 2 over `1..db_listall(quest)`.
- `[if_button2,questlist:list]` → `db_find(quest:id, last_slot)` →
  `%latest_quest_journal`, then `~cook_journal` or `~quest_journal_unwritten`.
- `~quest_journal($title, $text)` calls cache-backed `split_init` at the measured
  415px row width, paints `qj1..qjN`, sets `%qj_lines`, mounts `questjournal`
  into `mainmodal`, then
  `runclientscript*(2523)(1, N)` (mount before clientscript).
- `questjournal:switch` mounts `questjournal_overview` (782) and invokes the
  cache-authored 6821/6822 builder with the quest dbrow, named interface
  components, combat level and a content start-action derived from the row's
  `quest:startnpc`. `questjournal_overview:switch` resolves the stored dbrow's
  `quest:id` and repaints the journal. Both interfaces re-arm Close/Switch on
  every mount.
- `questlist:settings_button` is armed and opens the named Settings modal.
  The overview's cache-authored dynamic “Show on Map” op is likewise armed;
  its server trigger sends the quest row's `startcoord` through clientscript
  1749 before mounting the named world-map floater. The map close router sees
  this normal content mount as open state, so Close/Escape works identically to
  an orb-opened map.
- QP: `~questpoints_login` / `~quest_complete` (Cook's Assistant completion).
- Engine: `mock230_db_load_cache` fills cache DBTABLE/DBROW so `quest:id` /
  `displayname` / `questpoints` resolve; authored `quest.dbtable` supplies
  column names. Verified in `mock230 --selftest` ("quest journal" section).

Still open (named so they are not rediscovered): ops 4/5 wiki, op 6 "Pin journal",
and the other 56 LostCity per-quest journals (quests
with no gameplay here).

---

## 2. `chatmenu` (219)

### 2.1 How the panel is populated

`chatmenu.if` has 2 components (`universe`, `options`) and no `onload` — it is
filled entirely by a clientscript the server invokes at runtime:
`script_58 [clientscript,chatbox_multi_init](string $header, string $options)`,
where `$options` is `|`-joined (2-5 entries), `cc_deleteall`s `interface_219:1`,
then calls `~chatbox_multi_addoption` per row under that component by sub-id.

`chatbox_multi_addoption`'s own body (the actual `cc_create`/arming per row)
is **not present in this corpus** (script id ~59 is missing) — same caveat
class as §1.3, an inference gap rather than a mock230 gap, since mock230's
behaviour here was validated against a selftest exercising the real wire
traffic, not against reading that proc.

### 2.2 Server mechanism (already landed)

- `^clientscript_chatbox_multi_init = 58` declared content-side:
  `OSRS-Content/osrs239-content/server/scripts/interface_chat/configs/chat.constant:53`.
- Wire: `SS_OP_RUNCLIENTSCRIPT_SS`, opcode **11002**
  (`src/net/mock/mock230_opcode_coverage.gen.h:260`), handled at
  `src/net/mock/mock230_scripts.c:3462-3472`, encoder
  `mock230_send_run_clientscript_mixed` (`mock230.h:2610-2630`).
- `SS_OP_IF_ADDRESUMEBUTTON` (`mock230_scripts.c:3474-3498`) arms
  `chatmenu:options` across sub-ids `0..MOCK230_RESUME_SUB_MAX` (15,
  `mock230.h:220`) rather than slot 0, because the clientscript's rows carry
  sub-ids 1..5, not separate components.
- Content driver, fully rewritten against `chatmenu`:
  `OSRS-Content/osrs239-content/server/scripts/interface_chat/scripts/chat.rs2:142-232`
  (`p_choice_open`, `p_choice2..5`, `p_choice2_header`) — verified directly,
  matches the doc exactly:
  ```
  [proc,p_choice_open](string $header, string $options)
  if_openchat(chatmenu);
  runclientscript_ss(^clientscript_chatbox_multi_init, $header, $options);
  if_addresumebutton(chatmenu:options);
  p_pausebutton;
  ```
- Buffer caps raised in step (`PKT_RUNCLIENTSCRIPT_STR_LEN` 512,
  `TASK_CS2_RUN_STR_ARG_LEN` 512 with a `_Static_assert` tying them together,
  `task_cs2_run.c:60`) — documented at `docs/osrs230_mockserver.md` §3.11f.
- A live call site: `hans.rs2:17-36` (`[opnpc1,hans]`), and a selftest
  (`mock230_world.c:5609-5725`) that opens Hans's dialogue, resumes into
  `p_choice3`, captures the RUNCLIENTSCRIPT packet, sends `IF_BUTTON1` on
  `chatmenu:options` sub=3, and asserts `player->last_slot == 3` and the
  conversation continues into branch 3.

### 2.3 How the answer comes back

Not `last_com` (all rows share one component). `IF_BUTTON1` carries the row as
a sub-id; `handle_if_button_op` sets `last_com` / **`last_slot`** / `last_verb`
from it **and** resumes `p_pausebutton` when the uid is a registered resume
button (choice menus park on `chatmenu:options`). Content reads `last_slot` via
`SS_OP_LAST_SLOT` and `chat.rs2`'s `p_choice2..5` branch on it. Without that
resume on the same packet, a later continue left `last_slot` at 0 and every
`~p_choice*` fell through to its last option.

### 2.4 Loose ends

- `docs/LOSTCITY_PORT_TRIAGE.md` still lists `~p_choice*`/string-arg
  RUNCLIENTSCRIPT as blocked (§7.4 and its summary tables) — that predates
  the landing recorded in `docs/osrs230_mockserver.md` §3.11f. **Trust the
  mockserver doc over the triage doc on this specific point**; the triage's
  §9 step 6 ordering (questlist next) is still correct, it's just the §7.4
  blocker note that's stale.
- Only `p_choice2..5` and `p_choice2_header` were confirmed in `chat.rs2`;
  `p_choice3_header/4_header/5_header` exist in LostCity's precedent but
  weren't confirmed present here — check before assuming parity if a port
  calls one of those.
- A report of the rendered chatmenu (219) having badly-spread vertical
  spacing was investigated and **did not reproduce**: `TORIRS_DUMP_BOUNDS=219`
  driving Hans's real `~p_choice3` (and, temporarily, `~p_choice2`/`~p_choice5`)
  matches script 58's pixel math exactly in every option count and both
  transports. See `docs/REV230_UI_BLANK_PANELS.md` §5 for the full evidence
  and what to gather if it recurs (exact NPC, whether the window was resized
  off the fixed 765x503 layout root).

### 2.5 LostCity precedent (why the rewrite was necessary)

LostCity gives each choice count its own interface (`multi2..multi5`) with one
named text component per option (`com_1..com_N`) and answers via
`switch_component(last_com)`. rev-230 has one interface with client-built rows,
so the per-component `if_settext`/`last_com` idiom becomes a single
`runclientscript_ss` call plus a `last_slot` switch — exactly the rewrite in
§2.2. This is the concrete instance of `docs/UI_ERA_PORTING_GUIDE.md` §2.4's
general rule that population moved from "the server paints" to "the panel
paints itself."

---

## 3. `levelup_display` (233)

### 3.1 What's in the interface

25 hidden `type=0` sub-panels (`agility` … `sailing`, `levelup_display.if:63-762`),
one per skill (plus `combat` and `sailing`), each with 1-2 static `type=6`
model components already baked with `model=`/`modelzoom=`/`modelxan=`/`modelyan=`.
`text1`/`text2` (`:15-43`) are empty, server-populated text fields. `continue`'s
`onload=i:55,...` is **not** levelup-specific — script 55
(`chatbox_keyinput_init`) is a generic click/key-to-continue wire-up shared by
7 different message-box interfaces (`chat_both`, `chat_left`, `chat_right`,
`messagebox`, `messagebox_titled`, `objectbox_double`, `levelup_display`).

**Nothing in the CS2 corpus opens this interface.** `grep -rlF "if_open"
OSRS-Content/osrs239-content/scripts` is 0 hits across all 9,725 files — at
this era, opening an interface is never a clientscript's job (§2.4 of
`docs/UI_ERA_PORTING_GUIDE.md`). Panel selection is by name match (panel name
== skill name), so no index table is needed — just the string.

### 3.2 What mock230 already does

Level-crossing detection is landed and correctly shaped:

```c
// src/net/mock/mock230_combat.c — on skill level-up
mock230_scripts_run_trigger_specific(srv, SS_TRIGGER_ADVANCESTAT, stat, -1, -1);
```

Content binds `[advancestat,<skill>]` (see `levelup/scripts/levelup.rs2`) and
reaches `@levelup` / the combat tab message from there — no named-proc hook.
### 3.3 The gap

The hook stops at a chat string. Nothing calls `if_opensub` for 233, nothing
sets `text1`/`text2`, nothing `cc_sethide`s a skill panel — confirmed,
`grep -rln "levelup_display\|\b233\b" src/` is empty. This exact gap is named
in the repo's own backlog: `docs/LOSTCITY_PORT_TRIAGE.md:446` — `levelup (19
.if) | 19 advancestat triggers | rebuild against 233 levelup_display, which
osrs239 has`.

`mock230_send_if_opensub` already exists (`mock230.h:2587`,
`mock230_encode.c:355`) and is used for other interfaces — it is simply never
called with 233.

### 3.4 LostCity precedent

`engine/src/engine/entity/Player.ts:1817-1891` (`addXp`) detects the crossing
in the engine (`before`/`after` compare, same shape mock230 already uses) and
dispatches a per-skill `[advancestat,<skill>]` content trigger.
`content/scripts/levelup/scripts/levelup.rs2` (19 skills, one trigger each,
`:3-21`) jumps to a shared `[label,levelup]` that:

1. looks up the skill's row in a `levelup.dbtable` (columns `stat, interface,
   title, body, message, level_prefix, levelup_jingle, unlocks_jingle,
   unlocks_levels`),
2. sends the chat message **and** plays a jingle,
3. **and separately** does `if_settext(title,...)`, `if_settext(body,...)`,
   `if_openchat(<interface>)` against one of 19 *separate* per-skill `.if`
   files (`levelup_<skill>.if`, each with a `line1`/`line2` pair at the same
   geometry osrs239's `text1`/`text2` occupy).

The precedent is unambiguous: **congratulation message and popup are both
present, and both server/content-driven** — never left to client inference.
osrs239 consolidates LostCity's 19 separate interfaces into one
(`levelup_display`, 233) with 25 named hidden sub-panels, so the port is not
"one interface per skill" but "one shared interface, `cc_sethide`/equivalent
by skill name." The trigger side (`[advancestat,<skill>]`, 19 of them) is the
piece already flagged 94.7% ported in `docs/LOSTCITY_PORT_TRIAGE.md:317`
(`levelup 18/19`); this doc is about the **interface** those triggers still
need wired to, which is untouched.

Per `docs/PORTING_GUIDE.md` §2.4: the skill→panel name, the title/body text,
and which jingle plays are config-shaped and belong in a `.dbtable`/param
overlay content-side (following the `levelup.dbtable` shape above), not as a
switch statement in `mock230_combat.c`. The engine's job stays exactly what
it already does — detect the crossing and dispatch a trigger; the interface
open/populate belongs in the triggered content script.

---

## 4. What this doc does not cover

- Per-quest completion tracking beyond Cook's Assistant, and ops 3–6 / overview
  782 on the quest journal (named in §1.5).
- `chatbox_multi_addoption`'s body — missing from this decompiled corpus (§2.1);
  re-decompile directly from the live cache before porting.
- The "rest on demand" tail of the interface queue (`docs/LOSTCITY_PORT_TRIAGE.md`
  §7.3) — deliberately not surveyed here; each gets this same treatment when
  its driving script is next.
