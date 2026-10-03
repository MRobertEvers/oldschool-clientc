# Wave minigame tests

One file per encounter unit of a wave minigame: `test/waves/<game>_<unit>.lua`
(`inferno_nibblers`, `inferno_jad`, `inferno_zuk`, `colosseum_sol_heredit`; the
whole run is `<game>_full`), returning a table. The shape is a quest test's
exactly -- `{ id, fixture, setup = {cheats}, run = function(t) ... end }` or a
relay's `legs = {...}` (`test/quests/README.md`, `docs/QUEST_AUTHORING.md`) --
because a wave test IS a quest-driver test with a tick ledger
(`docs/WAVES_ORCHESTRATOR.md` section 6). The verbs are the driver's
(`tools/quest_gate/verb_list.py`); the fight verbs are `t.prayer.*`,
`t.ticklog.*`, `t.tick`, `t.npc.state / state_text / await_anim / await_face`,
`t.world.spotanims / projectiles / hazard_at`, `t.player.step_tick` and the fast
press (`t.player.attack` / `t.player.cast` with `ticks <= 2`); their notes are
`docs/minigames/waves_loop/DRIVER_NOTES.md`. `t.wave.*` (`enter`, `state`) is
`script/plugins/quest_driver/waves.lua`, empty until waves seam pass 2.

Written from the raid loop's `test/raids/README.md` at `94f55b306`
(`docs/minigames/waves_loop/FORKED_FROM.md`).

## Why not test/quests/

`tools/quest_gate/quest_list.py` discovers every `test/quests/*.lua`, so a wave
test there would be run by the quest loop's `run.py --all`, `gate.py --all` and
`make test-quests` on every machine, and its verdict would land in that loop's
gates. The waves loop is independent of the quest loop (section 2): no
`QUEUE.tsv` row, no claim, no `docs/quest_authoring/` edit.

## Running

```sh
python3 tools/waves_gate/run.py inferno_nibblers --no-build --no-publish   # one unit
python3 tools/waves_gate/run.py --all --jobs 2 --no-build                  # every test/waves/*.lua
python3 tools/waves_gate/gate.py inferno_nibblers                          # its gate + coverage
python3 tools/quest_gate/lint_quest.py test/waves/inferno_nibblers.lua
```

The wrappers take `tools/quest_gate/run.py` / `gate.py`'s arguments unchanged
(`--from-leg K`, `--script`, `--name`, `--no-build`, `QUEST_BINARY=...`); they
set `TORIRS_QUEST_TESTS_DIR=test/waves` and `TORIRS_QUEST_PUBLISH_DIR` and run
the quest gate script (`tools/waves_gate/README.md`, which also says how a
measurement scratch is run with no build and nothing published).

- Fixtures come from `test/waves/fixtures/` (seeded with `fresh_lumbridge.ini`).
- Artefacts land in `build/quest_gate/<id>/` like a quest's, `ticklog.tsv`
  beside the ledger once a test starts the tick log. That directory, the
  session lock (`build/quest_gate/.locks/<id>.lock`) and a relay's checkpoints
  are keyed by id alone, so **a wave id must never equal a quest id**, and it
  must start with its game: the wrappers refuse (exit 2) otherwise.
- A PASS is published to
  `OSRS-Content/osrs239-content/server/scripts/selftest/minigames/<game>/<unit>/play/`
  (`inferno_nibblers` -> `inferno/nibblers`: the id split at its first `_`), not
  under `selftest/quests/`. `--no-publish` skips it.
- `gate.py`'s Quest Helper coverage check reads "not graded" for a wave id (no
  `QUEUE.tsv` row, no guide) -- right: a unit's coverage is its encounter spec
  table, graded by `tools/waves_gate/waves_coverage.py`, which `gate.py` runs
  after the quest gate.
