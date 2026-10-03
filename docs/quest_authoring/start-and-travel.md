# Test shape, where you start, and getting there (sections 1-2)

Sections 1 and 2 of the authoring manual, reflowed. The skeleton here is the same one the core
`docs/QUEST_AUTHORING.md` shows. Section 2's bullets are the travel rules every step needs.

## Section 1. The test shape

```lua
return {
    id = "cooks_assistant",
    fixture = "fresh_lumbridge.ini",
    -- Setup STAGES a quest, never finishes one (trap 8): `::cook` resets
    -- the varp, clears ingredients, puts the player beside the Cook.
    setup = { "::cook" },

    run = function(t)
        -- bind touches the world not at all: it records varp/constants/
        -- display/points for later, and reads %qp now for that delta.
        t.quest.bind({
            varp = "varp29_cookquest",
            constants = { not_started = 0, started = 1, complete = 2 },
            display = "Cook's Assistant",
            points = 1,
        })
        t.ticks(3)  -- a setup cheat's effect is not client-side yet
        t.expect("cook.reset", t.quest.expect_stage("not_started"))

        -- t.exec(name, verb, ...) writes row `name` from (result, detail)
        -- and SHOOTS it; t.expect does not -- photograph clicks, not reads.
        t.exec("cook.greet", t.player.talk_to, "cook")

        -- chat.play answers ("ok", "<N> page(s): ..."), a real detail, so
        -- it goes through t.exec like any other verb.
        t.exec("cook.accept", t.chat.play, {
            "npc:What am I to do",
            "choose:What's wrong?",
            "player:What's wrong",
            "npc:terrible mess",
            "npc:forgotten to buy",
            "choose:Yes, I'll help you.",
        })
        t.expect("cook.started", t.quest.expect_stage("started"))

        -- ... gather, talk to the Cook a SECOND time, dismiss the mesbox ...
        t.quest.expect_complete()   -- four rows, and it closes the scroll
        t.finish(0)
    end,
}
```

