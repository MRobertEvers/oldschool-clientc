# b58 round 2: finish one quest test after the seam pass

You are an Opus FIXER finishing ONE quest test in
/Users/matthewevers/Documents/git_repos/3draster (branch matthew-mbp-m4-b58 in the
parent and in the OSRS-Content submodule). Your quest test id is in the message that
sent you here (`<id>` below). Six other fixers work on other quests at the same time:
touch ONLY `test/quests/<id>.lua`.

Read first, in this order:
1. `build/orchestrator/fix_b58/BRIEF.md` -- the door rule, the goto audit, what earlier
   fixers learned, how to run, the hard rules. ALL of it still applies (you commit
   nothing; no content, driver, tool or src edits; no `make` in this checkout; command
   output under 4 KB; no scratch under /tmp).
2. Your quest's section below.
3. `python3 tools/quest_gate/queue.py show <id>` (the row's RETRY note) and the round-1
   fixer's notebook `build/orchestrator/fix_b58/<id>.progress.md` (legends has none).
4. What the seam pass changed: docs/quest_authoring/seam-facts.md, the section for
   seam pass matthew-mbp-m4-b58-seam1 (items a-i), and its fixers' notebooks under
   `build/seam_state/matthew-mbp-m4-b58-seam1/` (fix.<key>.progress.md) for your seam.

Start from the COMMITTED `test/quests/<id>.lua` on this branch (the round-1 fixer's
file, reviewed and committed). Change what your section names, keep the rest, rerun the
full goto audit on anything you add, and prove it with a full run:

    python3 tools/quest_gate/run.py <id> --no-build --no-publish > build/orchestrator/fix_b58/<id>.r2.runN.log 2>&1
    python3 tools/quest_gate/gate.py <id>
    python3 tools/quest_gate/lint_quest.py test/quests/<id>.lua
    python3 tools/quest_gate/helper_coverage.py <id> | tail -4      # FULL

Notebook: `build/orchestrator/fix_b58/<id>.r2.progress.md` (append after every run; if
it exists, read it and continue). The file ends green to the completion scroll, with no
`t.blocked` left in it. If a leg still cannot be driven, stop at an honest `t.blocked`
naming the file:line of the content at fault and report it.

## haunted (Ernest the Chicken)

The seam made the nine basement maze gates pressable from the maze side (a content fix
in quest_haunted.rs2; the map is untouched). The committed file now runs through
(the seam closer saw 127 rows, 0 FAIL, nothing blocked). Remove the now-dead blocked
branch (`gate_from_walled_side` and its `t.blocked`), make every maze crossing a graded
row (the gate pressed from the side the maze reaches it, the tile before and after),
confirm the lever sequence is the guide's order, and that the way back out (ladder,
lever, doors, stairs, lab door) and the hand-in are rows that can fail. The oil can was
missing at the hand-in in an earlier probe: check it is really picked up.

## eadgar (Eadgar's Ruse)

The seam ported LostCity's walk-through for the storeroom door (both directions). The
committed file now runs through (the seam closer saw 280 rows, 0 FAIL). Remove the
conditional `t.blocked` at the door. The crate-room crossing past the patrolling guards
was proven only by a probe before: prove it in full runs under at least three account
names (`run.py --script test/quests/eadgar.lua --name <n> --no-build --no-publish`),
because guard timing differs per run, and report how often the player was caught and
retried. Leaving the storeroom, the knockout, the ejection, Sanfew's hand-in and the
rewards (11000 Herblore, 1 quest point) must be graded rows.

## betweenarock (Between a Rock...)

