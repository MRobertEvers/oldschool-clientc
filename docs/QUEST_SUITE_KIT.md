# Quest suite kit -- the spec the phase 1-3 workers build to

Companion to `QUEST_DRIVER_DESIGN.md` (fixed decisions) and
`QUEST_DRIVER_REMAINING.md` (history). This is the spec for the kit that lets
a small model write one client-driven test per quest. Full plan (tiers, the
Haiku loop, gates): the Claude Doc "Quest Suite Plan: Haiku-Scale Quest Tests",
2026-09-19. Inventory: `tools/quest_gate/quest_inventory.tsv` (179 quests).
Server cheat map: `docs/QUEST_SERVER_CHEATS.md`.

## Working rules for every worker

- Work ONLY in this checkout (`3draster-quest-driver`, branch
  `lane-quest-driver`). Never build in, cd into, or touch
  `/Users/matthewevers/Documents/git_repos/3draster` -- that is the owner's
  live checkout.
- A C change is built into a PRIVATE objdir and target, never
  `build_questtest`/`src/torirs_questtest` (other workers run on it):
  `make -C src OPT=1 EMBED_SERVER=1 PLATFORM_OBJ_BASE=build_qd_<you> PLATFORM_TARGET=torirs_qd_<you> torirs_qd_<you>`
  then drive quests with `QUEST_BINARY=src/torirs_qd_<you> python3 tools/quest_gate/run.py --no-build ...`.
- Lua/Python-only changes run with `python3 tools/quest_gate/run.py <quest> --no-build`.
- After ANY OSRS-Content edit: `make -C src torirsserver-scripts` (run.py does
  it too).
- Never `git stash`. Never commit `saves/`, `build*/`, `cache*`,
  `preferences.ini`, `plugin_prefs.ini`, `manifests/.*.ini`.
- Mutation checks ONLY in a throwaway worktree (CLAUDE.md), never here.
- The gate is behaviour: a change is done when a ledger row from a real run
  says so, not when it compiles.
- Do not edit files another worker owns (ownership map below). If you need a
  change there, write it in your report as `needs: <file>: <what>`.
- Only `core.lua` may declare chunk-scope `local`s; other driver files hang
  helpers off `QD`. The driver is ONE chunk (torirs_plugin_drive.c
  DRIVE_SCRIPT_PARTS); no `require`, no `pcall`, no `coroutine`, no
  `setmetatable`.
- Every new verb gets a conformance row (`test/quests/_conformance.lua`) --
  written by the CONFORMANCE CLOSER, not by the verb's author, to avoid edit
  collisions on that file. Verb authors verify with a scratch script via
  `run.py --script <file> --name <label> --no-build`.

## Result vocabulary (fixed)

`ok timeout not_found refused covered no_row not_visible closed unsupported`.
Ledger verdicts: `PASS`, `FAIL`, and (new) `BLOCKED`.

## Phase 1 -- server: one cheat path, setvar, kill, BLOCKED (C; Opus)

Owner files: `src/torirsserver/torirs_server_world.c`,
`src/torirsserver/torirs_server_world.h` (or wherever the export lives),
`src/plugin/torirs_plugin_drive.c`, `tools/quest_gate/gate.py` ONLY for the
BLOCKED verdict counting, a new `test/quests/_cheats.lua` + `make test-quest-cheats`.

1. Factor `handle_cheat`'s strncmp/sscanf ladder body into
   `ToriRSServer_RunCheatLadder(srv, player, text)` returning the same
   RAN/FAILED/NONE verdict the debugproc path returns. `handle_cheat` calls
   content first (unchanged), then the ladder (unchanged behaviour).
2. `ToriRSServer_RunCheatForTest(srv, text)`: content-first-then-ladder,
   exactly like `handle_cheat`. `DriveCore_Cheat` calls it instead of
   `RunDebugprocForTest`. `t.cheat` mapping stays RAN->ok, FAILED->refused,
   NONE->no_row. The driver's setup loop (run.py's wrapper) must turn a
   `no_row` from a setup cheat into a FAIL row named `setup.<cheat text>` and
   stop the run -- a setup that silently did nothing is the bug this exists
   to kill.
