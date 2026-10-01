# Verbs: `var` / `inv` / `msg` / `skill` (section 3)

Section 3's state-reading table. The varp/varbit carrier traps are in `gaps-combat.md` and
`gaps-world.md`.

## `var` / `inv` / `msg` / `skill` (`state.lua`)

### `t.var.varp(name)` / `t.var.varbit(name)`

`t.var.varp(name)` / `t.var.varbit(name)` -> `(ok, value)`; `t.var.server(name)` is the same read
server-side, varp-or-varbit-transparent, and answers `(result, value, source)`: `source` is `server`
(the client's copy of the server value) or `server content copy; no client copy` for a varp this
tree allocates above the cache's ids (`pack/varp.alloc`: `twocats_lamp_pick`, `dwarfrock_puzzle_*`),
which the server never sends to the client -- only the server's own copy can answer for those
(seam12).

### `t.var.await(name, value, ticks=10)` / `t.var.await_server(...)`

`t.var.await(name, value, ticks=10)` / `t.var.await_server(...)` -> `ok` `timeout` (client-side,
server-side). Detail on `ok`: `<name> = <value> (client|server <varp|varbit>) after K tick(s)`; a
timeout names the last value read. `await_server` works for `pack/varp.alloc` varps too; its detail
then reads `(varp, server content copy; no client copy)`.

### `t.var.expect(name, value)`

`t.var.expect(name, value)` -> `ok` `refused`. Requires client == server == value, so it answers
`not_found` for a `pack/varp.alloc` varp (no client copy): use `var.server`/`var.await_server` for
those.

### `t.inv.count(name)` -- also `t.inv.has`, `t.inv.slot`

`t.inv.count(name)` -> `(ok, total)`; `t.inv.has(name)` -> `(ok, bool)`; `t.inv.slot(index)` ->
`(ok, {name, count})`. An empty slot reads `{name='', count=0}`; obj id 0 (`mcannonremains`) is a
real item and reads by name (seam33: the driver's empty test is `obj_id < 0`, conformance row
`seam.inv_slot_obj_zero`).

### `t.clock.skip(minutes)` (`world.lua`, seam33)

`t.clock.skip(minutes)` -> `ok` (`date_minutes A -> B (+N skipped, world T min ahead; client
varp)`) `refused` (not a whole number >= 1, or the server's bound: 1-10080 per call, a year in all)
`no_row` (a binary built before `::clockskip`) `timeout`. Moves the embedded world's wall clock
forward for a wait measured in REAL minutes; the quest's own catch-up still does the work, so the
next row awaits the quest's effect. Lost on a relog. Details and caveats: gaps-world, "A step that
waits real minutes".

### `t.inv.expect_has(name, count)` -- also `t.inv.expect_absent`

`t.inv.expect_has(name, count)` -> `ok` `refused`. `t.inv.expect_absent(name)` -> `ok`
(`<name>: absent (count 0)`) `refused`.

### `t.inv.await(name, count, ticks=10)`

`t.inv.await(name, count, ticks=10)` -> `ok` (`<name> <before> -> <total> (>= N) after K tick(s)`)
`timeout`.

### `t.inv.await_all({name=count,...}, ticks=10)`

`t.inv.await_all({name=count,...}, ticks=10)` -> `ok` (`all held: a=n (>= m), ...`) `timeout`
(detail lists what is short).

### `t.msg.last(n)` -- also `t.msg.expect`, `t.msg.await`

`t.msg.last(n)` -> `(ok, list)`; `t.msg.expect(substring)` -> `ok`
(`matched: <the newest line containing it>`) `refused`; `t.msg.await(substring, ticks=10)` -> `ok`
(`matched: <line>`) `timeout`, only lines newer than the call.

### `t.skill.read(name)`

`t.skill.read(name)` -> `(ok, reading)`. `t.skill` is a TABLE of three reads --
`t.skill.read("cooking")`, never `t.skill("cooking")` and never `t.skill.cooking`.

### `t.skill.snapshot()`

`t.skill.snapshot()` -> `(ok, table)`, every stat read once.

#### `attempt to index a string value` on a snapshot, or a reading's `.xp` is nil (sonnet-b43)

*Origin: author batch sonnet-b43 (horror).*

Both reads answer the result string FIRST: `local snap = t.skill.snapshot()` holds `"ok"`, and
indexing it fails. Take the second value: `local _, snap = t.skill.snapshot()` (or
`select(2, t.skill.snapshot())`), then `snap.magic.experience`. A reading (`t.skill.read`'s second
value, and each entry of a snapshot) carries `.level` (current, after boosts and drains),
`.base_level` and `.experience`, in whole xp (`script/plugins/quest_driver/state.lua`
`QD.skill.expect_gain`). There is no `.xp`, so a reward of 4,662.5 reads as a delta of 4662.

### `t.skill.expect_gain(name, xp, snapshot)`

`t.skill.expect_gain(name, xp, snapshot)` -> `ok` `refused` `no_row`. Accepts `xp` or `xp*10`, names
the unit matched.
