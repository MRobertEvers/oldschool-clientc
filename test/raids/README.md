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
