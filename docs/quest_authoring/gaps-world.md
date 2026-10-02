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

### A stage a zone exit writes still reads the old value right after the goto out (sonnet-b41)

Some stages are written by leaving a map square, not by a click: Shilo Village's
`[mapzoneexit,0_45_145]` / `[mapzoneexit,0_45_146]` (`quest_zombiequeen.rs2:1077-1084`) only
`queue(exit_ah_za_rhoon)`, and that queue re-checks `inzone` before it moves `%zombiequeen` from
entered (7) to left (8). The exit and the queue land a few ticks after `goto_tile` returns, and
`t.quest.expect_stage` does not wait, so a stage row straight after the goto reads 7 and FAILs.
Wait for the write: `t.var.await("varp116_zombiequeen", 8, 10)`, or at least `t.ticks(4)` before the
`expect_stage` (the zombiequeen draft needed 4).

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

## A timed lift or a `covered` boss press is not a licence for `t.drive.op` (Haunted Mine, sonnet-b43)

*Origin: reviewer of author batch sonnet-b43 (hauntedmine, sent back).*

The Haunted Mine test was sent back with run, gate and lint green and helper_coverage FULL, because
two guide steps went out only through `t.drive.op`. The first was `goDownLift`. Turning the valve
gives an 80-tick window (`^hmq_lift_race_ticks`, `hauntedmine_dungeon.rs2:364-375`) before the
cheeky ghost shuts it, and the author said the camera hunt for `lift_side_r` took longer. The
second was Dayth's Attack. Treus Dayth rises on the key tile behind crates and track, so the pixel
press answered `covered`. A `t.drive.op` row sends the packet with no pixel and no route, so it is
never a guide step's row (verbs-pointer: `t.drive.op`). Turn the camera or step to a clear tile
first, and use `click_loc` or `t.player.attack`. If that still misses an 80-tick window, the file
ends in `t.blocked("<the seam>")`. The same review also wanted a row asserting the literal 22,000
Strength XP reward (`hauntedmine_dayth.rs2:298`); a scroll shot that shows the amount is not a
row.

