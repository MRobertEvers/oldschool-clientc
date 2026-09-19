# Quest authoring

How to write `test/quests/<quest>.lua` -- the only file you write, plus a
fixture under `test/quests/fixtures/` if the quest needs one (trap 7). This
is the page an author reads; `test/quests/README.md` only points here. The
live verb set with file:line is `tools/quest_gate/verb_list.py` -- read this
file for the *shape*, that one when a line here goes stale.

## 1. The test shape

```lua
return {
    id = "cooks_assistant",
    fixture = "fresh_lumbridge.ini",
    -- Setup STAGES a quest; it never finishes one (trap 8). `::cook` is the
    -- quest's own reset debugproc -- varp to 0, ingredients cleared, the
    -- player put beside the Cook.
    setup = { "::cook" },

    run = function(t)
        -- bind touches the world not at all: it records varp/constants/
        -- display/points for a later expect_stage/expect_complete, and
        -- reads %qp now for that delta.
        t.quest.bind({
            varp = "cookquest",
            constants = { not_started = 0, started = 1, complete = 2 },
            display = "Cook's Assistant",
            points = 1,
        })
        t.ticks(3)  -- a setup cheat's effect is not client-side yet
        t.expect("cook.reset", t.quest.expect_stage("not_started"))

        -- t.exec(name, verb, ...) calls verb(...), writes the row `name`
        -- from its (result, detail), and SHOOTS it. A plain t.expect row
        -- does not: photograph the clicks, not the reads.
        t.exec("cook.greet", t.player.talk_to, "cook")

        -- chat.play answers ("ok", "<N> page(s): kind:fragment, ...") now,
        -- a real detail, so it goes through t.exec like any other verb.
        t.exec("cook.accept", t.chat.play, {
            "npc:What am I to do",
            "choose:What's wrong?",
            "player:What's wrong",
            "npc:terrible mess",
            "npc:forgotten to buy",
            "choose:Yes, I'll help you.",
        })
        t.expect("cook.started", t.quest.expect_stage("started"))

        -- ... gather, talk to the Cook a SECOND time, dismiss the mesbox ...
        t.quest.expect_complete()   -- four rows, and it closes the scroll
        t.finish(0)
    end,
}
```

An excerpt, not a whole quest: every line above ran against the real client
and every row PASSed, `cook.accept` with the detail `6 page(s): npc:What am
I to do?, options:choose:What's wrong?, player:What's wrong?, npc:Ooh dear,
I'm in a terrible me, npc:Unfortunately, I've forgotten , options:choose:Yes,
I'll help you.` (2026-09-19) -- but a file this short fails section 6's
minimum shape. The complete green file is `test/quests/cooks_assistant.lua`;
read it next, then `test/quests/hans.lua` (a test with no quest varp).

## 2. The verb table

One line per verb, `t.<call>` as a quest file spells it, grouped by
namespace. `(result, detail)` unless noted. Full banners: the `.lua` file
named in brackets.

### the root of `t` -- the test's own controls, on the root table itself, with no second `t` inside it (`core.lua`, `ui.lua`)

- `t.cheat(text, wait_for_reply=true)` -> `ok` `refused` `no_row`. Reaches content debugprocs then the server ladder (`::give ::setlevel ::setvar ::kill ::spawn ::tele`). Awaits any new chat line (<=5 ticks) unless `wait_for_reply=false`.
- `t.ticks(n)` -> `ok` `timeout`. Advances the clock exactly `n` SERVER ticks. `t.settle()` -> `ok` `timeout`, waits for `api.drive.settled()`.
- `t.finish(code)`. Writes SUMMARY, ends the process. Does not stop the Lua script -- `return` right after it.
- `t.step(name, verdict, detail)` -> `(bool, detail)`. Manual ledger row; you already know the verdict. `t.expect(name, result, detail)` -> the pair unchanged, PASS iff `result == "ok"`.
- `t.check(name, condition_or_result, detail)` -> the pair unchanged. PASS iff `true` or `"ok"`; takes its own shot(s).
- `t.exec(name, verb, ...)` -> the wrapped verb's own `(result, detail)`. FAIL `hollow` if `ok` with a nil detail; FAIL `bad verb/target` if `verb` is not a function or the first arg is nil. Auto-shoots.
- `t.blocked(reason)`. Writes `BLOCKED`, shoots, calls `t.finish(0)`. `return` right after it.
- `t.key(name)` -> `ok`/press-release error; `t.text(str)` -> `ok`/error.
- `t.shot(name)` -> `ok` (path) `refused` `timeout`. `t.exec`/`t.check` call this for you; call it yourself only for a bare narrative shot.
- `t.note(text)`. Free text folded into the NEXT row's detail -- why a verb answered as it did, without a row of its own.
- `t.await({level=fn, event="server_tick"|"sub_mounted"|..., note=text}, ticks)` -> `ok` `timeout`. The await primitive every other verb is built on, for a settle no verb covers. `level` is polled; the deadline is SERVER ticks.

