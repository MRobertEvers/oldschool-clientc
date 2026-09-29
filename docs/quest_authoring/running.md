# Picking up, scaffolding, running (section 6) and harness facts

## Section 6. Picking one up, scaffolding it, running it

### Resuming a rejected attempt; stale walkthroughs

Resuming: if `test/quests/<id>.lua` already exists and `queue.py show <id>` reports status `todo`
with a `last_failure`, that file is the PREVIOUS author's rejected attempt -- read the failure and
continue from it, never regenerate over it (`new_quest.py` refuses without `--force` anyway). A
walkthrough under `docs/quests/` can predate the content-parity passes: `current_affairs.md`
(2026-08-17) still describes no Catherine spawn, hard-coded form answers and an any-tile duck, all
real content since parity1b-1d (OSRS-Content c8b3e2fede, aa38f602a3, 15c39665ce). Read the `.rs2`
and `git -C OSRS-Content log -- <quest dir>` before trusting one.

### The commands

```sh
# The queue (test/quests/QUEUE.tsv) hands out the work, one row per test:
python3 tools/quest_gate/queue.py next --tier 1 --claim <you>   # claims it
python3 tools/quest_gate/queue.py show <test_id>                # that row
python3 tools/quest_gate/queue.py summary                       # tier x status
# Scaffold from the Quest Helper guide (refuses to clobber; --force overrides):
python3 tools/quest_gate/new_quest.py <test_id>
# Run: first time or after a C change; --no-build for Lua-only iteration
# (always pass it -- trap 9); --all --no-build --no-publish for every
# quest without touching OSRS-Content's copy:
python3 tools/quest_gate/run.py <quest>
python3 tools/quest_gate/gate.py <quest>   # the verdict; read this, not run.py's exit code
# Report back (status is one of todo green blocked content_bug):
python3 tools/quest_gate/queue.py set <test_id> --status green --owner <you>
python3 tools/quest_gate/queue.py set <test_id> --status blocked --failure "<why>"
```

### `boss_fight=yes`, `-- CHECK gather`, env vars reach the run

`quest_inventory.tsv`'s `boss_fight=yes` is a guess that makes the scaffold end in a skipboss
`t.blocked` stub: a boss with a plain op2 Attack and a stat block is fought for real (Spirits of the
Elid's golems, `hero.lua`'s Ice Queen). The scaffold never `::give`s an item Quest Helper marks
`canBeObtainedDuringQuest()`; it leaves a `-- CHECK gather` comment at the first step that needs it
instead -- drive the gathering, or delete the marker and give it deliberately, but never leave the
marker in.

`run.py`'s `client_env()` starts from `dict(os.environ)` and overlays its own keys, so any
`TORIRS_*`/`TORIRSSERVER_*` variable set on the command line reaches the run:
`TORIRSSERVER_VERBOSE=1 python3 tools/quest_gate/run.py <quest> --no-build` is the way to see a
stalled script's triggers.

### The failure block, the artefact directory, and the per-id lock

A non-green run prints a `---- failures ----` block (last FAIL/BLOCKED row, its
`-FAIL.png`/`TIMEOUT.png`, the last `client.log` lines) -- start there. Artefacts live in
`build/quest_gate/<quest>/`, replaced every run. That directory is the quest id's ONE session
(gate.py, publish and the batch sheets all resolve it by name), so a second `run.py` of an id whose
run is still live now REFUSES, naming the live run's pid and start time and touching nothing -- it
no longer deletes a running session's ledger and shots (the 2026-09-20 "druid regression" was two
sessions writing one directory).

Wait for it, or drive a private copy under another name (`--script <file> --name <id>_2`). A lock
whose process is gone is cleared as stale, so a killed run never blocks the id.

## From section 8: harness facts

### `run.py --script` runs setup (seam19); `--no-build` skips only the client binary

*Origin: section 8 ("Gaps reported by authors").*

`run.py --script <file>` RUNS a non-empty `setup` list through the same loop a quest run uses
(seam19; the wrapped copy is `build/quest_gate/<name>/script/<basename>`); a file with no setup or
`setup = {}` runs unwrapped, as before. Before seam19 it ignored the list, and parity2a read that as
"`::setlevel` in setup is a no-op". A `::setlevel` setup line is DONE only when the client reads
that stat `stated` with `base_level` == n within 10 ticks, else a FAIL row `setup.::setlevel ...`
ends the run: name the stat (numeric ids are refused) and stay within 1..99.

Since seam20 the engine itself refuses an unknown stat, a level outside 1..99 or a missing level
(`t.cheat` -> `refused`, the reason in chat, like `::setvar`), so a `::setlevel` inside `run()` is
loud too (`_cheats.lua` row `cheats.setlevel_refused`). `test/quests/_setup.lua` is the setup-list
contract (`run.py --script test/quests/_setup.lua --name setup_contract --no-publish`, 7 rows).

#### `--no-build` still pays `sscompile`; other authors' files

Likewise `run.py --no-build` skips the CLIENT BINARY only. `ensure_scripts()` runs
`make -C src torirsserver-scripts` unconditionally on every call, by design ("the embedded server
refuses to boot on a stale script pack") -- so the ~38k-script `sscompile` pass is paid every
iteration and is most of the wall clock of a Lua-only retry, several minutes when other sessions are
building. Trap 9's "or pay a full build per retry" is about the C build, not this.

While you are waiting: other authors edit other `test/quests/*.lua` in this same shared worktree, so
a `git status` full of quest files you never touched is their work, not your harness misbehaving --
never revert or stage a file that is not yours.

### SIGSEGV, whole-client hangs, and long runs

*Origin: section 8 ("Gaps reported by authors").*

One run in seven died on a bare `SIGSEGV` (exit -11) with no FAIL or BLOCKED row and nothing in
`client.log`, immediately after a PASSing row, and the identical file ran clean on retry. Re-run
once before hunting a crash like that in the quest file. A WHOLE-CLIENT HANG (no tick, no log line,
a tick-bounded wait that never times out) is the frame loop not returning, not your file:
`sample <pid>` it first. Between a Rock's ^chat_shock page into the ~mesbox was a painter
scenery-chain CYCLE (`bucket_paint_world` at 100% CPU; seam11, `painters.c`), reached because a
multiloc on a quest varp becomes a runtime spawn the first time that varp changes with its scene
loaded, and the SECOND write released it -- which is why a `::setvar`-then-`goto_tile` repro did not
hang and the full quest did.

An assert-enabled private build
(`make OPT=1 OPT_RELEASE_CFLAGS= OPT_RELEASE_LDFLAGS= PROFILE=1 ...`) names the broken invariant;
attach with `lldb -p`, it cannot LAUNCH a client from ~/Documents.

#### A long run is moved to the background: how to tell alive from dead

A LONG run is neither: a `run.py` that outlives the harness's 120 s foreground command window is
moved to the background by the harness -- that is not a breach of a FOREGROUND-only instruction and
not a dead run. Do not start a second one (it refuses), do not sleep-poll (the tool blocks it); wait
for the harness's own background-task-completion notification. To tell alive from silently dead
meanwhile: `build/quest_gate/.locks/<id>.lock` holds run.py's pid (`ps -p <pid>`; the
`torirs_questtest` client is `pgrep -P <pid>`), and `build/quest_gate/<id>/ledger.tsv` gains a row
per step as it happens (`torirs_plugin_drive.c` appends each one) even while run.py's own output has
not surfaced -- a lock whose pid is gone and a ledger with no `SUMMARY` row is a dead run.
