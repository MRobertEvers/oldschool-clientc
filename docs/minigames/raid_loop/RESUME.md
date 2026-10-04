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

## State on 2026-10-04 07:50

- Mode tested: Theatre of Blood, ENTRY, SOLO only. Normal, Hard and party play are untested.
- Rooms kept (pass `matthew-mbp-m4-raid-b1-rooms-tob`): tob_maiden 3eb2a8cf8, tob_bloat
  e96af7766, tob_xarpus f417148e4, tob_sotetseg 812267e7f, tob_nylocas d87427d41.
  tob_verzik is committed blocked at 140 of 142 (7bd321420); its 142-row attempt was sent back
  (the raider died after her; a tornado row read a touch from after the kill) and is saved as
  `build/author_state/matthew-mbp-m4-raid-b1-rooms-tob/tob_verzik.sampled_l12_9b9b34b22.lua`.
- Running: seam pass `matthew-mbp-m4-raid-b1-seam15` (triage `SEAM_TRIAGE_2026-10-04c.md`):
  super restore no longer heals Hitpoints (owner confirmed), Verzik's tornado removed at her
  death, `::give` exact symbol -- all three fixed on disk, uncommitted; the joined whole-raid
  relay (notice board to vault, one kit, no cheat) was mid-flight; then its closer.

## Next, in order

1. Seam15 lands. Expect the super-restore fix to break rooms and maybe quests that healed
   from restores: re-author the rooms; list the quests for the quest session (never revert).
2. Room launch: tob_verzik (no death in the room, tornado touch before her death) and any
   room seam15 moved.
3. `tob_entry`: author the whole-raid test from the joined relay scratch, graded on
   `encounters/raidwide.tsv` (75 rows in scope for Entry solo).
4. Normal mode room pass, then Hard, then party play (needs a second driven client).
5. The frame-count pass for D/E tick rows (`tools/raid_gate/frame_diff.py`; the first pilot
   measured nothing).
6. Tombs of Amascut spec pass (corpus pinned), then Chambers of Xeric.

## Waiting on the owner

- The nine stray files a seam1 fixer wrote into the main checkout (restore was denied).
- The raid content worktree's old dirt under `selftest/quests/quest_cook/play` and
  `quest_druid/play` (restore was refused; never staged).
- Whether `docs/WAVES_ORCHESTRATOR.md` belongs on v3.
