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

### A setup `::give` of The Giant Dwarf's Consortium ores and bars is a brought item (sonnet-b42)

*Origin: the sonnet-b42 giantdwarf reviewer left "Consortium ore/bar tasks staged with ::give:
rule (c) risk, unjudged"; the sampler judged it from the guide.*

TheGiantDwarf.java lists the points-game deliveries as items, not as gathering steps: "Various
ores and bars" (`oresBars`, `canBeObtainedDuringQuest`, line 115) and ten each of copper, tin,
iron, coal, silver, gold and mithril ore and bronze, iron, silver, gold, steel and mithril bar
(lines 134-146). No guide step mines or smelts them, so a setup `::give` of them stages a brought
item and is not trap 16. The deliveries themselves are the quest's work: every hand-in to the
Consortium must be a driven `talk_to`/`use_on` row that moves the points varp.
