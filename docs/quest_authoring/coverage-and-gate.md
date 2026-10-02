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
  `goToTyrasCampEntrance`'s `enterTyrasCamp`.
- A start in no zone of X. Open ground may reach the landing some way the guide does not name.
- A start on D's island. The rule above judges those hops.

In the committed greens, 4 guides have such a route (grandtree, losttribe, regicide, tearsofguthix),
and no green step changes class.

#### The departure tile (gap (b), not closed)

**Proved, HELD BACK (matthew-mbp-m4-b50-seam1).** The driver change below works: `goto_tile` reads
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
so landing it means re-authoring those ten; that is the owner's call, not a seam closer's. The
proved diff is kept in the owner's checkout as
`build/seam_state/matthew-mbp-m4-b50-seam1/goto_departure_stamp.held.patch` (one `QD.world.tile()`
read at the top of `QD.player.goto_tile`, two detail formats); the suite's verdicts are
`close_gate_after.txt` beside it.

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
