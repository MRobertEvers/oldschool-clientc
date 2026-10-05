# Verbs: `world` / `drive` / `player` travel and clicks (section 3)

Section 3's `world.lua` / `pointer.lua` table: locating things, moving, and the click verbs.
Held-item verbs are in `verbs-inventory-shops.md`, fights in `verbs-combat.md`, `t.session.*` in
`verbs-sail-session.md`.

## `world` / `drive` / `player` (`world.lua`, `pointer.lua`)

### `t.world.loc_near(sym, radius)` / `t.world.obj_near(sym, radius)`

`t.world.loc_near(sym, radius)` / `t.world.obj_near(sym, radius)` ->
`(ok, {kind,id,element_id,tile_x,tile_z,level,...})` `not_found`. A LOC symbol resolves to the id
the scene actually holds and the row's `match` names the rule -- `exact`, `base`, or `multiloc`
(trap 20).

#### Stacked floors: `t.world.loc_near(sym, radius, { level = n | "here" })` (b59-seam1)

Without opts `loc_near` answers the FIRST copy, and the pool is ordered by x/z distance only (no
level term, `DriveUi_Locs`), so a copy on another floor at the same x,z ties with or beats the one
on yours. Miscellania's castles and the Sinclair mansion stack a door on levels 0 and 1 at one
tile: on level 1, `loc_near("opencastledoor", 3)` answered the level-0 leaf at 2506,3852,0
(misc run 4: 10 of 69 "door stands open" rows passed on the wrong floor). Name the floor:

- `{ level = n }` -- only a copy on raw level n; `{ level = "here" }` -- the player's level.
- `{ at = {x, z[, level]}, slack = s }` -- only a copy within `s` tiles (default 0) of x,z, on
  `level` when given: `loc_near("opencastledoor", 12, { at = { 2506, 3851, 1 }, slack = 1 })`.
