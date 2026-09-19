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