### `quest` (`quest.lua`)

- `t.quest.bind{varp=, constants={...}, row=, display=, journal_title=, points=}` -> `ok` `refused`. `constants.complete` is required. No world read.
- `t.quest.stage()` -> `(result, value)` from `var.varp(bound.varp)`.
- `t.quest.expect_stage(name_or_value)` -> `ok` `refused` (names the side that disagreed).
- `t.quest.expect_complete()` -> `ok` `refused`. Writes FOUR rows itself: `quest.varp_complete`, `quest.scroll_title`, `quest.points`, `quest.journal` (closes the reward scroll first). Never calls `::complete`.

### `chat` (`chat.lua`, `read.lua`)

- `t.chat.kind()` -> a bare string, no await: `npc` `player` `mesbox` `objbox` `options` `count` `name` `other_input` `none`.
- `t.chat.continue_()` -> `ok` `unsupported` `refused` `not_visible` `closed` `timeout`. On `ok` the detail is `"<from kind> -> <to kind>"`; see trap 12 for the one `t.exec` form it still refuses.
- `t.chat.drain{stop_at=, max_pages=, shots=true}` -> `(ok, kind)` `timeout`. Clicks through until `stop_at` or a terminal kind.
- `t.chat.close()` -> `ok` (idempotent).
- `t.chat.count(n)` / `t.chat.name_entry(text)` -> `ok` `unsupported` (not that kind of prompt); the detail names what was entered.
- `t.chat.options()` -> `(ok, rows)`; `t.chat.options_title()` -> `(ok, title)`.
- `t.chat.choose(selector)` -> `ok` `no_row` `refused`. `selector` is a 1-based index, exact row text, or `/lua pattern/`.
- `t.chat.play(list)` -> `ok` `mismatch` `unsupported`, or a wrapped verb's own result. Entries: `npc:<substr>` `player:<substr>` `mesbox:<substr>` (or `:*` for that kind, text unchecked), `options`, `choose:<row|/pattern/>`, `count:<n>`, `name:<text>`, `end`, `*` (any one continuable page). On `ok` the detail is a per-page summary, so `t.exec` takes it straight.
- `t.chat.text()` -> `(ok, text)`, awaits up to 10 ticks. `t.chat.head()` / `t.chat.name()` / `t.chat.item()` -> `(ok, <model|text|items>)` `unsupported`.
- `t.chat.expect_text(substring)` / `t.chat.expect_head(npc)` / `t.chat.expect_item(obj)` -> `ok` `not_found`.

### `scroll` / `levelup` (`read.lua`)

- `t.scroll.title()` -> `(ok, {name, points})`; `t.scroll.rewards()` -> `(ok, {lines, icon})`; both `not_visible` when no scroll is up.
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
- `t.skill.read(name)` -> `(ok, reading)`. `t.skill` is a TABLE of three reads -- `t.skill.read("cooking")`, never `t.skill("cooking")` and never `t.skill.cooking`.
- `t.skill.snapshot()` -> `(ok, table)`, every stat read once.
- `t.skill.expect_gain(name, xp, snapshot)` -> `ok` `refused` `no_row`. Accepts `xp` or `xp*10`, names the unit matched.

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

1. **Server ticks.** `t.ticks(n)`, every `ticks=` argument, every await
   deadline: all SERVER ticks, one tick being 30 client cycles. `t.ticks(10)`
   is not "wait a bit"; it is 200ms for the world to answer one thing.

2. **Content symbols, not display names -- and where to find one.** A
   target is always a content symbol (`"cook"`, `"cookquest"`,
   `"bucket_of_milk"`), never a numeric id or a client-lane spelling. Look
   one up in `OSRS-Content/osrs239-content/configs/all.*.compack` (what
   `lint_quest.py` checks against) or `quest_inventory.tsv`. `quest.bind`'s
   `display=` is the quest-LIST row text `ui.journal_open` searches for; the
   journal's own title can differ -- that is `journal_title=`.

3. **Exact chat row text, and the `/pattern/` form.** `chat.choose` and
   `chat.play`'s `"choose:<sel>"` take a 1-based index, the row's EXACT
   text, or a `/lua pattern/` -- never a description of it. Spell it wrong
   and you get `no_row`, not a fuzzy match.

4. **Two identical screenshots fail the gate.** `gate.py` MD5-compares
   every PNG in a quest's `shots/`; any two byte-identical files are a red
   finding, even across unrelated steps. Never shoot a static page twice.

5. **No `pcall` -- a guard that raises ends the whole run. `t.exec` does
   this for you.** A raw table handed to `t.step`/`t.expect` as `detail`
   raises inside `api.drive.ledger` and the run dies there, everything after
   it unreached. `t.exec` stringifies a table detail -- prefer it.

