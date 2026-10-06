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

## matthew-mbp-m4-camera-b1-seam2 (2026-10-05)

- runner_view_split: LANDED. While a Play runs in a client that presents there are two
  world views: `views[0]` AutomationRunner (every driver verb, its own pointer, pick
  results, menu and photographs) and `views[1]` PlayerClient (presented; the physical
  mouse and keys). `struct App_ViewSplit` (src/app.h) holds the split; attach copies
  views[0] into views[1] (no jump) and detach copies the watcher's view back. The
  driver's events go to their own CmdBus while attached; physical events always steer
  the PlayerClient camera and reach the game only through the plugin chrome or with
  Interact on, never inside a runner gesture. The runner's picks and shots come from an
  OFFSCREEN software frame through views[0], drawn before the presented frame (held poses
  and the scene event queue set aside, its own painter buffer, not counted as drawn, the
  CAM_SHAKE roll shared per loop iteration). CAM_* / cutscenes / CAM_FORCEANGLE take every
  view. The other view's menu is drawn under the presented one in 0x474745 (the chrome's
  FRAME_INSET grey) and takes no physical click. Verbs: t.view.attach / detach / status /
  interact / watcher (api.drive.view_*); QD.core_run_test attaches at Play, QD.finish
  and the on-demand release detach. Lanes: software and OpenGL3 carry it; D3D9, GLES and
  WebGL refuse it with a message (one view, the old behaviour).
- watch_debug_aids: LANDED. Client-drawn (src/app/app_overlay.c "THE WATCHER'S AIDS"):
  the badge top-centre of the world viewport ("Runner has control" / orange "You can
  interact (Scripts: Interact is on)"), the cyan ghost cursor at the runner's pointer with
  a red X on its presses and a label of what the runner's pick holds, and a 1.5 s cyan
  outline, through the watcher's camera, of the npc/loc/obj/player or tile the runner
  pressed from a menu. The Scripts page gains the Interact toggle and the Control, Under
  your pointer and Runner pointer rows. None of it reaches a runner photograph.
- Gates on the final tree (closer, each once): quest suite 115 green + deserttreasure,
  forgettabletale, regicide, troll RED on the same first failing rows (killKamil-engage,
  talkToVeldaban-dialog, goKillGuardAtSecondForest-walk-toForests, player.died) = the
  baseline; conformance 364/364 PASS (190 verbs + 174 seam rows; +5 verbs view.*, +1 seam
  row seam.watch_runner_reading); cooks_assistant and druid byte-identical to
  build/merge17_check; the six ToB Entry rooms green and their ledgers equal to the kept
  ones row for row after the worktree path (xarpus row 13 adds ` party=1 scale=1`, the
  raid.state detail an earlier raid seam added; the kept file predates it);
  party_repeat _party_smoke --runs 3 AGREE (sha 6cf6d25dd6a6, 230 boundaries, = seam27);
  check-quest-verbs, check-drive-abi, check-pt-switch, check-tree-walks, check-scan-meter
  (5 providers), test-plugin-lua, test-quest-cheats, lint (127 files) clean. Cost with no
  script attached: bench A/B +0.6% summed frame p50 vs HEAD (split) and -2.5% (aids vs
  split alone), both noise (fixers' logs in build/seam_state/<pass>/).
- PROVED (state, headless, TORIRS_VIEW_SPLIT_FORCE=1): cooks_assistant with two views
  equals the one-view ledger; with a watcher orbiting and zooming the whole run the
  runner's pose and pointer are identical in all 1691 frames and the ledger (ticks and
  shots) unchanged; tob_maiden two views with orbit = still = one view (46 rows); Interact
  off drops world and inventory clicks, on moves the character and writes watcher.click;
  ::cam moves both views to the same eye; the badge, ghost cursor and press outline by
  the per-frame trace (TORIRS_WATCH_TRACE).
- NOT PROVED: Play from the Scripts tab itself (every proof attached through
  t.view.attach in a copied test); held physical presses during a runner gesture, hover
  following the last pointer, chrome clicks with Interact off and typing with Interact on
  were never exercised; the OpenGL3 lane was built, not run (no GL context under SDL
  dummy: the offscreen software frame drops whatever load events it queued itself, and a
  GL3 model with no bind pose may take one extra pose on a frame that also drew
  offscreen); the Scripts page rows were proved in a plain-Lua harness, not live; the
  page cannot show what the runner's pick holds (no plugin read of views[0]'s pickset;
  the ghost label has it).
- visual_checks_for_later: see CAMERA_RESUME.md.