An excerpt, not a whole quest (every line above ran and PASSed, but a file
this short fails section 7's minimum shape). Read the complete green files
next: `test/quests/cooks_assistant.lua`, then `hans.lua` (no quest varp).

## Section 2. Where you start, and how to get there

### Where the fixture stands you

The fixture (`fresh_lumbridge.ini`) stands you beside Hans at 3206,3233, level 0 -- nothing else
moves you closer. Npcs are ALL spawned and live from boot; nothing needs summoning.

### `goto_tile` before every far step -- fix the goto's coordinates, never the verb

> CONFLICT (kept both, undated): this bullet says "a LOC's tile is its area's `configs/*.loc` row";
> section 8 (`gaps-world.md`, first gap) says no `*.loc` in the pack carries an x/z/level key and loc
> placement lives in the cache map (`maps/*.jl2`, trap 29). Neither passage is dated, so the
> later-date rule cannot settle it; trap 29's jl2 decode agrees with section 8.

The scaffold emits `t.exec("goto-...", t.player.goto_tile, x, z, level)` -- the engine's `::goto`
cheat plus an arrival await -- before every far step. `goto_tile`, never `goto`: `goto` is a
reserved word in this tree's Lua and does not parse. `level` is the plane, 0-3, default 0; x/z may
land a tile out (the world picks the nearest tile it accepts), the plane never does.
`screen_position` / `not_visible` / `no_row` from a `talk_to` or npc lookup means you are not
standing there: **fix the goto's coordinates, never the verb** -- the scaffold walks to the npc's
own `configs/*.spawn` row (trap 13; `configs/*.npc` is definition blocks, no tiles in them at all)
and falls back to Quest Helper's `WorldPoint(x, z, level)` only where no spawn row exists; a LOC's
tile is its area's `configs/*.loc` row (upper floors are level 1/2, never 0).

A LOC target that shares the player's own tile no longer answers
`not_visible: target shares the player's tile` -- `click_loc`/`use_on` step off it first, and the
message survives only as `target shares the player's tile and the step off it did not land`, which
means every one of the eight neighbours refused the walk (a walled-in loc), not that the camera is
pointed elsewhere.

### An npc behind a counter or wall: goto an ORTHOGONAL tile, not a diagonal one (matthew-mbp-m4-b54)

An npc that stands behind a counter, a bar or a wall corner is reached only from a tile that
shares its row or column across the gap. A goto to a tile diagonal to it lands, and then
`talk_to` walks around or answers "I can't reach that!", because the diagonal crosses a wall
corner. Put the goto on the open side, straight across from the npc. Tiles that work: Bone
Voyage's Varrock sawmill operator from 3303,3493, the Woodcutting Guild operator from
1623,3501, and Shadows of Custodia's bartender from 1391,3353. A press that moves a walled npc
is a different case (verbs-pointer: Four outcomes, "WALLED DIRECTION").

### A counter whose tiles are floor-blocked, with the npc two tiles away: the talk is refused (At First Light, matthew-mbp-m4-b56)

The rule above needs the npc to stand straight across a ONE-tile gap. Verity (1559,9464) stands
behind a bar whose counter row, z 9463 x 1556-1560, is floor-blocked in the map flags. There is no
counter loc there, and the flap `hg_table_tavern02_door01` has no op. `talk_to` from 1559,9462,
1561,9463 and 1559,9461 all answered "I can't reach that!". The tiles behind the bar are a sealed
pocket, so a goto there is a teleport past the bar. The row is a content_bug, not a goto. Name the
counter tiles and the refused tiles in the row.

### A loc inside a walled building, and the guide names no door: find the door in the map square (matthew-mbp-m4-b54)

A `walk_to` or `click_loc` from outside stops at the wall, and the easy fix is a `goto_tile`
inside. Find the building's door first and use it; a goto inside is the last resort. The
map square file lists every placed loc: `OSRS-Content/osrs239-content/maps/m<X>_<Z>.jl2`,
where X = x / 64 and Z = z / 64. Each line reads `level lx lz: id shape [rot]`, with
lx = x - 64*X and lz = z - 64*Z. Shape 0 is a straight wall, shape 3 a wall corner. Rot is
the tile edge the wall sits on: 0 west, 1 north, 2 east, 3 south. The id is named in
`configs/all.loc.compack` (`id=name`). A door is an id whose `configs/all.loc` record has
`op1=Open`, with a row in `server/scripts/doors/configs/doors.loc`. Stand on the tile outside
it and `click_loc` it with op 1, then walk in.

Example: Swan Song's stove (2316,3668) is in a building whose walls in `m36_57.jl2` are
`deal_wall`/`deal_wall_window` (ids 10109/10108), inside x 2316-2321, z 3666-3672. The
round-1 test did `goto_tile 2317,3668` because it had not found the door. The door is
`swan_building_door` (id 12657, `0 18 19: 12657 0`), on the west edge of 2322,3667 in
the east wall. Walk to 2322,3667, then `click_loc("swan_building_door", 1)`.

**The grader now catches the goto inside (matthew-mbp-m4-b56-seam1, `enclosure_entries`).** A
`goto_tile` that lands in a room the map walls in, with a closed door in its perimeter, from a tile
outside that room, is CHEAT whether or not the guide names the door. A PASS row that pressed that
door in the 500 ticks before (an opened door re-closes after 500) makes it a walk. The detail
names the door and the map square: `lands at 1542,3570,0 in a room the map walls in (11 tiles ...
door wallkit_shayzien_door01_l_reverse at 1540,3570,0: maps/m24_55.jl2) from ... it went past the
closed door (enclosure_entries)`. Fix it with the route above: goto to the street,
`t.player.click_loc(<door>, 1)`, then `walk_to` the tile inside. The first goto of a run is not
judged. Not judged either: a building with a climb down from level 0, a room over 400 tiles, a loc
content adds at run time, a sealed room with no door. Bone Voyage's sawmill tile 3303,3493 in the
section above is inside such a room: open the sawmill door first.

### No charter verb: reach a charter port with `goto_tile` when the guide allows any route (matthew-mbp-m4-b54)

No driver verb works Trader Stan's charter map (`transport_charter/scripts/charter_npc.rs2`
`[label,charter_op]` opens `~charter_map_open`). Ethically Acquired Antiquities' guide step
`talkToTraderStan` says "Charter at a cost of 3000 coins", but the wiki says the 3,000 coins are
only one recommended way to get there and the player may reach Port Sarim by any route
(`docs/quests/ethically_acquired_antiquities.md:50-52`). So `goto-stan` from the Fortis Cothon to
3039,3193 is plain travel, and the coins are not a bring-along to `::give`. If the voyage itself
is the guide's step (the quest's state changes on arrival), a goto is not enough: report the
missing verb as a gap.

### `walk_near` takes the `{kind=, id=}` table

