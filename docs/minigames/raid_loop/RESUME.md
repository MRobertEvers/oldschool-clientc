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

## State on 2026-10-04, after seam15

- Mode tested: Theatre of Blood, ENTRY, SOLO only. Normal, Hard and party play are untested.
- Seam15 LANDED (parent 53a8aa8e0, OSRS-Content 946c357a71; ledger bab6fbf19, bf7ca2f11): the
  super restore no longer heals Hitpoints, Verzik's tornado is removed at her death, `::give`
  takes the exact symbol, the Entry chest keeps its leftover bandages (10 in all). Gates: quest
  suite at the baseline (115 green + deserttreasure, forgettabletale, regicide, troll RED; no
  quest moved), conformance 324/324, server C selftest at its baseline 11 failures.
- Rooms kept and green on the seam15 tree (pass `matthew-mbp-m4-raid-b1-rooms-tob`): tob_bloat
  e96af7766, tob_xarpus f417148e4, tob_sotetseg 812267e7f, tob_nylocas d87427d41.
- Rooms to re-author (launch 13 of the same pass; their author/review state is moved aside as
  `*.launch12_seam15.*.bak`, sample.json carries the note): tob_maiden (109/110, hit_sound read
  the end-of-tick tile on a step from 13 tiles to 12; measure from the tile at tick - 1) and
  tob_verzik (out of food in P3 at 636 with no heal from restores; also still owes: no death in
  the room, the tornado touch before her death; the sent-back 142-row attempt is
  `build/author_state/matthew-mbp-m4-raid-b1-rooms-tob/tob_verzik.sampled_l12_9b9b34b22.lua`).
- Joined whole-raid relay (`build/seam_state/matthew-mbp-m4-raid-b1-seam15/trj/joined.lua`):
  green under 1 of 5 run names (s15k1 160/160, the raider at 8 hp with every dose drunk); the
  other four die at Xarpus or in Verzik P3. Verdict: do NOT author tob_entry as a must-survive
  test yet. Run names: only the first 12 characters seed a run (jbase37), case folded.

## Next, in order

1. Room launch 13: tob_maiden and tob_verzik (card `raid_author`, pass
   `matthew-mbp-m4-raid-b1-rooms-tob`, raid `tob`, all six rooms, mode `entry`, width 1). If it
   died with its session, relaunch with the same args; the state directory is the resume.
2. A seam pass for the relay's margin before `tob_entry`: the levers are the scratch's (P2/P3
   damage, melee P2; restores in place of the super combat and stamina for the second half; the
   Nylocas room decides the kit that reaches Sotetseg, 12-21 brew doses against the 18 the green
   run needed). Nothing sourced may be lowered. Then author `tob_entry` from the joined relay,
   graded on `encounters/raidwide.tsv` (75 rows in scope for Entry solo).
3. Normal mode room pass, then Hard: BOTH WITH THREE PLAYERS (owner, 2026-10-04: "For normal
   and hard mode, you will need 3 players"). Solo stays Entry only. Nothing drives more than
   one client today (DRIVER_NOTES.md has no multi-client verb; `::tobscale <n>` only restates
   a boss for a party of n), so a seam pass comes first: three driven clients in one run, one
   party through the notice board, each raider's own readouts and tick log. Triage it from
   the party rows in `encounters/*.tsv` before launching it.
4. The frame-count pass for D/E tick rows (`tools/raid_gate/frame_diff.py`; the first pilot
   measured nothing).
5. Tombs of Amascut spec pass (corpus pinned), then Chambers of Xeric.

## Waiting on the owner

- The nine stray files a seam1 fixer wrote into the main checkout (restore was denied).
- The raid content worktree's old dirt under `selftest/quests/quest_cook/play` and
  `quest_druid/play` (restore was refused; never staged).
- Whether `docs/WAVES_ORCHESTRATOR.md` belongs on v3.
