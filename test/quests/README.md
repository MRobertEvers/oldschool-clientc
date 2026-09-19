# Quest tests

One file per quest: `test/quests/<quest>.lua`, returning a table.

```lua
return {
    id = "cooks_assistant",
    -- A server save fixture in test/quests/fixtures/. [varps] holds only
    -- scope=perm vars, which is exactly the quest set -- a fixture carrying a
    -- temp var would be pinning a value the server owns.
    fixture = "cooks_assistant_start.ini",
    -- Cheats run once, after login, before the first step. A fixture states
    -- the world; setup states the ACCOUNT.
    setup = { "::setlevel cooking 10", "::give bucket_of_milk 1" },
    run = function(t)
        t.player.talk_to("cook")
        t.chat.expect_head("cook")
        t.chat.drain({ stop_at = "options" })
        t.chat.choose("What's wrong?")
        t.t.shot("cook-quest-offer")
        t.var.expect("cooks_assistant_progress", 1)
        t.t.finish(0)
    end,
}
```

The verbs a test is written in are listed once, with a line each, in
`docs/QUEST_AUTHORING.md` (phase 3); `tools/quest_gate/verb_list.py` prints
the live set with the file and line each one is defined at, and is the only
list that cannot go stale. Two spellings surprise everyone exactly once:

- **`t.t["do"](name, verb, ...)`**, never `t.do`. `do` is a Lua keyword, so
  `t.do` is a syntax error at every call site, not just at the definition.
- **`t.t.<verb>`** for the scheduler's own verbs (`cheat`, `ticks`, `settle`,
  `shot`, `step`, `expect`, `check`, `blocked`, `finish`): the `t` a `run(t)`
  receives IS the driver's root table, and those live in its `t` namespace.
  Everything else is `t.<namespace>.<verb>` (`t.chat.play`, `t.quest.bind`,
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
  them: `note` (its probe must appear in the row it folds into), `t.finish`
  (the ledger's `SUMMARY exit=0` and the process exit code) and `t.blocked`
  (the `BLOCKED` row it wrote, and SUMMARY's own `blocked=` bucket). The last
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