`walk_near(target, ticks, minimum)` takes the `{kind=, id=}` table `player.by_symbol` returns:
`local n = t.player.by_symbol("npc", "doric")` then
`t.exec("walk.doric", t.player.walk_near, n, 10)`. `minimum` is a standoff and authors never pass
it: `click_loc`/`use_on` pass 1 for a loc themselves, so nobody has to re-invent cog.lua's
hand-written "stand 3 tiles off, not on it" comment.

### A dialogue or loc that TELEPORTS you (and the stile hop)

A dialogue that asks the server to TELEPORT you -- Aubury's, Cromperty's and Sedridor's "Can you
teleport me to the Rune Essence?" -- is served a few ticks AFTER its page closes, so the teleport
lands on top of whatever you clicked next. `goto_tile` survives that case by itself now (it
re-issues), but the await (`t.await` on `t.world.tile()` reaching the destination's plane or region)
is still the way to be sure WHICH scene you are in before a PRESS.

Enter the Abyss looked intermittent for two batches over this -- the client was still drawing the
Rune Essence mine while the next row pressed at Cromperty's house, and every press there answered
`covered` because nothing of Cromperty was on screen to hittest.

#### Teleport locs settle on the `teleport` arm (seam10)

A LOC whose `[oploc<n>]` body is a bare `p_teleport` -- no chat line, no mesbox, no interface
change: a ladder, a staircase, a Temple of Light door -- used to read `timeout settle_after_click`
on a teleport that landed; since seam10 `_settle_after_click` has a fifth arm, `teleport`, that
resolves once the player's tile has moved in a way no walk makes (a plane change, a jump of 3+ tiles
in one read, or ANY move while no route was issued from an idle start) and held for 2 ticks, and the
row reads `ok teleport: 1898,4666,2 -> 1898,4666,1 (a jump no walk makes, held 2 tick(s))`. Still
read `t.world.tile()` after a crossing the guide names -- the arm proves the player MOVED, not that
he landed where the step needs him.

#### A short hop (stiles) does not trip it

A SHORT hop does not trip that arm: `stiles.rs2` `[oploc1,_stile]` (lines 45-54) is
`p_teleport($start)` + a silent `~agility_exactmove` + `p_teleport($land)` -- under 3 tiles, after
the click issued a route, no chat line -- so `click_loc` reads `timeout` on a crossing that landed.
Call it directly and grade the row on a `t.world.tile()` read of the far side.

### `outcome blocked` and `content_bug` end in `t.blocked(...)` then `return`

**`outcome blocked` REQUIRES a `t.blocked("<seam>")` row, then `return` right after it** -- anything
else is rejected, not blocked. The same holds for `content_bug`: its file ends in
`t.blocked("content_bug: <finding>")` and a `return`, exactly like a blocked one.

### The chat verbs act on a dialogue already open

`chat.play` / `chat.choose` / `chat.continue_` act on a dialogue already open, never opening one;
`not_visible: no dialogue is open` means the click before them did not land -- fix that click, not
the chat call.

### Doors: `I can't reach that!`

