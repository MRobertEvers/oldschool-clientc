# Verbs: `ui` and `npc` lookups (section 3)

Section 3's `ui` / `npc` table (`ui.lua`). The kill waits of the same namespace are in
`verbs-combat.md`; trap 33 covers IF1 buttons.

## `ui` / `npc` (`ui.lua`)

### `t.ui.open(interface, cheat_text)`

`t.ui.open(interface, cheat_text)` -> `ok` `no_row` `refused`, then awaits the mount.

### `t.ui.await_open(interface, ticks=20)` / `t.ui.await_close(...)`

`t.ui.await_open(interface, ticks=20)` / `t.ui.await_close(...)` -> `ok` `timeout` `no_row`.
`await_open`'s `ok` names it, `<interface> open (group N)` (seam27); `await_close` still answers a
bare `ok` with a NIL detail -- never the last argument of a PASS row.

#### Skill-multi menus

A SKILL-MULTI MENU (the make-X picker a `use_on` on a potter's wheel opens) is
`await_open("skillmulti")`, then `local r, cell = t.ui.widget("skillmulti:<letter>")` and
`t.ui.invoke(cell, 1)`, then an `inv.await` on the product (`onesmallfavour.lua`'s spinPotLid).

#### Fade overlays (`eadgar.lua`)

Worked example (`eadgar.lua`, the crate knockout): `await_open("fade_overlay")` FIRST -- it proves
the fade cycle started, where a bare `await_close` with nothing open reads `ok` at once -- then
`await_close("fade_overlay")`, then read the tile the fade teleported to; `canoe_cutscene.rs2` and
`eadgar_troll_sguard.rs2` are the content idiom behind it.

#### Readable books (seam28)

A READABLE BOOK (seam28) is interface 392 `book` (`~book_open`/`~book_spread`,
`interface_book/scripts/book.rs2`, ported from LostCity general/scripts/book.rs2): `inv_op` the
item's Read op, `t.ui.await_open('book')`, turn pages with
`t.ui.invoke(<book:page_right_button id>, 0)` (or the left one; the arrows are pause buttons -- look
the ids up with `t.ui.widget` AFTER await_open, they are nil before the mount), close with
`t.ui.invoke(<book:close_button>, 1)` or `t.key('escape')` and grade `is_modal()==false`.

Never `chat.play` a converted book (Grand Tree translation book and Glough's journal, the Dig Site
book on chemicals so far; the other ~mesbox books -- witches_diary, arrav_book, ... -- move as their
quests' parity passes convert them, and their tests move to await_open then). Assert the page text
with `t.ui.expect_text` (below, seam29) -- the PNG alone is not the evidence any more.

### `t.ui.text(component, sub)` / `t.ui.expect_text(component, want, ticks=5, sub)` (seam29)

`t.ui.text(component|id|list[, sub])` -> `(ok, plain_text)` `not_found` (the symbol does not resolve
or its interface is not open) `not_visible` (in the tree but hidden; the detail quotes the text). A
LIST reads a page spread over several rows as one string, rows joined by a space, empty rows skipped.
Markup comes off and a `|`/`<br>` break reads as a space. An empty reading is `ok ""`, which t.exec
grades hollow. `t.ui.expect_text(component, want[, ticks[, sub]])` -> `ok` `not_found`
`not_visible`: PASS when the text contains `want` (plain substring, or a Lua pattern written
`/.../`), waiting up to `ticks` for IF_SETTEXT to land; a miss quotes the last reading. Book pages,
journal lines and dial letters are asserted with it (s29w_after reads the Grand Tree translation
book's title and left page).

### `t.ui.widget(sym, sub)` -- also `t.ui.invoke`, `t.ui.tab`, `t.ui.is_modal`, `t.ui.model_pose`, `t.ui.await_model_pose`

`t.ui.widget(sym, sub)` -> `(ok, component_id)`; `t.ui.invoke(widget, op)` -> `ok`/error;
`t.ui.tab(name)` -> `ok` `no_row` (a number passes straight through); `t.ui.is_modal()` ->
`(ok, bool)`; `t.ui.model_pose(sym, sub)` ->
`(ok, detail, pose{xan,yan,zan,zoom,x_speed,y_speed,model,component})` `no_row` `not_visible`
`refused` (not a type-6 model component) -- what the cache baked and the server's
`if_setangle`/`if_setrotatespeed` last applied;
`t.ui.await_model_pose(sym, {field=value}, ticks, sub)` -> `(ok|timeout, detail, pose)`.

### `t.ui.journal_open(display_name)` -- also `t.ui.journal_close`

`t.ui.journal_open(display_name)` -> `(ok, {title, first_line, lines, line_count, complete})`
`not_visible` `refused` `timeout`; no argument reads whatever is open. `t.ui.journal_read()` is the
same with no click; `t.ui.journal_close()` -> `ok` `not_visible`.

### `t.npc.by_name(name)` / `t.npc.by_symbol(sym)` / `t.npc.nearest(sym, radius)`

`t.npc.by_name(name)` / `t.npc.by_symbol(sym)` / `t.npc.nearest(sym, radius)` -> `(ok, row)`
`not_found` `no_row`.

### `t.npc.tiles(sym, radius)`

`t.npc.tiles(sym, radius)` -> `(ok, summary, rows)` `not_found` `no_row`. EVERY copy of the symbol
in the pool, nearest first -- THREE returns, because a row detail has to be a string and a list of
tables renders as `1=<table>`. Each row is the pool's own: `slot`, `x`, `z`, `level`, `element_id`,
`name`. `nearest` answers one copy and cannot express "which of the three": `plaguesheep_1` has
three spawn rows two tiles apart, and a file that has to POSITION ITSELF relative to one (Sheep
Herder's prod pushes the sheep one tile along the dominant axis of sheep-minus-player, so the tile
to stand on is computed from the sheep's tile and the pen's) needs all of them. `t.expect`/`t.exec`
take the first two and ignore the third.

### `t.npc.await_present(sym, radius, ticks=10)` / `t.npc.await_gone(...)`

`t.npc.await_present(sym, radius, ticks=10)` / `t.npc.await_gone(...)` -> `ok` `timeout`. On `ok`
(seam27): `<sym> present within R: slot S at x,z` (read back through `npc.nearest`) /
`no <sym> within R` -- keep that second return: `local r = t.npc.await_gone(...)` then
`t.expect(name, r)` still writes an empty row.

`t.npc.await_dead` / `t.npc.await_dead_engaged` are in `verbs-combat.md`.
