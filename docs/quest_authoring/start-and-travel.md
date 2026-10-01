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
            varp = "cookquest",
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
