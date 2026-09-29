# Verbs: the root of `t`, `quest`, `scroll`/`levelup`, and the result words (sections 3-4)

Section 3's verb table, first part, with section 4's fixed result vocabulary.

## Section 3. The verb table (how it is laid out)

One line per verb, `t.<call>` as a quest file spells it, grouped by namespace, `(result, detail)`
unless noted; full banners in the `.lua` file named in brackets.

The verb table is split across the `verbs-*.md` files in this directory; the one-line table is in
the core `docs/QUEST_AUTHORING.md`.

## the root of `t` -- the test's own controls, on the root table itself, with no second `t` inside it (`core.lua`, `ui.lua`)

### `t.cheat(text, wait_for_reply=true)`

`t.cheat(text, wait_for_reply=true)` -> `ok` `refused` `no_row`. Reaches content debugprocs then the
server ladder (`::give ::setlevel ::setvar ::kill ::spawn ::tele ::goto ::passive`). Awaits any new
chat line (<=5 ticks) unless `wait_for_reply=false`. A content debugproc that fast-forwards a grind
(`::mortton_repairtemple`, `flamtaer_temple.rs2`:498) checks stage and stats only, never where the
player stands -- it needs no `goto_tile` first, unlike the click-based grind it replaces.

`::setvar` takes an EXACT name in either pack first (exact varbit, then exact varp) and only then
guesses by substring (varp, then varbit), so `::setvar agrith_quest ^sots_fight` and
`::setvar cowquest ^iom_fight` write the progress VARBITS (seam21; before, each substring-matched
its carrier varp and was refused).

### `t.ticks(n)` -- also `t.settle`

`t.ticks(n)` -> `ok` `timeout`. Advances the clock exactly `n` SERVER ticks. `t.settle()` -> `ok`
`timeout`, waits for `api.drive.settled()`.

### `t.finish(code)`

`t.finish(code)`. Writes SUMMARY and ENDS THE RUN: no later row, no later shot, the script parks. A
row attempted after it is refused with one stderr line,
`quest-driver: row after finish ignored: <name>`. `return` right after it anyway -- what follows is
unreachable.

### `t.step(name, verdict, detail)` -- also `t.expect`

`t.step(name, verdict, detail)` -> `(bool, detail)`. Manual ledger row; you already know the
verdict. `t.expect(name, result, detail)` -> the pair unchanged, PASS iff `result == "ok"`.

### `t.check(name, condition_or_result, detail)`

`t.check(name, condition_or_result, detail)` -> the pair unchanged. PASS iff `true` or `"ok"`; takes
its own shot(s).

### `t.exec(name, verb, ...)`

`t.exec(name, verb, ...)` -> the wrapped verb's own `(result, detail)`. FAIL `hollow` if `ok` with a
nil detail; FAIL `bad verb/target` if `verb` is not a function or the first arg is nil. Auto-shoots.

### `t.blocked(reason)`

`t.blocked(reason)`. Writes `BLOCKED`, shoots, then finishes -- same terminal rule, so a tier-4 stub
ENDS at its `t.blocked` and the ledger's last row is that one. `return` right after it.

### `t.key(name)` -- also `t.text`

`t.key(name)` -> `ok`/press-release error; `t.text(str)` -> `ok`/error.

### `t.shot(name)`

`t.shot(name)` -> `ok` (path) `refused` `timeout`. `t.exec`/`t.check` call this for you; call it
yourself only for a bare narrative shot. A capture byte-identical to the last picture the run WROTE
is answered `ok` with the detail `unchanged since <that shot>` and no file on disk: the row then
carries NO shot and `[frame unchanged]` in its detail (trap 4). A `<name>-FAIL` capture is never
suppressed.

#### The shot camera is aimed for you when the press pose is occluded (seam `driver-shot-camera-occluded-zero-cost`)

**The picture's camera is aimed for you when the press pose is occluded, at zero cost** (2026-09-22,
seam `driver-shot-camera-occluded-zero-cost`). A shot used to photograph whatever pose the last
press left, and indoors that is a wall face or, in a cave-rock basement, the inside of the rock --
Mourning's End I shots 47-90 were black. Now `QD.shot` (`ui.lua`) asks `QD.drive._shot_plan`
(`pointer.lua`) whether a WALL (loc shapes 0-3, 9) or a CENTREPIECE (10-11: tree, rock, cage) stands
in the corridor between the player and the eye under the line of sight; only then does it re-aim
(pitch 383, the nearest clear yaw, a shorter zoom only if no yaw is clear), and it puts the press
pose back on the first poll whose capture the renderer has taken -- before the next frame's follow
step, and before the shot answers.

