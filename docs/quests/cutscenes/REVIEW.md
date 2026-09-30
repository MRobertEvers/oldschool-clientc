# Verifying and reviewing a cutscene port

Stage 4 of `PORTING.md`. Two readers use this file: the author, who grades
their own port before handing it over (section 1), and the reviewer, who
decides whether it lands (sections 2-4). The automatic checks prove that a
cutscene plays with the right camera packets; only a person looking at the
comparison sheet can say whether it is the right cutscene.

## 1. Build the evidence

**The PORTS row.** Append to `docs/quests/cutscenes/PORTS.tsv`:

```
test_id	index	ledger_step	rs2_site	clip	status	note
tearsofguthix	1	tog.bowl.cutscene	quests/quest_tearsofguthix/scripts/tearsofguthix.rs2:cam_moveto(2_50_148_37_31	tearsofguthix-01-after-agreeing-to-start-the-quest-and.mp4	ported	camera-only: cut to a low view then a glide (rate 1,1) ... solved in 5 rounds
```

- `ledger_step` is the exact name of the `t.cutscene.await` row.
- `rs2_site` is the file and the first `cam_moveto` literal, so the reviewer
  can grep to the block.
- `status`: `ported` (everything in the recording is in the port), `partial`
  (the note says what is missing), `approximated` (a pan speed or a framing is
  a guess; the note says which).
- `note`: kind of cutscene, how many solve rounds, what is approximate, and
  after publishing, the sheet's artifact link.

**The run.** The quest test at the resizable canvas, then the gate:

```
TORIRS_ROOT_SIZE=1024x768 python3 tools/quest_gate/run.py --script test/quests/<id>.lua --name <id> --no-build --no-publish
python3 tools/quest_gate/gate.py <id>
```

Both must be green. If the quest's test does not pass at 1024x768 for reasons
unrelated to the cutscene, run it at the default 765x503 and say so in the
note; the sheet then crops the viewport.

**The sheet.**

```
python3 tools/quest_gate/cutscene_port/build_cutscene_sheet.py build/cutscene_sheet/<batch> <id> [<id> ...] --title "Cutscene Ports: <batch>"
```

`index.html` plus its `.webp` files; open it, or publish the directory as an
artifact (the page, `files` = every `.webp`). Put the link in the PORTS note.
Per cutscene the page shows the recording's frames (start, each keyframe's
time, every 4 s, the end) above the test's shots for every row that shares the
await row's prefix, both as the client's world picture in the same layout.

## 2. The rubric

Grade each cutscene on six lines. A port lands when every line is `yes` or
has a `partial` note the reviewer accepts.

| Line | Question | How to answer it |
|---|---|---|
| Presence | Does the cutscene play at the moment the recording plays it, and end where it ends? | The await row's first keyframe follows the trigger row named in the shot list; the reset keyframe's tick matches the shot list's end; the pages before and after are the transcript's |
| Framing | For each shot, do the same landmarks sit in the same thirds of the frame, with the same thing behind them? | Compare the sheet's recording frame and game frame for that shot region by region: top-left, centre, bottom-right; is the wall on the same side, the water at the same edge |
| Motion | Does the camera move the way the recording's does: cut where it cuts, glide where it glides, in the same direction and about the same duration? | The keyframes' `s=rate/rate2` (100/100 is a cut), the tick gap between keyframes, and the sheet's mid-glide frames (the `t.ticks` + `t.check` rows) against the recording's frames at the same offsets |
| HUD | Is the interface in the same state: up, or hidden with fades? | The game frames show the panel and minimap as the recording does; a hidden-HUD port shows the fade frames black at both ends |
| Words | Are the narration, title cards and dialogue the transcript's, in order? | The page shots' text against the transcript; the shot list's "on screen" column |
| Actors | Do the same npcs appear, walk, animate and speak at the same beats? | The game frames at each beat; the ledger's tick gaps against the shot list |

A `no` on Presence or HUD is a rejection. A `no` on Framing, Motion or Actors
is a return to `AUTHORING.md` section 4 or 5 with the specific shot named. A
`no` on Words is a content fix (copy the transcript).