3. `::setvar <varp|varbit> <int|^constant>`: resolve the name via
   `ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, name)` then the varbit
   pack; `^name` via the constant pack. Write through the same setter the
   `%var =` opcode uses (transmit + listeners). Unknown name -> FAILED with a
   message naming it. Add `cheat_varp_from_name` beside `cheat_obj_from_name`.
4. `::kill <npc_symbol> [radius]`: lethal damage to the nearest matching npc
   through the normal death path so `npc_death_step` reaches CORPSE and the
   npc's `[ai_queue3,...]` fires. Prints `Killed <name>`. No match -> FAILED.
5. Confirm `::give` lands in the backpack and `::setlevel` is permanent when
   reached through `t.cheat`.
6. BLOCKED verdict: `drive_ledger_write` counts `BLOCKED` separately
   (`SUMMARY ... pass=N fail=M blocked=K`); SUMMARY verdict is PASS when
   fail==0. stderr mirror prints `QUEST <id> BLOCKED <step> ...`. `gate.py`
   accepts the new SUMMARY shape and lists blocked rows separately (a quest
   with fail==0 and blocked>0 is reported `blocked`, exit 0 only if
   `--allow-blocked`; default exit non-zero so CI sees it).
7. `test/quests/_cheats.lua` (underscore = not a quest): one row per ladder
   command reached through `t.cheat`: `::give egg` then `inv.count("egg")==1`;
   `::setlevel cooking 40` then `skill("cooking").level`; `::setvar cookquest
   ^cook_started` then `var.server("cookquest")==1`; `::kill man` then the
   npc row gone within 5 ticks; `::tele varrock` then tile changed; a bogus
   `::nosuchcheat` answering `no_row`. `make test-quest-cheats` runs it and
   exits non-zero on any FAIL. Verified only when its ledger is all PASS.
   Also add an `unknown setup cheat fails the run` check: a scratch quest with
   `setup = {"::nosuchcheat"}` must produce a FAIL row and exit non-zero.

## Phase 2 -- driver kit (Lua; Sonnet x3 + Opus x2 + closer)

Ownership: 2a `core.lua` + new `quest.lua` (add to DRIVE_SCRIPT_PARTS in
torirs_plugin_drive.c -- 2a may edit ONLY that array); 2b `chat.lua`,
`read.lua`; 2c `state.lua`, `world.lua`, `ui.lua` (npc/world/var/inv/skill
helpers; NOT `ui.journal_*`); 2d (Opus) `pointer.lua`; 2e (Opus)
`QD.ui.journal_open/read/close` in `ui.lua` (append-only section) + the
revconfig role + any C reader in `torirs_plugin_drive_ui.c`. Closer:
`_conformance.lua`, `verb_list.py` if needed, `test/quests/README.md`.

