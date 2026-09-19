# quest-driver: the remaining work

Companion to `QUEST_DRIVER_DESIGN.md` (the fixed decisions) and
`QUEST_DRIVER_PLAN.md` (the per-verb mechanism). This file is only about what
is left, in the order it should be done, and the rules that come out of how
the first two attempts failed.

## Where this stands

Landed and verified by driving the client (2026-09-19, phases 1 and 2 of
`QUEST_SUITE_KIT.md`):

- **The runner and the gate.** `tools/quest_gate/run.py` runs one
  private-session client per quest (`--all` for the set, `--script` for a
  scratch file) and `gate.py` re-reads what it left behind. `make -C src
  test-quests` is the pair.
- **Two quest tests, green from a real run.** `test/quests/cooks_assistant.lua`
  (29 rows) plays Cook's Assistant through the cook's own dialogue and hands
  the ingredients in; `test/quests/hans.lua` (15 rows) follows Hans around the
  courtyard. Both are `green` under `gate.py`, and each green run's ledger and
  shots are published into `OSRS-Content/.../selftest/quest_tests/<quest>/`.
- **Every verb, executed.** `test/quests/_conformance.lua` calls all **97**
  verbs against a live world, one ledger row each, and
  `make -C src test-quest-conformance` is red unless every one of them PASSes.
  It is 97/97 today. `make -C src check-quest-verbs` refuses a verb with no
  row.
- **One cheat path.** `t.cheat` reaches the content debugprocs *and* the
  server's own ladder (`::give`, `::setlevel`, `::setvar`, `::kill`,
  `::spawn`, `::tele`), proved row by row by `test/quests/_cheats.lua` /
  `make -C src test-quest-cheats`. A setup cheat that answers `no_row` ends
  the run with a `setup.<cheat>` FAIL row rather than testing a world nobody
  stated.
- **The verb kit a generated test is written in.** `t["do"]`, `t.check`,
  `t.blocked` (the `BLOCKED` ledger verdict and its own SUMMARY bucket),
  `quest.bind/stage/expect_stage/expect_complete`, `chat.play`,
  `scroll.reward_xp`, the `await_*` family, `player.teleport`, and
  `ui.journal_open/read/close` driving the real quest-list click.

Not landed: the authoring kit (phase 3) -- `new_quest.py`, `lint_quest.py`,
`docs/QUEST_AUTHORING.md`, and the regeneration of both quests through that
scaffold -- and the `::skipboss` arms (phase 4). Until `QUEST_AUTHORING.md`
exists, the page an author reads is `test/quests/README.md`, whose headline
example predates the kit.

## The three rules this work earned

**1. The gate is behaviour, never artifacts.** Two review passes called this
tree green on build, gates and diff while every verb was inert, because
nothing ever ran a verb. Phase A exists to make that impossible again: from
then on the number that matters is "N of M verbs did their real work".

**2. Commit on every green gate.** Both previous attempts were background
workflows that died with the session, and the second one lost everything
because twenty agents of work sat uncommitted. Each phase below ends in a
commit on `lane-quest-driver`. A kill must never cost more than one phase.

**3. A verb is not fixed until a ledger row from a real run says so.** Not
"it compiles", not "the code looks right". The tick-unit bug passed every
static reading anyone gave it.

## Known traps (every one of these cost hours)

- `TORIRS_PLUGIN_MANIFEST` resolves **under `script/`**. Passing
  `script/plugins/quest_driver.ini` double-prefixes and silently loads no
  driver at all, which reads exactly like a dead plugin.
- A fixture only applies if it is **copied to `<saves>/<user>.ini`**.
  Otherwise the server makes a fresh character and you boot into the
  Character Creator modal, which blocks tab selection and reads as a broken
  verb.
- **Never reuse a saves directory** between runs: the server writes on exit,
  so run N loads run N-1's position.
- After any `OSRS-Content` edit, re-run `make -C src torirsserver-scripts` or
  the server refuses to boot on a stale pack.
- `t.ticks(n)` is **server ticks**. One is 30 client cycles. Waiting "10" of
  the wrong unit is 200 ms and nothing has happened yet.

---

## Phase A -- the two gates

Nothing else should start before these exist, because they are how every
later phase knows whether it worked.

### A1. Verb conformance harness

`test/quests/_conformance.lua` calls **every** verb once against a live
world and produces exactly one ledger row per verb, named after the verb,
with its result and a short detail. A failure must never abort the run.

It sets the world up first so every verb has a subject: the fixture stands
beside Hans, `::give` puts an item in the backpack, `::dropobj` puts one on
the floor, and a `talk_to` opens a real dialogue for the chat family.

It enumerates the verbs **from the Lua sources** (`QD.<ns>.<verb>` in
`script/plugins/quest_driver/*.lua`) and asserts the count, so adding a verb
without a conformance row fails the harness rather than being silently
untested.

`make test-quest-conformance` builds, ensures the script pack, runs it with a
private session, prints the ledger and exits **non-zero while any verb
fails**. A harness that returns 0 with failures is worse than none.

*Gate:* the harness runs and prints a per-verb table. Expect it to be mostly
red; that baseline is the deliverable.

### A2. Lua/C ABI and unit checker

`tools/quest_gate/check_drive_abi.py`, wired as `make check-drive-abi`:

- Parse the C registration tables (`{"name", lua_fn}`) in
  `src/plugin/torirs_plugin_drive*.c`, and for each `lua_fn` the arity it
  reads (`PluginDrive_Arg*`) and the number of values it returns.
- Parse every `api_drive.<name>(...)` call site in the driver's Lua and how
  many values each destructures.
- Report mismatches with file:line on both sides, every `api_drive.<name>`
  that is registered nowhere, and every registration nothing calls.