- Names starting with `_` are not tests (the quest suite's rule): a harness
  file may use it.

## Rules a wave test is held to (WAVES_ORCHESTRATOR.md section 6)

Every rule of `test/quests/README.md` (no numeric interface ids, every step a
ledger row, a shot per interaction, deadlines in server ticks) plus three kinds
of row, all required:

1. **The fight, driven.** Every kill is the player's own attacks; every mechanic
   is met by a real move built from the driver's verbs: the prayer switched on
   the tick, the tile stepped, the pillar stood behind, the supply drunk.
   Setup takes bring-alongs only (`::setlevel`, `::give`, a spellbook).
   `t.wave.enter` (seam pass 2) is the bring-along that places the player at the
   unit's first wave; the full-run test uses it only for wave 1. Never a god
   mode, a kill cheat, a heal, a wave skip, a teleport past a phase, or a
   debugproc that performs a mechanic.
2. **The technique rows.** Each published technique is a row proved from the
   tick log: the prayer was up on the tick the hit was rolled; no hit landed
   while the pillar stood between; the blob's style was read from its scan
   tick; the shield was followed. A kill row alone proves nothing.
3. **The tick ledger.** From `t.ticklog.rows()` / `t.ticklog.gaps()`, a
   `t.check("spec.scope", true, "mode=<m> party=<n>")` row first, then one PASS
   row per in-scope spec-table mechanic, named `spec.<mechanic_id>`, whose detail
   starts `measured <value>[ <unit>][, <free text>] (spec <value>[ <unit>], grade <G>, tol <tolerance>)`:
   `measured 4 ticks, 16 of 16 gaps (spec 4 ticks, grade B, tol exact)`;
   `measured 9 (spec 9, grade B, tol +-1)`; a grade E row ->
   `measured 3 (spec 1-5, grade E, tol approx); approximation, M40`. A measured
   comma list is the distribution (every instance must meet the tolerance). The
   spec value, grade and tolerance are copied from the unit's table
   (`docs/minigames/<game>/encounters/<unit>.tsv`); the grader skips, and lists,
   rows its `<unit>.scope.tsv` sidecar marks for another mode, another party
   size or `stat`. **Text rows** (unit `text`) read
   `measured <text> (spec <text>, grade <G>, tol exact)` and match by equality.
   The measured free text may carry parentheses; the `(spec ...)` group is the
   last one.

### Cheats inside `run`

Setup takes bring-alongs only. Inside `run` the only cheats a wave test may issue
are READ-ONLY state readouts the content provides as instruments (procs whose
body only `mes`es what it reads), used to corroborate a ledger row, never to
replace a measurement from the tick log. Anything that changes the world
(`::kill`, `::godmode`, `::setvar`, a heal, a wave skip, `::zukhp`,
`::infernopause` used as a mechanic) is a rejection.

An npc acting on tick T sees the world as it stood at the end of tick T-1
(`docs/minigames/theater_of_blood/ENCOUNTER_TIMING.md` section 1): a step that
must dodge an attack lands the tick BEFORE the scan, and a prayer pressed
between server ticks T-1 and T is in force for tick T's npc phase. Loop on the
monsters' state, never on a fixed tick count (`TORIRS_EMBED_CLOCK_MS=20` fixes
the clock). Set `max_frames` per leg from a measured run, under
`quest_list.MAX_FRAMES_CEILING`.

## Lessons the raid loop paid for (WAVES_ORCHESTRATOR.md section 10, lesson 15)

- **Prayer and food run out.** Sip a restore before prayer runs out
  (`t.prayer.points()`), eat on the kill wait's `opts.eat`, and count the
  backpack's slots before giving supplies.
- **Re-attack after every eat, dodge or step.** Each one clears the attack.
- **Detect your own death.** A respawn reads as "the npc is gone". Break the
  loop on `t.player.alive()` false or a tile outside the arena; a kill is an
  `npc_death` row for that slot.
- **Read an npc's footprint from its record's size** (`t.npc.state` rows carry
  `size`), never from a guess.
- **Measure a cadence from one sequence id.**
  `t.ticklog.gaps(slot, "npc_anim", {seq = <the attack seq>})`; translate the
  client slot first with `t.ticklog.slot(row)` (the log keys by the server's slot).
- **Destructure the driver's two return values**: `t.msg.last(n)` and
  `t.ticklog.rows(opts)` return `(status, list)`; `t.ticklog.gaps` returns
  `(status, text, gaps, ticks)`.
- **Never press into a menu an earlier press left open.**
- **A spec row is a measurement, not a restatement**: the measured value is
  computed from log rows named in the detail; report the whole distribution and
  a bracket as a bracket; the row's expect is the comparison.