It spends no tick and no frame: the shot answers on the same poll an unaimed one does, and every
unoccluded shot is the press pose's own picture byte for byte. Seam 7's version waited two frames
each way and turned three green quests red, because one run frame is one 20 ms logic cycle and extra
frames shift every later press against the server tick -- never add a wait to a shot. An aimed shot
leaves a `QUEST shot-aim <shot>: occluded ... -> clear ... [put back at poll N, answered at poll M]`
line in `client.log`, so a top-down picture in `shots/` says why it is not the press's own view.

Proved by `_conformance.lua`'s `seam.shot_camera_zero_cost` (Lumbridge castle cellar: aimed, put
back at poll 2, answered at poll 3, the same poll as the clear control). The frame right after a
teleport or a plane change can still be dark for a reason no camera fixes (the scene is not built
yet): `t.ticks(2)` after the goto, as trap 21 says.

### `t.note(text)`

`t.note(text)`. Free text folded into the NEXT row's detail -- why a verb answered as it did,
without a row of its own.

### `t.await({level=fn, event="server_tick"|"sub_mounted"|..., note=text}, ticks)`

`t.await({level=fn, event="server_tick"|"sub_mounted"|..., note=text}, ticks)` -> `ok` `timeout`; on
`ok` the detail is `<note>: met after N tick(s)` (seam27 -- it was NIL, and a `t.check` over it
wrote an empty PASS detail, sonnet-b31 (c)), so give every await a `note` that says what it waited
FOR. The await primitive every other verb is built on, for a settle no verb covers. `level` is
polled; the deadline is SERVER ticks.

## `quest` (`quest.lua`)

### `t.quest.bind{varp=, constants={...}, row=, display=, journal_title=, points=}`

`t.quest.bind{varp=, constants={...}, row=, display=, journal_title=, points=}` -> `ok` (detail:
`bound <varp> (<kind>) {constants} display= points= qp_before=`) `refused`. `constants.complete` is
required. No world read. `varp=` names a symbol from EITHER pack -- a quest tracked by a pure varbit
with no varp symbol (Prying Times' `%quest_pry`, `configs/all.varbit`, absent from `all.varp`) binds
and reads the same as a varp-tracked one.

### `t.quest.stage()`

`t.quest.stage()` -> `(result, value)`, resolved varp-or-varbit-transparently against `bound.varp`
(`t.var.server`'s own kind-detection); a manual stage check still names which kind matched in its
detail (trap 14: name the row `quest.stage.<constant>`).

### `t.quest.expect_stage(name_or_value)`

`t.quest.expect_stage(name_or_value)` -> `ok` `refused` (names the side that disagreed).

### `t.quest.expect_complete()`

`t.quest.expect_complete()` -> `ok` `refused`. Writes FOUR rows itself: `quest.varp_complete`,
`quest.scroll_title`, `quest.points`, `quest.journal` (closes the reward scroll first). It
photographs the completion scroll into the `quest.scroll_title` row (`quest.scroll` shot, never
deduped) and the gate requires that shot in every green run. Never calls `::complete`.
`quest.varp_complete` reads varp-or-varbit-transparently too, and a varp the CLIENT cannot address
at all (an id above the cache's top, never transmitted, both client reads `not_found`) falls through
to the embedded server's own copy with `[server content]` in the row -- so neither shape is a reason
on its own to `t.blocked` before `expect_complete()`. The channel is always named: `[client+server]`
means the two agreed.

## `scroll` / `levelup` (`read.lua`)

### `t.scroll.title()` -- also `t.scroll.rewards`

`t.scroll.title()` -> `(ok, {name, points})`; `t.scroll.rewards()` -> `(ok, {lines, icon})`; both
`not_visible` when no scroll is up.

### `t.scroll.reward_xp(skill)`

`t.scroll.reward_xp(skill)` -> `(ok, xp)` `no_row`. Parses `<n> <Skill> XP`, case-insensitive,
unscaled.

It does NOT parse thousands separators. The pattern is `(%d+)%s+(%a+)%s+XP`, so "10,500 Attack XP"
reads 500 (`script/plugins/quest_driver/read.lua:420`). For a reward of 1,000 xp or more, assert
with `t.skill.expect_gain` against a `skill.snapshot()`, or read `t.scroll.rewards()` lines
yourself (`ikov.lua` does).

### `t.scroll.close()`

`t.scroll.close()` -> `ok` `no_row`.

### `t.levelup.skill()` / `t.levelup.continue_()`

`t.levelup.skill()` / `t.levelup.continue_()` -> `unsupported` (`covered`) always in this content
pack -- no opener (plan U16).

## Section 4. Result vocabulary

`ok timeout not_found refused covered no_row not_visible closed unsupported` -- fixed, never add
one. `mismatch` and `hollow` are two verbs' own local words (`chat.play`, the hollow rule), not part
of the fixed set. Ledger verdicts are `PASS`, `FAIL`, `BLOCKED`.
