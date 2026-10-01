# Cutscene verbs: `t.cutscene.await`, `t.cutscene.mark`, `t.world.camera`

Seam32 (`cutscene_verb_and_camera_read`, 2026-09-30). Driver: `script/plugins/quest_driver/cutscene.lua`
and `world.lua`. Engine: `rs_gameproto_exec.c` (`exec_cam_script_record`) and `app.h`
(`struct App_CamScript`). Gate: `tools/quest_gate/gate.py` (`cutscene_row_required`).

## Why a cutscene is asserted

The guide has no "watch the cutscene" step, so `helper_coverage` never asks for one. Before seam32,
no test read the camera. A port could drop a cutscene, or a cutscene could set its stage and move
nothing, and the test stayed green. Fight Arena's ogre-pen sequence has 17 camera ops in LostCity,
none of them were in our port, and `arena` was green.

The client now stamps every CAM_MOVETO, CAM_LOOKAT, CAM_SHAKE and CAM_RESET packet it executes into
a serial and a 64-deep event ring. Coordinates are WORLD tiles, resolved when the packet arrives.
These verbs read that ring.

## `t.cutscene.await(name, opts)`

Waits for the sequence, follows it to its CAM_RESET, and records one keyframe per camera packet.
Use it on the row right after the row that starts the cutscene: the talk, click or chat.play whose
page plays it.

```lua
t.exec("climbDownTrapDoor-dialog", t.chat.play, { ..., "npc:Fool...meet my little friend", "end" })
t.exec("climbDownTrapDoor.cutscene", t.cutscene.await, "climbDownTrapDoor", { expect = {
    { op = "moveto", coord = "0_38_154_44_13", height = 2000 },   -- glough.rs2:144, copied verbatim
    { op = "lookat" },                                            -- glough.rs2:145 is an expression
    { op = "reset" },
} })
```

- **Result.** `ok`, or one of:
  - `no_cutscene`: no moveto or lookat arrived within `opts.timeout` ticks (default 100).
  - `unfinished`: `opts.quiet` ticks passed with no packet and no CAM_RESET (default 30).
  - `not_found`: an `expect` entry is missing.
  - `refused`: the ring overflowed and packets were lost.
  - `unsupported`: the binary predates seam32.
- **Detail.** The detail begins `cutscene: <n> keyframes, <first op> ... <last op>, reset=<yes|no>`.
  The keyframes follow: `#1 t=<tick> moveto <x>,<z> h=<height> s=<speed>/<speed2> | #2 ...`. Then
  come ` ;; ` notes: where the sequence was read from, the shots, and the expect verdict.
  `gate.py` matches the leading `cutscene:` and reads the `#i t=.. <op> x,z` list.
- **`opts.expect`.** Each entry must appear IN ORDER.
  - `op` is `moveto`, `lookat`, `shake` or `reset`.
  - The tile is either `coord = "<level_mx_mz_lx_lz>"` (the content's literal, copied from the
    `.rs2`) or `x =, z =`. Omit both for a site whose coord is an expression (`coord`,
    `movecoord(...)`).
  - `height` must match within 1.
  - The first missing entry FAILs the row and is named:
    `expected keyframe #1 (moveto 2476,9869 h=2000 from 0_38_154_44_13) not found after keyframe #0`.
- **`opts.shots`.** One shot is taken at the first framing keyframe. After that, a shot is taken
  whenever the camera has held a newer target for 2 ticks, which always includes the last framing
  before the reset. With `"all"`, every keyframe is shot as it arrives. A sequence that played and
  reset inside the trigger row gets no shot: the frame no longer shows it. The detail says
  `shots=none (the sequence ran inside the trigger row)`, and the chat.play page shots are the
  pictures.
