# Authoring a cutscene from its recording

Stages 1-3 of `PORTING.md`: read the clip into a shot list, solve each shot's
camera against the recording, write the `.rs2` block, and wire the test that
proves it. Read `PORTING.md`'s engine reference first; this file assumes its
units and commands.

Work on ONE cutscene at a time (one `VIDEOS.tsv` row). A quest with several
cutscenes is several passes through this file.

## 1. Set up

```
ID=<test_id>; IDX=<index>
grep -P "^$ID\t" docs/quests/cutscenes/VIDEOS.tsv | awk -F'\t' -v i="$IDX" '$3==i'       # the row
CLIP=$(ls ~/Documents/osrs_cutscenes/$ID/$ID-0$IDX-*.mp4)                                 # x-rows: $ID-x1-*.mp4
cat ~/Documents/osrs_cutscenes/$ID/meta-$(basename $CLIP .mp4).txt                         # the sidecar
grep -P "^$ID\t" docs/quests/cutscenes/TRIAGE.tsv | awk -F'\t' -v i="$IDX" '$4==i'       # wiki trigger, participants, summary
awk -F'\t' -v q="$ID" '$2==q {print $1, $5, $6}' test/quests/QUEUE.tsv                    # quest_dir, tier, status (must be green)
mkdir -p docs/quests/cutscenes/shots
```

Then open the wiki transcript (URL in `../CUTSCENES.tsv`) at the section the
TRIAGE row names, and read the ten lines before and after the scene. You will
need the exact wording of the line that closes as the cutscene begins and the
first line after it ends.

## 2. Read the clip

```
python3 tools/quest_gate/cutscene_port/frames.py "$CLIP" --sheet 1 build/cutscene_sheet/$ID-$IDX-clip.png
```

Read the sheet (one frame a second; the cutscene starts at 3.0 s because of the
padding, and ends 3 s before the clip does). Work through these questions in
order and write the answers down; they become the shot list.

**Where does control leave and return?** The first frame where the camera is
no longer the follow camera (a fade begins, the view jumps, the HUD blanks),
and the first frame where normal play is back. Everything between is the
cutscene; everything outside it is the trigger's ordinary dialogue or play.

**HUD up or hidden?** Inventory panel and minimap present through the scene:
HUD up. Panel empty or gone, minimap black: hidden. Fades to black at the
edges almost always mean hidden. This decides between template A/B and C/D
below.

**How many shots, and what motion?** Compare each frame with the one two
seconds later. If the same landmarks sit in the same places: a hold. If they
have drifted in one direction: a glide (write down which way: the eye rising,
pulling back, sliding sideways, the look-at turning). If the picture is a
different place: a cut. One shot list row per hold, glide or cut. A glide is a
row with a start framing and an end framing. The pilot's cutscene looked like
one fixed shot at a glance and was a twelve-second rising glide.

**What is on screen?** For each shot: landmarks you can find on the map (a
named loc, an npc, a wall, a doorway, water, the chasm edge), and where they
sit in the frame (thirds: top-left, centre, bottom-right). Also the dialogue
box text, any title card, any narration.

**Who acts?** Npcs that walk, animate or speak; the player's own animation;
things that appear (a spotanim, a loc). With the frame range of each beat.

**When does the reset come relative to dialogue?** Often the camera lets go
when a specific page closes; sometimes it holds through the fight that follows.

Write `docs/quests/cutscenes/shots/<test_id>-<index>.md`:

```
# Tears of Guthix, cutscene 1
VIDEOS.tsv: tearsofguthix 1, 3VoeqTrlLYA 0:04:38-0:04:53 (15 s). Clip: tearsofguthix-01-after-agreeing-to-start-the-quest-and.mp4
Trigger: tearsofguthix.rs2 ~tog_juna("But first you will need to make a bowl ...") closes -> cut.  HUD: up.  Fades: none.
Player stands: 3250,9517,2 (at Juna).  Ends: camera returns when ~tog_juna("Mine some stone ...") closes.

| shot | clip s      | ticks | motion | camera                                      | sees                                                          | on screen                              | actors |
| 1    | 3.5 - 5.0   | 2     | cut    | low, close, NE of the rocks, looking SSW     | rock pair lower-left, big rock lower-right, cave wall top      | Juna: "There is a cave on the south..." | none   |
| 2    | 5.0 - 17.0  | 20    | glide  | eye rises ~300 units and pulls back ~3 tiles | same landmarks drift down and shrink; wall line stays on top   | Juna: "There is a cave..." then "Mine some stone..." | none |
| 3    | 18.0        | -     | reset  | follow camera on the player at Juna          |                                                               | chat closed                            |        |

Solved (section 4):
  shot 1 start: cam_moveto(2_50_148_37_31, 400, 100, 100); cam_lookat(2_50_148_31_25, 0, 100, 100);   -- candidate S2
  shot 2 end:   cam_moveto(2_50_148_39_33, 700, 1, 1);                                                 -- candidate P, glide
```

Every row needs `ticks` (seconds / 0.6), a `motion`, and a `sees` that names
map things. If a row's `sees` is only "dark cave", go back to the frames.

## 3. Find the trigger and the landmarks

**The trigger.** Grep the dialogue on screen at the cut:

```
grep -rn "There is a cave on the south" OSRS-Content/osrs239-content/server/scripts/quests/quest_tearsofguthix/
```

The cutscene goes between the page that closes as the video cuts and the page
that shows during the first shot. If the port has no such dialogue, the quest
is missing a leg: stop, and put `needs: <file>: <leg>` in your report.

**Where the player stands.** The tile the player is on during the scene,
usually beside the npc they talked to. The test's `goto_tile` or a
`world.tile()` check near that step gives it; so does the npc's spawn in the
quest's `configs/*.npc` or `areas/world/configs/m<mx>_<mz>.spawn`.

**Landmark tiles.** Loc symbols the quest already uses (grep the test for
`click_loc` and `by_symbol`) resolve to tiles through the solve tool's
`--landmark`. Npc tiles come from spawn files or a `t.npc.nearest` detail in
an existing run. Terrain features (a chasm edge, a wall) are located by
elimination in the solve rounds.

## 4. Solve the camera

One command per round, up to four candidates, about ten seconds:

```
python3 tools/quest_gate/cutscene_port/camsolve.py --quest $ID --stand <x,z,level> \
    --clip "$CLIP" --at <clip seconds of the frame to match> \
    --cand A=<eye x>,<eye z>,<eye h>\><look x>,<look z>,<look h> --cand B=... --cand C=... --cand D=... \
    --landmark <loc symbol> --landmark <loc symbol>
```

It prints each candidate's resolved yaw and pitch and the `cam_*` lines, and
writes `build/quest_gate/camsolve_<test_id>/compare.png`: the recording's frame
first, then each candidate, all as the whole 1024x768 client canvas. Every
round's image is also kept as `build/quest_gate/camsolve_<test_id>.rounds/compare_NN.png`
(the run directory itself is wiped each run). Read the image every round.

**For a hidden-HUD cutscene (template C/D) add `--hud hidden`.** The cutscene
runs with `%cutscene_status`/`%fov_clamp` set and the panel closed, which
draws a wider view than the HUD-up client; a candidate solved with the HUD up
comes out smaller and higher in the real frame (Making History return 1).

**Round 1: direction.** Four compass positions around the main landmark at one
height and distance, look-at on the landmark. For an open area 8 tiles away
at height 600 and look-at height 0 is a fair start; for a building start 12-15
tiles away with the look-at at height 250-350 (its walls), or the walls fill
the frame and the pitch clamp pushes the roof to the top. Pick the one whose arrangement matches: what is behind what, which
side the wall or water is on. Orientation rule: the client faces north at yaw
0, so a camera looking south shows east on the LEFT of the frame.

