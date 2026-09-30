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

### `t.player.click_obj(obj, op=3)`

`t.player.click_obj(obj, op=3)` -> same, waits for the backpack count to rise.

Take is op 3 (the default). An op the obj does not have (op 5 on a knife) answers `covered`/"menu
has no row for it" even though the menu dump lists a Take row -- pass 3. A tile holding a STACK of
ground objs is fine: each obj is its own element with its own Take row, picked by name
(seam29: black bead, red bead and ashes taken by name from a 6-obj imp drop pile, s29lava_stack),
and so is an obj on a table tile that also holds a loc (Carnillean kitchen knife and bread,
s29lava_hazeel).

#### `click_obj` answers `timeout` on a pickup that landed; `expect_has` misses an obj with id 0

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