The seam fixed the ferry-cave landing (now on the bank, 2838,10124), ported the
Jump-through flame walls, and gave Keldagrim real exits (the boatman both ways, the
mine carts, the station platform). The committed file now FAILS at talkToDondakan:
Dondakan takes 2-3 ticks to appear after the ferry landing. Add
`t.npc.await_present("dwarfrock_dondakan", 15, 10)` (check the verb's real signature)
before each talk to or use on Dondakan. Replace the three gotos that depart from a
Keldagrim street tile (goto-roladHut, goto-trollTunnelWithCannonball,
goto-whiteWolfStairs) with Keldagrim's real exits: the seam's worked copy is
`build/seam_state/matthew-mbp-m4-b58-seam1/kelda/betweenarock.exitcopy.lua`. Jump
through the flame walls by click and run the Avatar fight (the margin row is written
but was never reached: lowest hp at least a quarter of max AND food left). Known and
NOT yours to fix (report only): trollromance_stronghold_exit_tunnel still lands on
2781,10160, a rock tile (LostCity and maplink give 2773,10162) -- if your route hits
it, stop at a t.blocked naming it.

## entertheabyss (Enter the Abyss)

The seam ported the Wilderness Ditch's Cross op
(areas/area_wilderness/scripts/wilderness_ditch.rs2: a three-tile jump; entering the
Wilderness shows the pack's warning). Replace the goto from the Wilderness start tile
(3106,3557) to Varrock with: a walk to the ditch, the Cross click, a row that checks the
landing tile on the south side, then an overland goto from that open tile to the Varrock
street. Keep every other row.

## blackarmgang (Shield of Arrav, Black Arm Gang)

The seam fixed phoenixdoor2 (the weapon-store door): from inside a plain press lets you
out; from the street it is refused without the key. Leave the weapon store by a plain
press (no key use on the way out). Also assert the refusal from the street without the
key, if the route allows it: the test holds the key from the partner hand-off onwards,
so the only honest place is a press BEFORE the key is obtained, if the route passes the
door then. If it does not, do not contrive it: leave that check out and say so. Keep
every other row.

## biohazard

The seam changed the CLIENT's pick: an npc whose model draws no visible face (Chancy =
gambler2, Da Vinci = artist2) now picks by its box, so a real `talk_to` reaches them.
Delete both `t.drive.op` fallbacks and their "covered" retry rows; use plain talk rows.
The shared binary `src/torirs_questtest` does NOT have the C change (it was deliberately
not rebuilt: this checkout's src/ carries another session's uncommitted edits). Build a
PRIVATE binary from the committed tree and run with it:
- `git worktree add --detach build/orchestrator/worktrees/b58-bin HEAD` (parent), give
  it the content and cache it needs the way the seam's cramped-room fixer did (read
  `build/seam_state/matthew-mbp-m4-b58-seam1/fix.npc_press_answers_covered_in_a_cramped_room.progress.md`
  for the exact recipe: symlinks, PLATFORM_OBJ_BASE / PLATFORM_TARGET, the make target);
- build there, never in the main checkout; then run in the main checkout with
  `QUEST_BINARY=<that binary> python3 tools/quest_gate/run.py biohazard --no-build --no-publish`;
- leave the worktree and the binary in place and report the binary's path (the review
  round needs it); unlink nothing another agent may be using.
If the private build fails, report exactly where and leave the test unchanged.

## legends (Legends' Quest)

No round-1 fixer; the committed file is green (560 rows) with its prayer plan hardened.
ONE step is wrong: the guide's `talkToUngaduluForForce` uses the DARK dagger
(deathdagger) on ungadulu_good; the committed run (about line 874, ledger row 216) used
the glowing dagger (deathdaggerdone). `python3 tools/quest_gate/helper_coverage.py legends`
shows the UNMATCHED step and its reason. Read ungadulu.rs2's branch for each dagger and
the guide's order (ladder.py legends): drive the guide's branch with the dark dagger at
the point the guide has it, with a row that asserts the item used and the effect. If at
that point of the quest the player can only hold the glowing dagger (the dark dagger
becomes the glowing one earlier), show that from the scripts and the guide, keep the
run's order, and mark the step with the grader's `-- ANY-OF:` or `-- GUIDE-GAP:` marker
with the evidence (docs/quest_authoring/coverage-and-gate.md explains the markers).
This run is long (over 5,000 ticks): redirect and poll. Touch nothing else in the file:
its prayer and food rows were proven under two rule sets on 2026-10-04.
