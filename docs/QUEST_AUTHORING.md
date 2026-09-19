# Quest authoring

How to write `test/quests/<quest>.lua`. This is the page an author reads;
`test/quests/README.md` only points here now. The live verb set (with
file:line) is `tools/quest_gate/verb_list.py` -- read this file for the
*shape*, that one when a line here goes stale.

You write ONLY `test/quests/<quest>.lua` (and, if the quest needs one, a
fixture under `test/quests/fixtures/`). Never edit `script/plugins/`, `src/`,
`tools/`, or `OSRS-Content/` to make your quest pass -- if the driver is
missing something, that is a `needs:` in your report, not an edit here.

## 1. The test shape

```lua
return {
    id = "cooks_assistant",
    fixture = "fresh_lumbridge.ini",
    -- Setup STAGES a quest; it never finishes one (trap 8). `::cook` is the
    -- quest's own reset debugproc -- cookquest back to 0, the ingredients
    -- cleared, the player put beside the Cook.
    setup = { "::cook" },

    run = function(t)
        -- bind touches the world not at all: it records the varp, the
        -- constants, the display name and the points a later expect_stage/
        -- expect_complete checks, and reads %qp now for that delta.
        t.quest.bind({
            varp = "cookquest",
            constants = { not_started = 0, started = 1, complete = 2 },
            display = "Cook's Assistant",
            points = 1,
        })
        t.t.expect("cook.reset", t.quest.expect_stage("not_started"))

        -- t["do"](name, verb, ...) calls verb(...), writes the row `name`
        -- from the verb's own (result, detail), and SHOOTS it. A plain
        -- t.expect row does not: photograph the clicks, not the reads.
        t.t["do"]("cook.greet", t.player.talk_to, "cook")

        -- chat.play answers (ok, nil), so t["do"] would grade it hollow
        -- (trap 12): call it directly and write the detail yourself.
        local played, played_detail = t.chat.play({
            "npc:What am I to do",
            "choose:What's wrong?",
            "player:What's wrong",
            "npc:terrible mess",
            "npc:forgotten to buy",
            "choose:Yes, I'll help you.",
        })
        t.t.check("cook.accept", played, played_detail or "six pages, accepted")
        t.t.expect("cook.started", t.quest.expect_stage("started"))

        -- ... gather, talk to the Cook a SECOND time, dismiss the mesbox ...
        t.quest.expect_complete()   -- four rows, and it closes the scroll
        t.t.finish(0)
    end,
}
```

An excerpt, not a whole quest: every line was run against the real client
(the six `chat.play` pages and both `expect_stage` rows are a live green
ledger, 2026-09-19), but a file this short fails section 6's minimum shape.
The complete green file is `test/quests/cooks_assistant.lua` -- read it
next, and `test/quests/hans.lua` for a test with no quest varp at all.

## 2. The verb table

One line per verb, `t.<call>` as a quest file spells it, grouped by
namespace. `(result, detail)` unless noted. Full banners: the `.lua` file
named in brackets.

### `t` -- the scheduler (`core.lua`, `ui.lua`)

