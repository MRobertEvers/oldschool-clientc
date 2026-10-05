# Camera loop: seam ledger

One heading per camera pass, one line per seam: the key, whether it landed, and
what remains open. The camera loop (the runner camera split, design
https://claude.ai/artifact/FDWBxLAVmDPdt5PxR2erQu) keeps its own ledger so it
never edits the raid orchestrator's SEAM_LEDGER.md. Where to resume is
CAMERA_RESUME.md, which the camera orchestrator keeps.

## matthew-mbp-m4-camera-b1-seam1 (2026-10-05)

- world_view_gather: LANDED, a pure refactor. `struct App_WorldView` (src/app.h)
  holds the 25 view fields (camera pose, orbit, zoom, reload hold, camera input
  latches, world pointer, pick results) plus a `minimenu` pointer; struct App
  has `views[2]`, `view_count` 1 and `frame_view` = `&views[0]`, set by
  `App_WorldViewsInit` (static inline in app.h; App_Init calls it after its
  memset, and a test that builds struct App by hand must call it too). All 558
  sites go through `app->frame_view->`. Proof on the final tree: cooks_assistant
  and druid ledgers byte-identical to build/merge17_check; the six ToB Entry
  rooms' ledgers and tick logs byte-identical to the HEAD baseline (maiden 112,
  bloat 94, nylocas 177, sotetseg 158, xarpus 120, verzik 259 PASS);
  _party_smoke green; party_repeat --runs 3 AGREE at tick log sha 2883b385659d;
  bench frame p50 within noise (median ratio 0.95), draw command counts identical
  on all 24 runs; quest suite 115 green, deserttreasure, forgettabletale, regicide
  and troll RED (the baseline); conformance 353/353. Built: torirs (OPT and
  OPT=0), questtest, scanmeter, gfmatrix, contenttest, torirsserver, maped,
  mapedctl, win64 (D3D9), win32, web, android armv7. NOT built on this machine:
  linux and iOS (no file there names a gathered field). Writer/reader table:
  CAMERA_VIEW_TABLE.md.
  Open for runner_view_split: the minimenu STORAGE is still
  `interact.minimenu` in struct UIInteraction (views[1] needs its own, and the
  UI interaction step must be told which menu it drives; never memcpy views[0]
  without repointing `minimenu`); the 7 UNCLASSIFIED rows in
  CAMERA_VIEW_TABLE.md (app_logic_tick's audio listener / CS2 hover / menu_open,
  the CS2 camera-angle mirror, CAM_MOVETO seeding the shared cam_script, and the
  four plugin-bridge scratch-menu swaps that need the caller's view).
  Pre-existing at HEAD and unchanged: test-chat-store link error
  (_App_DriveEvent), test-entity-info-shrink segfault, test-wev-rebuild's raft
  case exit 139 when ../cache.osrs239 is present. The quest gate needs
  QUEST_HELPER_ROOT=/Users/matthewevers/Documents/git_repos/quest-helper in this
  nested worktree; without it zombiequeen reads RED on cutscene_exempt_refused.
