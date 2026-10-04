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
- RUNNING: seam pass `matthew-mbp-m4-raid-b1-seam16` (triage `SEAM_TRIAGE_2026-10-04d.md`,
  3d5bea4a7): the green relay's own tick log shows an Entry solo raider spending ten times what
  the guides and the Entry Mode wiki's litmus (:123, at most four food and one dose on Maiden)
  allow: Maiden lands 20 of 21 autos under Protect from Magic for 199 (eleven above the halved
  max of 9), 13 restore presses in 213 ticks; Nylocas 360, Bloat 109, Xarpus 10 bandages + 3
  brew doses. Seams: tob_maiden_entry_prayed_autos, tob_entry_solo_room_costs (content), then
  tob_relay_wiki_kit (the joined relay under five names with the wiki's 6-brew kit; tob_entry
  is authored when 4 of 5 survive). Sourced fixes are expected to move rooms: re-author them.
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
