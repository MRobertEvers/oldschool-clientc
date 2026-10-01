# Porting recorded cutscenes: overview and engine reference

A recorded OSRS cutscene becomes content in this engine in four stages, each
with a file it produces and a check it must pass. The other documents in this
directory are the stage manuals:

| Stage | Manual | Produces | Passes |
|---|---|---|---|
| 1. Read the recording, write the shot list | `AUTHORING.md` sections 1-3 | `shots/<test_id>-<index>.md` | every shot has a framing, a motion and a duration in ticks |
| 2. Solve the camera against the recording | `AUTHORING.md` section 4 | `cam_moveto`/`cam_lookat` lines in the shot list | the compare sheet shows the same landmarks in the same regions |
| 3. Write the cutscene and assert it | `AUTHORING.md` sections 5-6 | the `.rs2` block, the test rows | `make -C src torirsserver-scripts`, `gate.py <test_id>` green |
| 4. Compare, review, record | `REVIEW.md` | the comparison sheet artifact, `PORTS.tsv` row | the reviewer's rubric |

`WORKER.md` is the one-page brief handed to a worker for one quest.

Second pilot, Making History (a Sonnet worker following these manuals cold):
a 35 s HUD-hidden flyover with five shots and narration, 7 solve rounds, 12
keyframes asserted, gate green, status `approximated`. It then went through
one review cycle (`REVIEW.md`): a second Sonnet returned shots 1, 3, 4, 5
with directions, and the author's return pass (26 rounds, 18 keyframes) fixed
shots 1-3 and 5b and left 4 and 5a `partial`, correcting the reviewer on one
claim (no second cut at 35 s) by reading the frames at 0.5 s. Both agents'
manual corrections are folded in here and in `AUTHORING.md` and `REVIEW.md`.

This was piloted on Tears of Guthix (2026-09-30, `PORTS.tsv` row 1). The pilot
took five camera-solve rounds of about ten seconds each and found two things
worth knowing before you start:

- **The first port read a rising glide as a cut.** Frames two seconds apart
  looked alike at a glance; the comparison sheet showed the drift. Read motion
  deliberately (`AUTHORING.md` section 2).
- **A blind text replacement deleted two dialogue pages.** The script then ran
  three camera ops and a reset in one tick with no pages between, and the
  first suspect was the engine. It was the edit. Edit a script with an exact
  old-string replacement, read the block back, and `git diff` it before you
  build.

## Inputs

| What | Where |
|---|---|
| Video, range, source, wiki summary, how the range was identified | `VIDEOS.tsv`: one row per cutscene, keyed `test_id`, `index`; `x1, x2...` are scenes found on video the wiki did not list. Its `client` column is unreliable (both pilots said "fixed" for resizable footage): judge the layout from the frames |
| The clip (1080p, 3 s padding either side) and its sidecar | `~/Documents/osrs_cutscenes/<test_id>/<test_id>-<index>-<slug>.mp4` and `meta-<same>.txt`; mirror on `/Volumes/MatthewLLM_Shared/osrs_cutscenes/` |
| The wiki's trigger, participants, description, evidence line | `TRIAGE.tsv` |
| The wiki transcript | the `source` URL in `../CUTSCENES.tsv` (with revision); `Transcript:<Quest>` |
| The quest's scripts | `OSRS-Content/osrs239-content/server/scripts/quests/<quest_dir>/scripts/*.rs2` |
| The quest's test | `test/quests/<test_id>.lua`; it must be `green` in `test/quests/QUEUE.tsv` |
| What is already ported | `PORTS.tsv` |

## Tools

All from the repo root with the repo's `python3` (`.venv`; `imageio-ffmpeg` is
installed there for frame decoding).

| Tool | Does |
|---|---|
| `tools/quest_gate/cutscene_port/frames.py <clip> --sheet <step> <out.png>` | contact sheet of the clip, one frame per `step` seconds, timestamps burned in |
| `tools/quest_gate/cutscene_port/frames.py <clip> <seconds> <out.png>` | one frame |
| `tools/quest_gate/cutscene_port/camsolve.py ...` | photographs up to four candidate camera framings next to a video frame in one ~10 s client run; prints the `cam_*` lines; `--hud hidden` photographs in the state a hidden-HUD cutscene draws; every round's image is kept in `build/quest_gate/camsolve_<id>.rounds/` |
| `tools/quest_gate/run.py --script test/quests/<id>.lua --name <id> --no-build --no-publish` | runs the quest test (prefix `TORIRS_ROOT_SIZE=1024x768` for cutscene work) |
| `tools/quest_gate/gate.py <id>` | grades the run; includes the cutscene rule |
| `tools/quest_gate/cutscene_port/build_cutscene_sheet.py <out_dir> <id>...` | the comparison sheet |
| `::cam <eye> <h> <look> <h> [rate rate2]`, `::camreset`, `::camhud <1|0>`, `::goto <x> <z> <level>` | content debugprocs the solve tool drives (`cheat_cam.rs2`, `cheat_cutscene.rs2`) |

## Engine reference

Everything a cutscene can do is one of the following. Read the units before
writing a number; most first-attempt errors are unit errors.

