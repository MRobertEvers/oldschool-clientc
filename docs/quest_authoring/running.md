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

#### A 3-5 minute `--from-leg` run slips into the background at 120 s: pass the tool's timeout (sonnet-b46)

The 120 s window is the Bash tool's DEFAULT timeout, not a ceiling. A `run.py <id> --from-leg K` on
a long quest takes 3-5 minutes (Underground Pass leg 8: about 3), so with the default it is moved
to the background. Pass `timeout: 590000` on the Bash call (just under the tool's 600000 ms
maximum) and the run stays in the foreground until its SUMMARY. A run you expect to pass about 9
minutes still goes through `--detach` / `--wait` (relay: Runs longer than the shell cap).

### A run that ended unfinished: `run.unfinished FAIL run ended without finishing` (seam31)

*Origin: seam31 run_never_ends_silently (legends leg 7: exit 0, no SUMMARY, three runs in a row).*

Only the client writes the SUMMARY row. A client that stops any other way used to leave a ledger
that just stopped. Now `run.py` finishes it: one FAIL row `run.unfinished` whose detail reads `run
ended without finishing: <reason>; inside row '<name>' (begun at tick T); last progress: ...; last
row written: <name>`, then a SUMMARY counted from the rows. The reasons:

- `the client exited 0 at the frame budget: TORIRS_MAX_FRAMES=N is about M server ticks` -- the
  virtual clock ran out. The budget is about `max_frames / 30` server ticks (the 60000 default is
  about 2000 ticks). Declare `max_frames = <n>,` beside `fixture` in the quest table (ceiling
  240000, about 8000 ticks). Legends through leg 7 used 4587 ticks.
- `killed by run.py after the wall-clock timeout of N s` -- the per-process `--timeout` ceiling.
- `the client was killed by signal N` / `the client exited N without t.finish (a crash or an
  assert)` -- with the last fatal-looking `client.log` line.
- `the client exited 0 without t.finish at tick T, short of the budget` -- unexplained; read the
  tail of `client.log`.

`client.log` now carries `QUEST row-begin <name> tick=T` before every `t.exec` verb and `QUEST
progress <text> tick=T` every 25 ticks of any await whose deadline is 30 ticks or more (every 10
ticks inside the kill waits): `grep 'QUEST progress'` shows where a slow run spends its ticks. A
Lua error's own `script-error` row gains `-- raised inside row <name> (begun at tick T)`. An await
predicate that raises is still read as not-yet-true by the C scheduler, so its row times out; run.py
appends `its await predicate raised ...: <error>` to that row.

### `--script` runs are deterministic: a rerun replays the same rolls (seam31)

