# Cutscene verbs: `t.cutscene.await`, `t.cutscene.mark`, `t.cutscene.exempt`, `t.world.camera`

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
- A site OFF the guide's route may instead be exempted by a `t.cutscene.exempt` row (below); the
  gate accepts or refuses each one (`cutscene_exempt_refused: ...`) and prints every accepted one
  as `(<quest>: cutscene: <site> exempt by row ... (entered from [oploc2,X], no guide step's; drove
  <step> instead))`.
- The finding names the quest, the site and its `file:line`.
- Blocked runs are not graded.
- `gate.py --cutscene-as <test_id> <artefact dir>...` grades ONLY this rule (with exemptions) of
  scratch runs (`run.py --script ... --name`) against `<test_id>`'s sites and guide.

`make -C src check-quest-cutscenes` (`cutscene_sweep.py --fail-on-dropped`) fails on a DROPPED or
PARTIAL port. WIKI_MISSING is the wiki backlog specced in `docs/quests/cutscenes/` and never fails
it.

The sweep reads TWO LostCity trees (seam34): `LostCity_Content2/scripts/quests/<quest_dir>` first,
then `LostCity_Server/content/scripts/quests/<quest_dir>`; a quest in either is a LostCity quest and
the `lostcity_tree` column says which was read. Ten quests live only in LostCity_Server (eadgar,
horror, misc, mm, mortton, regicide, routequest, tbwt, troll_love, viking); reading Content2 alone
graded them WIKI_* and hid two real losses. Neither tree's `[debugproc,...]` blocks are graded (a
developer's `::camtest` is not a cutscene; LostCity's `debug_routequest.rs2` made routequest a false
PARTIAL). A LostCity quest graded NONE whose `CUTSCENES.tsv` row says the wiki has one reads
WIKI_MISSING (Regicide's catapult). `--sites` lists, under each DROPPED/PARTIAL row, every LostCity
framing call with no same-op same-coord call in ours -- the next content pass's work list. Since
seam34 the check is RED on true losses: mm PARTIAL (`mm_demon.rs2:98`'s `cam_lookat` in
`[timer,teleport_mm_sigil]` is dropped) and troll_love DROPPED (both sled rides,
`quest_troll_love.rs2:145-293`, have no camera op in the port). Both are content-port work for the
cutscene session, not a gate bug. (troll_love's rides and crash have been ported since:
`trollromance_sled.rs2:236-245`, `:273-274`, `:355-366`; see "A camera site behind a random roll"
below.)

## A cutscene no guide step reaches

Death Plateau's troll-thrower cut plays from Reading the Danger sign (`death_dangersign_trolls`,
2839,3595). The sign stands on the main plateau path, and from the secret way it answers
"I can't reach that!". `death.lua` travels to 2840,3594 once the path is scouted, then reads the
sign. A loc the content gives a cutscene is driven like any other loc. The rule only asks that the
cutscene is asserted.

### `cutscene_row_required` names a site on a route you did not take: `t.cutscene.exempt`

The gate counts every `cam_moveto`/`cam_lookat` site in the quest's `.rs2`, including one on an
OPTIONAL route. Shilo Village frames its camera only on the table raft (`zqtableraft`,
`quest_zombiequeen.rs2:674-770`, sites `:738`/`:739`), one of the caverns' ways out. The guide names
no way out at all: its next step is `buryCorpse` at Tai Bwo Wannai. Driving the raft only to cover
the site (sonnet-b42's first green) tests a route the guide never takes. Seam34 added the exemption:

```lua
-- after the guide step driven instead (seam34's proof copy of zombiequeen.lua, after buryCorpse):
t.cutscene.exempt("quest_zombiequeen.rs2:738", "the guide leaves the caverns by no named step; "
    .. "this test left by the waterfall path (leaveCavernsWaterfall) and drove buryCorpse instead "
    .. "of the table raft ([oploc2,zqtableraft], rs2:674)")
```

`t.cutscene.exempt(site, reason)` -> `ok refused`. Call it DIRECTLY, never under `t.exec`. It
writes its own row `cutscene.exempt.<site>` (no shot), PASS with detail
`cutscene-exempt: <site> ;; <reason>`, one call per site. A `site` that is not `<path>.rs2:<line>`,
or an empty reason, is FAIL `refused`. The row is a claim. `gate.py` decides, and accepts it only when
all three of these hold:

1. `site` is a path suffix naming exactly ONE of the quest's camera sites (the `file:line` the
   `cutscene_row_required` finding prints).
2. `reason` names a guide step (a Quest Helper step variable, not a panel) that the ledger PASSed
   (`<step>`, `<step>-...` or `<step>.…`). That is the step the test drove instead.
3. The site is OFF the guide's route. The gate walks up from the site's script block: a proc to its
   `~` callers, a label to its `@` callers, a queue to `queue(...)`, a timer to `settimer(...)`,
   across all content. Every block it reaches must be a player-op trigger (`[op*,X]`/`[ap*,X]`)
   whose subject `X` no guide step targets or carries as an item. Multi-npc/loc families and
   categories count as the same subject.

When rule 3 fails, the finding is `cutscene_exempt_refused: ... is ON the guide's route ([opnpc1,rantz]
<- guide step talkToRantzWithToad...) -- a site a guide step reaches is covered by t.cutscene.await,
never exempt`. A walk that reaches something it cannot name is refused too, and so is a quest with
no guide. Things it cannot name include an npc `ai_*` script, a debugproc, an `if_button`, a mapzone
and a label nothing calls (`cannot show ... is off the guide's route`). The rule is strict because
a wrong "off route" lets a dropped cutscene pass. On 2026-10-01 the sites it calls OFF are
zombiequeen `:738/:739` (`[oploc2,zqtableraft]`) and death's danger sign (`death_locs.rs2:64/65`,
which `death.lua` asserts anyway). Every other site is ON or unresolved.

