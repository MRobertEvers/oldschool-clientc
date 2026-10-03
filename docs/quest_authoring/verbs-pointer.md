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
