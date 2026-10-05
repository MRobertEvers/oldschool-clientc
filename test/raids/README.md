# Raid room tests

One file per raid room (or per whole-raid relay): `test/raids/<raid>_<room>.lua`,
returning a table. The shape is a quest test's exactly -- `{ id, fixture,
setup = {cheats}, run = function(t) ... end }` or a relay's `legs = {...}`
(`test/quests/README.md`, `docs/QUEST_AUTHORING.md`) -- because a room test IS
a quest-driver test with a tick ledger (`docs/RAID_ORCHESTRATOR.md` section 6).
The verbs are the driver's (`tools/quest_gate/verb_list.py`); the raid verbs
are `t.prayer.*`, `t.raid.*`, `t.ticklog.*` (`docs/minigames/raid_loop/`).

## Why not test/quests/

`tools/quest_gate/quest_list.py` discovers every `test/quests/*.lua`, so a raid
test there would be run by the quest loop's `run.py --all`, `gate.py --all` and
`make test-quests` on every machine, and its verdict would land in that loop's
gates. The owner's rule (2026-10-02): the raid loop never interacts with the
quest loop -- no `QUEUE.tsv` row, no claim, no `docs/quest_authoring/` edit,
nothing pushed to v3.

## Running

```sh
python3 tools/raid_gate/run.py tob_maiden --no-publish     # one room
python3 tools/raid_gate/run.py --all --jobs 2              # every test/raids/*.lua
python3 tools/raid_gate/gate.py tob_maiden                 # its gate
python3 tools/quest_gate/lint_quest.py test/raids/tob_maiden.lua
```

The wrappers take `tools/quest_gate/run.py` / `gate.py`'s arguments unchanged
(`--from-leg K`, `--script`, `--name`, `--no-build`, `QUEST_BINARY=...`); they
set `TORIRS_QUEST_TESTS_DIR=test/raids` and `TORIRS_QUEST_PUBLISH_DIR` and exec
the quest gate script (`tools/raid_gate/README.md`).

- Fixtures come from `test/raids/fixtures/` (seeded with `fresh_lumbridge.ini`).
  `tob_normal_done.ini` is `fresh_lumbridge.ini` plus one Theatre of Blood completion
  (`6826 = 1`, `varp6826_tob_completions`), so every raider of a Hard party test passes the
  door's "You must complete the Theatre of Blood once" check (raid seam19).
- Artefacts land in `build/quest_gate/<id>/` like a quest's. That directory,
  the session lock (`build/quest_gate/.locks/<id>.lock`) and a relay's
  checkpoints are keyed by id alone, so **a raid id must never equal a quest
  id**: name it `<raid>_<room>` (`tob_maiden`, `cox_olm`, `toa_150`), and the
  wrappers refuse to run if any `test/raids/<id>.lua` shares an id with
  `test/quests/` or `QUEUE.tsv`.
- A PASS is published to
  `OSRS-Content/osrs239-content/server/scripts/selftest/minigames/<raid>/<room>/play/`
  (`tob_maiden` -> `tob/maiden`: the id split at its first `_`), not under
  `selftest/quests/`. `--no-publish` skips it.
- `gate.py`'s Quest Helper coverage check reads "not graded" for a raid id (no
  `QUEUE.tsv` row, no guide) -- right: a room's coverage is its encounter spec
  table, graded by the raid lane's own checker, not by a quest guide.
- Names starting with `_` are not tests (the quest suite's rule): a raid
  harness file may use it.

## Watch it

To watch any automated script -- a raid room, or any of the quests under test/quests/ --
play itself in a real window, run the one command:

```sh
./launch run osrs239-scripts
```

1. Log in with any name. If the Character Creator opens, Confirm it (measured 2026-10-05:
   a brand-new name in this profile showed none, and Play logged it out cleanly).
2. Open the **Scripts** tab on the plugin rail (the play-triangle icon).
3. Pick a **Suite** (All, Quests, Raids) and type in **Search** (for example `cook` or
   `maid`). The list follows every key, and shows the first 12 matches with "n more:
   refine the search" below them.
4. Click a row, then press **Play**.

Play logs you out, makes a FRESH account from the test's own fixture (the id's letters and
a number, for example `cooksass1`), logs it in and plays the test at real speed: the
setup list, then `run`, or every leg of a legs file in one sitting, as a full `run.py`
run does. **Stop** ends it at its next step. Driver, Test (suite, id, account), Leg (leg k
of n), Step and Rows follow the run; Summary and Session show its ledger and shots:
`build/quest_gate/watch/<account>/`.

The list is every test file the launcher found when it started (it writes
`script/tests/tests.ini` on every launch). The test itself is read again on every Play:
edit `test/raids/<room>.lua` or `test/quests/<quest>.lua`, press Stop, then Play, and the
edited file runs. A test file created after the client started appears after
`python3 tools/raid_gate/prepare_scripts.py` and **Refresh**. A script already running is
never swapped mid-run, and the driver's own verbs (script/plugins/quest_driver/) are read
only when the client starts.

A watched run is not a test run, and it grades nothing:

- It runs on the wall clock, and each account's random stream is seeded from its name, so
  its rows can differ from the kept ledger's (a combat room most of all).
- Party tests (`*_normal`) are listed as unavailable, with that reason: they need three clients
  in lock step.

Grade a room only with `tools/raid_gate/run.py` and `gate.py` (above). The profile, the
headless way to drive the tab and the measurements are in
docs/minigames/raid_loop/DRIVER_NOTES.md, "Watching a test: the Scripts tab".

## Rules a room test is held to (RAID_ORCHESTRATOR.md section 6)

Every rule of `test/quests/README.md` (no numeric interface ids, every step a
ledger row, a shot per interaction, deadlines in server ticks) plus three kinds
of row, all required:

1. **The fight, driven.** Every point of boss damage from the player's own
   attacks; every mechanic met by a real move built from the driver's verbs
   (prayer switched, tile stepped, pillar hidden behind, maze walked, orb
   picked up). Bring-alongs as for quests (`::setlevel`, gear, potions, food).
   Never `::godmode`, a scripted kill, a teleport past a room or phase, or a
   debugproc that performs the mechanic.
2. **The technique rows.** Each published technique is a row seen from the
   player's side: the 5-tick Xarpus step lands one tick before the scan, the
   player is never adjacent to Bloat on a stomp tick, every tile stood on in
   Sotetseg's maze is a maze tile, no Verzik P1 auto resolved while the pillar
   stood between. A kill row alone proves nothing.
3. **The tick ledger.** From `t.ticklog.rows()` / `t.ticklog.gaps()`, one PASS
   row per spec-table mechanic, named `spec.<mechanic_id>`, whose detail starts
   `measured <value>[ <unit>][, <free text>] (spec <value>[ <unit>], grade <G>, tol <tolerance>)`:
   `spec.maiden.cad` -> `measured 10 ticks, 40 of 40 gaps (spec 10 ticks, grade B, tol exact)`;
   `spec.maiden.first` -> `measured 9 (spec 9, grade B, tol +-1)`; a grade E row ->
   `measured 3 (spec 1-5, grade E, tol approx); approximation, M40`. A measured comma
   list is the distribution (every instance must meet the tolerance). The spec value,
   grade and tolerance are copied from the room's table
   (`docs/minigames/<raid>/encounters/<room>.tsv`); `tools/raid_gate/raid_coverage.py <id>`
   grades the ledger against it and `tools/raid_gate/gate.py <id>` runs it after the
   quest gate -- a room is green only when coverage reads FULL.
   **Scope.** A test plays one mode at one party size, so its first spec row is
   `t.check("spec.scope", true, "mode=entry party=1")`; rows the table marks for another
   mode (hard, entry, normal), rows whose `entry_` sibling applies in entry mode, and rows
   that name a party size or scale other than the test's are skipped by the grader and
   listed, never counted. **Text rows** (unit `text`) read
   `measured <text> (spec <text>, grade <G>, tol exact)` and match by equality. The
   measured free text may carry parentheses; the `(spec ...)` group is the last one.

### Cheats inside `run`

Setup takes bring-alongs only (`::setlevel`, `::give`). Inside `run` the only cheats a
room test may issue are READ-ONLY state readouts the raid's content provides as
instruments -- `::tobstate`, `::toastate`, `::coxstate`, `::tobwhy`, `::tobboss`,
`::tobsotestate` and their like, procs whose body only `mes`es what it reads -- used to
corroborate a ledger row, never to replace a measurement from the tick log. Anything
that changes the world (`::tobgo`, `::tobadv`, `::tobscale`, `::tobsotedrain`,
`::tobcrabdrop`, `::kill`, `::godmode`, `::setvar`, a heal, a teleport past a barrier
or a phase) is a rejection. `t.raid.enter` issues the room-entry cheat itself.

An npc acting on tick T sees the world as it stood at the end of tick T-1
(`docs/minigames/theater_of_blood/ENCOUNTER_TIMING.md` section 1): a step that
must dodge an attack lands the tick BEFORE the scan. Loop on the boss's state,
never on a fixed tick count (`TORIRS_EMBED_CLOCK_MS=20` fixes the clock, the
rolls are per-entity streams). Set `max_frames` per leg from a measured run,
under `quest_list.MAX_FRAMES_CEILING`.

## Lessons from the first ToB room pass (2026-10-02, entry mode, solo)

Five of six rooms were rejected, mostly for the author's own play. Do not repeat these:

- **Prayer and food run out.** Maiden's pools drained 29 prayer points and Protect from
  Magic lapsed at tick 470; the unprotected 25s then killed the player. Sip a restore
  when prayer points fall under 30 (`t.prayer.points()`), eat on the await's `opts.eat`,
  and count the slots: 26 sharks were given to a backpack with room for 21.
- **Re-attack after every eat, dodge or step.** Each one clears the attack; a fight with
  idle gaps of 50 ticks heals the boss (Maiden healed 88 from leaked crabs and 58 from
  pools). Xarpus P2: 13 dodges in 45 ticks produced 4 attack presses and 23 damage --
  stand and fire, and sidestep once on the spit tick (the spit aims at your T-1 tile).
- **Detect your own death.** A respawn at Lumbridge (tile 3222,3218, hitpoints back to
  full) read as "npc_free, boss dead" because the instance was torn down 100 ticks
  after the player left. Break the loop on `t.player.alive()` false or a tile outside the
  instance; `fight.done` must be an `npc_death` row for the boss's slot.
- **Read the boss's footprint from the cache, not from a guess.** Verzik P2 (8372) is
  size 3, south-west 6431,89: tiles 6431..6433 x 89..91. A "walk under and out" at
  Chebyshev distance 2 is neither under nor adjacent. The footprint is `t.npc.state`'s
  tile plus the record's size.
- **Measure a cadence from one seq.** `t.ticklog.gaps(slot, "npc_anim", {seq = <the attack
  seq>})`. Sotetseg's 8139 clock was a flat 5; the 2 and 3 in the ledger were one 8138
  melee mixed in.
- **Leaked crabs are not kills.** A Matomenos arriving at Maiden absorbs its remaining
  hitpoints as an `hit_npc` row with no player hit on that tick; `npc_death` the same
  tick with no player damage is a leak.
- **The driver's return shapes:** `t.msg.last(n)` returns `(status, list)`;
  `t.ticklog.rows(opts)` returns `(ok, list)`; destructure both.
- **Instance coordinates** are the room's region moved by whole 64-tile blocks (for
  region 13122: x - 3136, z + 4160); read tiles from `t.world.tile()` and the tick log,
  never from the wiki's world coordinates.
- **Budget the fight to the whole room.** The flicker rows need wave 16; a run capped at
  115 ticks stops at wave 8. `max_frames` from a measured run.
- **Scope row first.** `spec.scope` with `mode=entry party=1`; the grader prints which
  rows it skips -- measure the rest, all of them.

## A spec row is a measurement, not a restatement (sampler, third ToB room pass)

Two rooms reached full coverage and were sent back because a passing spec row said
something the tick log did not support. The rules the sampler holds a row to:

- **The measured value is computed from log rows named in the detail**, never copied from
  the content's constant or the table. A row whose measured value is a literal that equals
  the spec, or a formula that can only produce the spec, cannot fail and is rejected
  (`xarpus.p3.retaliate_uplift` wrote "40" from any base that fit one hit).
- **Report the whole distribution.** `measured 1,4` is graded on both values; a row that
  passes only because a later instance was ignored is a finding (`sotetseg.melee_hit_delay`).
- **Report a bracket as a bracket.** If the log only narrows a threshold to 19.6-23.1 %,
  write `measured 19.6-23.1`; if that is wider than the tolerance, measure more finely
  (hit on the exact thresholds) rather than writing the spec's figure.
  A threshold row whose table tolerance is `bracket<=N` (a phase trigger as a share of
  the pool: one run can only prove the interval between the reading before the crossing
  hit and the reading after it) is graded as that interval: it passes when
  `measured lo-hi` holds the spec value and is no wider than N. Write both readings and
  the hit in the free text; a single number on such a row fails.
- **The row's expect is the comparison**, never a constant `ok`: `t.check("spec.x", within, detail)`.
- **The scope row is required**: without `spec.scope` the gate grades every row of the table.
- A bring-along may include the spellbook (a setup cheat that sets the spellbook var, with a
  comment naming the spec row that needs it); it is not a raid var.

## A party run: three raiders in one world (raid seam17)

Normal and Hard need a party ("For normal and hard mode, you will need 3 players", owner
2026-10-04). A test file that declares `party = 3,` (or a run given `--party 3`) is run as
three client processes against ONE world:

```sh
python3 tools/raid_gate/run.py _party_smoke --no-build --no-publish   # the three-client smoke
python3 tools/raid_gate/gate.py _party_smoke                           # grades the union
```

- **Seats.** Raider 1, the LEADER, hosts the embedded world and holds its one tick log;
  raiders 2..N are MEMBERS that join it over the party link. Accounts are
  `<base>_p1` .. `<base>_pN` (`base` = the run name, sanitised and cut to 9 characters, so
  every account is at most 12: only the first 12 characters seed a run), password `test`.
- **One file, every client.** Each client runs the same file, wrapped with
  `QD_PARTY = {role, size, names}`. Branch with if/else on `t.party.role()` (1 = leader);
  `t.party.size()`, `t.party.names()`, `t.party.name(n)`.
- **Directories.** `build/quest_gate/<run>/p<n>/` is raider n's session (ledger.tsv, shots/,
  heartbeat, client.log, script/); `<run>/saves/` is the world's saves (every raider's
  fixture is written there before any client starts); `<run>/party.tsv` names the seats;
  `<run>/barrier.<name>.p<n>` are the barrier files. The world's tick log is written in
  `p1/` and copied to every `p<n>/` and to `<run>/` when the run ends.
- **Grading.** `gate.py` grades the UNION, written at `<run>/ledger.tsv` with every shot in
  `<run>/shots/`: the leader's rows keep their names (so its `spec.*` rows are what
  `raid_coverage.py` grades), a member's row is `p<n>:<step>` and its shots `p<n>-<shot>`.
  A raider that left no ledger is a FAIL row `p<n>:run.no_ledger`; duplicate-MD5 is judged
  per raider.
- **The leader's process is the run.** Its stall or exit ends the run. A member whose
  leader is gone stops at its next boundary (raid seam22): its client.log ends
  `net: party: abort: this member's world is gone (the leader's link closed after boundary
  k, tick t) -- a member never runs past its leader`, it exits 1, and run.py writes its
  `run.unfinished` row quoting that line. A member that was still mid-script when the leader
  finished is therefore red: end every raider's script on the same last barrier (the smoke's
  `done`), and every raider then finishes in the same frame, before any further boundary.
- **What a member can do.** Everything a client reads and clicks (ui, chat, msg, inv,
  npcs, locs, `t.party.players`, its own tile and stats). `t.cheat` on a member goes out
  as the client's own typed `::command` packet (setup lines included). `t.tick()` on a
  member (raid seam22) answers the tick its last TICK frame carried, which is what the
  leader's `t.tick()` (srv->tick) reads between the same two boundaries: the smoke's
  `party.tick.*` rows read 190, 199 and 227 on all three raiders. So a member can write
  tick-stamped rows and wait "until tick T"
  (`t.await({level = function() local _, now = t.tick() return now >= T end, note = ...}, n)`).
  The other server readers (`t.ticklog`, `t.var.server`, `t.raid.state`/`leave`/`start_tile`)
  answer `unsupported`. Spec rows are the leader's to write.
- **A raider who dies (raid seam22).** In the Theatre a dead raider is caged and watches
  (tob_spectate.rs2); its client keeps running, and so does its script. On a MEMBER the
  death is the row `player.died` (with its kept shot), written by the first fenced verb that
  sees it -- a click, an attack or cast, an `npc.await_dead*`, a quick press -- or at the
  latest by the next `t.party.barrier`; the run does NOT finish. The row is FAIL unless the
  member called `t.party.allow_death(reason)` before it (then PASS, quoting the reason: a
  death the plan allows, as the smoke's phase D does). After it every fenced verb answers
  `refused` with the death text and `t.player.alive()` answers `refused`; barriers, reads,
  `t.cheat` and `t.finish` work, so branch on `t.player.alive()` and carry the caged raider
  through the remaining barriers to the common end. The Theatre's own line ("You have died.
  Death count: N.", printed in place of "Oh dear, you are dead!") is latched as a death on
  every raider of a party (not in a solo run: there a Theatre death is read only through its
  hitpoints-0 ticks, as before seam22, because the conformance harness dies in a solo Entry
  room on purpose and drives on). The LEADER's death is unchanged: it ends the world and the run (FAIL).
  `party.lockstep` compares every trace up to the leader's last boundary; a dead member's
  trace reaches it like anyone's (its PASS detail adds `p3 died and stayed in step to the
  leader's end`).
- **Sync.** `t.party.barrier(name, ticks)` (every raider writes its file and waits for all;
  the wait is counted in frames and its detail says how many: "Determinism" below),
  and the in-game reads: `t.msg.await("has entered the Theatre of Blood")`,
  `t.party.see(names, radius, ticks)`, the party panel.
- **Lobby verbs** (real clicks, read back): `t.party.form(mode)`, `t.party.apply(leader)`,
  `t.party.accept(name)`, `t.party.ready()`, `t.party.follow_in()`.

### A party room test (raid seam19)

The id is `<raid>_<room>_<mode>` (`tob_maiden_normal`, `tob_maiden_hard`): `raid_coverage.py`
grades it against `<room>.tsv`, and its evidence publishes to
`selftest/minigames/tob/<room>_<mode>/play/` (the id split at its first `_`). The file
declares `party = 3,` (and `fixture = "tob_normal_done.ini"` for Hard), and every raider
calls `t.raid.enter("tob", "<room>", {mode = "<mode>"})` with the same arguments: the leader
lands with `::tobmode`, each member joins the leader's instance with its own typed
`::tobjoinroom <mode>` (read back on its client: the room line, its tile on the leader's
within 5 ticks), and the verb returns on every raider once all three stand at the entrance
(the leader's detail ends `party 3 of 3 in the instance (tobstate party=3 scale=1)`). The
leader writes `spec.scope` (`mode=<mode> party=3`), every `spec.*` row and every tick-ledger
row; a member's part is its own clicks, prayers, steps and eats and a `t.party.barrier` at
each phase. The LEADER crosses the barrier; since seam22 every raider in the room is judged
by its per-tick rules (Maiden's blood, Sotetseg's rag and tornado; tob_raid.rs2
`~tob_arm_party_watchdogs`), so a member's room damage is real and in the leader's tick log as
`hit_player pid n-1`. The worked example is `_party_smoke.lua` phase C (Normal
Bloat: `spec.bloat.hp_3` 1500, `scale=3`, the three orbs at 27 on every raider). In a party
scope `raid_coverage.py` keeps a sidecar `party` row only for the run's mode and size
(`maiden.hp_3` at 3, never `maiden.hp_5`, `maiden.hp_4` or Hard's `maiden.hp_hard_5`).

Knobs (run.py sets them; listed for a hand-run): leader `TORIRS_EMBED_PARTY_LISTEN=<port>`
and `TORIRS_EMBED_PARTY_SIZE=<n incl. leader>` (the first boundary waits for n-1 members);
member `TORIRS_EMBED_PARTY_JOIN=<port>` and `TORIRS_EMBED_PARTY_SEAT=<n>` (seat n logs in
n-th, so pids are stable: see "Pids" below); both `TORIRS_EMBED_PARTY_WAIT_S` (default 60; run.py passes the
environment's value through); `TORIRS_EMBED_PARTY_TRACE=1` (run.py sets it on every party
client since seam21) prints `net: party: boundary k -> tick t digest d` on the leader and on
every member, one format (see "Determinism" below).

**Pids (corrected in raid seam22).** Two numberings, both fixed by the seat:
- the TICK LOG's `pid` (player_tile, hit_player, player_anim, ...) is 0-based: the leader p1
  is pid 0, seat n is pid n-1; a projectile aimed at a player has `target = -(pid+1)`, so -1
  is the leader and -3 is p3 (measured in the smoke's phase D: Bloat's flies at p3 are
  `projectile ... -3 1568`, its hits `hit_player 2`);
- `t.party.players` / `api_drive.players` give the CLIENT's player index, which is the seat:
  p2 reads `_party_sm_p3 pid 3`, `_party_sm_p1 pid 1`.

**What the first Normal trio pass taught about the rooms (raid seam22).** DRIVER_NOTES.md
"Seam pass 22: what the first Normal trio pass found in the rooms" has each recipe. Sotetseg:
the arena raiders walk the path behind the runner, and the first arena step onto the fourth
row spawns the tornado. Xarpus: every spit's splat chains to the other raiders' tiles, so step
off after a teammate's landing too. Nylocas: count the table's aggros, not swaps. Verzik P1,
the owner (2026-10-05): "the players need to take the dawnbringer from the skeleton on the
ground after xarpus. That weapon does not have the shield penalty and the players should
share it using their special attack" -- one sword per raid, dropped for the next raider in
orb order (Strategies :875), everyone behind the pillar at 6426,93 by the bolt's launch (W+3).

**In a fight, press fast.** `t.player.inv_op`, `t.player.equip` wait for the world to go
quiet (3 ticks a press, up to 17 in Nylocas). In a party fight use `t.player.eat`,
`t.player.drink`, `t.player.inv_op(item, op, {quick = true})` and
`t.player.equip(item, {quick = true})`: one press, read back one tick later
(DRIVER_NOTES.md "The fast press"). On a member their "read on tick N" is the lockstep tick
since seam22.

**The Lua budget.** The driver's Lua runs under `PLUGIN_LUA_STEP_BUDGET` = 400000 Lua VM
instructions (src/plugin/torirs_plugin_lua.c:38), re-armed every time the driver resumes the
test's coroutine (`PluginLua_ThreadResume`, torirs_plugin_lua.h): it bounds the script's code
from one yield to the next (an await's predicate runs in the frame callback under a budget of
its own). Past it a count hook
raises `instruction budget exhausted (400000)` and the script dies with a `script-error` row.
Every await, `t.ticks`, shot and click yields; a plain Lua loop does not. A whole-log tick-log
query and a nested loop over it in one stretch is the usual way to hit it (the Normal Maiden
author: every region npc's `npc_spawn` rows, 506 of them, scanned per projectile, run 10).
Let the C side filter (`kind` and `slot` are filtered before a Lua table is built), give
`t.ticklog.rows` an `area` (below) or a `since` serial, index rows by tick once instead of
scanning per row, and put a `t.ticks(1)` between big analysis passes (a yield, so a new
budget).

**`t.ticklog.rows({..., area = {x0, z0, x1, z1[, level]}})`** (raid seam22) keeps a row
whose own tile is inside the box (world tiles, inclusive, either corner first): `x, z` (every
packed `coord` and npc_tile's tile), else `dst_x, dst_z` (a projectile's landing tile). A row
with no tile (hit_player, hit_npc, a mark) is dropped, so combine `area` with `kind`. The
smoke's `ticklog.rows.area`: 1519 npc_spawn rows in the whole log, 3 in Bloat's map square.

**Small readers.** `t.world.spotanims(radius)` and `t.world.projectiles(radius)` take the
radius in TILES (a square: |dx| and |dz| both within it; 0 = every one), so radius 1 drops a
shadow two tiles away. `t.prayer.points()` answers `("ok", reading, detail)` since seam22,
the same shape as `t.skill.read("prayer")` plus `reading.points` (= level, the points left)
and `reading.text`.

Cost: the smoke (three raid entries since seam19, 191 world ticks, three clients) takes
11-13 s of the leader's wall clock (about 60 s for the whole run.py, the script pack check
included). Its leader row `seam.three_clients_one_world` checks lock step over the whole run
(every world tick from the first with three raiders on carries three `player_tile` rows:
`ticks 4..191 (188 ticks)`). Why the world lives in the leader and how the link works:
docs/minigames/raid_loop/DRIVER_NOTES.md, "Three raiders in one run".

### Determinism (raid seam21)

**What is guaranteed.** A party run is a pure function of its test file, its run name (the
accounts, so the seeds: only the first 12 characters of an account name seed a run) and its
binary. Run it again under the same name and the world's tick log is byte-identical, every
raider's `p<n>/ledger.tsv` is identical row for row (step, verdict, ticks, shots and detail),
and every raider's boundary trace is identical. That holds under load: a raider that runs
slower (nice 19 under a busy loop per CPU) only makes the others wait at the boundary. The
machinery, in one paragraph each in DRIVER_NOTES.md "Lockstep, pinned": the world ticks only
when every member's READY is in, and READY carries the member's frame count, which must be
F = 30/k (else the run aborts); `t.party.barrier`'s marks count from the boundary after they
were written, so every raider passes a barrier on the same tick; and every wait in the driver
is counted in frames, never in wall time. `t.party.barrier`'s detail says it:
`party.barrier applied: all 3 raiders, p1 waited 1050 frame(s) (35 tick(s))`, the same number
in every run.

**The one wall-clock field.** Only a `run.unfinished` row (run.py's row for a run that was
killed or stalled) carries seconds in its reason ("no client tick for 91 s"). Every other
detail in a party ledger is frame- or tick-counted; nothing else may be stripped before
comparing.

**The checks.**
- Every party run: run.py sets `TORIRS_EMBED_PARTY_TRACE=1`, and each raider's `client.log`
  carries `net: party: boundary <k> -> tick <t> digest <8 hex>` (the digest is the leader's
  FNV-1a over the tick and every active player's pid, tile, level and hitpoints, then the npc
  count). After the run the union ledger gains a `party.lockstep` row: PASS when every member's
  trace equals the leader's line for line up to the leader's last boundary, FAIL naming the
  first boundary and tick that differs (a member with fewer boundaries than the leader is a
  FAIL too; nothing past the leader's last boundary is compared, raid seam22), and run.py fails
  the run on it. `gate.py` rebuilds the row from the traces whenever it grades the union.
- Across runs: `tools/raid_gate/party_repeat.py` runs the test N times under one run name
  (run.py clears the run directory each time, and the script checks no file in it predates the
  run), keeps each run's tick log, ledgers and traces under `build/raid_repeat/<name>/<label>/`,
  and compares them. Exit 0 only when all agree; otherwise the first difference, under 4 KB.

```sh
python3 tools/raid_gate/party_repeat.py <id> --runs 3 --load        # the gate a party room passes
python3 tools/raid_gate/party_repeat.py <id> --runs 3 --cycles default,1   # the party default against k=1
python3 tools/raid_gate/party_repeat.py --script <scratch.lua> --name <run> --runs 3   # a scratch
```

`--load` runs the last of the N with raider p2 at nice 19 (run.py `QUEST_PARTY_NICE_SEAT=2`)
and one busy-loop process per CPU for the length of that run. `--cycles k[,k...]` sets
`TORIRS_LOGIC_CYCLES_PER_FRAME=k` for every client (N runs per k; `default` leaves it unset)
and compares the first run of each k with the first of the first k. `--allow-red` compares
without requiring run.py to exit 0. `--name` renames a test id's run (run.py `--name` with a
party test id needs `--no-publish`; a renamed run is a scratch and never publishes).

**Before review.** `party_repeat.py <id> --runs 3 --load` exiting 0 is the gate a party room
author runs before handing the room to review, and the reviewer re-runs it. "Equal most of the
time" is a failure: a difference is a defect to find (the first differing boundary names the
tick), never a tolerance to add.

**Knobs.** `TORIRS_LOGIC_CYCLES_PER_FRAME=k` (k divides 30: 1, 2, 3, 5, 6, 10, 15 or 30; it
needs `TORIRS_MAX_FRAMES`, which run.py always sets, and one value for every client of a party:
the link refuses a seat with another k) makes a headless frame pay k logic cycles, so a world
tick is F = 30/k frames. The party default is k = 1, and k = 1 changes nothing. Each k is
deterministic on its own, but k > 1 is not the same run as k = 1 yet: the driver's verbs are
written in frames (a pointer step, a chat page, a per-frame await each cost k cycles), so at
k = 10 the smoke takes 303 boundaries instead of 193 and at k = 30 631, with different ledgers
(measured 2026-10-04: `--cycles 10,30 --runs 2 --allow-red`, identical within each k,
different across). So a room test keeps the default. Wall time on `_party_smoke` (run.py
total, seam21 closer): k=1 10.9 s, k=10 10.5 s, k=30 11.0 s; a tick is cheaper at k>1 but the
frame-granular driver spends more ticks. At k>1 a shot is of the frame's last cycle, and
`TORIRS_SHOT_FRAME=N` is frame N, which is cycle N x k. `QUEST_PARTY_NICE_SEAT=<n>` (run.py)
starts member n at nice 19. `TORIRS_EMBED_PARTY_TRACE=1` is set by run.py.
