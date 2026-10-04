# Brief for a door-rule fixer (one quest test each)

You are an Opus FIXER re-driving ONE committed quest test in
/Users/matthewevers/Documents/git_repos/3draster (the batch branch named in the message that sent you
here, in the parent and in the OSRS-Content submodule). Your quest test id is given in the message
that sent you here; call it `<id>` below. Seven other fixers do the same for other
quests at the same time: touch ONLY `test/quests/<id>.lua` (and, if you remove baseline
rows, `tools/quest_gate/mid_run_gives_baseline.tsv` -- report that edit).

The test was green until the coverage grader learned to read the map's walls. It is
reopened because at least one `goto_tile` teleports the player into or out of a closed
space. Fix the named rows AND audit every goto so none is left, prove it green, and
leave the file UNCOMMITTED for the batch's review round.

## The rule (docs/QUEST_ORCHESTRATOR.md standing rules, owner 2026-10-03)

A `goto_tile` into OR out of ANY closed space is a cheat, plain one-click doors
included. A goto departs only from an open, walkable tile outside and lands only on
one. Every door, gate, counter, stair, ladder, trapdoor, railing, fence, portal or npc
"Leave" option between the player and the target is clicked on every visit, going in
and coming out, including the room a setup cheat stands the player in. A plain overland
hop between open tiles, or between two open tiles of one dungeon passage, is travel and
stays.

## What counts as a closed space (learned in b56-b58)

- A room, house, shop back, fenced yard or pen, a walled city quarter behind a guarded
  gate, the space behind a bar counter.
- Another floor of a building, a cellar or dungeon reached by a ladder, stair or
  trapdoor.
- A cave or mine entered by a climb, a crawl, a rope or a cut-through, and a region
  such as the Kharazi Jungle that is entered by cutting through its edge.
- A mountain top, plateau or island the player cannot leave on foot without a climb
  (the Trollheim summit). Flood the walkable map from the departure tile with
  `comp.py`: if the component never reaches the landing, the hop crosses something.
