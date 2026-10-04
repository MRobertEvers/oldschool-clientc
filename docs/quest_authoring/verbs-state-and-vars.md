# Verbs: `var` / `inv` / `msg` / `skill` (section 3)

Section 3's state-reading table. The varp/varbit carrier traps are in `gaps-combat.md` and
`gaps-world.md`.

## `var` / `inv` / `msg` / `skill` (`state.lua`)

### Var names carry their kind and id

Since PR #99 (OSRS-Content PR #25, 2026-10-01) every varp, varbit and varc symbol is spelled
`varp<id>_<name>`, `varb<id>_<name>` or `varc<id>_<name>`: `varp29_cookquest`, `varp101_qp`,
`varb3185_anma_main`. `OSRS-Content/docs/VAR_NAMES.md` is the before/after table (grep it for the
old name). Use the prefixed spelling everywhere a var is named: `t.quest.bind{varp = ...}`, every
`t.var.*` call, and every `::setvar` cheat. The scaffold already writes it (the bind, and a
`::setvar varp101_qp <N>` for a guide's `QuestPointRequirement(N)`).

A bare name is wrong, and often not at the row that wrote it. `t.var.server("cookquest")` answers
`not_found/cookquest`. `t.quest.bind{varp = "cookquest"}` still answers `ok`, and the failure
comes at the first stage read: `t.quest.stage()` answers `not_found/cookquest (no varp and no
varbit of that name)`. In `::setvar` it misses the cheat's exact match and falls to its
substring match. That refuses an ambiguous name: the setup row reads
`setup.::setvar qp 43 FAIL -- setup cheat answered refused (nil); last lines: 'Which qp?
varb456_tog_qp_before_return, varb1782_qp_max, ...'`. Ten quests failed setup this way right after
the merge. A bare name that happens to be unique is worse: the cheat silently writes the one var whose
name contains it, which need not be the var you meant. `lint_quest.py` refuses a bare `varp =`/`varbit =` key, a bare `::setvar <name>` and a
bare `t.var.*` symbol, and each finding names the right spelling: `"::setvar qp": the bare var name
qp -- ... write varp101_qp`. It also refuses a prefix whose id is wrong (`varp30_cookquest`). Proof:
seam pass 37, `seam37_varnames_qp_bare` (FAIL) beside `seam37_varnames_qp_prefixed` (setup ok,
`var.server(varp101_qp) -> ok/43`) and `seam37_varnames_bare_reads` (the reads above).

### `t.var.varp(name)` / `t.var.varbit(name)`

`t.var.varp(name)` / `t.var.varbit(name)` -> `(ok, value)`; `t.var.server(name)` is the same read
server-side, varp-or-varbit-transparent, and answers `(result, value, source)`: `source` is `server`
(the client's copy of the server value) or `server content copy; no client copy` for a varp this
tree allocates above the cache's ids (`pack/varp.alloc`: `varp7152_twocats_lamp_pick`, `varp<id>_dwarfrock_puzzle_*`),
which the server never sends to the client -- only the server's own copy can answer for those
(seam12).

### `t.var.await(name, value, ticks=10)` / `t.var.await_server(...)`

`t.var.await(name, value, ticks=10)` / `t.var.await_server(...)` -> `ok` `timeout` (client-side,
server-side). Detail on `ok`: `<name> = <value> (client|server <varp|varbit>) after K tick(s)`; a
timeout names the last value read. `await_server` works for `pack/varp.alloc` varps too; its detail
then reads `(varp, server content copy; no client copy)`.

#### A quest varbit reads 0 through `var.server` / `var.await_server` (Monkey Madness `mm_daero`, sonnet-b44) -- FIXED seam35

*Origin: author batch sonnet-b44 (mm leg 1); fixed in seam pass 35.*

Monkey Madness's per-npc progress varbits (`mm_caranock`, `mm_daero`, `mm_narnode`, all on base varp
`mm_gnomes`, 372) read 0 through `t.var.server` and `t.var.await_server` for the whole run, while
the dialogue showed the stage had moved. A varbit's client record holds only what the server SENT,
and the server sends a varp only when a content `.varp` declares it `transmit=yes`; nothing
declares 372. Since seam35 both verbs read the embedded server's own copy for a varbit whose base
varp is never transmitted (or is past the client's varp array), and the source/detail says so:
`mm_daero = 5 (varbit, server content copy; base varp 372 is never transmitted) after 0 tick(s)`
(also `hazeelcult_alomone_vis` on 3748). A varbit on a transmitted base keeps the client-record
channel (`(server varbit)`). `var.server`'s third return (the source) applies to varbits too.

`t.var.expect` still requires a client copy; for such a varbit its refusal ends `-- no client copy
(server content copy; ...); var.server reads N`: use `var.server` / `var.await_server`. Not covered
yet: a `quest.bind{varp=<varbit on an untransmitted base>}` stage still grades the client pair
(quest.lua `_reading`). Row: `seam.varbit_server_reads_untransmitted_base`.

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

#### `attempt to index a string value` on a snapshot, or a reading's `.xp` or `.current` is nil (sonnet-b43, b44)

*Origin: author batch sonnet-b43 (horror).*

Both reads answer the result string FIRST: `local snap = t.skill.snapshot()` holds `"ok"`, and
indexing it fails. Take the second value: `local _, snap = t.skill.snapshot()` (or
`select(2, t.skill.snapshot())`), then `snap.magic.experience`. A reading (`t.skill.read`'s second
value, and each entry of a snapshot) carries `.level` (current, after boosts and drains),
`.base_level` and `.experience`, in whole xp (`script/plugins/quest_driver/state.lua`
`QD.skill.expect_gain`). There is no `.xp`, so a reward of 4,662.5 reads as a delta of 4662. There
is no `.current` either (Monkey Madness's eat-when-hurt check, sonnet-b44): the boosted or drained
level is `.level`.

### `t.skill.expect_gain(name, xp, snapshot)`

`t.skill.expect_gain(name, xp, snapshot)` -> `ok` `refused` `no_row`. Accepts `xp` or `xp*10`, names
the unit matched.

### lint: "a read's status is "ok" whatever it read, so this row cannot fail" (matthew-mbp-m4-b55)

`t.check(name, condition, detail)` passes when the condition is `true` or `"ok"`. A verb that only
READS (`t.world.tile`, `t.world.level`, `t.inv.count`, `t.inv.has`, `t.inv.slot`, `t.skill.read`,
`t.skill.snapshot`, `t.var.varp`, `t.var.varbit`, `t.var.server`) returns `"ok"` for anything it read,
so used whole as the condition it makes a row that can never fail:
`t.check("keris.in_inv", t.inv.count("contact_keris"))` passed with no keris in the pack, and
`t.check("enterCave.below", t.world.tile)` (no call) printed `table: 0x...`. `lint_quest.py` now
refuses both shapes. Write instead:

- an item: `t.check(name, t.inv.expect_has("contact_keris", 1))` or `t.inv.expect_absent(...)`;
- a tile: `local r, h = t.world.tile()` then
  `t.check(name, r == "ok" and h.x == X and h.z == Z and h.level == L, h.x .. "," .. h.z .. "," .. h.level)`;
- a stat: `local r, s = t.skill.read("hitpoints")` then compare `s.current`;
- a var: `t.check(name, t.var.expect("varb...", value))`.

A status from a verb that can refuse (`t.var.await`, `t.inv.expect_has`, `t.msg.expect`, ...) is the
designed use and is not flagged. `t.check(name, true, detail)` is still accepted as a note row; a row
that claims to assert something must not use it (reviewers and samplers send it back).

### lint: "quest_cheat.rs2 has no arm for <row>" on a setup `::complete` (matthew-mbp-m4-b56)

`::complete <row>` only works for a row `quest_cheat.rs2` has an arm for. Any other name is answered
"::complete has no arm for that quest." and setup carries on with the prerequisite unset, so the run
proves less than its setup says. Four committed greens had one: Forgettable Tale staged
`quest_fishingcompo` (the arm is `quest_fishingcontest`) and was green only because an engine bug wrote
that varp by accident (the raid loop's fix to region music exposed it); Ghosts Ahoy and Shades of
Mort'ton staged `quest_priestperil` (`quest_priestinperil`); Mourning's End Part II staged
`quest_mourningsendparti` (`quest_mourningsendpart1`). `lint_quest.py` now reads the arms from
`quest_cheat.rs2` and refuses a setup row that names none, suggesting the near names. The arm's name
is the dbrow's, which is not always the quest directory's or the varp's: grep `quest_cheat.rs2` for
`$row = ` before staging a prerequisite.

### lint: "outside setup: a mid-run ::give" / "::bankgive is a SETUP cheat" (matthew-mbp-m4-b56)

Item cheats belong in `setup = {...}` (the owner's rule: no `::give` after setup). `lint_quest.py`
refuses a `"::bankgive ..."` literal anywhere outside the setup table, marked or not: the driver
refuses it only after `t.quest.bind`, so one in run() before the bind would reach the server. It
refuses a `"::give ..."` outside setup (run(), a leg, a local helper, `"::give " .. item`) unless the
line carries, or the comment line directly above it is, `-- lint: kit-give <reason>` -- an exception
the orchestrator accepted -- or it is one of the 113 gives in 23 files committed before the rule
(`tools/quest_gate/mid_run_gives_baseline.tsv`, printed as BASELINED, each the orchestrator's to
decide; the list only shrinks and a stale row is a finding). A marker with no reason or covering no
give is refused. `lint_quest.py --mid-run-gives <files>` lists every one with its state.
`helper_coverage.py` grades a `::bankgive` exactly like a `::give` of the same item: a setup
`::bankgive` of an item the guide has you obtain is CHEAT (`GIVE_CHEATS`).
