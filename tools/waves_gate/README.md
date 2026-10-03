# tools/waves_gate

Tools of the waves loop (`docs/WAVES_ORCHESTRATOR.md`): the Inferno, then the
Fortis Colosseum. The wave tests are quest-driver tests run by the quest gate's
own scripts; this directory holds the thin layer that points those scripts at
`test/waves/` instead of `test/quests/`, plus the loop's own checkers. Each file
copied from the raid loop's `tools/raid_gate/` is listed, with the raid commit,
in `docs/minigames/waves_loop/FORKED_FROM.md`.

| File | What it does |
|---|---|
| `run.py` | `tools/quest_gate/run.py` over `test/waves/`: same argv, output and exit code |
| `gate.py` | `tools/quest_gate/gate.py` over `test/waves/`, then `waves_coverage.py` on every id named |
| `suite.py` | the shared part: the two paths, the id refusals, the exec |
| `spec_check.py` | validates an encounter spec table (`docs/minigames/<game>/encounters/<unit>.tsv`, section 6) |
| `waves_coverage.py` | grades a test's `spec.<mechanic_id>` ledger rows against its unit's table: FULL or findings |
| `frame_count.py` | dumps a video section's frames for a frame count (section 9) |
| `vtt_to_md.py` | turns a yt-dlp auto-caption `.vtt` into a transcript markdown (`--game` or `--raid` names the corpus) |
| `verify_blert.py` | the corpus pass's Blert reader (not part of the driver seam) |

## How the wrappers work

`tools/quest_gate/quest_list.py` reads two environment variables; unset (or
empty) they change nothing, byte for byte:

- `TORIRS_QUEST_TESTS_DIR` -- the suite directory. `quest_list.quests_dir`
  (and through it `discover`, `quest_path`, `fixtures_dir`) uses it, so
  run.py's `--all`, a single id, `--from-leg`, and the session fixture
  (`<dir>/fixtures/<name>.ini`) all follow it, and so does gate.py.
- `TORIRS_QUEST_PUBLISH_DIR` -- where a PASS is copied. When set, run.py's
  `quest_dir_for` does not consult `QUEUE.tsv`; the subdirectory is the id
  split at its first `_` (`inferno_nibblers` -> `inferno/nibblers`,
  `colosseum_sol_heredit` -> `colosseum/sol_heredit`).

The wrappers set them to `test/waves` and
`OSRS-Content/osrs239-content/server/scripts/selftest/minigames` and run the
quest gate script. Before that they refuse (exit 2):

- if any `test/waves/<id>.lua` shares an id with `test/quests/` or `QUEUE.tsv`'s
  `test_id` column: `build/quest_gate/<id>/`, the session lock and the relay
  checkpoints are keyed by id alone;
- if any `test/waves/<id>.lua` is not named `<game>_<unit>` with `<game>` one of
  `inferno`, `colosseum`: the publish path and the coverage grader read the game
  from the id.

`gate.py` then runs `waves_coverage.py` on the ids it was given: a unit is green
only when the quest gate is green AND coverage reads FULL. The full-run test is
`<game>_full`; its coverage is every unit table of its game.

Nothing here writes to `test/quests/`, `QUEUE.tsv`, claims, or
`docs/quest_authoring/`; `QUEUE.tsv` is read only, for the collision check.

## A measurement scratch: no build, nothing published

A spec worker's fought measurement is a scratch Lua file run once, outside the
suite (it is never a file under `test/waves/`):

```sh
python3 tools/quest_gate/run.py --script <path/to/scratch.lua> --name <unique label> --no-build --no-publish
```

- `--no-build` uses the client already built (`src/torirs_questtest`); a Lua
  edit needs no rebuild, an OSRS-Content edit needs `make -C src torirsserver-scripts`.
- `--no-publish` copies nothing into OSRS-Content.
- `--name` names the artefact directory `build/quest_gate/<name>/` (ledger,
  shots, `ticklog.tsv`, client log) and the session user; run.py refuses a
  second concurrent run of one name. Run it in the foreground. A run that may
  exceed the shell's limit: `--detach`, then `run.py --wait <name>` until it
  exits 0 or 1 (3 = still running).
- The fixture comes from `test/quests/fixtures/` when run through
  `tools/quest_gate/run.py` and from `test/waves/fixtures/` when run through
  `tools/waves_gate/run.py` (identical today).
