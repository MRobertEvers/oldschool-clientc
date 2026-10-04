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

## Relaunching seam15 if it is not closed

A background pass dies with the session that launched it. If
`build/seam_state/matthew-mbp-m4-raid-b1-seam15/close.json` does not exist, relaunch the card
`tools/raid_gate/workflows/raid_seam.workflow.js` with args `pass`
`matthew-mbp-m4-raid-b1-seam15`, `reuse_triage` `docs/minigames/raid_loop/SEAM_TRIAGE_2026-10-04c.md`,
`width` 1, and this `context` (one string):

> Seam15 of the raid loop, relaunched by a fresh session. On disk: every seam with a
> fix.<key>.json report is DONE and its edits are uncommitted in the tree; a seam with only a
> progress notebook resumes from it. EVERY FIXER reads its seam's full section in
> docs/minigames/raid_loop/SEAM_TRIAGE_2026-10-04c.md and the rules above the first section.
> One worker at a time; no command prints more than about 4 KB; if the editor's Read or Write
> hook times out, use the shell (sed -n ranges, python3 or heredocs under 8 KB) and never loop
> on the failing tool. The six room tests under test/raids/ are read-only (five KEPT:
> tob_maiden 3eb2a8cf8, tob_xarpus f417148e4, tob_bloat e96af7766, tob_sotetseg 812267e7f,
> tob_nylocas d87427d41; tob_verzik blocked at 7bd321420). THE CLOSER: (1) re-runs the six
> rooms on the final tree with run.py --no-build --no-publish and gate.py (--allow-blocked for
> tob_verzik), logs to files; the super-restore fix is EXPECTED to break rooms that healed from
> restores: record 'tob_<room> must be re-authored: <what moved>' in SEAM_LEDGER.md, never
> revert a sourced fix; a room broken for an unsourced reason reverts that seam. (2) Quest
> suite: baseline 115 green plus deserttreasure, regicide, troll, forgettabletale RED (fixed on
> v3); the owner confirmed on 2026-10-04 the super restore must be fixed, so a quest that goes
> red because its fight healed from restores is listed with its first failing row in
> SEAM_LEDGER.md and MERGE_CHECKLIST.md under 'The super restore no longer heals Hitpoints',
> not reverted; any other regression reverts its seam. (3) Runs the joined relay scratch under
> three run names and writes survival and supplies left into SEAM_LEDGER.md as the verdict on
> authoring tob_entry. (4) Recipes into DRIVER_NOTES.md ('The whole raid in one run'), handed
> spec rows applied (spec_check.py clean, nothing loosened without its quoted source line),
> CONTENT_BUGS.md updated, one line per seam in SEAM_LEDGER.md with the quest suite,
> conformance and C selftest results. Never stage or restore the content worktree's dirt
> under selftest/quests/quest_cook/play and quest_druid/play. QUEST_HELPER_ROOT is the
> absolute /Users/matthewevers/Documents/git_repos/quest-helper.
