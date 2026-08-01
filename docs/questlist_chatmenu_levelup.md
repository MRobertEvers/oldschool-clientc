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
> essentially done.** `questlist` and `levelup_display` are the real gaps.

---

## 0. Status at a glance

| interface | id | status | what's missing |
|---|---|---|---|
| `chatmenu` | 219 | **landed** | nothing server-side; two loose ends noted in §2.4 |
| `questlist` | 399 | **gap** | 7 varps/varbits never declared or written; no `%qp` accumulation |
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

None of these are declared or written anywhere under `OSRS-Content/osrs239-content/server/scripts`
(`grep -rniE "questpoint|questlist|qp_total|qp_max|varbit1782|var101\b" src/net/mock src/game` — zero
hits outside an unrelated selftest fixture, `mock_quest_progress`).

| state | read by | meaning | delivery | mock230 status |
|---|---|---|---|---|
| `%var101` (`qp`) | `script_1356:5` | current total quest points | varp transmit | **not declared** — `all.varp.compack:144` names it `qp` but has no `transmit=`/`scope=perm` overlay |
| `%varbit1782` (`qp_max`, backed by varp904) | `script_1356:5` | max obtainable QP (denominator) | varbit transmit via varp904 | **not declared** |
| `%varbit6347` (backed by varp904) | `script_5995:5` | quests completed count | varbit transmit via varp904 | **not declared** |
| `%varbit11877` (backed by varp2920) | `script_5995:5` | total quests available (denominator) | varbit transmit via varp2920 | **not declared** |
| `%var3717` bit 8 | `script_5849:3` | "in an active speedrun" — swaps QP/Completed for Speedrun-Trophies/Points | varp transmit | **not declared**, no speedrun system exists |
| `%var3365` | `script_5995:7` | speedrun score | varp transmit | **not declared** |
| `%varp3368/3369/3766` (backed by varbits 13632-13635, 6543) | `script_5893:3-7` | 5-tier speedrun trophy totals | varbit transmit | **not declared** |

The transport itself is generic and already works (`.varp` config `transmit=yes`
→ `src/net/mock/mock230_content.c:1635-1636`); the gap is entirely **content**:
no `.varp`/`.varbit` overlay declares these, and nothing computes `%qp`.

The one quest that's actually ported states the gap explicitly:
`OSRS-Content/osrs239-content/server/scripts/quests/quest_cook/scripts/quest_cook.rs2:154-156`
— *"The quest-point total is not tracked anywhere yet, so the count is stated
rather than accumulated."*

### 1.3 Corpus gap — read before porting

`~questlist_draw` (the actual list-population routine — the thing that walks
the 213 quest rows, sorts them, and creates the name rows) and
`~quicksort_questlist` are called from `script_1350`, `script_1340`, and
`script_5886`, but **no `[proc,questlist_draw]` exists anywhere in the 9,368
decompiled `.cs2` files** in this tree. Before implementing against this doc,
re-decompile it directly rather than guessing the body:

```sh
3rd/rscache/tools/cs2/cs2 decompile --rev osrs239 --cache cache.osrs239 --out /tmp/cs2 <questlist_draw's real id>
```

(id unknown — grep the live cache's script name table, not this corpus, since
this corpus is missing it.)

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
a sub-id; `mock230_world.c:3008-3038` sets `last_com`/**`last_slot`**/`last_verb`
from it. Content reads `last_slot` via `SS_OP_LAST_SLOT`
(`mock230_scripts.c:4647-4648`) and `chat.rs2`'s `p_choice2..5` branch on it.

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
// src/net/mock/mock230_combat.c:330-364
before = player->stat_level[stat];
...
player->stat_level[stat] = level_for_xp(...);
if( player->stat_level[stat] != before )
{
    if( stat == MOCK230_STAT_HITPOINTS )
        mock230_combat_sync_hitpoints(player);
    mock230_scripts_run_hook(srv, srv->hooks.combat_levelup_message, NULL, 0);
}
```

`combat_levelup_message` (`mock230.h:1301`, bound `mock230_scripts.c:200`)
dispatches to
`OSRS-Content/osrs239-content/server/scripts/skill_combat/combat.rs2:134-135`:

```
[proc,combat_levelup_message]
mes("You feel yourself getting stronger.");
```

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

- The 213-row `quest` dbtable's own content (per-quest completion tracking,
  `db_getfield` reads of columns beyond what §1 traces) — out of scope; only
  the header numbers (`%qp` and friends) were in the questlist onload's own
  var-transmit hooks.
- `chatbox_multi_addoption`'s and `questlist_draw`'s actual bodies — missing
  from this decompiled corpus (§1.3, §2.1); re-decompile directly from the
  live cache before porting either.
- The "rest on demand" tail of the interface queue (`docs/LOSTCITY_PORT_TRIAGE.md`
  §7.3) — deliberately not surveyed here; each gets this same treatment when
  its driving script is next.
