# Quest authoring

How to write `test/quests/<quest>.lua` -- the ONLY file you write, full stop (section 2). `test/quests/README.md` only points here; the live verb set with file:line is `tools/quest_gate/verb_list.py`, read this file for the *shape*, that one when a line here goes stale.

## 1. The test shape

```lua
return {
    id = "cooks_assistant",
    fixture = "fresh_lumbridge.ini",
    -- Setup STAGES a quest, never finishes one (trap 8): `::cook` resets
    -- the varp, clears ingredients, puts the player beside the Cook.
    setup = { "::cook" },

    run = function(t)
        -- bind touches the world not at all: it records varp/constants/
        -- display/points for later, and reads %qp now for that delta.
        t.quest.bind({
            varp = "cookquest",
            constants = { not_started = 0, started = 1, complete = 2 },
            display = "Cook's Assistant",
            points = 1,
        })
        t.ticks(3)  -- a setup cheat's effect is not client-side yet
        t.expect("cook.reset", t.quest.expect_stage("not_started"))

        -- t.exec(name, verb, ...) writes row `name` from (result, detail)
        -- and SHOOTS it; t.expect does not -- photograph clicks, not reads.
        t.exec("cook.greet", t.player.talk_to, "cook")

        -- chat.play answers ("ok", "<N> page(s): ..."), a real detail, so
        -- it goes through t.exec like any other verb.
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

An excerpt, not a whole quest (every line above ran and PASSed, but a file
this short fails section 7's minimum shape). Read the complete green files
next: `test/quests/cooks_assistant.lua`, then `hans.lua` (no quest varp).

## 2. Where you start, and how to get there

- The fixture (`fresh_lumbridge.ini`) stands you beside Hans at 3206,3233, level 0 -- nothing else moves you closer. Npcs are ALL spawned and live from boot; nothing needs summoning.
- The scaffold emits `t.exec("goto-...", t.player.goto_tile, x, z, level)` -- the
  engine's `::goto` cheat plus an arrival await -- before every far step.
  `goto_tile`, never `goto`: `goto` is a reserved word in this tree's Lua and does
  not parse. `level` is the plane, 0-3, default 0; x/z may land a tile out (the
  world picks the nearest tile it accepts), the plane never does.
  `screen_position` / `not_visible` / `no_row` from a `talk_to` or npc lookup means
  you are not standing there: **fix the goto's coordinates, never the verb** -- the scaffold walks to the npc's own `configs/*.spawn` row (trap 13; `configs/*.npc` is definition blocks, no tiles in them at all) and falls back to Quest Helper's `WorldPoint(x, z, level)` only where no spawn row exists; a LOC's tile is its area's `configs/*.loc` row (upper floors are level 1/2, never 0).
- `walk_near(target, ticks)` takes the `{kind=, id=}` table `player.by_symbol` returns: `local n = t.player.by_symbol("npc", "doric")` then `t.exec("walk.doric", t.player.walk_near, n, 10)`.
- **`outcome blocked` REQUIRES a `t.blocked("<seam>")` row, then `return` right after it** -- anything else is rejected, not blocked.
- `chat.play` / `chat.choose` / `chat.continue_` act on a dialogue already open, never opening one; `not_visible: no dialogue is open` means the click before them did not land -- fix that click, not the chat call.
- Doors: `I can't reach that!` after a click means a door, gate or wall blocks the path -- `click_loc` it (op 1, its symbol from the area's `configs/*.loc`) or `goto_tile` past it; `talk_to` now answers `refused` with that line, not `ok`.
- Inventory: the fresh character carries fourteen slots of tutorial kit (content's `[proc,newplayer_inv]`, granted a tick after login); every generated file's `setup` starts with `::clearinv` -- add it yourself too when hand-writing `setup`, and never in `run`: the runner waits for that grant before the first setup cheat, nothing waits for you.
- Floors and ladders: `goto_tile` the destination tile with ITS level is the whole of it -- it climbs stairs and ladders for you, no `click_loc` on the ladder first (druid reaches Sanfew at `2899,3429,1`, runemysteries the Duke at `3209,3222,1`, neither clicking anything); a scene that fails to load after a multi-region jump (`talk_to` answers `screen_position`, npc lookups `no_row`) is a known seam -- `t.blocked` it, naming the tile.

## 3. The verb table

One line per verb, `t.<call>` as a quest file spells it, grouped by namespace, `(result, detail)` unless noted; full banners in the `.lua` file named in brackets.

### the root of `t` -- the test's own controls, on the root table itself, with no second `t` inside it (`core.lua`, `ui.lua`)

- `t.cheat(text, wait_for_reply=true)` -> `ok` `refused` `no_row`. Reaches content debugprocs then the server ladder (`::give ::setlevel ::setvar ::kill ::spawn ::tele ::goto`). Awaits any new chat line (<=5 ticks) unless `wait_for_reply=false`.
- `t.ticks(n)` -> `ok` `timeout`. Advances the clock exactly `n` SERVER ticks. `t.settle()` -> `ok` `timeout`, waits for `api.drive.settled()`.
- `t.finish(code)`. Writes SUMMARY and ENDS THE RUN: no later row, no later shot, the script parks. A row attempted after it is refused with one stderr line, `quest-driver: row after finish ignored: <name>`. `return` right after it anyway -- what follows is unreachable.
- `t.step(name, verdict, detail)` -> `(bool, detail)`. Manual ledger row; you already know the verdict. `t.expect(name, result, detail)` -> the pair unchanged, PASS iff `result == "ok"`.
- `t.check(name, condition_or_result, detail)` -> the pair unchanged. PASS iff `true` or `"ok"`; takes its own shot(s).
- `t.exec(name, verb, ...)` -> the wrapped verb's own `(result, detail)`. FAIL `hollow` if `ok` with a nil detail; FAIL `bad verb/target` if `verb` is not a function or the first arg is nil. Auto-shoots.
- `t.blocked(reason)`. Writes `BLOCKED`, shoots, then finishes -- same terminal rule, so a tier-4 stub ENDS at its `t.blocked` and the ledger's last row is that one. `return` right after it.
- `t.key(name)` -> `ok`/press-release error; `t.text(str)` -> `ok`/error.
- `t.shot(name)` -> `ok` (path) `refused` `timeout`. `t.exec`/`t.check` call this for you; call it yourself only for a bare narrative shot. A capture byte-identical to the last picture the run WROTE is answered `ok` with the detail `unchanged since <that shot>` and no file on disk: the row then carries NO shot and `[frame unchanged]` in its detail (trap 4). A `<name>-FAIL` capture is never suppressed.
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

- `t.world.loc_near(sym, radius)` / `t.world.obj_near(sym, radius)` -> `(ok, {kind,id,element_id,tile_x,tile_z,level,...})` `not_found`. A LOC symbol resolves to the id the scene actually holds and the row's `match` names the rule -- `exact`, `base`, or `multiloc` (trap 20).
- `t.world.tile()` -> `(ok, {x,z,level})`. `t.world.level()` -> `(ok, level)`.
- `t.drive.screen_position(target)` -> `(ok, pos)` `not_visible`. `target = {kind="npc"|"loc"|"obj", id=...}`.
- `t.drive.click_minimenu(target, option, deadline=4)` -> `(ok, {row_text, row_action})` `covered` `not_visible` `timeout`. `option` is a 1-based op slot, `"examine"`, or `"select"` (held-item wildcard).
- `t.drive.op(target, option)` -> the logged bypass, never the default -- leaves a `note` in the next row's detail.
- `t.drive.camera(yaw, pitch, zoom)` -> `ok`.
- `t.player.by_symbol(kind, name)` -> `(target, "ok")` or `(nil, result, name)` -- reversed order from every other verb here.
- `t.player.walk_to(x, z, ticks=distance+10)` / `t.player.walk_near(target, ticks)` / `t.player.idle()` -> `ok` `timeout` (`unsupported` -- walk_near is npc/loc only).
- `t.player.teleport(name)` -> `(ok, "x,z L<level>")` `timeout` `refused` `no_row`. See trap 1's cousin below.
- `t.player.goto_tile(x, z, level=0)` -> `(ok, "x,z,level")` `timeout` `no_row`. An ABSOLUTE tile (the WorldPoint Quest Helper prints for every step) through `::goto`; returns only once the tile AND the npc pool around it are visible. Use it before the first `talk_to` of any step the fixture does not already stand at; `teleport(name)` is for destinations that have a NAME in `tele_destinations.rs2`.
- `t.player.talk_to(npc, op=1)` -> `ok`/click_minimenu's results.
- `t.player.click_loc(loc, op=1)` -> same, walks into range first.
- `t.player.click_obj(obj, op=3)` -> same, waits for the backpack count to rise.
- `t.player.inv_op(item, op=1)` -> `ok`/error. A numbered held op; op<0 is refused (that is `use_on`'s arming half).
- `t.player.equip(item)` -> `ok` `refused` (worn count must rise).
- `t.player.drop(item)` -> `ok` `timeout` (backpack falls AND a stack lands on the ground).
- `t.player.use_on(item, target)` -> `ok` `unsupported` `refused`. Arms the item, then `click_minimenu(target, "select")`.

## 4. Result vocabulary

`ok timeout not_found refused covered no_row not_visible closed unsupported`
-- fixed, never add one. `mismatch` and `hollow` are two verbs' own local
words (`chat.play`, the hollow rule), not part of the fixed set. Ledger
verdicts are `PASS`, `FAIL`, `BLOCKED`.

## 5. Twenty traps

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

4. **Two identical screenshots fail the gate -- but an empty `shots` column is
   not always a broken capture.** `gate.py` MD5-compares every PNG in `shots/`;
   any two byte-identical files are red. A capture identical to the LAST one
   written is dropped instead, its row's detail carrying `[frame unchanged]`
   (section 7 accepts that); a surviving duplicate is two NON-consecutive shots.

5. **No `pcall` -- a guard that raises ends the whole run. `t.exec` does
   this for you.** A raw table handed to `t.step`/`t.expect` as `detail`
   raises inside `api.drive.ledger` and the run dies there, everything after
   it unreached. `t.exec` stringifies a table detail -- prefer it.

6. **Fixture varps are perm-only.** A fixture's `[varps]` section may only
   carry `scope=perm` vars. A temp var belongs to the server; pinning one
   there pins a value the next tick overwrites -- a flaky test.

7. **Never edit `script/plugins/`, `src/`, `tools/`, `OSRS-Content/`, or
   anything else under `test/` -- no fixture, no helper `.lua`.** Your quest
   file is the only thing you can change to make a run green; a verb that is
   missing or wrong is a report item, not a local workaround.

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
    per-page shot is also a frame PUMP a nearby `chat.continue_` relies on to
    see `UITree_SetPausePending` clear; without it the next `continue_`
    answers `refused -- a resume is already outstanding` (2026-09-19).

12. **A verb that answers `ok` with no detail can never go through `t.exec`**
    -- the hollow rule grades that FAIL. Today: `t.cheat`, `t.inv.await_all`,
    `t.inv.expect_absent`. Call each directly and record it with
    `t.check`/`t.step`, writing the detail yourself (the counts you read back,
    the pages you walked) -- an empty detail column is a row that proves nothing
    to whoever reads the ledger later. `t.chat.play` and `t.chat.continue_` are
    off this list now -- both answer a real detail on `ok`. One leftover, NOT the
    hollow rule: a bare `t.exec("name", t.chat.continue_)` FAILs `bad
    verb/target` because `continue_` takes no target and `t.exec` refuses a nil
    first vararg. Pass a non-nil filler: `t.exec("n", t.chat.continue_, true)`.

13. **Finding an npc: use `*.spawn` rows, not just Quest Helper.** If `talk_to`/an npc lookup still answers `screen_position`/`no_row` at the WorldPoint tile, `grep -rn --include='*.spawn' "<symbol>" OSRS-Content/osrs239-content/server/scripts/` -- each hit is `symbol  x  z  level`, whitespace-separated, under an `==== NPC ====` header, one file per map square. Search the WHOLE of `server/scripts` as above, not just `areas/`: 972 of the 984 `*.spawn` files are `areas/world/configs`'s map squares but twelve are not, and a quest's own npcs are exactly what lives in those (`quests/quest_demon/configs`, `minigames/...`). `"location unknown"` is never a blocker -- the coordinate is in the tree.
14. **Name stage-check rows `quest.stage.<constant>`.** `quest.expect_complete()`'s own four rows satisfy section 7's ">=1 `quest.*` row" minimum shape automatically, but `quest.expect_stage` does not name its own row, and a run that ends `t.blocked` before hand-in never reaches `expect_complete` -- then only a stage row you named yourself counts. Name it `quest.stage.started`, never `quest_started` or `cook.started`: the gate matches the literal `quest.` prefix, not the intent.
15. **Never pad the shot count.** A shot exists because `t.exec`/`t.check` fired after a click that changed something -- never because the file was short of section 7's >=8-row, >=4-PNG minimum. Two byte-identical shots fail the gate (trap 4) whether or not they were padding, but reaching eight rows by repeating a no-op read is a rejection on its own.
16. **Never cheat the quest's own work.** An item, kill, craft, search or fetch the quest's own `.rs2` makes you do must be driven through clicks (`click_obj`, `use_on`, a real fight, `talk_to` + `chat.play`) -- `::give`/`::kill`/`::setvar` are for `setup` and for prerequisites Quest Helper lists as brought-along items (section 6's `canBeObtainedDuringQuest()` rule), never for the quest's own deliverable. The reviewer reads your file against the `.rs2` and rejects a hand-in it can show was cheated.
17. **Resolve every `-- CHECK` before the first run, not after.** `lint_quest.py` without `--allow-check` refuses a file that still has one; `--allow-check` is only for the scaffold's freshly generated output, never for something handed to review.
18. **A branch almost always opens with the PLAYER's line, not the npc's.** An `[opnpc1,...]` handler in these ports opens `~chatplayer_anim`/`~chatplayer_anim2` first (`brother_omad.rs2`'s `[label,omad_whats_wrong]`, `doctor_orbon.rs2`, `councillor_halgrive.rs2` all do) -- `chat.play` grades the page that is up and never skips ahead, so a list starting `"npc:..."` dies on page 1 with `expected kind=npc, got player`, and the red player name in that shot is the client being RIGHT. Read the handler's branch in its `.rs2` and spell every page in the order it actually opens; the generator emits the list from the script itself now -- verify it, do not rewrite it from memory.
19. **`screen_position` now names a reason after the colon.** A `not_found` reason one tile from an npc used to be the multinpc id bug -- a `multinpc1=` def's live entity carries a different `npc_id` than the symbol's (2,458 defs in this cache), fixed in `pointer.lua` -- if the same reason recurs after that fix, it is a real seam: `t.blocked` it, naming the npc symbol and the tile, not "stand somewhere else".
20. **The same is true of a LOC, and its detail now names the symbol.** A loc target is resolved by three rules in order, because the scenery pool stores the id the MAP or a zone packet named while `multilocN=` defs (4,675 in this pack) draw as a child: `exact` (the scene holds your symbol), `base` (you named a child, the map placed the wrapper -- the Wizards' Tower altar), `multiloc` (you named the wrapper, a `loc_change` put a child there -- The Restless Ghost's `openghostcoffin`, which nothing ever places). A detail reading `click_loc <sym> -> <other sym> (base|multiloc)` is the resolve WORKING, not a mis-click. `screen_position: no loc <id> (<symbol>)` after all three is a real seam -- `t.blocked` it. Doors in this pack are mostly not multilocs but `param=next_loc_stage` PAIRS, so `click_loc("<door>")` on an already-open door is `not_found` by design: name the `_open` half to close it again.

## 6. Picking one up, scaffolding it, running it

- Resuming: if `test/quests/<id>.lua` already exists and `queue.py show <id>` reports status `todo` with a `last_failure`, that file is the PREVIOUS author's rejected attempt -- read the failure and continue from it, never regenerate over it (`new_quest.py` refuses without `--force` anyway).

```sh
# The queue (test/quests/QUEUE.tsv) hands out the work, one row per test:
python3 tools/quest_gate/queue.py next --tier 1 --claim <you>   # claims it
python3 tools/quest_gate/queue.py show <test_id>                # that row
python3 tools/quest_gate/queue.py summary                       # tier x status
# Scaffold from the Quest Helper guide (refuses to clobber; --force overrides):
python3 tools/quest_gate/new_quest.py <test_id>
# Run: first time or after a C change; --no-build for Lua-only iteration
# (always pass it -- trap 9); --all --no-build --no-publish for every
# quest without touching OSRS-Content's copy:
python3 tools/quest_gate/run.py <quest>
python3 tools/quest_gate/gate.py <quest>   # the verdict; read this, not run.py's exit code
# Report back (status is one of todo green blocked content_bug):
python3 tools/quest_gate/queue.py set <test_id> --status green --owner <you>
python3 tools/quest_gate/queue.py set <test_id> --status blocked --failure "<why>"
```

The scaffold never `::give`s an item Quest Helper marks `canBeObtainedDuringQuest()`; it leaves a `-- CHECK gather` comment at the first step that needs it instead -- drive the gathering, or delete the marker and give it deliberately, but never leave the marker in.

A non-green run prints a `---- failures ----` block (last FAIL/BLOCKED row, its `-FAIL.png`/`TIMEOUT.png`, the last `client.log` lines) -- start there. Artefacts live in `build/quest_gate/<quest>/`, replaced every run.

## 7. Definition of done

From `tools/quest_gate/gate.py`, current as of this writing -- re-read that
file if this drifts. A quest is green when:

- Every `ledger.tsv` row is `PASS` (a `BLOCKED` row is its own bucket, not
  a pass and not a fail -- see below).
- The ledger has a trailing `SUMMARY` row whose counted `pass=`/`fail=`
  agree with the rows, whose verdict is `PASS` iff `fail==0`, and whose
  `blocked=` token (present only when count>0) agrees too.
- Every row's claimed shot exists on disk (>=1000 bytes); no two shots in `shots/` share an MD5.
- No shot's top-left 64x64 corner matches the Character-Creator or pre-login
  fingerprint (`tools/quest_gate/fingerprints/`) -- a run stuck at boot cannot pass by answering `refused`/`timeout` quietly.
- (the reviewer's rule, not `gate.py`'s) A reward row for every reward Quest Helper lists: `skill.snapshot()` before the hand-in, `skill.expect_gain`/a coins delta after `quest.expect_complete()` -- the scaffold emits both; a quest with a reward and no reward row is rejected.
- **Minimum shape**, gated on your file using the verb each rule is about
  (a `t.step`/`t.expect`-only file is exempt from the `quest.bind`/`t.exec`
  rules below, never from the row/shot counts):
  - at least 8 step rows and at least 4 PNGs;
  - if you call `quest.bind`: at least one `quest.*` row, and either a
    passing `quest.varp_complete` or the ledger's last row is `BLOCKED`
    (a tier-4 stub must end there, not fall through);
  - every row YOU wrote with `t.exec` or `t.check` carries a shot, unless
    its detail carries `[frame unchanged]` -- the driver took that picture,
    found it identical to the last and dropped it (trap 4). The rule is per
    row and reads your source with comments stripped (`gate.py`'s
    `shooting_row_names`), so a verb named in a comment switches nothing on,
    and a `t.expect`/`t.step` row is never asked for one.
- A `BLOCKED` row with `fail==0` reports `blocked`, not `green`, and still
  exits non-zero unless the check is run with `--allow-blocked`.

## 8. Gaps reported by authors

- Completion is asynchronous: `t.ticks(3)` between the final `chat.play`/`drain` and `quest.expect_complete()` is load-bearing, not padding. `chat.play` asserts a page list already read out of the `.rs2`; `chat.drain{stop_at="options"}` is for a branch you cannot spell until taken, but pass on its real `detail` and shot the menu yourself -- its own last shot is the page before it.
- A `choose:` entry is not always followed by a `player:` page: the echo exists only where that branch opens with `~chatplayer*` (Fred's `[label,fred_yes_okay]` does; Kaqemeex's first confirm goes mesbox -> options again with no player page between). Read the branch in its `.rs2` before spelling the `chat.play` list.
