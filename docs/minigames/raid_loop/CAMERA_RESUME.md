# Camera orchestrator: resume here

The runner camera split (design: https://claude.ai/artifact/FDWBxLAVmDPdt5PxR2erQu,
work order: SEAM_TRIAGE_2026-10-05e.md). Branch `matthew-mbp-m4-camera-b1` in both
repos, worktree `build/orchestrator/worktrees/camera`, cut from
`origin/matthew-mbp-m4-raid-b1` at e502128c9 (OSRS-Content fb292a9996). This branch
reaches the raid branch only by a merge the raid orchestrator makes.

## State (2026-10-05)

| Seam | Pass | State |
|---|---|---|
| world_view_gather | matthew-mbp-m4-camera-b1-seam1 | LANDED 325c57690 (pushed; gates in CAMERA_LEDGER.md) |
| runner_view_split | seam2 (planned) | waits for raid seam24 + seam25 |
| watch_debug_aids | seam3 (planned) | after the split |

## How to relaunch a pass

Workflow tool, `scriptPath` = `<camera>/tools/raid_gate/workflows/raid_seam.workflow.js`,
args `{pass, branch: "matthew-mbp-m4-camera-b1", worktree: "<camera abs path>",
reuse_triage: "docs/minigames/raid_loop/CAMERA_TRIAGE_seam<N>.md", width: 1, context}`.
Relaunch with the SAME args to resume; state is in `<camera>/build/seam_state/<pass>/`.
Never resumeFromRunId. The seam1 triage holds only world_view_gather (the card has
no stop-after option, so each pass gets a one-seam triage file).

## seam1 notes for the next seams

- Writer/reader table: `CAMERA_VIEW_TABLE.md`. 7 UNCLASSIFIED rows the split must
  decide (audio listener, CS2 hover coordinate, clientop menu_open, CAM_GETANGLE/GETYAW
  mirror, CAM_MOVETO seeding cam_script, four plugin-bridge scratch-menu swaps).
- The minimenu storage is still `interact.minimenu` in struct UIInteraction; the view
  holds a pointer. views[1] needs its own menu; never memcpy views[0] without repointing.
- `tools/appc_map_baseline.txt` app.c-lines raised 1328 -> 1331: the raid branch was
  already at 1330 (red) at e502128c9; +1 is the App_WorldViewsInit call.
- Pre-existing at the base, not ours: test-chat-store link error (_App_DriveEvent),
  test-entity-info-shrink segfault, test-wev-rebuild raft case exits 139 with the cache.
- In this nested worktree gate.py and lint_quest need
  `QUEST_HELPER_ROOT=/Users/matthewevers/Documents/git_repos/quest-helper`, or zombiequeen
  reads RED.
- The quest suite does not finish inside one 10-minute shell call: the closer drove it
  as detached single-quest runs (build/camera_close/suite_driver.py).
- 325c57690 carries an Opus 5.5 trailer instead of the required Fable 5.1 (pushed; not
  amended). Tell every closer the trailer explicitly overrides the session default.
- Raid f8bcaa9d1 (owner, seam25): only the watched client turns the camera, headless
  snaps, one driver call owns every camera move. Read it before triaging the split.

## Next steps

1. (done) seam1 landed.
2. Watch `docs/minigames/raid_loop/SEAM_LEDGER.md` on origin/matthew-mbp-m4-raid-b1 for
   sections for seam24 (Scripts tab lists every script) and seam25 (watched mouse
   mapping, TORIRS_SIM_SDL_CLICK_AT, ::resetcharacter).
3. Then merge that branch in (parent and OSRS-Content, merge commits, conflicts by
   hand), re-run seam1's proofs, write CAMERA_TRIAGE_seam2.md (runner_view_split,
   paths rewritten to this worktree) and launch; then seam3 for watch_debug_aids.

## Baselines

- `build/merge17_check/` copied read-only from the raid worktree (cooks/druid before.tsv).
- Quest suite baseline: 115 green; deserttreasure, forgettabletale, regicide, troll red.
