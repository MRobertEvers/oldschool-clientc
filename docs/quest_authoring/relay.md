# Long quests: the ladder, legs and fail.py

A quest with more than 30 guide steps runs an author out of context if it works the way a short
quest does. Measured on Dragon Slayer: a test run costs about 2 KB, but re-reading a 28 KB test
file, a 15 KB guide and 10-15 KB script files many times is what fills the window. Two tools
replace those reads.

## The ladder

`python3 tools/quest_gate/ladder.py <test_id>` prints the guide as a table: one row per guide step,
in the order `helper_coverage.py` grades them (its step count is the same number). You never need
to open the guide Java.

Each step shows:

- `leg.n` -- the leg it belongs to and its number in the guide's order.
- `s<stage>` -- the quest stage the guide files it under (the key of `steps.put(<stage>, ...)`).
- `step` -- the Java field name. Name your test rows after it (`talkToOziach`,
  `goto-talkToOziach`, `talkToOziach-dialog`): the coverage grade finds rows by this name.
- `kind` -- `NpcStep`, `ObjectStep`, `ItemStep`, `DetailedQuestStep`, ... A leaf that came out of a
  panel `ConditionalStep` is written `NpcStep<parentName>`.
- `target` -- `npc:oziach` is the guide's `NpcID.OZIACH`; `loc:` is `ObjectID`, `obj:` is `ItemID`.
  The part after the colon is the content symbol. A trailing `?` means the content has no such
  symbol: look it up (gaps-world) before you press it.
- `(x,y,z)` -- the guide's `WorldPoint`.
- the instruction text (cut at 160 characters), `subs` (the `addSubSteps` children folded into the
  step), `items` (its item requirements by display name), `dialog` (the `addDialogSteps` options).
- `trig` -- the content trigger for the target, `path.rs2:line trigger`, relative to
  `OSRS-Content/osrs239-content/server/scripts/`. The quest's own directory comes first, then
  shared scripts (an npc's area file, `ladders_stairs`), another quest's last; op1 before other ops;
  `+N` says how many more there are. This is the grep you no longer run.

`--json` gives the same data for a program. `--write` writes
`docs/quests/ladders/<test_id>.ladder.tsv` (a `#` header line with the leg cuts, then one TSV row
per step; `=` in `items` means the same items as the row above).

## Legs

A leg is a run of about ten consecutive guide steps: small enough to author, run and fix without
losing the thread. `ladder.py` cuts the steps evenly into `round(steps / 10)` legs, then moves each
cut to the nearest quest-stage boundary (the stage value changes between two steps) within three
steps of it; with no boundary that close, the cut stays where it fell. A quest of 30 steps or fewer
is one leg. `--legs N` asks for legs of about N steps and cuts even a short quest.

### The backpack overflows: setup gives plus an earlier leg's leftovers

Every setup `::give` is in the backpack from the first tick, beside whatever leg 1 has not used up
yet, and 28 slots run out fast: legends' setup gave leg 1's papyrus and charcoal and leg 2's
lockpicks, pickaxe and runes, and the seven gems leg 2 also needs no longer fit (the
gems were moved out of the setup). Count the slots before adding a later leg's items to the setup.
A later leg's bulky items are given INSIDE that leg, after it drops the earlier legs' spent items,
and a `leg.K.pack` `t.check` counts both what was dropped and what was given
(`test/quests/legends.lua` leg 2: drop papyrus and charcoal, then the seven gems).

The overview prints each leg's step range, stage range, and first and last step. Many guides file
most of the quest under one stage (Dragon Slayer: stage 2 from the Oracle to boarding the ship), so a
leg often starts and ends in the same stage; that is the guide, not an error.

### The full run ends at the frame budget: every leg's ticks add up

`--from-leg K` starts from a checkpoint, so each leg alone fits the default budget of about 2,000
server ticks. The full run plays every leg from tick 0, and a long quest passes that budget partway
through. Monkey Madness I passed it before leg 6 and used 2,654 ticks in all. The run then ends
with `run.unfinished` and `the client exited 0 at the frame budget` (running.md). Once the legs you
have written add up to more than about 1,500 ticks, put `max_frames = <n>,` beside `fixture` (n is
about 30 per tick, ceiling 240000). The ticks column of the last full run's ledger gives the sum.

## Working one leg

`python3 tools/quest_gate/ladder.py <test_id> --leg K` prints only leg K, after three lines:

```
quest: dragon (quest_dragon, guide .../DragonSlayer.java) -- leg 2 of 4, steps 12-22 of 44
starts at stage: 2 (enterMelzarsMaze)
previous leg ended with: optionsForLozarPiece
```

This is what a relay author is handed. The previous leg's last step is where the committed test
file already stops: find its row with `grep -n 'optionsForLozarPiece' test/quests/<id>.lua` and
read twenty lines around it, then write this leg's rows after it.

## After a run: fail.py

`python3 tools/quest_gate/fail.py <test_id>` prints, in at most 3,000 characters: the SUMMARY row,
the three rows before the first non-PASS row, that row with its full detail (a detail over 900
characters keeps its head and tail), the row after it, the row's screenshot path, and the last
three `script error` / `STALE SCRIPT PACK` / `assert` lines of the run's log.

- `--all` lists every non-PASS row, one line each (at most 40).
- `--context N` shows N rows before instead of three.
- `--leg K` keeps only rows named after leg K's steps (`goto-` / `walk-` prefixes and `.x` / `-x`
  suffixes count).