- A gate that is the ONLY way on foot between two regions is clicked on every
  crossing, however large the regions are (Karamja's members' gate between Musa Point
  and Brimhaven: sampler ruling, b59, docs/quest_authoring/sampler-findings.md "Sample
  matthew-mbp-m4-b59" (a)). Check with `reach.py` at margins 30, 80 and 160: a route
  that exists only through the gate means the gate is pressed.
- NOT a closed space: a gate with another on-foot way round it between the two tiles,
  and a building whose doorway the map leaves open with no door loc.

Repeated trips count every time. If the test walks into a place once and hops in or out
of it later, each later hop is a fault. For a long trip a player would not walk, use
what a player uses: a REAL teleport. Stage the magic level and the runes in SETUP, cast
the spell from the spellbook by click (`t.player.cast`), and grade it with three rows:
the cast answered TELEPORTED, the exact runes were consumed, the landing tile. Check
`skill_magic/scripts/spells/teleport.rs2` for which spells this pack implements and what
each refuses (Ardougne Teleport needs the scroll read; there are no standard teleport
tablets). Then an overland goto between open tiles is travel. Where no teleport reaches
(Trollheim before Eadgar's Ruse is done), factor the walked route into one helper and
call it on every trip.

## Start

- The committed `test/quests/<id>.lua`.
- The reopen note with the charged steps, ledger rows, landings and obstacles:
  `build/orchestrator/fix_<batch>/<id>.finding.txt` (read it first).
- `python3 tools/quest_gate/helper_coverage.py <id>` prints every CHEAT row and why.
- `python3 tools/quest_gate/ladder.py <id>` is the guide (never read the Java).
- Read docs/QUEST_AUTHORING.md (the core) once; look a failing row up through
  docs/quest_authoring/INDEX.md. Patterns proven in the last two batches:
  docs/quest_authoring/start-and-travel.md ("The grader now catches the goto inside")
  and docs/quest_authoring/sampler-findings.md (sections "Sample matthew-mbp-m4-b56"
  and "Sample matthew-mbp-m4-b57").
- Worked examples of a re-driven test, on this branch: test/quests/priest.lua and
  test/quests/cooks_assistant.lua are the OLD files here (their re-driven versions are
  in an open PR), so read instead the fixers' notebooks:
  build/orchestrator/fix_b57/priest.progress.md, cog.progress.md,
  blackknight.progress.md, and the finished files build/orchestrator/fix_b57/*.final.lua.

## The audit (do not skip it)

In the last batches tests were sent back round after round for "one more goto". List
EVERY `goto_tile` (grep -n; some are comments, some sit in loops or helpers). For each,
write in your notebook: row name, departure tile and level (the previous row's landing,
or the ledger's departure stamp after a run), landing tile and level, and the verdict of
the static walkability tools in test/quests/orchestrator/matthew-mbp-m4/reports/sample_tools/
(`reach.py`, `goto_table.py`, `locs_near.py`; read their usage; doors closed; raise the
margin before believing UNREACHABLE; they do not know stair or ladder links, counters,
or script-spawned locs). Fix every row whose departure or landing needs a door, changes
level or map frame where a stair, ladder or trapdoor exists, lands on a solid tile, or
lands in a pocket with no way out.

## Things earlier fixers learned the hard way

- **Door helper.** Use one `pass_door`-style helper: walk to the near side and check
  the tile; click the CLOSED leaf on the exact door tile, or, if the door stands open,
  assert the OPEN leaf's loc (an opened door's loc shifts one tile and may keep its
  symbol or take an `_open` one) with a check that can fail; walk through; check the
  far tile. Never press an already-open door.
- **Stacked floors.** `t.world.loc_near` searches every level and returns the first
  copy. A door or ladder with a copy on another floor at the same tile gets the wrong
  copy clicked. Count only a copy on the player's own level (press by tile AND level).
- **Climbs.** A ladder or stair click can answer before the climb lands: wait for the
  level to change, then check the landing tile. Where a ladder has no maplink row the
  climb is plus/minus one level on the tile you stand on.
- **Pushed crossings** (a walk-through door, a gate that moves you) can answer
  `timeout settle_after_click` although the crossing worked: grade the row on the tiles
  before and after, with the click's answer in the detail.
- **Message boxes swallow clicks** (a wilderness warning, a first-time gate dialogue):
  read and dismiss them.
- **A full pack silently drops gives and purchases**: count free slots at the step.
- **Random draws.** `run.py --name X` changes the account only together with
  `--script test/quests/<id>.lua`. If the test draws anything at random, run it under
  several account names that way and audit every branch in the file, drawn or not.
- **"Use ITEM on TARGET" guide steps.** The grader credits such a step when any row
  names the target. Check each one by hand: the row must use the item the guide names,
  and assert the item left the pack and the effect happened.

## Also fix while you are there

- Any `t.check(name, true, ...)` or check whose condition cannot fail (the status of a
  read, `x ~= nil`, an accepted `timeout`); any detail that prints a table address.
- Kill targets must be real. Before you trust a fight, check the npc the guide has the
  player kill has a combat block (hitpoints, attack, defence ... in a `.npc` file, not
  only anims in `npc_anims.generated.npc`) and, for a boss, an entry in
  `docs/bosses/quest_combat_manifest.json`. An npc with no block spawns with engine
  defaults and dies in one hit: that is a content bug, not a pass. Stop at an honest
  `t.blocked` before the fight, keep the fight rows below it, and report it (Between a
  Rock...'s Arzinian Avatar, b58).
- Fights: every real fight gets a margin row -- lowest hp at least a quarter of the
  player's maximum hitpoints AND food left (never OR, never a fallback value). Stage
  levels and the guide's recommended food and gear in SETUP only. Do not raise a level
  just to satisfy the margin on a trivial fight. Leave existing `::passive` setup lines
  alone.
- Two engine rules are about to change and the test must hold under both: prayer will
  NOT regenerate over time (stage and drink prayer potions, switch protection off when
  it is not needed, assert prayer points before a protected fight), and an eat will no
  longer hold a queued hit (eat earlier, carry margin).
- No `::give`, `::setlevel` or `::setvar` after setup.
  `python3 tools/quest_gate/lint_quest.py --mid-run-gives test/quests/<id>.lua` lists
  baselined mid-run gives: move each to setup, drive the item, or mark it
  `-- lint: kit-give <reason>` and delete its row from the baseline file.
- Reward rows assert the literal documented amounts.

## Running

Foreground only, one run at a time, logs to files:

    python3 tools/quest_gate/run.py <id> --no-build --no-publish > build/orchestrator/fix_<batch>/<id>.runN.log 2>&1
    python3 tools/quest_gate/gate.py <id>
    python3 tools/quest_gate/lint_quest.py test/quests/<id>.lua
    python3 tools/quest_gate/helper_coverage.py <id> | tail -4      # must be FULL
    python3 tools/quest_gate/fail.py <id>                           # to read a failure

Open `-FAIL.png` shots with the Read tool. If walking the whole quest outruns the frame
budget, set `max_frames` (ceiling 480000). Notebook:
`build/orchestrator/fix_<batch>/<id>.progress.md`, appended after every run (if it exists,
read it first and continue).

## Hard rules

- No content, driver, tool or src edits; no `make`.
- Never `git stash`, `reset`, `checkout -- <path>`, `clean`, `add`, `commit` or `push`:
  you commit NOTHING.
- Every shell command's output under about 4 KB (logs to files, read slices with
  tail/grep/cut). No scratch files under /tmp. Never screenshot the desktop.
- If the quest cannot be made green without a content or driver change (a door with no
  op, an npc unreachable behind a counter, a ladder with no destination, a monster that
  never attacks), stop that leg at an honest `t.blocked` row naming it, leave the file
  as the best honest version, and report exactly what is needed with file:line evidence.
  Settle a content question from LostCity first where LostCity has the quest
  (/Users/matthewevers/Documents/git_repos/LostCity_Server/content/scripts/quests/ and
  /Users/matthewevers/Documents/git_repos/LostCity_Content2/scripts/quests/).

## Return (concise)

The goto table (row, departure -> landing, verdict before, what you changed); the other
rows fixed; each "use ITEM on TARGET" guide step and the row that uses that item; the
final run (rows / FAIL, gate, lint, coverage, fight margins); the extra account runs
and the branches they drew; baseline rows removed; anything you could not settle.