- Classify what static parsing genuinely cannot know as `unknown`, list it,
  and do not fail the gate on it.

Add a **unit lint** in the same script: flag any expression that mixes an
identifier containing `tick` with `world->cycle` without going through
`APP_SERVER_TICK_LOGIC_CYCLES`. That is narrow on purpose. It is the exact
bug class that made this project look dead for two sessions, and it is
cheaper to pin than to rediscover.

*Gate:* `check-drive-abi` green after fixing what it finds. Commit.

---

## Phase B -- make the harness green

One sub-phase per verb family, each ending with the conformance number up
and a commit. Order is by dependency, cheapest first.

### B0. Spike: does a click have a pickset? (do this before B3, timebox it)

`ContentTest_DrawRequested` returns 0 in quest mode: its forced-draw branches
are all gated on `active` (the mailbox) or `running` (interactive). Frames
are still drawn whenever `App_RunOnce` sets `need_redraw`, which is why
screenshots work, but that means **the pickset exists only on frames the app
happened to redraw**, and `click_minimenu` needs a fresh one at a known pixel.

Answer one question: is a redraw reliably happening on the frame before a
synthesized press? If not, the fix is small and local: give quest mode the
same forced draw the mailbox gets for `click_phase`, i.e. let the driver
raise a "a click is pending" flag that `ContentTest_DrawRequested` honours.

Everything in B3 depends on the answer, so it is worth an hour before
committing to the rest.

### B1. chat family

`kind`, `continue_`, `drain`, `close`, `options`, `options_title`, `choose`,
`count`, `name_entry`. Hans is the fixture: `[opnpc1,hans]` opens a chathead
page and his `~p_choice5` gives a real options menu, so the whole family is
reachable without needing B3 first (use `::talk hans` to open it).

Watch for: the resume seam is scoped by component id, and `pause_pending` is
cleared on *every* mount, so a naive presence check is wrong (plan D6).

### B2. read family

`text`, `head`, `name`, `item`, `expect_*`, `scroll.*`, `levelup.*`.
`head` and `item` need the widget model reader; confirm it is wired before
assuming it is. `levelup_display` has no content opener in this tree (plan
U16), so those two verbs record `unsupported` with that reason until the
content decision lands, and the harness expects that.

### B3. pointer and world-interact families

`screen_position`, `click_minimenu`, `op`, `walk_to`, `walk_near`, `idle`,
`camera`, then `talk_to`, `click_loc`, `click_obj`, `use_on`, `equip`,
`drop`, `inv_op`. The biggest piece, and the one B0 de-risks.

Rules that already cost a debugging session: match a menu row by
`(action, pick.kind, pick.id)` and never by row text; `click_obj` defaults to
op 3, not 1; `walk_near` re-issues only when the target's tile changed.

---

## Phase C -- the runner

`tools/quest_gate/run.py` replacing the `UNIMPLEMENTED` stub: one process per
quest, a private session directory, **the fixture copied in**, the manifest
rewritten for `transport=embed` and this checkout's cache, the env block from
`QUEST_DRIVER_DESIGN.md`, its own objdir, artefacts to
`build/quest_gate/<quest>/`, `--all` and `--jobs N`.

`tools/quest_gate/gate.py`: red on any FAIL row, any missing or under-1000-byte
shot, any duplicate shot MD5 inside one quest (two byte-identical PNGs showed
up in a hand run, so this check has real work to do), or a listed quest with
no ledger.

`make test-quests` (`QUEST=<name>` or all).

*Gate:* the conformance harness runs **through the runner** and still reports
the same number it reports by hand.

---

## Phase D -- the quests

**D1 `test/quests/hans.lua`.** Talk to Hans, expect his head and text,
continue, read the options, choose the flee row, then assert he leaves within
`hans_flee_ticks` and returns within `hans_respawn_ticks`. Both clocks are
read at runtime through the `hans_test_constants` debugproc, never pinned as
literals.

**D2 `test/quests/cooks_assistant.lua`.** From the fixture with setup cheats:
greet, choose "What's wrong?", drain, `cookquest == 1`; give the three
ingredients and assert them; hand in, drain with shots, `cookquest == 2` on
client **and** server, ingredients gone, cooking xp up by the reward read from
content, the completion message, final shot. The server selftest
`src/torirsserver/test/quest_cook_selftest.u.h` is the reference for the
sequence and the symbol names.

A quest that passes by asserting nothing is a failure.

---

## Phase E -- close out

Four mutation checks, each in a **throwaway worktree**, never in this one:

1. Remove the `lua_sethook` re-arm on resume: the busy-loop fixture must stop
   being killed.
2. Remove the `sub_mounted` stamp: `hans.lua`'s talk step must time out.
3. Shift the action lookup in `click_minimenu` by one: the talk step must fail
   with `no_row` or the wrong op.
4. Make one verb silently return `ok` without doing its work: **the
   conformance harness must still go red.** This is the one that proves the
   harness tests behaviour rather than shape, and it is the mutation this
   project most needed and never had.

Then: decide whether `TORIRS_DRIVE_DEBUG` stays (it should), update
`QUEST_DRIVER_PLAN.md` where implementation diverged from it, and take PR #93
out of draft.

---

## Out of scope, by decision

- **rs289lc / the dat1 lane.** Owner decision: osrs239 with the embedded
  server only, for now. The plan's §2 and §5.9 hold the bindings for when it
  comes back.
- **The configurable cycle speed** (`TORIRS_TIME_SCALE`/`CYCLE_MS`/`TICK_MS`).
  Its own phase, worth doing once the suite exists and its run time is the
  thing being optimised. Note the tick-unit fix in this branch is a
  *prerequisite*: it put the two clocks behind one named ratio, which is what
  that work needs to vary.
- **`levelup_display`** until the content decision in plan U16.
