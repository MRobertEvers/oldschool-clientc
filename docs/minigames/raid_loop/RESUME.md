# Raid loop: resume here

For a FRESH session taking over the raid orchestrator (a long session's chat panel crashes the
editor: hand over every few hours; everything below is on disk). Worktree
`build/orchestrator/worktrees/raid`, branch `matthew-mbp-m4-raid-b1` in both repos, pushed.
The raid loop never touches the quest loop or v3. Read, in this order: this file,
`README.md`, the last section of `SEAM_LEDGER.md`, the last section of `ROOM_LEDGER.md`,
`MERGE_CHECKLIST.md`.

## How to run a pass

Cards under `tools/raid_gate/workflows/` (Workflow tool, `scriptPath`): `raid_seam` (args
`pass`, `reuse_triage`, `width`, `context`), `raid_author` (args `pass`, `raid`, `rooms`,
`mode`, `width`, `context`), `raid_spec`, `raid_rig`. State is under `build/<kind>_state/<pass>/`;
relaunch with the SAME pass name to resume, never `resumeFromRunId`. One pass at a time in the
worktree, `width` 1 (owner: one worker at a time), every command's output under about 4 KB.
Before a room launch run `python3 tools/raid_gate/pass_state.py build/author_state/<pass>` and
check it; a room to re-author must have its `<id>.author.json` / `.review.json` moved aside.

## State on 2026-10-04, after room launch 13

- Mode tested: Theatre of Blood, ENTRY, SOLO only. Normal, Hard and party play are untested.
- Seam15 LANDED (parent 53a8aa8e0, OSRS-Content 946c357a71): the super restore no longer heals
  Hitpoints, Verzik's tornado goes with her, `::give` takes the exact symbol, the Entry chest
  keeps its leftover bandages. Gates at the baseline (quest suite 115 green + deserttreasure,
  forgettabletale, regicide, troll RED; conformance 324/324; C selftest 11 baseline failures).
- ALL SIX Entry solo rooms KEPT (pass `matthew-mbp-m4-raid-b1-rooms-tob`, launch 13, ledger
  71dcbbcb8): tob_maiden f6d2c42e5 (63 rows), tob_bloat e96af7766 (55), tob_xarpus f417148e4
  (63), tob_sotetseg 812267e7f (83), tob_nylocas d87427d41 (76), tob_verzik d1b4ccf00 (144,
  deathless, every tornado row before her death). Coverage FULL on each.
- Seam16 LANDED (parent 2be1a3db5, ledger 0af2cc300, content a098da317e): Maiden's blackstorm
  always lands and is halved by prayer (sourced; my triage's premise was wrong, no change), Bloat's
  stomp needs line of sight and a hand tile rolled twice lands once, Verzik's P3 attacks roll
  accuracy from the cache record. Relay with the wiki's kit: 0 of 5 names survive (three die in
  Xarpus P2 to 51-63 acid splats, one at Vasilias, one in the Nylocas waves); tob_entry NOT
  authored. Rooms to re-author after seam18: tob_maiden (two new spec rows to measure), tob_bloat
  (hand_tiles row now 14-16 range), tob_verzik (256/14, dies in P3 without food); kept and green:
  tob_xarpus, tob_sotetseg, tob_nylocas. Their author/review state is moved aside as
  `*.launch13_seam16.*.bak`.
- Seam18 LANDED (parent 1746816fb, ledger 09ea62f64, content 6c7a65615a): the Saradomin brew
  raises Defence 20%+2 of base (owner confirmed; conformance 99 -> 120); br_potion.rs2's ToA
  supply brew still drains (CONTENT_BUGS, open); the relay recipe re-measured: 0 of 5 names
  survive, tob_entry NOT authored (the Nylocas uses up the supplies; next unproved lever is
  Ancient Magicks there). Quest suite at the baseline. ROOMS NOW MOVED: tob_maiden (two spec rows
  unmeasured), tob_bloat (hand_tiles range), tob_nylocas 91/24 (18 brew doses by tick 462,
  Vasilias never spawns), tob_verzik 256/14 (P3, no food); tob_xarpus, tob_sotetseg green.
- Seam17 MERGED into the raid branch at 41a93c0fc (three driven clients in one world; verified:
  _party_smoke green, cooks_assistant/druid byte-identical, conformance 341/341 = 177 verbs +
  164 seams). The raid17 worktree is removed; branch matthew-mbp-m4-raid-b1-seam17 stays pushed.
- Seam19 LANDED (parent 15c2ace2b, ledger 92e770eec, content 007a1c7c9b): `::tobjoinroom [mode]`
  puts a member in the leader's `::tobmode` instance (`::tobjoin` was taken by the C selftest);
  `::tobstate` ends ` party=N scale=K`; every raider's party orbs are current; `t.raid.enter`
  works for a party; fixture `tob_normal_done.ini` (a Normal completion, for the Hard door);
  raid_coverage.py party scope; the author card takes `party`; `_party_smoke` phase C at Bloat
  Normal reads hp_3 1500 = 2000 x 750/1000. Conformance 344/344. Open (CONTENT_BUGS seam19): the
  fight watchdog runs only for the barrier-crosser; room music reaches only the builder. Three
  clients are NOT tick-deterministic run to run (a member's typed command lands at its own
  frame-timed boundary; one run in three shifted by a tick).