- `t.t.cheat(text, wait_for_reply=true)` -> `ok` `refused` `no_row`. Reaches content debugprocs then the server ladder (`::give ::setlevel ::setvar ::kill ::spawn ::tele`). Awaits any new chat line (<=5 ticks) unless `wait_for_reply=false`.
- `t.t.ticks(n)` -> `ok` `timeout`. Advances the clock exactly `n` SERVER ticks.
- `t.t.settle()` -> `ok` `timeout`. Waits for `api.drive.settled()`.
- `t.t.finish(code)`. Writes SUMMARY, ends the process. Does not stop the Lua script -- `return` right after it.
- `t.t.step(name, verdict, detail)` -> `(bool, detail)`. Manual ledger row; you already know the verdict.
- `t.t.expect(name, result, detail)` -> the pair unchanged. PASS iff `result == "ok"`.
- `t.t.check(name, condition_or_result, detail)` -> the pair unchanged. PASS iff `true` or `"ok"`; takes its own shot(s).
- `t.t["do"](name, verb, ...)` -> the wrapped verb's own `(result, detail)`. FAIL `hollow` if `ok` with a nil detail; FAIL `bad verb/target` if `verb` is not a function or the first arg is nil. Auto-shoots.
- `t.t.blocked(reason)`. Writes `BLOCKED`, shoots, calls `t.t.finish(0)`. `return` right after it.
- `t.t.key(name)` -> `ok`/press-release error; `t.t.text(str)` -> `ok`/error.
- `t.t.shot(name)` -> `ok` (path) `refused` `timeout`. `t["do"]`/`t.check` call this for you; call it yourself only for a bare narrative shot.
- `t.await({level=fn, event="server_tick"|"sub_mounted"|..., note=text}, ticks)` -> `ok` `timeout`. The await primitive every other verb is built on, for a settle no verb covers. `level` is polled; the deadline is SERVER ticks.

### `quest` (`quest.lua`)

- `t.quest.bind{varp=, constants={...}, row=, display=, journal_title=, points=}` -> `ok` `refused`. `constants.complete` is required. No world read.
- `t.quest.stage()` -> `(result, value)` from `var.varp(bound.varp)`.
- `t.quest.expect_stage(name_or_value)` -> `ok` `refused` (names the side that disagreed).
- `t.quest.expect_complete()` -> `ok` `refused`. Writes FOUR rows itself: `quest.varp_complete`, `quest.scroll_title`, `quest.points`, `quest.journal` (closes the reward scroll first). Never calls `::complete`.

### `chat` (`chat.lua`, `read.lua`)

- `t.chat.kind()` -> a bare string, no await: `npc` `player` `mesbox` `objbox` `options` `count` `name` `other_input` `none`.
- `t.chat.continue_()` -> `ok` `unsupported` `refused` `not_visible` `closed` `timeout`.
- `t.chat.drain{stop_at=, max_pages=, shots=true}` -> `(ok, kind)` `timeout`. Clicks through until `stop_at` or a terminal kind.
- `t.chat.close()` -> `ok` (idempotent).
- `t.chat.count(n)` -> `ok` `unsupported` (not a quantity prompt).
- `t.chat.name_entry(text)` -> `ok` `unsupported` (not a name prompt).
- `t.chat.options()` -> `(ok, rows)`.
- `t.chat.options_title()` -> `(ok, title)`.
- `t.chat.choose(selector)` -> `ok` `no_row` `refused`. `selector` is a 1-based index, exact row text, or `/lua pattern/`.
- `t.chat.play(list)` -> `ok` `mismatch` `unsupported`, or a wrapped verb's own result. See trap 3.
- `t.chat.text()` -> `(ok, text)`, awaits up to 10 ticks. `t.chat.head()` / `t.chat.name()` / `t.chat.item()` -> `(ok, <model|text|items>)` `unsupported`.
- `t.chat.expect_text(substring)` / `t.chat.expect_head(npc)` / `t.chat.expect_item(obj)` -> `ok` `not_found`.

### `scroll` / `levelup` (`read.lua`)

- `t.scroll.title()` -> `(ok, {name, points})` `not_visible`.
- `t.scroll.rewards()` -> `(ok, {lines, icon})` `not_visible`.
- `t.scroll.reward_xp(skill)` -> `(ok, xp)` `no_row`. Parses `<n> <Skill> XP`, case-insensitive, unscaled.
- `t.scroll.close()` -> `ok` `no_row`.
- `t.levelup.skill()` / `t.levelup.continue_()` -> `unsupported` (`covered`) always in this content pack -- no opener (plan U16).

### `var` / `inv` / `msg` / `skill` (`state.lua`)