The same file, fixture and `--name` replay the same random stream, so a `t.blocked` on a roll
(Gujuo's bowl blessing, a `stat_random` smithing step) gives the same result on every rerun. Change
the attempt budget or the state before the roll (drink a restore, retry beyond N), not the run
count. The player NAME seeds the stream (seam-facts: Seam pass 32 (g)): a scratch copy under
another `--name` rolls differently (Zombie Queen's loose rocks caved in on 3 of 7 differently named
runs, seam33), so a test loops every roll it depends on instead of trusting its own id's luck.

### Render skip: a run draws only the frames something reads (seam34)

*Origin: owner request 2026-09-30 ("send commands to the client to stop rendering the scene ...
Make sure screenshot requests still work. Just don't waste time rendering every frame").*

A quest client is frame-locked (one 20 ms tick per frame) and uncapped, so a run goes as fast as
a frame costs, and the software renderer drawing 765x503 was most of that cost. `run.py` (and
`conformance.py`) now start every client with `TORIRS_RENDER_SKIP=1`: a frame still runs its
tick, input, net, plugins, UI layout and emit walk, but `App_Render` and the present are skipped
unless something must see the frame. `--render-every-frame` turns it off (the A/B; it is exactly
the old run). Nothing in a quest file changes.

The run is the SAME run as with skip off, frame for frame: render-time state (the world
pickset, posed model heights) is drawn late, at the moment something reads it, never waited for.
A first version cost a frame per screenshot and per pick read; five quests (hazeelcult,
idesofmilk, ikov, junglepotion, rovingelves) went red on the shifted timeline -- a death, a
missed item race -- and green again once nothing moved a frame. What draws (src/app/app_render.c's
render-skip banner is the source):

- a screenshot (`t.shot`, every `t.exec`/`t.check` shot): its own frame, and a skipped frame
  before it is drawn late first (the overlays and the mouseover text in the picture are laid out
  from the frame before, as with skip off);
- a pushed click or move (`api_drive.mouse_move`/`mouse_button`): the current frame, the pickset
  the click is resolved against next frame;
- a pickset read (`api_drive.pick_holds`/`pick_point`) or a camera move (`api_drive.camera`): a
  skipped previous frame is drawn late, with the camera it had, before the read or the move;
- a live sailing hull standing in the loaded scene (which hulls are drawn in full is decided
  while painting): every frame -- a sailing quest saves less;
- a GPU renderer: every frame (only the software lane can skip; quest runs are `--soft3d`).

Skip only ever takes a draw away: a frame the client would not have drawn anyway (an async
settle, a world load -- `App_RunOnce` withheld the commit) is not drawn, or drawn late, for any
of these. (Forcing one crashed conformance: its emit list still pointed at a widget model's
freed render cache.) A pick read that finds the skipped frame behind such a stretch answers
"nothing stamped" (`held=false`, `valid=false`) rather than an older frame's set.

Under skip a quest-script run also drops the content-test mailbox's 1 ms idle nap
(`ContentTest_End`), which a quest run -- never idle -- was paying every frame.

Render-time state NOT covered, all pictures-only on the 87-quest suite (ledgers identical, 63%
of shots byte-identical): the cursor tooltip box (the cache's `~mouseover_tooltip` reads mouseover
entries built from the last DRAWN frame's pickset, so it can name what the pointer hovered
earlier -- 96% of the differing shots differ only there); a model whose bind pose was never
captured accumulates its animation once per draw, so its pose depends on the draw count
(druid's suits of armour); and `CAM_SHAKE` jitter draws `rand()` while painting, which `cs2vm2`'s
random opcode shares (only during a camera shake; the server's rolls are its own). The
top-left mouseover text, overlays and everything else in a picture match skip off.

The verbs, for the rare test that reads render-time state no verb knows about:
`t.render.skip(true|false)` -> `ok refused` (detail names the old state and the frame counts);
`t.render.frame()` -> `ok timeout`, forces one drawn frame and waits for it. Live clients: type
`::renderskip` (toggle) or `::renderskip on|off` in the chatbox; the screen stops updating while it
is on, and the chatbox still takes the line that turns it off. `client.log` ends with
`render-skip: N frame(s) drawn, M not drawn, K of those drawn late for a read` whenever the knob
was set.

Speed, measured when it landed (2026-10-01, one binary, `run.py <quest> --no-build --no-publish`
wall, off then on back to back, other seam workers sharing the machine; every pair's ledgers
identical row for row, SUMMARY ticks included):

| quest | ticks | `--render-every-frame` | default (skip on) | speedup |
|---|---|---|---|---|
| cooks_assistant | 58 | 11.1 s | 5.4 s | 2.1x (boot-bound) |
| druid | 79 | 13.9 s | 7.0 s | 2.0x |
| elena | 393 | 51.4 s | 18.8 s | 2.7x |
| arena | 295 | 39.3 s | 10.9 s | 3.6x |
| zombiequeen | 707 | 69.0 s | 17.0 s | 4.1x |
| druidspirit | 845 | 90.1 s | 22.6 s | 4.0x |
| currentaffairs (sails) | 751 | 80.1 s | 42.9 s | 1.9x |
| pryingtimes (sails) | 1701 | 173.2 s | 101.3 s | 1.7x |
| legends | 5499 | 563.6 s (seam34 fixer's run) | 72.3 s | 7.8x |

All 88 discovered quests with skip on: 611 s wall at three at a time (the sum of the 88 runs
1,802 s), every ledger identical to the previous skip-off green run and `gate.py --all`
green; the fixer's skip-off suite was 1,318 s wall at four at a time (client sum 5,149 s).
Sailing saves least: a hull in the scene draws every frame.

A content fix in a file outside your seam can be proved without touching the shared tree
(matthew-mbp-m4-b49-seam1, Troll Romance's caves): copy `server/scripts` (without `png`, `bmp` and
`build*`) under a mirror root that symlinks every other content entry, edit the copy, run
`sscompile --src <mirror>/server/scripts --out <mirror>/server/scripts/build` with the mirror as the
content root (the shared root makes the lane dirs compile as base and fail), then
`TORIRSSERVER_SCRIPTS=<that build> python3 tools/quest_gate/run.py ...`; `client.log` names the pack
it loaded. Build a control pack without the edit too: before 12/6, after 18/18, control 12/6 is what
makes the one line the whole difference. Report the line as `needs:` for the closer.

### `new_quest.py`: `helper dir not found` on Linux, and the scaffold is one `run`, not legs (vm-b1)

`new_quest.py <test_id>` takes the queue row's `helper_dir` and looks for it under `--qh-root`.
That defaults to a macOS checkout (`DEFAULT_QH` in `tools/questhelper_extract.py`), so on any
other machine it stops with `helper dir not found: <helper_dir>` even though the row is right.
Pass the helpers root yourself:

```sh
python3 tools/quest_gate/new_quest.py <test_id> \
    --qh-root /home/user/quest-helper/src/main/java/com/questhelper/helpers/quests
```

The scaffold it writes is a single `run = function(t)`, not the `legs = { ... }` form that
`relay.md` and `run.py --from-leg` need. For a quest over 30 steps, split it into legs by hand
before the first run: one `{ name =, run = function(t) ... end }` per leg of the ladder, with the
setup list kept at the top.