- A filtered `not_found` names the copies it skipped WITH their level (`copies skipped, nearest
  first: 2506,3852,0`). The level is the RAW cache level: on a bridge deck the player stands one
  plane lower (gaps-world: `t.world.loc_near` reports a loc's raw cache level).
- `{ level = n | "here", deck = true }` (b61-seam1) -- the level named is the PLAYER's plane and
  the copy stands on a bridge deck over it: only a copy on raw level n + 1. The Waterfall ledge's
  `barrel_waterfall_quest` (2512,3463,1 beside the plane-0 ledge) and Sir Spishyus'
  `rd_bridge_left/right` (2483/2477,4972,1 over a plane-0 room) read this way. YOU say it is a
  deck (the map's level-1 flag 2, LINK_BELOW): the pool row carries no bridge flag, so the driver
  adds the 1 and cannot test it. A plain level filter that misses such a copy says so in its
  `not_found`: `the copy at 2512,3463,1 is one raw level up: a bridge-deck loc ...`.
- Never read the pool through `t.drive._pool_read` or parse `click_loc`'s `nearest copies` from an
  impossible level (`{ at = { x, z, 9 } }`): both were b59 workarounds for this.

### `t.player.pass_door(spec)` -- cross one door on foot (b59-seam1)

`t.player.pass_door{ closed=, open=, at={x,z[,level]}, near={x,z}, far={x,z} [, far_ok=fn,
far_desc=, op=1, close=true, ticks=] }` -> `(ok, detail)` `refused` `not_found` `timeout`. One row
per crossing, through `t.exec`:

```lua
t.exec("vargas1.castleGate", t.player.pass_door, { closed = "castledoor", open = "opencastledoor",
    at = { 2510, 3860, 0 }, near = { 2511, 3860 }, far = { 2508, 3860 } })
```

1. walks to `near`; must stand within 1 of it on the door's level, and NOT already satisfy the far
   test (`refused ... already past the door`);
2. waits up to 6 ticks for either leaf on the door's level (a scene that just loaded), then reads
   the CLOSED leaf on the exact `at` tile AND level. Present: presses it there (`click_loc`'s `at`
   selector) and grades the closed leaf LEAVING that tile and level, plus the open leaf standing
   within 1 of it when `open` is named (or the press carrying the player to the far side: a
   walk-through door). Absent: the door stands open (`stands open ..., not pressed` -- pressing an
   open leaf shuts it) and the OPEN leaf must stand within 1 of the door tile on that level;
   neither leaf -> `not_found ... neither leaf on level L`;
3. walks to `far` and grades exactly that tile on the door's level, or `far_ok(tile)`;
4. `close = true`: presses the open leaf on that level, grades the closed leaf back on the door
   tile, walks back to `far` and grades it (a door the client lost after its 500-tick revert while
   you were away is the reason misc_astrid closes doors behind it).

Name `open=` whenever the door's open leaf is a different symbol, even one with no name or op (the
Water Ravine golem doors open to `elid_underground_inactive_door`): the way back finds that leaf
standing open, and without `open` the return row fails `no <closed>: none within 0 ... and no open
leaf named (spec.open)` (b60-seam1; seam-facts: Seam pass matthew-mbp-m4-b60-seam1 (a)).

`level` defaults to the player's level at `near`. The press's own word (`ok`, or `timeout` for a door
that says nothing) is in the detail; the grade is the loc reads and the tiles. Proved on Miscellania
castle's stacked `castledoor` 2506,3851 (levels 0 and 1): conformance `player.pass_door` and
`seam.stacked_door_read_on_its_own_floor`; a misc.lua copy using it for all 25 call sites ran
207/0 (84 crossings, 17 pressed, 67 standing open, every one on its own floor).

### The crossing verbs: `cross_gate`, `cross_trap`, `walk_route`, `teleport_cast` (b60-seam0)

In b56-b59 every door-rule fixer hand-wrote these four helpers into its quest file (hero.lua
`taverley_gate`/`cross`/`teleport`, hunt.lua `cross`, rovingelves.lua and mourningsendparti.lua
`cross_trap`/`walk_route`/`trap_vitals`, misc.lua `camelotTeleport.*`), each a little different and
each needing a sampler round. Use the verbs; do not copy a helper. Every one is graded on the world
(tiles before and after, loc reads, rune counts), never on the press's answer, which is only in the
detail: a row must check something the press caused (sampler-findings: "Sample
matthew-mbp-m4-b59" (b)). A malformed spec raises. Proved by conformance `player.cross_gate`,
`player.walk_route`, `player.cross_trap` and `player.teleport_cast`, and by the scratch runs in
`build/seam_state/matthew-mbp-m4-b60-seam0/crossing/` (b60s0_cross1-3).

#### `t.player.cross_gate(spec)` -- a wall gate, either way, pressed on every crossing

`t.player.cross_gate{ loc=, at={x,z[,level]}, near={x,z}, far_ok=fn, far_desc= [, far={x,z},
op=1, ticks=, chat={...}, chat_optional="<why>"] }` -> `(ok, detail)` `refused` `not_found`
`timeout` `covered` `mismatch` ... One row per
crossing, through `t.exec`:

```lua
t.exec("goto-achietties.memberGate", t.player.goto_tile, 2938, 3450, 0)  -- open ground, this side
t.exec("achietties.memberGate", t.player.cross_gate, { loc = "membergater", at = { 2935, 3450, 0 },
    near = { 2936, 3450 }, far_ok = function(tile) return tile.x <= 2935 end,
    far_desc = "inside Taverley, x <= 2935" })
```

A WALK-THROUGH gate (gates.rs2 `[label,member_fencegate_try]`: `membergatel`/`membergater` at
Taverley 2935,3450-3451 and 2933-2934,3320, Karamja 2816,3182) moves the player across itself and
leaves no opened loc, so it is pressed on EVERY crossing, and where it lands depends on the side
(onto the gate tile from one side, one tile past it from the other): the far side is a TEST,
`far_ok(tile)` (the level is checked for you), not a tile. The verb walks to `near` (within 1, on the
gate's level, and NOT already far: `refused ... already ...`), reads the gate on its exact tile and
level, presses that copy, waits up to 12 ticks, and passes only when the tile before the press fails
`far_ok` and the tile after passes it. A pushed hop can answer `timeout settle_after_click` although
it landed (Karamja's crossBack did, b60s0_cross1 row 6): that is fine, the tiles are the verdict.
`far = {x, z}` then walks on to that exact tile and grades it. A gate that is the only way on foot
between two regions is clicked on every visit, however large the regions (sampler-findings: "Sample
matthew-mbp-m4-b59" (a)); never `goto_tile` across it.

**A GUARDED walk-through speaks first: `chat = { <chat.play list> }`** (b61-seam1). Fight Arena's
`fightarena_door1` (2617,3171 north, 2585,3141 west; `arena_locs.rs2 [oploc1,fightarena_door1]`)
in the Khazard disguise shows an `arena_guard1` within 5 tiles' "Nice observation guard..." page,
and the `p_telejump` lands only after that page is continued. Without `chat` the verb waited 12
ticks under the page and failed (`... landed 2617,3172,0; a page is up: npc 'Nice observation
guard. ' -- the press spoke before it moved the player (pass spec.chat ...)`). With it, the verb
waits for the press to open a page OR land, plays the list on the page, then waits for the
landing, which is still the verdict; the detail carries the page's whole line and chat.play's
answer (`the press opened npc 'Nice observation guard. You could have just asked to be let in
like a normal person.' after 0 tick(s): chat.play -> ok 1 page(s) ...; landed 2617,3170,0`). A
list that does not match is chat.play's own `mismatch`. `chat` says a page WILL open: none in
12 ticks (+2 after a landing) is `refused ... no page opened ...` even though the press may have
landed. When the page depends on the world (the guard speaks only within 5 tiles; his wander can
take him away) add `chat_optional = "<why>"`: no page is then written as `no page opened ...
(chat_optional: <why>)` and the landing alone grades the row. A page that refuses the crossing
(`Yeah, it's to prevent people like you...`) still plays, then the row fails on the landing with
the line in the detail.

```lua
t.exec("talkToGuard.northDoorIn", t.player.cross_gate, { loc = "fightarena_door1",
    at = { 2617, 3171, 0 }, near = { 2617, 3172 },
    far_ok = function(tile) return tile.z <= 3171 and tile.x >= 2613 and tile.x <= 2619 end,
    far_desc = "in the prison corridor, z <= 3171",
    chat = { "npc:Nice observation guard" },
    chat_optional = "arena_guard1 speaks only within 5 tiles (arena_locs.rs2)" })
```

Proved by conformance `seam.cross_gate_plays_a_guards_page` (the arena door in with the page;
Taverley's silent members' gate `refused` with `chat`, `ok` with `chat_optional`) and the scratch
runs `b61gc_reproA` (before: FAIL under the page) / `b61gc_reproB` (after: PASS) in
`build/seam_state/matthew-mbp-m4-b61-seam1/gatechat/`.

An OPENING gate (a leaf that stays open: `fencegate_l`/`openfencegate_l`) takes `open = "<open
leaf>"` and a `far` tile, and is handed to `t.player.pass_door` (closed = `loc`): the leaf standing
open is walked through, never pressed shut (b60s0_cross3 `penIn` pressed, `penOut` "stands open ...,
not pressed").

#### `t.player.cross_trap(spec)` -- a trap or obstacle by its own op, src tile to dest tile

`t.player.cross_trap{ loc=, at={x,z[,level]}, src={x,z}, dest={x,z} [, op=1, op_name="Jump",
attempts=4, vitals=fn|{eat=, below=, antipoison=true}, camera={yaw,pitch,zoom}] }` -> `(ok, detail)`
`refused` `covered` `not_visible` `timeout` ...

```lua
local VITALS = { eat = "shark", below = 60, antipoison = true }
t.exec("enterIsafdar.jumpPitfall", t.player.cross_trap, { loc = "regicide_pitfall_side", op_name = "Jump",
    at = { 2278, 3262, 0 }, src = { 2279, 3262 }, dest = { 2275, 3262 }, vitals = VITALS })
```

Only the op moves the player over the obstacle (a pit is walled by inviswalls, a dense forest is
solid, a tripwire's trigger tiles fire the trap when walked: regicide_traps.rs2), so the row is two
tiles: the player ON `src` before the press and ON `dest` after it (10 ticks). A failed roll that
leaves the player standing (a slipped pitfall: 15 damage, `You slip and fall onto the spikes.`) is
pressed again from `src`, at most `attempts` presses, with `vitals` between presses and once after.
Off `src` by 1-2 tiles (a stumble) it steps back on; further than that it STOPS (`stopped: ... not
walked round the obstacle`) -- a retry that walked round the pit put mourningsendparti's player in it
(run r4/2). A snagged tripwire still crosses, and its line is in the detail. `vitals` as a table eats
one `eat` below `below` Hitpoints and drinks one antipoison dose while `varp102_poison` is non-zero;
as a function it is called as is. Isafdar's crossings (rovingelves.lua `PITFALL_W/E`, `FOREST_E/W`,
`TRIPWIRE_E/W`; mourningsendparti.lua adds `PITFALL_S/N` at 2274,3173-3175) are its subjects. A dense
forest answering `You can see no way to get past this.` four times is content, not the verb:
`[label,regicide_cross_dense_forest]` needs `%varp328_regicide_quest >= ^regicide_spoken_tracker2`
(regicide_route.rs2:75). Every Isafdar trip presses every trap on it (sampler-findings: "Sample
matthew-mbp-m4-b59, round 3" (a)).

#### A loc on another raw level: `loc_level` (b61-seam1; all four crossing verbs)

`pass_door`, `cross_gate`, `cross_trap` and `climb` take `loc_level = n`: the RAW level of the copy
pressed (what `loc_near` reports), when it is not the player's. `at`'s level stays the PLAYER's
plane -- the near/src tile, the landing and the far test are graded on it; `loc_level` picks the
copy (the loc reads and the press) and nothing else. Omitted, it is `at`'s level, as before. A bridge
deck is the case: the map stores the loc on raw level 1 of a LINK_BELOW column and the player walks
it on plane 0, so `at = {x, z, 0}` pressed nothing (`no_row ... nearest copies: 2483,4972,1`) and
`at = {x, z, 1}` refused the player (`stopped: at ...,0, not within 2 of the src tile`). Write:

```lua
t.player.walk_to(2484, 4972, 12)   -- cross_trap steps back at most 2 tiles onto src, never walks round
t.exec("moveChickenToLeft", t.player.cross_trap, { loc = "rd_bridge_left", op_name = "Cross",
    at = { 2483, 4972, 0 }, loc_level = 1, src = { 2484, 4972 }, dest = { 2476, 4972 }, attempts = 1 })
```

The detail names both: `at 2483,4972,1 (raw level; the player on plane 0)`. Sea Slug's Fishing
Platform is the same shape one floor up (every platform loc one raw level above the plane-1 deck:
`climb{ at = {x, z, 1}, loc_level = 2, dest = {x, z, 0} }`). Proof: recruitmentdrive's seven bridge
crossings through `cross_trap` + `loc_level` (build/quest_gate/b61s1_rd_copy2, 160/0); the platform
ladder both ways through `climb` (b61s1_seaslug_climb) and the cabin's selfstage door in and out
through `pass_door` (b61s1_seaslug_door, `at = {2767, 3285, 1}, loc_level = 2`); conformance
`seam.bridge_deck_loc_named_by_loc_level` (the ledge barrel).

#### `t.player.walk_route(points, opts)` -- a waypoint chain, graded on the exact end tile

`t.player.walk_route({ {x,z}, ... } [, { max_hop=10, ticks=40, level=, vitals= }])` ->
`(ok, detail)` `refused` `timeout`. Each hop is `walk_to`; consecutive waypoints more than `max_hop`
tiles apart (Chebyshev) RAISE -- split the hop: `move_to` refuses a tile outside the scene the client
has built, and a long walk crosses scene rebuilds. The player more than `max_hop` from the first
waypoint is `refused ... nothing walked`. A hop that answers `refused` waits 3 ticks and is walked
once more; a second refusal stops the route there. The verdict is the player EXACTLY on the last
waypoint on the route's level; the detail lists every hop's answer and tile and counts the hops that
stopped short. `walk_to` is the client's pathfinder: it knows walls, not traps, so a route through
Isafdar is a chain the author keeps off every trigger tile (rovingelves.lua's chains were flooded
with them blocked). Conformance walks rovingelves' 26-waypoint walkToPitfall chain, 2385,3333 ->
2279,3262: 106 tiles west, wider than one 104-tile scene.

#### `t.player.teleport_cast(spell, landing, opts)` -- a real teleport, three rows

`t.player.teleport_cast(spell, {x, z[, level=0]}, { name=, runes={ {rune, n}, ... } [, radius=2,
where=, ticks=] })` -> `(ok, detail)` `refused`, and it WRITES three rows itself (like
`t.quest.expect_complete`): call it directly, never through `t.exec`.

```lua
t.player.teleport_cast("camelot_teleport", { 2757, 3478, 0 }, { name = "camelotTeleport",
    runes = { { "airrune", 5 }, { "lawrune", 1 } }, where = "Camelot" })
```

- `<name>.cast` -- `t.player.cast(spell)` answered `ok ... TELEPORTED`;
- `<name>.runes` -- each named rune left the backpack by EXACTLY its count (copy the cost from the
  spell's `magic_spells.dbrow` row, never from what the cast took);
- `<name>.landed` -- the player within `radius` (teleport.rs2 `map_findsquare`, 2) of `landing` on
  its level.

It answers `ok` only when all three passed, else `refused` naming the rows that did not. A step the
guide does with a teleport is done with this, not with `goto_tile` or `::tele`.

### `t.player.climb(spec)` -- a staircase, ladder or trapdoor, graded on the level and the landing (b60-seam1)

`t.player.climb{ loc=, at={x,z,level}, dest={x,z,level} [, op=1, op_name="Climb-up", slack=0,
src={x,z}, landed_ok=fn, landed_desc=, presses=2, ticks=10] }` -> `(ok, detail)` `refused` `covered`
`not_visible` `timeout` ...

```lua
-- No maplink row: ladders.rs2 [proc,climb] moves the player one plane on the tile it stands on.
t.exec("talkToDuke.stairsUp", t.player.climb, { loc = "spiralstairsbottom_3", op = 1, op_name = "Climb-up",
    at = { 3204, 3229, 0 }, src = { 3205, 3228 }, dest = { 3205, 3228, 1 } })
t.exec("talkToGillieAgain.stairsDown", t.player.climb, { loc = "spiralstairsmiddle", op = 3,
    op_name = "Climb-down", at = { 3204, 3229, 1 }, dest = { 3205, 3228, 0 } })
-- A maplink row telejumps to its dest from any approach tile; a room test narrows the landing.
t.exec("goUpToJohnathon", t.player.climb, { loc = "fai_varrock_stairs_taller", at = { 3285, 3493, 0 },
    dest = { 3285, 3496, 1 }, slack = 2, landed_ok = in_inn_upstairs, landed_desc = "the inn's upper floor" })
```

`goto_tile` is a teleport and `walk_to` never changes floor, so every level change past a staircase
is this verb (b60: crest, idesofmilk, vampire and fenkenstrain each hand-wrote a `climb()` for it).
`at` names the copy pressed AND the floor it is pressed from (a copy on another raw level, a
bridge deck: `loc_level`, "A loc on another raw level" above); `dest` names the landing and its NEW
level (a dest on `at`'s level raises: a one-floor crossing is `pass_door`/`cross_trap`/`walk_route`).
The verdict is the world: the player on `at`'s level before the press (else `refused ... not
pressed`), on `src` exactly when `src` is given (walked there first; else `refused ... not pressed`),
and after the press on `dest`'s level within `slack` (Chebyshev) of its x,z with `landed_ok(tile)`
true, awaited `ticks`. Which `dest`: a stair with a `maplink.dbrow` row lands on the row's dest tile
whatever tile it was pressed from; one with no row (`[proc,climb]`, ladders.rs2:69-78) lands on the
approach tile one plane up or down -- give `src` and the same x,z, or a `slack`. A press the client
could not land (`covered`, `not_visible`) from the start floor is pressed once more (`presses`,
vampire b60 run 2's stairstop); a press the server answered is never repeated. The detail names
every press, its landing and the chat it caused: `You can't go any further.` with the player still
on `at`'s level is a stair whose route the port does not have -- a content seam (Draynor Manor's
crypt stairs, vampire b60), stop at `t.blocked("content_bug: ...")`. `reached level N but not the
landing` is the wrong `dest`. Conformance `player.climb` climbs Lumbridge castle's stairs up and
down and refuses a press from the wrong floor.

### `t.player.cancel_selection(why)` -- drop a spell or Use left armed (b60-seam1)

`t.player.cancel_selection([why])` -> `(ok, detail, was_armed)` `refused` `not_visible` `timeout`.
`why` is only echoed at the head of the detail, and is what lets it go through `t.exec` (a nil first
argument is `bad verb/target`).

```lua
t.exec("dropSpell", t.player.cancel_selection, "fire blast left by the Chronozon kill")
```

An armed spell (or held-item Use) survives everything but a menu row's doAction tail or a left click
off any target: not a closed menu, a teleport, an `if_click`, a tick. While it is armed every world
press reads `covered ... menu rows: <Cancel>` (crest b60 run 1 rows 138 and 147: a click_obj and a
gate). The verb right-clicks the world beside the player and reads the menu: "Walk here" is offered
on every world right-click EXCEPT while a selection is armed (rs_minimenu_world.c), so no Walk here
IS an armed selection; it presses Cancel and reads the menu again -- `ok` with `was_armed` true only
when Walk here is back. Nothing armed: `ok`, `was_armed` false, the menu closed with Cancel. Nothing
walks. The driver now calls it itself in two places, so a test rarely needs it: a cast press that
missed on every try (`covered` x3) cancels the arming it made, and `npc.await_dead_engaged`'s cast
wrap cancels whatever is armed when the wait ends -- its detail ends `fight over with nothing armed`
or `fight over with a selection still armed -- cancel_selection: a selection WAS armed ...` (seam
`seam.cast_fight_ends_with_nothing_armed`). Use it by hand after a spell you armed and did not spend.

### `t.world.tile()` -- also `t.world.level`

`t.world.tile()` -> `(ok, {x,z,level})`. `t.world.level()` -> `(ok, level)`.

### `t.drive.screen_position(target)`

`t.drive.screen_position(target)` -> `(ok, pos)` `not_visible`.
`target = {kind="npc"|"loc"|"obj", id=...}`.

### `t.drive.click_minimenu(target, option, deadline=4)`

`t.drive.click_minimenu(target, option, deadline=4)` -> `(ok, {row_text, row_action, element_id})`
`covered` `not_visible` `timeout`. `option` is a 1-based op slot, `"examine"`, or `"select"`
(held-item wildcard). When every pose has answered `covered`, it SEARCHES for a press pixel before
giving up -- at the poses it already framed, most viewport-reach first, sharing one budget of 99
probes for the whole click, so the sweep costs no more probing than the single hunt did.

A loc projects at its footprint centroid at GROUND level and the model is drawn above it (measured
16-96 px up, up to 40 px sideways), and WHICH POSE matters as much as which pixel: cog's black
spindle holds at the flattest pose and at none of the other four. The search stays a last resort by
measurement, not by caution -- every earlier position for it, down to re-framing once more at the
end, cost green quests their rows.

An optional fifth argument `single` (seam35) answers the FIRST press -- with its npc re-aim -- and
returns a `covered` at once instead of running the pose loop, so a caller with a cheaper recovery
can try it first. Only `t.player.attack`'s first press passes it (verbs-combat: "A covered Attack
press, a boss that teleports, a timed walk"); every other press is unchanged.

#### The 8,192-row loc pool and the instruction budget

Many scenes fill the 8,192-row loc pool (`DRIVE_UI_POOL_CAP`: the Temple of Light, the Taverley
dungeon, Eadgar's cave; `api_drive.locs(0)` is truncated there), and a `scan-meter: yield one frame`
line in client.log means a verb walked enough of it in one resume to near the 400k Lua instruction
budget, so the driver took a frame -- not an error. `instruction budget exhausted` from a pool walk
is a driver seam, not a test bug.

### `t.drive.op(target, option)`

`t.drive.op(target, option)` -> the logged bypass, never the default -- leaves a `note` in the next
row's detail. It builds the packet with no pixel and no route, so an `ok` from it says the SERVER
accepted the op and nothing at all about whether a player could have pressed it from that tile:
never cite one as evidence that a tile is reachable.

### `t.drive.camera(yaw, pitch, zoom)`

`t.drive.camera(yaw, pitch, zoom)` -> `ok`.

### `t.player.by_symbol(kind, name)`

`t.player.by_symbol(kind, name)` -> `(target, "ok")` or `(nil, result, name)` -- reversed order from
every other verb here. It only RESOLVES a content symbol (`api_drive.symbol`, `pointer.lua`
`QD.player.by_symbol`) and answers `ok` for any valid symbol whether or not a copy is live -- never
use it as a presence check; `t.npc.by_symbol`/`t.npc.nearest` read the live pool.

### `t.player.walk_to(x, z, ticks=distance+10)` / `t.player.walk_near(target, ticks, minimum=0)` / `t.player.idle()`

`t.player.walk_to(x, z, ticks=distance+10)` / `t.player.walk_near(target, ticks, minimum=0)` /
`t.player.idle()` -> `ok` `timeout` (`unsupported` -- walk_near is npc/loc only).

#### A walk stops at an obstacle: cross it with `click_loc` (seam34)

`walk_to` follows the collision map. A loc the game makes you CROSS (a rock bridge, stepping
stone, log, ledge or pipe) blocks that map until you press its op, so a walk across one stops
beside it. That is the game's rule, not a driver bug: the server's own pathing stops there too.
A `walk_to` that walked over the obstacle would skip the guide step and its agility roll.
Underground Pass's maze bridges (`walkway_upass_narrow_mid_top`, `blockwalk=1` and `op1=Cross` in
`all.loc`; LostCity's `upass.loc` blocks too; `[oploc1]` at `upass_obstacles.rs2:360` can drop
you off) are the case that found it.

A stalled walk names the locs with an op within two tiles of where it stopped, nearest the
destination first:
`walk_to 2383,9634 from 2379,9634 stalled at 2380,9632 -- locs with an op beside the stop:
walkway_upass_narrow_mid_top at 2380,9634 op1 (a bridge, log or stone among them is crossed with click_loc, then walk on)`
(`build/quest_gate/s34bridge_after`). Walk to the obstacle's near side, then
`t.player.click_loc(sym, op, { at = { x, z } })`, check the tile you land on (a failed roll drops you
off it; retry), then walk on. The list is only a hint: a door or tree beside the stop is listed too.

### `t.player.teleport(name)`

`t.player.teleport(name)` -> `(ok, "x,z L<level>")` `timeout` `refused` `no_row`. See trap 1's
cousin below.

### `t.player.goto_tile(x, z, level=0, ticks=10, attempts=3)`

`t.player.goto_tile(x, z, level=0, ticks=10, attempts=3)` -> `(ok, "x,z,level")` `timeout` `no_row`.
An ABSOLUTE tile (the WorldPoint Quest Helper prints for every step) through `::goto`; returns only
once the tile AND the npc pool around it are visible. It RE-ISSUES the teleport: a content teleport
that lands behind the cheat (`p_delay(n); p_telejump(...)` -- every boat, trapdoor and cutscene in
this pack) used to take the player back and make this a hard FAIL on a tile it reaches perfectly
well, and Sea Slug proved it inside ONE run, row 27 FAIL against row 61 PASS on the same tile with
the same verb.

A first-attempt landing reads exactly as it always did (`at x,z,level`); a retry SAYS SO --
`at 2784,3286,1 on attempt 2 of 3 via ::goto -- attempt 1: never arrived, still at 2782,3273,0 after 10 tick(s) -- server said '...'`
-- so a row whose detail names two attempts is standing next to something that teleports and is NOT
a bug to chase.

#### A `timeout`, and when to use it

A `timeout` accounts for every attempt with the server's own last line on each: three identical
lines mean the tile is wrong, three different ones mean something else is moving the player. Use it
before the first `talk_to` of any step the fixture does not already stand at; `teleport(name)` is
for destinations that have a NAME in `tele_destinations.rs2`.

#### Since seam 15 a goto stops the player's action

Since seam 15 `::goto`/`::tele` first stop the player's action like LostCity's `::tele` (close the
modal, clear the interaction AND the fight, drop the map flag), so a goto issued mid-fight no longer
runs the player back toward the npc (Ernest the Chicken's goto-fountain). A goto fired inside a loc
script's `p_delay` is still overwritten by that script's own `p_teleport` (the manor bookcase): that
is the `attempt 2` row, or wait a tick after such a click.

### `t.player.talk_to(npc, op=1, opts)`

`t.player.talk_to(npc, op=1, opts)` -> `ok`/click_minimenu's results. `opts` is the same npc
selector as `press` (`{ slot = n }` / `{ at = {x, z} }`); the detail then starts
`talked to slot N (element E) at x,z; ...`. `talk_to`, `press` and `click_minimenu` on an npc re-aim
ONCE when the COPY under the pressed pixel stepped between the aim and the press (followed by its
element, never swapped for another copy); the row then says
`<npc> moved a,b -> c,d between aim and press; re-aimed at x,y`.

A multinpc target is resolved through `QD.player._live_npc_id` (pointer.lua): a live row whose
`npc_id` is the symbol's id first, then a row whose `base_npc_id` is. So a CHILD symbol
(`frog_quest_gary_unnamed`) finds the npc only while the varbit selects that very child; target the
spawned SHELL symbol (`frog_quest_gary`) to follow the npc across its forms (seam-facts: Seam pass
matthew-mbp-m4-b53-seam2 (b)).

### `t.player.press(npc, op=1, ticks=8, opts)`

`t.player.press(npc, op=1, ticks=8, opts)` -> `ok` `timeout` `refused` `no_row` / by_symbol's and
click_minimenu's own results. ONE numbered op on an npc, settled on THE NPC ITSELF MOVING -- for an
`[opnpc<n>]` whose success is SILENT. `talk_to` and every other click verb settle on the chat ring
or the dialogue interface, so a content label whose whole body is `anim` + `npc_say` (overhead text,
not a chat line) + `npc_walk` can only make them TIME OUT ON SUCCESS: Sheep Herder's
`[label,prod_sheep]` (diseased_sheep.rs2:117-121) is that shape, and fifteen prods that all landed
and all moved a sheep were reported `timeout`.

`press` snapshots the npc pool keyed by element id, presses through the ordinary `click_minimenu`,
and watches THE COPY THE PRESS NAMED (`click_minimenu`'s own `element_id`), so a neighbour's wander
is never credited to your click. The detail is the step --
`npc slot 117 2610,3345 -> 2609,3345 (1 tile(s), you at 2611,3345, range 1->2; away from you)` --
or, when the world answered in words instead, the dialogue page or content line, which are checked
BEFORE the tile every poll and always win. A step that does not take the npc further from you does
not end the wait; it is reported at the end with `may be the npc's own wander` in it.

#### Four outcomes

FOUR OUTCOMES, not three, and a loop that presses the same npc over and over lives on the
difference: `ok` + `npc slot N ... away from you` = it stepped; `ok` +
`it said 'X' and did not move ... the press landed; the step is what did not happen` = the op RAN
and the server answered, and the only thing that did not happen was the step -- read that as a
WALLED DIRECTION and come at the npc from another side, never as a lost click; `refused` = the
engine would not send it (out of range); `timeout` = nothing came back at all, which is now the only
shape that means the press may not have landed.

The say is the npc's OVERHEAD text and never reaches the chat ring (below), it is read off the npc
pool row's `overhead`, and it deliberately does not end the wait -- `npc_say` and `npc_walk` arrive
in the same NPC_INFO update, so resolving on the say would lose the step text a herding loop steers
by. Use `talk_to` for anything that answers with a conversation; this verb is for the press that
answers with a footstep.

#### Naming the copy: `opts` (seam13)

WITHOUT `opts` a bare symbol resolves through `App_NpcScreenPosition` to the live copy nearest the
camera VIEWPORT CENTRE, not the one nearest you or the one you walked to. `opts` NAMES the copy
(seam13): `{ slot = n }` (the server slot `t.npc.tiles` and this detail print) or
`{ at = {x, z[, level]} }` (the tile it stands on NOW); the press aims that copy's element, only
that copy's menu row is taken, and the detail starts `aimed at slot N (element E) at x,z; ...`.

A selector that matches no live copy answers `no_row` naming it and listing the live copies
(`nothing pressed; a selector never falls back to another copy`); a covered one reads
`the copy named slot N (...) at x,z: covered ...`. Track the SLOT you are pushing, not its tile -- a
tile goes stale the moment the npc wanders (78 `no_row` in seam13's herd probe).

#### A directional push reads the tile you actually stand on

A DIRECTIONAL push (stand opposite the goal, the press shoves the npc away from you) reads its
direction off the tile the player ACTUALLY stands on, not the tile a `walk_to` was aimed at: a walk
that stops short on a fence or wall leaves you on the wrong side, and the press then drives the npc
the OPPOSITE way and undoes earlier progress -- unlike the `use_on` bullet's interior-fence wrong
side below, which merely fails to reach.

It surfaces once the npc can drift between the walk and the press (Sheep Herder's plaguesheep_3 went
2592,3383 -> 2552,3392 over 28 presses after the seam15 wander parity), so read `t.world.tile()`
after every approach walk, compare its side against the stand tile you meant, and go around or skip
the press when they differ (`sheepherder.lua`'s `wrong_side` outcome).

### `t.player.click_loc(loc, op=1, opts)`

`t.player.click_loc(loc, op=1, opts)` -> same, walks into range first. On `I can't reach that!` it
walks the loc's OTHER approach tiles and re-presses from each (the orthogonal neighbours of every
copy of it, the loc's own squares last), and the row names the tile that worked -- so never
hand-code a `goto_tile` per side to work around a reachable-from-one-direction loc. When no approach
tile a route can end on serves it, the row FAILS `refused` with a detail starting `reach_failed:`
and naming every tile tried; the retry ::goto's onto the loc's own square only when the call says
`t.player.click_loc(loc, op, { stand_on_square = true })` /
`t.player.use_on(item, target, { stand_on_square = true })`, and that opt-in needs a `-- GUIDE-GAP:`
marker within 8 lines above it (trap 32).

#### Field crops pick on op 2; op 1 answers `menu has no row for it` (matthew-mbp-m4-b47)

Wheat, potato, onion, cabbage, sweetcorn and flax in the fields pick on op 2 (`[oploc2,...]` in
`general_use/scripts/pickables.rs2`). The loc has no op 1, so `click_loc(<crop>, 1)` fails with
`menu has no row for it`, which reads like a missing loc. Write `t.player.click_loc(<crop>, 2)`.
Grapevines, Draynor's magic cabbage and the brewing flowers are the exceptions that pick on op 1.

#### A press whose walk outlasts the 20-tick settle is followed (seam35)

A `click_loc` whose route is long and walked (Haunted Mine's valve to the lift: 4 tiles apart, a
60-step route, run energy 8-13 late in the quest) used to answer `timeout settle_after_click` with
the player still walking, and the next row's `::goto` cancelled the walk. Now a `timeout` while the
player is still moving is taken again, round by round, for as long as every round moved him, up to
80 ticks in all (`QD.player._long_walk_ticks`); the arrival's own answer (a chat line, a page, a
teleport) resolves it and the row notes `click_loc: the walk outlasted the 20-tick settle; followed
it N more tick(s)`. A player who stood still through a round is the old `timeout`.

#### A symbol STRING, not a table; the Temple of Light doors (seam 14)

It takes the loc SYMBOL STRING, where `use_on` and `walk_near` take the `{kind,id}` table
`by_symbol`/`loc_near` answer with -- handing `click_loc` that table raises
`bad argument #2 to symbol (string expected, got table)` and ENDS the run. In the Temple of Light
(Mourning's End Part II) an `I can't reach that!` whose walks all END on one tile beside a Door of
Light is a real collision cut, not stale state (seam 14): every barrier is blockwalk in both states
and is crossed only by its `Pass-through` op while a beam lights it, so press the LIT door before
any chest behind it (Chest #4 from the rope: `mourning_door_1_13_north`, Mirror #9 reflects north
per the wiki's chest-4 map; Chest #2: the lit `2_4_west`, not from the south).

Which barrier a pre-placed mirror lights comes from the wiki's chest-N solution map, not Quest
Helper's `cyanDoorOpen` varbit; floor-0 ids from `maps/m29_72.jl2`: 9778/9779 door_1_13_north/east,
9758 the parts_4 chest.

#### One named copy: `opts = { at = ... }` (seam26); other floors

`opts = { at = {x, z[, level]} }` (seam26) presses ONE named copy of a loc planted many times
(stepping stones, a row of doors) -- the copy nearest you is often the one underfoot; a tile with no
live copy answers `no_row` and presses nothing, never another copy, and the detail says
`pressed the copy at X,Z,L (element E)`. `t.player.use_on(item, loc, { at = ... })` takes the same
selector. A loc whose every copy is on a LOWER floor answers `not_visible` `other_floor: ...` naming
the locs on your floor at that square: a ladder's two ends are usually two locs, one per plane
(Watchtower first floor `qip_watchtower_ladder_top`, ground `towerladder`) -- read the square's jl2
rows for your plane.

`other_floor` is never a driver seam (vm-b1-seam1). The game's own client cannot press a loc on
another plane either: the pick drops it, and an oploc names only x,z and resolves on the player's
plane. The detail now ends `-- the game's client cannot press it from here either: reach level N by
the guide's route first (a missing route is a content seam, not a driver one)`. Reach that plane by
the guide's route; if the port has no route, report a CONTENT seam. Measured: Cold War's Ice steps
(`peng_agility_steps01`, 2635,4054,0) answer `other_floor` from level 1 and climb first time from
2634,4054,0 (`teleport: 2634,4054,0 -> 2634,4054,1`).

### `t.player.click_obj(obj, op=3)`

`t.player.click_obj(obj, op=3)` -> same, waits for the backpack count to rise.

Take is op 3 (the default). An op the obj does not have (op 5 on a knife) answers `covered`/"menu
has no row for it" even though the menu dump lists a Take row -- pass 3. A tile holding a STACK of
ground objs is fine: each obj is its own element with its own Take row, picked by name
(seam29: black bead, red bead and ashes taken by name from a 6-obj imp drop pile, s29lava_stack),
and so is an obj on a table tile that also holds a loc (Carnillean kitchen knife and bread,
s29lava_hazeel).

An obj lying ON a table or another blocking centrepiece (Legends' carved rock) is drawn RAISED onto
the loc: `raiseobject` defaults to `blocks_walk`, and the client lifts the stack by
`World_ObjRaiseGet`. Since seam32 the driver projects the stack at that drawn height, so
`click_obj` presses the item on the first pose (the Lumbridge table: 30 ticks and a pixel hunt
before, 2 ticks after; the 8-tick placed sapphire on the carved rock is now taken). A detail that
says `pickset held=false ... menu has no row for it` with only the loc's rows, or `hunted ...
hovered +0,-48/-64` on a table, is a binary built before seam32.

#### `click_obj` answers `timeout` on a pickup that landed; `expect_has` misses an obj with id 0 (FIXED seam32)

FIXED in seam32 (seam-facts: Seam pass 32 (c)): the client's empty sentinel is -1 everywhere, so
obj 0 is drawn, clickable and counted. `click_obj("mcannonremains", 3)` answers `ok click_obj: met
after N tick(s)`, and `t.inv.count` / `expect_has` / `await` see it. Prove a pickup with
`t.inv.await` / `expect_has` like any other item. `t.inv.slot` and the use-on diff
(`_inv_contents`) name it too since seam33 (seam-facts: Seam pass 33 (b)). The history follows.

Reported by the Dwarf Cannon author (sonnet-b36). `t.player.click_obj("mcannonremains", 3)` on the
guard-tower floor answered `timeout`, but the remains were in the backpack. Captain Lawgof's hand-in
(`mcannon_commander.rs2:195`, `inv_total(inv, mcannonremains) < 1`) accepted them, and the mesbox
read "You give the Dwarf Captain his subordinate's remains". `t.inv.expect_has` cannot see an item
whose obj id is 0 (`mcannonremains`), so it cannot prove this pickup. Prove it with the next step
that consumes the item: the stage it advances, and `t.inv.expect_absent` after the hand-in. Do not
write `t.check("getRemainsStep", true, ...)`. A row that always passes proves nothing, and its
detail says `timeout`.

seam31 found the cause, and it is the CLIENT, not content or the driver: obj 0 is a real id
(`mcannonremains` is the cache's Dwarf remains; no other content obj resolves to 0). The server and
the wire carry it correctly (`w239_inv_slot` writes obj+1; the client stores wire-1 with -1 as the
empty sentinel), but about 25 client sites test `obj_id <= 0` / `> 0` for "empty" (uitree_emit.c,
inv_manager.c, app_minimenu.c, rs_cs2_host.c, torirs_plugin_drive_state.c's `assert(obj_id > 0)`,
...). Until an engine seam makes them `< 0` / `>= 0`, a setup `::give mcannonremains` reads as
"the cheat answered ok and no mcannonremains reached the backpack", and no `t.inv.*` verb can see
the item.
