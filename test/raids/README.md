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
- **The leader's process is the run.** Its stall or exit ends the run; the members then
  get 20 s to finish their own script before they are killed, and an unfinished member
  ledger gets its SUMMARY like any unfinished run.
- **What a member can do.** Everything a client reads and clicks (ui, chat, msg, inv,
  npcs, locs, `t.party.players`, its own tile and stats). `t.cheat` on a member goes out
  as the client's own typed `::command` packet (setup lines included); the server readers
  (`t.tick`, `t.ticklog`, `t.var.server`, `t.raid.state`/`leave`/`start_tile`) answer
  `unsupported`. Spec rows are the leader's to write.
- **Sync.** `t.party.barrier(name, ticks)` (every raider writes its file and waits for all),
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
each phase. The LEADER crosses the barrier (only the crosser runs the room's per-tick
watchdog: CONTENT_BUGS.md, seam19). The worked example is `_party_smoke.lua` phase C (Normal
Bloat: `spec.bloat.hp_3` 1500, `scale=3`, the three orbs at 27 on every raider). In a party
scope `raid_coverage.py` keeps a sidecar `party` row only for the run's mode and size
(`maiden.hp_3` at 3, never `maiden.hp_5`, `maiden.hp_4` or Hard's `maiden.hp_hard_5`).

Knobs (run.py sets them; listed for a hand-run): leader `TORIRS_EMBED_PARTY_LISTEN=<port>`
and `TORIRS_EMBED_PARTY_SIZE=<n incl. leader>` (the first boundary waits for n-1 members);
member `TORIRS_EMBED_PARTY_JOIN=<port>` and `TORIRS_EMBED_PARTY_SEAT=<n>` (seat n logs in
n-th, so pids are stable); both `TORIRS_EMBED_PARTY_WAIT_S` (default 60; run.py passes the
environment's value through); member `TORIRS_EMBED_PARTY_TRACE=1` prints
`net: party: boundary k -> server tick T`.

Cost: the smoke (three raid entries since seam19, about 183 world ticks, three clients)
takes 12-14 s of the leader's wall clock (about 60 s for the whole run.py, the script pack
check included). Its tick logs are byte-identical between most runs; a member's typed
`::command` lands at the boundary its own frame timing reaches, so one run in three was
measured one tick later from phase B (seam19: runs 1 and 3 equal, run 2 shifted at tick 116). Its leader row `seam.three_clients_one_world`
checks lock step over the whole run (every world tick from the first with three raiders on
carries three `player_tile` rows: `ticks 4..166 (163 ticks)` on the closer's run). Why the
world lives in the leader and how the link works: docs/minigames/raid_loop/DRIVER_NOTES.md,
"Three raiders in one run".