### Camera: four commands, nothing else

```
cam_moveto(<coord>, <height>, <rate>, <rate2>);   // where the eye goes
cam_lookat(<coord>, <height>, <rate>, <rate2>);   // what it looks at
cam_shake(<axis 0..4>, <random>, <amplitude>, <rate>);
cam_reset;
```

- **coord** is `level_mx_mz_lx_lz`, in that order: `2_50_148_38_32` is level
  2, map square 50,148, local 38,32, i.e. tile 3238,9504 (x = mx*64 + lx,
  z = mz*64 + lz). `camsolve.py` prints the literals; copy them exactly as
  printed, never convert by hand.
- **The tile must be inside the scene loaded around the player** (104x104
  tiles). Outside it the view is black or wrong. For a far-away shot move the
  player or use a remote view (below).
- **height** is world units above the ground at that tile, 128 per tile. A
  standing player is about 200 tall. Dialogue-height shots 50-350; establishing
  shots 400-1200; overhead 2000+. The client clamps pitch to 128..383 (about 22
  to 67 degrees down); the reference client clamps the same way. The clamp
  means the view is never flatter than 22 degrees: to put a distant landmark
  in the upper third with sky above it (a small hut on a hill), the eye must
  be far AND high (h1000-1300 at 25-35 tiles), not low. When a near landmark
  climbs to the top of the frame instead, raise the look-at's height (200-400
  puts it on a building's walls) or bring the eye closer.
- **rate, rate2** set the motion. Each rendered frame every axis moves
  `rate + remaining * rate2 / 1000` toward the target, never overshooting;
  `rate2 >= 100` snaps at once. So:
  - `100, 100` is a **cut**. Both packets of a pair use it.
  - `1, 1` is a slow glide that eases in: about 10 s for a 650-unit move at
    50 fps (the per-frame step is `rate + remaining*rate2/1000`, so the time is
    `1000/rate2 * ln(1 + distance*rate2/(1000*rate))` frames: 650 units at
    `1,1` is about 500 frames). `2, 1`: about 6.7 s for 800 units. `4, 2`:
    about 5 s for 1400 units. `3, 10` settles hard. `0, 0` never moves.
  - Frames are rendered about 50 times a second in the test client (20 ms).
    For a rough tick count divide the seconds by 0.6. Cuts are exact, glides
    approximate; check the first run's mid-glide `t.world.camera()` readings
    against the shot list and adjust the rate.
  - A cut and a glide in the same tick work: `cam_moveto(A, h, 100, 100);
    cam_lookat(L, h, 100, 100); cam_moveto(B, h2, 1, 1);` frames A at once and
    glides toward B while the look-at holds on L. Both pilots use it.
- **Always send `cam_moveto` and `cam_lookat` together** for the first framing.
  A lone `cam_lookat` after a reset reuses stale eye state. A later `cam_moveto`
  alone (a glide to a new eye with the same look-at) is fine.
- **A scene rebuild cancels the scripted camera and every shake.** A
  `p_teleport`/`p_telejump` into another map square is a rebuild: teleport,
  `p_delay(1)`, then frame.
- **`cam_reset`** hands the camera back. Every cutscene ends with one.
- `cam_shake(axis, random, amplitude, rate)`: axis 0 x, 1 height, 2 z, 3 yaw,
  4 pitch; offset per frame is `rand(+-random) + sin(cycle*rate/100)*amplitude`.
  Shakes add up and only `cam_reset` (or a rebuild) stops them.

### Time

`p_delay(n)` parks the script for n+1 ticks. **One tick is 0.6 s; `p_delay(0)`
costs one tick.** `ticks = round(seconds / 0.6)`, and a hold of T ticks is
`p_delay(T - 1)`: a 14-second hold (23 ticks) is `p_delay(22)`. The fade
recipe's own delays cost about 3 ticks at each end; subtract them from the
first and last shots' holds. A dialogue page (`~chatnpc...`, `~mesbox`) waits
for the player's click, so camera commands placed between two pages fire the
instant the earlier page closes.

### The two kinds of cutscene the recordings show

Look at the clip's frames and pick one:

- **HUD up.** Inventory, minimap and chatbox stay; only the camera moves.
  Camera commands only. Tears of Guthix, Regicide, Horror from the Deep.
- **HUD hidden.** Side panel empty, minimap black, usually a fade to black at
  each end, often narration. Wrap the camera work in the canoe recipe
  (`OSRS-Content/.../canoes/scripts/canoe_cutscene.rs2:6-75` is the reference):