- **Where the sequence starts.** Camera packets often land WHILE the trigger row is still running.
  For example, Rantz points at the toad clearing in the middle of a chat.play list, and `cr_queue`
  resets as the dialogue closes. So the await counts packets from the moment the PREVIOUS `t.exec`
  row began, not from the call. Its own row is skipped if its name contains `cutscene`. Two further
  rules apply:
  - The start is never before the last packet an earlier await consumed.
  - A read STOPS at its own reset. When a script resets and frames again in the same tick (Fight
    Arena's pens), two awaits in a row read two sequences.

  `local m = t.cutscene.mark()` before the trigger, with `opts.since = m`, pins the start
  explicitly. Use it when the trigger is not a `t.exec` row.

### A cutscene between two dialogue pages: split the `chat.play` list

When a script closes a page, plays a cutscene and then opens the next page (Fight Arena's walk-in
between "Ok, we'd better hurry." and the round's mesbox), a single `chat.play` list fails at the
page after the cutscene: `the dialogue closed after N page(s)`. Split the list around the cutscene
row: pages, then `<step>.cutscene`, then the rest in a second `chat.play`. A cutscene that ENDS in a
page (the Fight Arena jail guard's "The General seems to have taken a liking to you.") needs a
`chat.play` row that reads it; otherwise the next `talk_to` lands on that page.

## `t.cutscene.mark()`

Returns the camera serial now, as a bare number.

## `t.world.camera()`

Returns a BARE TABLE, not a `(result, detail)` pair:

- `x, z, level`: the eye's world tile.
- `yaw, pitch, zoom`: the drawn angles.
- `server_driven`: a moveto or lookat holds the camera, with no CAM_RESET or scene rebuild since.
- `serial`: every camera packet this session.
- `last_op`: `moveto`, `lookat`, `shake`, `reset` or nil.
- `last_target = {x, z, height, op}`: the newest moveto or lookat.

Record it with `t.check` and write the reading into the detail. An old binary answers
`nil, "unsupported: ..."`.

## Leg ends are camera-quiet

`QD.core_legs_drive` waits up to `QD.LEGS_QUIET_TICKS` for a running sequence to reset before
`::checkpoint k`. A camera that never resets gets no checkpoint, and the next leg row says so:
`checkpoint k NOT written: the camera is server-driven after 10 quiet-wait tick(s) -- a cutscene with
no CAM_RESET (serial N, last_op lookat, target x,z)`. The save carries no camera, so a leg resumed
from that checkpoint would start free where the full run is mid-shot. See `relay.md`, Checkpoints.

## The gate rule: `cutscene_row_required`

This applies to every quest in `cutscene_sweep.quests_with_cutscene(repo)`, which is every quest
whose OWN `.rs2` calls `cam_moveto` or `cam_lookat`.

- A would-be-green ledger must hold a PASS row whose detail begins `cutscene:`.
- The union of those rows' keyframes must cover every SITE. A site is one `file:line` from
  `cutscene_sweep.cutscene_sites`.
  - A literal coord needs a keyframe of that op on that exact tile.
  - An expression coord needs any keyframe of that op.
- The finding names the quest, the site and its `file:line`.
- Blocked runs are not graded.

`make -C src check-quest-cutscenes` (`cutscene_sweep.py --fail-on-dropped`) fails on a DROPPED or
PARTIAL port. WIKI_MISSING is the wiki backlog specced in `docs/quests/cutscenes/` and never fails
it.

## A cutscene no guide step reaches

Death Plateau's troll-thrower cut plays from Reading the Danger sign (`death_dangersign_trolls`,
2839,3595). The sign stands on the main plateau path, and from the secret way it answers
"I can't reach that!". `death.lua` travels to 2840,3594 once the path is scouted, then reads the
sign. A loc the content gives a cutscene is driven like any other loc. The rule only asks that the
cutscene is asserted.

### `cutscene_row_required` names a site on a route you did not take (sonnet-b42)

The gate counts every `cam_moveto`/`cam_lookat` site in the quest's `.rs2`, including one on an
OPTIONAL route. Shilo Village's way out of Ah Za Rhoon can be the table turned into a raft
(`zqtableraft`, `quest_zombiequeen.rs2:674-770`), which the guide never names, and its camera site
still reds the gate until a ledger row covers it. Take that route in the test, as
`zombiequeen.lua` does (`leaveCavernsRaft` + `leaveCavernsRaft.cutscene` with the `.rs2`'s literal
`moveto 0_45_146_48_7`), rather than leave the quest red over a cut no guide step reaches.

## Test affordance: `::cutscene <level_mx_mz_lx_lz> [times] [hold]`

This is a content debugproc, `general/scripts/misc/cheat_cutscene.rs2`. It plays LostCity's Fire
Warrior door cut (`ikov_dungeon.rs2:183-185,217`, the ops and speeds verbatim) at the tile you name:

1. moveto the tile
2. lookat 5 east, 2 north
3. lookat 5 east, 2 south
4. reset

`times` repeats it, with the next moveto in the tick of the reset. `hold 1` skips the reset.
`::camreset` lets go. The conformance rows use it: `seam.cutscene_await_records_keyframes`,
`seam.cutscene_await_no_cutscene`, `seam.cutscene_expect_missing_keyframe`, `seam.world_camera_read`
(scratch: `build/seam_state/seam32/s32_cutscene_verb.lua`).
