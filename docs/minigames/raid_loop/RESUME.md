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
  THEN relaunch the Normal three-player pass with the same args and the seam22 notes.
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