Proof (seam34, on a copy of the test kept at
`build/seam_state/seam34/zombiequeen.seam34_waterfall_exempt.lua`; the committed `zombiequeen.lua`
still drives the raft and covers both sites with `t.cutscene.await` until its next author adopts the
copy): the test leaves by the waterfall path (`zqwaterfallrocks` op2,
`quest_zombiequeen.rs2:996`; "You climb your way out of the cavern"). It reaches `left_ah_za_rhoon`
by the `[queue,exit_ah_za_rhoon]` mapzone exit (`:1077-1084`, any route) and is green with both
exemptions accepted, FULL 30/30. The same file without the exempt rows is RED
`cutscene_row_required`. A worktree mutant of `chompybird.lua` that exempts Rantz's `rantz.rs2:317/318`
(on `talkToRantzWithToad`'s route) is refused on both sites.

### A camera site behind a random roll on the guide's route: `cutscene_exempt_refused` (Troll Romance's sled crash)

*Origin: author batch matthew-mbp-m4-b49 (troll_love, accepted and sampled).*

Symptom: `cutscene_row_required` names `trollromance_sled.rs2:273`/`:274` after both sled rides
passed, and a `t.cutscene.exempt` row for them is refused (`cutscene_exempt_refused: ... is ON the
guide's route`). The sites are the 1-in-250 rock crash: `[oploc1,trollromance_piste_walk_barrier_down]`
(the guide's `sledSouth`) -> `~trollromance_first_sled_ride` -> `if (random(250) = 0)
~trollromance_sled_crash` (`:221-223`). The crash is reached from a guide step, so rule 3 refuses
the exemption, and no number of reruns reaches a 1-in-250 roll.

The way out is a harness debugproc that runs the SAME proc from the same tile, then
`t.cutscene.await` on it. `[debugproc,trollromance_crash]` (`trollromance_sled.rs2:384-390`) needs
the waxed sled worn, teleports to the roll's tile 2770,3828 and calls `~trollromance_sled_crash`.
It writes no quest var. `troll_love.lua` drives it after `equipSled` and before the real ride:

```lua
do local r = t.cheat("::trollromance_crash"); t.check("sledCrash.hook", r, "::trollromance_crash answered " .. tostring(r)) end
t.exec("sledCrash.cutscene", t.cutscene.await, "sledCrash", { timeout = 60, expect = {
    { op = "moveto", coord = "0_43_59_26_39" }, { op = "lookat", coord = "0_43_59_22_44" }, { op = "reset" } } })
t.ticks(10)  -- the crash fades in before its chat opens; without this sledCrash.chat read no dialogue
t.exec("sledCrash.chat", t.chat.play, { "player:And I thought snow was soft", "player:Although it was a little softer", "end" })
t.exec("sledCrash.pickup", t.player.click_obj, "trollromance_toboggon_waxed", 3)   -- the crash drops the sled
```

Then wear the sled again, go back to the barrier and drive `sledSouth` for real: the hook covers
only the crash's camera, never the guide step. The same pattern fits any camera site behind a
`random(...)` on the route. Add the debugproc beside the proc it calls, and say in its comment which
roll it stands in for. A debugproc that writes a quest var, or that replaces the step the guide
names, is a cheat past the guide's work (trap 16), not coverage.

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

## A cutscene the port narrates as a blackout is not graded (A Porcine of Interest's Pig Thing)

*Origin: the matthew-mbp-m4-b52 porcineofinterest reviewer: "Pig Thing cutscene still narrated
blackout (spec pending)".*

`cutscene_row_required` covers only the camera ops in your quest's own `.rs2`. A scene the port
deferred has no camera op, so the gate says nothing about it. A Porcine of Interest's Pig Thing
(acid spit, the gnome-goggle line, the cameras of Spria's rescue) is a named leftover
(`porcineofinterest.rs2:18`, `porcineofinterest_locs.rs2:119`): investigating the skeleton fades
out and wakes you at Spria. `docs/quests/CUTSCENES.tsv` marks the row `ported=no`.

Drive the click that triggers it (`investigateSkeleton`) and the dialogue after the wake-up as
ordinary rows. Do not write a `cutscene:` row for a scene that has no camera op. Name it in the
review as spec-pending. Porting it is work for the cutscene session, not for the author, and once
the port adds a `cam_*` op the gate rule applies to the quest.

That is what happened to Porcine itself in matthew-mbp-m4-b70: `[proc,poi_pig_thing_cutscene]`
(`porcineofinterest_locs.rs2`) now plays the Pig Thing, the acid, the blackout and Spria's rescue
with the canoe recipe and seven camera sites, and wakes the player in Spria's house. From that
commit on, `porcineofinterest` needs its `pigThing.cutscene` await row (and the old
`player:Argh! My eyes!` page is an overhead `say`, not a page). The example above still shows the
rule for every remaining `ported=no` row.
The same holds for every `ported=no` row in `CUTSCENES.tsv`: Contact! (3 scenes), Ribbiting Tale (1)
and What Lies Below (2) were spec-pending in matthew-mbp-m4-b53, so they had no `cutscene:` rows.
In matthew-mbp-m4-b54 the same held for Ethically Acquired Antiquities (2 scenes; the port speaks
Haig's confession as plain dialogue, `eaa_haig_confront`, drained by the `watchCutscene` row),
A Soul's Bane (7) and Swan Song (2).