- `--name <run>` reads `build/quest_gate/<run>/ledger.tsv` (a `--name`d run).

Exit 0 when every row passed and the run wrote a PASS SUMMARY, 1 when it did not (a failing row, or
a run that stopped before its SUMMARY), 2 when there is no ledger, 3 when a detached run is still
going and no row has failed yet ("Runs longer than the shell cap", below). `run.py ... && fail.py ...`
therefore stops on a red run only after fail.py has printed it.

## Runs longer than the shell cap: --detach and --wait

A shell call is capped at 10 minutes, and the cap kills the run with it: no SUMMARY, and a ledger
that just stops. A full run of a long quest (the last runner of a relay, a reviewer, the gate pass)
can take longer. Any run you expect to take over about 8 minutes is started in the background and
waited on in pieces of 9 minutes:

```sh
python3 tools/quest_gate/run.py <id> --no-build --no-publish --timeout 1800 --detach   # returns at once
python3 tools/quest_gate/run.py --wait <id>                    # blocks up to 540 s (--timeout S)
python3 tools/quest_gate/run.py --wait <id>                    # again, while it exits 3
python3 tools/quest_gate/fail.py <id>                          # the finished run, as always
```

- `--detach` works with every single-session form: a quest id, `--from-leg K` / `--only-leg K` (the
  run name is `<id>.leg<K>`), and `--script <file> --name <label>` (the run name is the label).
  Not `--all`. Every other flag means what it means in the foreground; the child is the same
  `run.py` command without `--detach`.
- `--timeout` on the detached command is still the run's own wall-clock ceiling (default 400 s,
  scaled by the quest's `max_frames`): a run you detach because it is long needs a `--timeout`
  that covers it, or the run is killed at the ceiling exactly as in the foreground. On `--wait`,
  `--timeout` is how long this wait blocks (default 540).
- `--wait <name>` exits **0** when the run ended with exit 0, **1** when it ended otherwise, **2**
  when no detached run has that name, and **3** when it is still running. Exit 0 is run.py's own
  answer (the client finished and wrote a ledger), not a pass: a red SUMMARY still exits 0, so
  grade the run with `gate.py` / `fail.py` as always. On an end it prints
  `<name> ended: exit=N after Ss`, the ledger's SUMMARY row and the run's report and failure
  block. Still running, it prints `still running: last row <step> at tick N (<verdict>; R row(s);
  ...)` -- wait again. A child that died without writing its end (a Python exception, a kill)
  prints `DIED without writing its end status` and the tail of its output, exit 1.
