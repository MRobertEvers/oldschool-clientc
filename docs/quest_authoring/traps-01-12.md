# Traps 1-12 (section 5)

Section 5 of the manual ("Thirty-three traps"), traps 1-12. Trap numbers are unchanged; tests and
commit messages cite them.

## Trap 1. Server ticks.

`t.ticks(n)`, every `ticks=` argument, every await deadline: all SERVER ticks, one tick being 30
client cycles. `t.ticks(10)` is not "wait a bit"; it is 200ms for the world to answer one thing.

## Trap 2. Content symbols, not display names -- and where to find one.

A target is always a content symbol (`"cook"`, `"cookquest"`, `"bucket_of_milk"`), never a numeric
id or a client-lane spelling. Look one up in `OSRS-Content/osrs239-content/configs/all.*.compack`
(what `lint_quest.py` checks against) or `quest_inventory.tsv`. `quest.bind`'s `display=` is the
quest-LIST row text `ui.journal_open` searches for; the journal's own title can differ -- that is
`journal_title=`. Two obj symbols can share a display name too (a "Pigeon cage" is both `pigeons`
and `pigeoncage`) -- a screenshot's label cannot tell them apart, but `t.world.obj_near(sym, r)` and
`t.inv.count(sym)` can, and the ledger detail then names the id you actually picked up.

## Trap 3. Exact chat row text, and the `/pattern/` form.

`chat.choose` and `chat.play`'s `"choose:<sel>"` take a 1-based index, the row's EXACT text, or a
`/lua pattern/` -- never a description of it. Spell it wrong and you get `no_row`, not a fuzzy
match.

## Trap 4. Two identical screenshots fail the gate -- but an empty `shots` column is not always a broken capture.

`gate.py` MD5-compares every PNG in `shots/`; any two byte-identical files are red. A capture
identical to the LAST one written is dropped instead, its row's detail carrying `[frame unchanged]`
(section 7 accepts that); a surviving duplicate is two NON-consecutive shots. Content that REPEATS a
page word for word inside one chain produces exactly that (Merlin's Crystal's crate ride:
`You wait.` / `And wait...` twice, then `You wait...` / `And wait...` again after the voices): end
the `chat.play` list before the repeat, click the repeated pages through with a bare
`t.chat.continue_()` each preceded by `t.ticks(2)` (without the wait it answers
`resume outstanding`), recorded as a `t.step` row with its own result, then resume `chat.play` on
the first NEW line (sampler sonnet-b33, arthur).

## Trap 5. No `pcall` -- a guard that raises ends the whole run. `t.exec` does this for you.

A raw table handed to `t.step`/`t.expect` as `detail` raises inside `api.drive.ledger` and the run
dies there, everything after it unreached. `t.exec` stringifies a table detail -- prefer it.

## Trap 6. Fixture varps are perm-only.

A fixture's `[varps]` section may only carry `scope=perm` vars. A temp var belongs to the server;
pinning one there pins a value the next tick overwrites -- a flaky test.

## Trap 7. Never edit `script/plugins/`, `src/`, `tools/`, `OSRS-Content/`, or anything else under `test/` -- no fixture, no helper `.lua`.

Your quest file is the only thing you can change to make a run green; a verb that is missing or
wrong is a report item, not a local workaround.

## Trap 8. Never `::complete` your own quest.

`quest.expect_complete` reads what the playthrough put there; a setup cheat that jumps straight to
completion (or hand-writes the varp) proves nothing was played.

## Trap 9. `--no-build` for iteration.

`run.py` rebuilds the shared binary and the script pack every call unless told not to; a Lua-only
change never needs it, so pass `--no-build` or pay a full build per retry.

## Trap 10. Read the ledger and the failure block, not `client.log`.

A step's verdict is its `ledger.tsv` row and a failure's cause is `run.py`'s printed failure block;
`client.log` is a raw log, not the evidence.

## Trap 11. Never pass `shots=false` to `chat.drain` to dodge trap 4.