**Round 2: distance and height.** Closer or farther changes how much floor is
visible and how big the landmark is. Height with distance sets the pitch:
roughly `atan(height / distance)`, a tile being 128 units, and the client
never goes below pitch 128 (about 22 degrees). Two consequences: a landmark
that should sit in the UPPER third with sky above it needs a far, high eye
(h1000-1300 at 25-35 tiles); a near landmark that climbs too high in the
frame wants a higher look-at (its walls) or a closer eye. Slide a tile or two
sideways to put the landmark in the right third.

**Rounds 3-4: refinement.** One tile and 50-100 units at a time. Stop when the
same landmarks occupy the same thirds and the wall or horizon line sits at the
same height. Pixel agreement is not achievable (different client build, zoom
and lighting) and not the goal; a reviewer compares regions, not pixels.

**A glide** is two solves: the start framing (`--at` the frame just after the
cut) and the end framing (`--at` the frame just before the reset or the next
cut). Then pick the rate from the duration with `PORTING.md`'s formula: `1, 1`
is about 10 s over 650 units, `2, 1` about 6.7 s over 800, `4, 2` about 5 s
over 1400, `100, 100` a cut. A cut and a glide may be sent in the same tick. Preview a glide by passing
`--speed 1,1` with the END framing as the candidate after the start has been
framed by an earlier candidate in the same round; the tool waits 12 ticks
before photographing a glide.

**Several shots** are several `--at` values; solve each in its own rounds and
write each into the shot list.

Copy the winning lines, exactly as printed, into the shot list's "Solved"
block.

## 5. Write the cutscene