What is NOT a defect: differences in lighting or fog, texture detail, the
exact zoom, a landmark a few pixels off, the recording's other players. The
recording is a different client build.

Known engine gaps, graded `partial` and not returned to the author: flyover
narration shows in the chatbox rather than as top-of-view text, and the
hidden-HUD recipe also hides the chatbox the recording keeps
(`PORTING.md`, Narration). Dialogue the port paraphrases or lacks around the
cutscene is a `needs:` line for the quest's parity work, not a cutscene
defect; the cutscene lands as `approximated` if everything else holds.

## 3. The reviewer's checklist

Work through it in this order; the early items are cheap and catch the common
failures.

1. **The diff is only the cutscene.** `git -C OSRS-Content diff <quest_dir>`
   adds a commented camera block and changes nothing else; no dialogue line
   was removed or reworded. (The pilot lost two pages to a careless edit; the
   script then ran three camera ops and a reset in one tick.) The comment
   names the `VIDEOS.tsv` row, the video id and range, and the shot list.
2. **Every camera op is in the `expect`.** Count `cam_moveto`/`cam_lookat` in
   the block; the await row's `expect` has each one with the same coord and
   height, and a `reset`. `gate.py` enforces coverage, but also check no
   `cam_*` was left outside the block (a stray reset elsewhere).
3. **The block ends with `cam_reset`** and, for a hidden-HUD cutscene,
   restores `%cutscene_status`, `%minimap_state`, `%fov_clamp` and reopens
   `orbs` and `popout`, and closes the fade overlay. A player left without a
   HUD or with a stuck camera is a rejection.
4. **The ledger.** `SUMMARY ... PASS`; the await row PASS with
   `cutscene: N keyframes ... reset=yes`; the rows around it PASS; `gate.py`
   green; no other row changed verdict against the last committed run
   (compare with `git show HEAD:...` of the test if in doubt).
5. **The sheet, shot by shot**, with the rubric. Look at the recording frame
   and the game frame for the same shot side by side; say what is in each
   third. Write one line per shot in the review.
6. **Timing.** Keyframe tick gaps against the shot list's `ticks`; the mid-
   glide `t.check` detail's eye tile against the shot list's glide direction.
7. **The shot list exists** at `docs/quests/cutscenes/shots/<id>-<index>.md`
   and its "Solved" block matches the `.rs2`.
8. **The PORTS row** is present, `ledger_step` names the await row exactly,
   `status` is honest (a guessed pan is `approximated`, not `ported`), the
   sheet link is in the note.
9. **The ledger in `../CUTSCENES.tsv`**: when every cutscene of the quest is
   `ported` in PORTS.tsv, the quest's `ported` column is `yes`; otherwise it
   stays `no`.
10. **The sweep.** `python3 tools/quest_gate/cutscene_sweep.py --repo . | grep " <id> "`
    reads `WIKI_PORTED` (or `MATCH` for a LostCity quest).

## 4. Regression rules

- A port is committed together with its test rows; never one without the
  other (the gate would go red on the next run).
- The quest's test must stay green at its usual canvas too: run it once at
  765x503 before landing if the cutscene rows do anything layout-sensitive
  (they normally do not).
- `make -C src check-quest-cutscenes` must still pass (no DROPPED/PARTIAL).
- When a later change to the quest's dialogue moves the trigger, the mark and
  the page rows move with it; the await's `expect` does not change unless the
  camera does.
- Do not "fix" a mismatch by loosening the `expect` (dropping a coord) or by
  removing a `cam_*` op; fix the framing or the shot list.

## 5. What the reviewer writes

For each cutscene: the six rubric lines with `yes`/`partial`/`no` and one
sentence each, then a verdict: `land`, `return: <shot, line>` or `reject:
<reason>`. Record the verdict in the PORTS note (`reviewed <date> land`), and
for a batch, in `test/quests/BATCHES.tsv` the way quest batches are recorded,
with the sheet's artifact link.