- Room launch 14 DONE (pass `matthew-mbp-m4-raid-b1-rooms-tob`, Entry solo; ledger c1ca757e6):
  ALL SIX ENTRY SOLO ROOMS KEPT and FULL on the current tree: tob_maiden 1dffd5230 (65 rows),
  tob_bloat abc53c18e (55), tob_nylocas 116a1d938 (76), tob_xarpus f417148e4 (63), tob_sotetseg
  812267e7f (83), tob_verzik 3d7589ff4 (145; deathless; P3 autos accuracy-rolled, prayers off at
  P3 start so unprayed is proved by hits above the prayed max). Reviewers and samplers could not
  open PNGs this session (Read hook timeout): kept by ledger and tick log.
- OWNER RULINGS 2026-10-04 (CONTENT_BUGS.md "Owner rulings"): a protection prayer is read when
  the attack is SENT (default); Verzik P3 reads it on hit (the exception, kept).
- Seam20 LANDED (parent 4bfaff1b6, ledger 8cb03d5be; pack byte-identical): the prayer-read audit.
  Sotetseg's ball (Blert: 23 of 30 balls thrown with Magic off and prayed within 4 ticks cost
  nothing) and the P2 urn bombs (Verzik wiki :394/:397, Strategies :907) are SOURCED landing-tick
  reads and stay; Verzik P3 on hit is the owner's ruling AND now Blert-sourced (the orchestrator's
  analysis, pinned under sources/blert_api/spec_pass_verzik/: prayer off at the throw but on at the
  landing gives the prayed profile, max 16 of 33, n=28). Every other ToB prayer read is on the
  send tick (the list is in CONTENT_BUGS.md "From seam20"). Rooms: all six green; tob_sotetseg
  and tob_verzik must be re-authored only to MEASURE the two new spec rows
  (sotetseg.ball_prayer_read_tick, verzik.p2_bomb_prayer_read_tick); coverage, not behaviour.
- Seam21 LANDED (parent af629f4e3, ledger c430ecece): the party lockstep is PINNED. The one-tick
  shift was `t.party.barrier`'s cross-process file race (marks now stamped with the lockstep
  tick). Link protocol 2: SEAT(version, seat, k), READY(frame count, checked against F = 30/k),
  TICK(tick, world digest), a frame audit; every mismatch aborts loudly. `tools/raid_gate/
  party_repeat.py <id> --runs N --load [--cycles k]` is the determinism gate: AGREE on the smoke
  (3 runs under load) and on a 406-tick Normal Bloat fight; gate.py fails a run whose traces differ
  (`party.lockstep`). The raid_author party brief tells authors to run it. Open: a member keeps
  running after the leader exits (should abort); gate.py asserts on a scratch run name.
  FRAMES PER TICK: `TORIRS_LOGIC_CYCLES_PER_FRAME=k` exists; k>1 is deterministic but is a
  DIFFERENT run from k=1 because the driver's verbs are frame-granular (k=10 fails a chat row),
  and it saved nothing (smoke 10.9/10.5/11.0 s at k=1/10/30: the frame loop is not the cost).
  Party default stays k=1; making the driver tick-granular is a later seam if the owner wants it.