Edit the quest's `.rs2` at the trigger. Use an exact-string edit (the Edit
tool or a careful `sed` on a unique line), then read the block back and run
`git -C OSRS-Content diff osrs239-content/server/scripts/quests/<quest_dir>`
(the shared tree carries other workers' edits; diff only your quest) before
building. A replacement that swallows a
neighbouring line produces a script that runs but skips pages, and the trace
of that looks like an engine fault.

Put a comment above the block: what it shows, the `VIDEOS.tsv` row and video
id, the solve candidates, and the shot list path, as the pilot does at
`quest_tearsofguthix/scripts/tearsofguthix.rs2:145-156`.

**Template A. Camera only, held across dialogue pages (HUD up).**

```
~tog_juna("But first you will need to make a bowl in which to collect the tears.");
// Cutscene: <what it shows>. Solved against VIDEOS.tsv tearsofguthix 1 (3VoeqTrlLYA 0:04:38-0:04:53):
// start = candidate S2, end = candidate P; shot list docs/quests/cutscenes/shots/tearsofguthix-1.md.
cam_moveto(2_50_148_37_31, 400, 100, 100);
cam_lookat(2_50_148_31_25, 0, 100, 100);
cam_moveto(2_50_148_39_33, 700, 1, 1);       // the glide; runs while the pages are read
~tog_juna("There is a cave on the south side of the chasm ...");
~tog_juna("Mine some stone from that cave ...");
cam_reset;
```

**Template B. Camera only, timed (HUD up, no pages).**

```
cam_moveto(<eye>, <h>, 100, 100); cam_lookat(<look>, <h>, 100, 100);
p_delay(<ticks of shot 1> - 1);          // p_delay(n) holds n+1 ticks
cam_moveto(<eye 2>, <h>, 100, 100); cam_lookat(<look 2>, <h>, 100, 100);
p_delay(<ticks of shot 2> - 1);
cam_reset;
```

In template C the fade-out and fade-in cost about 3 ticks each; take them off
the first and last shots' holds so the whole sequence lasts what the recording
lasts. Check the sum against the ledger's keyframe ticks after the first run.

**Template C. HUD hidden, fades, narration, flyover.** The canoe recipe from
`PORTING.md` around the shots. Each narration box is a `~mesbox` between cuts
(its click replaces a `p_delay`); a pan is a `cam_moveto(..., 1, 1)` or
`(2, 1)` followed by `p_delay(<its ticks>)`. Restore every varbit and sub you
changed, in the order the recipe shows, before the final fade-in.

**Template D. Actors.** Template C plus, before the first cut, `npc_add` /
`npc_setowner` / `npc_setmode(none)` for each actor (spawn them off-screen or
under the fade), then beats of `npc_walk` / `npc_anim` / `npc_say` with
`p_delay`s from the shot list, and `npc_del` (or a lifetime that ends after
the reset) at the end. Two actors: the `.npc_*` prefix for the second.

**Elsewhere.** A far-away scene in the live world: `remote_view_start` around
template B or C. A flashback in a place the player must not really be: a map
instance, then teleport in, `p_delay(1)`, frame, and release after.

Then:

```
make -C src torirsserver-scripts       # compiles the whole pack; fix anything it prints as an error
```

## 6. Assert it in the test

`docs/quest_authoring/verbs-cutscene.md` is the full reference. The shape that
works, from the pilot (`test/quests/tearsofguthix.lua`):

```lua
local bowl_cutscene_mark = t.cutscene.mark()             -- BEFORE the row whose last click fires the packets
t.exec("tog.accept_dialog", t.chat.play, {
    ... ,
    "npc:But first you will need to make a bowl in which to collect the tears.",
})
t.exec("tog.bowl.page1", t.chat.play, {
    "npc:There is a cave on the south side of the chasm that is similarly infused",
})
t.ticks(8)                                               -- hold on the page while the glide runs, so the shots sample it
local glide_cam = t.world.camera()
t.check("tog.bowl.glide", glide_cam ~= nil and glide_cam.server_driven == true,
    "camera 8 ticks into the glide: eye " .. tostring(glide_cam and glide_cam.x) .. "," .. tostring(glide_cam and glide_cam.z)
        .. " pitch " .. tostring(glide_cam and glide_cam.pitch) .. " last_op " .. tostring(glide_cam and glide_cam.last_op))
t.exec("tog.bowl.page2", t.chat.play, {
    "npc:Mine some stone from that cave, make it into a bowl, and bring it to me",
    "end",
})
t.exec("tog.bowl.cutscene", t.cutscene.await, "tog.bowl", { since = bowl_cutscene_mark, expect = {
    { op = "moveto", coord = "2_50_148_37_31", height = 400 },   -- tearsofguthix.rs2, copied verbatim
    { op = "lookat", coord = "2_50_148_31_25", height = 0 },
    { op = "moveto", coord = "2_50_148_39_33", height = 700 },   -- the glide (1, 1)
    { op = "reset" },
} })
```

Rules:

- **Name every row of the cutscene with one prefix** (`tog.bowl.page1`,
  `tog.bowl.glide`, `tog.bowl.page2`, `tog.bowl.cutscene`). The comparison
  sheet collects the shots of every row sharing the await row's prefix.
- **Mark before the trigger.** The packets fire when the page BEFORE the cut is
  clicked away, at the end of the previous row; without `since` the await
  starts too late and answers `no_cutscene ... (serial N -> N+1)`.
- **Hold where the recording holds.** A `t.ticks(n)` and a `t.check` with a
  `t.world.camera()` detail between pages photographs the glide and records
  the eye's tile and pitch in the ledger.
- **Calibrate hold rows against the ledger.** Every `t.check`/`t.shot` row
  costs a tick or two of its own, so a planned hold at "+N ticks" lands later
  than N. After the first run, read the await detail's keyframe ticks and the
  rows' `ticks` column (they share a clock) and move the holds so each lands
  inside the shot it is meant to photograph; a hold meant for the closing fade
  that lands after the reset photographs the restored HUD instead.
- **Photograph the fades** of a hidden-HUD cutscene: one hold right after the
  trigger row (the black fade-out) and one just before the reset (the closing
  fade), so the HUD rubric line can be graded from the sheet.
- **Copy `expect` literals from the `.rs2`**, coord and height. Height must
  match within 1.
- A cutscene with no pages (templates B/C/D) has two workable shapes:
  - the trigger row, then the await with `shots = "all"` **immediately**: the
    await follows the sequence as it plays, photographs every keyframe as it
    arrives, and returns at the reset; or
  - the trigger row, then `t.ticks(n)` + `t.check` rows (with a
    `t.world.camera()` detail) at the moments you want photographed, then the
    await. The await then reads the ring after the fact and takes no shots of
    its own (`shots=none (the sequence ran inside the trigger row)`); the
    `t.check` rows are the pictures.
  Either way the page that FOLLOWS the cutscene must not be played until the
  reset: a `chat.play` issued mid-flyover answers `no dialogue is open`. The
  first shape handles that (the await returns at the reset); the second needs
  `t.ticks` summing to the cutscene's length first.
- A `cam_reset` in the middle of a sequence is two awaits, one per reset
  (`test/quests/arena.lua:157-181`).

Run at the resizable canvas and grade:

```
TORIRS_ROOT_SIZE=1024x768 python3 tools/quest_gate/run.py --script test/quests/$ID.lua --name $ID --no-build --no-publish
grep -E "\.cutscene|SUMMARY" build/quest_gate/$ID/ledger.tsv
python3 tools/quest_gate/gate.py $ID                       # must end "gate: 1 quest(s) green"
```

The await row's detail lists the keyframes with ticks:
`#1 t=68 moveto 3237,9503 h=400 s=100/100 | #2 t=68 lookat ... | #3 t=68 moveto 3239,9505 h=700 s=1/1 | #4 t=78 reset`.
Check the tick gaps against the shot list's `ticks` column.

## 7. Hand over

Add the `PORTS.tsv` row, build the sheet, and follow `REVIEW.md` section 1 to
grade your own work before you report. The report names: the shot list path,
the `.rs2` lines, the test rows, the ledger's cutscene detail, the sheet path,
and anything you could not match (a landmark you never found, a pan whose
speed is a guess, a page the port lacks).

**When a reviewer returns a shot.** Apply the return list, but verify every
claim about the recording against frames at 0.5 s steps around the moment
named before changing the shot list: a reviewer reads the same sheet you do
and can misread a cut (Making History's reviewer saw a second cut at 35 s
that the frames show is the hut leaving the frame at 32.5 s). Say in your
report where you disagreed and what the frames showed.

**When the port's dialogue is paraphrased or a leg is missing.** The cutscene
is still ported at the equivalent point; the shot list's "on screen" column
quotes the transcript; the PORTS status is `approximated`; and the report
carries `needs: <file>: <the transcript's line or leg>`. Never reword the
existing dialogue yourself.

## When it goes wrong

| Symptom | Cause | Fix |
|---|---|---|
| `no_cutscene ... (serial N -> N+1)` | packets fired before the await's default start | `t.cutscene.mark()` before the trigger row; `since =` |
| `unfinished` | no `cam_reset`, or the await ran while pages still wait for clicks | add the reset; play the pages on their own rows before the await |
| `not_found: expected keyframe ...` | `expect` differs from the `.rs2` | copy the literal again; height within 1 |
| pages vanish, several ops and the reset land in one tick | your edit removed the page lines | `git -C OSRS-Content diff`; restore them |
| black or wrong view in a solve | tile outside the loaded scene | `--stand` where the real player stands; or template C's remote view or teleport |
| candidate never moves (`server_driven` false) | scene rebuilt after the packets | teleport, `p_delay(1)`, then camera |
| pitch stuck at 128 | eye too low for its distance | raise the eye or come closer; the recording obeys the same clamp |
| the glide finishes too fast or slow | rate maths | `1,1` about 5 s per 650 units; halve the rate to double the time |
| page shots marked `[frame unchanged]` | a still camera between shots | fine; the row still counts |
| gate red: `cutscene_row_required` names a site | a `cam_*` call has no covering keyframe | every call site needs an await; a branch the test does not drive needs a `-- GUIDE-GAP` and the branch driven or the call moved |
| sheet has no game frames | `PORTS.tsv` `ledger_step` is not the await row's exact name, or the rows do not share its prefix | rename the rows |
