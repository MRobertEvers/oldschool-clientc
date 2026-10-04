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
- RUNNING: seam pass `matthew-mbp-m4-raid-b1-seam18` (triage `SEAM_TRIAGE_2026-10-04f.md`): the
  Saradomin brew DRAINS Defence 10%+2 where the wiki says it RAISES it 20%+2 of base (owner
  confirmed the fix 2026-10-04; every brew-drinking fight moves, quests that go red are listed,
  never reverted), then the relay's recipe faults (Maiden crabs by ranged, Nylocas aggros and
  supports, Vasilias' Magic level, Xarpus 2-tile steps and the exhumeds) re-measured under the
  same five names. Then ONE room launch (14) re-authors every room that moved.
- RUNNING IN PARALLEL (owner, 2026-10-04, overriding one-pass-at-a-time for this one pass):
  seam pass `matthew-mbp-m4-raid-b1-seam17` (triage `SEAM_TRIAGE_2026-10-04e.md`, three driven
  clients in one raid; engine + driver, no content) in ITS OWN WORKTREE
  `build/orchestrator/worktrees/raid17`, branch `matthew-mbp-m4-raid-b1-seam17` in both repos
  (from da183511b / content f8fefeb837; pushed). Its raid_seam card takes `worktree` and
  `branch` args (e0720cd55). When it lands: merge that branch into the raid branch here (parent
  and content, merge commits, by hand if seam16 touched the same files), then remove the
  worktree (`git worktree remove`, both repos). Never run a third pass.
- Joined relay scratch: `build/seam_state/matthew-mbp-m4-raid-b1-seam15/trj/joined.lua`; run
  names seed on their first 12 characters, case folded (jbase37).

## Next, in order

1. Seam16 lands: re-author the rooms it moved (same room pass, move each room's
   `<id>.author.json` / `.review.json` aside, note it in sample.json's orchestrator_notes).
2. `tob_entry` from the joined relay if the five-name verdict allows, graded on
   `encounters/raidwide.tsv` (42 of its 75 rows have a raidwide.<id> step in the green ledger;
   raid_coverage.py grades only spec.<id> steps, so the author names its rows spec.raidwide.*
   or the checker learns raidwide.*); otherwise seam17 on the room the verdict names.
3. THREE DRIVEN CLIENTS (owner, 2026-10-04: "For normal and hard mode, you will need 3
   players"): seam pass on `SEAM_TRIAGE_2026-10-04e.md` (the facts and the design questions are
   in it). Solo stays Entry only.
4. Normal mode room pass with three players, then Hard (Hard needs a Normal completion on
   every account: tob_party.rs2 `%varp6826_tob_completions < 1` at the door).
5. The frame-count pass for D/E tick rows (`tools/raid_gate/frame_diff.py`).
6. Tombs of Amascut spec pass (corpus pinned), then Chambers of Xeric.

## Waiting on the owner

- The nine stray files a seam1 fixer wrote into the main checkout (restore was denied).
- The raid content worktree's old dirt under `selftest/quests/quest_cook/play` and
  `quest_druid/play` (restore was refused; never staged).
- Whether `docs/WAVES_ORCHESTRATOR.md` belongs on v3.

## Relaunching seam16 if it is not closed

A background pass dies with the session that launched it. If
`build/seam_state/matthew-mbp-m4-raid-b1-seam16/close.json` does not exist, relaunch
`tools/raid_gate/workflows/raid_seam.workflow.js` with args `pass` `matthew-mbp-m4-raid-b1-seam16`,
`reuse_triage` `docs/minigames/raid_loop/SEAM_TRIAGE_2026-10-04d.md`, `width` 1, and this
`context` (one string):

> Seam16 of the raid loop, relaunched by a fresh session. On disk: every seam with a
> fix.<key>.json report is DONE and its edits are uncommitted in the tree; a seam with only a
> progress notebook resumes from it. Only ToB ENTRY mode SOLO is tested. All six Entry solo
> rooms are KEPT and read-only: tob_maiden f6d2c42e5, tob_bloat e96af7766, tob_xarpus
> f417148e4, tob_sotetseg 812267e7f, tob_nylocas d87427d41, tob_verzik d1b4ccf00. EVERY FIXER
> reads its seam's full section in docs/minigames/raid_loop/SEAM_TRIAGE_2026-10-04d.md and the
> RULES above the first section; the fixers run in the triage's order (the relay seam last).
> One worker at a time; no command prints more than about 4 KB; no shell command over 8 KB;
> never a recursive grep over OSRS-Content; if the editor's Read or Write hook times out use
> the shell and never loop on the failing tool. Nothing sourced is lowered without its quoted
> source line; a guide transcript is evidence, never the number to encode. THE CLOSER: (1)
> re-runs the six rooms with run.py --no-build --no-publish and gate.py, logs to files; a
> sourced fix that moves a room is EXPECTED: record 'tob_<room> must be re-authored: <what
> moved>' in SEAM_LEDGER.md, never revert a sourced fix; a room broken for an unsourced reason
> reverts that seam. (2) Quest suite: baseline 115 green + deserttreasure, regicide, troll,
> forgettabletale RED; any other regression reverts its seam unless its first failing row is a
> sourced ToB change, listed in SEAM_LEDGER.md and MERGE_CHECKLIST.md instead. (3) Carries the
> relay seam's five-name table into SEAM_LEDGER.md as the verdict on authoring tob_entry
> (authored when at least 4 of 5 survive with the wiki's kit). (4) Recipes into
> DRIVER_NOTES.md, spec rows applied (spec_check.py clean), CONTENT_BUGS.md a row per
> disagreement with a grade A-C source, one line per seam in SEAM_LEDGER.md with the quest
> suite, conformance and C selftest results; commit by explicit path, submodule first, push
> both. Never stage or restore the content worktree's dirt under selftest/quests/quest_cook/play
> and quest_druid/play. QUEST_HELPER_ROOT is /Users/matthewevers/Documents/git_repos/quest-helper.

## Relaunching seam17 if it is not closed

If `build/orchestrator/worktrees/raid17/build/seam_state/matthew-mbp-m4-raid-b1-seam17/close.json`
does not exist, relaunch `build/orchestrator/worktrees/raid17/tools/raid_gate/workflows/raid_seam.workflow.js`
with args `pass` `matthew-mbp-m4-raid-b1-seam17`, `branch` `matthew-mbp-m4-raid-b1-seam17`,
`worktree` `/Users/matthewevers/Documents/git_repos/3draster/build/orchestrator/worktrees/raid17`,
`reuse_triage` `docs/minigames/raid_loop/SEAM_TRIAGE_2026-10-04e.md`, `width` 1, and a `context`
that says: it runs ONLY in that worktree (seam16 runs in worktrees/raid at the same time; never
touch it or the main checkout); builds in its own src/build_questtest; no content edits; the
fixers in the triage's order; the editor rules (4 KB outputs, 8 KB commands, no recursive grep
over OSRS-Content, shell fallback when a hook times out); CLAUDE.md's assert/no-switch rules;
and the closer's duties: one-client quest runs byte-identical (cooks_assistant, druid against a
throwaway worktree at da183511b), the six rooms still pass, the three-client smoke run's three
tick logs agree on the server tick, check-drive-abi/check-pt-switch/test-plugin-lua, DRIVER_NOTES
'Three raiders in one run', README knobs, MERGE_CHECKLIST section 3, SEAM_LEDGER line, push the
seam17 branch only.
