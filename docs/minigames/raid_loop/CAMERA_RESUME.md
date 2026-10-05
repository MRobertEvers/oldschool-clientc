# Camera orchestrator: resume here

The runner camera split (design: https://claude.ai/artifact/FDWBxLAVmDPdt5PxR2erQu,
work order: SEAM_TRIAGE_2026-10-05e.md). Branch `matthew-mbp-m4-camera-b1` in both
repos, worktree `build/orchestrator/worktrees/camera`, cut from
`origin/matthew-mbp-m4-raid-b1` at e502128c9 (OSRS-Content fb292a9996). This branch
reaches the raid branch only by a merge the raid orchestrator makes.

## State (2026-10-05)

| Seam | Pass | State |
|---|---|---|
| world_view_gather | matthew-mbp-m4-camera-b1-seam1 | launched |
| runner_view_split | seam2 (planned) | waits for raid seam24 + seam25 |
| watch_debug_aids | seam3 (planned) | after the split |

## How to relaunch a pass

Workflow tool, `scriptPath` = `<camera>/tools/raid_gate/workflows/raid_seam.workflow.js`,
args `{pass, branch: "matthew-mbp-m4-camera-b1", worktree: "<camera abs path>",
reuse_triage: "docs/minigames/raid_loop/CAMERA_TRIAGE_seam<N>.md", width: 1, context}`.
Relaunch with the SAME args to resume; state is in `<camera>/build/seam_state/<pass>/`.
Never resumeFromRunId. The seam1 triage holds only world_view_gather (the card has
no stop-after option, so each pass gets a one-seam triage file).

## Next steps

1. Land seam1 (world_view_gather); report commits and gates.
2. Watch `docs/minigames/raid_loop/SEAM_LEDGER.md` on origin/matthew-mbp-m4-raid-b1 for
   sections for seam24 (Scripts tab lists every script) and seam25 (watched mouse
   mapping, TORIRS_SIM_SDL_CLICK_AT, ::resetcharacter).
3. Then merge that branch in (parent and OSRS-Content, merge commits, conflicts by
   hand), re-run seam1's proofs, write CAMERA_TRIAGE_seam2.md (runner_view_split,
   paths rewritten to this worktree) and launch; then seam3 for watch_debug_aids.

## Baselines

- `build/merge17_check/` copied read-only from the raid worktree (cooks/druid before.tsv).
- Quest suite baseline: 115 green; deserttreasure, forgettabletale, regicide, troll red.