6. **Fixture varps are perm-only.** A fixture's `[varps]` section may only
   carry `scope=perm` vars. A temp var belongs to the server; pinning one
   there pins a value the next tick overwrites -- a flaky test.

7. **Never edit `script/plugins/`, `src/`, `tools/`, or `OSRS-Content/`.**
   Your quest file is the only thing you can change to make a run green.
   If a verb is missing or wrong, that is a report item, not a local
   workaround.

8. **Never `::complete` your own quest.** `quest.expect_complete` reads
   what the playthrough put there; a setup cheat that jumps straight to
   completion (or hand-writes the varp) proves nothing was played.

9. **`--no-build` for iteration.** `run.py` rebuilds the shared binary and
   the script pack every call unless told not to; a Lua-only change never
   needs it, so pass `--no-build` or pay a full build per retry.

10. **Read the ledger and the failure block, not `client.log`.** A step's
    verdict is its `ledger.tsv` row and a failure's cause is `run.py`'s
    printed failure block; `client.log` is a raw log, not the evidence.

11. **Never pass `shots=false` to `chat.drain` to dodge trap 4.** Drain's
    per-page shot is also a frame PUMP a nearby `chat.continue_` relies on
    to see `UITree_SetPausePending` clear; without it the next `continue_`
    answers `refused -- a resume is already outstanding` (measured
    2026-09-19). Two drain pages colliding on MD5 is a driver report.

12. **A verb that answers `ok` with no detail can never go through
    `t.exec`** -- the hollow rule grades that FAIL. Today: `t.cheat`,
    `t.inv.await_all`, `t.inv.expect_absent`. Call each directly and record
    it with `t.check`/`t.step`, writing the detail yourself (the counts you
    read back, the pages you walked) -- an empty detail column is a row that
    proves nothing to whoever reads the ledger later. `t.chat.play` and
    `t.chat.continue_` are off this list now -- both answer a real detail on
    `ok`. One leftover, NOT the hollow rule: a bare `t.exec("name",
    t.chat.continue_)` FAILs `bad verb/target` because `continue_` takes no
    target and `t.exec` refuses a nil first vararg. Pass any non-nil filler,
    `t.exec("name", t.chat.continue_, true)`, which `continue_` ignores.

## 5. Picking one up, scaffolding it, running it

```sh
# The queue (test/quests/QUEUE.tsv) hands out the work, one row per test:
python3 tools/quest_gate/queue.py next --tier 1 --claim <you>   # claims it
python3 tools/quest_gate/queue.py show <test_id>                # that row
python3 tools/quest_gate/queue.py summary                       # tier x status

# Scaffold that row's file from its Quest Helper guide -- writes
# test/quests/<test_id>.lua, refuses to clobber one (--force overrides):
python3 tools/quest_gate/new_quest.py <test_id>

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

# Report back (status is one of todo green blocked content_bug):
python3 tools/quest_gate/queue.py set <test_id> --status green --owner <you>
python3 tools/quest_gate/queue.py set <test_id> --status blocked --failure "<why>"
```

The scaffold never `::give`s an item Quest Helper marks
`canBeObtainedDuringQuest()`: it leaves a `-- CHECK gather` comment at the
first step that needs it, because handing the ingredients over in setup
erases the gathering the test exists to prove. Drive the gathering, or
delete the marker and give it deliberately -- never leave the marker in.

A non-green run prints a `---- failures ----` block: the last FAIL/BLOCKED
row's name and detail, its `<name>-FAIL.png` (or `TIMEOUT.png` on a
wall-clock timeout), and the last few `QUEST ...`/content `mes` lines from
`client.log`. Start there. Artefacts: `build/quest_gate/<quest>/`
(`ledger.tsv`, `shots/NN-name.png`, `client.log`), replaced every run.
`--script` runs the file as-is: its `setup` cheats do NOT run.

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
  pre-login fingerprint (`tools/quest_gate/fingerprints/`) -- a run stuck at
  boot cannot pass by answering `refused`/`timeout` quietly.
- **Minimum shape**, gated on your file using the verb each rule is about
  (a `t.step`/`t.expect`-only file is exempt from the `quest.bind`/`t.exec`
  rules below, never from the row/shot counts):
  - at least 8 step rows and at least 4 PNGs;
  - if you call `quest.bind`: at least one `quest.*` row, and either a
    passing `quest.varp_complete` or the ledger's last row is `BLOCKED`
    (a tier-4 stub must end there, not fall through);
  - every row YOU wrote with `t.exec` or `t.check` carries a shot. The rule
    is per row and reads your source with comments stripped (`gate.py`'s
    `shooting_row_names`), so naming the verb in a comment switches nothing
    on, and a `t.expect`/`t.step` row is never asked for one.
- A `BLOCKED` row with `fail==0` reports `blocked`, not `green`, and still
  exits non-zero unless the check is run with `--allow-blocked`.