- The run's files are in its session directory, `build/quest_gate/<name>/`: `run.pid`,
  `run.status` (`state=starting|running|done`, and at the end `exit=`, `ended=`, `elapsed=`) and
  `run.out` (everything the foreground command would have printed, build output included).
- A second `--detach` of a live name is refused and names the pid (the session lock would refuse
  it anyway). A foreground run of the same name clears the old detached files.
- `fail.py` on a run that is still going says `IN PROGRESS: detached run pid N ... this ledger is
  partial` first, reads a missing SUMMARY as `not yet: the run is still going`, and exits **3**
  unless a row has already failed (then 1, with the row, as usual). Reading the partial ledger
  mid-run is fine; it is how you see where a long leg has got to without waiting for the end.
- Never wait on a detached run with `sleep` loops or a monitor; `--wait` is the one wait.

**A hung client dies in about 90 s, not at the ceiling (seam32).** The wall-clock ceiling above is
only a backstop. The driver's per-frame pump (`torirs_plugin_drive.c` `drive_heartbeat`) rewrites
`build/quest_gate/<name>/heartbeat` (`tick=T`) every 25 server ticks, about every 3 s of wall time.
run.py polls every 5 s and kills the process group when that file is older than
`TORIRS_QUEST_STALL_SECONDS` (default 90). Before the first beat the client gets
`TORIRS_QUEST_STALL_BOOT_SECONDS` (default 120) to boot, log in and start the script. The ledger then
ends in `run.unfinished` FAIL, `run stalled: no client tick for N s; last row <name> at tick T`
(`run stalled at boot: ...` for the boot case), and `TIMEOUT.png` holds the last shot, the same as for
a timeout. The report's `timed_out` column reads `stall`. A long await, a 500-tick kill wait or a relog
keeps ticking, so it keeps beating and is never killed: a long run is never the reason for a stall
kill, only a frozen frame loop (a C spin, or an await `level` predicate that loops: it runs outside
the instruction budget after its first call) or a clock that stopped. `TORIRS_QUEST_STALL_SECONDS=0`
turns the detector off. A binary built before seam32 writes no heartbeat; run.py prints `has no
heartbeat ... stall detector off` and only the ceiling applies.

## Output discipline

Every byte a command prints stays in your context for the rest of the quest. So:

- Never `cat` the whole test file. Locate with `grep -n '<row name>' test/quests/<id>.lua` and read
  twenty lines around the hit (`sed -n 'A,Bp'`).
- Never open a ledger directly. Use `fail.py` (and `fail.py --all` for the list).
- Never open the guide Java. Use the ladder; `--leg K` for the leg you are on.
- Read a script file by the line range the ladder's `trig` column gives (`sed -n 'L,+40p'`), not
  whole.
- Pipe any exploratory command (a `grep -rn` over the content, a `helper_coverage.py` run, a debug
  dump) through `| head -c 4000`.

## Checkpoints

A relay author working leg K should not replay legs 1..K-1 on every run. A legs file lets the
harness save the player after each leg and resume from there (seam30 leg_checkpoints).

### The file shape

```lua
return {
    id = "dragon",
    fixture = "fresh_lumbridge.ini",
    setup = { "::clearinv", ... },
    bind = { varp = "varp176_dragonquest", constants = { ... }, row = "quest_dragon", display = "Dragon Slayer", points = 2 },
    legs = {
        { name = "oziach", run = function(t) ... end },
        { name = "map",    run = function(t) ... end },
        ...
    },
}
```

- `legs` replaces `run`. A file with `run` works as before. `lint_quest` refuses a file that has both.
- `bind` is the `t.quest.bind` table. The harness binds it after setup and before the first leg that
  runs, so a leg resumed from a checkpoint is bound too. Never call `t.quest.bind` inside a leg
  (lint). A file that uses `t.quest.*` must declare `bind` (lint).
- Each leg is self-contained. It may read file-level constants. It shares no locals with another
  leg: a resumed leg never ran the others. Leg names are `[A-Za-z0-9_-]` and unique.