- `t.var.varp(name)` / `t.var.varbit(name)` -> `(ok, value)`; `t.var.server(name)` is the same read server-side, varp-or-varbit-transparent.
- `t.var.await(name, value, ticks=10)` / `t.var.await_server(...)` -> `ok` `timeout` (client-side, server-side).
- `t.var.expect(name, value)` -> `ok` `refused`. Requires client == server == value.
- `t.inv.count(name)` -> `(ok, total)`; `t.inv.has(name)` -> `(ok, bool)`; `t.inv.slot(index)` -> `(ok, {name, count})`.
- `t.inv.expect_has(name, count)` -> `ok` `refused`. `t.inv.expect_absent(name)` -> `ok` `refused`.
- `t.inv.await(name, count, ticks=10)` -> `ok` `timeout`.
- `t.inv.await_all({name=count,...}, ticks=10)` -> `ok` `timeout` (detail lists what is short).
- `t.msg.last(n)` -> `(ok, list)`; `t.msg.expect(substring)` -> `ok` `refused`; `t.msg.await(substring, ticks=10)` -> `ok` `timeout`, only lines newer than the call.
- `t.skill(name)` -> `(ok, reading)`. A function, not a table -- `t.skill("cooking")`, never `t.skill.cooking`.
- `t.skill_snapshot()` -> `(ok, table)`, every stat read once.
- `t.skill_expect_gain(name, xp, snapshot)` -> `ok` `refused` `no_row`. Accepts `xp` or `xp*10`, names the unit matched.

### `ui` / `npc` (`ui.lua`)

- `t.ui.open(interface, cheat_text)` -> `ok` `no_row` `refused`, then awaits the mount.
- `t.ui.await_open(interface, ticks=20)` / `t.ui.await_close(...)` -> `ok` `timeout` `no_row`.
- `t.ui.widget(sym, sub)` -> `(ok, component_id)`; `t.ui.invoke(widget, op)` -> `ok`/error; `t.ui.tab(name)` -> `ok` `no_row` (a number passes straight through); `t.ui.is_modal()` -> `(ok, bool)`.
- `t.ui.journal_open(display_name)` -> `(ok, {title, first_line, lines, line_count, complete})` `not_visible` `refused` `timeout`; no argument reads whatever is open. `t.ui.journal_read()` is the same with no click; `t.ui.journal_close()` -> `ok` `not_visible`.
- `t.npc.by_name(name)` / `t.npc.by_symbol(sym)` / `t.npc.nearest(sym, radius)` -> `(ok, row)` `not_found` `no_row`.
- `t.npc.await_present(sym, radius, ticks=10)` / `t.npc.await_gone(...)` -> `ok` `timeout`.

### `world` / `drive` / `player` (`world.lua`, `pointer.lua`)