Doors: `I can't reach that!` after a click means a door, gate or wall blocks the path -- `click_loc`
it (op 1, its symbol from the area's `configs/*.loc`) or `goto_tile` past it; `talk_to` now answers
`refused` with that line, not `ok`. If EVERY click answers it, from every tile, the player may be
walled in on one square: read the route trace (`TORIRSSERVER_VERBOSE=1`, `steps=0` from every
neighbour) before blaming reach code -- the Ghosts Ahoy lower hull was sea-overlay floor blocking,
fixed in `torirs_server_scene.c` (seam20).

#### FIXED (seam21): hidden multiloc placements come back

FIXED (seam21): a multiloc placement hidden by a -1 rung is remembered by the client's world builder
and re-placed when its varbit changes, with no scene rebuild, as the reference DynamicObject does
(Deobfuscator `class123.java` method4108/4109; `world_builder.c` WorldBuilderHiddenLoc +
`app_varp_transforms.c`), so a hidden statue reads `not_found` only while its rung is -1 (Lady
Table's room drives end to end: `build/quest_gate/s21ml_after2`, `s21ml_static_after2`). A hide/show
proof drives BOTH paths: the varbit changing in scene, and the scene rebuilt while hidden.

### Inventory: the tutorial kit and `::clearinv`

Inventory: the fresh character carries fourteen slots of tutorial kit (content's
`[proc,newplayer_inv]`, granted a tick after login); every generated file's `setup` starts with
`::clearinv` -- add it yourself too when hand-writing `setup`, and never in `run`: the runner waits
for that grant before the first setup cheat, nothing waits for you.

### Floors and ladders

Floors and ladders: `goto_tile` the destination tile with ITS level is the whole of it, even onto a
staircase the guide names as its own ObjectStep: `helper_coverage.py` grades a step naming a
`TRAVEL_WORDS` word (ladder/stair/staircase/steps/trapdoor, `is_travel`) TRAVEL, not CHEAT, unless
the loc's own trigger writes a quest var (Fenkenstrain's castle stairs; `writes_quest_var`) -- it
climbs stairs and ladders for you, no `click_loc` on the ladder first (druid reaches Sanfew at
`2899,3429,1`, runemysteries the Duke at `3209,3222,1`, neither clicking anything); a scene that
fails to load after a multi-region jump (`talk_to` answers `screen_position`, npc lookups `no_row`)
is a known seam -- `t.blocked` it, naming the tile.

#### Why not `click_loc` the ladder: `~maplink_try`

Clicking the ladder/stairs loc directly instead can answer `refused -- You can't go any further.`
from the ladder's OWN tile: `~maplink_try` (`ladders_stairs/scripts/maplink.rs2`) is keyed on the
PLAYER's coord, not the loc's, so a click from wherever `goto_tile` actually landed you matches no
maplink row and falls through to `[proc,climb]`'s +/-1-plane default. `goto_tile` is the way, not a
`click_loc` on the ladder -- delete any scaffold-emitted `click_loc` row for a ladder or gate; the
same tile trick reaches an instanced area behind a trapdoor too (`z+6400`: `3120,9567,0` for the
Wizards' Tower basement), with no `click_loc` on the trapdoor at all.

A cellar ladder (`ladder_cellar`, 17384) is the exception since b54-seam2 (OSRS-Content
662de599a5): its name binding climbs from wherever you stand to `z+6400`, as LostCity does, so
`click_loc` on it works from any side (seam-facts: Seam pass matthew-mbp-m4-b54-seam2 (a)). After
it, `t.npc.await_present` the npc you want before the `talk_to` (same entry, (b)).

#### An agility crossing answers "Nothing interesting happens." off its source tile (sonnet-b44)

*Origin: author batch sonnet-b44 (regicide, content_bug).*

Logs, leaves, rocks and tunnels that end in `~maplink_agility`
(`skill_agility/scripts/maplink_agility.rs2`) look up `maplink_agility:src` by the PLAYER's coord
when the click lands, then match the loc. They teleport only from a row's exact `src` tile. Any
other tile answers `~displaymessage(^dm_default)` ("Nothing interesting happens."), and so does a
loc with no row at all. Before the crossing, `grep -n -B2 -A3 '<loc symbol>'
OSRS-Content/osrs239-content/server/scripts/skill_agility/configs/maplink_agility.dbrow`, stand on
the row's `src` (`click_loc(..., { at = {x, z} })` or a walk), and click. If no row names the loc
(Regicide's `regicide_logbalance*_start`), that is a content seam, not a tile to hunt for.
Regicide's dense forests (`regicide_cross_over*`) no longer read the table at all (FIXED seam37):
stand within 1 of the middle square of your side and click; the crossing is a 3-square forcemove,
and its row reads `teleport: A -> B (a jump no walk makes ...)` (seam-facts, Seam pass 37 (c)).

#### A trap you can walk over gets crossed the wrong way (matthew-mbp-m4-b48, fourth check)

*Origin: sampler matthew-mbp-m4-b48, fourth check of regicide (accepted, d06b64289).*

Regicide's Sticks (`regicide_trap_woodspring`, `configs/all.loc`: `blockwalk=0`) do not block the
path. Leg 3 stood on the east side (2238,3181) to cross west. The first press came from 2236,3181,
which is not the row's `src`. `click_loc` then walked the player over the trap and
pressed it from the west side ("pressed again from side 1 ... 2236,3181 -> 2234,3181"). The
successful roll carried the player back EAST, to 2237,3181. The `-tile` row passed anyway, because
it allows 6 tiles. The walk that followed went west straight over the trap tile, which shot 164
shows. Two fixes: stand on the `src` of the direction you want before you click (`walk_to` to the
exact tile), and grade the crossing on the exact far-side tile, not on a 6-tile box. A trap the
player can walk over is not a gate. Drive it because the guide names it, and check the direction.
