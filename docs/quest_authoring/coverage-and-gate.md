# Definition of done: `gate.py` and `helper_coverage.py` (section 7)

## Section 7. Definition of done

From `tools/quest_gate/gate.py`, current as of this writing -- re-read that file if this drifts. A
quest is green when:

### Every row PASS

Every `ledger.tsv` row is `PASS` (a `BLOCKED` row is its own bucket, not a pass and not a fail --
see below).

### The SUMMARY row

The ledger has a trailing `SUMMARY` row whose counted `pass=`/`fail=` agree with the rows, whose
verdict is `PASS` iff `fail==0`, and whose `blocked=` token (present only when count>0) agrees too.

### Shots on disk, no duplicate MD5

Every row's claimed shot exists on disk (>=1000 bytes); no two shots in `shots/` share an MD5.

### Fingerprints (Character-Creator / pre-login / title screen)

A run stuck before login cannot pass by answering `refused`/`timeout` quietly: a shot that matches
one of three fingerprints is RED (`shot '<name>' matches the <fingerprint> fingerprint -- this run
never actually got past boot/login`).

- `character_creator`: the shot's own top-left 64x64 corner (`tools/quest_gate/fingerprints/`).
- `pre_login` (the loading screen) and `title_screen` (Welcome / Existing User / connecting): BOTH
  the top-left and the bottom-left 64x64 probes of the 765x503 client canvas (centred in the shot)
  must match -- the loading screen is black at both, the title screen's edge art is held as
  `TITLE_SCREEN_CELLS` in `gate.py`.

Since seam35 a cutscene fade, a void corner at a low camera or a dark cave no longer trips
`pre_login`: an in-game frame's bottom-left is the chat-tab stone, never black (mm's
`163-clickPuzzle-cutscene` and Miss Cheevers' shelves used to match the corner-only test; no camera
turn is needed any more). A row driven while logged out (a refused relog, then six "PASS" rows on
the title screen) is now caught by `title_screen`. A PASS `t.session.logout` row's own shot is
exempt, because photographing the title screen IS that step. `python3 tools/quest_gate/gate.py
--probe <png>...` prints a shot's match, its probe distances and its cell means (seam-facts: Seam
pass 35 (b)).

### A reward row for every reward (reviewer's rule)

(the reviewer's rule, not `gate.py`'s) A reward row for every reward Quest Helper lists:
`skill.snapshot()` before the hand-in, `skill.expect_gain`/a coins delta after
`quest.expect_complete()` -- the scaffold emits both; a quest with a reward and no reward row is
rejected.

### Minimum shape

**Minimum shape**, gated on your file using the verb each rule is about (a `t.step`/`t.expect`-only
file is exempt from the `quest.bind`/`t.exec` rules below, never from the row/shot counts). The
row/PNG floor depends on how the ledger ENDS, not on whether you call `quest.bind`: last row
`BLOCKED` needs only 4 step rows and 2 PNGs (an honest `t.blocked` reached three or four steps in
cannot manufacture the green minimum -- trap 15); anything else (a green run) needs 8 rows and 4
PNGs.

- Either way, if you call `quest.bind`: at least one `quest.*` row, and either a passing
  `quest.varp_complete` or the ledger's last row is `BLOCKED` (a tier-4 stub must end there, not fall
  through).

- Every row YOU wrote with `t.exec` or `t.check` carries a shot, unless its detail carries
  `[frame unchanged]` -- the driver took that picture, found it identical to the last and dropped it
  (trap 4). The rule is per row and reads your source with comments stripped (`gate.py`'s
  `shooting_row_names`), so a verb named in a comment switches nothing on, and a `t.expect`/`t.step`
  row is never asked for one.

### How a guide step is DRIVEN (`helper_coverage.py`, seam26)

**How a guide step is DRIVEN** (`tools/quest_gate/helper_coverage.py`, seam26). Name each row after
the guide step it does, and press the step's target with the step's own op. **A row named after the
step** drives it. The name is the step's variable (`talkToUnferth`), with a suffix
(`drunkenAli-beer1`, `pickpocketVillager1`) or with the panel's puzzle-wrapper name
(`pwMsHynnTerprett` counts for `msHynnDialogQuiz`). The row does NOT count when its own line presses
the target with an op other than the step's.

The step's op is its text's first verb (`Talk to` -> Talk-to, `Kill` -> Attack, `Lure` -> Lure), and
it is checked only when that verb is one of the target's menu ops in `configs/all.npc` / `all.loc`.
Using the item the step lists on its target is never a conflict. **A line or row that only names the
target** drives the step only when (i) that line is not another guide step's own row, and (ii) it
presses the step's own op.

A step's own row is the FIRST row named after it plus that visit's non-pressing rows
(`talkToRolad-dialog`). A later row that presses again (`talkToRoladWithPages`,
`talkToJorral-handin`) is a visit of its own and can drive `returnToRolad`. So: two talks to the
same npc need two presses. A `returnToX` step with one `talkToX` row in the file grades UNMATCHED,
and the reason column lists each line it refused and why. **The only persisted state it credits** is
an Open/Close/Lock/Unlock press on a loc.

This covers a ConditionalStep leaf the guide shows only while the state is missing: The Restless
Ghost's `openCoffinToPutSkullIn` is met by the earlier `openCoffin` press, because the coffin is
still open. **Composite steps are graded by their parts.** A custom step class beside the guide
(`MissCheeversStep.java`, `SlugSteps.java`, `GiveIngredientsToHelpersStep.java`,
`DyeShipSteps.java`) contributes every step its `getPanelSteps()` / `getDisplaySteps()` lists.

A ConditionalStep subclass with no such list contributes its `addStep` children, and an
NpcStep/ObjectStep subclass is graded on the target of its own `super(...)`. The same holds for the
lists a guide grows with `.addAll(...)` and `panel.addSteps(...)`. Drive every one of these, or
declare it with a `-- GUIDE-GAP:` marker citing the `.rs2` line: a port that grants the whole Miss
Cheevers kit in one dialogue has fourteen undriven steps.

### A step named `teleportAway` grades UNMATCHED although its own row PASSed (sonnet-b41)

Before it matches rows to steps, `helper_coverage.py` drops every PASS row whose name starts with
`goto`, `walk`, `travel`, `teleport`, `tele-` or a bare `tele` (also `quest.`, `setup`, `reset`;
case-insensitive, the `action_rows` filter), and every row whose detail mentions `goto_tile` or
`::goto`: those are travel rows. A guide step whose own name starts with one of those words (Lost
City's `teleportAway`, a `walkTo...` or `travelTo...` step) is therefore never credited by a row
named exactly after it. Put the step's verb in front: `cast-teleportAway` (zanaris) still contains
the step's name and grades DRIVEN. `telegrabKey` is not caught (`tele` followed by a letter other
than `port`).

### `helper_coverage.py` reads FULL

`helper_coverage.py <id>` reads FULL, or its only gaps are CONTENT_GAP steps the file declares
(`t.blocked`/`content_bug`, or a `-- GUIDE-GAP:` marker citing the `.rs2` line); a step that is not
a gap carries a VERIFIED `BRANCH-IN`/`PARTNER`/`NOT-A-STEP`/`OBSOLETE`/`ANY-OF` marker and grades
EQUIVALENT, which counts as FULL -- trap 32; `--no-coverage` is for the `_conformance`/`_cheats`
harnesses only. `gate.py` runs this check only when the ledger has no BLOCKED row
(`if not findings and not blocked and not arguments.no_coverage`): on a blocked/`content_bug` run
the `-- GUIDE-GAP:` markers are optional and a MIXED verdict past the `t.blocked()` is informational
(the classifier credits a step whose symbol merely appears in the file).

### The ladder lists a route back to front; FULL does not see every sub-step (matthew-mbp-m4-b47)

The ladder follows the order of the guide's `addStep` calls, and a `ConditionalStep` lists its
LATEST state first. Regicide's Underground Pass prints `leaveWellCave, enterWell, openIbansDoor,
leaveUnicornArea, ...` down to `collectPlank`, which is the walk in reverse. Take the route from the
zones and the step texts, not from the ladder's order.

FULL grades only the steps the ladder holds. Regicide read FULL (64 of 64) while
`crossTheGrid` and its sub-steps `climbOverRockslide4` and `climbOverRockslide5` (the grid between
the pit and the lever) never appeared in the ladder, and the test crossed them with one `goto_tile`
(sampler-findings: Sample matthew-mbp-m4-b47). Read the guide's whole `ConditionalStep` tree for
the stretch you drive, and drive every obstacle in it, listed or not. Since seam
matthew-mbp-m4-b49-seam2 such a hop reads CHEAT anyway: see "lands at ... without pressing the
rockslide" below.

### "lands at ... without pressing the rockslide ... names" / "PASSed, but the newest server line of its attempt is" (b49-seam2)

*Origin: Regicide was sent back three times (reverts 9c28a4e0c, 9260f12d8, 7fc9688a4) for `goto_tile`
hops across the Underground Pass and the Tirannwn forest, each read FULL 64/64. The ladder grades
`climbOverRockslide1`..`5` and `passTrap1`..`5` as one step each (siblings that differ in one name word
are "one step, done any way"), so one press of rockslide 1 drove all five, and `crossTheGrid` was in no
ladder at all.*

**A goto across a route obstacle is CHEAT, whatever else drove the step.** Every `ConditionalStep` in
the guide is read, not only the branch-only states (seam36). An ObjectStep on an obstacle loc (a
door or gate, a rockslide, spear trap, rock swing, log, pitfall, tripwire, dense forest, ledge, ...; a
plain ladder or stair stays travel) that an `addStep(<zone>, step)` shows in zone A is skipped when a
goto row:

- left from a reading inside A (the run's own positions: a goto's `at x,z,l`, `standing at x,z level
  l`, `from a,b to x,z`, `tile=`, `teleport: a -> b`, a bare `x,z`; never the server lines quoted after
  ` :: `, and never `pressed the copy at`),
- landed outside A in another state of that route (a zone of the same `ConditionalStep`, or of any
  `ConditionalStep` that shows it, such as the pass section around a sub-route),
- on the same level of the same map frame (a climb or a dungeon exit is judged by the climb rules
  above),
- with no press of that step's own copy between them. A press names its copy as `pressed the copy at
  x,z,l`; a copy more than 4 tiles from the step's WorldPoint, like rockslide 1 for rockslide 4, does
  not count.

The step reads CHEAT even when another row pressed the same loc elsewhere. A step no ladder lists is
added to the report as its own CHEAT row. Example:
`climbOverRockslide4: ledger row 71 'goto-pullLeverAfterGrid' lands at 2466,9673,0 (isAfterTheGrid)
from 2466,9699,0 (isAfterThePit, row 69 'crossThePit-tile') without pressing the rockslide
climbOverRockslide4 names (rockslide2_obstacle_upass)`.

Where one zone is a state of several routes heading different ways (Regicide's `inWestForestPath`
shows the ring of leaves on the way to Iorwerth and the dense forest on the way to Tyras), the hop is
charged to the step whose WorldPoint it heads toward. It is charged to every candidate only when it
heads toward none of them.

**A crossing row passes on THIS attempt's line.** A PASS row named after an obstacle step whose last
` :: ` chat block opens with a failure (`...and fail, activating the trap!`; the block lists the
newest line first, and a multi-attempt detail's last block is its last attempt) is CHEAT: `ledger row
93 'passTrap5-tile' PASSed, but the newest server line of its attempt is '...and fail, activating the
trap!'`. Regicide's own `last_lines(4)` check found trap 4's "...and succeed" still in view. Count
the success lines before and after the press, or check the tile the attempt moved you to.