Drain's per-page shot is also a frame PUMP a nearby `chat.continue_` relies on to see
`UITree_SetPausePending` clear; without it the next `continue_` answers
`refused -- a resume is already outstanding` (2026-09-19). `chat.play` now does this waiting itself
-- each entry awaits the page being READABLE (the pause latch clear, and a page identity different
from the one the previous entry clicked) before it grades -- so a `t.ticks(N)` inserted before a
`chat.play` list to dodge a guessed race is no longer needed.

## Trap 12. A verb that answers `ok` with no detail can never go through `t.exec`

> CONFLICT (kept both): this trap (seam27) says `t.player.click_obj`'s `ok` now reads
> `click_obj: met after N tick(s)`; the first gap in `gaps-dialogue.md` still says it answers `ok`
> with a nil detail. The later passage (seam27, this one) wins; the detail still says only that the
> backpack total rose.

-- the hollow rule grades that FAIL. Today: `t.cheat` (and the plain waits `t.ticks`/`t.settle`).
**`gate.py` fails EVERY PASS row whose detail is empty (seam27; the driver's `[frame unchanged]`
marker does not count), whichever verb wrote it** -- vampire's `draynor.present` (sampler
sonnet-b27) and The Feud's `carpet-landed`/`safe-open` were such rows.
`t.npc.await_present`/`t.npc.await_gone`, `t.ui.await_open` and `t.await` answer a detail on `ok`
since seam27 (section 3), so they go through `t.exec`/`t.expect` like any verb -- as long as you
pass the second return on.

### What else is hollow, and what is off the list

`t.skill.snapshot()` answers `(ok, table)`, so a row written from its first value alone reads
`skill.snapshot -> ok` and proves nothing: put the xp you will diff against (`attack xp=13034531`)
in the detail. `t.var.await`, `t.var.await_server`, `t.var.expect`, `t.inv.await`,
`t.inv.await_all`, `t.inv.expect_absent`, `t.msg.expect`, `t.msg.await` and `t.quest.bind` are off
this list since seam7 (2026-09-22): each `ok` names what it read (the value and side, the count
reached, the matched line), so they go through `t.exec`/`t.expect` like any verb.

Note `t.inv.count` answers the `(result, count)` PAIR, not a bare number. Call a hollow verb
directly and record it with `t.check`/`t.step`, writing the detail yourself (the counts you read
back, the pages you walked) -- an empty detail column is a row that proves nothing to whoever reads
the ledger later.

### Grind debugprocs, `drop` with a shop open, `continue_` filler

A grind debugproc answers `ok (nil)` even when it did NOTHING (Mort'ton's `::mortton_repairtemple`
printed `after 0 repair(s)` on two of five calls, all five PASS -- sent back by sampler sonnet-b13):
read the stage/percent it moves before and after into the row, and let a no-op call fail. Same for
`t.player.drop` with a shop open: it dropped nothing, four tries a trip on every trip
(`dropped 4 shark(s), 4 left`), so drop before `shop.open`/`shop.attach` and read the count back.

`t.chat.play` and `t.chat.continue_` are off this list now -- both answer a real detail on `ok`. One
leftover, NOT the hollow rule: a bare `t.exec("name", t.chat.continue_)` FAILs `bad verb/target`
because `continue_` takes no target and `t.exec` refuses a nil first vararg. Pass a non-nil filler:
`t.exec("n", t.chat.continue_, true)`.

### `t.expect` is not an escape hatch; hollow on success

And `t.expect` IS NOT AN ESCAPE HATCH from that rule: routing a hollow verb through it dodges the
`t.exec` grader but still lands a PASS with a blank detail column, and `atailoftwocats` shipped
eighteen of them -- every chore confirmation and every stage gate from 40 to 70 -- where the ledger
cannot show what was read. Read the value back and write it yourself. Also hollow on SUCCESS:
`t.player.walk_to` (returns `ok, nil` once the tile is reached -- only a stall carries a detail,
`pointer.lua`), `t.ui.await_close` (a bare `ok` from the await, `ui.lua`) and `t.ui.invoke` -- plus
`t.player.click_obj` (section 8; since seam27 its `ok` reads `click_obj: met after N tick(s)`, which
says only that the backpack total rose). Call them directly and write what you read back: the tile,
the interface's presence, the count.
