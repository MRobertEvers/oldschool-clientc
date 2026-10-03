# What the waves loop copied from the raid branch

`docs/WAVES_ORCHESTRATOR.md` section 2 allows one verbatim copy of the raid loop's
tooling as a starting point. Every copy is listed here with the raid commit it came
from. After the copy the file is the waves loop's own; later changes are reconciled
by whichever branch reaches `v3` second. Nothing was cherry-picked or merged.

Raid branch: `origin/matthew-mbp-m4-raid-b1`. Its merge base with `v3` is `80b58e323`.

| Date | Raid commit | Raid path | Waves path | Kind | Changed since the copy |
|---|---|---|---|---|---|
| 2026-10-03 | `94f55b306` | `docs/WAVES_ORCHESTRATOR.md` | `docs/WAVES_ORCHESTRATOR.md` | spec, in place | no |
| 2026-10-03 | `94f55b306` | `tools/toa_fetch_wiki.py` | `tools/toa_fetch_wiki.py` | tool, in place (the pin guard: a different text lands as `.rev<revid>`); `v3` had not touched the file since the merge base | no |
| 2026-10-03 | `94f55b306` | `tools/raid_gate/spec_check.py` | `tools/waves_gate/spec_check.py` | tool, renamed directory | no |
| 2026-10-03 | `94f55b306` | `tools/raid_gate/raid_coverage.py` | `tools/waves_gate/waves_coverage.py` | tool, renamed | no |
| 2026-10-03 | `94f55b306` | `tools/raid_gate/frame_count.py` | `tools/waves_gate/frame_count.py` | tool, renamed directory | no |
| 2026-10-03 | `94f55b306` | `tools/raid_gate/vtt_to_md.py` | `tools/waves_gate/vtt_to_md.py` | tool, renamed directory | no |

The copied tools still name `tools/raid_gate/` and raid test ids in their docstrings
and paths. Adapting them to `tools/waves_gate/`, `test/waves/` and the
`docs/minigames/inferno|colosseum/` layout is a row of the driver seam (section 5,
"tests directory"); when a file is changed, set its last column to the commit.

Driver and engine files (the prayer verb, npc state and hazard reads, the tick log,
the step-on-tick verb, the fast attack press, the tests-directory override) are not
copied yet. They are rows of the driver seam, and each copy is added to this table by
the fixer that makes it.

The three workflow cards under `tools/waves_gate/workflows/` are written from the
raid cards' shape (`tools/raid_gate/workflows/raid_{spec,seam,author}.workflow.js` at
`94f55b306`) with the worktree path, the nouns and the phases changed; they are not
verbatim copies.
