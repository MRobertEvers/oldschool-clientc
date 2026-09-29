# Section 8 gaps: the world, locs, coordinates and clicks

## `^*_coord` decoding, loc placement, `loc_add`, multiloc indexing, `docs/quests/`, `-- CHECK choose`, untransmitted carriers

> CONFLICT (kept both, undated): section 2 (`start-and-travel.md`) says a LOC's tile is its area's
> `configs/*.loc` row; this passage says no `*.loc` carries placement. See the note in
> `start-and-travel.md`.

*Origin: section 8 ("Gaps reported by authors").*

A quest's own `^*_coord` constant is `level_regionX_regionY_localX_localY`, and decoding it is how
you cross-check a Quest Helper WorldPoint against this pack's ground truth or find a tile no
`*.spawn` row names: `^ca_councillor_coord = 0_44_53_9_62` is worldX 44*64+9 = 2825, worldZ 53*64+62
= 3454; `^makinghistory_dig_coord = 0_38_49_10_4` is 2442,3140.

### A LOC's tile has no textual row; `loc_add` for an unplaced loc; multiloc = VALUE+1

Trap 20's "a LOC's tile" has no textual row behind it at all: every `*.loc` under `server/scripts`
is per-symbol op/category config (`op1=Check`, a transformed loc's missing action), never placement
-- no `*.loc` in the pack carries an x/z/level key -- because loc placement lives in the cache map
itself. There is nothing to grep the way `*.spawn` covers npcs, so trust `click_loc`'s own
three-rule resolve and its auto-walk rather than hunting for a spawn row this format does not have.

When a quest LOC is defined but placed NOWHERE at all, the answer is
`loc_add(<coord>, <loc>, <angle>, <shape>, 0)`: duration 0 means never-revert, re-adding the same
tile+shape replaces rather than stacks, and it is recorded and reapplied on rebuild even for a tile
outside the built scene window, so the call works from clear across the map
(`quest_waterfall_locs.rs2:272`, `quest_priest.rs2:185` are precedents). A `multiloc<N>` table is
indexed by VALUE+1 -- `multiloc1` is varbit value 0, `multiloc6` is value 5 -- so read the def's own
list before assuming a state maps to the number it looks like it should.

### Read `docs/quests/<quest>.md`; `content_bug` vs `blocked`

Read `docs/quests/<quest>.md` before driving a quest cold. Nothing else here points at it, and for
Current Affairs it already held the exact answer -- "Councillor Catherine has no world spawn and no
script creates her, so the player stops immediately after accepting the quest" -- that otherwise
costs a whole-tree grep plus a live probe to rediscover. It is also the line between the two
non-green statuses: a symbol that resolves in the compack but has zero live placements anywhere in
the loaded world is `content_bug` (a dialogue nobody can reach) once that per-quest audit doc or the
pack's own absence of a `*.spawn` row confirms it, and `blocked` only when the seam is the DRIVER's
-- a verb that cannot land the click on something that is really there.

### `-- CHECK choose (page N)` is a blind first-row fallback

Trap 17's `-- CHECK` markers are not all equal, and the one that reads most like a note is a
correctness bug: `-- CHECK choose (page N): no addDialogStep of [...] matched [...] -- row 1 used`
(`new_quest.py`'s `_resolve_and_emit_choice`) means the emitted `choose:` text is a blind FIRST-ROW
fallback, not a resolved answer -- a merchant's second page and all twelve of a riddle's rows come
out that way. Resolving it means reading that branch's `~p_choice2`/`~p_choice3` row list in the
`.rs2` and picking the row the quest actually wants (Dron's twelve answers are spelled out in
Blanin's own `mes()` lines), never deleting the marker and running whatever the scaffold guessed.

### A coord constant is where the player SHOULD go, not where the run puts him

A `^*_coord` decode is where a constant SAYS the player goes, not where the run puts him:
`^golem_demon_lair = 0_55_77_24_24` decodes to 3544,4952 and the live `[oploc1,golem_portal]` path
lands in region 42,76 instead (2721,4911 and a throne at 2719,4913,2 -- the plane `TheGolem.java`'s
`throneRoom` zone names), because a door can be registered as a maplink transition AND a scripted
`p_teleport` at once and the constant behind the script arm goes vestigial.

So trust a live run and its screenshot over the constant, and after any multi-region jump await
ARRIVAL by polling `t.world.tile()` before `t.world.loc_near` or a press -- the same treatment
section 2 gives a teleport dialogue. Write that predicate so it can FAIL: `tile.z > 4000` after a
goto that already stood you at z=4886 is true before the click and proves nothing, so compare
against the tile you left, and let the row's real evidence be the server var the arrival set
(`golem_seen_underground`) plus the shot.

### A stage poll that never converges: the carrier is not transmitted

A `t.var.server`/`t.quest.expect_stage` poll can fail to converge because the basevar is never
transmitted at all, not because it is slow the way traps 1 and 8 describe: check the basevar in
`configs/all.varp` -- `[mourning_quest]` has an EMPTY body where `[current_affairs_main]` declares
`transmit=yes` (`[makinghistory]` was the standing example until 2026-09-21, when
`quest_makinghistory/configs/quest_makinghistory.varp` fixed it; `murdersus` in `quest_murder.varp`
is declared but WITHOUT `transmit=yes`, which reads the same from a quest file), so every varbit on
it (`%makinghistory_prog`, `%makinghistory_trader_prog`) reads 0 on the client forever, however long
you poll.

THE FIX IS A CONTENT FILE, not a workaround: declare the carrier beside its quest in
`server/scripts/quests/<quest>/configs/<abbr>.varp` as `protect=no` / `transmit=yes` / `scope=perm`,
the way `quest_pryingtimes/configs/pryingtimes.varp`, `quest_kingsransom` and (2026-09-20) both
Mourning's End quests do, then `make -C src torirsserver-scripts`; an empty `all.varp` body is a
bare NAME RESERVATION and nothing more. When a poll never converges, stop polling and cross-check
through a channel that does reflect the change: `t.ui.journal_open("<display name>")` and its
`journal.first_line` run the quest's own `*_journal.rs2` proc server-side, and
`t.inv.await`/`t.inv.count` read real grants.

Then say in the row's detail which channel answered and why -- a run whose stage is unreadable has
to carry its evidence in the details, so call `inv.await` directly and write the count back (trap
12's habit) instead of folding it into a bare `t.expect` that lands a PASS with nothing in it.

## An `ok` from a CLICK verb is not the row's thing; the held `map_flag` ok (2026-09-20); fights are waits

*Origin: section 8 ("Gaps reported by authors").*

An `ok` from a CLICK verb is still not the thing the row is named after, and three more verbs join
trap 12's hollow list. `talk_to`'s own re-press-to-confirm can answer `covered` /
`menu has no row for it` AFTER the click already worked, because the dialogue it opened is now
covering the world -- traps 8 and 20 describe that only for `click_loc`/`use_on` on a loc, and it
happens to an npc whose `[opnpc1]` ends in a `p_delay` (measured on Gertrude's Cat's `kittens_mew`:
the raw click read `covered` in the same frame that shows "You find a kitten!"), so grade the row on
the DIALOGUE a `chat.play`/`chat.kind` read right after, never on that click result.

`t.drive.click_minimenu` answering `ok` with the correct row text read back is not proof the server
opened anything either (a Black Knights' Fortress `Listen-at` press read its own row and mounted no
page) -- follow it with `t.await(chat.kind() ~= "none")`.

### `click_loc` `ok` on a bare `map_flag` is held and regraded `refused` (2026-09-20)

`click_loc` answering `ok` on a bare `map_flag` -- a route finished, nothing else -- used to be the
worst row in this suite, because the engine says `I can't reach that!` on the tick AFTER that route
runs out and the row had already been graded PASS. `click_loc` and `use_on` now hold such an `ok` --
and ONLY that one, the `map_flag` settle, where nothing else happened -- for one more tick and
regrade it `refused` (2026-09-20), so A ROW THAT WENT PASS -> FAIL AFTER THIS SEAM IS A PRESS THAT
NEVER LANDED, not a new bug: read the detail, which names the tiles the retry tried.

A press that settled on a chat line or a page is never held, because it has already shown an effect
and because a tick spent waiting is a tick of WORLD -- a wandering npc walks off the pixel your next
row presses. What is left of the old advice still holds for a loc placed far from the player: ask
`t.world.loc_near` for its real tile and `goto_tile` there before pressing.

### `talk_to` steps off; a press answered by NOTHING is an aborted script

Two more shapes behind the same verbs: `talk_to` now steps off the target's own tile itself, at
distance 0 only, the way `click_loc`/`use_on` already did (`pointer.lua`'s `_standoff_for_kind`), so
the hand-written `t.player.walk_near(target, 10, 1)` that used to be needed whenever `goto_tile`
landed you ON a stationary npc is no longer needed -- a `goto_tile` IS a teleport and it does put
you inside him, but the press steps off first now, and the row says
`walk_near: stepped off the target tile <x>,<z>`.

A loc press that lands a real menu row and is answered by ABSOLUTELY NOTHING -- no teleport, no
message, not even "Nothing interesting happens." -- is the signature of a ServerScript that ABORTED,
not of a missing trigger or an unclickable loc: `TORIRSSERVER_VERBOSE=1` on the run prints
`handle_oploc`'s own `<- OPLOC%d <id> at x,z lvl slot` line and the VM's `script error:` beside it
in `client.log`, where a trigger that merely was not found prints `no trigger for [...]` or
content's `^dm_default` sentence instead -- the two read identically from a quest file's own row.

### A fight is a wait; overhead `npc_say`; `inv_op`'s blind `ok`

A fight is not a click, it is a wait. `player.attack` starting a fight and the npc still standing is
normal -- content's `[label,player_melee_attack]` re-arms the interaction with `p_opnpc(2)` after
every swing, so ONE click keeps swinging every `attackrate` ticks indefinitely (measured: thirty
consecutive swings from one click). A fight that does not end is almost always the player's STATS,
not the driver: level 3 with a bronze dagger lands one hit in thirty against defence 7 and dies
first.

Arm the character in `setup` -- gear is a prerequisite, not the quest's own work (trap 16) -- and
gate on `npc.await_dead`. An npc's overhead `npc_say` (dig.rs2's "Hey, leave off my flowers!") is a
BUBBLE, not a chat-log line, so `t.msg.await` can never see it; assert on what the redirected action
stopped saying instead. `t.player.inv_op`'s `ok` is just as blind: it says the press was accepted,
never WHICH branch of a multi-branch script ran -- Pirate's Treasure's spade routes through dig.rs2
to `hunt_dig` or, silently, to `pirate_irate_gardener_attack` (`npc_say` + `npc_setmode`, no page,
no chat line), and that cost 2 of 4 runs; grade the row on the branch's own effect (the item, the
varp, the stage).

## A seam row that reads the player's tile off the rows above it

*Origin: section 8 ("Gaps reported by authors").*

A seam row that reads the player's TILE off the rows above it grades those rows, not its own seam:
`seam.press_pixel` was green for weeks and went red the day combat rows were added above it that end
the fight twenty-three tiles from the nearest tree. A row that needs the world in a particular state
puts it there itself, and a row that reads the npc pool right after a cross-plane teleport uses
`npc.await_present`, never a single `npc.nearest`.

## The `goto_tile` bypass covers a puzzle-gated door when the hand-in reads no lever state

*Origin: section 8 ("Gaps reported by authors").*

The doc's `goto_tile` bypass (ladders, stairs, trapdoors, `cog.lua`'s navigation-only side puzzle)
covers a SCRIPTED PUZZLE-GATED door too, whenever the hand-in reads no lever or door state: Ernest
the Chicken's six-lever maze is pure navigation, because `quest_haunted.rs2:451`'s hand-in tests
`inv_total` on `pressure_gauge`/`oil_can`/`rubber_tube` and never a lever varp, so `goto_tile` past
the maze is legal evidence and a hand-derived nine-bit lever solve buys nothing. Prove it the same
way first: read the hand-in's own guard, and say in the row's detail that it reads items only.

## `click_minimenu`'s hunt can fail 100% on one npc in a cluster; `t.drive.op` last resort

*Origin: section 8 ("Gaps reported by authors").*

`click_minimenu`'s pose-and-pixel hunt can fail 100% reproducibly, not intermittently, on one npc in
a cramped cluster (Biohazard's errand boys stand a tile apart -- `gambler2` 3271,3388 and `artist2`
3272,3389, `areas/world/configs/m51_52.spawn:12-13`; two identical failures back to back, not a
one-off miss). A hand-typed `WorldPoint` for an NPC-CLICK target can also be a camera/hittest dead
zone even when the coordinate is copied exactly off that npc's own `*.spawn` row -- in the ledger it
reads exactly like a wrong coordinate, so a second retry at the same tile proves which it is.

When the normal press has failed twice from two tiles, the last resort is
`t.drive.op(target, option)` -- `target` is the `{kind=,id=}` table `by_symbol` returns and `option`
is the op NUMBER (`"examine"` for Examine):
`t.exec("row", t.drive.op, t.player.by_symbol("npc", sym), 1)`. It leaves its own `drive.op bypass:`
note in the row and is never evidence that a player could have pressed it (see section 3), so name
it in the detail and keep the failed presses' rows above it.

## Quest Helper's `WorldPoint` for a SHARED object id (furnaces)

*Origin: section 8 ("Gaps reported by authors").*

Quest Helper's own `WorldPoint` for a SHARED object id is not a tile you can resolve here:
`ObjectID.FAI_FALADOR_FURNACE` at 3227,3256,0 answers `not_found` through
`t.world.loc_near("furnace", r)`, because that id is reused by every town's furnace and trap 20's
rule means no `*.loc` row exists to check a coordinate against. Reuse a symbol another green file
has already located (`betweenarock.lua`/`prince.lua` both smelt at `dwarf_keldagrim_furnace`,
2869,10202,0) instead of hunting more coordinates -- `smelting.rs2`'s `[label,use_furnace]`
dispatches on `last_useitem`, never on which furnace was clicked.

## `t.world.loc_near` reports a loc's raw cache level (bridge decks)

*Origin: section 8 ("Gaps reported by authors").*

`t.world.loc_near` REPORTS A LOC'S RAW CACHE LEVEL, and on a bridge deck the server holds it one
plane lower. Any column whose jm2 level-1 flag carries `LINK_BELOW` is shifted down by
`record_loc_at` (`torirs_server_scene.c`), so the Fishing Platform's crane answers `2770,3287,2`
while the player walks and the script runs on plane 1. Take x/z from `loc_near` but the LEVEL from
`t.world.tile()` after you have walked onto the surface the loc stands on -- feeding the reported
level into `goto_tile` puts you on the wrong plane and the op then silently never runs, which is how
a Sea Slug draft produced six `drive.op -> ok nil, msg.await -> timeout` rows and blamed a content
gate for them.

The PRESS is aimed at the height the scene drew the loc since seam24
(`drive_pointer_screen_position_loc` projects at `World_TerrainWalkLevel` -> `World_HeightAt`), so
`click_loc`/`use_on` press a bridge-deck loc first try and re-press loops around deck locs are no
longer needed (The Tourist Trap's clifftop climbs, 10/10 first-try; conformance
`seam.bridge_deck_loc_press`).

## A `t.drive.op` that produces nothing is not a content refusal

*Origin: section 8 ("Gaps reported by authors").*

A `t.drive.op` THAT PRODUCES NOTHING IS NOT EVIDENCE A CONTENT GUARD REFUSED. Its banner already
says an `ok` from it proves nothing about reachability; the converse holds too -- measured at
distance 7 and 17 from the seaslug crane on walkable deck, both answered `ok` with no chat line, no
stage change and no script run at all. Await the REFUSAL sentence, not only the success sentence, or
a never-ran op and a refusal read the same.

## `coordz(...) < coordz(movecoord(...))` is a SIDE test; `stood on with ::goto` is a FAIL

*Origin: section 8 ("Gaps reported by authors").*

`coordz(coord) < coordz(movecoord(loc_coord, 0, 0, N))` IS A SIDE TEST, not a proximity test --
which side of the loc you approached from, the doors/ladders idiom this tree uses all over
(`mm_bamboo_doors.rs2:45`, `fluffs.rs2:127`). A refusal reading "I need to get closer to use that."
in front of one is the tell that a port used the wrong idiom: proximity is
`distance(coord, loc_coord) > N` (`upass_cages.rs2:18`, `handsand_betty.rs2:182`), and N must be
measured against the loc's FOOTPRINT, because `loc_coord` names the south-west corner -- for a WxL
loc a legitimately adjacent tile is already `max(W, L)` tiles from it. Sea Slug's crane refused
every tile a player could stand on for exactly this.

### Side-gated pairs and the step-off; the reach retry since seam10

A side-gated `oplocu`/`oploc1` PAIR also meets section 2's door-teleport advice head on:
`use_on`/`click_loc` step you off the loc's own tile BEFORE they press
(`walk_near: stepped off the target tile a -> b`), and `b` can be the wrong side of the `coordz`
test -- Ali's door (alidoor) cost 2 of 3 runs; read `b` in the detail and choose the approach tile
so the step-off lands on the side the gate wants. The same verbs' reach retry used to end
`pressed from x,z (the loc's own square, standoff suppressed, stood on with ::goto)` unasked; since
seam10 it does so only under `stand_on_square = true`, and a default call fails `reach_failed`
instead -- the ::goto is the DRIVER teleporting you, and on a crossing it is the crossing skipped --
Roving Elves' `useRopeOnRock` accepted 2512,3476, the rock rope's `forcewalk2` START, the island was
never reached, and the tree rope was pressed from a `::goto` across the river (reverted by sampler
sonnet-b16).

Grade a traversal on the tile its script's LAST `p_teleport`/`~forcemove` names, and treat
`stood on with ::goto` in a detail as a FAIL on any leg the guide names.

## `t.player.use_on` waits for the backpack to paint (seam23); tab and journal races

*Origin: section 8 ("Gaps reported by authors").*

`t.player.use_on` NOW WAITS FOR THE BACKPACK TO PAINT before it arms, and a refused arming is
retried after that wait (seam23; before, a `use_on` right after `player.unequip`, a journal read or
another tab read `refused ... -- armed by this call (tab nil nil)`, `build/quest_gate/s23b_base1`)
-- no `t.ui.tab("inventory")` + `t.ticks(2)` shim is needed any more. Two more asymmetries that cost
whole runs: `use_on`'s `target` is a `{kind,id}` TABLE (`t.player.by_symbol("loc", sym)`) while
`click_loc` takes a symbol STRING, and a one-shot `t.ui.journal_open()` taken right after a dialogue
closes loses the UI-mount race and answers `timeout first_line=nil`, which reads exactly like a
content bug -- three attempts, as `makinghistory.lua` and `rovingelves.lua` both do by hand.

The shape that costs a whole run: `t.ui.tab("equipment")` pressed once for a WORN item's verb (a
Locate/Rub on `wornitems:slotN`) and never put back -- every `use_on` thirty rows later refuses its
arm, and a 36-row cascade of silently no-op chores reads like the back half of the quest is broken
(A Tail of Two Cats, run 1). Put `t.ui.tab("inventory")` right after the worn-verb row, not before
the next `use_on`.

## Ranking multiple copies of one npc: the mechanic's own metric; `npc_walk` only queues

*Origin: section 8 ("Gaps reported by authors").*

RANKING MULTIPLE COPIES OF ONE NPC BY DISTANCE NEEDS THE MECHANIC'S OWN METRIC, not the one that
looks natural. `t.npc.tiles(sym, radius)` hands back every copy nearest-first, but "nearest" there
is not "fewest actions" for any mechanic that moves ONE AXIS PER ACTION -- a push, a prod, a herd.
Chebyshev distance to the target zone's centre (max of the two axis gaps, the obvious pick) chose a
candidate needing nearly DOUBLE the real remaining presses against a Manhattan / clamped-per-axis
metric, because a diagonal gap costs both axes when only one moves per press.

Sum the per-axis gaps, each clamped to the zone's near edge rather than its centre, and rank on
that. And copies that stand a couple of tiles apart at spawn do not stay that way: after 15-30
pushes one copy can be far from the rest, and `npc.tiles`'s radius-40 list can then rank copies a
press cannot cleanly reach (see `press`: the copy nearest the viewport centre wins). And a script's
own zone test on `npc_coord` right after its `npc_walk` reads the OLD tile: `SS_OP_NPC_WALK` only
queues a waypoint (`torirs_server_scripts.c`), so `diseased_sheep.rs2`'s gate-zone jump fires on the
press AFTER the one that lands the sheep in `sheepherder_pen_gate` -- confirm a landing with a
genuine follow-up press, not a longer varbit poll.
