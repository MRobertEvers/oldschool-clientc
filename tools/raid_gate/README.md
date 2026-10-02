# tools/raid_gate

Tools of the raid loop (`docs/RAID_ORCHESTRATOR.md`). The raid room tests are
quest-driver tests run by the quest gate's own scripts; this directory holds
the thin layer that points those scripts at `test/raids/` instead of
`test/quests/`, plus the raid lane's own checkers.

| File | What it does |
|---|---|
| `run.py` | `tools/quest_gate/run.py` over `test/raids/`: same argv, output and exit code |
| `gate.py` | `tools/quest_gate/gate.py` over `test/raids/` |
| `suite.py` | the shared part: the two paths, the id-collision refusal, the exec |
| `spec_check.py` | validates an encounter spec table (section 3) |

## How the wrappers work

`tools/quest_gate/quest_list.py` reads two environment variables; unset (or
empty) they change nothing, byte for byte:

- `TORIRS_QUEST_TESTS_DIR` -- the suite directory. `quest_list.quests_dir`
  (and through it `discover`, `quest_path`, `fixtures_dir`) uses it, so
  run.py's `--all`, a single id, `--from-leg`, and the session fixture
  (`<dir>/fixtures/<name>.ini`) all follow it, and so does gate.py.
- `TORIRS_QUEST_PUBLISH_DIR` -- where a PASS is copied. When set, run.py's
  `quest_dir_for` does not consult `QUEUE.tsv`; the subdirectory is the id
  split at its first `_` (`tob_maiden` -> `tob/maiden`).

The wrappers set them to `test/raids` and
`OSRS-Content/osrs239-content/server/scripts/selftest/minigames` and
`os.execv` the quest gate script. Before that they refuse (exit 2) if any
`test/raids/<id>.lua` shares an id with `test/quests/` or `QUEUE.tsv`'s
`test_id` column: `build/quest_gate/<id>/`, the session lock and the relay
checkpoints are keyed by id alone.

Nothing here writes to `test/quests/`, `QUEUE.tsv`, claims, or
`docs/quest_authoring/`; `QUEUE.tsv` is read only, for the collision check.