- Room launches 15a-c DONE (ledgers 146987813, befac048e, fb45d4463): ALL SIX ENTRY SOLO ROOMS
  KEPT AND FULL: tob_maiden 1dffd5230 (65), tob_bloat abc53c18e (55), tob_nylocas 116a1d938 (76),
  tob_sotetseg 1c86bf153 (84; the ball's impact read proved both ways), tob_xarpus f417148e4 (63),
  tob_verzik d3ff92400 (145; the urn bomb's landing read proved by a reverse trial: prayer
  dropped after the throw, hit 12 > prayed max 8, eight prayed bombs 0-7).
- THE NORMAL THREE-PLAYER ROOM PASS, first launch (pass `matthew-mbp-m4-raid-b1-rooms-tob-normal`,
  3.7 h, six authors x 6-19 runs): ALL SIX REJECTED, nothing committed; the six
  test/raids/tob_<room>_normal.lua files are uncommitted author attempts (read-only; the next
  launch's authors continue from them). Its sampler stopped on a card bug (the pass name did not
  strip to the branch; fixed e7eb5ebbd), so no ROOM_LEDGER section and the 12 doc gaps are
  unfolded (they are in the *.review.json files). What it found: (a) HARNESS: a member that dies
  ends its script and party.lockstep fails the run (Maiden and Sotetseg wiped on this); no member
  t.tick; the Lua instruction budget undocumented; t.prayer.points broken; pid mapping wrong in
  README. (b) CONTENT: no tornado for a trio at Sotetseg (tob_sotetseg.rs2:1470 + the seam19
  watchdog gap) and 99 prayer drained in 165 ticks; Xarpus P2 orbs per landing 1,2,3 vs spec
  "1,2" (both weakly sourced); Verzik P1 at three not survivable by the authors (shield 1500,
  hides 5 tiles out); nylocas spawn_aggro 34 of 35. (c) STRATEGY: Maiden with one freezer
  leaks 11 crabs; Bloat trio deals ~30 per down. Closest: tob_nylocas_normal 81/85, tob_xarpus_normal 62/63,
  tob_maiden_normal 47/70 with a survived run.
- Seam22 LANDED (parent 1b22d51c9, content fb292a9996, pushed; gates at the baseline, no Entry
  room moved): a dead raider stays in lockstep (`_party_smoke` kills p3 on tick 220, 157 PASS,
  party_repeat AGREE); member t.tick; t.prayer.points; the instruction budget and pid mapping in
  README; every raider judged by the room watchdog; Sotetseg's maze for a trio; the Xarpus party
  chain settled from Blert (the solo coin kept, an Open row); Verzik P1 trio recipe with the
  owner's Dawnbringer quote (settled, no content change); the nylocas aggro row re-worded.
  Conformance 350/350 (178 verbs + 172 seams). Open: the Dawnbringer drop-and-take is not yet
  driven end to end and no verb reads special energy; t.player.step_tick is unsupported on a
  member (walk_to instead). The four trio attempts for Xarpus, Sotetseg, Verzik and Nylocas must
  be re-authored (fitted to the old behaviour); Maiden's and Bloat's are strategy problems.
- Seam23 LANDED (parent 3ba1774cc, ledger 1d75019f1): THE SCRIPTS TAB, first cut. In a client
  launched with `python3 tools/raid_gate/prepare_scripts.py && ./launch run osrs239-scripts`:
  log in, Confirm the Character Creator on a new name, open Scripts, search, pick, Play; the six
  solo Entry rooms play at real speed; Stop ends a run; the client stays up. api.drive.start/
  stop/status under TORIRS_DRIVE_ON_DEMAND=1. Proved HEADLESS only (no window was opened): the
  owner has not yet tried it. Known rough edges (seam24 fixes them): consecutive runs share one
  account's world, the search box loses the keyboard 600 ms after a key, unavailable rows are
  plain labels, disabled/enabled button captions read backwards (orange = disabled).
- RUNNING: seam pass `matthew-mbp-m4-raid-b1-seam24` (triage `SEAM_TRIAGE_2026-10-05c.md`,
  e23b9275a). The owner's rules for it, 2026-10-05: the tab shows ALL the automated scripts
  (123 quests + raid rooms); "The client should just query for the available scripts - the
  server should tell it - in a similar way the client asks for the available plugins" (a
  manifest fetched through the IO layer like plugins/plugins.ini; no prepare step, no env index;
  the wrapping moves into the driver's Lua); searchable; a test's source is reloaded on every
  Play (hot reload); a fresh account per Play; legs files in one sitting; seam23's open list.
  The owner then runs just `./launch run osrs239-scripts`.
  THEN SEAM25 (`SEAM_TRIAGE_2026-10-05d.md`, bfeb3cc68): what the owner hit when he first
  watched (2026-10-05): the mouse landed far from the pointer at the login screen in the default
  window (fixed for him by `TORIRS_HIDPI=0 ./launch run osrs239-scripts -- --soft3d --window
  765x503`, both together); the camera flickered while a script ran (first cause fixed by the
  orchestrator, 63ea41d39: a watched client never re-aims for a photograph). Seam23's proof missed
  the mouse fault: injected clicks enter after the window-to-game mapping. RULE: a watch
  feature's proof goes through the real SDL event path.
  THE OWNER WATCHES FROM `build/orchestrator/worktrees/raid-watch` (detached, with its own
  OSRS-Content worktree and cache link; the orchestrator moves it to a landed commit with
  `git -C <raid-watch> checkout -q --detach <commit>` after each watch-related landing, and
  rebuilds with `./launch run osrs239-scripts --no-client`). NEVER tell the owner to launch from
  worktrees/raid while a pass runs there: workers edit it (he ran a half-edited tree once).
  THEN SEAM26 (`SEAM_TRIAGE_2026-10-05e.md`, 7252ddade): THE RUNNER CAMERA SPLIT, the owner's
  feature (design published at https://claude.ai/artifact/FDWBxLAVmDPdt5PxR2erQu; a copy of the
  page is not in the repo, the triage repeats everything): a view per role (AutomationRunner /
  PlayerClient: camera + pointer + pickset + menu), the runner's pick and screenshot passes
  offscreen on the software lane on demand, an Interact on/off switch always indicated, scripted
  cameras take every view, the runner's menu tinted; three seams (gather = pure refactor, split,
  debug aids). It was to supersede seam25's camera item; the OWNER REINSTATED IT (2026-10-05):
  "The scripts should also attempt to turn the camera, rather than snap to the camera for a
  screenshot." Seam25's `watched_camera_and_shots` now makes a watched driver TURN the camera
  through the arrow keys' own path at their rate (never an instant pose), not blocking inputs
  that need no camera. OWNER'S RULING (2026-10-05): "Only the watched client should turn the
  camera, headless can just snap, but make sure they both work and that the tests don't rely on
  the snapping behavior - that should be handled by the code in a single call." So one driver
  call owns every camera move (snap headless, turn watched, its own deadline per mode); no test
  writes a pose itself (conformance check); the same scripts proved in both modes.
  SEAM26 IS NOT RUN HERE ANY MORE (2026-10-05): the owner started a separate camera session from
  the orchestrator's prompt; it works in `build/orchestrator/worktrees/camera` on branch
  `matthew-mbp-m4-camera-b1` (cut from this branch), does the gather seam first, and merges this
  branch in for seam24 and seam25 before the split. This session never touches that worktree.
  Its work comes back by a merge the raid orchestrator makes when the owner asks.
  OWNER'S RULE, 2026-10-05, FOR EVERY PASS FROM NOW ON: "Never do a real time play through. That
  was a huge waste." "Unless I ask." (Seam24's one fixer ran 3.5 hours, the last 1.5 replaying
  raid rooms in a real-time client.) Put it in every work order and every card context: proofs run
  on the virtual clock; a script is played only as far as the claim needs and never twice for one
  claim; 30 minutes of proof after the gates are green, then the report says what is not proved.
  OWNER, 2026-10-05, ON SPEED: "Perhaps instead of watching, you can use something like what blert
  does and just look at the log and see where you went wrong. I want you to be faster, you are
  being incredibly innefficient." and "Save the visual verification for afterwards." So: a failed
  run is DIAGNOSED FROM ITS TICK LOG with `tools/raid_gate/raid_report.py <run dir>...` (the
  orchestrator's first cut; seam27's gate seam adds the raider's state and inputs to the log and a
  mistakes list), never replayed or watched; no screenshots, crops or frame dumps in a pass:
  what needs eyes is listed for a later visual pass. The orchestrator does small tools and
  analyses itself instead of waiting on a pass.
  SEAM24 LANDED 2026-10-05: parent 473097e5e (no content change); the closer then merged the
  camera session's first refactor from the remote raid branch (world_view_gather, 325c57690; merge
  9e946b5a0, clean) and re-verified ON THE MERGED TREE: cooks_assistant and druid identical to
  merge17_check, conformance 355/355 (183 verbs + 172 seams), six rooms byte-identical and FULL,
  quest suite 115 green + the four known reds, _party_smoke 157 and party_repeat AGREE. Pushed at
  9e946b5a0 by the orchestrator; raid-watch moved to 9e946b5a0 and rebuilt. The pass took 4.9
  hours (fixer 3.5, closer 1.3): the closer ran the rooms twice and three watched Plays. NEXT
  CARDS: the context must cap the closer too (suite once, no watched Play, 30 minutes).
  STATE 2026-10-05 17:40. SEAM27 fixed, closing (pass matthew-mbp-m4-raid-b1-seam27; the session
  restarted mid-close; relaunched with the same args, it resumes from close.progress.md):
  `t.together` (six inputs in one tick measured; combo eating in one tick); `t.raid.play(plan_id)`
  = the play library (in raid.lua's last banner block for now), proved on BLOAT ENTRY only: green
  on five names, 196-281 ticks a kill (old play 329, or never), 66-160 damage taken (old 724), 3-8
  eats (old 41); one Normal trio Bloat run measured, all three died (no Defence drain in the plan;
  hazard skill missed the attack path). The card SKIPPED `seed_survey_gate` (it read "tooling:" as a
  design note): the orchestrator wrote `tools/raid_gate/seed_survey.py` itself (c7d273792; proved:
  `_play_smoke` 5 of 5 names green). USE KINDS driver / engine / content IN A TRIAGE, nothing else.
  THE OWNER'S TWO STEPS (2026-10-05): "1. Add multiple actions per tick to the runner 2. Prefer
  using the log to iterate on a program that can be TOB - save visual verification for the end
  when you're succeeding." So the order below is superseded:
  OWNER, 2026-10-05: "Keep going. Do not stop until you can beat the theater of blood. If you need
  a feature in the client automation runner to make things faster (watch for events, add
  callbacks and triggers or whatever, then do so)". He did not answer whether the room plans may
  run in parallel worktrees: they run ONE AT A TIME until he says otherwise.
  SEAM27 LANDED 0b9633ed2 (ledger 7cc5aac68), pushed: conformance 358/358, six rooms and both
  baseline ledgers byte-identical, suite 115 green + 4 known reds, party repeat AGREE. SEAM29
  LAUNCHED 17:50 (pass matthew-mbp-m4-raid-b1-seam29, triage 05h = its first two seams only:
  library files + hazard fix, raider log); the five room plans are SEAM30 (`SEAM_TRIAGE_2026-10-05i.md`,
  pass matthew-mbp-m4-raid-b1-seam30), split off so the first two land early.
  OWNER'S RULE, 2026-10-06 12:15: "It is a waste of time to prove these, just mark them done" (the
  room-clear restore and the chest points). So: a CONTENT fix made from a quoted source line is
  DONE when it compiles and the pack builds; no tick-log proof run, no survey re-run for it alone;
  the row in CONTENT_BUGS.md says FIXED with the source line. Run proofs are for behaviour in
  doubt (a disagreement between sources, an engine path) and for the plays. The chest points fix
  (per player, 6-13 by individual performance, Chest:20, Strategies:568) is DONE as of bf3dabef7c.
  Put this rule in every content work order from now on.
  OWNER, 2026-10-06 13:40: "I don't care about the blert entry mode data, we can consider entry
  mode complete." ENTRY MODE IS COMPLETE: six rooms and the whole raid green on five names through
  the library; no Entry reference work, no Entry re-measurement against Blert. The Blert bar
  applies to NORMAL (and later Hard): every room green on five leaders with the lockstep repeat,
  every outcome number inside the recorded death-free trio range, then the Normal relay.
  MERGED INTO v3 2026-10-06 13:20 (PR #126 at 97602934a; OSRS-Content PR #50): everything landed
  through seam40/44; the raid branch continues from there and reaches v3 by further PRs.
  STATE 2026-10-06 11:40. LANDED AND MERGED since 09:50: seam38 (p_stopaction + 5 ops bound to the
  script's player; overheal decays; scythe 1x3 arc; salve accuracy; content bb84907f76), the
  camera lane's seam39 (the ENTRY RELAY 5 of 5 ON THE FIXED CONTENT: the potion row reads the
  stat against its base, the barrier re-pressed, Verzik re-talked; the NORMAL RELAY harness
  `_play_normal.lua` built and deterministic, 0 of 3: supplies empty after Maiden, where the trio
  takes 1050-1429 against 625 in the room harness), the Maiden plan f5144341d (rangers scythe the
  crabs, stay on her at 30 percent; rows re-sourced to Blert's 26 trio rooms, sources/blert_api/
  maiden_trio_crabs/). Raid branch c6abc903b. LANES: raid = seam37 (launch service: seams 1-2
  proved, seam 3 the tab running); raid25 = seam40 (THE PLANS FOLLOW BLERT: reference tool,
  raid_report --against, Maiden then Nylocas); camera = seam41 (the room-clear restore from the
  wiki, the supply chest, the last bound-player ops, the kept tests' two wrong rows). Honest
  state: Entry solo whole raid green 5 of 5; Normal rooms Bloat, Sotetseg, Xarpus, Verzik green;
  Normal Maiden and Nylocas clear below the real trios' rates; the Normal relay runs but does not
  clear (supplies). NOTE: the raid worktree's conformance VERB_COUNT lags the launch verbs until
  seam37's closer lands (verb_list says 202 defined vs 195 asserted while its edits are dirty).
  STATE 2026-10-06 09:50. ENTRY RELAY LANDED (w25 debbf5380, merged 56ae174c9): the whole raid
  Entry solo end to end, 5 of 5 names on content 1c612cdfe3. CONTENT PASS LANDED (9998dec43,
  content 525e8538a9: Verzik reds cycle from 60 Blert streams, salve amulet, br_ brews boost
  Defence, overheal holds; Bloat flies and Sotetseg melee settled as not bugs). ON THE MERGED TREE
  the relay is 2 of 3 (`build/mergerelay_entry.log`): `svaplayentry` fails at Sotetseg --
  `sotetseg.potion` reads attack 96 after the brews' drain (the overheal now holds, so the raider
  drinks differently and the name diverges), then `sotetseg.begin` finds no dialogue (the barrier
  talk pressed while the plan was mid-drink): a PLAN robustness fault the new content exposed, not
  a content regression. TO FIX in the next free lane: `_play_entry` Sotetseg entry waits for the
  dose to land before the barrier talk (or re-talks), then `seed_survey.py _play_entry --names 5`.
  LANES: raid = seam37 (launch service); raid25 = seam38 (p_stopaction, overheal decay, health
  regen, scythe arc, salve accuracy); camera = seam35m (Normal Maiden rows, Nylocas supports).
  STATE 2026-10-06 09:05. Normal Verzik LANDED (camera lane e17f5f9db, merged 3134c8804): 5 of 5
  leaders, repeat AGREE, Dawnbringer shared by drop-and-take. Normal Xarpus LANDED (w25 e98866545,
  merged d29386f06). Normal green: Bloat, Sotetseg, Xarpus, Verzik. RUNNING: raid lane seam36 (the
  content rows, closing), raid25 lane seam35e (the Entry relay; its closer found the content
  checkout STALE at fb292a9996 and re-proves on 1c612cdfe3), camera lane seam35m (Normal Maiden
  rows + Nylocas supports; its content checkout was ALSO stale until 09:00 -- the orchestrator
  fast-forwarded it; the Maiden result was measured on stale content and must be RE-RUN after the
  merge). LESSON: after merging a parent branch into a lane, ALWAYS `git -C <lane>/OSRS-Content
  merge --ff-only <the parent's gitlink>` (the content worktree does not follow the gitlink).
  NEXT in the raid lane after seam36 lands: SEAM37 = `SEAM_TRIAGE_2026-10-06j.md` (the owner's
  launch service: the embedded IO server spawns and manages party members; three seams; owner:
  "Ok implement it."). THEN the Normal relay (06f's last section) once Maiden and Nylocas land.
  OWNER, 2026-10-06 07:00: "You need to speed this up. I expect full clearance in 5 hours." THREE
  LANES NOW (the classifier refused creating new worktrees, so the two idle ones are reused):
  worktrees/raid (seam33 closing, then SEAM35 = `SEAM_TRIAGE_2026-10-06f.md`: Normal Maiden's rows,
  Normal Nylocas' supports, the ENTRY SOLO RELAY `_play_entry.lua`, the NORMAL TRIO RELAY
  `_play_normal.lua`); worktrees/raid25 on branch matthew-mbp-m4-raid-b1-w25 (pass seam34x:
  Normal Xarpus, `SEAM_TRIAGE_2026-10-06d.md`); worktrees/camera on branch matthew-mbp-m4-camera-b1
  (pass seam34v: Normal Verzik with the Dawnbringer, `SEAM_TRIAGE_2026-10-06e.md`). The lane
  closers run only conformance, cooks and their own surveys; THE ORCHESTRATOR gates each merge
  into the raid branch with cooks+druid, conformance and the harness surveys, then pushes. Bars
  for speed: iterate on three names, five names and the repeat gate on the final plan; the relays
  on three names. FULL CLEARANCE = `_play_entry` (solo) and `_play_normal` (trio) green end to end.
  STATE 2026-10-06 ~04:30. SEAM32 LANDED 328d7ec9b (pushed): NORMAL TRIO BLOAT GREEN (five leaders,
  lockstep repeat AGREE; 267-332 room ticks, 4-5 downs, no deaths); Normal Nylocas kept on a THIN
  margin (one support left at 1-11 percent; names 6 and 7 fail); Normal Maiden 0 of 5 (supplies).
  The reports name three gaps real trios have and we do not: powered staves nulled by a Hagios
  (CONTENT_BUGS :216), chinchompas single-target (player_ranged.rs2:17), no loaded blowpipe in a
  kit. NEXT = SEAM33 (`SEAM_TRIAGE_2026-10-06c.md`): those three fixed at the source, then Normal
  Maiden green, Nylocas on seven names with supports above 50 percent, Normal Sotetseg. THEN SEAM34:
  Normal Xarpus and Verzik (the Dawnbringer shared by special). Honest state: six Entry rooms green
  on five names; Normal Bloat green; Normal Nylocas thin; Normal Maiden red; three rooms unplanned.
  STATE 2026-10-06 ~01:40. SEAM31 LANDED 89146dbf1 (pushed): the two library faults fixed (prayer
  groups from prayers.dbrow; death_serial seeded; quick-press answers made true; boss-gone = dead),
  Nylocas 5 of 5 (and 10 of 10), Verzik 5 of 5: ALL SIX ENTRY ROOMS are played through the library
  and green on five names. Entry Vasilias' prayed max: unsourced, CONTENT_BUGS row, no change.
  NEXT = SEAM32 (`SEAM_TRIAGE_2026-10-06b.md`): the NORMAL TRIO plans for Bloat (Defence-drain
  run-by, roles), Maiden (the freezer), Nylocas (lanes); kept = `seed_survey.py <harness> --party 3`
  5 of 5 leaders AND `party_repeat.py --runs 3` AGREE. THEN SEAM33: Sotetseg (maze runner, ball
  soak), Xarpus, Verzik (P1 Dawnbringer shared by special: the owner's recipe) in Normal. THEN the
  whole-raid relay in Entry solo and Normal trio (`tob_entry`, `tob_normal`), the re-author of the
  kept tests on the library, Hard, ToA, CoX. Honest state: six Entry rooms green on five names
  through the library; nothing in Normal is green.
  STATE 2026-10-06 ~00:00. SEAM30 LANDED a1c03f6ae (pushed): Entry plans through the library --
  Maiden, Sotetseg, Xarpus GREEN on five names (with Bloat: FOUR of six rooms); Nylocas 1 of 5
  (supply budget at Vasilias), Verzik 4 of 5 (svd dies in P3 after a 273-tick reds phase). Two
  LIBRARY FAULTS found and worked around (prayer switch re-lights the old prayer; death_serial 0
  ends the play on a boss spawn into a reused slot). NEXT = SEAM31 (`SEAM_TRIAGE_2026-10-06a.md`):
  the library faults, Nylocas and Verzik to five names, the Entry Vasilias prayed-max content
  question. THEN SEAM32: the Normal three-player plans on the library (Bloat's Defence drain
  run-by and roles first; then the other rooms), measured with `--party 3` surveys. THEN the
  re-author of the kept tests on the library (the `_play_<room>` harnesses become the rooms) and
  Hard; `tob_entry`; ToA; CoX. Honest state: four Entry rooms green on five names through the
  library; Nylocas and Verzik not; nothing in Normal is green.
  STATE 2026-10-05 ~20:30. THE CAMERA SPLIT IS ON THE RAID BRANCH: camera pass seam2 landed
  d4dac4338 (runner_view_split + watch_debug_aids; CAMERA_LEDGER.md / CAMERA_RESUME.md hold the
  proofs, the lanes -- software and GL3 carry it, D3D9/GLES/WebGL gated off -- and
  visual_checks_for_later for the owner), merged at 636621d4a (pointer.lua resolved: a watched
  client turns only when the script has NO view of its own; with its own view the runner snaps and
  the watcher's camera never moves), verified on the merged build: cooks and druid identical,
  conformance 369/369 (193 verbs + 176 seams), _play_smoke 5 of 5, six rooms green, pt-switch,
  tree-walks, plugin-lua PASS. The owner tries it: ./launch run osrs239-scripts, Play, move his own
  camera. The camera branch and worktree are DONE (nothing further planned there; the
  picture-in-picture is 'later'); raid25/w25 likewise: both worktrees may be removed. RAID WORK
  RESUMES: seam30 launched (the five Entry room plans).
  STATE 2026-10-05 ~19:45. SEAM25 LANDED on w25 (a0f479d06, ledger d6c875357) and is MERGED into
  the raid branch (c04b94da9; verified on the merged build: cooks and druid identical, conformance
  363/363, _play_smoke 5 of 5, six rooms green; pushed 79b4dd3da). What seam25 left open: the
  mouse-mapping ROOT CAUSE on the GPU/Retina window was not reproduced headless (the profile now
  PINS --soft3d --window 765x503 TORIRS_HIDPI=0, so the one command works; the owner should try the
  unpinned GPU window later); the camera turn is in (one call QD.drive.camera_aim) but no watched
  Play has turned yet; the Scripts tab's "Start from" select was NOT wired by the closer -- THE
  ORCHESTRATOR WIRED IT (419333fb1, proved headless: reset on the watcher's own account, no relog,
  then Bloat to the kill). raid-watch moved to b5be907ac. The raid25 worktree and the w25 branches
  can be removed once the camera split is merged (nothing else is on them).
  THE CAMERA SPLIT (pass matthew-mbp-m4-camera-b1-seam2, worktrees/camera): runner_view_split is
  FIXED (uncommitted; two views proved by state: cooks and maiden ledgers equal with two views, the
  runner's pose and pointer identical across 1691 frames while a simulated watcher orbited; Interact
  off/on proved; ::cam moves both); watch_debug_aids running; then its closer. WHEN IT LANDS: merge
  origin/matthew-mbp-m4-camera-b1 into the raid branch (conflicts expected in
  torirs_plugin_drive_pointer.c, pointer.lua, script_runner.lua, _conformance.lua, DRIVER_NOTES.md,
  SEAM_LEDGER.md), verify as above, push, move raid-watch, THEN launch seam30.
  OWNER, 2026-10-05 ~18:10, THE PRIORITY ABOVE EVERYTHING BELOW: "Ok before you do any more raid
  work, that MUST be implemented. The full multi WorldView support". THE RAID ORCHESTRATOR TOOK
  OVER THE CAMERA SPLIT (the camera session was idle since 14:34, its worktree clean): merged the
  raid branch into `matthew-mbp-m4-camera-b1` (21079b994), wrote
  `<camera>/docs/minigames/raid_loop/CAMERA_TRIAGE_seam2.md` (runner_view_split + watch_debug_aids,
  c3837b69e) and launched pass `matthew-mbp-m4-camera-b1-seam2` at 18:15 (card args worktree =
  worktrees/camera, branch = matthew-mbp-m4-camera-b1; state in `<camera>/build/seam_state/`).
  NO NEW RAID PASS IS LAUNCHED UNTIL IT LANDS ON THE RAID BRANCH: seam30 (the room plans) WAITS.
  Seam29 (worktrees/raid) and seam25 (worktrees/raid25) were already running and are left to
  finish (never stop a running pass); merge each when it closes. WHEN THE SPLIT LANDS: merge
  `origin/matthew-mbp-m4-camera-b1` into the raid branch (by hand where seam25 touched
  torirs_plugin_drive_pointer.c, pointer.lua, script_runner.lua), re-run cooks_assistant, druid,
  conformance, the six rooms and the Bloat five-name survey on the merged tree, push, move
  raid-watch, tell the owner how to try it, THEN launch seam30.
  OWNER, 2026-10-05 ~18:00: "Can you just do those things? Do seam25." So SEAM25 RUNS NOW, IN ITS
  OWN WORKTREE, alongside seam29: `build/orchestrator/worktrees/raid25`, branch
  `matthew-mbp-m4-raid-b1-w25` (parent and OSRS-Content), cut at 2877bbe99; its triage is that
  worktree's copy of `SEAM_TRIAGE_2026-10-05d.md` (paths rewritten; proofs by state, not
  pictures); pass `matthew-mbp-m4-raid-b1-seam25` with card args worktree + branch; state in
  `<raid25>/build/seam_state/`. WHEN IT LANDS: merge `origin/matthew-mbp-m4-raid-b1-w25` into the
  raid branch by hand (pointer.lua, _conformance.lua, DRIVER_NOTES.md will overlap with seam29/30),
  re-run cooks_assistant, druid, conformance and the Bloat five-name survey on the merged tree,
  push, move raid-watch, remove the raid25 worktree (and its content worktree and branches), and
  tell the camera session's owner that seam24 + seam25 are both on the raid branch (its split
  waits on exactly that). Also done directly (50a346343): the Scripts tab lists `_play_*` harnesses.
  NEXT = SEAM29 (`SEAM_TRIAGE_2026-10-05h.md`): the library into its own files (one plan file per
  room) and the hazard fix; the raider's state and inputs into the tick log and a mistakes list in
  raid_report.py; then a sourced Entry plan per room (maiden, nylocas, sotetseg, xarpus, verzik),
  each proved in `test/raids/_play_<room>.lua` by the five-name survey, iterated from logs only.
  THEN the Normal three-player plans on the library (Bloat's Defence drain first), THEN seam28
  (the survey's measurement and content rows) and the Entry re-author of the kept tests on the
  library, THEN, when the play is succeeding, the visual pass: seam25 (mouse mapping, camera turn,
  reset character) and the camera branch's split.
  ORDER AFTER SEAM24 (owner, 2026-10-05, after watching the whole Entry raid: "I noticed that the
  driver is not very fast or good. That is not going to work in normal mode. You will need to code
  up the agents a lot smarter using the actual strategies. Did you also fix the single action
  issue?" -- it was NOT fixed then, only written): THE PLAY COMES FIRST.
  (1) SEAM27 (`SEAM_TRIAGE_2026-10-05f.md`), three seams: `seed_survey_gate`
  (tools/raid_gate/seed_survey.py; the author card keeps a room only when green on five names);
  `several_inputs_one_tick` (owner: "update the script runner so that it can do multiple things at
  once ... it's really slow to equip, eat move around": no driver verb lands two inputs in one
  tick, Verzik's bow-and-arrows swap takes two ticks, Bloat eats through the slow 3-tick press, the
  eater eats every 6 ticks where the server allows 3; the seam sources what OSRS takes in a tick,
  makes the server match, adds a send-together block, fixes the eater);
  `raid_play_by_tick_intent` (ONE play library, script/plugins/quest_driver/raid_play.lua: every
  tick read what a person can see, decide the tick's whole intent, send it together; shared skills
  -- attack on cooldown, pray by the telegraph, one-tick loadouts, supplies by the largest hit,
  hazards, party roles; room plans cited from the Strategies pages, the trio guides and
  transcripts, Blert; proved on Bloat: Entry green on five names, one Normal trio run measured).
  THE SEED FINDING behind the gate: `SEED_SURVEY_2026-10-05.md` (evidence in
  `build/seed_survey_2026-10-05/`): each kept room replayed unchanged under four other account
  names, 7 of 24 green (Xarpus 4 of 4; Maiden, Bloat, Sotetseg 1 of 4; Nylocas and Verzik 0 of 4).
  (2) SEAM25 (`SEAM_TRIAGE_2026-10-05d.md`, above): the watch pass (mouse mapping, camera turn in
  one call, reset character).
  (3) SEAM28 (`SEAM_TRIAGE_2026-10-05g.md`): the survey's failing rows per fragile room; the
  measurement and content rows fixed or written; play rows only named (the play is re-authored).
  (4) THE ENTRY RE-AUTHOR pass, all six rooms ON THE LIBRARY (card raid_author, pass
  `matthew-mbp-m4-raid-b1-rooms-tob-entry2`, raid tob, mode entry, party 1, width 1, context =
  PLAY_NOTES.md, the authors' rule in test/raids/README.md and the seam28 findings; kept = the
  five-name survey green; a test with its own fight loop is rejected).
  UNTIL THEN say it plainly in every report: ToB Entry solo is tested ON ONE SEED PER ROOM, with a
  play the owner judged too slow for Normal.
  (5) THEN the Normal three-player pass, on the library, with the seam22 notes and the five-name
  survey.
- OWNER, 2026-10-05, Verzik P1 for a trio: "the players need to take the dawnbringer from the
  skeleton on the ground after xarpus. That weapon does not have the shield penalty and the
  players should share it using their special attack." Our content agrees (tob_xarpus.rs2:1817-1850
  the skeleton with the sword, one per raid; verzik.tsv p1_cap 10/3/3 Dawnbringer exempt (A),
  p1_dawn_damage 75-150 ignoring the cap (C)). The Normal Verzik author's P1 recipe is therefore:
  take the Dawnbringer at the skeleton, every raider uses BOTH specials on the shield, pass it by
  dropping and picking up (a drop/pick-up recipe for the driver if none exists), hide behind the
  pillars between bolts. Put this in the Normal relaunch context and in DRIVER_NOTES.
- Joined relay scratch: `build/seam_state/matthew-mbp-m4-raid-b1-seam15/trj/joined.lua`; run
  names seed on their first 12 characters, case folded (jbase37).

## Next, in order

1. Seam22 lands (above); then relaunch the Normal pass with the same args (its rejected rooms get the findings); re-launch for rejected or sent-back rooms until kept. (Seam21 landed.) SEAM21 was on
   `SEAM_TRIAGE_2026-10-04i.md` (owner, 2026-10-04: "I don't want to introduce nondeterminism.
   The server and clients should be able to run in lockstep"): a member runs exactly F frames
   per tick, READY carries the frame count (the leader refuses a mismatch), TICK carries a world
   digest, SEAT a protocol version; `party_repeat.py` runs a party test N times (one under CPU
   load) and passes only when tick logs, member ledgers and boundary traces are identical. Also (owner): fewer frames per tick headlessly --
   TORIRS_LOGIC_CYCLES_PER_FRAME=k pays k 20 ms cycles per frame (k=1 today's 30 frames a tick;
   k=10 three; k=30 one), same k on every client of a party run; measured, party default picked. Then the NORMAL THREE-PLAYER ROOM PASS: card raid_author, pass
   `matthew-mbp-m4-raid-b1-rooms-tob-normal`, raid tob, the six rooms, mode normal, party 3, width 1
   (ids tob_<room>_normal; sources: Strategies wiki per-room sections, the six trio transcripts,
   Blert guides and data; roles only from the sources) (same room pass, move each room's
   `<id>.author.json` / `.review.json` aside, note it in sample.json's orchestrator_notes).
2. `tob_entry` from the joined relay if the five-name verdict allows, graded on
   `encounters/raidwide.tsv` (42 of its 75 rows have a raidwide.<id> step in the green ledger;
   raid_coverage.py grades only spec.<id> steps, so the author names its rows spec.raidwide.*
   or the checker learns raidwide.*); otherwise seam17 on the room the verdict names.
3. (landed) Seam19 on `SEAM_TRIAGE_2026-10-04g.md`: `::tobjoin` puts a member in
   the leader's room instance, `t.raid.enter` for a party, the member HUD orb bug, a Hard fixture
   with a Normal completion, the author card's `party` arg, `_party_smoke` at Bloat Normal.
4. Normal mode room pass with three players (`tob_<room>_normal`, pass `matthew-mbp-m4-raid-b1-rooms-tob-normal`, roles from the trio transcripts and Blert), then Hard (Hard needs a Normal completion on
   every account: tob_party.rs2 `%varp6826_tob_completions < 1` at the door).
5. The frame-count pass for D/E tick rows (`tools/raid_gate/frame_diff.py`).
6. Tombs of Amascut spec pass (corpus pinned), then Chambers of Xeric.

## Waiting on the owner

- The nine stray files a seam1 fixer wrote into the main checkout (restore was denied).
- The raid content worktree's old dirt under `selftest/quests/quest_cook/play` and
  `quest_druid/play` (restore was refused; never staged).
- Whether `docs/WAVES_ORCHESTRATOR.md` belongs on v3.

## Relaunching a pass

A background pass dies with the session that launched it. Every card resumes from its state
directory when relaunched with the SAME args: `build/author_state/<pass>/` for `raid_author`,
`build/seam_state/<pass>/` for `raid_seam` (`close.json` present = closed). Never use
resumeFromRunId. The context string for a room launch is in the "RUNNING" line above and the
state directory's sample.json orchestrator_notes; for a seam pass it is the triage file's RULES
plus the closer's duties (rooms re-run and 'must be re-authored' lines, quest suite at its
baseline with any sourced red listed in SEAM_LEDGER.md and MERGE_CHECKLIST.md, conformance,
DRIVER_NOTES, CONTENT_BUGS, commit by explicit path submodule first, push the raid branch only).
