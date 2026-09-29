# Verbs: `chat` (section 3)

Section 3's `chat` table. See also traps 3, 11, 18, 22, 25, 30 and `gaps-dialogue.md`.

## `chat` (`chat.lua`, `read.lua`)

### `t.chat.kind()`

`t.chat.kind()` -> a bare string, no await: `npc` `player` `mesbox` `objbox` `options` `count`
`name` `other_input` `none`.

### `t.chat.continue_()`

`t.chat.continue_()` -> `ok` `unsupported` `refused` `not_visible` `closed` `timeout`. On `ok` the
detail is `"<from kind> -> <to kind>"`; see trap 12 for the one `t.exec` form it still refuses.

### `t.chat.drain{stop_at=, max_pages=, shots=true}`

`t.chat.drain{stop_at=, max_pages=, shots=true}` -> `(ok, kind)` `timeout`. Clicks through until
`stop_at` or a terminal kind.

### `t.chat.close()`

`t.chat.close()` -> `ok` (idempotent).

### `t.chat.count(n)` / `t.chat.name_entry(text)`

`t.chat.count(n)` / `t.chat.name_entry(text)` -> `ok` `unsupported` (not that kind of prompt); the
detail names what was entered.

### `t.chat.options()` -- also `t.chat.options_title`

`t.chat.options()` -> `(ok, rows)`; `t.chat.options_title()` -> `(ok, title)`.

### `t.chat.choose(selector)`

`t.chat.choose(selector)` -> `ok` `no_row` `refused`. `selector` is a 1-based index, exact row text,
or `/lua pattern/`.

### `t.chat.play(list)` -- also `t.game.runedraw`

`t.chat.play(list)` -> `ok` `mismatch` `unsupported`, or a wrapped verb's own result.

#### `t.game.runedraw` -- Robin's rune-draw (Ghosts Ahoy, seam24)

ROBIN'S RUNE-DRAW (Ghosts Ahoy) has its own verb since seam24: `t.game.runedraw(policy=nil)` ->
`(ok, {outcome='won'|'lost'|'level', mine, robin, draws, holds, debt, signed, pages, text})`
`mismatch` `refused` `timeout` plays ONE game from the page `talk_to('ahoy_robin', 1)` leaves up to
the settlement, every Draw/Hold the exact best reply to Robin's fixed rule
(`ahoy_manual.rs2:104-215`) read only from the game's own pages (+0.094/game, mean 15.4 games to the
signed bow; any fixed hold threshold is worse -- hold-at-12 is -0.25/game and drifts DOWN).

Loop `talk_to` + `runedraw` until `t.var.server('ahoy_subquest_bow') >= 2`, cap at 100 games,
declare `max_frames = 120000`; its first decision spends ~20-34 ticks on the search, once per run.

#### Any other unbounded random loop

Any OTHER unbounded random loop is not one list: call `t.chat.play({<one entry>})` once per decision
inside a Lua loop, read the page it lands on (`t.chat.kind()`/`t.chat.text()`), and choose the next
entry from that, so each click keeps chat.play's own readable-page await and answer grading -- a
hand loop of raw `t.chat.continue_`/`t.chat.choose` loses both.

#### The entry forms

Entries: `npc:<substr>` `player:<substr>` `mesbox:<substr>` (or `:*` for that kind, text unchecked),
`options`, `choose:<row|/pattern/>`, `count:<n>`, `name:<text>`, `end`, `*` (any one continuable
page). There is NO `objbox:` entry -- an objbox page is `*`, and a wrong entry rejects the whole
list before any page is played, which reads like the dialogue never opened. On `ok` the detail is a
per-page summary, so `t.exec` takes it straight.

A `*` entry's summary names the page it continued past (`*=npc 'Let's get this fixed the'`).
chat.play awaits the ANSWER to each entry's click; a click still showing 'Please wait...' after 16
ticks is `timeout ... the server never answered ... orphaned page` -- the script that opened it was
dropped (rerun with `TORIRSSERVER_VERBOSE=1` and look for the server's `dropping [...]` line); an
answer that took 8+ ticks adds an `unanswered for N ticks` note to the next failure (seam23).

### `t.chat.text()` -- also `t.chat.item`

`t.chat.text()` -> `(ok, text)`, awaits up to 10 ticks. `t.chat.head()` / `t.chat.name()` /
`t.chat.item()` -> `(ok, <model|text|items>)` `unsupported`.

### `t.chat.expect_text(substring)` / `t.chat.expect_head(npc)` / `t.chat.expect_item(obj)`

`t.chat.expect_text(substring)` / `t.chat.expect_head(npc)` / `t.chat.expect_item(obj)` -> `ok`
`not_found`.

### A page count you do not know: `drain{stop_at="options"}` then `choose`

*Origin: sample sonnet-b34 (2026-09-29), The Dig Site (`itexam`).*

`t.chat.play` wants every page before a `choose:` listed in order. When you cannot know the count
(an exam that echoes a varying number of lines, a quest-state greeting), drive the menu in two
rows instead. `t.exec("q1-ask", t.chat.drain, { stop_at = "options" })` clicks through to the menu,
and `t.exec("q1-answer", t.chat.choose, "<exact row>")` answers it. Repeat the pair for each menu,
then run `t.chat.drain{}` with no `stop_at` for the tail. Every one of those rows gets a detail
(`options`, `chat.choose: page changed`, `none`). `itexam.lua`'s three exams and Doug's seven-menu
beg follow this shape.

For an "answer any dialogue" row, one that only has to get the conversation out of the way, use
`t.chat.drain{}` by itself. Author workflows forbid local helper functions in a quest file
(`author_batch.workflow.js` rule 4), and helper_coverage grades each step by its own static row
(seam pass 26 (e)). So a conversation that a seam driver wrote as a `conv()` helper is unrolled
into these drain/choose pairs, one pair per menu, each named after its guide step.
