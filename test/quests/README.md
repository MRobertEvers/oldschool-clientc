# Quest tests

One file per quest: `test/quests/<quest>.lua`, returning a table.

The verb kit, a full worked example (`quest.bind`/`t.exec`/`chat.play`/
`quest.expect_complete`), the result vocabulary and the twelve traps that cost
someone hours each all live in `docs/QUEST_AUTHORING.md` -- read that page
before writing a quest. This file states the rules the runner and the gate
enforce, not how to write to them.

A quest file is `{ id, fixture, setup = {cheats}, run = function(t) ... end }`.
There is deliberately no example on this page: the one that used to sit here
named a fixture that does not exist and a `run` body that asserted nothing,
and it was the only page an author read. The worked example lives in
`docs/QUEST_AUTHORING.md` section 1, and the two complete green files are
`test/quests/cooks_assistant.lua` (a quest, with a varp and a reward scroll)
and `test/quests/hans.lua` (an npc behaviour test, with neither).

The verbs a test is written in are listed once, with a line each, in
`docs/QUEST_AUTHORING.md` (phase 3); `tools/quest_gate/verb_list.py` prints
the live set with the file and line each one is defined at, and is the only
list that cannot go stale. One shape to know:

- The `t` a `run(t)` receives IS the driver's root table. The scheduler's own
  controls sit directly on it -- `t.cheat`, `t.ticks`, `t.settle`, `t.shot`,
  `t.step`, `t.expect`, `t.exec`, `t.check`, `t.note`, `t.blocked`,
  `t.finish`, `t.key`, `t.text` -- and everything else is
  `t.<namespace>.<verb>` (`t.chat.play`, `t.quest.bind`, `t.skill.read`,
  `t.ui.journal_open`, ...).

Rules the runner and the gate depend on:

- **No numeric interface or component ids, and no client op strings.** A target
  is a content symbol; an option is an op NUMBER; a row is chosen by its own
  text or its index. Never a lane name, anywhere.
- **Every step's verdict is a ledger row.** A step that asserts nothing
  produces no row, and a quest of those passes while proving nothing.
- **A screenshot per interaction**, named for what it shows, not numbered by
  hand: the runner numbers them. Two identical shots fail the gate.
- Deadlines are in **server ticks** and every verb already has a default; pass
  one only when the quest genuinely waits longer (a cutscene, a long walk).

Fixtures live in `test/quests/fixtures/*.ini` and are server saves.

## `QUEUE.tsv` -- which quest is whose

`test/quests/QUEUE.tsv` is the work queue: 188 rows, one per test, tab
separated, with a header line. Columns, in order:

`quest_dir` `test_id` `helper_dir` `helper_file` `tier` `status` `owner`
`last_failure`

- `quest_dir` is the OSRS-Content content directory (`quest_cook`);
  `test_id` is the file stem this row produces -- `test/quests/<test_id>.lua`
  -- and the key every tool takes. It is `quest_dir` with its
  `quest_`/`miniquest_` prefix stripped, except `quest_cook` ->
  `cooks_assistant`, the one hand-written file that predates the rule.
- `helper_dir` is the Quest Helper guide directory `new_quest.py` reads
  (`?` when no guide matches). `helper_file` narrows that to ONE `.java`
  file and is blank everywhere except the ten Recipe for Disaster rows,
  which all share `recipefordisaster/` -- `quest_recipefordisaster`'s single
  row is replaced in place by `rfd_intro` plus its nine kitchen subquests.
- `tier` is the difficulty tier from `quest_inventory.tsv` (5 = unknown).
- `status` is one of `todo green blocked content_bug`, plus the
  `claimed/<owner>` a `queue.py next --claim` writes, which is a hold and
  not a verdict. `owner`/`last_failure` are free text.

Read and write it with `tools/quest_gate/queue.py` (`next --tier N [--claim
OWNER]`, `set <test_id> --status ...`, `show`, `summary`) -- every write is
atomic and preserves the column order. `new_quest.py <test_id>` looks the row
up here for the guide to scaffold from. `--write-queue` regenerates the whole
file from `quest_inventory.tsv`, which is how the shape stays checkable.

Artefacts land in `build/quest_gate/<quest>/` (ledger, `shots/NN-name.png`,
`client.log`), which is deleted on the next run. A run whose ledger SUMMARY
says PASS is also copied to
`OSRS-Content/osrs239-content/server/scripts/selftest/quest_tests/<quest>/`
(ledger + shots), so the last green run's evidence is versioned with the
content it photographs. A FAIL never overwrites that set. `run.py
--no-publish` skips the copy.

## `_conformance.lua` -- the verb conformance harness

`test/quests/_conformance.lua` is not a quest. It is the gate that replaced
"it compiles": it calls **every** verb the driver exposes on `t`, exactly once,
against a live world, and leaves one ledger row per verb saying what that verb
actually did.

```sh
make -C src test-quest-conformance   # builds, runs, prints the table, red on any failure
make -C src check-quest-verbs        # just the drift check, no client
```

- The underscore prefix keeps it out of the quest set: `tools/quest_gate/run.py
  --all` runs quests, this runs verbs.
- `tools/quest_gate/verb_list.py` enumerates the verbs from
  `script/plugins/quest_driver/*.lua` and `--check` refuses to agree unless the
  harness plans exactly one row for each. **A verb added to the driver with no
  row here fails a make gate**, rather than being quietly never called.
- The sandbox has no `pcall`, so a verb that *raises* instead of answering ends
  the run where it stands. `tools/quest_gate/conformance.py` attributes the
  error to the first verb with no row, records it as `ERROR` with the message,
  and re-runs with that verb skipped -- so an unrunnable verb costs its own row
  and nobody else's, and all 97 verbs are always scored.
- Three rows are re-graded outside the coroutine, because Lua cannot check
  them: `note` (its probe must appear in the row it folds into), `finish`
  (the ledger's `SUMMARY exit=0` and the process exit code) and `blocked`
  (the `BLOCKED` row `t.blocked` wrote, and SUMMARY's own `blocked=` bucket --
  the verb's own row shares that name, so the checker tracks the BLOCKED one
  separately). The last
  two END the run, so both are called after the harness's loop -- `t.blocked`
  is the terminator, and it calls `t.finish(0)` itself.
- Cheats that state the world are **setup**, not rows. They reach BOTH cheat
  paths: `DriveCore_Cheat` calls `ToriRSServer_RunCheatForTest`, which tries
  the content debugprocs first and then torirs_server_world.c's own ladder,
  exactly as a logged-in player's `::` line does -- so `::give`, `::spawn`,
  `::setlevel`, `::setvar` and `::kill` work here, and only a cheat that
  matches nothing at all answers `no_row`. A setup cheat that answers `no_row`
  fails the run with a `setup.<cheat text>` row rather than leaving the test
  to assert against a world nobody stated.
- The rewritten manifest stays in `manifests/.conformance.ini`: the manifest's
  own directory is load-bearing -- the same file run from the session dir boots
  a client where `::objbox` and the cook's quest-start mount nothing (measured
  A/B 2026-09-19: 50/78 from `manifests/`, 46/78 from the session dir).
- Artefacts land under `build/quest_gate/_conformance/attempt-NN/`: the exact
  generated script, `ledger.tsv`, `log.txt` and `shots/`.