Drive it as the game does. Walk the route with `walk_to` hops and `click_loc` each obstacle, and
re-read the tile after each crossing (`standing at x,z level l`). Use `goto_tile` only between two
tiles in one state of the route, or out of it the way a player teleports out.

Not judged:

- a hop with any loc press, walk, `use_on` or cast between the last reading and the goto, because the
  start is unknown (re-read the tile after a crossing and the hop is judged from there). A goto row
  whose detail reads `at <landing> from <departure>` starts at its departure tile, and nothing between
  is consulted. The driver does not write that form yet (see "The departure tile" below);
- a hop back into the zone the run walked into A from (Regicide leg 6's two-tile step back east
  to `passTrap5`'s stand tile);
- a hop that starts outside every obstacle zone, unless it lands in another island of a route whose
  entry route it skips (next section).

#### "which no zone of ... joins to it ... the guide's way into ... is ..." (b50-grader)

*Origin: Regicide d3505f4ee row 75, `goto-crossThePit` from the voyage cave (2314,9624,
`isInWellEntrance`) to the pit (2461,9699). The round-6 sampler called it a teleport back into the
pass. The rule above missed it because the voyage cave's step, `leaveWellCave`, is not an obstacle.
2c35057c6 row 67 makes the same hop.*

**A goto from one island of a route to another, past the route's way in, is CHEAT.** Take a
`ConditionalStep` X whose constructor default D is itself a `ConditionalStep` with obstacle steps in
zones of its own (`travelThroughPassSection3 = new ConditionalStep(this, crossTheBridge)`). The guide
sends a player who is in none of X's zones through D, so D is the way into X's states. The grader
joins X's zones, and the zones of every ConditionalStep under X (D's included), into islands: zones
that overlap or sit within 2 tiles of each other are one island. A goto is CHEAT when:

- it lands in a state zone of X, on the island that D's zones touch, outside D's own zones;
- it starts in a zone of X on another island (the voyage cave does not touch the pass's zones);
- it stays on one level of one map frame, with nothing between to make the start unknown;
- no row between presses one of D's obstacle steps.

The charge goes to D's obstacle steps whose zones touch the landing state's zone, or to all of them
when none touch it. Example: `climbOverRockslide2: ledger row 75 'goto-crossThePit' lands at
2461,9699,0 (isInUndergroundSection2) from 2314,9624,0 (isInWellEntrance, row 74
'reenterVoyageCave-tile'), which no zone of travelThroughPassSection3 joins to it, without pressing the
rockslide climbOverRockslide2 names (rockslide2_obstacle_upass): the guide's way into
isInUndergroundSection2 is crossTheBridge (travelThroughPassSection3's default, guide line 741)`. The
bridge (`shootBridgeRope`) and rockslide 3 are charged for the same row.

Not judged:

- A default that is a single step. Quest Helper puts a route's last step in the default as often as its
  first: `pathToIorwerth`'s `talkToIorwerth`, `theUndergroundPass`'s `climbDownWell`,
  `goToTyrasCampEntrance`'s `enterTyrasCamp`. A single DOOR default is judged by the next section.
- A start in no zone of X. Open ground may reach the landing some way the guide does not name (except
  through the door of the next section).
- A start on D's island. The rule above judges those hops.

In the committed greens, 4 guides have such a route (grandtree, losttribe, regicide, tearsofguthix),
and no green step changes class.

#### The departure tile (gap (b), closed)

**LANDED (matthew-mbp-m4-b50 4ff821094; reaches v3 with that batch's PR).** Seam1 proved it and held it back; the orchestrator landed it, because the ten hops it exposes are cheats, not false positives. The driver change: `goto_tile` reads
`QD.world.tile()` once before its first `::goto` and opens its ok detail with `at <landing> from
<departure>` (`from ?` when the read fails). Live, d3505f4ee's `regicide.lua` wrote row 75 `at
2461,9699,0 from 2314,9624,0` itself and was charged CHEAT; the green regicide stayed FULL 64/64.
It was not landed because a full suite run with it turns TEN committed greens RED (every ledger
100% PASS; `helper_coverage` TEST_GAP on a hop no rule could see before): blackknight row 27
`goto-hole` (cabbage-hole room -> east room), desertrescue row 145 (deep mine -> mine 1 past the
winch and exit), eadgar row 14 (mountain path -> troll area past `troll_climbingrocks`), hero row 27
(garden -> secret room past `pete_sidedoor`), mourningsendparti row 125 (HQ -> basement past the
trapdoor), mourningsendpartii row 18 (basement -> caves past `mourner_hideout_door4`),
recruitmentdrive row 33 (west side -> Sir Kuam's room past `rd_bridge_right`), rumdeal row 95 (north
island past `deal_gate_closed`), totem row 29 (entrance -> stairway past `combodoor`), troll row 17
(troll area -> arena past its entrance). Most read as real gotos past an obstacle the guide names,
so all ten went back to `todo` in QUEUE.tsv with their hop named: re-author only that hop (drive the
obstacle), the rest of each file stands. A green re-run after this lands can read CHEAT on a hop
the grader could not judge before; that is the stamp working. Conformance row
`seam.goto_departure_stamp`. The suite's verdicts are
`build/seam_state/matthew-mbp-m4-b50-seam1/close_gate_after.txt` (matthew-mbp-m4).

The gap as the grader pass wrote it:

No ledger row records where a goto left from. `player.goto_tile` writes `at <landing>`
(script/plugins/quest_driver/pointer.lua). The server's `::goto` reply names only the destination
(`Teleported to x,z,l.`). client.log has no per-row tile. Across the committed greens, 993 of 1,735
goto hops have a press or walk between the last reading and the goto, and 406 change level or frame,
so the route rules judge only about a fifth of the gotos. The smallest driver change: `goto_tile`
reads `QD.world.tile()` once before its first `::goto`. Its ok detail then becomes
`at <landing> from <departure>`, and a retry's detail becomes `at <landing> from <departure> on
attempt ...`. The grader already reads that form (`hop_start`). Synthetic proof: d3505f4ee's ledger
with row 74's reading replaced by a press reads row 75 not judged, and with row 75 stamped `at
2461,9699,0 from 2314,9624,0` reads it CHEAT again.

Proof (`--ledger` grades another run's ledger):

```sh
git show 2c35057c6:test/quests/regicide.lua > /tmp/r.lua
git -C OSRS-Content show 5f4908df1:osrs239-content/server/scripts/selftest/quests/quest_regicide/play/ledger.tsv > /tmp/r.tsv
python3 tools/quest_gate/helper_coverage.py regicide --lua /tmp/r.lua --ledger /tmp/r.tsv
```

Results:

| Run | Before | After |
| --- | --- | --- |
| 2c35057c6 | FULL 64/64 | TEST_GAP (CHEAT=8; 9 with b50-grader's row 67) |
| d3505f4ee | FULL 64/64 | TEST_GAP (CHEAT=6; 8 with b50-grader's row 75) |
| 5deefa070 | CONTENT_GAP | MIXED |
| Green regicide (473 rows) | FULL | FULL |
| Committed greens, last runs (`build/quest_gate`) | 99 FULL | 99 FULL, no step changed class |
| Committed greens, published ledgers | 93 FULL + 6 older evidence | the same, no step changed class |

The reverted runs read TEST_GAP from the hops their samplers named: pit to lever, plank room to well,
temple to Iban's room, trap to trap, Tyras's camp to the sulphur, past the forests, ring to log, and
the three stale trap rows. 5deefa070's ring-to-log re-teleport reads CHEAT; its mid-run `::setlevel
agility 99` is not a goto and is not this rule's.

### "outside the building ... the guide's way in is ..." / "leaves ... that is the room's only way out" (b51 seam1)

*Origin: the matthew-mbp-m4-b51 sampler sent back two greens the grader read FULL. Black Knights'
Fortress 4b849c46b row 16 `goto-fortress-entrance` went from the White Knights' Castle (2960,3335,2)
straight to 3016,3516,0 inside the fortress, past `bkfortressdoor1`, whose `[oploc1]` is the disguise
check (quest_blackknight.rs2:10). Heroes' Quest 9bdd40c4d row 30 `goto-grip` went from inside the
sealed secret room (2781,3197) to 2774,3192 and meleed Grip, where the guide's `killGrip` says "kill
him with magic/ranged" from your own room (HeroesQuest.java:411). Seam
`goto_into_a_guarded_interior_from_outside_any_zone`.*

**A goto into the room a door default opens on is CHEAT** (`door_entries`). Take a `ConditionalStep` X
whose constructor default D is ONE ObjectStep on a route obstacle (door, gate, barrier...; a plain
ladder or stair is travel) with a WorldPoint. QH shows D whenever the player is in none of X's zones,
so D is the way in. The rooms D opens on are X's zone boxes within 2 tiles of D's point on its level;
a zone the guide shows D itself in is the door's outside and is left out (Eadgar's Ruse:
`returnParrotToEadgar.addStep(inTrollheimArea, enterEadgarCaveWithTrainedParrot)` is the mountain
before the cave). The building's footprint is the x,z hull of those rooms' island of X's zones plus
the floors above and below it. A goto is CHEAT for D when:

- it lands in one of the rooms D opens on;
- it starts outside the footprint on the same map frame, on any level (Falador's second floor counts);
- nothing between makes the start unknown (a departure stamp, or a reading with no press or walk after
  it), and no row between presses D's copy.

Example: `enterFortress: ledger row 16 'goto-fortress-entrance' lands at 3016,3516,0 (inMainEntrance)
from 2960,3335,2 (inFaladorF2, ...), outside the building infiltrateTheFortress's zones make, without
pressing the door enterFortress names (bkfortressdoor1): the guide's way in is enterFortress
(infiltrateTheFortress's default, guide line 299), whose door at 3016,3514,0 opens on
inMainEntrance`. Drive it as the game does: goto to the outside of the door, wear the disguise,
`click_loc` the door under a row named `enterFortress`.

Not judged:

- A landing deeper in the building than the room the door opens on. A building can have doors the guide
  does not name: Ernest the Chicken's `pickupSpade` says to leave Draynor Manor's east room "through the
  door in the same room", so the goto from the fountain into that room is not a hop past `enterManor`.
- A start in a dungeon frame (z + 6400). A ladder out of it may come up inside: In Search of the
  Myreque's hideout ladder comes up on the entrance island past `climbTree`.
- A start inside the footprint. A hop between the building's floors skips a ladder, not the front door;
  the climb rules judge it.
- A door several ConditionalSteps share as their default is charged to each of them. Mourning's End
  Part I's `mournerstewdoor` is the default of four, so one hop reads four CHEAT rows.

**A goto out of a guarded room, after which the room's step is driven elsewhere, is CHEAT**
(`room_exits`). A guarded room is a state zone Z of X that holds, within one tile, the WorldPoint of
an obstacle step O that X shows in ANOTHER zone A (Heroes' Quest: `useKeyOnDoor`, `pete_sidedoor` at
2781,3197, is shown `inGarden` and opens on `secretRoom`). S is the step X shows in Z (`killGrip`). The
step S is CHEAT when a goto meets all of these:

- it starts in Z;
- it lands within 24 tiles on the same level, outside Z, outside A and outside every zone of X;
- nothing between makes the start unknown, and no row between presses O;
- a PASS row before the next goto is named after S or quotes S's target (`killGrip`, `attackGrip`).

Example: `killGrip: ledger row 30 'goto-grip' leaves inSecretRoom (from 2781,3197,0, ...) for
2774,3192,0, in no zone of getThievesArmband, without pressing the door useKeyOnDoor names
(pete_sidedoor) that is the room's only way out, and row 32 'killGrip' drives killGrip there`. Drive
it as the guide does: stay in the room and use `t.player.cast` or a ranged weapon on Grip once he is
lured next door.

Not judged: a hop further than 24 tiles or to another level (a player may teleport out of a room), a
hop back to the door's side, a hop with an unknown start, and a room whose step is not driven after
the hop.

**Known gap: a sealed room the guide does not door-guard** (HALF FIXED matthew-mbp-m4-b56-seam1, e0d2cdfcc: `MapWalls` now reads the `.jl2` walls and `enclosure_entries` grades a goto INTO a walled room past its closed door, see below; a goto OUT through a wall still needs a guide obstacle step). `room_exits` knows a room is closed only
because an obstacle step's door opens on it. A room enclosed by walls the guide never names (no
obstacle step points into it), or a goto through a wall between two zones that the guide joins with no
step, cannot be judged without the map's walls. `walkable_probe` (src/torirsserver/test/) reads
standability, not wall edges, and no Python reader of the `.jl2` walls or the server's
`collision_map_bfs_path` exists for the grader. Until one does, the samplers judge these hops from the
map square (Heroes' Quest's room is walled in `maps/m43_49.jl2`, `snipable_wall` at 2780,3198).

Results (`--lua`/`--ledger` on the reverted runs; every committed green on both its last-run ledger and
its published ledger, 208 grades, `PYTHONHASHSEED=0`):

| Run | Before | After |
| --- | --- | --- |
| blackknight 4b849c46b + OSRS-Content 0baada3fd2 | FULL 32 | TEST_GAP: `enterFortress` CHEAT (row 16), nothing else changes |
| hero 9bdd40c4d + OSRS-Content 45e8690c34 | FULL 43 | TEST_GAP: `killGrip` CHEAT (rows 30/32), nothing else changes |
| regicide 2c35057c6 | TEST_GAP CHEAT 9 | the same |
| `helper_coverage_two_op_test.py` | 3/3 | 3/3 |
| Committed greens (104 x 2 ledgers) | 202 FULL + 6 already not FULL (eaglepeak x2, misc, itwatchtower, shadowstorm: older published evidence; ratcatchers: a concurrent run's ledger) | 200 FULL; the 6 unchanged step by step; mourningsendparti x2 is the only change |

The one green that changes is a real cheat: mourningsendparti row 172 `goto-cookToxin` goes from
Rimmington (2927,3211,0) into the Mourner HQ (2547,3323,0) to use its range. The HQ door
`mournerstewdoor` is quest-gated: area_ardougne_west/scripts/doors.rs2:19 checks the full mourner
disguise (mend1_disguise.rs2:335). Re-author that hop: go to the door, wear the gear, and press it.
Evidence: `build/seam_state/matthew-mbp-m4-b51-seam1/fix_guarded/` (`sweep_diff.txt`,
`synthetic/`: a landing outside the door, a door press before an unstamped goto, a hop to the
garden and an 81-tile hop each stay uncharged).

### A `BLOCKED` row reports `blocked`

A `BLOCKED` row with `fail==0` reports `blocked`, not `green`, and still exits non-zero unless the
check is run with `--allow-blocked`.

The GUIDE-GAP and verified-marker rules (BRANCH-IN, PARTNER, NOT-A-STEP, OBSOLETE, ANY-OF) are trap
32, in `traps-23-33.md`. What the coverage tool and the gate still miss is in `sampler-findings.md`.

### A promoted sub-step: "leaves the <step> side ... without crossing the ladder/stair/trapdoor" (seam31)

*Origin: seam30's deferred sub-step grader, landed in seam31 with three tier 1 tests clicking the
crossings.*

A guide sub-step with a target of its own (`addSubSteps`, at any depth) is graded as its own step
just before its parent. A test that was never in that state has nothing to drive there: the step is
ALTERNATIVE. A test that WAS on that side and left it by `goto_tile` for the parent's tile without
clicking the sub-step's loc is CHEAT (`teleported_across`), and the reason names both gotos.

- A ladder, stair or trapdoor sub-step is plain travel only when its trigger neither writes nor
  reads one of the quest's vars. A quest-gated climb (`mourning_hideout_trap_door`,
  `watchladderup`) teleported past is CHEAT. For a climb the sides are floors: a goto on the
  ladder's floor, then a goto chain to the parent's floor with no `click_loc` between, is a
  teleport past it.
- A `click_loc` on the sub-step's own loc between the two gotos means the crossing was made for
  real (under another row's name), and clears it.
- Drive the crossing with a real row named after the sub-step: biohazard
  `exitBackyardOfHeadquarters` (`mournerstewfence`), eadgar `leaveEadgarsCaveForThistle`
  (`troll_mad_eadgar_exit`), mourningsendparti `enterMournerBaseAfterPoison` /
  `enterMournerBasementAfterPoison`.
- Known looseness: a skipped ladder is reported against every promoted sub-step that shares its
  loc and parent, so the cited goto pair may be a sibling's. Only a PANEL step's stairs still use
  the plain ladder rule (mourningsendparti `enterBasementAfterSheep` grades TRAVEL).

### "presses op5 'pick-lock' ... a gating op: a 'enter' step needs the travel op" (seam35)

*Origin: the sonnet-b44 sampler reverted losttribe (c32508b14): the test pressed Pick-Lock on the
H.A.M. trapdoor, then `goto_tile`'d into the lair, and `helper_coverage` credited `enterHamLair` to
the `ham.picklock` row.*

A loc that gates its own crossing has two kinds of op: the GATING op that readies it (Pick-Lock,
Unlock, Light, Search) and the TRAVEL op that crosses it (Climb-down, Enter, Cross, Open, Go-through,
Squeeze-through...). A guide step that goes through a loc -- its leading verb (text, else name) is
enter, climb, cross, go, exit, leave, descend, ascend, squeeze, crawl, jump, walk, pass, swing or
board -- is credited only by:

- a press of one of the loc's travel ops (any state of a multiloc: `ham_multi_trapdoor` is
  Open/Pick-Lock while closed and Climb-down/Close while open), or
- a row whose detail shows the player put across it: a `teleport: a -> b` / `x,z,l -> x,z,l` pair
  that changes level or moves more than one tile (a content Pick-Lock that walks you through).

A Pick-Lock press alone is refused with the reason above, the step falls through to the goto rule,
and a `goto_tile` landing on the far side of a climb (the other map frame, or another level) is
CHEAT: `goto_tile 3152,9644,0 at line 162 lands past the trapdoor the guide names`. A press on a
multiloc CHILD state (`osf_trapdoor_closed`) is held to the step's op exactly like a press on the
guide's parent symbol, and a multiloc parent's triggers are its states' triggers when the goto
rule asks whether the climb writes a quest var. Drive it as the game does: the gating op under its
own row, then the travel op under a row named after the step
(`t.exec("enterHamLair", t.player.click_loc, "osf_trapdoor_open", 1)`). Fixture:
`python3 tools/quest_gate/helper_coverage_two_op_test.py` (3 cases on the reverted losttribe run).

### A branch-only step: "lands at ... without pressing the door <step> names" (seam36)

*Origin: the sonnet-b45 sampler reverted mm (16e31e41a): leg 8 `goto_tile`'d from the Ape Atoll
dock (2802,2707) straight to Garkor inside Marim (2807,2760), past the Bamboo Gate. The guide's
`enterGate` (MonkeyMadnessI.java:854) lives only in `bringMonkey.addStep(onApeAtollSouth,
enterGate)`, not in `getPanels()`, so the run read FULL.*

The ladder is now every step the `steps.put` ConditionalStep tree can show, not only the panels: a
leaf no panel lists (and that is not a folded sub-step or a custom class whose own panels were
spliced) is a BRANCH-ONLY step. `ladder.py` shows it as `<Kind><<composite>[<condition>]`, placed
before the state it leads to (`ObjectStep<bringMonkey[onApeAtollSouth]` just before
`talkToGarkorWithMonkey`). It is graded like a promoted sub-step: a state the run may never be in.

- Driven by a real row (or a click on its target) it is DRIVEN. Panel steps take their credit
  first, so a branch-only step never steals a panel step's click.
- Not driven, it is ALTERNATIVE (TRAVEL for a plain climb), unless the run teleported past it. A
  strong CHEAT stays CHEAT: a debugproc named after it, a stand-on, or its own item `::give`n.
- **CHEAT -- the zone crossing.** The step is an ObjectStep on a gated loc (door, gate, barrier,
  raft...; a ladder or stair only when its trigger writes or reads a quest var), and its
  `addStep` condition holds the player in zone A (a `ZoneRequirement`, by name or inline; a
  `not(...)` is skipped). A goto row then landed in a zone that a sibling listed BEFORE it in the
  same ConditionalStep is shown in (a later state), from a position in A, with no row between
  pressing the loc. The positions come from the run's own ledger: a goto row's `at x,z,l`, a
  leg's `tile=`, `checkpoint N written at`, `teleport: a -> b`, `still at`, or a detail that
  opens with `at x,z,l`. `pressed the copy at` is a loc's tile and is never read. Example reason:
  `ledger row 263 'goto-talkToGarkorWithMonkey' lands at 2807,2760,0 (onApeAtollNorth) from
  2802,2707,0 (onApeAtollSouth, row 261 'leg.8.garkor_to_narnode') without pressing the door
  enterGate names (mm_bamboo_largedoor_left)`.
- Drive it as the game does. For mm: on the south side, wield the Karamjan greegree (`inv_op ...,
  2`), `t.exec("enterGate", t.player.click_loc, "mm_bamboo_largedoor_left", 1)`, then Kruk's page
  (`npc:Open the gates`). The gate `p_telejump`s you 3 tiles north (mm_bamboo_doors.rs2:32).
  Proved by `build/seam36_proof/mm_enter_gate.lua`: 2721,2762 -> 2721,2768.
- Limits. The rule needs a position reading before the goto: a run whose last position row is
  elsewhere is not judged. A branch-only step whose condition names no zone is never CHEAT by
  crossing. An NpcStep journey (Lumdo's boat) is not judged here either.
- Regrade on 2026-10-01, all 93 committed tests with a guide: 121 steps added (DRIVEN 67,
  ALTERNATIVE 35, TRAVEL 18, CHEAT 1), no existing step changed class, and the only verdict
  that changed is mm, FULL -> TEST_GAP. The mm ladder grew from 76 to 78 steps (`useWool`,
  `enterGate`).

### A setup `::give` of The Giant Dwarf's Consortium ores and bars is a brought item (sonnet-b42)

*Origin: the sonnet-b42 giantdwarf reviewer left "Consortium ore/bar tasks staged with ::give:
rule (c) risk, unjudged"; the sampler judged it from the guide.*

TheGiantDwarf.java lists the points-game deliveries as items, not as gathering steps: "Various
ores and bars" (`oresBars`, `canBeObtainedDuringQuest`, line 115) and ten each of copper, tin,
iron, coal, silver, gold and mithril ore and bronze, iron, silver, gold, steel and mithril bar
(lines 134-146). No guide step mines or smelts them, so a setup `::give` of them stages a brought
item and is not trap 16. The deliveries themselves are the quest's work: every hand-in to the
Consortium must be a driven `talk_to`/`use_on` row that moves the points varp.

### A `-- GUIDE-GAP:` over a `::give` of a quest drop reads FULL, and it is still a cheat (In Search of Knowledge, matthew-mbp-m4-b52)

*Origin: the matthew-mbp-m4-b52 reviewer noted "12 tattered pages: one is a real drop, the other
11 are ::give with a GUIDE-GAP comment; the fast-forwards doc names no page cheat". The sampler
sent the quest back.*

`insearchofknowledge.lua` killed until one tattered page dropped. Then it ran
`::give hosdun_sun_page 4`, `::give hosdun_moon_page 4` and `::give hosdun_temple_page 4` under
`-- GUIDE-GAP: getPages ... no cheat names them`. `helper_coverage` read FULL and the reviewer
accepted it. All twelve pages the tomes took could have been given ones: the temple insert went
`5->1`, so the real drop was left over in the backpack. A drop that the inserts do not need
proves nothing.

The marker cited `leftover_forthos_combat_page_drops`. That proc is only a mesbox saying the drops
are deferred. The drops have since been ported: `isok_page_drop`
(`insearchofknowledge_locs.rs2:166`) runs from `red_dragon.rs2:10` and `npc_combat.rs2:145`. It
rolls 1/10 on a red dragon, 1/20 on an Undead Druid, 1/25 on a baby red dragon and 1/30 on a
temple spider, inside map square 28_155 only, and each hit is one of the three pages at random.
A GUIDE-GAP is for work the port cannot do. Before you write one, read the line it cites and grep
the item's `obj_add`. A leftover mesbox does not prove that the mechanic is missing.

No grind fast-forward covers a drop. The only sanctioned ones are the debugprocs in traps-23-33
("Sanctioned grind debugprocs"), and a `::give` of the item a drop hands over is trap 16. Drive
the drops: about fifteen pages give four of each kind, so expect roughly 150 red dragon kills, and
check the frame budget first (gaps-combat: "A run has about 2,000 server ticks"). If the budget
cannot hold that, ask a seam pass for a `[debugproc]` that runs `isok_page_drop`'s own roll on a
real kill, with a read-back row after each call.

### Gate RED on the last leg's `leg.N.end` row, and a Search press graded UNMATCHED (vm-b1; the Search half fixed in vm-b1-seam4)

*Origin: the vm-b1 Darkness of Hallowvale review (`test/quests/wip/darknessofhallowvale/relay.md`).*

GATE RED: "step 'leg.6.end' is written with t.exec/t.check, which always shoots, but has no shot
recorded". After the LAST leg the legs driver writes its own `leg.<k>.end` row with the checkpoint
reply and no shot (`core.lua`, `if k == last`). `gate.py` collects every literal row name the file
passes to `t.check`/`t.exec`, so an author's own `t.check("leg.6.end", ...)` in the last leg makes the
driver's shotless row of that name a finding. Name your closing row of the last leg something else
(`leg.6.finish`, Cold War's `leg.5.quiet`). Earlier legs may keep `leg.N.end`, because the driver
writes the end row only for the last leg.

`kickBoard` UNMATCHED, "presses op1 'Search' ... a gating op" -- FIXED in seam pass vm-b1-seam4
(`doh_kickboard_travel_grade`). `travel_op_conflict` reads the first word of the guide text as the
step's verb. Quest Helper's `kickBoard` says "Climb up the walls and search the marked floor", and
its floorboards loc (`meiyerditch_wall_floorboards_multi_loc`) also has Climb-down in its kicked
state, so the step was held to a travel op, and the Search press, which is the step's real work
(`doh_meiyerditch.rs2:47`; it moves nobody: a scratch run reads `tile 3590,3173,1 -> 3590,3173,1`
across the kick), did not count. The grader now also reads the verbs that open the guide text's LATER
clauses (`Grader.clause_verbs`: split at `and`/`then`/`,`/`;`/`.`): a press whose op word is one of
them is the op the guide names, not a gating op. The leading "Climb up the walls" is the walk there.
A gating op the text does not name (Lost Tribe's Pick-Lock for `enterHamLair`) is still
refused. Write the row on the op the guide names (`kickBoard` = the Search press, then its Yes) and
give the following Climb-down its own guide step's row (`climbDownBoard`); no GUIDE-GAP is needed.

### CONTENT_GAP at an unrelated line on a `goToX` step that has its own PASS rows (FIXED vm-b1-seam4)

*Origin: seam pass vm-b1-seam4 (`doh_gotomines_graded_gap`, v3 c458a4d92).*

Darkness of Hallowvale's `goToMines` is a talk to a Vyrewatch ("Send me to the mines"), but it graded
CONTENT_GAP citing `doh_castle.rs2:64`, a Safalaan line that only shares the words "sent" and
"vyrewatch". `Grader.action_rows` dropped every row whose name starts with `goto`/`walk`/`travel`
(case-folded) as a travel row, so the test's own `goToMines`, `goToMines-dialog` and `goToMines.at`
rows never counted, and the step fell through to a weak narration match. Now a row whose head segment
names a guide step (or alias) is graded as the step's action. It stays travel only when its source
line is itself a `goto_tile`, `::goto`, `::tele` or `player.teleport` call (Fenkenstrain's
`goToMonsterFloor1`). Name the row after the guide step, as usual; no GUIDE-GAP is needed.

### CONTENT_GAP "only <other quest>.rs2, another quest's" for a trigger that serves every quest (matthew-mbp-m4-b55)

Meat and Greet's `leaveColosseumToReturnToEmelio` names `colosseum_exit_lobby`. `helper_coverage`
said "no [op*]/[ap*] trigger on colosseum_exit_lobby serves this quest (only
twilightspromise.rs2:367, another quest's)" and then graded the step ALTERNATIVE, a sub-step of
`returnToEmelioWithNewsOfYourAdvertisingSuccess`. But `[oploc1,colosseum_exit_lobby]` is an
unconditional `p_teleport` out of the lobby, so it works for every quest. The test used
`goto_tile` from 1819,9485 to Emelio and skipped the exit the guide names, while the grader read
FULL. The sampler sent the quest back (sampler-findings: Sample matthew-mbp-m4-b55). When the
grader cites another quest's file, open that trigger. If it reads no quest var, click it. The
failure-path legs (`leaveColosseumToGetAnotherKebabFromEmelio`, `enterArenaAfterFailing`) are a
real ALTERNATIVE when the first fight is won. The grader half is FIXED: see the next section.

### A goto out of a place whose exit the guide names reads FULL through "no trigger ... serves this quest" (Meat and Greet, matthew-mbp-m4-b55; FIXED)

Meat and Greet's `leaveColosseumToReturnToEmelio` names `colosseum_exit_lobby`. The test left the lobby
with one `goto_tile` from 1819,9485 to Emelio and helper_coverage still read FULL. Two holes, both closed:

- **A plain exit in another quest's file serves every quest.** The only `[oploc1,colosseum_exit_lobby]`
  lives in `quest_twilightspromise/scripts/twilightspromise.rs2:367`, and `relevant_triggers` counted
  another quest's loc only when that quest is a prerequisite, so the step read CONTENT_GAP ("no trigger
  serves this quest (only ..., another quest's)") and then ALTERNATIVE. A trigger whose body reads no
  `%variable` (`trigger_is_unconditional`) does the same thing for everyone and now counts.
- **The departure stamp is where the player stood.** `teleported_across` judged a promoted sub-step by
  the goto BEFORE the hop, which here landed outside the Colosseum (the player then walked in by the
  entrance). When the goto row carries `at <landing> from <departure>`, the departure tile is now the
  "before" side: beside the sub-step's loc, landing at the parent, no click on the loc since the last
  goto -> CHEAT "leaves the <step> side (departure x,z,l stamped by its own row) ... without crossing".
  The parent's targets are matched by family, so a guide id `mag_emelio_1op` is found in a test that
  talks to the shell `mag_emelio`.

What an author does: click the exit the guide names, then walk or goto from where it lands. All 130
committed greens grade the same before and after the change.

Fixture: `python3 tools/quest_gate/helper_coverage_departure_cross_test.py` (3 cases on the reverted
Meat and Greet run: the goto from the lobby is CHEAT, a click on the exit first is DRIVEN, the same goto
stamped outside the Colosseum is not a cheat). It reads commits 171bc81b0 and OSRS-Content 343f1b4163,
which live on the b55 batch branch until that batch merges. On the grader before the fix it fails 2/3.

### A goto back into a cave reads FULL when an earlier row already clicked its entrance (The Eyes of Glouphrie, matthew-mbp-m4-b55; FIXED by `frame_entries`, see the next section)

The Eyes of Glouphrie's guide uses `enterCave` (Brimstail's cave entrance) as the default step of
almost every stage: `ConditionalStep(this, enterCave)` with `addStep(inCave, ...)`, and in the
repair stage `fixMachine.addStep(magicGlue, enterCaveAgain)`. The round-3 test clicked the entrance
twice (rows 3 and 44). After that it went back into the cave three times with `goto_tile`:

- `goto-repairMachine`, from the evergreen at 2359,3529,0;
- `goto-killCreature1`, from Narnode at 2466,3496,0;
- `goto-allDead`, from the Grand Tree.

helper_coverage still read FULL 23/23. `enterCave` and `enterCaveAgain` are panel steps, matched to
their first PASS row, and the crossing checks (`zone_crossing` for branch-only steps,
`teleported_across` for promoted sub-steps) never look at the stages after that. The sampler sent
the quest back.

What an author does: when you are outside a place and the guide's step for that state is its way
in, goto only to the entrance, click it, and read back the landing tile. Do this every time, not
just the first. What a sampler does: check every goto row's `at <landing> from <departure>` against
the guide's zones. A departure outside and a landing inside, with no click on the way in since the
last goto, is a teleport past it.
### A goto from one cave into another reads FULL (Eadgar's Ruse, matthew-mbp-m4-b55 round 4; OPEN)

`frame_entries` only judges a goto that LEAVES from the surface. A goto that starts in one
underground place and lands in another is not judged, even when the real route goes up to the
surface and back down through a door the guide names. Eadgar's Ruse round 4 went from Eadgar's
cave (2890,10085,2) straight to the Troll Stronghold prison rack (2829,10097,0) and to Burntmeat's
kitchen (2844,10057,1). On the way it skipped `troll_mad_eadgar_exit`, `troll_stronghold_door` and
the stairs. The guide names all of them for that state (`leaveEadgarsCaveWithParrot`,
`enterStrongholdWithParrot`, `goDownNorthStairsWithParrot`, `goDownToPrisonWithParrot`, and the
same for stages 85 and 87). The steps still read DRIVEN, because helper_coverage matched them to
other rows: an earlier `catchParrot` row, and the first visit's door click at line 271.

- What an author does: leaving one cave for another place underground is two trips. Click the
  exit, goto the next entrance on the surface, click it, then click each flight of stairs. Read back
  the landing tile (and level) after each click.
- What a sampler does: list every goto row whose departure AND landing are both above z 6400. If
  the two tiles are in different places (a different dungeon, building or floor), find the guide's
  steps for that state. A goto that skips any exit, door or stairs the guide names there is a
  teleport.
- Grader gap (OPEN): judge an underground-to-underground hop the same way as a surface-to-cave one
  when the landing zone is a step's zone and the departure is outside every zone of that step.

### "lands at ... from ..., another map frame, without pressing the entrance <step> names ... on every visit" (`frame_entries`, matthew-mbp-m4-b55)

The Eyes of Glouphrie's guide is `new ConditionalStep(this, enterCave)` with `addStep(inCave, ...)`
at almost every stage. The round-3 test clicked Brimstail's cave entrance twice, then came back into
the cave three times by `goto_tile` (from the evergreen, from Narnode, from the Grand Tree) and
helper_coverage read FULL: `enterCave` already had a PASS row. The sampler sent it back.

`frame_entries` now charges the entrance step for every such hop. It reads a `ConditionalStep` whose
constructor default is ONE ObjectStep on a route obstacle, and whose state zone lies in another map
frame directly under (or over) that obstacle: the zone's box, folded by whole 6400-tile frames, holds
the obstacle's tile within 16 tiles. A goto row that leaves the entrance's frame from outside every
zone of that step and lands in the zone, with no press of the entrance between, is CHEAT on the
entrance step.

- Not judged: a hop inside one frame (the other route rules read those), a start inside one of the
  step's own zones, and a dungeon the map puts somewhere else than under its mouth (Eadgar's Ruse's
  `useParrotOnRack` defaults to the Troll Stronghold entrance, and Eadgar's cave is not under it).
- What an author does: goto only to the entrance, click it, read back the landing tile. Every visit.
- One committed green moved when the rule landed: Eadgar's Ruse, `goto-eadgar-3` from Ardougne
  (2610,3287) into Eadgar's cave (2890,10086,2) past `troll_mad_eadgar_entrance`
  (`enterEadgarCaveWithTrainedParrot`). It was reopened into batch matthew-mbp-m4-b55. The other 129
  grade the same before and after.

Fixture: `python3 tools/quest_gate/helper_coverage_frame_entry_test.py` (3 cases on the reverted Eyes
of Glouphrie run, commits 9813e5044 and OSRS-Content 30d5622731 on the b55 batch branch until it
merges). 3/3 on the rule, 1/3 on the grader before it.

### `enclosure_entries`: a goto into a walled room past its closed door (matthew-mbp-m4-b56-seam1)

The b56 sampler sent Tale of the Righteous back for a goto past Phileas's house door that
`door_entries` graded DRIVEN: that rule needs a guide `ObjectStep` on the door, and the guide has
none. `MapWalls` (helper_coverage.py) parses `maps/m<x>_<z>.jl2` walls and the `.jm2`
blocked/bridge flags with the server's stamp rules (`torirs_server_scene.c`), `blockwalk` from
`configs/all.loc`. A door is a wall loc with an Open op or a door/gate name (an open leaf with only
Close never closes a room). `Grader.enclosure_entries` floods the landing (at most 400 tiles,
following the room's own climbs to their floors) and charges the goto's step CHEAT when the room has
a closed door in its perimeter, the departure is outside it, and no PASS row pressed that door in
the 500 ticks before. Proof: the reverted Tale ledger FULL 30/30 -> TEST_GAP (rows 36, 93);
`tools/quest_gate/helper_coverage_enclosure_entry_test.py` 6/6, 1/6 with the rule stubbed out.
Landing it moved 70 committed greens plus bonevoyage, every one on this rule alone (Hetty's house,
Juliet's room, the Duke's room, Phileas ...); their rows were reopened. Conservative misses: a
building with a climb down from level 0 (the Champions' Guild), a room over 400 tiles, run-time
locs, a door-less sealed room reached by a ladder.

### A goto onto a table, a stair or a bar's back reads FULL; a goto out of a room, or with no step named, too (matthew-mbp-m4-b56-grader2)

The b56 round-1 sampler found gotos of the `enclosure_entries` class that the rule still read FULL
in four tests (atfirstlight, twilightspromise, wanted, dreammentor; shot_sampling_b56_2026-10-03.md).
Why each missed, and what grades it now:

- **The landing was ON a loc.** A goto lands where it is told, and these landed on Atza's table, a
  barrel, `civitas_stairs_1x3`'s footprint, the bar's rock: `MapWalls.enclosure` read a blocked tile
  as no room at all. It now floods from the whole footprint of the loc the player stands in (a
  2x3 staircase steps off at its front). A blocked landing whose flood reaches no walkable tile
  (water, the museum barge's gangway) is still no room. A landing on a diagonal wall at a room's
  corner (`goto-talkToAtza`, 1696,3061, shape 9) is read by where the player stood next, before any
  press: `(a blocked tile on the room's edge; the player stood at x,z,l next)`. A departure on a
  blocked tile that steps off into the room is inside it (this removed one seam-1 false CHEAT:
  death's `goToHaroldDoor1`, from the top of the stairs into the corridor).
- **No guide step to charge.** `goto-mage`, `pos2.goto` (Wanted!) and `goto-enterHQ` (no
  `enterHQ` step) were dropped because `_hop_step` found no leaf. Such a hop is now charged to the
  next guide row before the next goto (`goUpHQ`), else reported as a step of its own, CHEAT:
  `(goto past <door>)`, `(goto out past <door>)`, `(goto into a sealed pocket)`, `(goto out of a
  sealed pocket)` with "no guide step to charge: ...".
- **`enclosure_exits`: "leaves a room the map walls in ... it went out past the closed door".**
  The mirror: the departure's flood is a room with a closed door, the landing is outside it on the
  same level at most 24 tiles away (`ENCLOSURE_EXIT_TILES`, `room_exits`' policy: further may stand
  for a teleport out), and no PASS row pressed the door in the 500 ticks before. Dream Mentor's
  `goto-returnToOneiromancer` (76 tiles out of the brazier hall) is left to that policy.
- **`sealed_entries`: "lands at ... in a pocket the map closes on every side".** A landing whose
  flood closes with no door and no climb, from a tile outside it on the same level of the same
  frame: behind Verity's bar (`goto-talkToVerity`, 14 tiles, the flap has no op), behind the
  Custodia and Rising Sun bars, inside the Ikov web, in Kennith's room. A pocket whose edge or
  inside has a loc with an op or a script trigger is charged only when none of those was pressed in
  the 500 ticks before (Wanted!'s `pos4.goto` past the swamp cave's stepping stone), and not at all
  when the run uses one right after the goto (it stood the player AT that loc: Spirits of the Elid's
  root, Twilight's Promise's Colosseum entrance).
- **`sealed_exits`: "leaves a pocket the map closes on every side ... no walk leaves it"**
  (matthew-mbp-m4-b62-seam2). The mirror of `sealed_entries`: the DEPARTURE's flood closes within
  400 tiles with no door, no climb and no climb down from level 0, and the landing is outside it on
  the same level of the same map frame, at ANY distance (a pocket has no door to walk out of, so
  `enclosure_exits`' 24-tile cap does not apply). It is charged unless a PASS row pressed one of the
  pocket's op locs in the 500 ticks before (since matthew-mbp-m4-b63-seam1: only a press made after
  the player last arrived in the pocket that nothing after shows still inside; see "A goto through
  the only gate" below). Another Slice of H.A.M.'s `goto-talkToTegdak` left the
  train platform (2488,5536, 296 tiles, its only op loc the way back to the city) for Tegdak in the
  dig and read FULL. Landing it reopened Holy Grail's `goto-talkToFisherman` (out of the pocket
  past the Black Knight Titan, where `defeat_titan` lands the player) and Watchtower's
  `goto-leaveGrewIsland` (over the water from Grew's island instead of the rope swing). Fixtures:
  `asoh_sealed_departure`, `asoh_sealed_departed_outside`, `asoh_sealed_op_pressed`,
  `asoh_run2_full_route`.
- **Deliberately not graded:** a goto across a climb the guide names in an ObjectStep (the White
  Knights' Castle stairs, the Taverley Dungeon ladder, the Lumbridge cellar trapdoor). Plain climbs
  are travel; a prototype charging them moved 24 more tests (8 greens) and reverses that policy.
  The Champions' Guild keeps its climb-down skip: `champions_trap_door_open` has no maplink row, so
  where the cellar is cannot be read. McGrubor's Wood (876 tiles), the essence mine (825) and the
  Black Knights' base in Taverley Dungeon (707) are over the 400-tile room limit.

What an author does: never goto onto furniture or behind a counter; stand on the walkable side and
talk or use across it. To leave a room, click its door (or walk out an open doorway), then goto.

Proof: `tools/quest_gate/helper_coverage_enclosure_hops_test.py` (12 cases on the four round-1 runs,
commits 7936d2d97 and OSRS-Content 994d64d507): 12/12, 4/12 with all four changes off, and each
change off fails its own cases. Landing it moved 15 committed greens of b56 (blackknight,
cooks_assistant, eadgar, enlightenedjourney, hauntedmine, hero, hunt, ikov, itgronigen, murder,
queenofthieves, recruitmentdrive, seaslug, shadowsofcustodia, totem) and 25 of the 71 seam-1
reopened rows; the list is build/seam_state/matthew-mbp-m4-b56-grader2/movers.tsv. Since
matthew-mbp-m4-b62-seam2 the same file holds 16 cases (the four `asoh_*` `sealed_exits` cases on
Another Slice of H.A.M.'s round-7 pair ab832b98d / OSRS-Content 58364738ca; `asoh_run2_full_route`
is graded only while its build/orchestrator/fix_b62/r2/ files exist, else 15): 16/16.

### `reach.py` says NEEDS-OP; a goto over a trap, a log or climbing rocks reads CHEAT (matthew-mbp-m4-b60-seam0)

The sample tools (`test/quests/orchestrator/matthew-mbp-m4/reports/sample_tools/reach.py`,
`goto_table.py`, `comp.py`) and `MapWalls` used to treat every `blockwalk=0` loc as floor. Regicide's
pitfalls (Jump), tripwires (Step-over) and woodsprings (Pass) are such locs, and
`regicide_traps.rs2:118-153` hurts a player who steps on them, so a goto from the Arandar gate to
Islwyn's camp read `REACH closed-doors len=545`. Three b59 tests (rovingelves, mourningsendparti
twice) went back for gotos the tool had called clean (sampler-findings, "Sample matthew-mbp-m4-b59,
round 3" (a)).

Now, by default:

- **An OP LOC blocks the flood.** That is a loc on a tile the map leaves walkable (`blockwalk=0`, or a
  ground decoration that is not `blockwalk=1` and active) with any `op1`-`op5`. Two kinds are not op
  locs. A loc whose every op is in `PASS_THROUGH_OPS` is walked through: an open door leaf (Close) and
  a crop (Pick: X Marks the Spot's dig in the Draynor wheat). A wall decoration (shapes 4-8) is clicked
  from the tile beside it.
- **A ZONE-TRIGGER tile blocks too.** These are the tiles a `[zone]`/`[mapzone]` timer hurts you on,
  and `sample_tools/zone_triggers.tsv` lists them with the `.rs2` file:line. It covers Regicide's
  tripwire (its tile and the tiles north and east of it), Regicide's pitfalls, and the Underground
  Pass spear traps (the tile and the tile north). Add a row when you find another walk trigger.
- **`reach.py` answers one rung**, in this order:
  1. `REACH closed-doors`.
  2. `NEEDS-DOOR via <door>@x,z`.
  3. `NEEDS-OP len=N via <loc>@x,z,... [doors ...] [zone triggers: <file:line>]`. This is the path that
     clicks the fewest op locs, then crosses the fewest doors. It may also cross a ground decoration
     that blocks and has an op, such as Troll Stronghold's climbing rocks. REACH and NEEDS-DOOR treat
     those as solid.
  4. `UNREACHABLE`.

  Pass `--allow-op-locs` to get the old flood back. `goto_table.py` tries margins 30, 100 and 250
  until a hop reads REACH. A charge is the widest box's. `comp.py` prints the doors and op locs on the
  edge of its component.
- **`MapWalls` (`enclosure_entries`, `enclosure_exits`, `sealed_entries`, `sealed_exits`) agrees.** Op tiles and
  trigger tiles stop its flood as blocked tiles do (`MapWalls.OP_LOCS_BLOCK`). The loc becomes one of
  the room's `ops`.

What an author does: walk to the trap and press it (`click_loc` with the op), then grade the tiles
before and after. Never goto across one. Stage the Agility level the trap needs in setup.

Proof: `tools/quest_gate/helper_coverage_op_loc_reach_test.py` 9/9. The test runs each rule in both
settings:

- The b59 rovingelves round-1 ledger (OSRS-Content 9f83f98e1b): rows 7, 16, 38, 44 and 93 read
  NEEDS-OP via the pitfall at 2276-2278,3263 and the woodspring at 2235,3181. With `--allow-op-locs`
  they read REACH.
- The round-3 ledger (dafbe3c8e3) stays clean.
- Lumbridge gotos stay REACH.
- Monkey Madness `goto-talkToZooknock` grades CHEAT: a 294-tile pocket past
  `mm_double_springtrap_trigger`. With `OP_LOCS_BLOCK` False it grades DRIVEN.

What landing it moves:

- **`helper_coverage --all-green`.** Of 78, only mm changed: FULL went to TEST_GAP (rows 110 and 221),
  and `gate.py mm` is RED.
- **`goto_table.py` over all 142 published ledgers (2,384 goto rows).** Six new NEEDS-OP rows:
  - mm 110 and 221;
  - regicide 362 (`goto-passTrap5-again` across `upass_speartrap`);
  - troll 15 (`goto-enterArena` over the climbing rocks);
  - contact 6 and 135 (`icthalarins_door_arch`). This arch is `blockwalk=0` with an Open op and has no
    `[oploc]` handler, so a sampler should judge it.

  The wider margins also turned 11 old `UNREACHABLE (margin 30)` rows into NEEDS-DOOR (desertrescue's
  `thttmineexitl` four times, Taverley's member gate, ...) and 27 into REACH.

### A "use X on Y" step reads DRIVEN though the run used another item on Y (Shades of Mort'ton, matthew-mbp-m4-b57-grader3)

The b57 sampler (sampler-findings.md, b57 (a)) sent Mort'ton back. The run cured Razmire and
Ulsquire with Serum 207 and never made Serum 208, but `use208OnRazmire` and `use208OnUlsquire`
still read DRIVEN: "an action at line 366 names 'razmire_keelgan_afflicted'". Line 366 is only a
`by_symbol` lookup. `use207OnFlame` read DRIVEN from the olive oil used on the altar. The verdict
was FULL. There were two causes. `Guide.items` kept only an ItemRequirement's first id: no
`addAlternates`, no ItemCollections, no `addIcon`. And nothing knew `mort_serum3` was a dose of the
guide's `mort_serum1`. So the old `^use` check never saw the cure as a guide item, and the
any-action-names-the-target loop drove the step.

`Grader.use_item_wanted` / `use_item_driven` replace that now:

- **Which steps.** A step counts when it has an npc/loc target and its text leads with "Use", or
  when it is named `use<X>On<Y>`. A step named for another verb that a later clause does is left
  out: "Use the filled druid pouch on a ghast ... and kill it" is `killGhasts`. So is "use the
  ancient mace's special attack" (`useSpecial`) and a step that only carries its items.
- **Which item.** Take the `use <X> on|with|in <Y>` clause whose `<Y>` names the target. "Then use
  it on the pipe" means the clause before's `<X>`. X is the requirement or icon its words name
  best, by the requirement's name or the display name of any id it accepts. If no name matches, use
  the highlighted requirement, then the icon. Every id the requirement accepts counts: the ctor's,
  `addAlternates`, ItemCollections (Quest Helper's `ItemCollections.java`).
- **Same item.** `obj_family`: the noted form, placeholder and stack images (`certlink`,
  `placeholderlink`, `countobj`), and dose or charge variants. Those are the objs whose display
  name matches once a trailing `(N)` is cut, where one of the two carries the `(N)`. Olive oil(3)
  is the guide's Olive oil(4). `same_thing`'s display rule also counts (Viking's sealed vase,
  `_water` / `_frozen`). Not `next_obj_stage`, which walks into other objects (`oliveoil4 ->
  sacred_oil4`).
- **What drives it.** A `use_on(item, target)` call on the step's target, or one whose row is
  named after the step when the call names no npc or loc this cache has (an unbound variable): a
  row named after the step never stands in for a use on ANOTHER npc or loc (b66-seam1, the section
  at the end of this file). The call's item must be in the family. That is its literal, the local it
  was bound to, or a loop over `ipairs({...})`. If the item is a variable the test never binds,
  the row's `[backpack: ... lost X]` must show the item left. A call that writes a named row needs
  a PASS of that row in the ledger (`"x" .. i` matches by prefix). Without one, the run never got
  there, and the step is refused. A row named after the step with no `use_on` counts when its
  backpack diff lost X; a row a `use_on` line writes is judged by that call's target only.
- **Otherwise.** The other classes grade it as they grade any undriven step. UNMATCHED reads "no
  use_on of the step's item drives it: the guide's step uses <item> (<ids>) on <target>; line N
  (ledger row ...) uses <what it used>".

What an author does: use the guide's item on the guide's target with `t.player.use_on`. If the port
offers another item (Legends' glowing dagger on Ungadulu), or the content's own op does the job
(the Red Vine "Check", Viking's pipe), declare it with an `ANY-OF:` / `GUIDE-GAP:` marker that
cites the `.rs2` branch.

Proof: `tools/quest_gate/helper_coverage_use_item_test.py` uses the reverted Mort'ton run (Lua
03d4ac076, OSRS-Content ledger b52050c293). Rule on: 8/8. With `QUEST_GATE_USE_ITEM_RULE=0`: 5/8.
With the rule off, the three steps are DRIVEN, the unbound-item and never-ran cases fail, and
every other grade is identical to v3 over the 62 b57 greens. The rule moves seven green steps in
six tests, listed in build/seam_state/matthew-mbp-m4-b57-grader3/movers.tsv:

- legends `talkToUngaduluForForce`: the glowing dagger, not the dark dagger.
- itwatchtower `useNightshadeOnGuard` and `useNightshadeOnGuardAgain`: the run stopped BLOCKED
  before them.
- shadowstorm `useImplementOnGolem`: the same.
- fishingcompo `goToRedVine`: a click on the vine.
- thefeud `pickupDung`: no use at all.
- viking `useStrangeObjectOnPipe`: a click on the pipe.

Two verdicts change: legends FULL -> TEST_GAP, shadowstorm TEST_GAP -> MIXED.

### "names <npc>, but its own action does not reach that npc": a row named after the npc is not a talk (matthew-mbp-m4-b62-seam1)

An NpcStep's noun match ("retrieve it from Harold" against a row whose NAME holds `harold`) used to
credit any action row. Death Plateau's optional `talkToHarold3` read DRIVEN through
`goToHaroldStairs1.castleDoorOut`, a `pass_door`. Now (`Grader.row_reaches_npc`) the match counts
only a row whose detail names one of the step's npc symbols (an accepted press reports its target),
or whose own `t.exec` span presses that npc (`talk_to`/`click_npc`/`npc_op`, `attack`, `use_on`;
`emote` for an NpcEmoteStep). A pass_door, a walk, a varbit read, a leg checkpoint or a dialogue
page row named after the npc no longer drives a talk step; it is refused with `ledger row N 'X'
names <noun>, but its own action does not reach that npc`. Fixture:
`helper_coverage_two_op_test.py` `death_as_committed` / `death_row_reaches`. Moves at the time:
death `talkToHarold3` DRIVEN -> ALTERNATIVE (still FULL); anothersliceofham FULL -> TEST_GAP
(`talkToZanikRailway` was credited only by `zanik.at_dig`, a varbit read); eadgar, mm and seaslug
steps regraded with no verdict move. OPEN: the generic target-token loop at the end of
`Grader.driven` still credits by name (seaslug `travelWithHolgartFreeingKennith` via
`holgartsunkboat.drain_board`; death `goToHaroldStairs3` via an earlier visit's stairs row).

### A goto through the only gate, onto a solid tile, or off an island it swung onto; a use that did nothing (matthew-mbp-m4-b63-seam1)

Four holes the b63 fixers found, each closed in `helper_coverage.py`, each with a `Grader` switch
(False restores the old reading for the fixtures):

- **`gate_crossings`: "goes from ... to ...: with every door shut no walk joins them (margin 160),
  and every walk on foot opens <gate> (NEEDS-DOOR at margins 30/80/160) ... it skipped an only-way
  gate"** (`GATE_CROSSINGS`). The b59 sampler ruling as a map question (sampler-findings.md "Sample
  matthew-mbp-m4-b59" (a)): a gate that is the only way on foot between two regions is clicked on
  every crossing, however large they are. `MapWalls.only_way_gates` is reach.py's flood on the
  grader's own map reader: no walk with every closed door shut inside the hop's box widened by 160
  tiles, and the shortest walk with doors open crosses the SAME gate (or the other leaf within 2
  tiles) at every margin of 30, 80 and 160 that has a walk at all (a margin too small to go round
  says nothing). Charged unless a PASS row pressed that gate (its copy, or a local bound to it:
  `prince.unlock` uses the key on `alidoor_target`) in the 500 ticks before. This also covers a
  fenced yard bigger than `enclosure_entries`' 400 tiles: Dwarf Cannon's railing yard closes at
  1,325, and the committed run (69f7e7bd8 / OSRS-Content 22669be78) read FULL while it hopped in and
  out of it and on to Nulodion's workshop. The departure: the goto's own `from` stamp, else the
  reading before it when every row between kept the player on one side of every door (a talk, a
  page, a read, a walk, a press of a loc that is no door, climb or travel loc: `_side_kept`); a
  press's `walk_near: stepped off the target tile A (A -> B)` is read as B. The run's start is
  judged (owner ruling 2026-10-05: the first goto and the setup placement obey the door rule;
  matthew-mbp-m4-b64-seam1): a first goto with no `from` stamp leaves from the fixture's tile (its
  ini's `x`/`z`/`level`), or from the last setup placement's landing, in every rule; and the setup's
  net placement (a `::goto`/`::tele`, or a debugproc whose body reaches `p_teleport`/`p_telejump`
  through `@label`/`~proc` two levels down) is a ledger row of its own, `(setup placement) <cheat>`,
  from the fixture's tile to the run's first reading (else the tile the script names), charged to a
  step of that name. A BLOCKING loc a walk crosses by its own op (shapes 9-21 with Cross, Go-through,
  Squeeze, Jump...; never a ladder or stair: `MapWalls.is_crossing`) is a gate too: when no walk
  exists even with the doors open, the route may enter it (fewest crossings, then doors, then
  shortest) and names it -- the Wilderness Ditch (`ditch_wilderness_cover`, 3106,3521), the Shantay
  Pass (`shantay_pass_henge_doorway`, 3302,3116, with its op-less `inviswall`), the Barbarian agility
  pipe; the detail then says `crosses (by its own op) ... (NEEDS-OP ...)`. A `cross_trap` row
  pressing it in the 500 ticks before credits it like a gate press. Not judged: a hop to another
  level or map frame, a hop over 1,200 tiles on its longer axis, a row
  `enclosure_entries`/`enclosure_exits` already charged, a start whose landing is unknown (a setup
  `::tele <name>`, a debugproc whose teleport target is computed). A hop with NO route at margin 160
  is no longer skipped: it is flooded at 250/400/600 and charged as a gate or as "no on-foot route"
  (matthew-mbp-m4-b65-seam1, the section at the end of this file).
- **`solid_landings`: "lands at x,z,l on a solid tile (<loc> at ...): no walk ends there"**
  (`SOLID_LANDINGS`). A goto row (a `goto_tile`/`::goto` line, or goto_tile's bare `at x,z,l[ from
  ...]` detail; never a climb row named `goToFirstFloor` whose `at` is the stair's own tile) that
  lands on a tile the map blocks: an object's footprint (stairs, a ladder, a cave entrance, a
  crate, a diagonal railing, a table), a blocking ground decoration, or a blocked floor flag (a pier
  edge, water). Charged to a step of its own, `(goto onto <loc>)`, never to the guide step: the
  fault is the goto row's, and a landing on the staircase the next row climbs did not skip the
  climb. Every goto with a landing is judged, the first included.
- **A "use X on Y" step needs an effect** (`USE_NEEDS_EFFECT`). `use_item_driven` used to refuse a
  call only when it wrote a named row that never PASSed, so a bare `local r, d =
  t.player.use_on(...)` was DRIVEN from its source line (Haunted Mine's `useKeyOnValve`: the press
  timed out, the key stayed in the pack, no line was said). Now the call's row is its `t.exec`
  span, or a `t.check`/`t.expect` within 12 lines that reads one of its answers; with neither it is
  refused "line N is a bare use_on". The row (or a row named after it, `useX.var`, or a read row
  before the next press, walk or goto) must show an effect: an item lost or gained, a server line
  (`chat_message`: the driver answers `refused` on "Nothing interesting happens."), a `matched:`
  line, a var or `quest.stage` read, a page or interface, a landing or a tile check. A row that only
  says `map_flag` reads "shows no effect". `driven()`'s older `^use` fallback ("an action at line N
  uses X on the target") takes the same test (`_line_use_effect`).
- **`sealed_exits`' pocket press** (`POCKET_PRESS_SINCE_ARRIVAL`). Any press of a pocket's op loc
  in the 500 ticks before used to exempt the hop out, so Watchtower's rope used on
  `tree_ropeswing4_norope` (the way IN to Grew's island, `landed 2505,3087,0`) exempted a goto off
  the island. `_pocket_left_by_press` reads the rows in order and each detail in its own order: a
  press is a candidate; a reading outside the pocket after it keeps it (the press took the player
  out); a reading inside drops it unless the row says it opened something (`open leaf`, `opened`,
  `unlocked`). A press with no reading after it keeps the benefit (`asoh_sealed_op_pressed`).

What an author does: never goto onto a loc (stand on a walkable tile beside it and press it);
cross a members' gate, a yard gate or a fence gate with `t.player.cross_gate` / `pass_door` on
every crossing, or use a real teleport (`t.player.teleport_cast`) for a long trip; record every use
through `t.exec` (or a `t.check` of its answer) and read what it did on the next row.

Proof: `tools/quest_gate/helper_coverage_two_op_test.py` 19/19 (the five old cases plus 14:
`mcannon_rules_off` FULL with every switch off; `mcannon_committed` TEST_GAP with six
`gate_crossings` CHEATs and three `(goto onto ...)` steps; `mcannon_gate_pressed` /
`mcannon_gate_not_pressed`; `mcannon_off_the_crate`; five synthetic Haunted Mine use cases;
`hm_fullroute`, the b63 fixer's copy with its GUIDE-GAP marker removed, graded only while
build/orchestrator/fix_b63/ holds it; `wt_goto_off_island` CHEAT, `_off` FULL, `wt_swung_off_first`
FULL). Every other helper_coverage fixture file holds. `--calibrate` 12/39 -> 15/39. Re-grading the
108 green rows moved 47 (45 verdicts, plus mm and shadowstorm, already not FULL); the list with
each charged goto is build/seam_state/matthew-mbp-m4-b63-seam1/helper_coverage.movers.json. Six
movers' only charge is their first goto (death, druid, eadgar, murder, sleepinggiants, soulsbane):
held for the owner's first-goto ruling, then queued `todo` (the owner ruled 2026-10-05 that the
first goto obeys the door rule; matthew-mbp-m4-b64-seam1 also judges an UNSTAMPED first goto and the
setup placement, below).
The closer's FRESH runs of all 143 committed tests (a fresh ledger stamps the first goto's
departure) moved 52 queued-green tests: the grader's movers, plus doric, sheep, runemysteries and
scorpcatcher under the older `enclosure_entries` (HEAD's grader agrees on those ledgers), plus
itwatchtower (a content change). doric and sheep join the first-goto-only list (eight in all: death,
doric, druid, eadgar, murder, sheep, sleepinggiants, soulsbane); the other 44 were reopened naming
their gotos (seam-facts: Seam pass matthew-mbp-m4-b63-seam1 (f)).
Seam matthew-mbp-m4-b64-seam1 (`helper_coverage_judges_the_start_placement_and_crossing_locs`):
`START_JUDGED` (an unstamped first goto from the fixture's tile; the setup placement as a
`(setup placement) <cheat>` row) and `MapWalls.CROSSINGS` (a blocking crossing loc is a gate). Fixtures
in helper_coverage_two_op_test.py (27/27: `druid_first_goto` CHEAT on goto-talkToKaqemeex past
membergater 2935,3450, `_off` FULL; `atotc_sphinx` CHEAT naming shantay_pass_henge_doorway 3302,3116,
`_off` DRIVEN; `eta_placement` CHEAT naming ditch_wilderness_cover, `_off` absent; `eta_remedy` clean;
`eadgar_unchanged`) and helper_coverage_op_loc_reach_test.py (13/13: reach.py NEEDS-OP via the pass
and the ditch, goto_table's goto-sphinx row, only_way_gates on and off, reach.py's `--root`).
`--calibrate` 10/39 -> 9/39 (entertheabyss's audit FULL predates the ruling). Re-grading the 67 green
rows moved 4: currentaffairs and taleoftherighteous (their setup placement), entertheabyss (its
placement over the ditch), horror (goto-talkToGunnjorn into the Barbarian agility course past
agility_obstical_pipe_barbarian 2552,3559; the route from the repaired bridge's east end also names
two log balances and Lawgof's railing gate, because the bridge is a run-time loc).
OPEN: a `GUIDE-GAP:` marker on a use step is still accepted when the content has a use trigger on
the target (the b63 Haunted Mine copy declares "no [oplocu,hauntedmine_lift_valve]"; the
concurrent content seam adds one).

### "no on-foot route (UNREACHABLE at margin 600)", "the fewest-door walk on foot opens ... at margin N only"; a `goToX` crossing row is no goto (matthew-mbp-m4-b65-seam1)

Two holes the b65 fixers found in `gate_crossings` and `player_track`, closed in
`helper_coverage.py`, each with a switch (False restores the old reading for the fixtures):

- **A hop with no on-foot route charged nothing** (`Grader.NO_ROUTE_CHARGED`, `MapWalls.NO_ROUTE`,
  `MapWalls.hop_route`). `only_way_gates` returned `[]` both when a closed-doors walk exists and when
  no walk exists at 30/80/160 at all, so a goto onto an island, into Morytania past the Paterdomus
  trapdoor, or round a members' gate the margin-160 box could not hold was clean. Now a hop with no
  route at 160 (doors open, crossing locs enterable) is flooded again at `WIDE_MARGINS` 250, 400,
  600: a closed-doors walk there is clean; the FEWEST-door walk (door_route "cross": crossings, then
  doors, then length -- a shortest walk cuts through every house on the way) is charged naming its
  gate, "the fewest-door walk on foot opens membergater at 2933,3320,0 (NEEDS-DOOR at margin 250
  only: no route at all at 30/80/160)" (Making History's row 5 Lumbridge -> Jorral at 250; Chompy
  Bird's Lumbridge -> Rantz, Clock Tower's -> Kojo and Watchtower's -> the trellis at 400); none at 600
  is "no on-foot route (UNREACHABLE at margin 600)" (the Pandemonium, Lunar Isle, Mort'ton, Canifis,
  Lletya). Excused: a travel row between the departure and the goto (a cast or teleport, a climb, a
  `t.sail` verb that moves the hull or disembarks, a page naming a ferry, boat, cart or carpet, a row
  NAMED travel/ferry/boat/landed/arrived, or a row whose own detail reads a tile more than 30 from the
  departure -- The Fremennik Isles' ferry talk whose check says `tile 2311,3782,0 want ...`). Not
  charged as no-route: a hop whose departure or landing is in a room or pocket the enclosure rules
  judge (`enclosure()` closes within 400 tiles: `enclosure_entries`/`exits`, `sealed_entries`/`exits`
  own it), a hop off or onto a solid tile (`solid_landings` owns it), an end on a map square not on
  disk. Any older switch off (`CROSSINGS`, `START_JUDGED`, `POCKET_PRESS_SINCE_ARRIVAL`,
  `GOTO_BY_ACTION`) turns this rule off too, so their `*_off` fixtures keep meaning "the reading
  before that seam".
- **A diagonal door is a door** (`MapWalls.DIAGONAL_DOORS`). A closed door set diagonally across a
  wall's corner is an object (shape 9) that blocks its tile; the doors-open walk read it as a wall.
  The Wizards' Tower ladder room (`fai_wiztower_poor_door` at 3107,3162,0) was a 22-tile room with no
  way in; Imp Catcher's `goto-moveToTower` now reads "every walk on foot opens fai_wiztower_poor_door
  at 3109,3167,0, fai_wiztower_poor_door at 3107,3162,0" (Rune Mysteries `pass_door`s both).
- **The departure is read past a sail verb that moves nothing** (`SAIL_NO_MOVE_KEPT`): `_side_kept`
  read every `t.sail.` line as a side change, so Prying Times' `goto-thurgo` (after
  `deliverCargo`, a `t.sail.cargo_deliver`) had no departure. `state`, `tasks`, `task_board`,
  `task_accept`, `cargo_take`, `cargo_load`, `cargo_deliver` keep the player's side now.
- **A goto is what a row DID, never its name** (`GOTO_BY_ACTION`). `player_track` read any row whose
  step name holds `goto`/`goTo` as a goto, and the first `at x,z,l` in its detail as its landing: a
  `cross_gate` row named `goToHemenster.gateIn` "landed" on the gate's own tile, so its own press was
  not "before" it and Fishing Contest read CHEAT (the b65 fixer renamed the row to `enterHemenster` to
  get past it). Now a row is a goto when it is a setup placement or `_real_goto` (its source line calls
  `goto_tile`/`::goto`, or its detail is goto_tile's own `at x,z,l[ from a,b,c]`, its `::goto` retry's
  `... on attempt N of M via ::goto` included). Any other row contributes its END tile
  (`TRACK_END_READS`): `landed x,z,l` (climb, cross_gate), `-> at x,z,l` (pass_door's far side,
  walk_route's end), a cast's `at a,b,c -> x,z,l`, `reached x,z,l`, `; at x,z,l` (walk_to),
  `player at x,z,l`. Name a crossing row after the guide step; it no longer changes the grade.

Proof: `tools/quest_gate/helper_coverage_departure_cross_test.py` 19/19 (committed Lua/ledger pairs
read from git): Prying Times before b65 (753ee4a3c / 74e86ee0d4) gains CHEAT on the `::pryingtimes`
placement and on `goto-thurgo` (getKey), neither with `NO_ROUTE` off, goto-thurgo not with
`SAIL_NO_MOVE_KEPT` off; Making History before b65 (9a9d57559 / 776bd1593e) gains talkToJorral row 5
naming membergater 2933,3320 at margin 250 only; Watchtower 16ad11389 / c436ddab5a (FULL before)
reads CHEAT on goto-goUpTrellis; Fishing Contest with `enterHemenster` renamed `goToHemenster` reads
FULL (CHEAT with `GOTO_BY_ACTION` off); death keeps exactly its one charge, grandtree stays FULL; the
b65 greens of pryingtimes, makinghistory and itwatchtower stay FULL. Re-grading the 71 green rows
against their committed ledgers moved 10: chompybird, cog, elena, ikov, sheepherder (a members' gate
at 250/400), dreammentor, mortton, fenkenstrain (`::fenkenstrain`), mourningsendpartii (`::mend2`)
(no on-foot route), imp (the Wizards' Tower doors). arena, biohazard, mourningsendparti and seaslug
are not charged. `--calibrate` 10/39 -> 10/39. The closer's fresh runs of all 143 committed tests
(matthew-mbp-m4-b65-seam1 close) moved exactly those ten and no other queued green; all ten were
reopened. `helper_coverage_two_op_test`'s `wt_swung_off_first` (the same Watchtower pair) now wants
`leaveGrewIsland` DRIVEN instead of a FULL verdict.

### A step credited to a same-named npc, or to another copy of its loc (matthew-mbp-m4-b66-seam1)

Seam `helper_coverage_credits_a_step_to_a_same_named_npc_or_another_copy_of_its_loc`. Two ways a
step read DRIVEN off a row that did not do it:

- **A same-named npc.** `same_thing`'s display-name rule took any npc of the guide npc's display
  name. Scorpion Catcher's questscorpiona (Taverley), questscorpionb (the Barbarian Outpost) and
  questscorpionc (the monastery) are all "Kharid scorpion", so a run catching a, b, c credited
  catchMonasteryScorpion to the use on questscorpionb and catchOutpostScorpion to the use on
  questscorpiona. Now two npc or loc symbols that BOTH exist in this cache, differ, and are not in
  each other's `family()` (multinpc/multiloc parents) are different things (`distinct_things`),
  whatever their names share: the spelling-prefix rule no longer reads `gertrude` as
  `gertrudescat` or `mourning_temple_pillar_1_1` as `..._1_10` either. The display-name rule is the
  objs' alone (a guide item's rev 239 variants, `use_item_matches`). A use step is credited only by
  a `use_on` on the step's own npc or loc (`USE_ON_OWN_TARGET`).
- **Another copy of the loc.** The last resort credits a row whose detail merely NAMES the step's
  loc. Cog's climbWhiteLadder (ladder_from_cellar 2575,9655) read DRIVEN off `enterBasement-black`,
  which climbed ladder_cellar 2566,3242 and only says it landed "beside ladder_from_cellar
  2566,9642". Now, when the guide step has a WorldPoint, such a row credits it only if it worked the
  copy within 2 tiles (`row_off_point`, `POINT_SLACK`): the tile written right after the symbol, else
  any world tile in the detail (four-digit x; a screen pixel pair is not one), else the copy nearest
  where the player last stood (`player_track`, within 15 tiles). A row with no tile and no copy in
  reach is not judged. The refusal reads "ledger row N 'x' names <loc>, but names it at <tile>, not
  the copy within 2 tiles of the guide's x,z,l". A plain stair or ladder the run took at another copy
  then grades TRAVEL (merged into the step it leads to), not DRIVEN.

What a test does about it: catch, talk to or use on the npc the guide step names (its own symbol),
and drive each guide step in the guide's item order. Scorpion Catcher's catchOutpostScorpion wants
the cage holding the Taverley AND monastery scorpions (scorpioncageac): a run that catches the
outpost scorpion second (cage a) reads UNMATCHED there until it visits the monastery first.

Proof: `tools/quest_gate/helper_coverage_use_item_test.py` 16/16: `scorp_same_name` (the b66
fixer's three catches, run3's ledger details), `scorp_named_row_elsewhere`, `cog_white_ladder_other_copy`
(test/quests/cog.lua at 5e3675055, its ledger at OSRS-Content 6762eaf875), four `same_thing` rows,
and `rules_off`: every case fails with `DISTINCT_SYMBOLS` / `USE_ON_OWN_TARGET` / `ROW_AT_POINT` off
(each flag alone fails its own case). The b66 scorpcatcher fixer's file on run3's ledger reads
catchMonasteryScorpion line 349 and catchOutpostScorpion UNMATCHED (was 299 and 234, FULL).
Re-grading the 68 green rows against their published ledgers moved no verdict: cog climbWhiteLadder,
grail goUpStairsBrokenCastle, misc_astrid goUpstairsToAstrid and runemysteries
goF1ToF0LumbridgeCastle went DRIVEN -> TRAVEL (each was credited to another copy of a stair).
`--calibrate` 11/39 -> 12/39 (scorpcatcher now agrees with the audit's TEST_GAP).

### A step below the block graded CHEAT; a setup give charged for a shared word; a goto charged on its near side (matthew-mbp-m4-b67-seam1)

Seam `helper_coverage_credits_by_a_shared_word_and_charges_steps_below_a_block_by_proximity`. Three
rules in `tools/quest_gate/helper_coverage.py`, each behind a `Grader` switch:

- **Below the block (`BELOW_BLOCK`).** On a ledger with a BLOCKED row, a step whose own lines (rows
  named after it, and action lines naming its targets) all sit below the `t.blocked` call the run
  stopped at grades UNMATCHED `below the block at line N: the run stopped there, and the step's own
  lines ... never ran`. Such a step is never graded CHEAT, CONTENT_GAP or DRIVEN off a line that
  never ran. The block line is found by the `t.blocked` reason's literal prefix (or the only call).
  Rum Deal's useBucketOnTap and Witch's House's enterGate/useCheeseOnHole read CHEAT before, off
  lines past the block.
- **A setup give charges only its own step (`CHEAT_BY_OWN_ITEM`).** A setup `::give` charges a step
  only for the step's own item: an obj target, its family, an ItemStep's requirement. A prose-only
  step still matches by word, minus the quest's words and its dominant symbol prefix (`deal` in
  `deal_slayer_gloves` vs `deal_brewvat_tap`), and never charges the step's INPUT item (the bucket a
  "Fill a bucket" step fills). A bring-along given as one of its guide alternates is a bring-along.
- **A goto past a gate is charged on its far side only (`GOTO_FAR_SIDE`).** "lands past the <gate>
  the guide names" now fires only for a goto landing in the loc's own map frame and level (across
  it, for a climb) whose ledger departure does NOT reach the landing with every door shut
  (goto_table's `REACH closed-doors`, margins 30/100/250). A goto with no ledger row is judged on its
  landing alone; one whose line never ran is not charged.

Still by design: on a FULL run, an action line naming the step's target is fallback DRIVEN evidence
even with no row of the step's own; on a BLOCKED run it can credit a step from a line ABOVE the
block (ball's returnToBoy off the opening talkToBoy). Neither run is green. Known quirk, not fixed:
`Test.action_lines` binds a table field `{ loc = "x" }` to the identifier `loc`, which then matches
the string literal `"loc"` in `t.player.by_symbol("loc", ...)`.

Proof: `helper_coverage_use_item_test.py` 29/29 (6 b67 cases on the real guides and walls, 4 rule-off
cases reproducing the old CHEAT readings, 3 real-fixture cases). `--all-green`: 77/77 FULL before and
after, no step class moved; `--calibrate` 11/39 before and after, row by row.
