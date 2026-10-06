# Camera orchestrator: resume here

The runner camera split (design: https://claude.ai/artifact/FDWBxLAVmDPdt5PxR2erQu,
work order: SEAM_TRIAGE_2026-10-05e.md). Branch `matthew-mbp-m4-camera-b1` in both
repos, worktree `build/orchestrator/worktrees/camera`, cut from
`origin/matthew-mbp-m4-raid-b1` at e502128c9 (OSRS-Content fb292a9996). This branch
reaches the raid branch only by a merge the raid orchestrator makes.

## State (2026-10-05)

| Seam | Pass | State |
|---|---|---|
| world_view_gather | matthew-mbp-m4-camera-b1-seam1 | LANDED 325c57690 (gates in CAMERA_LEDGER.md); on origin/matthew-mbp-m4-raid-b1 via cd6eae16d (owner asked, 2026-10-05) |
| runner_view_split | matthew-mbp-m4-camera-b1-seam2 | LANDED (gates and open items in CAMERA_LEDGER.md) |
| watch_debug_aids | matthew-mbp-m4-camera-b1-seam2 (run in the same pass as the split) | LANDED |

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

## How the owner tries it

`./launch run osrs239-scripts`, open the Scripts tab, pick a script (cooks_assistant is
the quickest), press Play, then move your own camera the whole time it runs: arrow keys,
middle-drag, wheel. The badge at the top of the game view says "Runner has control"; the
cyan ghost cursor is the script's pointer. Flip **Interact** on the Scripts page to play
yourself (the badge turns orange, each action becomes a `watcher.*` ledger row) and off
again. If the client comes up on D3D9, GLES or WebGL, the page's Control row says the lane
keeps one view and the script moves your camera as before.

## visual_checks_for_later (state proved headless; these need eyes)

- Play cooks_assistant and orbit/zoom the whole time: your picture never jumps, flickers
  or follows the runner's re-aims (photographs included).
- The dark "Runner has control" badge sits top-centre over interfaces at the fixed,
  resizable and fullscreen sizes; Interact on turns it orange "You can interact (Scripts:
  Interact is on)" and back.
- The cyan ghost cursor tracks the runner's presses (also over the inventory and chat),
  shows a red X on each press and a label of what the runner's pick holds; it starts at
  the top-left corner until the runner's first move (cosmetic, known).
- Talk-to/Attack by the runner: the npc gets a cyan outline in YOUR view (orbit away first)
  for about 1.5 s; a Walk here outlines the tile.
- A runner right-click while yours is closed: its menu is grey (0x474745); with Interact on,
  your own menu draws brown on top of it.
- A runner screenshot (build/quest_gate/watch/<account>/shots/) shows no badge, ghost
  cursor or cyan outline, and its overlays (health bars, names) sit on the models in the
  runner's camera.
- Interact on: a walk-here click moves you and the ledger gains watcher.click; Interact
  off: inventory and world clicks do nothing while the Scripts panel still works.
- The Scripts page rows: Under your pointer follows your hover (npc name and id, loc, obj,
  bare tile); Runner pointer updates every 10 frames.
- On the OpenGL3 lane, if the profile comes up on it: all of the above, plus no missing
  models or flicker on frames where the runner picks.

## seam2 notes for the next seams

- Raid seam25 (worktrees/raid25) edits DrivePointer_Camera and pointer.lua's pose callers;
  seam2 left both bodies alone (only `_shot_plan`'s watched-client guard changed: it now
  applies only when no view of its own is attached). The driver's view is chosen by the
  `frame_view == views[0]` invariant asserted in lua_drive_pump. Merge by hand.
- Open: the Scripts page cannot show what the runner's pick holds (add the first world hit
  to `drive_push_view`, torirs_plugin_drive.c, about ten lines); a press outline follows
  menu presses only (a drive.op bypass or a minimap walk draws none).
- Picture-in-picture of the runner's view: the owner said "Later".

## Next steps

0. Owner, 2026-10-05: "merge everything" was scoped to the raid branch family: the camera
   branch was pushed to origin/matthew-mbp-m4-raid-b1 (fast-forward to cd6eae16d) and
   raid-watch checked out there. v3, waves, b63 and the other worktrees get it with the
   raid branch's PR (it carries 241 raid commits).

1. (done) seam1 landed. (done) seam2 landed both runner_view_split and watch_debug_aids.
2. Watch `docs/minigames/raid_loop/SEAM_LEDGER.md` on origin/matthew-mbp-m4-raid-b1 for
   sections for seam24 (Scripts tab lists every script) and seam25 (watched mouse
   mapping, TORIRS_SIM_SDL_CLICK_AT, ::resetcharacter).
3. (done) Then merge that branch in (parent and OSRS-Content, merge commits, conflicts by
   hand), re-run seam1's proofs, write CAMERA_TRIAGE_seam2.md (runner_view_split,
   paths rewritten to this worktree) and launch; then seam3 for watch_debug_aids.
4. The raid orchestrator merges matthew-mbp-m4-camera-b1 into the raid branch (seam25 by
   hand, see the seam2 notes above); then the owner's visual checks.

## Baselines

- `build/merge17_check/` copied read-only from the raid worktree (cooks/druid before.tsv).
- Quest suite baseline: 115 green; deserttreasure, forgettabletale, regicide, troll red.