- `t.world.loc_near(sym, radius)` / `t.world.obj_near(sym, radius)` -> `(ok, {kind,id,element_id,tile_x,tile_z,level,...})` `not_found`.
- `t.world.tile()` -> `(ok, {x,z,level})`. `t.world.level()` -> `(ok, level)`.
- `t.drive.screen_position(target)` -> `(ok, pos)` `not_visible`. `target = {kind="npc"|"loc"|"obj", id=...}`.
- `t.drive.click_minimenu(target, option, deadline=4)` -> `(ok, {row_text, row_action})` `covered` `not_visible` `timeout`. `option` is a 1-based op slot, `"examine"`, or `"select"` (held-item wildcard).
- `t.drive.op(target, option)` -> the logged bypass, never the default -- leaves a `note` in the next row's detail.
- `t.drive.camera(yaw, pitch, zoom)` -> `ok`.
- `t.player.by_symbol(kind, name)` -> `(target, "ok")` or `(nil, result, name)` -- reversed order from every other verb here.
- `t.player.walk_to(x, z, ticks=distance+10)` / `t.player.walk_near(target, ticks)` / `t.player.idle()` -> `ok` `timeout` (`unsupported` -- walk_near is npc/loc only).
- `t.player.teleport(name)` -> `(ok, "x,z L<level>")` `timeout` `refused` `no_row`. See trap 1's cousin below.
- `t.player.talk_to(npc, op=1)` -> `ok`/click_minimenu's results.
- `t.player.click_loc(loc, op=1)` -> same, walks into range first.
- `t.player.click_obj(obj, op=3)` -> same, waits for the backpack count to rise.
- `t.player.inv_op(item, op=1)` -> `ok`/error. A numbered held op; op<0 is refused (that is `use_on`'s arming half).
- `t.player.equip(item)` -> `ok` `refused` (worn count must rise).
- `t.player.drop(item)` -> `ok` `timeout` (backpack falls AND a stack lands on the ground).
- `t.player.use_on(item, target)` -> `ok` `unsupported` `refused`. Arms the item, then `click_minimenu(target, "select")`.

## 3. Result vocabulary

`ok timeout not_found refused covered no_row not_visible closed unsupported`
-- fixed, never add one. `mismatch` and `hollow` are two verbs' own local
words (`chat.play`, the hollow rule), not part of the fixed set. Ledger
verdicts are `PASS`, `FAIL`, `BLOCKED`.

## 4. Twelve traps

1. **Server ticks.** `t.t.ticks(n)`, every `ticks=` argument, every await
   deadline: all SERVER ticks. One tick is 30 client cycles. `t.ticks(10)`
   is not "wait a bit", it is 200ms of client time for the world to answer
   one thing.

2. **Content symbols, not display names -- and where to find one.** A
   target is always a content symbol (`"cook"`, `"cookquest"`,
   `"bucket_of_milk"`), never a numeric id, never a client-lane spelling.
   Look one up in `OSRS-Content/osrs239-content/configs/all.*.compack`
   (`lint_quest.py` checks against the same file) or
   `tools/quest_gate/quest_inventory.tsv`/the Quest Helper extract for the
   quest's own npc/obj/loc names. `quest.bind`'s `display=` is the
   quest-LIST's own row text (what `ui.journal_open` searches for); the
   journal's own title can differ -- that is what `journal_title=` is for.

3. **Exact chat row text, and the `/pattern/` form.** `chat.choose` and
   `chat.play`'s `"choose:<sel>"` take a 1-based index, the row's EXACT
   text, or a `/lua pattern/` (leading and trailing `/`) -- never a
   description of the row. Spell it wrong and you get `no_row`, not a
   fuzzy match.

4. **Two identical screenshots fail the gate.** `gate.py` MD5-compares
   every PNG in a quest's `shots/` directory; any two byte-identical files
   anywhere in it are a red finding, even across unrelated steps. Do not
   shoot the same static page twice on purpose.

5. **No `pcall` -- a guard that raises ends the whole run. `t["do"]` does
   this for you.** A raw table handed to `t.t.step`/`t.t.expect` as
   `detail` raises inside `api.drive.ledger` (no `pcall` catches it), and
   the run dies at that row with everything after it unreached. `t["do"]`
   stringifies any table detail automatically -- prefer it over hand-rolled
   `t.t.step` calls for exactly this reason.

6. **Fixture varps are perm-only.** A fixture's `[varps]` section may only
   carry `scope=perm` vars. A temp var belongs to the server, not the
   fixture; pinning one there is pinning a value the next tick can
   silently overwrite, which reads as a flaky test.

7. **Never edit `script/plugins/`, `src/`, `tools/`, or `OSRS-Content/`.**
   Your quest file is the only thing you can change to make a run green.
   If a verb is missing or wrong, that is a report item, not a local
   workaround.

8. **Never `::complete` your own quest.** `quest.expect_complete` reads
   whatever the playthrough already put there; a setup cheat that jumps
   straight to completion (`::complete`, or hand-writing the varp) proves
   nothing was played and defeats the point of the test.

9. **`--no-build` for iteration.** `run.py` rebuilds the shared binary and
   the server script pack on every call unless told not to; while writing
   a quest, pass `--no-build` (Lua/Python-only changes never need a
   rebuild) or you pay a full build every retry.

10. **Read the ledger and the failure block, not `client.log`.**
    `client.log` is the raw process log and is not the evidence; a step's
    verdict is its `ledger.tsv` row, and a failing quest's cause is
    `run.py`'s own printed failure block (name below).

11. **Never pass `shots=false` to `chat.drain` to dodge trap 4.** Drain's
    per-page shot is also a frame PUMP that a nearby `chat.continue_`
    relies on to see `UITree_SetPausePending` clear; without it the next
    `continue_` answers `refused -- a resume is already outstanding`
    (measured 2026-09-19 on `cooksassistant.handin_drain`). Two of drain's
    own pages colliding on MD5 is a driver report, not a knob to turn.

12. **A verb that answers `ok` with no detail can never go through
    `t["do"]`** -- the hollow rule grades that FAIL. Today: `t.chat.play`,
    `t.chat.continue_`, `t.t.cheat`, `t.inv.await_all`,
    `t.inv.expect_absent`. Call each directly and record it with
    `t.t.check`/`t.t.step`, writing the detail yourself (the counts you
    read back, the pages you walked) -- an empty detail column is a row
    that proves nothing to whoever reads the ledger later.

## 5. Running a quest

```sh
# First run (or after a C change): builds, runs, publishes on green.
python3 tools/quest_gate/run.py <quest>

# Iterating on the Lua only:
python3 tools/quest_gate/run.py <quest> --no-build

# Every quest, no publish (leaves OSRS-Content's copy alone):
python3 tools/quest_gate/run.py --all --no-build --no-publish

# A scratch script, not a quest table (probing one verb):
python3 tools/quest_gate/run.py --script build/scratch.lua --name scratch --no-build

# The verdict (read this, not run.py's exit code, for WHY):
python3 tools/quest_gate/gate.py <quest>
```

A non-green run prints a `---- failures ----` block: the last
FAIL/BLOCKED row's name and detail, its own `<name>-FAIL.png` if one was
captured (or `TIMEOUT.png` on a wall-clock timeout), and the last few
`QUEST ...`/content `mes` lines from `client.log`. Start there -- it is
built to be the one screenful you need without re-running anything.
Artefacts: `build/quest_gate/<quest>/` (`ledger.tsv`, `shots/NN-name.png`,
`client.log`), replaced on every run.

## 6. Definition of done

From `tools/quest_gate/gate.py`, current as of this writing -- re-read that
file if this drifts. A quest is green when:

- Every `ledger.tsv` row is `PASS` (a `BLOCKED` row is its own bucket, not
  a pass and not a fail -- see below).
- The ledger has a trailing `SUMMARY` row whose counted `pass=`/`fail=`
  agree with the rows, whose verdict is `PASS` iff `fail==0`, and whose
  `blocked=` token (present only when count>0) agrees too.
- Every row's claimed shot exists on disk and is >=1000 bytes.
- No two shots in `shots/` share an MD5.
- No shot's top-left 64x64 corner matches the Character-Creator or
  pre-login fingerprint (`tools/quest_gate/fingerprints/`) -- a run that
  never got past boot/login cannot pass by having every verb answer
  `refused`/`timeout` quietly.
- **Minimum shape**, gated on your file actually using the verb each rule
  is about (an older `t.step`/`t.expect`-only file is exempt from the
  `quest.bind`/`t["do"]` rules below, but never from the row/shot counts):
  - at least 8 step rows and at least 4 PNGs;
  - if you call `quest.bind`: at least one `quest.*` row, and either a
    passing `quest.varp_complete` or the ledger's last row is `BLOCKED`
    (a tier-4 stub must end there, not fall through);
  - every row YOU wrote with `t["do"]` or `t.t.check` carries a shot. The
    rule is per row and reads your source with the comments stripped
    (`gate.py`'s `shooting_row_names`), so naming the verb in a comment
    switches nothing on, and a `t.t.expect`/`t.t.step` row -- which has
    never shot anything -- is never asked for one.
- A `BLOCKED` row with `fail==0` reports `blocked`, not `green`, and still
  exits non-zero unless the check is run with `--allow-blocked`.