- Only the LAST leg calls `t.finish(0)` (lint). `t.blocked` may end any leg. When the last leg
  returns without finishing, the harness finishes the run.
- Every other rule (`lint_quest`, `gate.py`, `helper_coverage`) applies inside each leg exactly as
  it applies inside `run`. Row names do not change. `test/quests/_legs.lua` is the committed
  harness fixture. The seam30 conversion of cooks_assistant (legs `start`, `gather`, `handin`: the
  same 45 rows, green) is kept at build/seam_state/seam30/cooks_assistant_legs_form.lua; it was not
  committed, because the seam pass never commits a quest file.

### What a run writes

- Before leg k, row `leg.<k>.<name>` (PASS). Its detail is the player's tile (client), the bound
  quest variable read from the SERVER, and the backpack:
  `tile=3208,3215,0 stage=cookquest=1 inv=... -- checkpoint 1 written: checkpoint 1 written at 3208,3215,0`.
- After leg k, when every row leg k wrote was PASS, the harness sends `::checkpoint k`. The server
  writes the player through its own save serialiser in checkpoint mode (`torirs_server_save.c`: the
  logout save plus every non-zero varp whatever its scope, the player's random stream, and the
  world's `map_clock`) to `<session>/saves/checkpoints/<k>.ini`. The next leg row carries the
  outcome; the last leg of the run (the file's last leg, or leg K of `--only-leg K`) writes
  `leg.<k>.end` for it.
- The file's LAST leg gets a checkpoint too when it returns without `t.finish` (seam31): the leg a
  relay author has just written is the last one, and the next author appends leg k+1 and runs
  `--from-leg k+1` from it (a checkpoint hashes legs 1..k only, so appending a leg keeps it fresh).
  No placeholder leg (`leg2_todo`) is needed any more. A leg that calls `t.finish` (the quest's last
  leg) or `t.blocked` ends the run there and writes no checkpoint and no `.end` row. The harness's
  own row is named `leg.<k>.end`; a leg's closing reading of its own is better named
  `leg.<k>.state`, or the ledger carries two rows of one name (legends' `t.check("leg.2.end", ...)`
  does today).
- After the run, `run.py` wraps each save as `build/quest_gate/<run>/checkpoints/<k>.ckpt`. The
  `[checkpoint]` manifest holds the test id, k, the leg names, and sha256 hashes of legs 1..k's
  source text, the setup list, the `bind` table, the content pack
  (`server/scripts/build/script.dat` + `.idx`) and the engine binary. It also holds the fixture
  name, the saved tile, the source run and the time. A leg whose checkpoint was refused leaves
  `checkpoints/<k>.refused` with the reason.

### Quiet points: when a checkpoint is refused

A checkpoint is per-player state only. It does not restore npc positions, a spawned loc, an
instance, a parked script or an open interface. The server refuses `::checkpoint` and names the
reason:

- `checkpoint k refused: a dialogue is open (chat modal interface 219)`
- `checkpoint k refused: an interface is open (main M, side S)`
- `checkpoint k refused: a script is parked on the player (a dialogue, delay or cutscene is still running)`
- `checkpoint k refused: the player is in combat (attacking <npc>, slot N)`, `(single-way claim
  for N more tick(s))`, or `(<npc> is attacking)`

A parked script or a combat claim often clears by itself: the milk script is still parked one tick
after the bucket fills. So the harness retries once a tick for up to `QD.LEGS_QUIET_TICKS` (10) and
notes `(after N quiet-wait tick(s))`. It does not retry a dialogue or an interface. The leg row then
says `checkpoint k NOT written: ...`, and the leg boundary is in the wrong place. Put boundaries at
quiet points: outside a fight, a cutscene, an instance or a dialogue, with the camera free
(`t.world.camera().server_driven == false`): before `::checkpoint` the harness waits the same
`QD.LEGS_QUIET_TICKS` for a running cutscene to reset, and a camera still held is
`checkpoint k NOT written: the camera is server-driven ...` (verbs-cutscene.md). A leg that wrote a non-PASS row
gets no checkpoint (`NOT written: leg k wrote N non-PASS row(s)`).

#### Still "in combat" after the fight: an aggressive npc respawned across a barrier

`checkpoint k refused: the player is in combat` can outlast the fight that just ended. In legends
leg 7 Ranalph Devere respawned on the far side of the force barrier at 2421,4691 and engaged the
player standing at 2421,4690: he could not reach, the player could not reach him ("I can't reach
that!"), and the combat state never cleared, so every quiet-wait tick refused. Walk out of his
aggression range before the leg ends (leg 7 walks to 2396,4679 and waits 12 ticks), and put the
boundary there.

#### `checkpoint k refused: the player is in combat` in a room the next leg starts in (sonnet-b42)

No checkpoint can be written inside a room of aggressive npcs that keep attacking. Underground
Pass leg 4 ended in the skeleton room (2371-2383,9605-9611, `m37_150.spawn`; level-25 skeletons),
and leg 5 began by clicking the door at 2375,9611 in that same room: every quiet-wait tick refused,
four runs in a row. Move the boundary, not the fight: end the leg BEFORE the room, or carry the
door click into the leg so it ends on the far side. Arm the account in `setup` for the fight the
room forces.

Clearing the room for real also works when the npcs do not respawn at once. Seam34 ran that same
leg 4 unchanged on the fixed driver. It armed the account, wore the scimitar and killed all five
skeletons with `t.player.attack` plus `t.npc.await_dead_engaged` (`leg.4.skeletons ... attack ok
dead ok`), and the quiet wait then passed: `checkpoint 4 written at 2380,9607,0`
(build/quest_gate/upass, 114/0). The author's refused runs ended with skeletons still alive: run 9
fought unarmed and died, and run 10's attacks answered `refused` (already under attack). Run 10's
`leg.4.wield` row also failed. That `timeout` on a wield that had landed was a driver seam;
`inv_op` now answers `ok ... [WORN <item>: worn 0 -> 1, wear slot N]`.

### Running one leg

```sh
python3 tools/quest_gate/run.py <id> --no-build --no-publish        # full run: writes the checkpoints
python3 tools/quest_gate/run.py <id> --from-leg K --no-build        # legs K..end from checkpoint K-1
python3 tools/quest_gate/run.py <id> --only-leg K --no-build        # leg K alone
python3 tools/quest_gate/fail.py <id> --name <id>.legK              # read that run
```

- Both modes run under `build/quest_gate/<id>.leg<K>/`, so they never overwrite the full run. They
  log in as `<id>`, the same account as the full run: the checkpoint is that account's save, and
  `session.relog` types that name. They skip the setup list and wait for the client to stand on the
  saved tile. The first leg row says `resumed from checkpoint K-1 (from_leg=K)`.
- A checkpoint run writes checkpoints for the legs it completes. Leg K+1's author can resume from
  a `--from-leg K` run without a full run: `--from-leg` takes the newest fresh `<K-1>.ckpt` under
  `<id>/` or any `<id>.leg<J>/`.
- It REFUSES with exit 2 and names the reason when checkpoint K-1 is missing (and why: the
  `.refused` reason, or "run the full test first") or STALE: legs 1..K-1's text, the setup, `bind`,
  the fixture, the content pack or the binary differ from the manifest. A shared file-level constant
  is not hashed. After editing one, run the full test again.
  A rebuilt content pack or client binary makes EVERY checkpoint stale at once, even when the
  test file did not change: after a seam lands or another author's script edit, plan a full run
  before any `--from-leg`. On a long quest that is a `--detach` run (legends: ~6 min to leg 7,
  ~9 min for all ten legs, past the shell cap -- "Runs longer than the shell cap" above).
- The SUMMARY row is stamped `from_leg=K` (and `only_leg=1`). Such a run is never published.

### The clock: map_clock comes back with the checkpoint

A resumed run boots a fresh world, whose `map_clock` starts again from 0. The content stamps a class
of player varps with `map_clock` -- `%action_delay = map_clock` (`bullroarer.rs2:8` and every
skill loop), `%frozen = add(map_clock, n)`, the agility shortcuts' `*_used`, the boss cooldowns --
and a checkpoint carries every varp, so before seam31 a stamp from the full run read as hundreds or
thousands of ticks in the future: `run.py legends --from-leg 2` answered every bullroarer swing
with "You're a bit too busy to do that at the moment." (`bullroarer.rs2:2`, `if(add(%action_delay, 8) > map_clock)`).

So the checkpoint also writes `[clock] map_clock = N`, and a login from it moves the world's clock
FORWARD to N (server log: `checkpoint login from ...: map_clock 0 -> N (restored, ...)`). Every
varp of the class stays consistent at once, and a script that reads `map_clock` itself sees what
the full run saw. Measured on legends leg 1 (`_s31_legends`, leg 2 = one swing): full run
`%action_delay=320`, answer "You start to swing"; `--from-leg 2` before the fix `%action_delay=263`
"too busy", after it `%action_delay=322` "You start to swing".

- The class, by `grep -rhoE '%[a-z0-9_]+ *= *[^;]*map_clock'` over `server/scripts/` (2026-09-30):
  60 vars, 52 of them player varps -- 50 `scope=temp` (`action_delay`, `frozen`,
  `blackjack_ko_expire`, `vengeance_ready`, `gauntlet_eat_delay`, the `*_pipe_used` /
  `*_ropeswing_used` agility stamps, the ToB/Nex/Inferno/Zulrah timers, ...) and 2 `scope=perm`
  (`imbued_heart_ready_tick`, `hunter_falcon_expire`; both `scope=temp` since seam32, the one perm
  stamp left is `zq_rash_timer`), plus npc (`npc_action_delay`) and world
  (`wildy_hot_ends`, `star_next_crash`, `cell_*_unlock_timer`) vars a checkpoint does not carry.
  LostCity saves none of them: a logout drops every `scope=temp` varp. The full list is
  `build/seam_state/seam31/checkpoint_clock_class.txt`. A new clock stamp needs nothing here: the
  clock, not a list, is what is restored.
- Forward only. A world already past N keeps its clock (a clock run backwards would push world
  timers into the future) and logs `the clock is not run backwards`; on a fresh boot it cannot
  happen, because the full run's clock at leg k counts the same boot plus the legs.
- The jump is time passing for the world: an npc respawn or a shop restock that falls due fires on
  the first tick. The npcs themselves start fresh as before (a bird that took a swing in the full
  run may not be there in the resumed one).

### Honesty

A checkpoint run is for authoring. It is never a verdict:

- `gate.py <id>.leg<K>` (or any ledger whose SUMMARY carries `from_leg`) is RED:
  `a checkpoint run is for authoring; grade the full run`.
- `queue.py set <id> --status green` needs a full-run ledger at `build/quest_gate/<id>/ledger.tsv`
  with a SUMMARY row and no `from_leg`.
- `publish()` refuses a `from_leg` ledger, and any run that did not reach `t.quest.expect_complete`
  (seam31): a legs file, or a run file that calls `expect_complete`, publishes only a ledger whose
  `quest.varp_complete` and `quest.scroll_title` rows are PASS. A legs file whose last leg returns
  unfinished gets a PASS SUMMARY from the harness, and that partial run used to replace the
  quest's evidence in OSRS-Content; now run.py prints `not published <id> (the run did not reach
  t.quest.expect_complete ...)`. `--no-publish` is still the rule for an authoring run.
- Determinism: legs K..end from checkpoint K-1 reach the same stage, backpack and tile as the full
  run. Measured: `_legs` `--from-leg 3` ends with a `legs.end_state` detail byte-identical to the
  full run's. `cooks_assistant --from-leg 3` writes the same 16 rows as the full run from
  `leg.3.handin`: varp 2/2, qp 0 -> 1, 300 Cooking XP, the completion scroll. A resumed fight rolls
  as it would have, because the player's random stream is in the checkpoint. The npcs' streams are
  world state and start fresh.