| Verb | Owner | Spec |
| --- | --- | --- |
| `t.exec(name, verb, ...)` -> verb's own returns | 2a | Calls `verb(...)`; if `verb` is not a function or the first arg is a nil target, writes FAIL `name` with detail `bad verb/target`. Writes the ledger row `name` PASS iff result=="ok" (hollow rule: an `ok` whose detail is nil where the verb documents one is graded `hollow` and FAIL). Takes screenshot `name` after the call; on non-ok also `name-FAIL`. A repeated `name` in one run appends `-2`, `-3`. |
| `t.check(name, condition_or_result, detail)` | 2a | Assertion that is not a verb; PASS if `true` or `"ok"`. Same screenshot rule. |
| `t.blocked(reason)` | 2a | Writes verdict `BLOCKED` row `blocked` with the reason, screenshots `blocked`, then `t.finish(0)`. |
| `t.cheat` awaits its reply | 2a | After dispatch, `msg.await` any new chat line for <=5 ticks (every ladder branch and debugproc prints one) so effects are visible before the next read. Result unchanged. |
| `quest.bind{varp=, constants={name=value}, row=, display=, points=}` | 2a | Called by the generated skeleton header. `quest.stage()` -> (ok, value); `quest.expect_stage(name_or_value)` -> refused on client/server mismatch, naming the side; `quest.expect_complete()` -> four rows: `quest.varp_complete` (client==server==complete const), `quest.scroll_title` (display name), `quest.points` (delta == points, read `%qp` before at bind time), `quest.journal` (via `ui.journal_open`, title matches display, first line contains `QUEST COMPLETE`, then close) -- if `ui.journal_open` is absent, that row is `unsupported`, not skipped. `quest.expect_complete` never calls `::complete`. |
| `chat.play(list)` | 2b | List entries: `"npc:<substr>"`, `"player:<substr>"`, `"mesbox:<substr>"`, `"options"`, `"choose:<exact row or /lua pattern/>"`, `"*"` (any one page), `"count:<n>"`, `"name:<text>"`, `"end"` (expect no dialogue). Walks pages with `continue_`, screenshots each page as `<step>-pN`, fails on the first mismatch with the page kind and text in the detail. Returns (ok) or (mismatch, detail). |
| `chat.choose` pattern form | 2b | Third form: a string `/.../` is a Lua pattern matched against row text. |
| `scroll.reward_xp(skill)` -> (ok, xp) | 2b | Parses `(%d+)%s+<Skill> XP` from `scroll.rewards().lines`. |
| `npc.await_present(sym, radius, ticks)`, `npc.await_gone(...)` | 2c | Over `t.await` + `npc.nearest`. |
| `var.await_server(name, value, ticks)` | 2c | Like `var.await` on `var.server`. |
| `inv.await_all({sym=count,...}, ticks)` | 2c | One await; detail lists what is short. |
| `skill.snapshot()` -> table; `skill.expect_gain(name, xp, snapshot)` | 2c | Reads every stat once; accepts xp or xp*10, names the unit matched. `t.skill` is a TABLE: `t.skill.read(name)` is the reading the old `t.skill(name)` gave, plus `QD.skill.snapshot`/`QD.skill.expect_gain` exposed as `t.skill.snapshot`, `t.skill.expect_gain` (verb_list must see all three). |
| `player.teleport(name)` | 2d | `::tele <name>` then await `world.tile` to change (<=5 ticks). |
| `player.talk_to` re-talk fix | 2d | `_settle_after_click` treats a chat page whose kind OR text differs from the pre-click page as a fresh mount. Prove: remove the `talk_to_and_settle` shim from `cooks_assistant.lua` and the quest stays green; then in a THROWAWAY worktree delete the `sub_mounted` stamp and `cooks_assistant.handin_talk` must time out. Report both results verbatim. |
| `ui.journal_open()` -> (ok, {title, first_line}); `ui.journal_close()` | 2e | Open via the quest list row's op 2 for the bound quest (or the questjournal debugproc if one exists -- prefer the click). Needs revconfig roles for `questjournal:title` and `qj1`. Must be verified on the completed Cook's Assistant. |

Hollow rule: every `t.exec` row whose verb returned `ok` and a nil/empty detail
where the verb's banner documents a detail is FAIL `hollow`.

## Phase 3 -- authoring kit (Python + docs; Sonnet, Opus review)

