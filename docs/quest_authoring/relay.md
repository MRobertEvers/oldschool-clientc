# Long quests: the ladder, legs and fail.py

A quest with more than 30 guide steps runs an author out of context if it works the way a short
quest does. Measured on Dragon Slayer: a test run costs about 2 KB, but re-reading a 28 KB test
file, a 15 KB guide and 10-15 KB script files many times is what fills the window. Two tools
replace those reads.

## The ladder

`python3 tools/quest_gate/ladder.py <test_id>` prints the guide as a table: one row per guide step,
in the order `helper_coverage.py` grades them (its step count is the same number). You never need
to open the guide Java.

Each step shows:

- `leg.n` -- the leg it belongs to and its number in the guide's order.
- `s<stage>` -- the quest stage the guide files it under (the key of `steps.put(<stage>, ...)`).
- `step` -- the Java field name. Name your test rows after it (`talkToOziach`,
  `goto-talkToOziach`, `talkToOziach-dialog`): the coverage grade finds rows by this name.
- `kind` -- `NpcStep`, `ObjectStep`, `ItemStep`, `DetailedQuestStep`, ... A leaf that came out of a
  panel `ConditionalStep` is written `NpcStep<parentName>`.
- `target` -- `npc:oziach` is the guide's `NpcID.OZIACH`; `loc:` is `ObjectID`, `obj:` is `ItemID`.
  The part after the colon is the content symbol. A trailing `?` means the content has no such
  symbol: look it up (gaps-world) before you press it.
- `(x,y,z)` -- the guide's `WorldPoint`.
- the instruction text (cut at 160 characters), `subs` (the `addSubSteps` children folded into the
  step), `items` (its item requirements by display name), `dialog` (the `addDialogSteps` options).
- `trig` -- the content trigger for the target, `path.rs2:line trigger`, relative to
  `OSRS-Content/osrs239-content/server/scripts/`. The quest's own directory comes first, then
  shared scripts (an npc's area file, `ladders_stairs`), another quest's last; op1 before other ops;
  `+N` says how many more there are. This is the grep you no longer run.

`--json` gives the same data for a program. `--write` writes
`docs/quests/ladders/<test_id>.ladder.tsv` (a `#` header line with the leg cuts, then one TSV row
per step; `=` in `items` means the same items as the row above).

## Legs

A leg is a run of about ten consecutive guide steps: small enough to author, run and fix without
losing the thread. `ladder.py` cuts the steps evenly into `round(steps / 10)` legs, then moves each
cut to the nearest quest-stage boundary (the stage value changes between two steps) within three
steps of it; with no boundary that close, the cut stays where it fell. A quest of 30 steps or fewer
is one leg. `--legs N` asks for legs of about N steps and cuts even a short quest.

The overview prints each leg's step range, stage range, and first and last step. Many guides file
most of the quest under one stage (Dragon Slayer: stage 2 from the Oracle to boarding the ship), so a
leg often starts and ends in the same stage; that is the guide, not an error.

## Working one leg

`python3 tools/quest_gate/ladder.py <test_id> --leg K` prints only leg K, after three lines:

```
quest: dragon (quest_dragon, guide .../DragonSlayer.java) -- leg 2 of 4, steps 12-22 of 44
starts at stage: 2 (enterMelzarsMaze)
previous leg ended with: optionsForLozarPiece
```

This is what a relay author is handed. The previous leg's last step is where the committed test
file already stops: find its row with `grep -n 'optionsForLozarPiece' test/quests/<id>.lua` and
read twenty lines around it, then write this leg's rows after it.

## After a run: fail.py

`python3 tools/quest_gate/fail.py <test_id>` prints, in at most 3,000 characters: the SUMMARY row,
the three rows before the first non-PASS row, that row with its full detail (a detail over 900
characters keeps its head and tail), the row after it, the row's screenshot path, and the last
three `script error` / `STALE SCRIPT PACK` / `assert` lines of the run's log.

- `--all` lists every non-PASS row, one line each (at most 40).
- `--context N` shows N rows before instead of three.
- `--leg K` keeps only rows named after leg K's steps (`goto-` / `walk-` prefixes and `.x` / `-x`
  suffixes count).
- `--name <run>` reads `build/quest_gate/<run>/ledger.tsv` (a `--name`d run).

Exit 0 when every row passed and the run wrote a PASS SUMMARY, 1 when it did not (a failing row, or
a run that stopped before its SUMMARY), 2 when there is no ledger. `run.py ... && fail.py ...`
therefore stops on a red run only after fail.py has printed it.

## Output discipline

Every byte a command prints stays in your context for the rest of the quest. So:

- Never `cat` the whole test file. Locate with `grep -n '<row name>' test/quests/<id>.lua` and read
  twenty lines around the hit (`sed -n 'A,Bp'`).
- Never open a ledger directly. Use `fail.py` (and `fail.py --all` for the list).
- Never open the guide Java. Use the ladder; `--leg K` for the leg you are on.
- Read a script file by the line range the ladder's `trig` column gives (`sed -n 'L,+40p'`), not
  whole.
- Pipe any exploratory command (a `grep -rn` over the content, a `helper_coverage.py` run, a debug
  dump) through `| head -c 4000`.