Since seam35 both are real presses with no help from the test: `click_loc` follows the 60-step
valve-to-lift walk past its 20-tick settle (the lift answers inside the window), and
`t.player.attack` re-takes Dayth's `covered` first press from a settled camera, while the kill
wait follows him across his `npc_tele` re-slots (verbs-combat: "A covered Attack press, a boss that
teleports, a timed walk"). What is left is the fight's damage: bring Protect from Missiles or more
food and fight off the track rows.

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

## A loc with only Examine: the way back in is the item that opened it

*Origin: sample sonnet-b34 (2026-09-29), Plague City (`elena`).*

A guide step that says "go down the hole" can point at a loc whose only op is Examine. Plague City's
`plaguemudpatch1`/`plaguemudpatch2` have no op1: `quest_elena/scripts/mud_patch.rs2` binds
`[oplocu]` only, so a `click_loc(..., 1)` has nothing to press. The way down again after
`climbMudPile` is the spade used on the patch a second time (`t.player.use_on("spade", patch)`).
Past `quest_elena_mud_patch4` the dig label drops you in the sewer again, and the row is the guide's
`goDownHole`. Before you call a loc unreachable, read its script's trigger list (`grep -n "^\["`).

## Temple of Ikov: the walk back east from the lever stops at x=2644 (seam29)

The lever room is behind a CLOSED castle double door (`castledoubledoorl`, west edge of 2645,9828;
LostCity maps/m41_153.jm2 has its doors on the same tiles) between the lava bridge's west landing
(2647,9828) and the lever (2637,9819). Open it with `click_loc` and walk; a `goto_tile` past it
leaves the walk back east facing it shut ("walk_to times out at x=2644"). The bridge itself tracks
which side you are on with `%ikov_dungeon` bit `^ikov_bridge` (LostCity ikov_dungeon.rs2):
teleporting onto the west side leaves the bit at 0 and the next crossing throws you back WEST.
Cross it for real, wearing the boots, both ways (s29lava_ikovcopy 38/38, `pickUpLever-recross`
'now at 2651,9828').

## A sink or water pump answers "Nothing interesting happens." to a bucket (FIXED seam29)

A loc whose cache category is an unnamed id (not in pack/category.pack) never reaches a
`_<category>` head. Sinks are cache category 175 and water pumps 177 in osrs239, so the Varrock
palace kitchen sink (Demon Slayer's fillBucket) did nothing; 21 of them are now bound by name heads
stacked on `[oplocu,_watersource]` (general_use/scripts/water_sources.rs2; source LostCity
water_sources.loc `category=watersource` on its sinks and the wiki Water source page). Seam30 bound the rest of the placed, uncategorised sinks and fountains
(`_watersource`: Lumbridge castle kitchen, Rimmington, Keldagrim, Lunar Isle, ...) and wells
(`_well`: elf village, Keldagrim, Burgh de Rott, the desert well) by name; the list is in
water_sources.rs2. It also closed two name heads that shadowed them: skill_farming's
`farming_craft.rs2` (a jug, bowl or vial at `sink`/`sink2`/`fountain`/`goldfountain` now fills) and
garden_althric.rs2's `well` (the Edgeville well fills a bucket). Left unbound on purpose, with the
reason in water_sources.rs2: taps (wiki: vials only), troughs, the Trouble Brewing pump, the Blast
Furnace sink, and the quest fountains and wells that have their own ops.

## A multinpc shell whose every visible child is a `*_noop` form cannot be talked to (seam29)

The client builds the menu from the cache, so a shell whose visible rungs all resolve to a form with
no ops is never clickable; server-side `.npc` overrides and `[opnpc1,<shell>]` do not help. The fix
pattern is an owner-private `npc_add` of the op-carrying child, placed by the quest's own
round/scene procs, with the trigger on that child's symbol: Fight Arena's arena Sammy is
`sammy_servil_vis` placed by `~arena_spawn_sammy` for stages 6..11 and removed at freed_servils
(LostCity jeremy_servil_arena op1=Talk-to). Drive it with `talk_to('sammy_servil_vis')`.


## Paterdomus: Drezel's offer, the holy barrier and the east trapdoor (Nature Spirit)

`::complete quest_priestinperil` sets the varp and grants no items. Before Drezel makes the Nature
Spirit offer, the backpack must hold `dagger_wolfbane` (Priest in Peril's reward, `::give` it in
setup) and the player must ask to cross the holy barrier ("Drezel, might I be permitted to cross the
holy barrier").

The way OUT of Drezel's room is that barrier. `[oploc1,pip_underground_wall_side_withportal]`
(`quest_priestperil/scripts/mausoleum_interactions.rs2:23`) `p_telejump`s to 3423,3485 once
`%priestperil >= ^priestperil_access_holy_barrier`. Quest Helper names it as `leaveDrezel`, a
sub-step of `enterSwamp`. Click it rather than using `goto_tile` from the room to the swamp gate.

The way back IN (`goBackDownToDrezel`, `pipeastsidetrapdoor` at 3422,3485) is a CONTENT SEAM as of
2026-09-29. `quest_sinsofthefather/scripts/sinsofthefather.rs2:507` owns
`[oploc1,pipeastsidetrapdoor]` and answers "Lab stairs and trapdoors sit locked." to anyone outside
the Myreque arc. Report it; do not `goto_tile` past it.

## Death Plateau: Harold's door and Tenzing's fenced house

Harold's room is upstairs in the Burthorpe inn. His door (`harold_door`, 2906,3543,1) cannot be
reached from the stair corridor, so `goto_tile` to a tile outside the door first. The roof hides the
door from the default camera; turn the camera (`t.drive.camera`) before the click. Tenzing's house
is fenced; approach `sherpa_door` (2822,3555, east wall) from 2823,3555.

## Crandor: the hole from one side only, and a stalagtite wall that answers "Nothing interesting happens" (wall FIXED seam31)

*Origin: the Dragon Slayer author (sonnet-b36); the sampler sent dragon back over the second item.*

- The Crandor hole's maplink (`dragon_slayer_qip_ruin_entrance`) works only when you click it from
  the west or south side. Put the `goto_tile` on one of those sides.
- `dragon_slayer_qip_stalagtite_jump` (the guide's `enterElvargArea`, "Climb-over Wall") has no
  handler. The click answers "Nothing interesting happens." and the lair can be walked into from the
  north. A row that passes on `map_flag` there reports a climb that never happened. The sampler
  reverted `dragon` over this, together with the one-hit Elvarg (`gaps-combat.md`). Report the
  missing handler as a content seam, and write the row as a GUIDE-GAP that cites the loc.
- FIXED seam31: `crandor.rs2 [oploc1,dragon_slayer_qip_stalagtite_jump]` is the two-tile climb
  (2009scape DragonSlayerPlugin.java:134-162; no Agility requirement in any source). Stand on the
  WEST side (`goto_tile(2845, 9636, 0)`), `click_loc("dragon_slayer_qip_stalagtite_jump", 1)`, and
  assert the landing x == 2847. In only while `%dragon_sailed`; out always; "You have already slain
  the dragon." after completion. The completion teleport now lands on 2845,9636 (west of the wall),
  not on the wall tile.

## Dwarf Cannon: the tower's two ladders have different names

*Origin: the Dwarf Cannon author (sonnet-b36).*

The ground-floor ladder of the guard tower is the generic `ladder` at 2570,3441. Press it with
`click_loc("ladder", 1, { at = { 2570, 3441 } })`. The level-1 ladder is `mcannonladder`, and both
top ends are `laddertop`.

## A fence squeeze pulls you back after the next goto; "I can't reach that!" after a squeeze (seam31)

*Origin: seam31 substep_grader_with_three_real_clicks (biohazard exitBackyardOfHeadquarters).*

An `~agility_exactmove` squeeze (`general_use/scripts/fence.rs2:5-32`, `mournerstewfence`) ends
with `p_delay(2)` + `p_teleport($end)`. The client tile already reads the far side while the
exactmove is in flight, so a `goto_tile` issued then lands first and is undone when the teleport
fires: the next click answers "I can't reach that!" from beside the fence. After the click, wait
about 5 ticks (`t.ticks(5)`) and assert the rest tile (biohazard
`exitBackyardOfHeadquarters.crossed`: 2541,3331). The same fence runs both ways (`~check_axis`
swaps start and end), so the yard is left through the fence it was entered by.

## The Mourner HQ basement is an instance region, not `z + 6400` (seam31)

`mourning_hideout_trap_door` (`mend1_disguise.rs2:136-143`) teleports to `mend1_hq_basement_coord`,
2044,4628,0. That is not the underground frame, so a `z > 6400` test never sees the landing; check
the distance to 2044,4628 instead (mourningsendparti `enterMournerBasementAfterPoison.landed`).

## Zanaris has no Door man, market door or exit ladder in OSRS (seam32)

The OSRS wiki Door man page is `{{Gone}}`: "removed 27 February 2006 ... never been present in Old
School RuneScape". Gatekeepers (id 5840, near 2468,4436) replaced him: the market costs one cut
diamond, and its exit is a one-way fairy ring behind the Al Kharid bank. The 239 map m38_69 has no
`zanarisladderout`, `zanarisladderout2` or `zanarismarketdoor` placement, so LostCity's
`doorman.rs2` / `ladder_fairy.rs2` must NOT be placed. Lost City's guide ends at `enterZanaris`
(Quest Helper LostCity.java:174), so none of this blocks the zanaris row. The OSRS-era market is a
separate content job (Gatekeeper, mushroom gate, exit fairy ring).

## Leaving the Entrana dungeon: `cast lumbridge_teleport ... the cast never ran` (CLOSED, seam33)

*Origin: sampler sonnet-b40 (zanaris review). Closed by seam33 spellbook_cast_never_runs.*

Lost City's `teleportAway` is a spell cast from the Entrana dungeon floor. In batch sonnet-b40,
`t.player.cast("lumbridge_teleport")` at 2861,9736,0 answered FAIL: `no line, no move and no Magic
XP inside 10 ticks: the cast never ran`. None of the three suspects was the cause: the button
reached the server, `p_finduid` never ran, and the dbrow is fine. The server REFUSED the press:
the row came on the same tick as `cutDramenBranch`, and `[oploc1,dramentree]`
(`quest_zanaris/scripts/leprechaun_tree.rs2`) ends in `p_delay(1)`. An interface click that
would start a script while the player is delayed is dropped, with nothing on screen. This is
LostCity's rule: `IfButtonHandler` runs the `[if_button]` with protected access, and
`Player.runScript` returns -1 while `delayed`. The engine copy is `if_button_refused_while_delayed`
in `torirs_server_world.c`, and its verbose line is
`<- IF_BUTTONN 218:29 refused: player is delayed (p_delay)`
(`build/quest_gate/s33cast_repro2`, row 4). The same cast from a quiet dungeon tile lands in
Lumbridge (`s33cast_repro`, row 2). The Entrana dungeon does allow teleporting out, as it does in
the OSRS game, and nothing in content blocks it.

The fix is in the driver. A self-cast (`spell.lua` `_cast_self`) re-presses the spell's cell when
a press leaves no trace (no line, no move, no Magic XP) for `SELF_CAST_REPRESS_TICKS` (3). It
presses up to `SELF_CAST_PRESSES` (3) times, as a person clicks again, which is the same rule the
equip verb uses. The detail says `(press N: ...)` when it took more than one press
(`s33cast_after2` row 4: `TELEPORTED to 3220,3219,0 ... (press 2: ...)`). Conformance row
`seam.cast_self_teleport_from_dungeon_after_delay` covers the chop and the cast.

What a test should do: nothing special. Cast right after the step, and do not add a
`t.ticks` to dodge the delay. A cast on a HELD item (`{kind="held"}`, OPHELDT) is refused the
same way while the player is delayed, and it still presses only once. If that case reads
`the cast never ran` right after a `p_delay` step, it is the same seam.

The dungeon's other way out is no substitute. `[oploc1,zanarismagicdoor]`
(`areas/entrana/scripts/entrana_dungeon.rs2:15`) `p_telejump`s to `0_50_58_50_60` (3250,3772), in
the deep Wilderness, so the guide does not use it.


## A step that waits real minutes: `t.clock.skip` (seam33)

*Origin: author batch sonnet-b40 (forgettabletale leg 2, `waitForKelda`). Fixed by seam33
test_clock_for_realtime_waits.*

Some waits are measured in REAL minutes, not ticks: content stores a deadline in `date_minutes`
(wall-clock minutes since 1970) and a catch-up proc compares against it. Forgettable Tale's kelda
patch is four stages of `^forget_kelda_stage_minutes` (4), sixteen minutes from planting
(`forget_farming.rs2 [proc,forget_kelda_catchup]`); its brew, every farming patch, the Home
Teleport cooldown (`home_teleport.rs2`) and the fight cave rotation read the same clock. A run's
frame budget ends at about 12.5 wall minutes (run.py `MAX_FRAMES_CEILING`), so the symptom was an
await that sat at the planted stage until the run ran out: `var.await forget_farming == 8 (last
client read: 4)`.

A real-time wait is a grind, and the fast-forward is `t.clock.skip(minutes)`
(`quest_driver/world.lua`). It sends `::clockskip <minutes>`, which moves the embedded world's
wall clock forward (docs/QUEST_SERVER_CHEATS.md, "Grind fast-forwards"), and reads the new minute
back through varp `date_minutes`. It moves only the clock. The quest's own catch-up still does the
work (its softtimer, or the next op that calls it), so the next row reads the QUEST's effect:

```lua
t.exec("waitForKelda-skip", t.clock.skip, 16)
t.exec("waitForKelda", t.var.await, "forget_farming", 8, 110)   -- forget_tick fires every 100 ticks
```

Evidence: `build/quest_gate/s33clock_old` (the old binary) answers `::clockskip 16 -> no_row:
Unknown command` and the await times out at 4. `build/quest_gate/s33clock_new` answers `date_minutes
29846673 -> 29846689 (+16 skipped, world 16 min ahead; client varp)`, then `forget_farming = 8
(client varbit) after 88 tick(s)`, and the real harvest gives `kelda_hops 0 -> 1`.

- The catch-up is NOT instant. It runs when the content calls it: the softtimer here (up to 100
  ticks), or a patch op. Await the stage; do not read it on the next row.
- Skip the whole wait, not more. The clock only moves forward (`0`, a negative number or more
  than a week in one call answers `refused` with the server's line), and every later deadline is
  measured from the skipped clock.
- Ordinary farming re-arms from NOW (`farming_hops.rs2 [proc,farming_advance_hops]`), so one skip
  advances a crop ONE stage. Skip one stage's minutes, await the stage, and repeat. A quest that
  measures from the deadline (the kelda patch) catches up every stage at once.
- The skip lives as long as the server process. `t.session.relog` re-boots the embedded server,
  and the clock is real again. Skip AFTER the relog, never before it.
- Prefer it to a quest's own "set the result" debugproc (`::forget_growkelda`,
  `::forget_ferment`). Those write the outcome; the skip lets the quest compute it.

## A loc that changes symbol each use: Shilo Village's bone door (`thzq_tombrooml1/2/3`)

*Origin: author batch sonnet-b42 (zombiequeen `useBonesOnDoor1..3`).*

Each bones use on Rashiliyia's tomb door `loc_change`s it to the next symbol for 50 ticks
(`quest_zombiequeen.rs2:1419-1434`: `thzq_tombrooml1` -> `thzq_tombrooml2` -> `thzq_tombrooml3`).
A second `use_on` aimed at `by_symbol("loc", "thzq_tombrooml1")` finds nothing. Name the NEXT
symbol on each use, with `{ at = { 2892, 9480 } }` because two `thzq_tombrooml1` copies exist
(seam-facts: Seam pass 32 (e)). The way back out is op1 on whichever of the three is there now:
try each with `t.world.loc_near(sym, 8)` first (`zombiequeen.lua:388-432`).

## Underground Pass: `walk_to` stalls under attack and stops at rock bridges (sonnet-b42; bridges seam34)

*Origin: author batch sonnet-b42 (upass legs 3-4, rejected; relay notes in
`build/author_state/sonnet-b42/upass.relay.md`).*

- `walk_to` answers `timeout` while blessed spiders and ogres hit you (2397-2402,9680-9684 on the
  way to orb 2, `m37_151.spawn`). A 10-hitpoint account dies in the hops. Put the combat levels in
  `setup` (hitpoints 40 and defence 30 survived), hop-walk in short legs, and retry a stalled hop;
  `::setlevel` inside a leg is too late for an earlier leg's resume.
- `walk_to` stops before the narrow rock bridges of the maze after the ledge. They ARE walls in the
  game until crossed (`blockwalk=1`, `op1=Cross`; LostCity blocks them too), so this is not a driver
  bug: a stalled walk now names them (verbs-pointer: "A walk stops at an obstacle", seam34), and a
  straight `walk_to` across the maze never arrives. Walk to x-1 of each bridge,
  `click_loc` the `walkway_upass_narrow_mid_top` copy at 2380,9634 / 2387,9631 / 2392,9627 /
  2399,9632 / 2406,9637, then walk on to the pipe (`upass_pipe6`, 2417,9605).
- The hops between the bridges (proved in seam pass matthew-mbp-m4-b48-seam2, seam-facts (a)):
  ledge landing 2374,9638 -> 2373,9634 -> 2379,9634, bridge 2380,9634; 2384,9634 -> 2384,9631 ->
  2386,9631, bridge 2387,9631; 2389,9631 -> 2389,9627 -> 2391,9627, bridge 2392,9627; 2395,9627 ->
  2395,9632 -> 2398,9632, bridge 2399,9632; 2403,9632 -> 2403,9637 -> 2405,9637, bridge 2406,9637;
  then 2421,9637 -> 2422,9634 -> 2422,9610 -> 2421,9606 -> 2419,9605, the pipe's east mouth. Click
  each bridge with `{ at = { bx, bz } }` and `t.ticks(10)`; success is standing on bx+1. A `covered`
  click happens (2392 once in a run): retry the hop and the click.
- Where the pipe drops you depends on `%varp161_upass` (`upass_obstacles.rs2:410-414`). Before
  `^upass_killed_unicorn` the crawl lands one tile on, in the live-unicorn room (x 2393-2417); from
  that stage on (Regicide's walks, Underground Pass complete) it lands 26 tiles further west, at
  2387,9605 in the dead-unicorn copy (x 2368-2392), where skeletons attack. From there walk
  2378,9605 -> 2378,9607 -> 2375,9607 -> 2375,9610 and click `upass_unicorn_doorl`
  `{ at = { 2375, 9611 } }` (angle south): it teleports you to 2371,9666
  (`upass_unicorn_tunnels.rs2:27-29`).
- A `goto` to a cell-door tile can land INSIDE the cell. `goto_tile 2393,9657` puts you in the north
  cell row (z 9657-9660), closed by `cave_railings2` on z 9656. The guide's `pickCellLock` tile is
  the corridor 2393,9655, reached on foot from the well landing (`walk_to` 2410,9656, then
  2393,9655). Check the guide's ObjectStep tile against the railing walls before a goto.

## Underground Pass: `walk_to` cannot reach the witch's cat, house or demons (collapsed bridges, sonnet-b43)

*Origin: author batch sonnet-b43 (upass leg 7, gave up; notes in
`build/author_state/sonnet-b43/upass.leg7.progress.md`).*

- On Iban's level (`m33_71`, level 1) the paths between the witch's house, her cat and the three
  demons are cut by COLLAPSED BRIDGES, which are different locs from the maze's narrow rock bridges
  above. A straight `walk_to` never arrives there. The witch's door (`cavewitch_door` 2158,4566) is
  not path-reachable from the east cliff (2172,4561) either, because the house sits in a pocket of
  its own.
- `bridgecollapsed1` and `bridgecollapsed2` are crossed with op1 (`upass_obstacles.rs2:425-426`,
  `@upass_cross_bridge`). The crossing rolls `stat_random(agility, 160, 300)`. A failed roll drops
  you into the darkness below (`0_36_153_31_29` or `0_36_154_29_10`), hits for 25% of hitpoints
  plus 4, and spawns Koftik the first time. Set agility in `setup` (leg 7 used 70), and read
  `t.world.tile()` after every crossing.
- The crossings the leg used are 2156,4582 (c2), 2147,4583 (c2), 2142,4562 (c1, approached from the
  north) and 2126,4566 (c2). A static BFS over `maps/m33_71` (jm2 and jl2) found that route faster
  than probing it with walks.

## Underground Pass: the demons and Iban's temple were never there (`holthion=no_row`; FIXED seam36)

*Origin: author batch sonnet-b45 (upass parked at leg 7) and seam pass 36.*

- The three demons (`holthion`, `doomion`, `othainian`) and the temple actors (`iban` and the
  Disciples, `ibanmonk`) are spawned by `[mapzone,0_33_71]` and `[mapzone,0_33_72]`
  (upass_encounters.rs2), once `%upass` is at least `^upass_entered_main_area`. Before seam36 both
  were spelled `[mapzone,1_...]`, which never fires (seam-facts, Seam pass 36 (a)). Use the plain
  symbols, not the guide's `*_vis`.
- Holthion stands at 2132,4554 and Doomion at 2134,4565, both on level 1. Othainian's platform
  (2122,4563) is across `bridgecollapsed2` at 2126,4566. Iban stands at 2133,4647 on the temple
  floor.
- Leg 7's rows were proved on a copy of the parked file: 167 PASS / 0 FAIL, through
  `killHolthion`, `killDoomion`, `crossToOthainian`, `killOthainian` and
  `searchDoomionsChest-shadow`. The copy is `build/seam_state/seam36/upass_leg7.lua`, and the
  hand-off is in `build/author_state/sonnet-b45/upass.relay.md`. It stops before guide step 7.66
  (`returnToDwarfs`).
- Food is tight. Every lobster was gone by Othainian, and the hp orb read 12/99. Carry more.
- The rope swing (`crossThePit`) rolls off the player's own random stream, which is seeded by the
  account name (seam-facts, Seam pass 33 (g)). So a `--script` copy of the relay reproduces only
  under `--name upass`.

## Underground Pass: the finale -- locked after Iban's bolt, no Koftik in the pocket (FIXED seam37)

*Origin: author batch sonnet-b46 (upass parked at leg 8, `leg.8.player-locked`) and seam pass 37.*

- A bolt that hit while the doll throw at the altar was running left the player locked for good:
  the npc timer's `player_lock` + `p_delay(1)` + `player_unlock` was dropped mid-delay
  (seam-facts, Seam pass 37 (d)). The hit now lands in one tick: damage, a throw back to the temple
  entrance (2143,4648), and the stun. A probe ate a lobster and walked right after a hit.
- The doll throw lands the player at 2482,9607 in a closed pocket. Koftik (`caveguide6`) stands
  behind the cave wall at 2443,9607, so Talk-to on him answers "I can't reach that!". The way out
  is the Cave beside him (`upass_last_out` at 2438,9607, op 1 Enter), which plays his whereami
  dialogue and leads you out to 2481,9717 (seam-facts, Seam pass 37 (e)). Drive
  `talkToKoftikAfterTemple` by clicking the Cave, and declare that with the `upass_tablets.rs2`
  evidence if `helper_coverage` asks.
- Proof: the parked file with its leg-8 tail replaced, run as `--name upass`, read 222 PASS / 2
  FAIL through `quest.varp_complete` (`build/seam_state/seam37/upass_run2/`). The 2 FAILs were the
  test's bind `display`: the scroll reads "Underground Pass", not "Underground Pass quest". The
  hand-off with every row's verb is in `build/author_state/sonnet-b46/upass.relay.md`.

## Underground Pass: Iban's temple door after the quest -- "The temple is in ruins..." (Regicide; FIXED b48-seam1)

*Origin: sampler matthew-mbp-m4-b48 (regicide's `enterTemple`) and seam pass
matthew-mbp-m4-b48-seam1.*

- At Regicide stage 2 (`^regicide_spoken_lathas`) or later, Iban's temple doors open for you with
  no robes and no "The temple is in ruins..." refusal, even with Underground Pass complete, and put
  you in the ruined temple beside the Well of Voyage (seam-facts, Seam pass
  matthew-mbp-m4-b48-seam1 (a)). Enter from the EAST:
  `t.player.click_loc("upass_templedoor_closed_right", 1, { at = { 2143, 4648 } })` lands on
  2014,4712 level 1; `regicide_voyage_temple_well1` is at 2008,4711. Both Regicide walks use it.
- The doors are 77 tiles from Iban's door landing (2173,4725 level 1), outside the scene, so a
  `click_loc` from there answers `not_found`. Walk the guide's `enterTemple` line points
  (Regicide.java:530-551). They cross FOUR collapsed bridges (`upass_obstacles.rs2:425`), each a
  `click_loc` with an agility roll (a fall drops you to level 0 of the pass): walk 2172,4723 ->
  2172,4686; `bridgecollapsed2` at 2164,4686; walk 2161,4686 -> 2161,4699 -> 2157,4699 ->
  2154,4697; `bridgecollapsed1` at 2154,4690; walk 2154,4686 -> 2152,4685 -> 2153,4682 ->
  2153,4678 -> 2154,4676 -> 2160,4676 -> 2160,4670 -> 2165,4670 -> 2165,4667 -> 2162,4667;
  `bridgecollapsed1` at 2162,4663; walk 2161,4659; `bridgecollapsed2` at 2161,4654; walk
  2147,4648; then the door. A plain `walk_to` across a gap stalls (x 2167 on z 4686). Agility 56
  crossed all eight bridges of both walks in one run; budget a retry for a fall.
- Proof: a copy of the reverted round-2 Regicide file (9b7c0756c) with both `goto 2010,4709,1`
  rows replaced by this route and the door, 76/76
  (`build/seam_state/matthew-mbp-m4-b48-seam1/scratch/regicide_r2_door.lua`); the hand-off is
  `test/quests/wip/regicide/relay.md`, "seam1 (temple door)".

## Underground Pass: `goBackUpToIbansCavern` reads CONTENT_GAP -- the dwarf cavern is where a bridge fall lands

*Origin: sampler matthew-mbp-m4-b48, third check of regicide (green 5deefa070, reverted).*

- The guide's `goBackUpToIbansCavern` (Regicide.java:528, the `isInDwarfCavern` branch at :742;
  Underground Pass has the same step) is the way back after a FALL from one of Iban's collapsed
  bridges. On a failed `stat_random(agility, 160, 300)`, `upass_obstacles.rs2:431-436` sends you to
  2335,9821 or 2333,9866 on level 0. Both tiles are inside the guide's `inDwarfCavern` zone
  (2304,9789 - 2365,9921, Regicide.java:344). The way up is `cavewalltunnel_upass_up` at 2336,9793
  (`upass_tunnels.rs2:21`), which lands at 2150,4546 on level 1. In Underground Pass's ladder, the
  next step after it starts at the bridges' south-east corner (`upass.ladder.tsv:61-62`).
- The cavern is NOT reached only through the down tunnel (`upass_tunnels.rs2:9`). A `-- GUIDE-GAP:`
  that says so is false, and helper_coverage's CONTENT_GAP on it proves nothing.
- Never raise a stat mid-run to close a branch. The reverted file ran
  `t.cheat("::setlevel agility 99")` before the bridges. At 99, `stat_random(agility, 160, 300)`
  cannot fail, so the branch never opens. Setup cheats stage prerequisites only (QUEST_AUTHORING,
  the contract). Keep agility at the guide's 56, where each crossing fails about 7% of the time.
  When a roll sends you down, click the tunnel up and walk back to the bridges. A GUIDE-GAP is honest
  only when it cites `upass_obstacles.rs2:431-436` and says the run's rolls never failed.
- The tunnel up has two copies, and they land in two pockets (`upass_tunnels.rs2:21-25`). The copy
  at 2336,9793 lands at 2150,4546; the other copy lands at 2113,4729. Regicide's green run
  (d06b64289, ledger rows 90-93) fell off bridge B to 2333,9866, clicked the nearest tunnel and came
  up at 2113,4729. Read the landing tile and pick the walk back to the bridges' approach (2172,4686):
  east along z 4730 from the west pocket, or north up x 2173 from the south one (`regicide.lua`
  leg 2, `west_hops` / `south_hops`).
- You never walk to the tunnel, because the fall puts you beside it. A wip leg JSON or relay note
  that calls the step a GUIDE-GAP because "the dwarf cavern tunnel is not walkable from the pass" is
  stale. The b48 reviewer copied that claim from Regicide's wip leg 6 JSON (removed once the quest
  went green, in git at 1735662fd); the green file drives the step.

## Underground Pass: the mud pile (`upass_mud`) has no walkable approach tile (sonnet-b44)

*Origin: author batch sonnet-b44 (regicide's Underground Pass section).*

The mud fills its own approach tiles, so the walk to it ends two tiles short (2395,9651) and the
spade use never lands (`[oplocu,upass_mud]`, `upass_unicorn_tunnels.rs2:9`, `p_arrivedelay`).
`regicide.lua` digs it with `t.player.use_on("spade", mud, { stand_on_square = true })`, and the
player comes out at 2392,9646. That opt-in needs its `-- GUIDE-GAP:` marker (traps-23-33:
`stand_on_square` needs).

## `walk_to` never arrives on an upper level: plan the route from jm2/jl2 (Underground Pass level 1, sonnet-b46)

*Origin: author batch sonnet-b46 (upass leg 8: the cage and Iban's temple).*

Underground Pass's level-1 cavern (`maps/m33_71`, `m33_72`) is cut into pockets joined by rock
bridges, collapsed bridges and doors, so a straight `walk_to` toward the cage or the temple stalls
and its detail does not say which pocket you are in. The leg-8 author found the route with a static
BFS over the region's map files, faster than probing it with walks. relay.md has no tool for this;
write the script in your scratchpad:

- Region `mX_Z` covers x `X*64`..`X*64+63` and z `Z*64`..`Z*64+63`; the `lx lz` in its files are
  offsets from that corner.
- Each `maps/mX_Z.jm2` line is `level lx lz: h.. [o..] [f<flags>] [u..]`. A flag value with bit 1
  set (`f1`, `f5`) is a blocked tile (traps-23-33: naming a blocker from jm2/jl2).
- Each `maps/mX_Z.jl2` line is `level lx lz: <loc id> <shape> [rot]`. Resolve the id through
  `configs/all.loc.compack`; a loc with `blockwalk` blocks its tile, and a wall blocks one edge
  (traps-23-33: Decoding a wall's `rot`).
- A column whose jm2 level-1 flag carries `LINK_BELOW` is held one plane lower (`t.world.loc_near`
  reports a loc's raw cache level, above). Read the plane you stand on from `t.world.tile()`.
- Search 4-neighbour from your tile to the target's op tile. Treat each bridge or obstacle loc as
  a stop: press it, then read `t.world.tile()`, because a crossing can fail
  (`bridgecollapsed1/2` above). Give `walk_to` the path as hops of 10-15 tiles.

## Monkey Madness: a greegree wearer is drawn as the monkey (seam34)

*Origin: seam34 greegree_transmog_render (mm parity3f legs_left: "client rendering of player
transmog").*

- Since seam34 the client draws a transmogged player as the npc (`p_transmogrify`, a greegree held):
  the appearance block's 0xffff entry was decoded and then dropped, so the wearer stayed a human in a
  monkey's stance. Proof read is the pick-set silhouette (`t.drive._projection` of `{kind="player",
  id=-1}`, then the highest pixel that still holds the player): a human is about 84 px tall at camera
  (0,400,350), a Karamjan monkey 18-24 px. `::transmog <npc>` / `::transmog off` is the ladder twin
  for a scratch probe (`docs/QUEST_SERVER_CHEATS.md`).
- The Ape Atoll ravine archers knock a HUMAN out at 1/20 per arrow: hold the greegree before any
  probe there. The Ardougne zoo monkey pen (2604,3277) is inside `~mm_greegree_zone` and quiet; the
  reusable greegree drivers are `build/parity_state/parity3f/mm_scratch/transmog.lua` and
  `transmog_apeatoll.lua` (Hold -> monkey drawn -> unequip -> human).
- Not built yet (no quest needs it): a transmog into an npc bigger than one tile is not re-centred
  (the reference's `transformedSize`), and a transmogged player's chathead is still the player's.

## Penguin Agility Course (Cold War stage 100): the water leg cannot be walked (vm-b1-seam1)

Symptom: `click_loc peng_agility_steps01` / `peng_agility_stepstone01` answer `other_floor` from the
course start 2636,4054,1; from the water (`goto_tile` 2634,4054,0) a `walk_to` one tile times out in
place, and the first stepping stone refuses "I can't reach that!" even from the adjacent 2630,4056,0.
Three causes, none in the driver:
- ENGINE: `torirs_server_scene.c` `terrain_is_ocean()` counts overlay 537 (the course's wading
  water, 2628-2635 x 4053-4065, level 0) as ocean, and `ocean_blocks_walk` blocks every tile of it.
  Open: the wiki puts the crushers and the first stone in that water, so it must be walkable.
- CONTENT: nothing takes the player from the guide's start tile 2636,4054,1 (Quest Helper
  `agilityEnterWater`) down into the level-0 water (zone `inAgilityWater`); no source names the
  mechanism. Open.
- CONTENT: the Crusher npcs (`peng_agility_crushcourse_crushblock01..04_npc`, 856-859) have no op in
  `all.npc`, so `[opnpc1,crushblock*]` never fires and the shared penguin lap tracker never starts
  (`lap course=17 step=0` throughout); the wiki passes a crusher by timing a walk. Open.

FIXED in the same pass: stone 7 (`peng_jump_stone_clickzone_07`, 2635,4065,1) is an `[aploc1]`
jumped from stone 6 two tiles away (it answered "I can't reach that!": stone 6's serverside wall
and the level-0 `peng_coast_6` leave no op-adjacent walked tile); the ice ends with the wiki's slide
to the finish line 2657,4039,1; the fence gate `peng_agility_fencing_door` (west edge of 2652,4039)
crosses either way -- finish (east) -> start 2651,4039 is the obstacle with 65 XP, start -> finish
2652,4039 a plain crossing. The Agility Instructor refuses state 100 -> 105 until the course is run
(seam-facts, Seam pass vm-b1-seam1 (c)); until the water leg lands, a test crosses it with
`goto_tile` onto the first stone (2630,4057,1) and says so, or stops at `t.blocked`.

## A player-owned house is bare grass; `loc_near` finds no hotspot (FIXED vm-b1-seam1)

Symptom: inside a POH (`tile 64xx,..`) the instance is empty grass, `loc_near poh_workshop_2` or a
furniture loc is `not_found`, and the save's `[poh_rooms]` holds 84xx ids. Cause and fix: seam-facts,
Seam pass vm-b1-seam1 (b). Cold War's clockwork suit: `::coldwarpoh` (setup) stages the house the
guide requires (Rimmington, a Workshop, a Crafting table 3) at the Rimmington portal 2953,3224,0;
`enterPoh` is the portal's op 2 `Home` (op 1 Enter opens a four-option menu); then
`click_loc("poh_clockmaking_3", 1)` + `choose:Clockwork` makes the mechanism and a second click +
`choose:Clockwork toys` + `choose:Clockwork penguin` makes `peng_suit_unwound`.

## A cave or tunnel click answers a chat line and the tile is unchanged: a name binding shadows the maplink (FIXED matthew-mbp-m4-b49-seam1)

Symptom: `click_loc` on a cave mouth, crevice or tunnel answers `chat_message` (Troll Romance:
`A snowy cave.`) and `t.world.tile()` is where you stood. Every maplink loc is bound by category,
`[oploc1,_maplink_transition]` (`ladders_stairs/scripts/maplink.rs2:77`), and a quest that binds the
same loc BY NAME shadows that for every player. Curse of Arrav bound
`[oploc1,trollromance_caveentrance]` and `[oploc1,trollromance_snow_cavewall_crevis]` for its own
soft-skip and printed `A snowy cave.` otherwise, so Troll Romance could not enter or leave the
Trollweiss cave, the sled was never worn (`Ride` answered `You cannot use that here!` from the cave
tile 2772,10232) and both sled `.cutscene` rows failed. FIXED in `curseofarrav.rs2`: the fallthrough
is `~maplink_transition;`, so the cave lands at 2803,10187 and the crevice at 2778,3869 (conformance
row `seam.trollweiss_cave_maplink_not_shadowed`). Before blaming `maplink.dbrow`, grep the loc symbol
across `server/scripts/` for a name-specific `[oploc<N>,<loc>]`.

## Underground Pass: the fall pocket is left over five rockslides and a rock pile (matthew-mbp-m4-b49-seam1)

Symptom: after the swamp (`upass_swampbubbles1`) or a failed rope swing you stand at 2485,9649, and
`walk_to` toward the exit never moves -- it reads as "sealed by collision". It is not: the guide's
`leaveFallArea` line crosses five `rockslide2_obstacle_upass` (op 1 Climb-over,
`upass_obstacles.rs2:29`) at 2479,9629 / 2467,9646 / 2456,9633 / 2455,9647 / 2448,9650, each clicked
from its east side, then `caverockpile` 2443,9651 (op 1 Climb, `:77`) surfaces you at 2482,9715.
LostCity's m38_150 places the same slides and pile. A slip ("...but you slip back down.") costs
3 hp; at Agility 1 about ten slips took 40 hp to 6, so carry food. `upass_swampbubbles1` 2465,9713
answers `I can't reach that!` from 2482,9715; click it from Koftik's ledge 2453,9716. The full route
with hops: `test/quests/wip/upass/relay.md` and `docs/quests/ladders/upass.notes.md`. The same lesson
as the maze bridges: list the locs with an op in a pocket before calling it a map bug.