Ownership: 3a `tools/quest_gate/run.py` (failure block, TIMEOUT.png,
empty-suite non-zero), `gate.py` (minimum shape, login/creator fingerprint,
BLOCKED handling beyond phase 1's counting, empty discovery non-zero),
new `lint_quest.py`; 3b new `new_quest.py` (extends
`tools/questhelper_extract.py`), `test/quests/QUEUE.tsv`; 3c
`docs/QUEST_AUTHORING.md`, `test/quests/README.md` pointer; 3d regenerate
`cooks_assistant.lua` and `hans.lua` through the scaffold.

- Failure block: after the report, for each non-green quest print the last
  FAIL/BLOCKED row's name, detail, its `-FAIL.png` path, and the last five
  chat lines from `client.log` (the stderr mirror lines `QUEST ...` and
  content `mes` lines).
- `TIMEOUT.png`: on wall-clock timeout, if the driver's last shot exists copy
  it as `TIMEOUT.png`.
- `gate.py` minimum shape: >=8 rows; >=1 `quest.expect_stage` or `quest.*`
  row; `quest.varp_complete` present OR (tier 4) a trailing BLOCKED row; >=4
  distinct PNGs; one PNG per `t.exec` row (rows whose `shots` column is empty
  and whose name is not `setup.*` fail); `run.py --all`/`gate.py` with zero
  discovered quests exit non-zero.
- `lint_quest.py <file>`: refuses numeric ids where a symbol belongs
  (`talk_to(123)`), `::complete <own row>` in setup, `t.step(..., "PASS"`
  literals, `-- CHECK` markers, duplicate `t.exec` names, symbols not in the
  compack (`OSRS-Content/osrs239-content/configs/all.*.compack`).
- `new_quest.py <helper_dir>`: see the plan's section 5 table. Emits
  `test/quests/<quest>.lua` with header (helper path, varp, constants table,
  tier from the inventory), `quest.bind{...}`, `setup` from requirements
  (`::give`, `::setlevel`, `::complete <prereq row>`, the quest's own reset
  cheat), one `t.exec` per linear step in Quest Helper order with the
  description as a comment and `-- CHECK` where guessed (op numbers, chat
  rows, routes), `quest.expect_stage` between `steps.put` boundaries,
  `::skipboss` stubs (as `t.blocked("skipboss not landed")` until phase 4)
  where the manifest lists a fight, `quest.expect_complete()`, `finish(0)`.
  Output must pass `lint_quest.py` with `--allow-check`.
- `QUEUE.tsv`: `quest_dir helper_dir tier status owner last_failure` for all
  179 (+9 RFD subquests), status `todo`, from the inventory.
- `QUEST_AUTHORING.md` <= 300 lines: test shape (one generated example),
  verb table (one line each), result words, ten traps, run command,
  definition of done. Replace README.md's bad example with a pointer.
- 3d: regenerate both quests, make them green with NO local helper functions
  (no `talk_to_and_settle`), publish, and have Opus review the diff and the
  shots.

LANDED 2026-09-19: `new_quest.py` is keyed by `<test_id>` (a QUEUE.tsv row,
not a helper dir -- the old form is `--all --helper <dir>`); QUEUE.tsv is
eight columns (`quest_dir test_id helper_dir helper_file tier status owner
last_failure`), 188 rows, with `quest_recipefordisaster` replaced in place by
`rfd_intro` + its nine subquests; an item Quest Helper marks
`canBeObtainedDuringQuest()` is no longer a setup `::give` but a `-- CHECK
gather` marker at the first step whose own args need it; and a `boss_npcs`
fight stub is emitted AT the fight step (with the unreachable tail
commented out), not at the end of the file. `tools/quest_gate/queue.py`
(`next --tier N [--claim OWNER]`, `set`, `show`, `summary`) is the queue's
front door; every write is atomic. Author-facing docs: `QUEST_AUTHORING.md`
section 6, `test/quests/README.md`.

LANDED 2026-09-19 (the pilot pass): the first four tier-1 quests went 0/4,
every one of them dying at its first `talk_to` with `screen_position` /
`not_visible` -- the fixture stands the player in Lumbridge and nothing
moved him. Fixed in four places, each proved by a ledger row:
`::goto <x> <z> [level]` on the engine ladder (NOT `::tele`, which content's
`[debugproc,tele]` claims and answers `nowhere called 2951` to);
`t.player.goto_tile(x, z, level=0)` over it, which returns only once the
tile AND the npc pool around it are visible (the pool lands a tick later);
`new_quest.py` spending every step's Quest Helper WorldPoint, emitting a
`goto-<step>` row before the first step and before any step more than 12
tiles or a plane away from the last (a `gotos` column in `--all`); and a
`t.finish`/`t.blocked` that ENDS THE RUN -- `drive_ledger_write` refuses a
row once finished (one stderr line, `quest-driver: row after finish
ignored: <name>`) and core.lua parks the script at its next row, shot or
await, so "reported blocked while the file ran on to expect_complete" is no
longer possible. The verb is `goto_tile`, never `goto`: `goto` is a reserved
word in this tree's Lua (3rd/lua/llex.c) and does not parse.

## Phase 5 run book rules (owner, 2026-09-20)

- Authors are Sonnet by default (`author_model` in the loop's args); four Haiku
  batches landed 4 of 24, the first Sonnet batch 5 of 8 on the same quests.
- **If a Haiku author's context compacts during the authoring step, that
  quest switches to Sonnet 5 at medium effort.** The author card tells Haiku to
  stop and report `compacted=true` the moment it sees a summary in place of its
  earlier messages; the loop (`tools/quest_gate/haiku_loop.workflow.js`,
  `ESCALATE_MODEL`/`ESCALATE_EFFORT`) then re-runs the same card with Sonnet,
  which resumes the file Haiku left. An author that returns no report at all is
  treated as compacted.
- The reviewer is always Sonnet; the sampler is always Opus; neither changes
  with the author model.
- **Every batch publishes a contact-sheet artifact.** After the sampler
  pushes, the orchestrator runs
  `.venv/bin/python tools/quest_gate/batch_sheet/build_sheet.py . build/batch_sheet/<batch> <the batch's test ids>`
  then `render_page.py build/batch_sheet/<batch> <batch> "Quest Batch <batch>"`,
  publishes `build/batch_sheet/<batch>/index.html` with every `*.webp` in that
  directory as supporting files (one artifact per batch, title "Quest Batch
  <batch>", favicon the map), and appends a row to `test/quests/BATCHES.tsv`
  (batch, date, author model, tests, green/blocked/content_bug/rejected counts,
  artifact link). The sheets read `build/quest_gate/<id>/` -- the reviewers'
  last run -- so build them before the next batch overwrites those directories.
  Rejected quests are included: their screenshots are the evidence for the
  rejection.
- **Only the automation plugin loads.** `run.py` and `conformance.py` set
  `TORIRS_PLUGIN_ONLY=lua` so the native registry registers only the Lua
  host, whose manifest (`script/plugins/quest_driver.ini`) names only the
  quest driver. No item-stats, xp/loot trackers, minimap orbs, tile
  indicator or NXT plugins run in a quest test: the screenshots show the
  engine's own frame, and no plugin hook sits between the driver and the
  client. A quest test that needs another plugin's behaviour is testing that
  plugin, and belongs in its own harness.
- **Every batch ends with the seam pass.** After the sampler pushes, run
  `tools/quest_gate/seam_fixer.workflow.js` (content inline, no args): an Opus
  triage groups the blocked and content_bug rows by seam, one Opus agent fixes
  each DRIVER seam with a live reproduction and proof, and its closer adds the
  conformance rows, runs the gates, commits, pushes, and reopens the freed rows
  with `RETRY after <sha>`. The next author batch then resumes those files.
  Content, engine and design seams are listed in its report for the content
  queue; they are not fixed by the loop. This is how quests become unblocked --
  never by hand between batches.
- **Never overlap the seam pass with an author batch.** Authors load
  `script/plugins/quest_driver/*.lua` at run time; a seam agent's half-written
  function crashed Between a Rock's author mid-quest on 2026-09-20
  (`attempt to call a nil value (field 'inv_arm')`). The order is strict:
  batch, its sampler push, its contact sheet, THEN the seam pass, THEN the next
  batch. A batch rejected by such a crash is reopened with a RETRY note, not
  counted against the author.