```
if_close;
if_opensub(toplevel_osrs_stretch:overlay_atmosphere, fade_overlay, 1);
runclientscript*(^canoe_fade_trans_client)(255, 0, ^canoe_fade_cycles, fade_overlay:fader);  // to black, 1 s
p_delay(2);
%cutscene_status = 1;          // toplevel scripts hide the side panel
%minimap_state = 2;            // minimap off
%fov_clamp = 1;
if_closesub(toplevel_osrs_stretch:orbs);
if_closesub(toplevel_osrs_stretch:popout);
// optional: p_telejump(<stage>); p_delay(1);
cam_moveto(...); cam_lookat(...);
runclientscript*(^canoe_fade_trans_client)(0, 255, ^canoe_fade_cycles, fade_overlay:fader);  // back to clear
p_delay(2);
if_closesub(toplevel_osrs_stretch:overlay_atmosphere);
//   ... shots, narration, actors ...
if_opensub(toplevel_osrs_stretch:overlay_atmosphere, fade_overlay, 1);
runclientscript*(^canoe_fade_trans_client)(255, 0, ^canoe_fade_cycles, fade_overlay:fader);
p_delay(2);
cam_reset;
%cutscene_status = 0; %minimap_state = 0; %fov_clamp = 0;
if_opensub(toplevel_osrs_stretch:orbs, orbs, 1);
if_opensub(toplevel_osrs_stretch:popout, popout, 1);
// optional: p_telejump(<back>); p_delay(0);
runclientscript*(^canoe_fade_trans_client)(0, 255, ^canoe_fade_cycles, fade_overlay:fader);
p_delay(1);
if_closesub(toplevel_osrs_stretch:overlay_atmosphere);
```

`^canoe_fade_trans_client` (7060) and `^canoe_fade_cycles` (50) are in
`canoes/configs/canoes.constant`; reuse them. `if_settab`, `minimap_toggle`
and `tut_*` are not implemented here; do not reach for them.

### Narration, title cards, dialogue during a cutscene

- A narration box the player clicks through: `~mesbox("...")`.
- A line in the chatbox with no click: `mes("...")`.
- **Known gap:** OSRS draws flyover narration as small text at the top of the
  world view while the chatbox stays; this engine has no such interface yet,
  so `mes()` in the chatbox stands in for it, and the canoe recipe's
  `%cutscene_status = 1` also hides the chatbox that the recording keeps.
  Grade those as `partial` on Words/HUD and note them; do not invent an
  interface.
- A chapter card ("Monkey Madness: Chapter 2") is a `~mesbox`
  (`quest_mm/scripts/mm_waydar.rs2:157-166`).
- An npc line with a chathead: `~chatnpc(...)` or the quest's own speaker proc.
- Copy every line from the wiki transcript verbatim.

### Actors

```
npc_add(<coord>, <npc>, <ticks alive>); npc_setowner;   // private to this player
npc_setmode(none);                                        // off AI; npc_setmode(null) restores
npc_walk(<coord>); npc_tele(<coord>); npc_facesquare(<coord>);
npc_anim(<seq>, 0); npc_say("..."); npc_del;
.npc_add / .npc_walk / .npc_anim ...                       // the second actor
facesquare(<coord>); anim(<seq>, 0); say("...");          // the player
spotanim_map(<spotanim>, <coord>, <height>, 0); loc_add(...); loc_del(...); sound_synth(...);
```

An npc walks one tile a tick; `p_delay(distance)` lets it arrive. References:
`quest_tbwt/scripts/tbwt_tamayu.rs2:207-319` (a staged fight), LostCity
`quest_arena/scripts/quest_arena.rs2:140-225` (a walk-in with doors),
`quest_grandtree/scripts/glough.rs2:111-150` (a private actor and a shake).

### A scene somewhere else

- Live world, far from the player: `remote_view_start(<coord>, <ticks 1..200>)`
  rebuilds the client around that coord without moving the player; camera
  commands; `remote_view_end()` (`skill_construction/scripts/poh_portal_nexus.rs2:641-646`).
- A private copy of a map square (a flashback in a place the player must not
  really be): `~map_instance_from_square(<coord>)`, then
  `p_teleport(map_instance_coord($handle, dx, dz, level))`, and
  `~map_instance_release_here` after (`quest_dragonslayer2/scripts/dragonslayer2.rs2:1601-1682`).
- Otherwise `p_telejump` the player to the stage and back, as TBWT and the
  canoe do.

### Assertion vocabulary (tests)

- `t.cutscene.mark()` pins where an await starts counting camera packets.
- `t.cutscene.await(name, { since=, expect={...}, shots="all" })` reads the
  client's camera-packet ring and fails if the expected ops are missing.
- `t.world.camera()` returns `{x, z, level, yaw, pitch, zoom, server_driven,
  serial, last_op, last_target}` for a `t.check` detail.
- The gate rule `cutscene_row_required`: a quest whose scripts call
  `cam_moveto`/`cam_lookat` needs a PASS `cutscene:` row covering every site.
  Full text: `docs/quest_authoring/verbs-cutscene.md`.

### Client layout for pictures

The test client is 765x503 (fixed layout, world in a 512x334 viewport) unless
`TORIRS_ROOT_SIZE=WxH` is set; **at 1024x768 it lays out in resizable-classic
mode**, the layout most recordings use, with the world filling the canvas and
the HUD drawn over it. The solve tool runs at 1024x768 by default and the quest
test should be run that way for cutscene work, so the comparison sheet shows
two pictures of the same shape. Tears of Guthix's whole test passes at 1024x768;
if a quest's test does not, run it at 765x503 and the sheet crops the viewport
instead, with a narrower field of view than the recording.
