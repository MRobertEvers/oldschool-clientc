# Worker brief: port one quest's cutscenes

You are porting the recorded cutscenes of ONE quest into this engine and
proving them. You have never seen this engine: the manuals below tell you
every command and file. Read them fully before touching anything, in this
order: `PORTING.md` (engine reference), `AUTHORING.md` (the steps),
`REVIEW.md` section 1 (what you hand over). The pilot's files are your
worked example: `shots/tearsofguthix-1.md`, the block at
`OSRS-Content/osrs239-content/server/scripts/quests/quest_tearsofguthix/scripts/tearsofguthix.rs2`
(grep `cam_moveto`), the rows named `tog.bowl.*` in `test/quests/tearsofguthix.lua`,
and `PORTS.tsv` row 1.

Repo: `/Users/matthewevers/Documents/git_repos/3draster`. Run everything from
there with `python3`. Clips are under `~/Documents/osrs_cutscenes/<test_id>/`.

## Your quest

`<test_id>` -- given in the task. Its cutscenes are the `VIDEOS.tsv` rows with
that `test_id` and `status` = `found`; do them in index order. `x`-rows are
scenes found on video that the wiki did not list; port them too.

## Order of work, per cutscene

1. Set up and read the transcript (`AUTHORING.md` 1).
2. Contact sheet of the clip; write the shot list, motion row by row
   (`AUTHORING.md` 2). Do not skip the "compare each frame with the one two
   seconds later" test; the pilot misread a glide as a cut without it.
3. Find the trigger line in the `.rs2` and the player's tile and landmarks
   (`AUTHORING.md` 3).
4. Solve the camera with `camsolve.py`, four candidates a round, reading the
   compare image every round, until the landmarks sit in the same thirds
   (`AUTHORING.md` 4). Two solves for a glide (start and end).
5. Write the block at the trigger with an exact-string edit; read it back;
   `git -C OSRS-Content diff`; `make -C src torirsserver-scripts`
   (`AUTHORING.md` 5).
6. Add the test rows with one shared prefix, the mark before the trigger, the
   holds, and the await with verbatim `expect`; run at
   `TORIRS_ROOT_SIZE=1024x768`; `gate.py` green (`AUTHORING.md` 6).
7. PORTS row; build the sheet; grade yourself with the rubric
   (`REVIEW.md` 1-2). A `no` on Framing or Motion goes back to step 4 or 5.

## Rules

- Never remove or reword existing dialogue. Camera lines go between pages;
  the pages stay.
- Never loosen an `expect` to make a row pass, and never drop a `cam_*` op
  to satisfy the gate.
- Never edit files outside: the quest's `scripts/*.rs2`, `test/quests/<test_id>.lua`,
  `docs/quests/cutscenes/shots/<test_id>-*.md`, `docs/quests/cutscenes/PORTS.tsv`.
  If the port lacks a dialogue leg the cutscene needs, stop that cutscene and
  write `needs: <file>: <what>` in your report.
- Do not commit. Do not run other quests' tests. Do not edit `core.lua` or any
  driver file.
- Every number you write (a coord, a height, a tick count) comes from the
  tool's printout, the shot list's frame timings, or the transcript, never
  from memory of the game.

## Report

Return, in this order:
1. Per cutscene: the shot list path; the `.rs2` file:line range of the block;
   the test row names; the await row's `cutscene:` detail from the ledger; the
   solve rounds run; the sheet path; your rubric grades with one sentence
   each.
2. `gate.py <test_id>` output's last line.
3. Anything approximate or missing, and every `needs:` line.
4. Anything in the manuals that was wrong, missing or unclear when you
   followed it, quoted, so the manuals can be fixed.
